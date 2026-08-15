// =============================================================================
// TD Engine - WebSocketPeer (concrete NetPeer for browser <-> C++ multiplayer)
//
// A real, working WebSocket (RFC 6455) implementation of the NetPeer abstract
// interface. Speaks the SAME JSON-RPC wire format as web/net_websocket.js,
// so a C++ authoritative server can directly accept browser clients — no
// translation layer, no Node.js middleman.
//
// Two operating modes:
//
//   1. HOST mode (native desktop only):
//        WebSocketPeer peer;
//        peer.host(8080, 64);   // listen on TCP :8080, accept up to 64 browsers
//        td::RpcServer::get().setPeer(&peer);
//        // in main loop: peer.poll();
//
//      Each connecting browser becomes a remote peer with peerId 1, 2, 3, ...
//      The local peerId is 0 (the host). Broadcast sends (targetPeer=-1) fan
//      out to all connected browsers.
//
//   2. CLIENT mode (native OR WASM):
//        WebSocketPeer peer;
//        peer.connect("example.com", 8080);
//        td::RpcServer::get().setPeer(&peer);
//        // in main loop: peer.poll();
//
//      Used when the C++ side is itself a client of another C++ server, or
//      when running in the browser via Emscripten (WASM builds can ONLY be
//      clients — browsers cannot listen on TCP).
//
// Wire format (per net_websocket.js + json_rpc.h):
//   All RPC frames are sent as WebSocket TEXT frames containing one JSON
//   object: {"id":N,"m":"method","a":[...]} etc.
//   Binary frames pass through unchanged for raw byte transport (e.g. delta-
//   compressed world snapshots).
//
// Status: REAL. Native impl uses Winsock2 (Windows) / POSIX sockets (Linux,
// macOS). WASM impl uses Emscripten's <emscripten/websocket.h> API.
// =============================================================================

#pragma once
#include "transport.h"

#include <cstdint>
#include <memory>
#include <string>

namespace td {

class WebSocketPeer : public NetPeer {
public:
    WebSocketPeer();
    ~WebSocketPeer() override;

    WebSocketPeer(const WebSocketPeer&) = delete;
    WebSocketPeer& operator=(const WebSocketPeer&) = delete;

    // --- NetPeer interface --------------------------------------------------

    // HOST mode (native only): start a TCP listener on `port`. Up to
    // `maxPeers` browsers may connect simultaneously. Returns false on
    // bind/listen failure (e.g. port in use). On WASM, returns false and
    // logs an error (browsers cannot accept inbound TCP).
    bool host(int port, int maxPeers) override;

    // CLIENT mode: connect to a remote WebSocket server. URL is constructed
    // as "ws://host:port/" (use connectUrl() for wss:// or a path).
    // Returns true if the connection attempt started; the Connected event
    // fires later via poll() when the handshake completes.
    bool connect(const char* host, int port) override;

    // CLIENT mode (alt): connect using a full URL (ws:// or wss://, optional
    // path). On native, wss:// uses the OS TLS stack (SChannel on Windows,
    // OpenSSL on Linux). On WASM, the browser handles TLS transparently.
    bool connectUrl(const char* url);

    // Disconnect all peers and close the listener (if any).
    void disconnect() override;

    // Per-frame pump. Performs:
    //   - accept() on the listener (host mode) to pick up new browsers
    //   - recv() on each connected socket, decoding WebSocket frames
    //   - dispatch of complete frames via RpcServer::dispatchPacket()
    //     (RpcServer::get() singleton — set with setPeer)
    //   - send of any queued outgoing packets
    //   - timeout / keepalive handling
    // Must be called once per frame from the main thread.
    void poll() override;

    // Send a packet to `targetPeer` (or broadcast if -1). The packet's
    // `data` is copied before send() returns; caller may free it immediately.
    //
    // Reliability/channel are ignored — WebSocket is a single reliable
    // ordered stream per connection, so all packets are reliable+ordered
    // regardless of the requested NetReliability. (Browsers can't do UDP.)
    //
    // Returns true if at least one peer received the packet (host mode) or
    // the single peer is connected (client mode). Returns false if no peer
    // is connected or the connection is in a failed state.
    bool send(const NetPacket& packet) override;

    // --- Introspection ------------------------------------------------------

    NetPeerType type() const override { return NetPeerType::WebRTC; }
    // ^ Returns WebRTC because that's the closest existing enum tag for
    // "browser-friendly transport". A future enum revision may add a
    // dedicated WebSocket tag.

    bool isHost() const override;
    int  localPeerId() const override;
    int  connectedPeerCount() const override;

    // --- Diagnostics --------------------------------------------------------

    // Last error message (empty if none). Useful for logging on connect()
    // or host() failure.
    const std::string& lastError() const;

    // Bytes sent/received since this peer was created. For profiler UI.
    uint64_t bytesSent() const;
    uint64_t bytesReceived() const;

private:
    // Pimpl — the platform-specific state (sockets, buffers, Emscripten
    // handles) lives in the .cpp. The header stays portable.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace td
