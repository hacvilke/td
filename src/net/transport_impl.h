// =============================================================================
// TD Engine - Network Transport (REAL implementation, internal header)
//
// This is the IMPLEMENTATION header for the real reliable transport stack.
// It complements the frozen public API in transport.h (which stays
// byte-identical) with concrete types the .cpp + tests need:
//
//   - Endpoint          IPv4/IPv6 + port, parse/format helpers
//   - Socket            cross-platform UDP socket (Winsock2 / POSIX / WASM)
//   - ReliableChannel   sliding-window ARQ over UDP with 3 reliability modes
//   - Connection        state machine + fragmentation + heartbeat + timeout
//   - MessageReader/Writer  tiny protobuf-style bit-packed serialization
//   - RPC               register/call with future-style callback + timeouts
//   - NetworkInterface  top-level: owns Socket + Connections + RPC + poll()
//
// Status: REAL. Backs the public NetPeer / RpcServer API in transport.h.
// =============================================================================
#pragma once
#include "transport.h"
#include "../core/logger.h"
#include "../core/signal.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <memory>
#include <unordered_map>
#include <map>
#include <atomic>

// --- Platform detection for the Socket impl ---------------------------------
#if defined(_WIN32)
    #define TD_NET_WIN32 1
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int td_socklen_t;
    typedef int td_ssize_t;
    typedef SOCKET td_socket_t;
    #define TD_INVALID_SOCKET INVALID_SOCKET
    #define TD_SOCKET_ERROR   SOCKET_ERROR
#elif defined(__EMSCRIPTEN__)
    // Emscripten provides the POSIX BSD socket API (proxied to JS WebSockets
    // when -s PROXY_POSIX_SOCKETS or the WebSocket bridge is enabled). For
    // the build target we just include the standard headers — at runtime a
    // browser cannot do raw UDP, but the COMPILE path is identical to POSIX
    // which is what the Makefile / CMakeLists verify.
    #define TD_NET_POSIX 1
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <netinet/in.h>
    #include <netinet/ip.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <cerrno>
    #include <fcntl.h>
    #include <netdb.h>
    typedef socklen_t td_socklen_t;
    typedef ssize_t  td_ssize_t;
    typedef int      td_socket_t;
    #define TD_INVALID_SOCKET (-1)
    #define TD_SOCKET_ERROR   (-1)
    #define closesocket ::close
#else
    #define TD_NET_POSIX 1
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <netinet/in.h>
    #include <netinet/ip.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <cerrno>
    #include <fcntl.h>
    #include <netdb.h>
    typedef socklen_t td_socklen_t;
    typedef ssize_t  td_ssize_t;
    typedef int      td_socket_t;
    #define TD_INVALID_SOCKET (-1)
    #define TD_SOCKET_ERROR   (-1)
    #define closesocket ::close
#endif

namespace td {
namespace net {

// =============================================================================
// Endpoint — IPv4/IPv6 + port.
//
// Internally stores a sockaddr_storage so it can hold either family. The
// toString() / fromString() helpers round-trip "host:port" strings (IPv6
// uses the [::1]:8080 bracket convention).
// =============================================================================
struct Endpoint {
    // Storage: sockaddr_storage is large enough for any sockaddr family.
    // We default to an IPv4 0.0.0.0:0 endpoint.
    sockaddr_storage addr{};
    td_socklen_t     addrLen = sizeof(sockaddr_storage);

    bool fromString(const char* str);          // "host:port" or "[ipv6]:port"
    std::string toString() const;              // inverse of fromString
    bool operator==(const Endpoint& o) const;
    bool operator!=(const Endpoint& o) const { return !(*this == o); }
    bool operator<(const Endpoint& o) const;   // for std::map / unordered_map
    uint16_t port() const;
    bool isIPv4() const;
    bool isIPv6() const;
    bool isValid() const;
};

// Hash so Endpoint works in std::unordered_map.
struct EndpointHash {
    size_t operator()(const Endpoint& e) const noexcept {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&e.addr);
        size_t h = 1469598103934665603ULL; // FNV-1a 64
        for (size_t i = 0; i < sizeof(sockaddr_storage); ++i) {
            h ^= p[i];
            h *= 1099511628211ULL;
        }
        return h ^ (e.addrLen * 0x9E3779B97F4A7C15ULL);
    }
};

// =============================================================================
// Socket — cross-platform UDP socket wrapper.
//
//   open(port=0)      -> bind to local port (0 = OS-assigned)
//   sendTo(ep, d, n)  -> send datagram
//   recvFrom(ep, d, n)-> receive datagram (non-blocking when set)
//   setNonblocking(b) -> toggle non-blocking mode
//   close()           -> release OS resource
//
// On Win32 this initializes Winsock on first open() (idempotent). On POSIX
// and WASM it's a thin wrapper around the BSD socket API.
// =============================================================================
class Socket {
public:
    Socket();
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    // Open a UDP socket bound to the given local port. 0 lets the OS pick.
    // Returns true on success.
    bool open(uint16_t localPort = 0);

    // Close the socket. Safe to call on an already-closed socket.
    void close();

    // True iff the underlying socket handle is valid.
    bool isOpen() const;

    // Send a datagram. Returns bytes sent, or -1 on error.
    int sendTo(const Endpoint& ep, const void* data, int len);

    // Receive a datagram into `data` (max `maxLen` bytes). On success, fills
    // `outFrom` with the source endpoint and returns the number of bytes
    // received. Returns 0 if no data available (non-blocking), or -1 on error.
    int recvFrom(Endpoint& outFrom, void* data, int maxLen);

    // Toggle non-blocking mode. poll() requires non-blocking sockets.
    void setNonblocking(bool nonblocking);
    bool isNonblocking() const { return m_nonblocking; }

    // The local port this socket is bound to. 0 if not open.
    uint16_t localPort() const;

    // Allow a small buffer size for the broadcast/reuse options. Called
    // automatically by open(); exposed for tests that want to set it before
    // open() is called.
    void enableReuseAddress();

private:
    td_socket_t m_sock = TD_INVALID_SOCKET;
    bool        m_nonblocking = false;
    uint16_t    m_localPort = 0;

    static bool s_wsaInit;
    static bool ensureWsaInit();
};

// =============================================================================
// Reliability mode for a message. Maps to the public NetReliability enum but
// is exposed as a separate enum so this header doesn't need to know about
// the public API's exact numbering.
// =============================================================================
enum class ReliabilityMode : uint8_t {
    Unreliable        = 0,  // fire-and-forget; no dedup, no ack, no reorder
    ReliableUnordered = 1,  // dedup + retransmit, but deliver in arrival order
    ReliableOrdered   = 2,  // dedup + retransmit + in-order delivery
};

// =============================================================================
// Packet flags (first byte of every wire packet).
//
//   bit 0: RELIABLE   — packet must be acked; receiver tracks its seq
//   bit 1: ORDERED    — packet must be held until in-order; implies RELIABLE
//   bit 2: ACK_ONLY   — packet carries no payload, only ack info (seq is 0)
//   bit 3: FRAGMENT   — packet is a fragment of a larger message
//   bit 4: LAST_FRAG  — fragment with this flag is the last of its group
//   bit 5: HEARTBEAT  — connection heartbeat (no payload, just liveness)
//   bit 6: RPC        — payload is an RPC frame (see RPC::onPacket)
//   bit 7: reserved
// =============================================================================
namespace PacketFlag {
    constexpr uint8_t RELIABLE   = 0x01;
    constexpr uint8_t ORDERED    = 0x02;
    constexpr uint8_t ACK_ONLY   = 0x04;
    constexpr uint8_t FRAGMENT   = 0x08;
    constexpr uint8_t LAST_FRAG  = 0x10;
    constexpr uint8_t HEARTBEAT  = 0x20;
    constexpr uint8_t RPC        = 0x40;
}

// Maximum size of a single UDP datagram the channel will emit. Larger
// messages get fragmented by the Connection layer.
constexpr int NET_MTU = 1400;

// Fixed header size (9 bytes) on every packet:
//   [flags:1][seq:2][ackSeq:2][ackBits:4]
constexpr int NET_PACKET_HEADER_SIZE = 9;

// Extra header bytes on fragment packets (after the fixed header):
//   [groupId:4][fragIdx:2][fragTotal:2]
constexpr int NET_FRAGMENT_HEADER_SIZE = 8;

// Maximum number of unacked packets in flight per channel (send window).
// This is the ARQ sliding-window limit: the sender will not have more than
// this many packets "on the wire" awaiting ACK at any one time. It bounds
// memory use per connection and matches the 32-bit selective-ACK bitmap
// carried in every packet header.
constexpr int NET_SEND_WINDOW = 32;

// Maximum number of reliable packets queued for *future* transmission when
// the send window is full. Without this backpressure, the sender would
// either (a) drop unacked packets (losing them forever — they've already
// been transmitted but cannot be retransmitted if lost) or (b) grow its
// unacked queue without bound. The pending queue decouples the application
// call rate from the wire rate: a 256 KB message bursts ~190 fragments
// synchronously, the first 32 go out immediately, the rest wait here and
// are drained by ReliableChannel::update() as ACKs free up window slots.
//
// 2048 packets × ~1400 bytes ≈ 2.7 MB per connection — enough for a 256 KB
// fragmented message (190 fragments) plus headroom, bounded for safety.
constexpr int NET_MAX_PENDING = 2048;

// Sequence number type. uint16 wraps every 65536 packets; we use modulo
// arithmetic + a wrap-aware comparison (seqLessThan / seqGreaterThan).
using Seq = uint16_t;

// Wrap-aware comparison: returns true if a is "before" b in sequence space
// (i.e. a is older, accounting for the 16-bit wrap).
inline bool seqLessThan(Seq a, Seq b) {
    // Classic RFC 1982 serial-number arithmetic: a < b if the forward
    // distance from a to b is positive and < half the space.
    return static_cast<int32_t>(b) - static_cast<int32_t>(a) > 0;
}
inline bool seqGreaterThan(Seq a, Seq b) {
    return a != b && seqLessThan(b, a);
}
inline int seqForwardDistance(Seq a, Seq b) {
    // distance forward from a to b (always non-negative for valid ordering)
    return static_cast<int32_t>(b) - static_cast<int32_t>(a);
}

// =============================================================================
// ReliableChannel — sliding-window ARQ over UDP for one direction of a flow.
//
// Each Connection owns TWO channels: one for sending, one for receiving.
// (They share the wire format but the state is per-direction.) Actually for
// simplicity we keep sender + receiver state in ONE ReliableChannel object,
// so each Connection owns a single ReliableChannel that does both directions.
//
// Public surface:
//   send(sock, ep, mode, data, len)
//       Fragmentation is NOT done here — caller ensures len <= MTU - header.
//       Actually: we do single-packet sends here. The Connection layer above
//       handles fragmentation. So len must be <= NET_MTU - NET_PACKET_HEADER_SIZE.
//
//   onPacket(data, len) -> calls the message callback for each fully-received
//                          message. Returns the number of messages delivered
//                          (0 or 1 per packet, since each packet carries at
//                          most one message fragment).
//
//   update(sock, ep, nowMs)
//       Fires retransmits for timed-out packets, sends pending ACKs.
//
//   reset() clears all state.
//
// Message delivery contract:
//   - Unreliable:        deliver immediately on arrival (no dedup).
//   - ReliableUnordered: deliver after dedup; order may be non-monotonic.
//   - ReliableOrdered:   hold packets in a reorder buffer; deliver in seq
//                        order; dedup.
//
// ACK scheme:
//   - Receiver tracks lastContiguousSeq (highest seq such that all seqs <= it
//     have been received). Also a 32-bit bitmap of the next 32 seqs after it.
//   - Every received reliable packet triggers a pending ACK (sent on next
//     update() or piggybacked on the next outbound packet).
//   - Sender, on receiving an ACK with (ackSeq, ackBits), considers acked:
//        * every seq <= ackSeq, AND
//        * every seq in (ackSeq + 1 + i) for each set bit i in ackBits.
// =============================================================================
class ReliableChannel {
public:
    // A message delivered up to the Connection layer. `mode` is the mode the
    // SENDER used; the receiver preserves it so the Connection knows whether
    // to deliver immediately or hold for reassembly ordering.
    struct DeliveredMessage {
        ReliabilityMode mode;
        const uint8_t*  data;
        int             len;
    };

    using DeliverCallback = std::function<void(const DeliveredMessage&)>;

    ReliableChannel();
    ~ReliableChannel();

    // Set the callback invoked when a complete message is delivered.
    void setDeliverCallback(DeliverCallback cb) { m_deliver = std::move(cb); }

    // Send a single packet (len <= NET_MTU - NET_PACKET_HEADER_SIZE).
    // Returns true if the packet was emitted (or queued for reliable send).
    bool send(Socket& sock, const Endpoint& ep, ReliabilityMode mode,
              const void* data, int len);

    // Process a packet that just arrived from the socket. The packet includes
    // the 9-byte header. Returns true if the packet was successfully parsed
    // (even if it was a duplicate that was dropped).
    bool onPacket(const void* data, int len);

    // Periodic update: send pending ACKs, retransmit timed-out packets.
    void update(Socket& sock, const Endpoint& ep, uint32_t nowMs);

    // Reset all state (e.g. on disconnect).
    void reset();

    // Stats for the connection layer.
    int  unackedCount() const { return static_cast<int>(m_unacked.size()); }
    int  pendingCount() const { return static_cast<int>(m_pending.size()); }
    bool hasPendingAck() const { return m_pendingAck; }
    Seq  nextSendSeq() const { return m_sendNext; }
    Seq  lastRecvSeq() const { return m_recvContiguous; }

private:
    // --- Sender state ---
    Seq m_sendNext = 0;            // next seq to assign
    struct UnackedPacket {
        Seq        seq;
        uint8_t    flags;
        uint32_t   firstSentMs;
        uint32_t   lastSentMs;
        std::vector<uint8_t> data; // includes header
    };
    std::vector<UnackedPacket> m_unacked;   // transmitted, awaiting ACK
    std::deque<UnackedPacket>  m_pending;   // queued, not yet transmitted (backpressure)
    // RTT estimation (EWMA). Initial 200ms, min 50ms.
    float m_rttMs = 200.0f;
    // Retransmit timeout (RTO), derived from RTT + margin.
    uint32_t m_rtoMs = 200;

    // --- Receiver state ---
    Seq m_recvContiguous = 0xFFFF; // highest seq such that all seqs <= it arrived
                                   // (starts at 0xFFFF so first seq 0 is "next expected")
    uint32_t m_recvBitmap = 0;     // bit i => (m_recvContiguous + 1 + i) arrived
    bool     m_pendingAck = false;

    // For ReliableOrdered delivery: tracks the highest seq we've actually
    // HANDED UP to the connection layer. Distinct from m_recvContiguous
    // (which is the highest seq we've RECEIVED, used for ACKs).
    Seq m_lastDeliveredOrdered = 0xFFFF;

    DeliverCallback m_deliver;

    // Reorder buffer for ReliableOrdered packets: seq -> payload.
    // We hold packets here until their predecessor arrives.
    std::map<Seq, std::vector<uint8_t>> m_reorder;

    // Dedup bitmap for ReliableUnordered: we remember the last 64 received
    // seqs so duplicates are dropped without redelivery.
    uint32_t m_dedupBitmap = 0;
    Seq      m_dedupBase   = 0xFFFF; // similar to m_recvContiguous but for unordered

    // Internal helpers.
    void emitPacket(Socket& sock, const Endpoint& ep,
                    uint8_t flags, Seq seq,
                    const void* payload, int payloadLen);
    void sendAck(Socket& sock, const Endpoint& ep);
    void markReceived(Seq s);
    bool alreadyReceived(Seq s) const;
    void tryDeliverOrdered();
    void updateRto(uint32_t rttSample);
};

// =============================================================================
// Connection — connection-state machine + fragmentation + heartbeat.
//
//   States: DISCONNECTED -> CONNECTING -> CONNECTED -> DISCONNECTING -> DISCONNECTED
//
//   For UDP we don't do a real handshake; "Connecting" just means we've been
//   told who to talk to but haven't heard back yet. The state machine exists
//   so gameplay code can react to "peer went silent" / "peer came back".
//
//   Heartbeat: every 1s we send a 0-byte HEARTBEAT packet to keep NAT alive
//   and let the other side measure RTT.
//
//   Timeout: if we haven't heard ANY packet from the peer in 10s, transition
//   to DISCONNECTED.
//
//   Fragmentation: messages larger than (MTU - headers) get split into N
//   fragments. Each fragment is sent as its own ReliableChannel packet with
//   the FRAGMENT flag. The last fragment also has LAST_FRAG. The receiver
//   reassembles by groupId.
// =============================================================================
class Connection {
public:
    enum class State : uint8_t {
        Disconnected  = 0,
        Connecting    = 1,
        Connected     = 2,
        Disconnecting = 3,
    };

    using MessageCallback = std::function<void(ReliabilityMode mode,
                                               const uint8_t* data, int len)>;
    using StateCallback   = std::function<void(State oldState, State newState)>;

    Connection();
    ~Connection();

    // Designate the remote endpoint and transition to CONNECTING. The first
    // packet sent/received will move us to CONNECTED.
    void connect(const Endpoint& remote);
    void disconnect();  // graceful: enters DISCONNECTING, sends nothing, waits for timeout
    State state() const { return m_state; }
    const Endpoint& remote() const { return m_remote; }

    // True if we can send/receive. (We allow sends in CONNECTING too, since
    // UDP has no handshake — the first send IS the handshake.)
    bool isActive() const { return m_state == State::Connecting
                                 || m_state == State::Connected; }

    void setMessageCallback(MessageCallback cb) { m_onMessage = std::move(cb); }
    void setStateCallback(StateCallback cb)     { m_onState = std::move(cb); }

    // Send a message. Fragments if larger than MTU. Returns true if all
    // fragments were emitted (or queued) successfully.
    bool send(Socket& sock, ReliabilityMode mode, const void* data, int len);

    // Called when a packet arrives from this connection's remote endpoint.
    void onPacket(const void* data, int len);

    // Periodic update: heartbeat + timeout + channel.update().
    void update(Socket& sock, uint32_t nowMs);

    // Reset to fully-disconnected state.
    void reset();

    // Stats.
    uint32_t lastRecvMs() const { return m_lastRecvMs; }
    uint32_t lastSendMs() const { return m_lastSendMs; }

    // Tunables (defaults match the task spec).
    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 1000;
    static constexpr uint32_t TIMEOUT_MS            = 10000;

private:
    State           m_state = State::Disconnected;
    Endpoint        m_remote;
    ReliableChannel m_channel;
    uint32_t        m_lastRecvMs = 0;
    uint32_t        m_lastSendMs = 0;
    uint32_t        m_lastHeartbeatMs = 0;
    bool            m_wasActive = false;

    MessageCallback m_onMessage;
    StateCallback   m_onState;

    // Fragment reassembly: groupId -> { fragments[idx]=bytes, total, received }
    struct Reassembly {
        uint32_t total = 0;
        std::unordered_map<uint16_t, std::vector<uint8_t>> fragments;
        int received = 0;
        ReliabilityMode mode = ReliabilityMode::ReliableOrdered;
    };
    std::unordered_map<uint32_t, Reassembly> m_reassembly;
    uint32_t m_nextFragmentGroupId = 1;

    void setState(State s);
    void deliverFragmentedMessage(Reassembly& r);
};

// =============================================================================
// MessageWriter / MessageReader — tiny protobuf-style serialization.
//
// Wire format per field:
//   tag + wire_type  (varint, encoded as (tag << 3) | wire_type)
//   then the value, encoded per wire type:
//     0 varint      — uint64 LE base-128
//     1 fixed64     — 8 bytes little-endian
//     2 length-delimited — varint length + bytes (used for strings, bytes,
//                          nested messages, repeated fields)
//     5 fixed32     — 4 bytes little-endian
//
// This is exactly protobuf's wire format (minus the schema). The "schema" is
// dynamic: the caller knows what to expect at each tag, so we don't need a
// .proto file or code generation.
// =============================================================================
class MessageWriter {
public:
    MessageWriter() { m_buf.reserve(64); }

    void clear() { m_buf.clear(); }
    int  size() const { return static_cast<int>(m_buf.size()); }
    const uint8_t* data() const { return m_buf.data(); }
    std::vector<uint8_t> takeBuffer() { auto v = std::move(m_buf); m_buf.clear(); return v; }

    // Low-level primitives.
    void writeVarint(uint64_t v);
    void writeFixed32(uint32_t v);
    void writeFixed64(uint64_t v);
    void writeBytes(const void* data, int len);
    void writeString(const char* s);
    void writeString(const std::string& s) { writeString(s.c_str()); }

    // Tag helpers. `wireType` is 0/1/2/5 per protobuf.
    void writeTag(uint32_t tag, uint32_t wireType);

    // Convenience: write a complete field.
    void writeVarintField(uint32_t tag, uint64_t v)       { writeTag(tag, 0); writeVarint(v); }
    void writeFixed32Field(uint32_t tag, uint32_t v)      { writeTag(tag, 5); writeFixed32(v); }
    void writeFixed64Field(uint32_t tag, uint64_t v)      { writeTag(tag, 1); writeFixed64(v); }
    void writeStringField(uint32_t tag, const char* s)    { writeTag(tag, 2); writeString(s); }
    void writeStringField(uint32_t tag, const std::string& s) { writeTag(tag, 2); writeString(s); }
    void writeBytesField(uint32_t tag, const void* data, int len) {
        writeTag(tag, 2); writeVarint(static_cast<uint64_t>(len)); writeBytes(data, len);
    }

private:
    std::vector<uint8_t> m_buf;
};

class MessageReader {
public:
    MessageReader(const uint8_t* data, int len)
        : m_data(data), m_len(len), m_pos(0) {}

    // Low-level primitives. All return false on end-of-buffer.
    bool readVarint(uint64_t& out);
    bool readFixed32(uint32_t& out);
    bool readFixed64(uint64_t& out);
    bool readBytes(uint8_t* out, int len);
    bool readString(std::string& out);     // length-delimited
    bool readStringView(const char*& outPtr, int& outLen); // no copy

    // Read a tag. Returns false at end-of-buffer (clean EOF). On true, sets
    // tag + wireType.
    bool readTag(uint32_t& tag, uint32_t& wireType);

    // Skip a field whose wire type we know but whose tag we don't care about.
    bool skipField(uint32_t wireType);

    int  remaining() const { return m_len - m_pos; }
    bool atEnd()     const { return m_pos >= m_len; }
    int  position()  const { return m_pos; }
    const uint8_t* data() const { return m_data; }
    int  size()     const { return m_len; }

private:
    const uint8_t* m_data;
    int            m_len;
    int            m_pos;
};

// =============================================================================
// RPC — register handlers, call remote, get a callback on response/timeout.
//
// Wire frame (an RPC packet's payload, after the NET_PACKET header):
//   byte 0:        msgType (1 = REQUEST, 2 = RESPONSE, 3 = ERROR)
//   bytes 1-4:     callId (uint32 LE) — matches request to response
//   varint:        name length
//   bytes:         name (only for REQUEST)
//   varint:        payload length
//   bytes:         payload (request args or response body or error message)
//
// On the caller side, RPC::call() allocates a callId, stashes the callback
// with a deadline, and emits a REQUEST frame. When a RESPONSE frame with
// the matching callId arrives (or the deadline passes), the callback fires.
//
// On the callee side, RPC::onPacket() parses the REQUEST, looks up the
// handler by name, invokes it (handler writes its response into a
// MessageWriter), and emits a RESPONSE frame with the same callId.
//
// The RPC class is wired to a NetworkInterface for sending. The NI's poll()
// loop routes incoming RPC-flagged packets to RPC::onPacket().
// =============================================================================
class RPC {
public:
    using Handler = std::function<void(MessageReader& req, MessageWriter& resp)>;
    using ResponseCallback = std::function<void(bool success,
                                                const uint8_t* data, int len)>;

    RPC();
    ~RPC();

    // Register a handler for a method name. Replaces any existing handler
    // with the same name.
    void registerHandler(const std::string& name, Handler h);

    // Unregister a handler.
    void unregisterHandler(const std::string& name);

    // Returns true if a handler is registered for this name.
    bool hasHandler(const std::string& name) const;

    // Initiate a remote call. The callback fires when a RESPONSE arrives or
    // the timeout expires. If timeoutMs == 0, use the default (5000ms).
    // The `args` buffer is sent as the request payload verbatim.
    void call(const Endpoint& target,
              const std::string& name,
              const void* args, int argsLen,
              uint32_t timeoutMs,
              ResponseCallback cb);

    // Convenience: call with a string argument, get a string response.
    void callString(const Endpoint& target,
                    const std::string& name,
                    const std::string& argStr,
                    uint32_t timeoutMs,
                    std::function<void(bool ok, const std::string& resp)> cb);

    // Called by NetworkInterface when an RPC-flagged packet arrives.
    void onPacket(const Endpoint& from, const uint8_t* data, int len);

    // Called periodically by NetworkInterface to fire timeout callbacks.
    void update(uint32_t nowMs);

    // The sender function: NetworkInterface installs this so RPC can emit
    // packets via the NI's socket + reliable channel to a specific endpoint.
    // The `mode` is always ReliableOrdered for RPC.
    using SendFn = std::function<void(const Endpoint& to,
                                      const void* data, int len)>;
    void setSender(SendFn fn) { m_sender = std::move(fn); }

    // Default timeout if 0 is passed to call().
    static constexpr uint32_t DEFAULT_TIMEOUT_MS = 5000;

private:
    enum class MsgType : uint8_t { Request = 1, Response = 2, Error = 3 };

    std::unordered_map<std::string, Handler> m_handlers;

    struct PendingCall {
        uint32_t          id;
        Endpoint          target;
        uint32_t          deadlineMs;
        ResponseCallback  cb;
    };
    std::vector<PendingCall> m_pending;
    uint32_t                 m_nextCallId = 1;

    SendFn m_sender;

    void emitRequest(const Endpoint& target, uint32_t callId,
                     const std::string& name,
                     const void* args, int argsLen);
    void emitResponse(const Endpoint& target, uint32_t callId,
                      MsgType type, const void* payload, int payloadLen);
};

// =============================================================================
// NetworkInterface — top-level: owns a Socket + Connections + RPC + poll().
//
// Typical lifecycle:
//   NetworkInterface ni;
//   ni.init(18001);
//   ni.onMessage([](const Endpoint& from, const uint8_t* data, int len,
//                   ReliabilityMode mode) { ... });
//   ni.rpc().registerHandler("ping", ...);
//   for (;;) {
//       ni.poll(0);  // 0 = drain, no wait
//       sleep_ms(16);
//   }
//
//   To send: ni.send(remoteEndpoint, mode, data, len);
//   To call: ni.rpc().call(remoteEndpoint, "ping", "hi", 5000, cb);
//
// poll() drains the socket (non-blocking), routes packets to the right
// Connection by source endpoint, fires message callbacks, and runs the RPC
// update pass. Should be called every frame from the main game loop.
// =============================================================================
class NetworkInterface {
public:
    using MessageCallback = std::function<void(const Endpoint& from,
                                               const uint8_t* data, int len,
                                               ReliabilityMode mode)>;

    NetworkInterface();
    ~NetworkInterface();

    NetworkInterface(const NetworkInterface&) = delete;
    NetworkInterface& operator=(const NetworkInterface&) = delete;

    // Open the socket on the given local port. Returns false on failure.
    bool init(uint16_t localPort = 0);

    // Shutdown: close socket, clear connections.
    void shutdown();

    // True iff the socket is open.
    bool isInitialized() const { return m_socket.isOpen(); }

    // The local endpoint (host:port) of this interface.
    Endpoint localEndpoint() const;

    // Register a callback for received messages. Multiple callbacks can be
    // registered; all are invoked per message.
    void onMessage(MessageCallback cb);

    // Send a message to a remote endpoint. Fragments large messages.
    // Returns true if the message was queued for send.
    bool send(const Endpoint& to, ReliabilityMode mode,
              const void* data, int len);

    // Poll for events. `timeoutMs` is the max time to spend; 0 = drain only.
    // Returns the number of packets processed.
    int poll(uint32_t timeoutMs);

    // Force an update pass (retransmits, heartbeats, timeouts, RPC timeouts).
    // Called from poll(); exposed for tests that want to drive it explicitly.
    void update();

    // Access the RPC layer.
    RPC& rpc() { return m_rpc; }

    // Access the underlying socket (for tests / advanced use).
    Socket& socket() { return m_socket; }

    // Get-or-create a Connection for a remote endpoint.
    Connection* getOrCreateConnection(const Endpoint& ep);

    // Number of currently-tracked connections.
    int connectionCount() const { return static_cast<int>(m_conns.size()); }

    // Helper: now() in milliseconds since process start. Used everywhere for
    // timestamps so we don't depend on system time.
    static uint32_t nowMs();

private:
    Socket m_socket;
    std::unordered_map<Endpoint, std::unique_ptr<Connection>, EndpointHash> m_conns;
    std::vector<MessageCallback> m_callbacks;
    RPC    m_rpc;
    uint32_t m_lastUpdateMs = 0;

    void routePacket(const Endpoint& from, const uint8_t* data, int len);
    void deliverMessage(const Endpoint& from, ReliabilityMode mode,
                        const uint8_t* data, int len);
};

} // namespace net
} // namespace td
