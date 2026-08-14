// =============================================================================
// TD Engine - Profiler v1 (Tier 1.6)
//
// Per-system CPU timing ring buffer + scope timer. Inspired by Unity's
// Profiler modules and Godot's profilers. Designed to be:
//   - Zero-cost when disabled (TD_PROFILE() compiles to nothing)
//   - Lock-free for the push path (single-threaded by design; the engine
//     update loop is single-threaded today)
//   - Bounded memory (fixed-size ring, no allocation in steady state)
//   - Self-contained (no external dependency; the editor reads the buffer
//     via Profiler::frameSnapshot())
//
// Usage:
//   void MovementSystem::update(World* w, float dt) {
//       TD_PROFILE_SCOPE("MovementSystem::update");
//       // ... hot loop ...
//   }
//
//   // In the editor / debug overlay:
//   td::Profiler::Snapshot snap;
//   td::Profiler::get().frameSnapshot(snap);
//   for (auto& zone : snap.zarm) {  // up to MAX_ZONES_PER_FRAME entries
//       ImGui::Text("%s: %.3f ms", zone.label, zone.elapsedMs);
//   }
//
// The profiler is a singleton because systems are scattered across the
// codebase and we don't want to thread a pointer through every call.
// Single-threaded use means this is safe.
// =============================================================================
#pragma once
#include "../core/logger.h"
#include <cstdint>
#include <cstring>
#include <chrono>

namespace td {

// Maximum number of distinct zones (labelled scopes) recorded per frame.
// 128 is enough for a typical frame: ~20 systems + ~50 entity scripts +
// ~30 render passes + headroom.
constexpr int PROFILER_MAX_ZONES_PER_FRAME = 128;

// Maximum label length. Long labels are truncated; the profiler never
// allocates strings.
constexpr int PROFILER_LABEL_LEN = 48;

// Ring buffer depth. The profiler keeps the last N frames so the editor
// can show a sliding-window graph. 240 frames @ 60 FPS = 4 seconds of
// history, which is enough to spot spikes without bloating memory.
constexpr int PROFILER_HISTORY_FRAMES = 240;

struct ProfileZone {
    char     label[PROFILER_LABEL_LEN] = {};
    float    elapsedMs = 0.0f;
    uint32_t hits      = 0;       // number of begin/end pairs with this label
};

struct ProfileFrame {
    ProfileZone zones[PROFILER_MAX_ZONES_PER_FRAME];
    int         zoneCount = 0;
    float       frameMs   = 0.0f;  // total frame time
    int         frameIdx  = 0;
};

class Profiler {
public:
    static Profiler& get() {
        static Profiler instance;
        return instance;
    }

    // --- Frame lifecycle -----------------------------------------------------
    // Call beginFrame() at the top of the engine update, endFrame() at the
    // bottom. All TD_PROFILE_SCOPE() blocks in between record into the
    // current frame.
    void beginFrame() {
        int idx = m_writeIdx;
        m_frames[idx].zoneCount = 0;
        m_frames[idx].frameIdx  = m_totalFrames++;
        m_frameStart = clock::now();
    }

    void endFrame() {
        int idx = m_writeIdx;
        m_frames[idx].frameMs = sinceMs(m_frameStart);
        m_writeIdx = (m_writeIdx + 1) % PROFILER_HISTORY_FRAMES;
    }

    // --- Zone recording ------------------------------------------------------
    // Called by TD_PROFILE_SCOPE's constructor. Returns the slot index in
    // the current frame's zones[] array, or -1 if the frame is full
    // (further zones are dropped to avoid overflow).
    //
    // If a zone with the same label was already recorded this frame, we
    // accumulate elapsedMs and increment hits — this is how a system that
    // calls TD_PROFILE_SCOPE("ai_think") inside a per-entity loop reports
    // total time + call count.
    int beginZone(const char* label) {
        int idx = m_writeIdx;
        ProfileFrame& frame = m_frames[idx];

        // Linear scan for an existing slot with the same label.
        // PROFILER_MAX_ZONES_PER_FRAME is small (128), and most frames
        // touch < 30 distinct labels, so this is effectively free.
        for (int i = 0; i < frame.zoneCount; i++) {
            if (std::strncmp(frame.zones[i].label, label, PROFILER_LABEL_LEN) == 0) {
                return i;
            }
        }
        if (frame.zoneCount >= PROFILER_MAX_ZONES_PER_FRAME) return -1;
        int slot = frame.zoneCount++;
        std::strncpy(frame.zones[slot].label, label, PROFILER_LABEL_LEN);
        frame.zones[slot].label[PROFILER_LABEL_LEN - 1] = '\0';
        frame.zones[slot].elapsedMs = 0.0f;
        frame.zones[slot].hits      = 0;
        return slot;
    }

    void endZone(int slot, float elapsedMs) {
        if (slot < 0) return;
        ProfileZone& z = m_frames[m_writeIdx].zones[slot];
        z.elapsedMs += elapsedMs;
        z.hits++;
    }

    // --- Snapshot for the editor / debug overlay ----------------------------
    // Copies the most recent COMPLETED frame into `out`. The editor calls
    // this once per UI refresh (typically once per frame).
    void frameSnapshot(ProfileFrame& out) const {
        // m_writeIdx points at the frame currently being recorded (or the
        // next one to record if we just called endFrame). The most recent
        // COMPLETED frame is at (m_writeIdx - 1 + N) % N.
        int idx = (m_writeIdx - 1 + PROFILER_HISTORY_FRAMES) % PROFILER_HISTORY_FRAMES;
        out = m_frames[idx];
    }

    // Returns the sliding-window history for graph rendering. `outFrames`
    // is filled in chronological order (oldest first). Returns count.
    int historySnapshot(ProfileFrame* outFrames, int maxFrames) const {
        int n = maxFrames < PROFILER_HISTORY_FRAMES ? maxFrames : PROFILER_HISTORY_FRAMES;
        // Chronological order: start at (m_writeIdx - n + N) % N, walk n.
        int start = (m_writeIdx - n + PROFILER_HISTORY_FRAMES) % PROFILER_HISTORY_FRAMES;
        for (int i = 0; i < n; i++) {
            outFrames[i] = m_frames[(start + i) % PROFILER_HISTORY_FRAMES];
        }
        return n;
    }

    int totalFrames() const { return m_totalFrames; }

    // --- Enable / disable ----------------------------------------------------
    // When disabled, TD_PROFILE_SCOPE compiles to nothing (see macro below).
    // The runtime flag is for the editor: the user can toggle profiling on
    // and off without recompiling.
    void setEnabled(bool e) { m_enabled = e; }
    bool isEnabled() const  { return m_enabled; }

private:
    Profiler() = default;

    using clock = std::chrono::high_resolution_clock;
    static float sinceMs(clock::time_point t) {
        auto now = clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - t).count();
        return static_cast<float>(us) * 0.001f;
    }

    ProfileFrame       m_frames[PROFILER_HISTORY_FRAMES];
    int                m_writeIdx     = 0;
    int                m_totalFrames  = 0;
    bool               m_enabled      = true;
    clock::time_point  m_frameStart;
};

// =============================================================================
// Scope guard. Constructor calls Profiler::beginZone(); destructor calls
// endZone() with the elapsed time. Use via the TD_PROFILE_SCOPE macro so
// the variable name is unique per scope.
// =============================================================================
class ProfileScope {
public:
    ProfileScope(const char* label) {
        if (!Profiler::get().isEnabled()) { m_slot = -1; return; }
        m_start = clock::now();
        m_slot  = Profiler::get().beginZone(label);
    }
    ~ProfileScope() {
        if (m_slot < 0) return;
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now() - m_start).count();
        Profiler::get().endZone(m_slot, static_cast<float>(us) * 0.001f);
    }
private:
    using clock = std::chrono::high_resolution_clock;
    int                m_slot = -1;
    clock::time_point  m_start;
};

} // namespace td

// =============================================================================
// Public macros. TD_PROFILE_SCOPE is the only one most code uses.
//
// We use __COUNTER__ to generate a unique variable name per source line,
// so the same scope can have multiple TD_PROFILE_SCOPE() blocks without
// name collisions.
// =============================================================================
#ifdef TD_DISABLE_PROFILER
    #define TD_PROFILE_SCOPE(label) ((void)0)
#else
    #define TD_PROFILE_CONCAT2(a, b) a##b
    #define TD_PROFILE_CONCAT(a, b)  TD_PROFILE_CONCAT2(a, b)
    #define TD_PROFILE_SCOPE(label) \
        ::td::ProfileScope TD_PROFILE_CONCAT(_td_prof_, __COUNTER__)(label)
#endif
