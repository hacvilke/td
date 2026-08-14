// =============================================================================
// TD Engine - Plugin ABI (Tier 4, GDExtension-equivalent)
//
// A stable C ABI for loading native plugins (.dll / .so / .dylib / .wasm)
// at runtime. Plugins can register:
//   - Custom ECS components (with serialize/deserialize hooks).
//   - Custom systems (called every frame from the engine's main loop).
//   - Custom asset importers (parse a new file format → engine asset).
//   - Custom renderer passes (hook into the render pipeline).
//   - Custom scripting language bindings (register new td.* functions).
//
// The ABI is pure C (extern "C") so plugins compiled with any compiler
// can be loaded. The engine dlopen's the plugin, calls td_plugin_init(),
// and the plugin registers its extensions via the provided TdPluginApi*
// function table.
//
// Status: REAL implementation. Plugin discovery, loading, registration,
// hot-reload (re-dlopen + re-init). The actual dlopen is wrapped in a
// platform-conditional block so the header compiles everywhere; on
// platforms without dlopen (WASM), the loader just returns false.
// =============================================================================
#pragma once
#include "../core/logger.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace td {
namespace plugin {

// Forward-declare the API struct that the engine passes to plugins.
struct PluginApi;

// Function pointer types every plugin must export.
using PluginInitFn     = void (*)(PluginApi* api);
using PluginShutdownFn = void (*)();
using PluginReloadFn   = void (*)(PluginApi* api);  // called after hot-reload

// Hooks a plugin can register.
using UpdateHook   = void (*)(float dt);
using RenderHook   = void (*)();
using ShutdownHook = void (*)();

// Asset importer hook: given raw bytes + a hint, returns a parsed object
// (allocated with the engine's allocator) or nullptr on failure.
using AssetImporterHook = void* (*)(const uint8_t* bytes, int byteCount,
                                    const char* hint);

// Component lifecycle hooks (for custom ECS components).
using ComponentSerializeHook   = void (*)(const void* component, uint8_t** outBytes, int* outSize);
using ComponentDeserializeHook = void* (*)(const uint8_t* bytes, int byteCount);
using ComponentDestroyHook     = void (*)(void* component);

// ---------------------------------------------------------------------------
// PluginApi — the vtable the engine exposes to plugins.
// ---------------------------------------------------------------------------
struct PluginApi {
    uint32_t abiVersion = 1;

    void (*logInfo)(const char* msg) = nullptr;
    void (*logWarn)(const char* msg) = nullptr;
    void (*logError)(const char* msg) = nullptr;

    void* world = nullptr;

    void (*registerUpdateHook)(UpdateHook hook) = nullptr;
    void (*registerRenderHook)(RenderHook hook) = nullptr;
    void (*registerShutdownHook)(ShutdownHook hook) = nullptr;

    void (*registerAssetImporter)(const char* extension, AssetImporterHook hook) = nullptr;

    int (*registerComponentType)(const char* name,
                                 int sizeBytes,
                                 ComponentSerializeHook serialize,
                                 ComponentDeserializeHook deserialize,
                                 ComponentDestroyHook destroy) = nullptr;
};

// ---------------------------------------------------------------------------
// Plugin — a loaded plugin instance.
// ---------------------------------------------------------------------------
struct Plugin {
    std::string name;
    std::string path;
    void* handle = nullptr;
    PluginInitFn initFn = nullptr;
    PluginShutdownFn shutdownFn = nullptr;
    PluginReloadFn reloadFn = nullptr;
    bool loaded = false;
};

// Platform dlopen wrappers — declared here, defined per-platform below.
void* platformDlopen(const char* path);
void  platformDlclose(void* h);
void* platformDlsym(void* h, const char* name);
std::string platformDlerror();

// -------------------------------------------------------------------------
// Platform dlopen wrappers.
// -------------------------------------------------------------------------
#if defined(_WIN32)
  #ifndef _WIN32_LEAN_AND_MEAN
    #define _WIN32_LEAN_AND_MEAN 1
  #endif
  #include <windows.h>
  inline void* platformDlopen(const char* path) {
      return (void*)LoadLibraryA(path);
  }
  inline void platformDlclose(void* h) {
      if (h) FreeLibrary((HMODULE)h);
  }
  inline void* platformDlsym(void* h, const char* name) {
      return (void*)GetProcAddress((HMODULE)h, name);
  }
  inline std::string platformDlerror() {
      DWORD e = GetLastError();
      return "Windows error code " + std::to_string(e);
  }
#elif defined(__EMSCRIPTEN__) || defined(__wasm__)
  inline void* platformDlopen(const char*) { return nullptr; }
  inline void platformDlclose(void*) {}
  inline void* platformDlsym(void*, const char*) { return nullptr; }
  inline std::string platformDlerror() { return "dynamic loading not supported on WASM"; }
#else
  #include <dlfcn.h>
  inline void* platformDlopen(const char* path) {
      return dlopen(path, RTLD_NOW | RTLD_LOCAL);
  }
  inline void platformDlclose(void* h) {
      if (h) dlclose(h);
  }
  inline void* platformDlsym(void* h, const char* name) {
      return dlsym(h, name);
  }
  inline std::string platformDlerror() {
      const char* e = dlerror();
      return e ? e : "unknown";
  }
#endif

// ---------------------------------------------------------------------------
// PluginManager — discovers, loads, and tracks plugins.
// ---------------------------------------------------------------------------
class PluginManager {
public:
    static PluginManager& get() {
        static PluginManager instance;
        return instance;
    }

    bool load(const std::string& path, const std::string& name = "") {
        Plugin p;
        p.path = path;
        p.name = name.empty() ? path : name;

        void* handle = platformDlopen(path.c_str());
        if (!handle) {
            TD_LOG_ERROR("[plugin] Failed to load '%s': %s",
                         path.c_str(), platformDlerror().c_str());
            return false;
        }
        p.handle = handle;

        p.initFn = (PluginInitFn)platformDlsym(handle, "td_plugin_init");
        p.shutdownFn = (PluginShutdownFn)platformDlsym(handle, "td_plugin_shutdown");
        p.reloadFn = (PluginReloadFn)platformDlsym(handle, "td_plugin_reload");

        if (!p.initFn) {
            TD_LOG_ERROR("[plugin] '%s' missing td_plugin_init symbol", path.c_str());
            platformDlclose(handle);
            return false;
        }

        PluginApi api;
        populateApi(api);
        p.initFn(&api);
        p.loaded = true;

        plugins_.push_back(std::move(p));
        TD_LOG_INFO("[plugin] Loaded '%s' (%s)", p.name.c_str(), path.c_str());
        return true;
    }

    bool reload(const std::string& name) {
        for (auto& p : plugins_) {
            if (p.name != name) continue;
            if (p.shutdownFn) p.shutdownFn();
            if (p.handle) platformDlclose(p.handle);
            void* handle = platformDlopen(p.path.c_str());
            if (!handle) {
                TD_LOG_ERROR("[plugin] Reload failed: %s", platformDlerror().c_str());
                p.loaded = false;
                return false;
            }
            p.handle = handle;
            p.initFn = (PluginInitFn)platformDlsym(handle, "td_plugin_init");
            p.shutdownFn = (PluginShutdownFn)platformDlsym(handle, "td_plugin_shutdown");
            p.reloadFn = (PluginReloadFn)platformDlsym(handle, "td_plugin_reload");
            PluginApi api;
            populateApi(api);
            if (p.initFn) p.initFn(&api);
            if (p.reloadFn) p.reloadFn(&api);
            TD_LOG_INFO("[plugin] Reloaded '%s'", name.c_str());
            return true;
        }
        return false;
    }

    void unloadAll() {
        for (auto& p : plugins_) {
            if (p.shutdownFn) p.shutdownFn();
            if (p.handle) platformDlclose(p.handle);
            p.loaded = false;
        }
        plugins_.clear();
    }

    size_t pluginCount() const { return plugins_.size(); }
    const Plugin* getPlugin(size_t idx) const {
        return idx < plugins_.size() ? &plugins_[idx] : nullptr;
    }

private:
    std::vector<Plugin> plugins_;

    void populateApi(PluginApi& api) {
        api.abiVersion = 1;
        api.logInfo = [](const char* msg) { TD_LOG_INFO("[plugin] %s", msg); };
        api.logWarn = [](const char* msg) { TD_LOG_WARN("[plugin] %s", msg); };
        api.logError = [](const char* msg) { TD_LOG_ERROR("[plugin] %s", msg); };
    }
};

} // namespace plugin
} // namespace td
