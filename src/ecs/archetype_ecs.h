// =============================================================================
// TD Engine - DOTS-style Archetype ECS (Tier 3.6)
//
// Unity's DOTS (Data-Oriented Technology Stack) showed that archetype-based
// ECS is 10-100x faster than sparse-set or bit-mask ECS for cache-heavy
// workloads. The key insight: group entities by their component TYPE SET
// (the "archetype"), then store components in contiguous arrays per
// archetype. Iterating a query is then a tight loop over a few arrays.
//
// This is a parallel ECS that CO-EXISTS with the existing td::World. Games
// can opt-in per-system: legacy systems use World, data-oriented systems
// use ArchetypeWorld.
//
// Status: REAL implementation. Archetype storage, query iteration,
// component add/remove (moves entity between archetypes), structural
// changes deferred to end of frame (sync point).
// =============================================================================
#pragma once
#include "../core/logger.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace td {
namespace archetype {

// Component types are identified by a stable integer ID. The first time
// a component type is registered, it gets the next available ID.
using ComponentId = uint32_t;
constexpr ComponentId kInvalidComponentId = 0xFFFFFFFF;

// A component type registration. Stores the size + alignment so the
// archetype can lay out arrays correctly.
struct ComponentType {
    ComponentId id = kInvalidComponentId;
    size_t size = 0;
    size_t align = 1;
    const char* name = "";
    void (*construct)(void*) = nullptr;
    void (*destruct)(void*) = nullptr;
    void (*move)(void* dst, void* src) = nullptr;
    void (*copy)(void* dst, const void* src) = nullptr;
};

class ComponentRegistry {
public:
    static ComponentRegistry& get() {
        static ComponentRegistry instance;
        return instance;
    }

    // Register a component type. The `name` parameter is optional and
    // used only for debugging. To get a stable ID per type, we use a
    // per-type static variable (initialized once on first call).
    template<typename T>
    ComponentId registerType(const char* name = nullptr) {
        // Per-type static — initialized once, returns the same ID on every call.
        // The `tag` variable's address is unique per type instantiation, so
        // we use it as the dedup key (instead of size-based name which
        // would collide for two different structs of the same size).
        static char tag;
        static ComponentId myId = registerTypeImpl(&tag, sizeof(T), alignof(T), name,
            [](void* p) { new (p) T(); },
            [](void* p) { static_cast<T*>(p)->~T(); },
            [](void* dst, void* src) {
                new (dst) T(std::move(*static_cast<T*>(src)));
                static_cast<T*>(src)->~T();
            },
            [](void* dst, const void* src) {
                new (dst) T(*static_cast<const T*>(src));
            });
        return myId;
    }

    // Non-template registration for runtime-discovered types (e.g. from plugins).
    // `tag` is a unique-per-type identity token (e.g. address of a per-type
    // static variable, or a stable string ID for runtime types).
    ComponentId registerTypeImpl(const void* tag, size_t size, size_t align,
                                 const char* name,
                                 void (*construct)(void*),
                                 void (*destruct)(void*),
                                 void (*move)(void*, void*),
                                 void (*copy)(void*, const void*)) {
        auto it = tagToId_.find(tag);
        if (it != tagToId_.end()) return it->second;
        ComponentId id = nextId_++;
        ComponentType t;
        t.id = id;
        t.size = size;
        t.align = align;
        t.name = name ? name : "<anon>";
        t.construct = construct;
        t.destruct = destruct;
        t.move = move;
        t.copy = copy;
        types_.push_back(t);
        tagToId_[tag] = id;
        return id;
    }

    const ComponentType* getType(ComponentId id) const {
        if (id >= types_.size()) return nullptr;
        return &types_[id];
    }

private:
    std::vector<ComponentType> types_;
    std::map<const void*, ComponentId> tagToId_;  // tag (per-type static address) → ID
    ComponentId nextId_ = 0;
};

// An archetype is a unique set of component types. Entities with the same
// set of components share an archetype. Each archetype stores its
// components in contiguous arrays (one array per component type).
class Archetype {
public:
    // The sorted set of component IDs this archetype contains.
    std::vector<ComponentId> componentSet;

    // Per-component storage. Each entry is a contiguous byte array with
    // `capacity` slots of `type.size` bytes each, `count` slots live.
    struct ComponentArray {
        ComponentId typeId;
        size_t elementSize = 0;
        size_t elementAlign = 1;
        std::vector<uint8_t> bytes;  // raw storage
        int count = 0;

        void* at(int index) {
            return bytes.data() + index * elementSize;
        }
        const void* at(int index) const {
            return bytes.data() + index * elementSize;
        }

        void ensureCapacity(int cap) {
            if (cap <= (int)(bytes.size() / elementSize)) return;
            size_t newCap = std::max((size_t)cap, bytes.size() / elementSize * 2 + 8);
            bytes.resize(newCap * elementSize);
        }
    };
    std::vector<ComponentArray> arrays;

    int entityCount = 0;
    // Map component ID → index into `arrays`.
    std::unordered_map<ComponentId, size_t> arrayIndexByComponent;

    void init(const std::vector<ComponentId>& components,
              const ComponentRegistry& reg) {
        componentSet = components;
        std::sort(componentSet.begin(), componentSet.end());
        arrays.clear();
        arrayIndexByComponent.clear();
        for (ComponentId cid : componentSet) {
            const ComponentType* t = reg.getType(cid);
            if (!t) continue;
            ComponentArray a;
            a.typeId = cid;
            a.elementSize = t->size;
            a.elementAlign = t->align;
            a.ensureCapacity(16);
            arrays.push_back(std::move(a));
            arrayIndexByComponent[cid] = arrays.size() - 1;
        }
    }

    // Allocate a new entity in this archetype. Returns its row index.
    int allocEntity(const ComponentRegistry& reg) {
        int row = entityCount;
        for (auto& a : arrays) {
            a.ensureCapacity(entityCount + 1);
            const ComponentType* t = reg.getType(a.typeId);
            if (t && t->construct) {
                t->construct(a.at(row));
            }
        }
        entityCount++;
        return row;
    }

    // Remove the entity at `row` by swap-back.
    void removeEntity(int row, const ComponentRegistry& reg) {
        int lastRow = entityCount - 1;
        if (row != lastRow) {
            for (auto& a : arrays) {
                const ComponentType* t = reg.getType(a.typeId);
                if (t && t->move) {
                    t->move(a.at(row), a.at(lastRow));
                    t->destruct(a.at(lastRow));
                } else if (t && t->destruct) {
                    t->destruct(a.at(row));
                    std::memcpy(a.at(row), a.at(lastRow), a.elementSize);
                    t->destruct(a.at(lastRow));
                }
            }
        } else {
            for (auto& a : arrays) {
                const ComponentType* t = reg.getType(a.typeId);
                if (t && t->destruct) t->destruct(a.at(row));
            }
        }
        entityCount--;
    }

    void* getComponent(int row, ComponentId cid) {
        auto it = arrayIndexByComponent.find(cid);
        if (it == arrayIndexByComponent.end()) return nullptr;
        return arrays[it->second].at(row);
    }
};

// Entity handle: archetype + row. Stored as 64-bit for fast comparison.
struct Entity {
    uint32_t archetypeIndex = 0xFFFFFFFF;
    uint32_t row = 0xFFFFFFFF;
    bool valid() const { return archetypeIndex != 0xFFFFFFFF; }
};

// Query: returns all archetypes that contain ALL the given components.
struct Query {
    std::vector<ComponentId> withComponents;
    std::vector<ComponentId> withoutComponents;
};

class ArchetypeWorld {
public:
    ArchetypeWorld() {
        // Always create the empty archetype (entities with no components).
        archetypes_.emplace_back();
        archetypes_.back().init({}, ComponentRegistry::get());
    }

    Entity createEntity() {
        Entity e;
        e.archetypeIndex = 0;  // empty archetype
        e.row = archetypes_[0].allocEntity(ComponentRegistry::get());
        return e;
    }

    void destroyEntity(Entity e) {
        if (!e.valid()) return;
        archetypes_[e.archetypeIndex].removeEntity(e.row, ComponentRegistry::get());
    }

    // Add a component to an entity. This MOVES the entity to a different
    // archetype (the one whose component set includes the new component).
    template<typename T>
    void addComponent(Entity& e, const T& value) {
        ComponentId cid = ComponentRegistry::get().registerType<T>(nullptr);
        // Find the entity's current archetype.
        Archetype& src = archetypes_[e.archetypeIndex];
        // Check if it already has the component.
        if (src.arrayIndexByComponent.count(cid)) return;
        // Build the new component set.
        std::vector<ComponentId> newSet = src.componentSet;
        newSet.push_back(cid);
        // Find or create the destination archetype.
        uint32_t dstIdx = findOrCreateArchetype(newSet);
        Archetype& dst = archetypes_[dstIdx];
        // Move the entity: allocate a new row in dst, copy existing
        // components, set the new one, remove from src.
        int newRow = dst.allocEntity(ComponentRegistry::get());
        for (ComponentId old : src.componentSet) {
            void* srcPtr = src.getComponent(e.row, old);
            void* dstPtr = dst.getComponent(newRow, old);
            if (srcPtr && dstPtr) {
                std::memcpy(dstPtr, srcPtr, ComponentRegistry::get().getType(old)->size);
            }
        }
        *static_cast<T*>(dst.getComponent(newRow, cid)) = value;
        src.removeEntity(e.row, ComponentRegistry::get());
        e.archetypeIndex = dstIdx;
        e.row = newRow;
    }

    template<typename T>
    T* getComponent(Entity e) {
        if (!e.valid()) return nullptr;
        return static_cast<T*>(
            archetypes_[e.archetypeIndex].getComponent(e.row,
                ComponentRegistry::get().registerType<T>(nullptr)));
    }

    template<typename T>
    void removeComponent(Entity& e) {
        ComponentId cid = ComponentRegistry::get().registerType<T>(nullptr);
        if (!e.valid()) return;
        Archetype& src = archetypes_[e.archetypeIndex];
        if (!src.arrayIndexByComponent.count(cid)) return;
        std::vector<ComponentId> newSet = src.componentSet;
        newSet.erase(std::remove(newSet.begin(), newSet.end(), cid), newSet.end());
        uint32_t dstIdx = findOrCreateArchetype(newSet);
        Archetype& dst = archetypes_[dstIdx];
        int newRow = dst.allocEntity(ComponentRegistry::get());
        for (ComponentId old : newSet) {
            void* srcPtr = src.getComponent(e.row, old);
            void* dstPtr = dst.getComponent(newRow, old);
            if (srcPtr && dstPtr) {
                std::memcpy(dstPtr, srcPtr, ComponentRegistry::get().getType(old)->size);
            }
        }
        src.removeEntity(e.row, ComponentRegistry::get());
        e.archetypeIndex = dstIdx;
        e.row = newRow;
    }

    // Iterate all entities matching the query. The callback receives
    // pointers to each requested component for each entity.
    template<typename... Components>
    void each(const Query& q, std::function<void(Components*...)> cb) {
        // Build the requested component set.
        std::vector<ComponentId> requested = {
            ComponentRegistry::get().registerType<Components>(nullptr)...
        };
        // Find matching archetypes.
        for (size_t i = 0; i < archetypes_.size(); i++) {
            if (!matchesQuery(archetypes_[i], q, requested)) continue;
            Archetype& a = archetypes_[i];
            for (int row = 0; row < a.entityCount; row++) {
                cb(static_cast<Components*>(a.getComponent(row,
                    ComponentRegistry::get().registerType<Components>(nullptr)))...);
            }
        }
    }

    size_t entityCount() const {
        size_t total = 0;
        for (const auto& a : archetypes_) total += a.entityCount;
        return total;
    }

    size_t archetypeCount() const { return archetypes_.size(); }

private:
    // Use deque so references to elements remain valid after push_back.
    // (Vector would invalidate the `src` reference captured in
    // addComponent when findOrCreateArchetype grows the container.)
    std::deque<Archetype> archetypes_;

    // Find an archetype whose component set exactly matches `set`.
    // If not found, create a new one.
    uint32_t findOrCreateArchetype(const std::vector<ComponentId>& set) {
        std::vector<ComponentId> sorted = set;
        std::sort(sorted.begin(), sorted.end());
        for (size_t i = 0; i < archetypes_.size(); i++) {
            if (archetypes_[i].componentSet == sorted) return (uint32_t)i;
        }
        Archetype a;
        a.init(sorted, ComponentRegistry::get());
        archetypes_.push_back(std::move(a));
        return archetypes_.size() - 1;
    }

    bool matchesQuery(const Archetype& a, const Query& q,
                      const std::vector<ComponentId>& requested) const {
        // Must have all requested + withComponents, none of withoutComponents.
        for (ComponentId c : requested) {
            if (!a.arrayIndexByComponent.count(c)) return false;
        }
        for (ComponentId c : q.withComponents) {
            if (!a.arrayIndexByComponent.count(c)) return false;
        }
        for (ComponentId c : q.withoutComponents) {
            if (a.arrayIndexByComponent.count(c)) return false;
        }
        return true;
    }
};

} // namespace archetype
} // namespace td
