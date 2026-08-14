// =============================================================================
// TD Engine - Server-Authoritative Netcode (REAL implementation, internal)
//
// Backs the frozen public API in server_authoritative.h with concrete types
// the gameplay code + tests can target:
//
//   - Input               fixed-size player input frame (8 keys + 2 axes + frame)
//   - Snapshot            entity transforms at a single tick
//   - ClientPredictor     predicts local simulation, reconciles on snapshot
//   - ServerReconciler    authoritative sim driver + snapshot broadcaster
//   - LagCompensator      rewinds world for hit detection
//
// Status: REAL. The public ClientPrediction / LagCompensationHistory classes
// in server_authoritative.h stay as stubs (their inline bodies are frozen);
// these new classes provide the working implementation.
// =============================================================================
#pragma once
#include "server_authoritative.h"
#include "transport_impl.h"
#include "../core/logger.h"
#include "../core/signal.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>
#include <string>
#include <array>

namespace td {
namespace net {

// =============================================================================
// Input — fixed-size player input frame.
//
//   8 boolean keys + 2 float axes + 1 uint32 frame number.
//   Total 32 bytes (matches the InputBuffer slot size in the public header).
//   Wire-friendly: trivially serializable as a fixed32 + bitfield.
// =============================================================================
struct Input {
    uint8_t  keys = 0;       // bit i = key i (8 keys)
    float    axisX = 0.0f;
    float    axisY = 0.0f;
    uint32_t frame = 0;

    bool key(int i) const {
        return (keys & (1u << i)) != 0;
    }
    void setKey(int i, bool down) {
        if (down) keys |=  (1u << i);
        else      keys &= ~(1u << i);
    }

    bool operator==(const Input& o) const {
        return keys == o.keys && axisX == o.axisX && axisY == o.axisY && frame == o.frame;
    }
    bool operator!=(const Input& o) const { return !(*this == o); }
};
static_assert(sizeof(Input) <= 32, "Input must fit in InputBuffer's 32-byte slots");

// =============================================================================
// Transform — minimal network transform (pos + rot + scale). Used in snapshots.
// =============================================================================
struct Transform {
    float x = 0, y = 0, z = 0;
    float rotX = 0, rotY = 0, rotZ = 0;
    float scaleX = 1, scaleY = 1, scaleZ = 1;

    bool operator==(const Transform& o) const {
        return x == o.x && y == o.y && z == o.z
            && rotX == o.rotX && rotY == o.rotY && rotZ == o.rotZ
            && scaleX == o.scaleX && scaleY == o.scaleY && scaleZ == o.scaleZ;
    }
    bool operator!=(const Transform& o) const { return !(*this == o); }

    bool approxEquals(const Transform& o, float eps) const {
        return std::fabs(x - o.x) <= eps && std::fabs(y - o.y) <= eps && std::fabs(z - o.z) <= eps;
    }
};

// =============================================================================
// Snapshot — entity transforms at a single tick.
// =============================================================================
struct Snapshot {
    uint32_t frame = 0;
    std::map<uint32_t, Transform> entities;  // entityNetworkId -> transform

    void set(uint32_t entityId, const Transform& t) { entities[entityId] = t; }
    bool get(uint32_t entityId, Transform& out) const {
        auto it = entities.find(entityId);
        if (it == entities.end()) return false;
        out = it->second;
        return true;
    }
    int entityCount() const { return static_cast<int>(entities.size()); }
    void clear() { frame = 0; entities.clear(); }
};

// =============================================================================
// ClientPredictor — client-side prediction + reconciliation.
//
// Workflow per frame (on the CLIENT):
//   1. Capture the player's input for this frame.
//   2. predictor.recordInput(input) — stashes (frame, input) in a ring buffer.
//   3. Step the local simulation: predictor.applyPredicted(input).
//      (The caller wires the actual simulation step via the StepFn callback.)
//
// When a server snapshot arrives:
//   4. predictor.onSnapshot(snapshot, myEntityId).
//      - Find the predicted transform at snapshot.frame in the ring buffer.
//      - Compare to snapshot.entities[myEntityId].
//      - If drift > threshold:
//          a. Snap local transform to snapshot.
//          b. Replay all inputs newer than snapshot.frame via applyPredicted.
//      - Else: do nothing (prediction was correct).
//      - Mark lastAckedFrame = snapshot.frame.
//
// The ring buffer holds the last 64 inputs, which at 60Hz is ~1s of history.
// =============================================================================
class ClientPredictor {
public:
    static constexpr int  RING_SIZE     = 64;
    static constexpr float DEFAULT_DRIFT_THRESHOLD = 0.05f; // 5cm in world units

    using StepFn           = std::function<void(const Input&)>;
    using GetTransformFn   = std::function<Transform(uint32_t entityId)>;
    using SetTransformFn   = std::function<void(uint32_t entityId, const Transform&)>;

    ClientPredictor();
    ~ClientPredictor();

    // Wire the predictor to the local simulation. All three callbacks must be
    // set before recordInput / onSnapshot are called.
    void init(uint32_t startFrame,
              StepFn         stepFn,
              GetTransformFn getFn,
              SetTransformFn setFn);

    // Record an input frame and advance the local prediction by one tick.
    void recordInput(const Input& input);

    // Process a server snapshot. May trigger a rewind + replay.
    void onSnapshot(const Snapshot& snap, uint32_t myEntityId);

    // Tunables.
    void setDriftThreshold(float eps) { m_driftThreshold = eps; }

    // Stats / introspection.
    uint32_t currentFrame()    const { return m_currentFrame; }
    uint32_t lastAckedFrame()  const { return m_lastAckedFrame; }
    int      mispredictionCount() const { return m_mispredictions; }
    int      replayedInputCount() const { return m_replayedInputs; }
    bool     hadMisprediction() const { return m_lastWasMisprediction; }
    int      ringCount()       const { return m_count; }

    // Reset to initial state.
    void reset();

private:
    struct RingEntry {
        uint32_t frame = 0;
        Input    input{};
    };
    std::array<RingEntry, RING_SIZE> m_ring;
    int      m_writeIdx = 0;
    int      m_count    = 0;

    uint32_t m_currentFrame    = 0;
    uint32_t m_lastAckedFrame  = 0;
    float    m_driftThreshold  = DEFAULT_DRIFT_THRESHOLD;
    int      m_mispredictions  = 0;
    int      m_replayedInputs  = 0;
    bool     m_lastWasMisprediction = false;

    StepFn         m_step;
    GetTransformFn m_get;
    SetTransformFn m_set;

    // Find the ring entry whose frame == target, or nullptr.
    RingEntry* findEntry(uint32_t frame);
    // Drop all ring entries with frame <= ackedFrame (they're confirmed).
    void dropAcked(uint32_t ackedFrame);
    // Replay all ring entries with frame > fromFrame up to current.
    void replayFrom(uint32_t fromFrame);
};

// =============================================================================
// ServerReconciler — authoritative simulation driver + snapshot broadcaster.
//
// Workflow per server tick:
//   1. For each connected client, drain its pending input queue.
//   2. Step the authoritative simulation for each input.
//   3. Every (1000 / SNAPSHOT_RATE_HZ) ms, build + broadcast a snapshot.
//
// Inputs are acked as soon as they're processed (the ack is implicit in the
// next snapshot's frame number — the client treats snapshot.frame as the
// "last processed input frame" for that client).
// =============================================================================
class ServerReconciler {
public:
    static constexpr uint32_t SNAPSHOT_INTERVAL_MS = 1000 / 20; // 20 Hz

    using StepFn          = std::function<void(uint32_t clientId, const Input&)>;
    using BuildSnapshotFn = std::function<Snapshot()>;
    using BroadcastFn     = std::function<void(const Snapshot&)>;

    ServerReconciler();
    ~ServerReconciler();

    void init(StepFn stepFn, BuildSnapshotFn buildFn, BroadcastFn broadcastFn);

    // Receive an input from a client. Inputs from the same client are queued
    // in order by frame number; older duplicates are dropped.
    void onInput(uint32_t clientId, const Input& input);

    // Drive the simulation. Returns the number of inputs processed.
    // `nowMs` is the same time base as NetworkInterface::nowMs().
    int tick(uint32_t nowMs);

    // Force a snapshot broadcast now (e.g. on a critical state change).
    void forceSnapshot();

    // True if a snapshot is due this tick (called by tick()).
    bool snapshotDue(uint32_t nowMs) const;

    // Stats.
    uint32_t currentFrame()    const { return m_currentFrame; }
    uint32_t lastSnapshotFrame() const { return m_lastSnapshotFrame; }
    uint32_t lastAckedFrame(uint32_t clientId) const;
    int      pendingInputCount(uint32_t clientId) const;
    int      clientCount() const { return static_cast<int>(m_clients.size()); }

    // Per-client input queue size cap. Older inputs are dropped on overflow.
    static constexpr int MAX_CLIENT_PENDING = 128;

private:
    struct ClientState {
        std::vector<Input> pending;
        // Initial value UINT32_MAX means "no frames processed yet" so the
        // first input at frame 0 is accepted (0 <= UINT32_MAX is true, but
        // we use strict < for the "already processed" check below).
        uint32_t lastProcessedFrame = 0;
        bool     hasProcessed = false;
        uint32_t nextExpectedFrame  = 0;
    };
    std::unordered_map<uint32_t, ClientState> m_clients;

    StepFn          m_step;
    BuildSnapshotFn m_build;
    BroadcastFn     m_broadcast;

    uint32_t m_currentFrame       = 0;
    uint32_t m_lastSnapshotMs     = 0;
    uint32_t m_lastSnapshotFrame  = 0;
};

// =============================================================================
// LagCompensator — server-side rewind for hit detection.
//
// When a client sends "I fired at tick T", the server has already advanced
// past tick T. To make hits fair, the server REWINDS its world state to
// tick T, performs the hit test there, then snaps back.
//
// We keep a ring buffer of the last 1 second of per-tick entity transforms
// (at 60Hz, that's 60 ticks). Each tick's data is a map<entityId, Transform>.
//
// hitScanAt() does a sphere-vs-ray intersection at the rewound position.
// =============================================================================
class LagCompensator {
public:
    // ~1 second at 60Hz. The ring overwrites oldest entries on overflow.
    static constexpr int HISTORY_SIZE = 64;

    LagCompensator();
    ~LagCompensator();

    // Record a single entity's transform at a single tick. Multiple calls
    // for the same (tick, entity) update the entry.
    void recordTick(uint32_t tick, uint32_t entityId, const Transform& t);

    // Sample an entity's transform at `tick`. Returns false if not in history.
    bool sampleAt(uint32_t tick, uint32_t entityId, Transform& out) const;

    // Hit-test: ray from origin O in direction D (assumed normalized) at the
    // rewound tick, vs. a sphere of radius r around the target entity.
    // Returns true if the ray hits the sphere within its length.
    bool hitscanAt(uint32_t tick,
                   float ox, float oy, float oz,
                   float dx, float dy, float dz,
                   uint32_t targetEntityId,
                   float radius,
                   float maxDist) const;

    // Convenience: closest-entity hit-scan. Returns the entityId of the
    // closest entity hit by the ray, or 0xFFFFFFFF if none.
    uint32_t hitscanClosest(uint32_t tick,
                            float ox, float oy, float oz,
                            float dx, float dy, float dz,
                            float radius, float maxDist) const;

    // Stats.
    int  recordedTicks() const { return m_count; }
    void clear();

private:
    struct TickRecord {
        uint32_t tick = 0;
        std::map<uint32_t, Transform> entities;
        bool     valid = false;
    };
    std::array<TickRecord, HISTORY_SIZE> m_history;
    int m_writeIdx = 0;
    int m_count    = 0;

    const TickRecord* findTick(uint32_t tick) const;
};

} // namespace net
} // namespace td
