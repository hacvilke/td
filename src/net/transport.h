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
// Status: STUB. The interface is defined; concrete implementations
// (ENetPeer, WebRTCPeer) are TODO and tracked in docs/MODULARITY_ROADMAP.md
// Tier 1.5. The stub lets the editor and the WASM bridge compile + link
// against the API today; networking calls become no-ops until a real
// peer is plugged in.
// =============================================================================
#pragma once
#include "../core/logger.h"
#include "../core/signal.h"
#include <cstdint>
#include <cstring>

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
// High-level RPC layer (Tier 1.5, also stubbed here).
//
// Mirrors Godot's @rpc annotation. A gameplay script registers a method:
//
//   td::RpcServer::get().registerMethod("player:say",
//       [](int peerId, const RpcArgs& args) {
//           TD_LOG_INFO("Peer %d says: %s", peerId, args.getString(0));
//       });
//
// And calls it on a remote peer:
//
//   td::RpcArgs args;
//   args.pushString("hello");
//   td::RpcServer::get().callRemote("player:say", args, /*targetPeer=*/-1);
//
// The RPC layer serializes args using the same JSON reader/writer as the
// scene serializer (Tier 1.2), so types match across the wire without IDL.
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

    // Call a method on a remote peer (or all peers if targetPeer == -1).
    // argsJson is a JSON string; the remote side dispatches it to the
    // registered handler.
    void callRemote(const char* name, const char* argsJson, int targetPeer = -1);

    // Called by NetPeer::poll() when a packet arrives on the RPC channel.
    // Dispatches to registered handlers.
    void dispatchPacket(int peerId, const void* data, int size);

private:
    RpcServer() = default;
    NetPeer* m_peer = nullptr;
    // Method table: name -> list of handlers.
    // Implementation detail: use the SignalBus with a payload convention:
    //   emit("rpc:" + name, { intValue: peerId, strValue: argsJson })
    // This avoids a separate method-table data structure.
};

} // namespace td
