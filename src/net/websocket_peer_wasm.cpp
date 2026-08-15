// =============================================================================
// TD Engine - WebSocketPeer (WASM implementation, Emscripten)
//
// Browser C++ clients use this. Browsers can't listen on TCP, so host()
// always returns false. connect() opens a real browser WebSocket via
// Emscripten's <emscripten/websocket.h>. The browser handles TLS, framing,
// masking — we just get/put message payloads.
//
// This file is ONLY compiled in the WASM build (TD_BUILD_WEB=ON). The native
// build compiles websocket_peer.cpp instead.
// =============================================================================

#include "websocket_peer.h"
#include "json_rpc.h"
#include "../core/logger.h"

#include <emscripten/websocket.h>
#include <emscripten.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace td {

namespace {

// Emscripten gives us a single WebSocket per peer (we're always a client in
// the browser). We keep global state because the Emscripten callback API is
// C-style and doesn't pass user data through cleanly without a trampoline.
//
// If you need multiple WebSocketPeers in one WASM instance (rare — usually
// one player = one connection), wrap this in a registry. For now we support
// one peer per WASM module, which matches every browser-game use case.

struct WasmState {
    EMSCRIPTEN_WEBSOCKET_T sock = 0;
    bool connected = false;
    bool closing = false;
    std::string url;
    std::string lastError;
    uint64_t bytesSent = 0;
    uint64_t bytesReceived = 0;

    // Outgoing queue: messages we couldn't send yet because the socket
    // wasn't OPEN when send() was called. Flushed on the onopen callback.
    struct QueuedMsg {
        std::vector<uint8_t> data;
        bool isText;
    };
    std::vector<QueuedMsg> txQueue;
};

WasmState g_state;

EM_BOOL onOpen(int /*eventType*/, const EmscriptenWebSocketOpenEvent* /*e*/, void* /*userData*/) {
    g_state.connected = true;
    TD_LOG_INFO("[WebSocketPeer/WASM] connected to %s", g_state.url.c_str());
    // Flush any messages that were queued before open.
    for (auto& m : g_state.txQueue) {
        if (m.isText) {
            emscripten_websocket_send_utf8_text(g_state.sock,
                reinterpret_cast<const char*>(m.data.data()));
        } else {
            emscripten_websocket_send_binary(g_state.sock, m.data.data(),
                                              (uint32_t)m.data.size());
        }
        g_state.bytesSent += m.data.size();
    }
    g_state.txQueue.clear();
    return EM_TRUE;
}

EM_BOOL onClose(int /*eventType*/, const EmscriptenWebSocketCloseEvent* /*e*/, void* /*userData*/) {
    g_state.connected = false;
    g_state.sock = 0;
    if (!g_state.closing) {
        TD_LOG_WARN("[WebSocketPeer/WASM] socket closed by remote");
    }
    return EM_TRUE;
}

EM_BOOL onError(int /*eventType*/, const EmscriptenWebSocketErrorEvent* /*e*/, void* /*userData*/) {
    g_state.lastError = "WebSocket error (browser reported)";
    TD_LOG_ERROR("[WebSocketPeer/WASM] %s", g_state.lastError.c_str());
    return EM_TRUE;
}

EM_BOOL onMessage(int /*eventType*/, const EmscriptenWebSocketMessageEvent* e, void* /*userData*/) {
    g_state.bytesReceived += e->numBytes;
    // Dispatch into the RpcServer singleton. RpcServer::dispatchPacket
    // parses the JSON-RPC frame and invokes registered handlers.
    //
    // From our perspective (client), the "remote peer id" is 1 (the server).
    RpcServer::get().dispatchPacket(/*peerId=*/1, e->data, (int)e->numBytes);
    return EM_TRUE;
}

} // namespace

// =========================================================================
// WebSocketPeer::Impl — thin wrapper so the header's pimpl stays consistent
// with the native build.
// =========================================================================
struct WebSocketPeer::Impl {
    // All state lives in g_state above (per-process singleton). The Impl
    // struct exists only so the header's unique_ptr<Impl> compiles.
};

// =========================================================================
// Public API
// =========================================================================

WebSocketPeer::WebSocketPeer() : m_impl(std::make_unique<Impl>()) {}
WebSocketPeer::~WebSocketPeer() {
    // Don't actually close the global socket on destruction — there's only
    // one per WASM process, and the next WebSocketPeer construction would
    // see it disappear. Caller should explicitly call disconnect().
}

bool WebSocketPeer::host(int /*port*/, int /*maxPeers*/) {
    // Browsers cannot accept inbound TCP connections.
    g_state.lastError = "host() not supported in WASM — browsers can't listen";
    TD_LOG_ERROR("[WebSocketPeer/WASM] %s", g_state.lastError.c_str());
    return false;
}

bool WebSocketPeer::connect(const char* host, int port) {
    std::string url = "ws://" + std::string(host) + ":" + std::to_string(port) + "/";
    return connectUrl(url.c_str());
}

bool WebSocketPeer::connectUrl(const char* url) {
    // If there's already an open socket, close it first.
    if (g_state.sock != 0) {
        g_state.closing = true;
        emscripten_websocket_close(g_state.sock, 1000, "reconnect");
        emscripten_websocket_delete(g_state.sock);
        g_state.sock = 0;
        g_state.connected = false;
    }
    g_state.closing = false;
    g_state.url = url;

    EmscriptenWebSocketCreateAttributes attrs;
    emscripten_websocket_init_create_attributes(&attrs);
    attrs.url = url;
    attrs.createOnMainThread = EM_TRUE;
    attrs.protocols = nullptr;  // accept any subprotocol

    g_state.sock = emscripten_websocket_new(&attrs);
    if (g_state.sock <= 0) {
        g_state.lastError = std::string("emscripten_websocket_new failed for ") + url;
        TD_LOG_ERROR("[WebSocketPeer/WASM] %s", g_state.lastError.c_str());
        return false;
    }

    emscripten_websocket_set_onopen_callback(g_state.sock, nullptr, onOpen);
    emscripten_websocket_set_onclose_callback(g_state.sock, nullptr, onClose);
    emscripten_websocket_set_onerror_callback(g_state.sock, nullptr, onError);
    emscripten_websocket_set_onmessage_callback(g_state.sock, nullptr, onMessage);

    TD_LOG_INFO("[WebSocketPeer/WASM] connecting to %s", url);
    return true;
}

void WebSocketPeer::disconnect() {
    if (g_state.sock != 0) {
        g_state.closing = true;
        emscripten_websocket_close(g_state.sock, 1000, "client disconnect");
        emscripten_websocket_delete(g_state.sock);
        g_state.sock = 0;
        g_state.connected = false;
    }
    g_state.txQueue.clear();
}

void WebSocketPeer::poll() {
    // Emscripten's WebSocket API is event-driven: onopen/onmessage/onclose
    // callbacks fire on the browser's main-thread event loop. There's no
    // explicit pump needed from C++. This function exists to honor the
    // NetPeer interface contract; calling it is a no-op.
    //
    // The browser dispatches events between frames automatically. When
    // control returns to the JS event loop (after the WASM main loop
    // returns), any pending WebSocket callbacks fire.
}

bool WebSocketPeer::send(const NetPacket& packet) {
    if (!packet.data || packet.size <= 0) return false;
    if (g_state.sock == 0) return false;

    // Treat as TEXT if it looks like JSON, else BINARY.
    const uint8_t* p = static_cast<const uint8_t*>(packet.data);
    int i = 0;
    while (i < packet.size && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n')) i++;
    bool isText = (i < packet.size && (p[i] == '{' || p[i] == '['));

    if (!g_state.connected) {
        // Not yet open — queue for the onopen callback to flush.
        if (g_state.txQueue.size() < 64) {
            g_state.txQueue.push_back({
                std::vector<uint8_t>(p, p + packet.size),
                isText
            });
        }
        return false;
    }

    EMSCRIPTEN_RESULT result;
    if (isText) {
        // send_utf8_text expects a null-terminated C string. We need to
        // copy and null-terminate.
        std::vector<char> buf(packet.size + 1);
        memcpy(buf.data(), packet.data, (size_t)packet.size);
        buf[packet.size] = 0;
        result = emscripten_websocket_send_utf8_text(g_state.sock, buf.data());
    } else {
        result = emscripten_websocket_send_binary(g_state.sock,
            const_cast<void*>(packet.data), (uint32_t)packet.size);
    }

    if (result != EMSCRIPTEN_RESULT_SUCCESS) {
        g_state.lastError = "emscripten_websocket_send failed";
        return false;
    }

    g_state.bytesSent += (uint64_t)packet.size;
    return true;
}

bool WebSocketPeer::isHost() const { return false; }

int WebSocketPeer::localPeerId() const { return 1; }

int WebSocketPeer::connectedPeerCount() const {
    return g_state.connected ? 1 : 0;
}

const std::string& WebSocketPeer::lastError() const { return g_state.lastError; }

uint64_t WebSocketPeer::bytesSent() const { return g_state.bytesSent; }
uint64_t WebSocketPeer::bytesReceived() const { return g_state.bytesReceived; }

} // namespace td
