// =============================================================================
// TD Engine - ScriptVM stub implementation (Tier 1.3)
//
// All methods are stubs that log + return failure. This lets the engine
// link against the ScriptVM API today; the real Lua embedding is tracked
// as Tier 1.3 in docs/MODULARITY_ROADMAP.md.
//
// To implement: link lua54 (or luau) and replace each stub with a real
// implementation. The header (script_vm.h) does NOT change.
// =============================================================================
#include "script_vm.h"

namespace td {

bool ScriptVM::init() {
    if (m_initialized) return true;
    TD_LOG_INFO("ScriptVM: init (stub — Lua embedding is Tier 1.3 TODO)");
    m_initialized = true;
    return true;
}

void ScriptVM::shutdown() {
    if (!m_initialized) return;
    TD_LOG_INFO("ScriptVM: shutdown (stub), unloaded %d scripts", m_loadedCount);
    m_initialized = false;
    m_loadedCount = 0;
}

ScriptHandle ScriptVM::loadScript(const char* path) {
    if (!m_initialized) {
        TD_LOG_WARN("ScriptVM::loadScript('%s') called before init()", path);
        return { -1 };
    }
    TD_LOG_INFO("ScriptVM: loadScript('%s') (stub — would compile + run Lua file)", path);
    // In the real impl, we'd:
    //   1. Read the file from disk (or from a packed asset bundle).
    //   2. lua_pcall() the chunk to define its functions.
    //   3. Stash the script's env table in the Lua registry, return the ref.
    // For the stub, return -1 so the ScriptSystem logs a one-time warning
    // and doesn't keep retrying every frame.
    return { -1 };
}

bool ScriptVM::reloadScript(ScriptHandle h) {
    if (!h.valid()) return false;
    TD_LOG_INFO("ScriptVM: reloadScript(%d) (stub)", h.id);
    return false;
}

void ScriptVM::unloadScript(ScriptHandle h) {
    if (!h.valid()) return;
    TD_LOG_INFO("ScriptVM: unloadScript(%d) (stub)", h.id);
    if (m_loadedCount > 0) m_loadedCount--;
}

void ScriptVM::updateAll(EntityId entityId, float dt) {
    (void)entityId; (void)dt;
    // Stub: no-op. Real impl iterates loaded scripts and calls their
    // update(entityId, dt) function.
}

void ScriptVM::bindSignal(ScriptHandle h, const char* eventName) {
    (void)h; (void)eventName;
    // Stub: no-op.
}

void ScriptVM::startFileWatcher() {
    TD_LOG_INFO("ScriptVM: startFileWatcher (stub — disabled on WASM, no-op on native)");
}

void ScriptVM::stopFileWatcher() {
    // Stub: no-op.
}

} // namespace td
