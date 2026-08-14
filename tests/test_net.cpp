// =============================================================================
// TD Engine - Network transport + server-authoritative netcode tests.
//
// Tests:
//   1. Endpoint parse/format round-trip (IPv4).
//   2. Two NetworkInterfaces on localhost; send UNRELIABLE -> arrives once.
//   3. RELIABLE_ORDERED large message (256KB) -> arrives exactly once, in order.
//   4. RPC ping/pong: client calls "ping" with "hi", server replies "pong",
//      client receives "pong".
//   5. RPC timeout: call nonexistent RPC, expect rejection after timeout_ms.
//   6. MessageReader/Writer round-trip (varint, fixed32, string, bytes, tags).
//   7. ClientPredictor: 3 frames of input, snapshot matches -> no rewind.
//      Then a divergent snapshot -> rewind + replay fires.
//   8. ServerReconciler: receives inputs, advances simulation, broadcasts
//      snapshots at 20Hz, acks inputs.
//   9. LagCompensator: records entity transforms per tick, rewinds for
//      hitscan, returns the correct hit.
//
// Build:
//   g++ -std=c++17 -Wall -Wextra -O2 -Isrc -DTEST_STUB_LOGGER
//       tests/test_net.cpp tests/stub_logger.cpp
//       src/net/transport.cpp src/net/server_authoritative.cpp
//       -o /tmp/test_net
//
// Run:
//   /tmp/test_net  (exits 0 on success, 1 on failure)
// =============================================================================

#include "net/transport_impl.h"
#include "net/server_authoritative_impl.h"
#include "core/logger.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using td::net::Endpoint;
using td::net::Socket;
using td::net::ReliableChannel;
using td::net::ReliabilityMode;
using td::net::Connection;
using td::net::MessageReader;
using td::net::MessageWriter;
using td::net::RPC;
using td::net::NetworkInterface;
using td::net::Input;
using td::net::Transform;
using td::net::Snapshot;
using td::net::ClientPredictor;
using td::net::ServerReconciler;
using td::net::LagCompensator;

static int g_failures = 0;
static int g_passes   = 0;

#define CHECK(cond, ...) do { \
    if (cond) { \
        ++g_passes; \
        std::printf("PASS: " __VA_ARGS__); \
        std::printf("\n"); \
    } else { \
        ++g_failures; \
        std::fprintf(stderr, "FAIL: " __VA_ARGS__); \
        std::fprintf(stderr, "\n"); \
        std::fprintf(stderr, "      (%s:%d)\n", __FILE__, __LINE__); \
    } \
} while (0)

// Sleep helper. Uses std::this_thread::sleep_for so we don't busy-wait.
static void sleepMs(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Drain both interfaces for up to `maxMs` milliseconds, calling poll() in a
// tight loop. Used to let packets flow between the two ends.
static void pumpBoth(NetworkInterface& a, NetworkInterface& b, uint32_t maxMs) {
    uint32_t elapsed = 0;
    const uint32_t step = 5;
    while (elapsed < maxMs) {
        a.poll(0);
        b.poll(0);
        sleepMs(step);
        elapsed += step;
    }
}

// =============================================================================
// Test 1: Endpoint parse/format round-trip
// =============================================================================
static void test_endpoint() {
    std::printf("\n--- Test 1: Endpoint parse/format ---\n");

    Endpoint ep;
    bool ok = ep.fromString("127.0.0.1:18001");
    CHECK(ok, "Endpoint::fromString('127.0.0.1:18001') succeeded");
    CHECK(ep.isIPv4(), "endpoint is IPv4");
    CHECK(!ep.isIPv6(), "endpoint is not IPv6");
    CHECK(ep.port() == 18001, "port is 18001 (got %u)", ep.port());
    std::string s = ep.toString();
    CHECK(s == "127.0.0.1:18001", "toString round-trips ('%s')", s.c_str());

    Endpoint ep2;
    ok = ep2.fromString("localhost:18002");
    CHECK(ok, "Endpoint::fromString('localhost:18002') resolves hostname");
    CHECK(ep2.port() == 18002, "port is 18002 (got %u)", ep2.port());

    Endpoint epBad;
    ok = epBad.fromString("not_a_valid_endpoint");
    CHECK(!ok, "Endpoint::fromString rejects garbage");

    // Equality
    Endpoint a, b;
    a.fromString("127.0.0.1:18001");
    b.fromString("127.0.0.1:18001");
    CHECK(a == b, "equal endpoints compare equal");
    Endpoint c;
    c.fromString("127.0.0.1:18002");
    CHECK(a != c, "different ports compare unequal");
}

// =============================================================================
// Test 2: Two NIs; UNRELIABLE send -> arrives once
// =============================================================================
static void test_unreliable() {
    std::printf("\n--- Test 2: UNRELIABLE send -> arrives once ---\n");

    NetworkInterface niA, niB;
    CHECK(niA.init(18001), "NI A opened on port 18001");
    CHECK(niB.init(18002), "NI B opened on port 18002");

    Endpoint bEp;
    bEp.fromString("127.0.0.1:18002");

    std::atomic<int> recvCount{0};
    std::string recvData;
    niB.onMessage([&](const Endpoint& /*from*/, const uint8_t* data, int len,
                      ReliabilityMode mode) {
        recvCount++;
        recvData.assign(reinterpret_cast<const char*>(data),
                        static_cast<size_t>(len));
        CHECK(mode == ReliabilityMode::Unreliable,
              "received message mode is Unreliable");
    });

    const char* msg = "hello_unreliable";
    bool sent = niA.send(bEp, ReliabilityMode::Unreliable, msg,
                         static_cast<int>(std::strlen(msg)));
    CHECK(sent, "send returned true");

    pumpBoth(niA, niB, 100);

    CHECK(recvCount.load() == 1, "received exactly 1 unreliable message (got %d)",
          recvCount.load());
    CHECK(recvData == "hello_unreliable",
          "received payload matches ('%s')", recvData.c_str());

    niA.shutdown();
    niB.shutdown();
}

// =============================================================================
// Test 3: RELIABLE_ORDERED large message (256KB) -> arrives exactly once
// =============================================================================
static void test_large_ordered() {
    std::printf("\n--- Test 3: RELIABLE_ORDERED 256KB message ---\n");

    NetworkInterface niA, niB;
    CHECK(niA.init(18003), "NI A opened on port 18003");
    CHECK(niB.init(18004), "NI B opened on port 18004");

    Endpoint bEp;
    bEp.fromString("127.0.0.1:18004");

    // Build a 256KB message with a recognizable pattern.
    const int MSG_LEN = 256 * 1024;
    std::vector<uint8_t> msg(MSG_LEN);
    for (int i = 0; i < MSG_LEN; i++) {
        msg[i] = static_cast<uint8_t>((i * 7 + 3) & 0xFF);
    }

    std::atomic<int> recvCount{0};
    std::vector<uint8_t> recvData;
    bool dataMatches = false;
    niB.onMessage([&](const Endpoint& /*from*/, const uint8_t* data, int len,
                      ReliabilityMode mode) {
        recvCount++;
        recvData.assign(data, data + len);
        dataMatches = (len == MSG_LEN) &&
                      (std::memcmp(data, msg.data(), static_cast<size_t>(len)) == 0);
        CHECK(mode == ReliabilityMode::ReliableOrdered,
              "received message mode is ReliableOrdered");
    });

    bool sent = niA.send(bEp, ReliabilityMode::ReliableOrdered, msg.data(),
                         MSG_LEN);
    CHECK(sent, "send returned true for 256KB message");

    // Large messages need retransmits; give it up to 2 seconds.
    pumpBoth(niA, niB, 2000);

    CHECK(recvCount.load() == 1, "received exactly 1 message (got %d)",
          recvCount.load());
    CHECK(dataMatches, "received data matches sent data byte-for-byte");
    CHECK(static_cast<int>(recvData.size()) == MSG_LEN,
          "received length is %d (expected %d)",
          static_cast<int>(recvData.size()), MSG_LEN);

    niA.shutdown();
    niB.shutdown();
}

// =============================================================================
// Test 4: RPC ping/pong
// =============================================================================
static void test_rpc_ping() {
    std::printf("\n--- Test 4: RPC ping/pong ---\n");

    NetworkInterface niA, niB;
    CHECK(niA.init(18005), "NI A opened on port 18005");
    CHECK(niB.init(18006), "NI B opened on port 18006");

    Endpoint bEp;
    bEp.fromString("127.0.0.1:18006");

    // Server (niB) registers a "ping" handler that replies "pong".
    // We write "pong" as raw bytes (no length prefix) so the client receives
    // exactly the 4-byte string.
    niB.rpc().registerHandler("ping",
        [](MessageReader& /*req*/, MessageWriter& resp) {
            resp.writeBytes("pong", 4);
        });

    // Client (niA) calls "ping" with arg "hi".
    std::atomic<bool> gotReply{false};
    std::atomic<bool> replyOk{false};
    std::string replyStr;
    std::string argStr = "hi";
    niA.rpc().callString(bEp, "ping", argStr, 5000,
        [&](bool ok, const std::string& resp) {
            gotReply = true;
            replyOk = ok;
            replyStr = resp;
        });

    // Pump for up to 2 seconds.
    pumpBoth(niA, niB, 2000);

    CHECK(gotReply.load(), "client received a reply");
    CHECK(replyOk.load(), "reply was success (ok=true)");
    CHECK(replyStr == "pong", "reply payload is 'pong' (got '%s')",
          replyStr.c_str());

    niA.shutdown();
    niB.shutdown();
}

// =============================================================================
// Test 5: RPC timeout
// =============================================================================
static void test_rpc_timeout() {
    std::printf("\n--- Test 5: RPC timeout ---\n");

    NetworkInterface niA, niB;
    CHECK(niA.init(18007), "NI A opened on port 18007");
    CHECK(niB.init(18008), "NI B opened on port 18008");

    Endpoint bEp;
    bEp.fromString("127.0.0.1:18008");

    // Server registers NO handlers. Client calls a nonexistent RPC.
    std::atomic<bool> gotReply{false};
    std::atomic<bool> replyOk{true};  // we expect ok=false
    auto t0 = std::chrono::steady_clock::now();
    niA.rpc().callString(bEp, "nonexistent", "x", 300,
        [&](bool ok, const std::string& /*resp*/) {
            gotReply = true;
            replyOk.store(ok);
        });

    // Pump for up to 1 second (well beyond the 300ms timeout).
    pumpBoth(niA, niB, 1000);
    auto t1 = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    CHECK(gotReply.load(), "client received a (timeout) reply");
    CHECK(!replyOk.load(), "reply was rejection (ok=false)");
    CHECK(elapsedMs >= 250,
          "timeout fired after >= 250ms (got %lld ms)", (long long)elapsedMs);
    CHECK(elapsedMs < 1500,
          "timeout fired within 1.5s (got %lld ms)", (long long)elapsedMs);

    niA.shutdown();
    niB.shutdown();
}

// =============================================================================
// Test 6: MessageReader/Writer round-trip
// =============================================================================
static void test_message_io() {
    std::printf("\n--- Test 6: MessageReader/Writer round-trip ---\n");

    MessageWriter w;
    w.writeVarintField(1, 150);
    w.writeStringField(2, "hello world");
    w.writeFixed32Field(3, 0xDEADBEEF);
    w.writeFixed64Field(4, 0x0123456789ABCDEFull);
    w.writeBytesField(5, "\x00\x01\x02\x03\x04", 5);

    MessageReader r(w.data(), w.size());
    uint32_t tag, wireType;
    uint64_t v;
    uint32_t f32;
    uint64_t f64;
    std::string s;

    CHECK(r.readTag(tag, wireType), "read tag 1");
    CHECK(tag == 1 && wireType == 0, "tag=1 wire=0 (varint)");
    CHECK(r.readVarint(v) && v == 150, "varint value is 150 (got %llu)",
          (unsigned long long)v);

    CHECK(r.readTag(tag, wireType), "read tag 2");
    CHECK(tag == 2 && wireType == 2, "tag=2 wire=2 (length-delimited)");
    CHECK(r.readString(s) && s == "hello world", "string is 'hello world'");

    CHECK(r.readTag(tag, wireType), "read tag 3");
    CHECK(tag == 3 && wireType == 5, "tag=3 wire=5 (fixed32)");
    CHECK(r.readFixed32(f32) && f32 == 0xDEADBEEF, "fixed32 is 0xDEADBEEF");

    CHECK(r.readTag(tag, wireType), "read tag 4");
    CHECK(tag == 4 && wireType == 1, "tag=4 wire=1 (fixed64)");
    CHECK(r.readFixed64(f64) && f64 == 0x0123456789ABCDEFull,
          "fixed64 is 0x0123456789ABCDEF");

    CHECK(r.readTag(tag, wireType), "read tag 5");
    CHECK(tag == 5 && wireType == 2, "tag=5 wire=2 (bytes)");
    uint64_t blen;
    CHECK(r.readVarint(blen) && blen == 5, "bytes length is 5");
    uint8_t bbuf[5];
    CHECK(r.readBytes(bbuf, 5), "read 5 bytes");
    CHECK(bbuf[0] == 0x00 && bbuf[4] == 0x04, "bytes content matches");

    CHECK(r.atEnd(), "reader at end after consuming all fields");
}

// =============================================================================
// Test 7: ClientPredictor — match + misprediction paths
// =============================================================================
static void test_client_predictor() {
    std::printf("\n--- Test 7: ClientPredictor ---\n");

    // A toy simulation: a single entity at (x, y, 0). Each input advances x
    // by axisX * 1.0 and y by axisY * 1.0.
    Transform localTransform{};
    localTransform.x = 0; localTransform.y = 0; localTransform.z = 0;

    ClientPredictor cp;
    cp.init(/*startFrame=*/0,
        /*step*/    [&](const Input& in) {
            localTransform.x += in.axisX;
            localTransform.y += in.axisY;
        },
        /*get*/     [&](uint32_t /*id*/) -> Transform {
            return localTransform;
        },
        /*set*/     [&](uint32_t /*id*/, const Transform& t) {
            localTransform = t;
        });

    // Simulate 3 frames of input locally.
    Input in0{}, in1{}, in2{};
    in0.frame = 0; in0.axisX = 1.0f; in0.axisY = 0.0f;
    in1.frame = 1; in1.axisX = 1.0f; in1.axisY = 0.0f;
    in2.frame = 2; in2.axisX = 1.0f; in2.axisY = 0.0f;
    cp.recordInput(in0);
    cp.recordInput(in1);
    cp.recordInput(in2);

    // After 3 frames, local x should be 3.
    CHECK(localTransform.x == 3.0f, "predicted x = 3.0 after 3 frames (got %f)",
          localTransform.x);
    CHECK(cp.currentFrame() == 3, "currentFrame is 3");
    CHECK(cp.ringCount() == 3, "ring has 3 entries");

    // Server sends a snapshot at frame 2 that MATCHES our prediction.
    // Our predicted x at frame 2 is 3.0 (after applying 3 inputs at frames 0,1,2).
    // Wait — frame 2 means after applying inputs for frames 0, 1, 2. So x=3.
    Snapshot snap;
    snap.frame = 2;
    Transform authoritative{};
    authoritative.x = 3.0f; authoritative.y = 0.0f; authoritative.z = 0.0f;
    snap.set(/*myEntityId=*/1, authoritative);

    cp.onSnapshot(snap, /*myEntityId=*/1);
    CHECK(!cp.hadMisprediction(),
          "no misprediction when snapshot matches");
    CHECK(cp.mispredictionCount() == 0, "misprediction count is 0");
    CHECK(cp.lastAckedFrame() == 2, "last acked frame is 2");

    // Now send a DIVERGENT snapshot at frame 2: x=10 instead of 3.
    Snapshot snap2;
    snap2.frame = 2;
    Transform divergent{};
    divergent.x = 10.0f; divergent.y = 0.0f; divergent.z = 0.0f;
    snap2.set(1, divergent);

    int replayedBefore = cp.replayedInputCount();
    cp.onSnapshot(snap2, /*myEntityId=*/1);
    CHECK(cp.hadMisprediction(),
          "misprediction detected when snapshot diverges");
    CHECK(cp.mispredictionCount() == 1, "misprediction count is 1");
    CHECK(localTransform.x == 10.0f,
          "local transform snapped to authoritative x=10 (got %f)",
          localTransform.x);
    // We should have replayed the input at frame >2 (just frame 2's input? No —
    // frame 2's input was already applied. Replay from frame > snap.frame, so
    // frame 3+ — but we have no inputs at frame 3+. So 0 replays.
    // Actually our ring has frames 0,1,2 — replayFrom(2) replays frames >2,
    // which is none. So replayedInputs doesn't change.
    CHECK(cp.replayedInputCount() == replayedBefore,
          "replay count unchanged when no inputs newer than snapshot (got %d)",
          cp.replayedInputCount());

    // Now do a divergent snapshot at an OLDER frame (frame 1), with a value
    // different from the current local state (which is x=10). The predictor
    // should snap to the authoritative x and replay the input at frame 2.
    Snapshot snap3;
    snap3.frame = 1;
    Transform diverge2{};
    diverge2.x = 5.0f; diverge2.y = 0.0f; diverge2.z = 0.0f;
    snap3.set(1, diverge2);

    int replayedBefore2 = cp.replayedInputCount();
    cp.onSnapshot(snap3, 1);
    CHECK(cp.hadMisprediction(), "misprediction on divergent snapshot at frame 1");
    CHECK(cp.mispredictionCount() == 2, "misprediction count is 2");
    // Snap to x=5, then replay inputs for frame >1 (just frame 2: axisX=1).
    // So x = 5 + 1 = 6.
    CHECK(localTransform.x == 6.0f,
          "after rewind + replay, x = 6 (snapped to 5 + replayed frame 2) (got %f)",
          localTransform.x);
    CHECK(cp.replayedInputCount() > replayedBefore2,
          "replay count incremented after divergent snapshot at older frame");
}

// =============================================================================
// Test 8: ServerReconciler
// =============================================================================
static void test_server_reconciler() {
    std::printf("\n--- Test 8: ServerReconciler ---\n");

    // A toy authoritative sim: one entity per client. Each input advances
    // the client's entity by (axisX, axisY).
    std::unordered_map<uint32_t, Transform> serverEntities;
    uint32_t lastBroadcastFrame = 0;
    int broadcastCount = 0;

    ServerReconciler sr;
    sr.init(
        /*step*/     [&](uint32_t clientId, const Input& in) {
            Transform& t = serverEntities[clientId];
            t.x += in.axisX;
            t.y += in.axisY;
        },
        /*build*/    [&]() -> Snapshot {
            Snapshot s;
            for (const auto& kv : serverEntities) {
                s.set(kv.first, kv.second);
            }
            return s;
        },
        /*broadcast*/ [&](const Snapshot& s) {
            lastBroadcastFrame = s.frame;
            broadcastCount++;
        });

    // Client 1 sends 3 inputs at frames 0, 1, 2.
    Input in0{}, in1{}, in2{};
    in0.frame = 0; in0.axisX = 1.0f;
    in1.frame = 1; in1.axisX = 1.0f;
    in2.frame = 2; in2.axisX = 1.0f;
    sr.onInput(/*clientId=*/1, in0);
    sr.onInput(1, in1);
    sr.onInput(1, in2);

    // Tick once. All 3 inputs should be processed.
    int processed = sr.tick(/*nowMs=*/50);
    CHECK(processed == 3, "server processed 3 inputs in one tick (got %d)",
          processed);
    CHECK(serverEntities[1].x == 3.0f, "client 1 entity x = 3.0 (got %f)",
          serverEntities[1].x);
    CHECK(sr.lastAckedFrame(1) == 2, "server acked frame 2 for client 1");

    // First tick also broadcasts a snapshot (snapshotDue returns true when
    // m_lastSnapshotMs == 0).
    CHECK(broadcastCount == 1, "snapshot broadcast once after first tick");
    CHECK(lastBroadcastFrame == 3, "snapshot frame is 3 (current frame)");

    // Tick again immediately — no new inputs, but a snapshot is due only
    // after SNAPSHOT_INTERVAL_MS (50ms). So no new broadcast.
    int processed2 = sr.tick(/*nowMs=*/60);
    CHECK(processed2 == 0, "no new inputs to process");
    CHECK(broadcastCount == 1, "no new broadcast (within interval)");

    // After 50ms+ a new snapshot is due.
    int processed3 = sr.tick(/*nowMs=*/120);
    CHECK(processed3 == 0, "still no new inputs");
    CHECK(broadcastCount == 2, "new snapshot broadcast after interval");

    // Duplicate input (frame 1, already processed) is dropped.
    sr.onInput(1, in1);
    int processed4 = sr.tick(/*nowMs=*/130);
    CHECK(processed4 == 0, "duplicate input is dropped");
    CHECK(serverEntities[1].x == 3.0f, "entity unchanged after dup input");
}

// =============================================================================
// Test 9: LagCompensator
// =============================================================================
static void test_lag_compensator() {
    std::printf("\n--- Test 9: LagCompensator ---\n");

    LagCompensator lc;

    // Record an entity at ticks 0..5, moving along +x.
    for (uint32_t t = 0; t <= 5; t++) {
        Transform tr;
        tr.x = static_cast<float>(t);  // x = 0, 1, 2, 3, 4, 5
        tr.y = 0; tr.z = 0;
        lc.recordTick(t, /*entityId=*/42, tr);
    }

    // Sample at tick 3 should return x=3.
    Transform out;
    bool found = lc.sampleAt(3, 42, out);
    CHECK(found, "sampleAt(tick=3, entity=42) found");
    CHECK(out.x == 3.0f, "entity 42 at tick 3 has x=3 (got %f)", out.x);

    // Sample at a tick not recorded should fail.
    Transform out2;
    bool found2 = lc.sampleAt(99, 42, out2);
    CHECK(!found2, "sampleAt(tick=99) returns false (not in history)");

    // Sample an entity not recorded should fail.
    Transform out3;
    bool found3 = lc.sampleAt(3, /*entityId=*/999, out3);
    CHECK(!found3, "sampleAt(entity=999) returns false (entity not recorded)");

    // Hitscan: at tick 2, entity 42 is at x=2. Ray from (0,0,0) dir (1,0,0)
    // should hit a sphere of radius 0.5 around (2,0,0).
    bool hit = lc.hitscanAt(/*tick=*/2,
                            /*ox*/0, /*oy*/0, /*oz*/0,
                            /*dx*/1, /*dy*/0, /*dz*/0,
                            /*target*/42, /*radius*/0.5f, /*maxDist*/100.0f);
    CHECK(hit, "hitscan at tick 2 hits entity 42");

    // Hitscan in the opposite direction should miss.
    bool hit2 = lc.hitscanAt(2, 0,0,0, -1,0,0, 42, 0.5f, 100.0f);
    CHECK(!hit2, "hitscan in -x direction misses");

    // Hitscan beyond maxDist should miss.
    bool hit3 = lc.hitscanAt(2, 0,0,0, 1,0,0, 42, 0.5f, /*maxDist*/1.0f);
    CHECK(!hit3, "hitscan with maxDist=1.0 misses entity at x=2");

    // hitscanClosest: record two entities, find the closer one.
    LagCompensator lc2;
    Transform e1{};  e1.x = 5; e1.y = 0; e1.z = 0;
    Transform e2{};  e2.x = 10; e2.y = 0; e2.z = 0;
    lc2.recordTick(0, 1, e1);
    lc2.recordTick(0, 2, e2);
    uint32_t closest = lc2.hitscanClosest(0, 0,0,0, 1,0,0, /*radius*/1.0f, 100.0f);
    CHECK(closest == 1, "hitscanClosest returns entity 1 (closer)");

    // History overflow: record more than HISTORY_SIZE ticks; oldest should
    // be evicted.
    LagCompensator lc3;
    for (uint32_t t = 0; t < LagCompensator::HISTORY_SIZE + 10; t++) {
        Transform tr;
        tr.x = static_cast<float>(t);
        lc3.recordTick(t, 42, tr);
    }
    // The oldest 10 ticks should be gone.
    Transform outOld;
    bool foundOld = lc3.sampleAt(5, 42, outOld);
    CHECK(!foundOld, "oldest tick evicted after overflow");
    Transform outNew;
    bool foundNew = lc3.sampleAt(LagCompensator::HISTORY_SIZE + 9, 42, outNew);
    CHECK(foundNew, "newest tick still in history after overflow");
    CHECK(outNew.x == static_cast<float>(LagCompensator::HISTORY_SIZE + 9),
          "newest tick has correct x");
}

// =============================================================================
// Test 10: ReliableChannel dedup (reliable-ordered duplicate suppression)
// =============================================================================
static void test_reliable_dedup() {
    std::printf("\n--- Test 10: ReliableChannel dedup + reorder ---\n");

    // Set up two NIs. Send the same RELIABLE_ORDERED message twice (same
    // payload). The receiver should deliver each ONCE (no dedup across
    // distinct messages, but a re-arrival of the SAME packet is dedup'd).
    NetworkInterface niA, niB;
    CHECK(niA.init(18009), "NI A opened on port 18009");
    CHECK(niB.init(18010), "NI B opened on port 18010");

    Endpoint bEp;
    bEp.fromString("127.0.0.1:18010");

    std::atomic<int> recvCount{0};
    niB.onMessage([&](const Endpoint&, const uint8_t*, int, ReliabilityMode) {
        recvCount++;
    });

    const char* m1 = "msg1";
    const char* m2 = "msg2";
    niA.send(bEp, ReliabilityMode::ReliableOrdered, m1, 4);
    niA.send(bEp, ReliabilityMode::ReliableOrdered, m2, 4);

    pumpBoth(niA, niB, 500);

    CHECK(recvCount.load() == 2, "received exactly 2 distinct messages (got %d)",
          recvCount.load());

    niA.shutdown();
    niB.shutdown();
}

// =============================================================================
// Main
// =============================================================================
int main() {
    std::printf("TD Engine - Network transport + server-authoritative tests\n");
    std::printf("============================================================\n");

    test_endpoint();
    test_unreliable();
    test_large_ordered();
    test_rpc_ping();
    test_rpc_timeout();
    test_message_io();
    test_client_predictor();
    test_server_reconciler();
    test_lag_compensator();
    test_reliable_dedup();

    std::printf("\n============================================================\n");
    std::printf("Results: %d passed, %d failed\n", g_passes, g_failures);
    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("SOME TESTS FAILED\n");
    return 1;
}
