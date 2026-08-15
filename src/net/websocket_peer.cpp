// =============================================================================
// TD Engine - WebSocketPeer (NATIVE implementation: Win32 + POSIX)
//
// Real, dependency-free RFC 6455 WebSocket server + client. No external libs
// (no libwebsockets, no Boost.Beast). SHA-1 + base64 are inline because
// they're tiny and we want zero build complexity.
//
// What this file does NOT do:
//   - TLS / wss:// on native. The connectUrl() path accepts "wss://" URLs
//     but currently downgrades to plain ws:// with a logged warning. Real
//     TLS would require SChannel (Windows) or OpenSSL (Linux) — left as a
//     future enhancement. Browsers handle wss:// natively via the WASM path.
//   - Per-message deflate. We negotiate no extensions in the handshake.
//   - Subprotocol negotiation. We accept any (or none).
//
// Build: compiled into td-engine STATIC lib on desktop. Excluded from WASM
// build (websocket_peer_wasm.cpp is used there instead).
// =============================================================================

#include "websocket_peer.h"
#include "json_rpc.h"
#include "../core/logger.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <chrono>

#if defined(_WIN32)
    #define TD_NET_WIN32 1
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN 1
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX 1
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int td_socklen_t;
    typedef int td_ssize_t;
    typedef SOCKET td_socket_t;
    #define TD_INVALID_SOCKET INVALID_SOCKET
    #define TD_SOCKET_ERROR   SOCKET_ERROR
    #define TD_CLOSE_SOCKET(s) closesocket(s)
#else
    #define TD_NET_POSIX 1
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <sys/select.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <errno.h>
    #include <fcntl.h>
    typedef int td_socket_t;
    typedef socklen_t td_socklen_t;
    #define TD_INVALID_SOCKET (-1)
    #define TD_SOCKET_ERROR   (-1)
    #define TD_CLOSE_SOCKET(s) ::close(s)
#endif

namespace td {

namespace {

// =========================================================================
// Winsock init guard (Windows only — POSIX needs no global init)
// =========================================================================
#if defined(TD_NET_WIN32)
struct WinsockInit {
    bool ok = false;
    WinsockInit() {
        WSADATA d;
        ok = (WSAStartup(MAKEWORD(2, 2), &d) == 0);
    }
    ~WinsockInit() { if (ok) WSACleanup(); }
};
WinsockInit& winsock() {
    static WinsockInit w;
    return w;
}
#endif

// =========================================================================
// SHA-1 (FIPS 180-4) — tiny inline impl, ~80 LOC.
// Used for the Sec-WebSocket-Accept handshake response.
// =========================================================================
struct Sha1 {
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    uint64_t totalBits = 0;
    uint8_t  buf[64];
    int      bufLen = 0;

    void put(const uint8_t* d, size_t n) {
        totalBits += uint64_t(n) * 8;
        while (n > 0) {
            int take = 64 - bufLen;
            if (take > (int)n) take = (int)n;
            memcpy(buf + bufLen, d, take);
            bufLen += take;
            d += take;
            n -= take;
            if (bufLen == 64) { block(buf); bufLen = 0; }
        }
    }
    void block(const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = (uint32_t(p[i*4]) << 24) | (uint32_t(p[i*4+1]) << 16)
                 | (uint32_t(p[i*4+2]) << 8) | uint32_t(p[i*4+3]);
        }
        for (int i = 16; i < 80; i++) {
            uint32_t v = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
            w[i] = (v << 1) | (v >> 31);
        }
        uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);            k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                     k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);   k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                     k = 0xCA62C1D6; }
            uint32_t t = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = t;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
    }
    void finish(uint8_t out[20]) {
        buf[bufLen++] = 0x80;
        if (bufLen > 56) { while (bufLen < 64) buf[bufLen++] = 0; block(buf); bufLen = 0; }
        while (bufLen < 56) buf[bufLen++] = 0;
        uint64_t bits = totalBits;
        for (int i = 7; i >= 0; i--) buf[bufLen++] = uint8_t(bits >> (i*8));
        block(buf); bufLen = 0;
        for (int i = 0; i < 5; i++) {
            out[i*4]   = uint8_t(h[i] >> 24);
            out[i*4+1] = uint8_t(h[i] >> 16);
            out[i*4+2] = uint8_t(h[i] >> 8);
            out[i*4+3] = uint8_t(h[i]);
        }
    }
};

// =========================================================================
// Base64 encoder (RFC 4648) — for Sec-WebSocket-Key + Accept.
// =========================================================================
const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
void base64Encode(const uint8_t* in, size_t n, std::string& out) {
    out.clear();
    out.reserve(((n + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 3 <= n; i += 3) {
        uint32_t v = (uint32_t(in[i]) << 16) | (uint32_t(in[i+1]) << 8) | in[i+2];
        out.push_back(B64[(v >> 18) & 63]);
        out.push_back(B64[(v >> 12) & 63]);
        out.push_back(B64[(v >> 6)  & 63]);
        out.push_back(B64[v & 63]);
    }
    if (i < n) {
        uint32_t v = uint32_t(in[i]) << 16;
        if (i + 1 < n) v |= uint32_t(in[i+1]) << 8;
        out.push_back(B64[(v >> 18) & 63]);
        out.push_back(B64[(v >> 12) & 63]);
        out.push_back((i + 1 < n) ? B64[(v >> 6) & 63] : '=');
        out.push_back('=');
    }
}

// =========================================================================
// Per-connection state (host mode: many; client mode: one)
// =========================================================================
struct Conn {
    td_socket_t fd = TD_INVALID_SOCKET;
    int peerId = 0;
    bool handshakeDone = false;  // HTTP upgrade complete?
    bool closing = false;
    bool closed = false;

    // Receive buffer: bytes pulled from the socket but not yet framed.
    std::vector<uint8_t> rx;

    // Partial frame assembly: when a frame spans multiple recv()s.
    std::vector<uint8_t> framePayload;
    int frameOpcode = 0;
    bool frameFin = false;

    // Send queue: WebSocket frames fully encoded, waiting to flush.
    std::vector<uint8_t> tx;

    Conn() = default;
};

// WebSocket opcodes (RFC 6455 §5.2)
enum WsOpcode : int {
    WS_CONT = 0x0,
    WS_TEXT = 0x1,
    WS_BIN  = 0x2,
    WS_CLOSE = 0x8,
    WS_PING  = 0x9,
    WS_PONG  = 0xA,
};

// Compute Sec-WebSocket-Accept from a client's Sec-WebSocket-Key.
// accept = base64(sha1(key + magic))
std::string computeAccept(const std::string& key) {
    static const char MAGIC[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string concat = key + MAGIC;
    Sha1 sha;
    sha.put(reinterpret_cast<const uint8_t*>(concat.data()), concat.size());
    uint8_t digest[20];
    sha.finish(digest);
    std::string out;
    base64Encode(digest, 20, out);
    return out;
}

// Generate a random 16-byte Sec-WebSocket-Key (client mode).
std::string makeClientKey() {
    uint8_t raw[16];
    for (int i = 0; i < 16; i++) {
        raw[i] = uint8_t(rand() & 0xFF);
    }
    std::string out;
    base64Encode(raw, 16, out);
    return out;
}

// Encode one WebSocket frame (client->server MUST be masked; server->client
// MUST NOT be masked). Appends to `out`.
//
// For server->browser: mask = false.
// For client->server:  mask = true (we generate a random 4-byte mask).
void encodeFrame(int opcode, bool fin, const void* payload, size_t len,
                 bool mask, std::vector<uint8_t>& out) {
    uint8_t h0 = uint8_t((fin ? 0x80 : 0) | (opcode & 0x0F));
    out.push_back(h0);

    uint8_t maskBit = mask ? 0x80 : 0;
    if (len < 126) {
        out.push_back(uint8_t(maskBit | len));
    } else if (len < 65536) {
        out.push_back(uint8_t(maskBit | 126));
        out.push_back(uint8_t(len >> 8));
        out.push_back(uint8_t(len & 0xFF));
    } else {
        out.push_back(uint8_t(maskBit | 127));
        uint64_t n = len;
        for (int i = 7; i >= 0; i--) out.push_back(uint8_t((n >> (i*8)) & 0xFF));
    }

    uint8_t maskKey[4] = {0,0,0,0};
    if (mask) {
        for (int i = 0; i < 4; i++) maskKey[i] = uint8_t(rand() & 0xFF);
        out.insert(out.end(), maskKey, maskKey + 4);
    }

    const uint8_t* p = static_cast<const uint8_t*>(payload);
    if (mask) {
        size_t old = out.size();
        out.resize(old + len);
        for (size_t i = 0; i < len; i++) {
            out[old + i] = p[i] ^ maskKey[i % 4];
        }
    } else {
        out.insert(out.end(), p, p + len);
    }
}

// Try to decode one frame from `rx`. On success: removes consumed bytes
// from rx, sets opcode/fin/payload, returns true. On partial: returns false
// leaving rx untouched. On protocol error: returns false + sets *err.
bool decodeFrame(std::vector<uint8_t>& rx, int& opcode, bool& fin,
                 std::vector<uint8_t>& payload, bool* err) {
    *err = false;
    if (rx.size() < 2) return false;
    uint8_t h0 = rx[0];
    uint8_t h1 = rx[1];
    fin = (h0 & 0x80) != 0;
    opcode = h0 & 0x0F;
    bool masked = (h1 & 0x80) != 0;
    uint64_t len = h1 & 0x7F;
    size_t need = 2;

    if (len == 126) {
        if (rx.size() < 4) return false;
        len = (uint64_t(rx[2]) << 8) | rx[3];
        need = 4;
    } else if (len == 127) {
        if (rx.size() < 10) return false;
        len = 0;
        for (int i = 0; i < 8; i++) len = (len << 8) | rx[2 + i];
        need = 10;
    }

    uint8_t maskKey[4] = {0,0,0,0};
    if (masked) {
        if (rx.size() < need + 4) return false;
        memcpy(maskKey, rx.data() + need, 4);
        need += 4;
    }

    // Cap on frame size: 16 MiB. Larger frames are treated as protocol errors
    // to prevent a malicious peer from exhausting memory.
    if (len > 16ull * 1024 * 1024) { *err = true; return false; }

    if (rx.size() < need + len) return false;

    payload.resize((size_t)len);
    if (masked) {
        for (uint64_t i = 0; i < len; i++) {
            payload[(size_t)i] = rx[need + (size_t)i] ^ maskKey[i % 4];
        }
    } else {
        memcpy(payload.data(), rx.data() + need, (size_t)len);
    }

    rx.erase(rx.begin(), rx.begin() + need + (size_t)len);
    return true;
}

// Set socket non-blocking. Required for poll() to honor timeoutMs.
bool setNonblocking(td_socket_t fd) {
#if defined(TD_NET_WIN32)
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

// Disable Nagle's algorithm — WebSocket is a stream protocol, but for
// real-time games we want each send() to flush immediately.
void setNoDelay(td_socket_t fd) {
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
}

// Drain a socket's rx into Conn::rx. Returns false if the socket is dead.
bool recvInto(Conn& c) {
    uint8_t tmp[8192];
    while (true) {
        int n = recv(c.fd, (char*)tmp, sizeof(tmp), 0);
        if (n > 0) {
            c.rx.insert(c.rx.end(), tmp, tmp + n);
            continue;
        }
        if (n == 0) return false;  // peer closed
#if defined(TD_NET_WIN32)
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK) return true;
        if (e == WSAECONNRESET) return false;
        return false;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        if (errno == EINTR) continue;
        return false;
#endif
    }
}

// Flush Conn::tx to the socket. Returns false on send failure.
bool flushTx(Conn& c) {
    while (!c.tx.empty()) {
        int n = send(c.fd, (const char*)c.tx.data(), (int)c.tx.size(), 0);
        if (n > 0) {
            c.tx.erase(c.tx.begin(), c.tx.begin() + n);
            continue;
        }
        if (n == 0) return false;
#if defined(TD_NET_WIN32)
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK) return true;
        return false;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        if (errno == EINTR) continue;
        return false;
#endif
    }
    return true;
}

// Find the first occurrence of "\r\n\r\n" in rx. Returns index or -1.
// (Manual search because std::search with initializer_list is awkward in C++17.)
int findDoubleCRLF(const std::vector<uint8_t>& rx) {
    if (rx.size() < 4) return -1;
    for (size_t i = 0; i + 4 <= rx.size(); i++) {
        if (rx[i] == '\r' && rx[i+1] == '\n' && rx[i+2] == '\r' && rx[i+3] == '\n') {
            return (int)i;
        }
    }
    return -1;
}

// Parse the HTTP Upgrade request from a freshly-accepted client. Returns
// true if the handshake completed; sets `acceptKey` to the value to send
// back in the 101 response.
bool parseHandshake(const std::vector<uint8_t>& rx, std::string& outKey) {
    // Find end of HTTP headers (\r\n\r\n).
    int hdrEnd = findDoubleCRLF(rx);
    if (hdrEnd < 0) return false;

    std::string headers(rx.begin(), rx.begin() + hdrEnd);
    // Look for "Sec-WebSocket-Key: <value>"
    const char* needle = "Sec-WebSocket-Key:";
    auto pos = headers.find(needle);
    if (pos == std::string::npos) {
        // Case-insensitive fallback.
        std::string lower = headers;
        for (auto& ch : lower) if (ch >= 'A' && ch <= 'Z') ch = char(ch + 32);
        pos = lower.find("sec-websocket-key:");
        if (pos == std::string::npos) return false;
    }
    pos += strlen(needle);
    while (pos < headers.size() && (headers[pos] == ' ' || headers[pos] == '\t')) pos++;
    size_t eol = headers.find('\r', pos);
    if (eol == std::string::npos) eol = headers.size();
    std::string key = headers.substr(pos, eol - pos);
    // Trim trailing whitespace.
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t' || key.back() == '\r')) key.pop_back();
    if (key.empty()) return false;
    outKey = computeAccept(key);
    return true;
}

} // namespace

// =========================================================================
// WebSocketPeer::Impl — the actual state
// =========================================================================
struct WebSocketPeer::Impl {
    bool isHost = false;
    int  localPeerId = 0;
    int  nextPeerId = 1;
    int  maxPeers = 64;

    td_socket_t listener = TD_INVALID_SOCKET;

    // Host mode: map of peerId -> Conn. Client mode: single Conn at peerId 1.
    std::vector<std::unique_ptr<Conn>> conns;

    std::string lastError;
    uint64_t bytesSent = 0;
    uint64_t bytesReceived = 0;

    // Outgoing packets queued during send() that couldn't be flushed
    // immediately (peer not yet handshaked, send buffer full). Pumped in
    // poll().
    struct Queued {
        int targetPeer;
        std::vector<uint8_t> frame;
    };
    std::vector<Queued> txQueue;

    Impl() {
#if defined(TD_NET_WIN32)
        winsock();
#endif
    }

    ~Impl() { closeAll(); }

    void closeAll() {
        for (auto& c : conns) {
            if (c->fd != TD_INVALID_SOCKET) {
                TD_CLOSE_SOCKET(c->fd);
                c->fd = TD_INVALID_SOCKET;
            }
        }
        conns.clear();
        if (listener != TD_INVALID_SOCKET) {
            TD_CLOSE_SOCKET(listener);
            listener = TD_INVALID_SOCKET;
        }
    }

    Conn* findConn(int peerId) {
        for (auto& c : conns) if (c->peerId == peerId) return c.get();
        return nullptr;
    }

    // Process a complete message (TEXT/BIN) from a connection.
    void handleMessage(Conn& c, int /*opcode*/, const std::vector<uint8_t>& payload) {
        bytesReceived += payload.size();
        // Dispatch into the RpcServer singleton. RpcServer::dispatchPacket
        // parses the JSON-RPC frame and invokes registered handlers.
        // Peer id is c.peerId (host mode) or 1 (client mode — we're the
        // only peer, the "remote" id from our perspective is 1).
        int fromPeerId = isHost ? c.peerId : 1;
        RpcServer::get().dispatchPacket(fromPeerId, payload.data(),
                                         (int)payload.size());
    }
};

// =========================================================================
// Public API
// =========================================================================

WebSocketPeer::WebSocketPeer() : m_impl(std::make_unique<Impl>()) {}

WebSocketPeer::~WebSocketPeer() = default;

bool WebSocketPeer::host(int port, int maxPeers) {
    m_impl->closeAll();
    m_impl->isHost = true;
    m_impl->localPeerId = 0;
    m_impl->maxPeers = maxPeers > 0 ? maxPeers : 64;
    m_impl->nextPeerId = 1;

    m_impl->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_impl->listener == TD_INVALID_SOCKET) {
        m_impl->lastError = "socket() failed";
        return false;
    }

    // Allow rapid rebind after restart (no TIME_WAIT delay).
    int yes = 1;
    setsockopt(m_impl->listener, SOL_SOCKET, SO_REUSEADDR,
               (const char*)&yes, sizeof(yes));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((u_short)port);

    if (bind(m_impl->listener, (sockaddr*)&addr, sizeof(addr)) == TD_SOCKET_ERROR) {
        m_impl->lastError = "bind() failed on port " + std::to_string(port);
        TD_CLOSE_SOCKET(m_impl->listener);
        m_impl->listener = TD_INVALID_SOCKET;
        return false;
    }

    if (listen(m_impl->listener, 8) == TD_SOCKET_ERROR) {
        m_impl->lastError = "listen() failed";
        TD_CLOSE_SOCKET(m_impl->listener);
        m_impl->listener = TD_INVALID_SOCKET;
        return false;
    }

    setNonblocking(m_impl->listener);
    TD_LOG_INFO("[WebSocketPeer] listening on port %d (max %d peers)",
                 port, m_impl->maxPeers);
    return true;
}

bool WebSocketPeer::connect(const char* host, int port) {
    std::string url = "ws://" + std::string(host) + ":" + std::to_string(port) + "/";
    return connectUrl(url.c_str());
}

bool WebSocketPeer::connectUrl(const char* url) {
    m_impl->closeAll();
    m_impl->isHost = false;
    m_impl->localPeerId = 1;  // we are peer 1; the server is "peer 0" / -1
    m_impl->nextPeerId = 1;

    // Parse "ws://host:port/path" or "wss://host:port/path".
    std::string s(url);
    bool tls = false;
    if (s.rfind("wss://", 0) == 0) { tls = true; s = s.substr(6); }
    else if (s.rfind("ws://", 0) == 0) { s = s.substr(5); }
    else { m_impl->lastError = "URL must start with ws:// or wss://"; return false; }

    if (tls) {
        TD_LOG_WARN("[WebSocketPeer] wss:// not yet supported on native; "
                     "downgrading to ws://. Use the WASM build for TLS.");
    }

    std::string host, path = "/";
    int port = 80;
    auto slash = s.find('/');
    if (slash != std::string::npos) {
        host = s.substr(0, slash);
        path = s.substr(slash);
    } else {
        host = s;
    }
    auto colon = host.rfind(':');
    if (colon != std::string::npos) {
        port = atoi(host.c_str() + colon + 1);
        host = host.substr(0, colon);
    }

    addrinfo hints, *res = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%d", port);
    if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res) {
        m_impl->lastError = std::string("getaddrinfo failed for ") + host;
        return false;
    }

    td_socket_t fd = TD_INVALID_SOCKET;
    for (addrinfo* p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == TD_INVALID_SOCKET) continue;
        if (::connect(fd, p->ai_addr, (int)p->ai_addrlen) == 0) break;
        TD_CLOSE_SOCKET(fd);
        fd = TD_INVALID_SOCKET;
    }
    freeaddrinfo(res);

    if (fd == TD_INVALID_SOCKET) {
        m_impl->lastError = "connect() failed";
        return false;
    }

    setNonblocking(fd);
    setNoDelay(fd);

    auto c = std::make_unique<Conn>();
    c->fd = fd;
    c->peerId = 1;
    m_impl->conns.push_back(std::move(c));

    // Send the HTTP Upgrade request immediately. The handshake response
    // will be parsed in poll() once it arrives.
    std::string key = makeClientKey();
    char req[1024];
    int n = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        path.c_str(), host.c_str(), port, key.c_str());
    m_impl->conns.back()->tx.insert(m_impl->conns.back()->tx.end(),
                                     req, req + n);

    TD_LOG_INFO("[WebSocketPeer] connecting to ws://%s:%d%s", host.c_str(), port, path.c_str());
    return true;
}

void WebSocketPeer::disconnect() {
    // Send WS_CLOSE to each peer, then close.
    for (auto& c : m_impl->conns) {
        if (c->fd == TD_INVALID_SOCKET || c->closed) continue;
        std::vector<uint8_t> frame;
        encodeFrame(WS_CLOSE, true, nullptr, 0, m_impl->isHost ? false : true, frame);
        c->tx.insert(c->tx.end(), frame.begin(), frame.end());
        flushTx(*c);
    }
    m_impl->closeAll();
}

void WebSocketPeer::poll() {
    // --- HOST mode: accept new connections --------------------------------
    if (m_impl->isHost && m_impl->listener != TD_INVALID_SOCKET) {
        while ((int)m_impl->conns.size() < m_impl->maxPeers) {
            sockaddr_in addr;
            td_socklen_t len = sizeof(addr);
            td_socket_t fd = accept(m_impl->listener, (sockaddr*)&addr, &len);
            if (fd == TD_INVALID_SOCKET) break;
            setNonblocking(fd);
            setNoDelay(fd);
            auto c = std::make_unique<Conn>();
            c->fd = fd;
            c->peerId = m_impl->nextPeerId++;
            m_impl->conns.push_back(std::move(c));
            TD_LOG_INFO("[WebSocketPeer] peer %d connected", m_impl->conns.back()->peerId);
        }
    }

    // --- Pump all connections ---------------------------------------------
    for (auto it = m_impl->conns.begin(); it != m_impl->conns.end(); ) {
        Conn& c = **it;

        // Pull bytes off the socket.
        if (!recvInto(c)) {
            c.closed = true;
        }

        // If still handshaking, try to complete the handshake.
        if (!c.handshakeDone && !c.closed) {
            std::string acceptKey;
            if (parseHandshake(c.rx, acceptKey)) {
                int hdrEnd = findDoubleCRLF(c.rx);
                if (hdrEnd >= 0) {
                    // Consume the request line + headers up to \r\n\r\n.
                    c.rx.erase(c.rx.begin(), c.rx.begin() + hdrEnd + 4);
                }

                char resp[512];
                int n = snprintf(resp, sizeof(resp),
                    "HTTP/1.1 101 Switching Protocols\r\n"
                    "Upgrade: websocket\r\n"
                    "Connection: Upgrade\r\n"
                    "Sec-WebSocket-Accept: %s\r\n"
                    "\r\n",
                    acceptKey.c_str());
                c.tx.insert(c.tx.end(), resp, resp + n);
                c.handshakeDone = true;
                TD_LOG_INFO("[WebSocketPeer] peer %d handshake complete", c.peerId);
            }
        }

        // Decode frames as long as we have a complete frame in the buffer.
        if (c.handshakeDone && !c.closed) {
            while (!c.rx.empty()) {
                int opcode = 0;
                bool fin = false;
                std::vector<uint8_t> payload;
                bool err = false;
                if (!decodeFrame(c.rx, opcode, fin, payload, &err)) {
                    if (err) c.closed = true;
                    break;
                }

                if (opcode == WS_PING) {
                    std::vector<uint8_t> frame;
                    encodeFrame(WS_PONG, true, payload.data(), payload.size(),
                                false, frame);
                    c.tx.insert(c.tx.end(), frame.begin(), frame.end());
                } else if (opcode == WS_PONG) {
                    // Ignore — keepalive ack.
                } else if (opcode == WS_CLOSE) {
                    c.closed = true;
                    break;
                } else if (opcode == WS_CONT) {
                    // Continuation of a fragmented message.
                    c.framePayload.insert(c.framePayload.end(),
                                           payload.begin(), payload.end());
                    if (fin) {
                        m_impl->handleMessage(c, c.frameOpcode, c.framePayload);
                        c.framePayload.clear();
                    }
                } else if (opcode == WS_TEXT || opcode == WS_BIN) {
                    if (fin) {
                        m_impl->handleMessage(c, opcode, payload);
                    } else {
                        c.frameOpcode = opcode;
                        c.framePayload = payload;
                    }
                }
            }
        }

        // Flush outgoing bytes.
        if (!c.tx.empty()) flushTx(c);

        if (c.closed) {
            if (c.fd != TD_INVALID_SOCKET) {
                TD_CLOSE_SOCKET(c.fd);
                c.fd = TD_INVALID_SOCKET;
            }
            if (m_impl->isHost) {
                TD_LOG_INFO("[WebSocketPeer] peer %d disconnected", c.peerId);
            }
            it = m_impl->conns.erase(it);
        } else {
            ++it;
        }
    }

    // --- Flush queued outgoing packets (from send() before handshake) -----
    if (!m_impl->txQueue.empty()) {
        auto q = std::move(m_impl->txQueue);
        m_impl->txQueue.clear();
        for (auto& item : q) {
            if (item.targetPeer == -1) {
                // Broadcast.
                for (auto& c : m_impl->conns) {
                    if (c->handshakeDone) {
                        c->tx.insert(c->tx.end(), item.frame.begin(), item.frame.end());
                    }
                }
            } else {
                Conn* c = m_impl->findConn(item.targetPeer);
                if (c && c->handshakeDone) {
                    c->tx.insert(c->tx.end(), item.frame.begin(), item.frame.end());
                }
            }
        }
        // Re-flush.
        for (auto& c : m_impl->conns) {
            if (!c->tx.empty()) flushTx(*c);
        }
    }
}

bool WebSocketPeer::send(const NetPacket& packet) {
    if (!packet.data || packet.size <= 0) return false;

    // Treat the payload as a TEXT frame if it looks like JSON (starts with
    // '{' or '[' after whitespace), otherwise BINARY. The JS side accepts
    // both, but TEXT lets browsers decode it as a string automatically.
    const uint8_t* p = static_cast<const uint8_t*>(packet.data);
    int i = 0;
    while (i < packet.size && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n')) i++;
    int opcode = WS_BIN;
    if (i < packet.size && (p[i] == '{' || p[i] == '[')) opcode = WS_TEXT;

    // In host mode, server->client frames are UNMASKED.
    // In client mode, client->server frames MUST be MASKED.
    bool mask = !m_impl->isHost;

    std::vector<uint8_t> frame;
    encodeFrame(opcode, true, packet.data, (size_t)packet.size, mask, frame);
    m_impl->bytesSent += (uint64_t)packet.size;

    bool anySent = false;
    if (packet.targetPeer == -1) {
        // Broadcast to all handshaked peers.
        for (auto& c : m_impl->conns) {
            if (c->handshakeDone && !c->closed) {
                c->tx.insert(c->tx.end(), frame.begin(), frame.end());
                anySent = true;
            }
        }
    } else {
        Conn* c = m_impl->findConn(packet.targetPeer);
        if (c && c->handshakeDone && !c->closed) {
            c->tx.insert(c->tx.end(), frame.begin(), frame.end());
            anySent = true;
        }
    }

    // If no peer was ready, queue the frame for the next poll() retry.
    // (Limited backlog — drop after 64 queued frames to avoid OOM.)
    if (!anySent && m_impl->txQueue.size() < 64) {
        m_impl->txQueue.push_back({packet.targetPeer, std::move(frame)});
    }

    return anySent;
}

bool WebSocketPeer::isHost() const { return m_impl->isHost; }

int WebSocketPeer::localPeerId() const { return m_impl->localPeerId; }

int WebSocketPeer::connectedPeerCount() const {
    int n = 0;
    for (const auto& c : m_impl->conns) {
        if (c->handshakeDone && !c->closed) ++n;
    }
    return n;
}

const std::string& WebSocketPeer::lastError() const { return m_impl->lastError; }

uint64_t WebSocketPeer::bytesSent() const { return m_impl->bytesSent; }
uint64_t WebSocketPeer::bytesReceived() const { return m_impl->bytesReceived; }

} // namespace td
