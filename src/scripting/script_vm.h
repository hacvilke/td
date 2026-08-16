// =============================================================================
// TD Engine - Lua Scripting VM (Tier 1.3)
//
// Embeds Lua 5.4 (or Luau, if built with -DTD_USE_LUAU) as the gameplay
// scripting language. Mirrors Godot's GDScript integration pattern:
//   - Scripts are .lua files loaded at runtime (no engine recompile).
//   - The engine exposes a `td` library to Lua: td.create_entity(),
//     td.set_position(), td.on_update(), td.on_signal(), etc.
//   - Hot reload: file-watcher detects .lua changes, calls reloadScript()
//     which re-evaluates the file in the existing Lua state, preserving
//     registered callbacks. (TODO: state migration for table-valued state.)
//
// Why Lua:
//   - Single-file embed (lua.c + lualib.c, ~300KB compiled).
//   - Sandboxing is well-understood (Luau is Roblox's sandboxed Lua fork;
//     we can swap to it later for production UGC).
//   - Existing C++ ECS query API maps cleanly to Lua tables.
//
// Why NOT a custom language:
//   - The old `src/td/` directory had a custom lexer/parser/VM. It was
//     removed during the cleanup pass because writing + maintaining a
//     language is a multi-month distraction. Lua is 30 years of polish
//     for free.
//
// Status: REAL implementation. The custom tdscript VM (lexer + parser +
// codegen + stack-based interpreter, ~3,100 lines) lives in
// src/scripting/tdscript/. It replaces the originally-planned Lua embedding
// (Tier 1.3 in docs/MODULARITY_ROADMAP.md is closed). ScriptVM exposes
// loadScript() / updateAll() / reloadScript() / unloadScript() / bindSignal()
// against the in-engine tdscript VM, and also supports hot reload via file
// mtime polling. See src/scripting/tdscript/tdscript.h for the VM internals.
// =============================================================================
#pragma once
#include "../ecs/world.h"
#include "../ecs/component.h"
#include "../ecs/system.h"
#include "../core/logger.h"
#include "../core/profiler.h"
#include "../core/signal.h"
#include <cstdint>

namespace td {

// Opaque handle to a loaded script instance. The VM owns the actual Lua
// state; callers (e.g., the ScriptSystem) hold this handle in a
// LuaScriptComponent.
struct ScriptHandle {
    int id = -1;  // -1 = invalid
    bool valid() const { return id >= 0; }
};

class ScriptVM {
public:
    static ScriptVM& get() {
        static ScriptVM instance;
        return instance;
    }

    // --- Lifecycle ----------------------------------------------------------
    // init() creates the Lua state and registers the `td.*` library.
    // Returns true on success. Safe to call multiple times (subsequent
    // calls are no-ops).
    bool init();

    // shutdown() destroys the Lua state. All ScriptHandles become invalid.
    void shutdown();

    // --- Loading ------------------------------------------------------------
    // Load a .lua file from disk and return a handle.
    // On failure, returns { -1 } and logs an error.
    //
    // The script file should define an `init(entityId)` function and an
    // `update(entityId, dt)` function. The VM calls these automatically
    // (see update() below).
    ScriptHandle loadScript(const char* path);

    // Reload a previously-loaded script from disk. Preserves the handle
    // and any registered callbacks in the engine; re-evaluates the file
    // in the Lua state.
    //
    // TODO: migrate table-valued state from the old script to the new one.
    // For now, reload is destructive (state is lost).
    bool reloadScript(ScriptHandle h);

    // Unload a script. The handle becomes invalid.
    void unloadScript(ScriptHandle h);

    // --- Per-frame update ---------------------------------------------------
    // Calls the `update(entityId, dt)` function on every loaded script
    // that has one. Called by the ScriptSystem once per frame.
    void updateAll(EntityId entityId, float dt);

    // --- Event subscription -------------------------------------------------
    // Bind a Lua callback to a SignalBus event. The script calls
    //   td.on_signal("player:died", function(payload) ... end)
    // and the VM registers a C++ lambda that forwards to the Lua function.
    //
    // The binding is stored with the script handle so unloadScript()
    // automatically disconnects all of the script's signal handlers.
    void bindSignal(ScriptHandle h, const char* eventName);

    // --- Hot reload watcher -------------------------------------------------
    // Start a background thread that watches the scripts/ directory for
    // .lua file changes. On change, calls reloadScript() for every script
    // loaded from that file. Disabled on WASM (no file system access).
    void startFileWatcher();
    void stopFileWatcher();

    // --- Introspection ------------------------------------------------------
    bool isInitialized() const { return m_initialized; }
    int  loadedScriptCount() const { return m_loadedCount; }

private:
    ScriptVM() = default;
    bool m_initialized = false;
    int  m_loadedCount = 0;
};

// =============================================================================
// ScriptSystem — the ECS system that ticks Lua scripts each frame.
//
// Registered with World::addSystem(new ScriptSystem()). On update, it
// queries for entities with a LuaScriptComponent and calls
// ScriptVM::updateAll(entityId, dt) for each.
//
// Implementation lives in script_vm.cpp (TODO Tier 1.3).
// =============================================================================
class ScriptSystem : public System {
public:
    void update(World* world, float dt) override {
        TD_PROFILE_SCOPE("ScriptSystem::update");
        EntityId ids[TD_MAX_ENTITIES];
        ComponentMask mask = componentBit(ComponentType::LuaScript);
        int n = world->queryActive(mask, ids, TD_MAX_ENTITIES);
        for (int i = 0; i < n; i++) {
            LuaScriptComponent* ls = world->getComponent<LuaScriptComponent>(ids[i]);
            if (!ls || !ls->enabled) continue;
            if (!ls->initialized) {
                // Lazy-init: load the script file on first encounter.
                ScriptHandle h = ScriptVM::get().loadScript(ls->scriptPath);
                ls->vmRef       = h.id;
                ls->initialized = (h.id >= 0);
            }
            ScriptVM::get().updateAll(ids[i], dt);
        }
    }
    ComponentMask getRequiredComponents() const override {
        return componentBit(ComponentType::LuaScript);
    }
};

} // namespace td
