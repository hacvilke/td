// =============================================================================
// TD Engine - Network Transport (Tier 1.5)
//
// Pluggable transport layer for multiplayer. Mirrors Godot's MultiplayerPeer
// abstraction: the high-level RPC layer talks to a NetPeer interface, and
// concrete implementations (ENet for desktop, WebRTC for WASM) plug in below.
//
// Why this design:
//   - Browsers cannot use raw UDP. The WASM target MUST use WebRTC data
//     channels. Native targets can use either ENet (lower latency, no NAT
//     traversal needed on LAN) or WebRTC (matches the WASM path).
//   - By keeping the high-level RPC layer transport-agnostic, the same
//     gameplay code runs on both targets without #ifdefs.
//   - The interface is small enough (8 methods) that a new transport
//     (e.g. SteamNetworkingSockets) can be added in <100 LOC.
//
// Status: REAL. The NetPeer ABC is in this file. The concrete UDP
// transport (Socket + ReliableChannel + Connection + NetworkInterface) is
// in transport_impl.h + transport.cpp. The JSON-RPC framing that matches
// the JS TDNet.RPC wire format is in json_rpc.h + json_rpc.cpp. A
// MockNetPeer (loopback, for unit tests) is in mock_peer.h.
//
// Concrete peer TODOs:
//   - WebSocketPeer  (for native clients talking to a JS server)  — TODO
//   - ENetPeer       (raw UDP, native-only, lower latency)        — TODO
// Browsers cannot use raw UDP, so the WASM path always goes through
// WebSocketPeer (or the JS TDNet.Socket directly).
// =============================================================================
#pragma once
#include "../core/logger.h"
#include "../core/signal.h"
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace td {

enum class NetPeerType : uint8_t {
    None    = 0,
    ENet    = 1,  // native desktop, reliable UDP
    WebRTC  = 2,  // WASM + native, browser-friendly
    Mock    = 3,  // in-process loopback for tests
};

enum class NetReliability : uint8_t {
    Unreliable,        // fire-and-forget (e.g. position updates)
    UnreliableOrdered, // latest-wins per channel (e.g. velocity)
    Reliable,          // TCP-like (e.g. RPC calls, chat)
};

struct NetPacket {
    NetReliability reliability = NetReliability::Reliable;
    int            channel     = 0;        // 0-31, app-defined
    const void*    data        = nullptr;
    int            size        = 0;        // bytes
    int            targetPeer  = -1;       // -1 = broadcast, 1+ = specific peer
};

struct NetPeerEvent {
    enum class Kind : uint8_t {
        Connected,        // a remote peer connected (peerId set)
        Disconnected,     // a remote peer disconnected (peerId set)
        PacketReceived,   // a packet arrived (packet set)
        Error,            // transport error (message set)
    };
    Kind         kind = Kind::Error;
    int          peerId = -1;
    NetPacket    packet;
    const char*  message = nullptr;
};

// NetPeer is the abstract transport. Concrete implementations:
//   - ENetPeer      (src/net/enet_peer.h/.cpp — TODO Tier 1.5)
//   - WebRTCPeer    (src/net/webrtc_peer.h/.cpp — TODO Tier 1.5, WASM)
//   - MockPeer      (src/net/mock_peer.h/.cpp — for unit tests, loopback)
class NetPeer {
public:
    virtual ~NetPeer() = default;

    // --- Lifecycle ----------------------------------------------------------
    // Host a server on the given port. Returns true on success.
    virtual bool host(int port, int maxPeers) = 0;

    // Connect to a remote host:port. Returns true if the connection attempt
    // started (the actual Connected event fires later via poll()).
    virtual bool connect(const char* host, int port) = 0;

    // Disconnect from all peers and close the socket.
    virtual void disconnect() = 0;

    // --- Per-frame pump -----------------------------------------------------
    // Pumps the transport's event queue. Events are delivered via the
    // onEvent callback. Must be called once per frame from the main thread.
    virtual void poll() = 0;

    // --- Sending ------------------------------------------------------------
    // Send a packet. Returns true if the packet was queued for send.
    // The buffer is copied (or refcounted) by the implementation; the
    // caller may free it immediately after send() returns.
    virtual bool send(const NetPacket& packet) = 0;

    // --- Subscription -------------------------------------------------------
    // Register a callback for peer/packet events. Multiple callbacks can be
    // registered; they're all invoked per event. The callback runs on the
    // main thread, inside poll().
    //
    // The SignalBus is used so scripts can subscribe to "net:packet_received"
    // etc. without a direct dependency on the NetPeer type.
    virtual void onEvent(/* std::function<void(const NetPeerEvent&)> cb */) {
        // Default impl: no-op. Concrete peers wire the SignalBus events:
        //   "net:peer_connected"    (intValue = peerId)
        //   "net:peer_disconnected" (intValue = peerId)
        //   "net:packet_received"   (ptrValue = NetPacket*; intValue = peerId)
        //   "net:error"             (strValue = message)
    }

    // --- Introspection ------------------------------------------------------
    virtual NetPeerType type() const = 0;
    virtual bool isHost() const = 0;
    virtual int  localPeerId() const = 0;     // 1 for host, 2+ for clients
    virtual int  connectedPeerCount() const = 0;
};

// =============================================================================
// High-level RPC layer (JSON wire format, matches JS TDNet.RPC).
//
// Wire format (identical to web/net_websocket.js TDNet.RPC):
//
//   Request:  {"id":123,"m":"methodName","a":[arg1,arg2,...]}
//   Response: {"id":123,"r":<result>}
//   Error:    {"id":123,"e":"error message"}
//   Notify:   {"m":"methodName","a":[...]}      (no id = no reply)
//
// Gameplay scripts register a handler:
//
//   td::RpcServer::get().registerMethod("player:say",
//       [](int peerId, const char* argsJson) {
//           // argsJson is e.g. `["hello", 42]` — decode however you want
//       });
//
// And call a remote method:
//
//   td::RpcServer::get().callRemote("player:say", "[\"hello\",42]",
//                                    /*targetPeer=*/-1);
//
// For request/response semantics (with a callback when the reply arrives),
// use callWithReply() instead. It tracks the call ID, matches the response,
// and invokes the callback on the main thread inside poll().
//
// The wire format is parsed by json_rpc.h. The transport is whatever
// NetPeer you bound via setPeer() — WebSocketPeer, MockNetPeer, etc.
// =============================================================================
class RpcServer {
public:
    static RpcServer& get() {
        static RpcServer instance;
        return instance;
    }

    // Bind a NetPeer to the RPC layer. All callRemote() invocations route
    // through this peer; all incoming packets are dispatched to registered
    // methods. Pass nullptr to disable networking.
    void setPeer(NetPeer* peer) { m_peer = peer; }

    // Register a method handler. Multiple handlers per method name are
    // allowed (all are invoked).
    void registerMethod(const char* name,
                        std::function<void(int peerId,
                                            const char* argsJson)> handler);

    // Fire-and-forget call. Encodes a Notify frame (no id, no reply) and
    // sends it via the bound NetPeer. targetPeer == -1 broadcasts.
    //
    // If no peer is bound, this is a no-op (returns false). This makes the
    // call safe to issue in single-player mode where networking is disabled.
    bool callRemote(const char* name, const char* argsJson, int targetPeer = -1);

    // Request/response call. Encodes a Request frame (with a fresh id),
    // sends it, and invokes `cb` when the matching Response/Error arrives
    // (or when timeoutMs elapses, whichever is first). The callback runs on
    // the main thread, inside poll().
    //
    // Returns the assigned call id, or 0 if no peer is bound.
    //
    // `argsJson` must be a valid JSON array, e.g. `["hello", 42]`.
    // `cb` receives (ok, resultJson) where resultJson is the raw JSON value
    // of the "r" field (success) or the "e" field text (error).
    uint32_t callWithReply(const char* name, const char* argsJson,
                            int targetPeer, uint32_t timeoutMs,
                            std::function<void(bool ok, const char* resultJson)> cb);

    // Send a Response frame (success). Used by handlers that want to reply
    // explicitly (rare — callWithReply handles this automatically).
    bool sendResponse(uint32_t callId, const char* resultJson, int targetPeer);

    // Send an Error frame.
    bool sendError(uint32_t callId, const char* errorMessage, int targetPeer);

    // Called by NetPeer::poll() when a packet arrives on the RPC channel.
    // Parses the frame as JSON and dispatches:
    //   - Request  -> invoke registered handlers; if a handler is registered,
    //                auto-send a Response with the handler's return JSON
    //                (the handler sets it via the SignalPayload's ptrValue;
    //                see implementation for the convention).
    //   - Response -> match against a pending callWithReply() call.
    //   - Error    -> match against a pending callWithReply() call (with ok=false).
    //   - Notify   -> invoke registered handlers (no reply).
    void dispatchPacket(int peerId, const void* data, int size);

    // Pump: expire timed-out callWithReply() calls. Call once per frame.
    void update(uint32_t nowMs);

    // Reset all state (pending calls, method table). Used by tests.
    void reset();

    // Test-only: peek at the number of pending calls.
    int pendingCallCount() const { return static_cast<int>(m_pending.size()); }

private:
    RpcServer() = default;
    NetPeer* m_peer = nullptr;

    struct PendingCall {
        uint32_t     id         = 0;
        uint32_t     deadlineMs = 0;
        std::function<void(bool ok, const char* resultJson)> cb;
    };
    std::vector<PendingCall> m_pending;
    uint32_t m_nextCallId = 1;

    // Track whether the current Request being dispatched has already been
    // responded to (via sendResponse/sendError) by the handler. If so,
    // dispatchPacket skips the auto-null fallback. Reset at the start of
    // each Request dispatch.
    uint32_t m_dispatchedCallId = 0;
    bool     m_respondedDuringDispatch = false;
    void     markResponded(uint32_t callId) {
        if (callId == m_dispatchedCallId) m_respondedDuringDispatch = true;
    }
};

} // namespace td
