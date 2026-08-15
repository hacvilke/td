// =============================================================================
// TD Engine - MockNetPeer (concrete NetPeer for unit tests)
//
// A loopback peer that never touches the network. All sends are queued in
// an internal channel; the test thread pumps them out by calling drain()
// on the receiver. This lets unit tests verify the RPC + JsonRPC layers
// end-to-end without sockets, threads, or timing flakiness.
//
// Usage:
//   td::MockNetPeer server, client;
//   server.connectTo(&client);   // bidirectional bridge
//   td::RpcServer::get().setPeer(&server);
//   td::RpcServer::get().registerMethod("ping", [](int peerId, const char* args) {
//       td::RpcServer::get().sendResponse(/*callId from frame*/, "\"pong\"", peerId);
//   });
//   client.sendText(td::net::makeRequest(1, "ping", "[]"));
//   server.drain();   // server processes the request, sends a response
//   client.drain();   // client receives the response
//
// Status: REAL. Used by tests/test_net_json_rpc.cpp.
// =============================================================================
#pragma once
#include "transport.h"
#include "json_rpc.h"

#include <chrono>
#include <cstring>
#include <string>
#include <vector>

namespace td {

class MockNetPeer : public NetPeer {
public:
    MockNetPeer() = default;
    ~MockNetPeer() override { disconnect(); }

    // Connect two peers bidirectionally. Each peer's send() is delivered to
    // the other's receive queue, drained by drain().
    void connectTo(MockNetPeer* other) {
        m_partner = other;
        if (other) other->m_partner = this;
        m_isConnected = true;
        m_localPeerId = m_isHost ? 1 : 2;
        if (other) {
            other->m_isConnected = true;
            other->m_localPeerId = other->m_isHost ? 1 : 2;
        }
    }

    // --- NetPeer interface --------------------------------------------------

    bool host(int /*port*/, int /*maxPeers*/) override {
        m_isHost = true;
        m_localPeerId = 1;
        return true;
    }

    bool connect(const char* /*host*/, int /*port*/) override {
        m_isHost = false;
        m_localPeerId = 2;
        m_isConnected = true;
        return true;
    }

    void disconnect() override {
        m_isConnected = false;
        if (m_partner) {
            m_partner->m_isConnected = false;
            m_partner = nullptr;
        }
    }

    // We don't pump anything automatically; the test calls drain().
    void poll() override { /* no-op */ }

    bool send(const NetPacket& packet) override {
        if (!m_partner) return false;
        if (!packet.data || packet.size <= 0) return false;
        // Copy the bytes — the caller may free them after send() returns.
        std::vector<uint8_t> buf(static_cast<size_t>(packet.size));
        std::memcpy(buf.data(), packet.data, static_cast<size_t>(packet.size));
        m_partner->m_inbox.push_back(std::move(buf));
        return true;
    }

    NetPeerType type() const override { return NetPeerType::Mock; }
    bool isHost() const override { return m_isHost; }
    int  localPeerId() const override { return m_localPeerId; }
    int  connectedPeerCount() const override {
        return (m_partner && m_isConnected) ? 1 : 0;
    }

    // --- Test-only helpers --------------------------------------------------

    // Deliver all queued packets to the RpcServer via dispatchPacket.
    // Returns the number of packets processed.
    int drain() {
        int n = 0;
        while (!m_inbox.empty()) {
            auto buf = std::move(m_inbox.front());
            m_inbox.erase(m_inbox.begin());
            RpcServer::get().dispatchPacket(/*peerId*/ m_isHost ? 2 : 1,
                                             buf.data(), static_cast<int>(buf.size()));
            ++n;
        }
        return n;
    }

    // Direct text send (bypasses the JSON encoder). Useful for simulating
    // incoming JS frames verbatim.
    bool sendText(const std::string& s) {
        if (!m_partner) return false;
        m_partner->m_inbox.emplace_back(s.begin(), s.end());
        return true;
    }

    // Direct binary send (for tests that want to inject malformed frames).
    bool sendBytes(const uint8_t* data, int len) {
        if (!m_partner || !data || len <= 0) return false;
        m_partner->m_inbox.emplace_back(data, data + len);
        return true;
    }

    // Peek at the inbox (for tests that want to verify a frame was queued).
    size_t inboxSize() const { return m_inbox.size(); }

    // Pump timeouts. TheRpcServer::update() expects a monotonic-ms clock.
    // We expose a hook so tests can advance time deterministically.
    static uint32_t nowMs() {
        static auto start = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        return static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
    }

private:
    MockNetPeer* m_partner = nullptr;
    bool m_isHost = false;
    bool m_isConnected = false;
    int  m_localPeerId = 0;
    std::vector<std::vector<uint8_t>> m_inbox;
};

} // namespace td
