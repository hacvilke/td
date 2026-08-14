// =============================================================================
// TD Engine - Server-Authoritative Netcode (Tier 3.1)
//
// Lag-compensated, server-authoritative multiplayer with client-side
// prediction + reconciliation. Inspired by Unity Netcode for Entities and
// the Source/Quake model.
//
// Architecture:
//   - The SERVER owns the canonical World state. Clients send INPUT to the
//     server at a fixed tick rate (default 60 Hz).
//   - The CLIENT runs the same simulation locally (prediction) so the
//     player sees immediate feedback. Each predicted tick is tagged with
//     the input sequence number that produced it.
//   - The server sends WORLD SNAPSHOTS at a lower rate (default 20 Hz).
//     Each snapshot contains the entity states the client can see (interest
//     management — Tier 3.2).
//   - When a snapshot arrives, the client rewinds its prediction to the
//     snapshot's tick, replays all inputs newer than that tick, and snaps
//     to the result (reconciliation). If the rewind result differs from
//     the snapshot, the snapshot wins (server is authoritative).
//
// Status: SKELETON. The classes + interfaces are defined so gameplay code
// can target the API today. The actual integration with the Network
// transport (Tier 1.5) + the snapshot delta-compression (Tier 3.2) are
// tracked in docs/MODULARITY_ROADMAP.md.
// =============================================================================
#pragma once
#include "../ecs/world.h"
#include "../ecs/component.h"
#include "../core/logger.h"
#include "../core/signal.h"
#include "../net/transport.h"
#include <cstdint>

namespace td {

constexpr int NET_TICK_RATE_HZ       = 60;
constexpr int NET_SNAPSHOT_RATE_HZ   = 20;
constexpr int NET_INPUT_BUFFER_SIZE  = 256;   // ~4 seconds @ 60Hz
constexpr int NET_SNAPSHOT_BUFFER_SIZE = 64;  // ~3 seconds @ 20Hz

// NetRole identifies whether this instance is the server, a client, or
// running standalone (single-player).
enum class NetRole : uint8_t {
    Standalone,  // no networking
    Server,      // authoritative
    Client,      // predictive
};

// NetTransformComponent: marks an entity as network-replicated. The
// server snapshots these; clients interpolate between snapshots.
// (For larger entities, use NetStateComponent which carries arbitrary
// scripted state. TODO Tier 3.2.)
struct NetTransformComponent {
    uint32_t networkId = 0;          // stable across server/client (unlike EntityId)
    bool     serverOwned = true;     // true on the server, false on clients for the same entity
    bool     interpolate = true;     // snap (false) or interpolate (true) between snapshots
    // Snapshots are kept in a small ring buffer; the client reads from
    // the buffer at (renderTime - interpolationDelay) and lerps.
    struct Snapshot {
        uint32_t tick = 0;
        float    x = 0, y = 0, z = 0;
        float    rotX = 0, rotY = 0, rotZ = 0;
    };
    Snapshot  history[NET_SNAPSHOT_BUFFER_SIZE];
    int       historyWriteIdx = 0;
    int       historyCount    = 0;
};

// InputBuffer: client-side ring buffer of (tick, inputBlob) pairs.
// Used to replay inputs after a server snapshot arrives (reconciliation).
struct InputBuffer {
    uint32_t tick[NET_INPUT_BUFFER_SIZE];
    uint8_t  data[NET_INPUT_BUFFER_SIZE * 32];  // 32 bytes per input frame
    int      dataSize[NET_INPUT_BUFFER_SIZE];
    int      writeIdx = 0;
    int      count    = 0;

    void push(uint32_t t, const void* in, int sz) {
        if (sz > 32) sz = 32;
        tick[writeIdx] = t;
        dataSize[writeIdx] = sz;
        memcpy(&data[writeIdx * 32], in, sz);
        writeIdx = (writeIdx + 1) % NET_INPUT_BUFFER_SIZE;
        if (count < NET_INPUT_BUFFER_SIZE) count++;
    }
};

// ClientPrediction: drives the predicted simulation + reconciliation.
//   - recordInput(): called each frame with the player's input.
//   - onSnapshot(): called when a server snapshot arrives; rewinds +
//     replays + snaps.
//   - getRenderTransform(): returns the predicted transform for rendering.
class ClientPrediction {
public:
    void init(uint32_t startTick) {
        m_currentTick = startTick;
    }

    // Called each frame on the CLIENT. Captures the input, advances the
    // local simulation by one tick, and stashes the (tick, input) pair
    // for later reconciliation.
    void recordInput(const void* inputBlob, int inputSize) {
        m_inputBuffer.push(m_currentTick, inputBlob, inputSize);
        m_currentTick++;
        // The caller is responsible for actually stepping the simulation.
        // We just track the tick + input here.
    }

    // Called when a server snapshot arrives. Replays all inputs newer
    // than snapshot.tick, snapping the predicted state to the snapshot
    // first.
    void onSnapshot(uint32_t snapshotTick, const NetTransformComponent::Snapshot& snap) {
        // 1. Find the entry in the input buffer with tick >= snapshotTick.
        // 2. Set the local state to the snapshot's transform.
        // 3. Replay each input from there to the current tick.
        // (Stub: real impl needs to call back into the gameplay code to
        // step the simulation. We just log.)
        TD_LOG_INFO("ClientPrediction: snapshot @tick=%u (replay stub)", snapshotTick);
        (void)snap;
    }

    uint32_t currentTick() const { return m_currentTick; }

private:
    uint32_t     m_currentTick = 0;
    InputBuffer  m_inputBuffer;
};

// =============================================================================
// Lag compensation for hit detection (server-side).
//
// When the server receives a "I shot at tick T" request from a client, the
// server has already advanced past tick T. To make hits feel fair, the
// server REWINDS its world state to tick T, performs the hit test there,
// then snaps back to the current tick.
//
// This requires keeping a per-tick history of every relevant entity's
// transform. LagCompensationHistory stores that ring buffer.
// =============================================================================
class LagCompensationHistory {
public:
    void recordTick(uint32_t tick, EntityId id, float x, float y, float z) {
        (void)tick; (void)id; (void)x; (void)y; (void)z;
        // Stub: push (tick, id, transform) into a ring buffer indexed by tick % SIZE.
    }

    // Returns the transform of `id` at tick `tick`, or false if not in history.
    bool sampleAt(uint32_t tick, EntityId id, float& outX, float& outY, float& outZ) {
        (void)tick; (void)id;
        outX = outY = outZ = 0;
        return false;  // stub
    }
};

} // namespace td
