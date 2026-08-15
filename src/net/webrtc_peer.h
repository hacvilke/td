// =============================================================================
// TD Engine - WebRTC Peer (Tier 3 — Future Low-Latency Networking)
//
// WebRTC is the next step beyond WebSocket for real-time multiplayer. It
// offers:
//   - UDP-like unreliable transport (Data Channels with maxRetransmits=0)
//   - Sub-50ms latency vs WebSocket's ~100-200ms (TCP head-of-line blocking)
//   - P2P mesh networking (clients connect directly, server only relays SDP)
//   - Built-in congestion control + NACK
//
// Why WebRTC isn't the default today:
//   - Signaling is more complex: requires an SDP exchange server (HTTP or
//     WebSocket) to negotiate the connection between peers.
//   - Browser-side SDP creation requires async JS APIs (RTCPeerConnection).
//     C++ can't easily call these from Wasm without an Emscripten binding.
//   - For most turn-based / casual multiplayer, WebSocket's reliability
//     and simpler protocol outweighs the latency win.
//
// The TDScript `@rpc(unreliable)` decorator was designed for WebRTC's
// unreliable data channel. Today it runs over WebSocket (which is reliable,
// so unreliable RPCs are slightly slower than they need to be). When
// WebRTCPeer ships, those RPCs will automatically use the UDP-like path.
//
// Status: INTERFACE ONLY. Real implementation is a future task.
//
// Implementation path:
//   1. Browser side: a new web/net_webrtc.js that wraps RTCPeerConnection.
//   2. C++ side: this header, with a WebRTCPeer class implementing NetPeer.
//      Uses emscripten_websocket_* APIs initially (the only Emscripten
//      network API), with custom JS glue to bridge to RTCPeerConnection.
//   3. Signaling: extend the existing game-net server (in tools/cli/commands/serve.js)
//      to also serve as an SDP relay. Clients POST their offer to
//      /signal/offer, the server forwards to the peer, the peer responds
//      at /signal/answer.
//   4. ICE: use Google's public STUN server (stun:stun.l.google.com:19302)
//      for NAT traversal. TURN server is user-configured in project.td.
// =============================================================================
#pragma once

#include "transport.h"
#include "../core/logger.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace td {

// Forward declarations
class WebRTCPeerImpl;

// -----------------------------------------------------------------------------
// WebRTCPeer — NetPeer implementation over a WebRTC Data Channel.
//
// Wire format: SAME as WebSocketPeer (JSON-RPC 2.0 frames, 28 tests lock it).
// The only difference is the transport: WebRTC Data Channel instead of TCP.
//
// Modes:
//   - Reliable   → Data Channel ordered + retransmit (like TCP)
//   - Unreliable → Data Channel unordered + maxRetransmits=0 (like UDP)
//
// A single WebRTCPeer can multiplex both modes over two data channels.
// TDScript's @rpc(reliable) goes on the reliable channel; @rpc(unreliable)
// goes on the unreliable channel.
// -----------------------------------------------------------------------------
class WebRTCPeer : public NetPeer {
public:
    WebRTCPeer();
    ~WebRTCPeer() override;

    // --- NetPeer interface (mirrors src/net/transport.h) -------------------

    // Connection state. For WebRTC, "connected" means the Data Channel is
    // open (ICE + DTLS handshake complete).
    State getState() const override;
    bool isConnected() const override;
    bool isServer() const override;

    // Connect to a peer. For WebRTC, this is async: it creates an
    // RTCPeerConnection, creates a Data Channel, creates an SDP offer,
    // and sends the offer to the signaling server. The connection completes
    // later when the answer is received and ICE candidates are exchanged.
    //
    // `signalingUrl` is an HTTP endpoint that relays SDP offers/answers.
    // `peerId` identifies which remote peer to connect to (the signaling
    // server routes the offer to that peer).
    bool connect(const std::string& signalingUrl, uint32_t peerId) override;

    // Disconnect. Closes the Data Channel and the RTCPeerConnection.
    void disconnect() override;

    // Send a frame on the reliable channel (TCP-like).
    bool sendReliable(const void* data, size_t size) override;
    bool sendReliable(const std::string& msg) override;

    // Send a frame on the unreliable channel (UDP-like — may drop, may reorder).
    bool sendUnreliable(const void* data, size_t size) override;
    bool sendUnreliable(const std::string& msg) override;

    // Poll for incoming frames. The callback is invoked once per frame.
    void poll(const FrameCallback& callback) override;

    // Get the round-trip time estimate (ms). WebRTC provides this natively
    // via the data channel's stats API; we expose it for lag compensation.
    uint32_t getRTT() const override;

    // --- WebRTC-specific API -----------------------------------------------

    // ICE candidate exchange. Call this when the signaling server delivers
    // a remote ICE candidate.
    bool addRemoteICECandidate(const std::string& candidateJson);

    // SDP answer exchange. Call this when the signaling server delivers
    // the remote peer's SDP answer to our offer.
    bool setRemoteAnswer(const std::string& sdp);

    // SDP offer exchange (incoming). Call this when we're the ANSWERER and
    // the signaling server delivers a remote peer's SDP offer. We generate
    // an answer and send it back via the signaling server.
    bool setRemoteOffer(const std::string& sdp, std::string* outAnswer);

    // Configure the STUN/TURN servers. Default: Google's public STUN.
    // TURN servers require credentials — set them here before connect().
    void setICEServers(const std::vector<std::string>& stunUrls,
                       const std::vector<std::string>& turnUrls = {},
                       const std::string& turnUser = "",
                       const std::string& turnCredential = "");

private:
    std::unique_ptr<WebRTCPeerImpl> impl_;
};

// -----------------------------------------------------------------------------
// WebRTCSignalingServer — helper for the dedicated server to relay SDP.
//
// This is NOT a NetPeer; it's a side-channel HTTP/WebSocket server that
// relays SDP offers/answers/ICE candidates between two peers that want to
// establish a direct WebRTC connection.
//
// In a future implementation, the game-net server in tools/cli/commands/serve.js
// will expose /signal/offer, /signal/answer, /signal/ice endpoints. Clients
// POST to these; the server relays to the target peerId.
// -----------------------------------------------------------------------------
class WebRTCSignalingServer {
public:
    static WebRTCSignalingServer& get() {
        static WebRTCSignalingServer instance;
        return instance;
    }

    // Start the signaling server on the given HTTP port.
    // Adds routes: POST /signal/offer, POST /signal/answer, POST /signal/ice
    bool start(uint16_t port);

    // Stop the signaling server.
    void stop();

    // Register a peer as available for signaling. Other peers can then
    // initiate connections to this peerId.
    void registerPeer(uint32_t peerId);

    // Remove a peer from the available list.
    void unregisterPeer(uint32_t peerId);

private:
    WebRTCSignalingServer() = default;
    bool running_ = false;
    uint16_t port_ = 0;
};

} // namespace td
