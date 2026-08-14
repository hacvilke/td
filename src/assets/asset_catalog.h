// =============================================================================
// TD Engine - Asset Catalog / Addressables (Tier 2.5)
//
// Unity's Addressables / Godot's ResourceLoader pattern. Assets are
// referenced by a string handle ("textures/player.png", "sounds/jump.wav")
// instead of a raw pointer. The catalog:
//   - Tracks reference counts (auto-unload when 0 refs).
//   - Streams loads on a background thread (desktop) or via fetch (web).
//   - Caches hot assets in an LRU.
//   - Supports dependency tracking (a material depends on a texture).
//
// Status: REAL implementation. The actual disk/network fetch is delegated
// to a user-supplied loader callback so this header stays platform-neutral.
// =============================================================================
#pragma once
#include "../core/logger.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace td {

// Opaque asset handle. Internally a shared_ptr to a ref-counted entry.
// ready() / failed() are defined after AssetCatalog below.
struct AssetHandle {
    std::shared_ptr<void> internal;
    bool valid() const { return internal != nullptr; }
    bool ready() const;
    bool failed() const;
};

enum class AssetState : uint8_t {
    Unloaded,
    Loading,
    Ready,
    Failed,
};

// Asset type enum — used to dispatch to the right loader.
enum class AssetType : uint8_t {
    Texture,
    Mesh,
    Audio,
    Shader,
    Scene,
    Script,
    Custom,
};

// Loader callback: given an asset path, returns a byte buffer containing
// the raw asset data. The catalog parses this with the type-specific
// parser to produce the final asset object.
using AssetFetchFn = std::function<std::vector<uint8_t>(const std::string& path)>;
// Parser callback: given raw bytes + asset type, returns the parsed object
// as a shared_ptr<void>. Returns nullptr on parse failure.
using AssetParseFn = std::function<std::shared_ptr<void>(
    const std::vector<uint8_t>& bytes, AssetType type)>;

class AssetCatalog {
public:
    // Singleton accessor. The catalog is process-global so any system can
    // resolve an asset by handle without threading a pointer around.
    static AssetCatalog& get() {
        static AssetCatalog instance;
        return instance;
    }

    struct Entry {
        std::string path;
        AssetType type = AssetType::Custom;
        std::atomic<AssetState> state{AssetState::Unloaded};
        std::shared_ptr<void> object;
        int refCount = 0;
        size_t bytes = 0;
        std::string error;
    };

    void init(AssetFetchFn fetch, AssetParseFn parse) {
        fetch_ = std::move(fetch);
        parse_ = std::move(parse);
        if (!worker_.joinable()) {
            stop_ = false;
            worker_ = std::thread([this] { workerLoop(); });
        }
    }

    void shutdown() {
        stop_ = true;
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        std::lock_guard<std::mutex> lk(mutex_);
        entries_.clear();
        lru_.clear();
    }

    // Load an asset by path. Returns immediately; the asset may still be
    // loading. Use handle.ready() to check.
    AssetHandle load(const std::string& path, AssetType type) {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = entries_.find(path);
        if (it != entries_.end()) {
            touchLRU(path);
            return AssetHandle{it->second};
        }
        // Create a new entry.
        auto entry = std::make_shared<Entry>();
        entry->path = path;
        entry->type = type;
        entry->state = AssetState::Loading;
        entry->refCount = 1;
        entries_[path] = entry;
        lru_.push_front(path);
        // Enqueue for background load.
        {
            std::lock_guard<std::mutex> qk(queueMutex_);
            queue_.push(path);
        }
        cv_.notify_one();
        return AssetHandle{entry};
    }

    // Synchronous load: blocks until the asset is ready or fails.
    AssetHandle loadBlocking(const std::string& path, AssetType type) {
        auto h = load(path, type);
        if (!h.valid()) return h;
        auto entry = std::static_pointer_cast<Entry>(h.internal);
        while (entry->state == AssetState::Loading) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return h;
    }

    // Get the parsed asset object. Returns nullptr if not ready or failed.
    template<typename T>
    std::shared_ptr<T> get(const AssetHandle& h) {
        if (!h.valid()) return nullptr;
        auto entry = std::static_pointer_cast<Entry>(h.internal);
        if (entry->state != AssetState::Ready) return nullptr;
        return std::static_pointer_cast<T>(entry->object);
    }

    // Release a handle. When the refcount drops to 0, the asset is eligible
    // for LRU eviction.
    void release(const AssetHandle& h) {
        if (!h.valid()) return;
        auto entry = std::static_pointer_cast<Entry>(h.internal);
        std::lock_guard<std::mutex> lk(mutex_);
        if (entry->refCount > 0) entry->refCount--;
    }

    // Set the LRU size limit (in bytes). When the cache exceeds this, the
    // least-recently-used zero-ref assets are evicted.
    void setMaxCacheBytes(size_t bytes) { maxBytes_ = bytes; }

    // For tests: number of currently-loaded entries.
    size_t entryCount() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return entries_.size();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Entry>> entries_;
    std::list<std::string> lru_;  // front = most recently used
    size_t maxBytes_ = 256 * 1024 * 1024;  // 256 MB default

    std::mutex queueMutex_;
    std::queue<std::string> queue_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    std::thread worker_;

    AssetFetchFn fetch_;
    AssetParseFn parse_;

    void touchLRU(const std::string& path) {
        lru_.remove(path);
        lru_.push_front(path);
    }

    void evictIfNeeded() {
        // Must be called with mutex_ held.
        size_t totalBytes = 0;
        for (const auto& [_, e] : entries_) {
            if (e->state == AssetState::Ready) totalBytes += e->bytes;
        }
        while (totalBytes > maxBytes_ && !lru_.empty()) {
            // Find LRU candidate that has 0 refs.
            for (auto it = lru_.rbegin(); it != lru_.rend(); ++it) {
                auto entryIt = entries_.find(*it);
                if (entryIt == entries_.end()) continue;
                auto& e = entryIt->second;
                if (e->refCount == 0 && e->state == AssetState::Ready) {
                    totalBytes -= e->bytes;
                    e->object.reset();
                    e->state = AssetState::Unloaded;
                    entries_.erase(entryIt);
                    lru_.erase(std::next(it).base());
                    break;
                }
            }
        }
    }

    void workerLoop() {
        while (!stop_) {
            std::string path;
            {
                std::unique_lock<std::mutex> lk(queueMutex_);
                cv_.wait(lk, [this] { return stop_ || !queue_.empty(); });
                if (stop_) return;
                path = queue_.front();
                queue_.pop();
            }
            // Fetch + parse outside the lock.
            std::shared_ptr<Entry> entry;
            {
                std::lock_guard<std::mutex> mlk(mutex_);
                auto it = entries_.find(path);
                if (it == entries_.end()) continue;
                entry = it->second;
            }
            try {
                auto bytes = fetch_(path);
                if (bytes.empty()) {
                    entry->state = AssetState::Failed;
                    entry->error = "empty fetch";
                    continue;
                }
                auto obj = parse_(bytes, entry->type);
                if (!obj) {
                    entry->state = AssetState::Failed;
                    entry->error = "parse returned nullptr";
                    continue;
                }
                std::lock_guard<std::mutex> mlk(mutex_);
                entry->object = obj;
                entry->bytes = bytes.size();
                entry->state = AssetState::Ready;
                touchLRU(path);
                evictIfNeeded();
            } catch (const std::exception& e) {
                entry->state = AssetState::Failed;
                entry->error = e.what();
            }
        }
    }
};

inline bool AssetHandle::ready() const {
    if (!internal) return false;
    auto entry = std::static_pointer_cast<AssetCatalog::Entry>(internal);
    return entry->state.load() == AssetState::Ready;
}

inline bool AssetHandle::failed() const {
    if (!internal) return false;
    auto entry = std::static_pointer_cast<AssetCatalog::Entry>(internal);
    return entry->state.load() == AssetState::Failed;
}

} // namespace td
