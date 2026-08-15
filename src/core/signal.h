// =============================================================================
// TD Engine - Signal / Event Bus (Tier 1.4)
//
// A first-class decoupled event system inspired by Godot's Signal type and
// Unity's UnityEvent. Lets systems, scripts, and gameplay code emit and
// subscribe to named events without compile-time dependencies on each other.
//
// Design:
//   - Events are keyed by a string name (e.g. "player:died", "scene:loaded").
//     String keys allow scripts (Lua/JS) to subscribe to events the engine
//     doesn't know about at compile time — critical for UGC.
//   - Payloads are passed as a small Variant struct (string + 4 floats + 1 int).
//     This is enough for 99% of gameplay events without forcing the caller
//     to allocate a heap object. For richer payloads, store a pointer in
//     the intValue field.
//   - Subscriptions are held in a fixed-size slot map (no per-emit alloc).
//   - The bus is a singleton (single-threaded engine today). When we go
//     multithreaded, we'll add a thread-local bus per worker + a deferred
//     main-thread bus.
//
// Usage:
//   // Subscribe
//   td::SignalBus::get().on("player:died", [](const td::SignalPayload& p) {
//       TD_LOG_INFO("Player %d died at %f, %f", p.intValue, p.f[0], p.f[1]);
//   });
//
//   // Emit
//   td::SignalPayload p;
//   p.intValue = playerId;
//   p.f[0] = x; p.f[1] = y;
//   td::SignalBus::get().emit("player:died", p);
//
//   // Or use the convenience overload for the common "no payload" case:
//   td::SignalBus::get().emit("scene:loaded");
//
// Thread safety: NOT thread-safe. Call only from the main thread. If a
// worker thread needs to emit, use SignalBus::defer() to queue the event
// for the next main-thread pump() call.
// =============================================================================
#pragma once
#include "../core/logger.h"
#include <cstdint>
#include <cstring>
#include <functional>
#include <array>

namespace td {

constexpr int SIGNAL_NAME_LEN   = 48;   // max event name length
constexpr int SIGNAL_MAX_SLOTS  = 256;  // max subscriptions per event name
constexpr int SIGNAL_MAX_EVENTS = 128;  // max distinct event names
constexpr int SIGNAL_PAYLOAD_FLOATS = 4;
constexpr int SIGNAL_PAYLOAD_STRLEN  = 32;

struct SignalPayload {
    std::array<float, SIGNAL_PAYLOAD_FLOATS> f = {{0,0,0,0}};
    int         intValue   = 0;
    const void* ptrValue   = nullptr;
    char        strValue[SIGNAL_PAYLOAD_STRLEN] = {};

    void setStr(const char* s) {
        if (!s) { strValue[0] = '\0'; return; }
        std::strncpy(strValue, s, SIGNAL_PAYLOAD_STRLEN - 1);
        strValue[SIGNAL_PAYLOAD_STRLEN - 1] = '\0';
    }
};

using SignalCallback = std::function<void(const SignalPayload&)>;

// A SubscriptionHandle is returned from on() so the caller can disconnect
// later. Disconnecting via handle is O(1); disconnecting by name+callback
// is O(n). Use handles for hot subscriptions (per-entity script bindings).
struct SubscriptionHandle {
    int eventIdx = -1;
    int slotIdx  = -1;
    bool valid() const { return eventIdx >= 0 && slotIdx >= 0; }
};

class SignalBus {
public:
    static SignalBus& get() {
        static SignalBus instance;
        return instance;
    }

    // Subscribe to a named event. Returns a handle for fast disconnect.
    // If the event name doesn't exist yet, it's registered.
    // If all slots for this event are full, returns { -1, -1 } and logs
    // a warning.
    SubscriptionHandle on(const char* name, SignalCallback cb) {
        int eventIdx = findOrCreateEvent(name);
        if (eventIdx < 0) {
            TD_LOG_WARN("SignalBus: too many distinct events (max %d), "
                        "cannot register '%s'", SIGNAL_MAX_EVENTS, name);
            return {};
        }
        int slot = findFreeSlot(eventIdx);
        if (slot < 0) {
            TD_LOG_WARN("SignalBus: event '%s' has %d subscriptions (max %d), "
                        "dropping new subscription", name, SIGNAL_MAX_SLOTS, SIGNAL_MAX_SLOTS);
            return {};
        }
        m_events[eventIdx].slots[slot]     = std::move(cb);
        m_events[eventIdx].slotActive[slot] = true;
        return { eventIdx, slot };
    }

    // Disconnect via handle. O(1).
    void off(SubscriptionHandle h) {
        if (!h.valid()) return;
        m_events[h.eventIdx].slots[h.slotIdx]     = nullptr;
        m_events[h.eventIdx].slotActive[h.slotIdx] = false;
    }

    // Disconnect ALL subscriptions for a named event. O(slots).
    void clear(const char* name) {
        int idx = findEvent(name);
        if (idx < 0) return;
        for (int i = 0; i < SIGNAL_MAX_SLOTS; i++) {
            m_events[idx].slots[i]      = nullptr;
            m_events[idx].slotActive[i] = false;
        }
    }

    // Emit a named event with payload. Synchronously calls every active
    // subscriber. Subscribers can subscribe/unsubscribe OTHER events from
    // inside the callback, but unsubscribing the currently-emitting event
    // is deferred until after emit() returns (to avoid iterator invalidation).
    void emit(const char* name, const SignalPayload& payload) {
        int idx = findEvent(name);
        if (idx < 0) return;
        EventEntry& e = m_events[idx];
        // Snapshot the active slot list so callbacks can mutate the
        // subscription set without invalidating our iteration.
        bool activeSnapshot[SIGNAL_MAX_SLOTS];
        for (int i = 0; i < SIGNAL_MAX_SLOTS; i++) {
            activeSnapshot[i] = e.slotActive[i];
        }
        for (int i = 0; i < SIGNAL_MAX_SLOTS; i++) {
            if (activeSnapshot[i] && e.slots[i]) {
                e.slots[i](payload);
            }
        }
    }

    // Convenience overload for events with no meaningful payload.
    void emit(const char* name) {
        SignalPayload empty;
        emit(name, empty);
    }

    // Queue an event for delivery on the next pump() call. Used by worker
    // threads (when we add them) and by systems that want to defer event
    // delivery until after the current simulation step (e.g. "entity
    // destroyed" events shouldn't fire mid-iteration).
    void defer(const char* name, const SignalPayload& payload) {
        if (m_deferredCount >= DEFERRED_QUEUE_SIZE) {
            TD_LOG_WARN("SignalBus: deferred queue full, dropping event '%s'", name);
            return;
        }
        DeferredEvent& d = m_deferred[m_deferredCount++];
        std::strncpy(d.name, name, SIGNAL_NAME_LEN);
        d.name[SIGNAL_NAME_LEN - 1] = '\0';
        d.payload = payload;
    }

    // Flush the deferred queue. Call once per frame from the main thread,
    // AFTER all systems have finished updating (so deferred events see a
    // consistent world state).
    void pump() {
        int n = m_deferredCount;
        m_deferredCount = 0;  // reset first so callbacks can defer again
        for (int i = 0; i < n; i++) {
            emit(m_deferred[i].name, m_deferred[i].payload);
        }
    }

    // Clear all subscriptions and deferred events. Called by World::clear()
    // so a scene reload doesn't leak subscriptions from the previous scene.
    void clearAll() {
        for (int i = 0; i < m_eventCount; i++) {
            for (int s = 0; s < SIGNAL_MAX_SLOTS; s++) {
                m_events[i].slots[s]      = nullptr;
                m_events[i].slotActive[s] = false;
            }
            m_events[i].name[0] = '\0';
        }
        m_eventCount    = 0;
        m_deferredCount = 0;
    }

    int eventCount() const { return m_eventCount; }

    // Returns true if at least one subscriber has ever been registered for
    // `name` (even if all slots are now inactive — we only forget an event
    // name on clearAll()). Used by RpcServer to detect "unknown method".
    bool eventExists(const char* name) const {
        return findEvent(name) >= 0;
    }

private:
    SignalBus() = default;

    struct EventEntry {
        char            name[SIGNAL_NAME_LEN] = {};
        SignalCallback  slots[SIGNAL_MAX_SLOTS];
        bool            slotActive[SIGNAL_MAX_SLOTS] = {};
    };

    struct DeferredEvent {
        char           name[SIGNAL_NAME_LEN] = {};
        SignalPayload  payload;
    };

    static constexpr int DEFERRED_QUEUE_SIZE = 256;

    int findEvent(const char* name) const {
        for (int i = 0; i < m_eventCount; i++) {
            if (std::strncmp(m_events[i].name, name, SIGNAL_NAME_LEN) == 0) {
                return i;
            }
        }
        return -1;
    }

    int findOrCreateEvent(const char* name) {
        int idx = findEvent(name);
        if (idx >= 0) return idx;
        if (m_eventCount >= SIGNAL_MAX_EVENTS) return -1;
        idx = m_eventCount++;
        std::strncpy(m_events[idx].name, name, SIGNAL_NAME_LEN);
        m_events[idx].name[SIGNAL_NAME_LEN - 1] = '\0';
        return idx;
    }

    int findFreeSlot(int eventIdx) const {
        const EventEntry& e = m_events[eventIdx];
        for (int i = 0; i < SIGNAL_MAX_SLOTS; i++) {
            if (!e.slotActive[i]) return i;
        }
        return -1;
    }

    EventEntry     m_events[SIGNAL_MAX_EVENTS];
    int            m_eventCount    = 0;
    DeferredEvent  m_deferred[DEFERRED_QUEUE_SIZE];
    int            m_deferredCount = 0;
};

} // namespace td
