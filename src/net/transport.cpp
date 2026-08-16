// =============================================================================
// TD Engine - Network Transport (REAL implementation)
//
// Implements every type declared in transport_impl.h plus the public RpcServer
// methods declared in transport.h. Cross-platform: Win32 (Winsock2), POSIX
// (BSD sockets), WASM (Emscripten — POSIX headers proxied to JS).
//
// Design notes:
//   - All packets share a 9-byte fixed header: [flags][seq][ackSeq][ackBits].
//   - Fragment packets add an 8-byte fragment header after the fixed header.
//   - The ReliableChannel handles one packet at a time (no fragmentation);
//     the Connection layer splits large messages into fragments.
//   - poll() never blocks: the socket is set non-blocking during init().
//   - RPC frames use a tiny custom wire format (msgType + callId + name + body)
//     built on top of MessageWriter/Reader.
// =============================================================================
#include "transport_impl.h"
#include "json_rpc.h"

#include <cerrno>     // errno on POSIX (WSAGetLastError is used on Win32 instead)
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <utility>

namespace td {
namespace net {

// ---------------------------------------------------------------------------
// Platform helpers
// ---------------------------------------------------------------------------

#if defined(TD_NET_WIN32)
bool Socket::s_wsaInit = false;

bool Socket::ensureWsaInit() {
    if (s_wsaInit) return true;
    WSADATA wsaData;
    int err = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (err != 0) {
        TD_LOG_ERROR("WSAStartup failed: %d", err);
        return false;
    }
    s_wsaInit = true;
    return true;
}
#else
bool Socket::s_wsaInit = false;       // never used on POSIX; declared for ABI
bool Socket::ensureWsaInit() { return true; }
#endif

// Monotonic milliseconds since process start. Used as the time base for all
// timeout / RTT logic. Wraps every ~49 days (uint32_t); we don't worry about
// it because all comparisons use signed deltas.
static uint32_t monotonicNowMs() {
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    return static_cast<uint32_t>(ms);
}

// ---------------------------------------------------------------------------
// Endpoint
// ---------------------------------------------------------------------------

bool Endpoint::fromString(const char* str) {
    if (!str || !*str) return false;
    std::string s = str;

    // IPv6 with brackets: [::1]:8080
    if (s[0] == '[') {
        auto close = s.find(']');
        if (close == std::string::npos) return false;
        std::string host = s.substr(1, close - 1);
        std::string portStr;
        if (close + 1 < s.size() && s[close + 1] == ':') {
            portStr = s.substr(close + 2);
        } else {
            return false;
        }
        int port = std::atoi(portStr.c_str());
        if (port <= 0 || port > 65535) return false;

        sockaddr_in6* a = reinterpret_cast<sockaddr_in6*>(&addr);
        std::memset(a, 0, sizeof(*a));
        a->sin6_family = AF_INET6;
        a->sin6_port   = htons(static_cast<uint16_t>(port));
        if (inet_pton(AF_INET6, host.c_str(), &a->sin6_addr) != 1) {
            // Try getaddrinfo as a fallback (for hostnames).
            addrinfo hints{};
            hints.ai_family = AF_INET6;
            hints.ai_socktype = SOCK_DGRAM;
            addrinfo* res = nullptr;
            if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
                if (res) freeaddrinfo(res);
                return false;
            }
            std::memcpy(a, res->ai_addr, res->ai_addrlen);
            a->sin6_port = htons(static_cast<uint16_t>(port));
            freeaddrinfo(res);
        }
        addrLen = sizeof(sockaddr_in6);
        return true;
    }

    // IPv4: host:port
    auto colon = s.rfind(':');
    if (colon == std::string::npos) return false;
    std::string host = s.substr(0, colon);
    std::string portStr = s.substr(colon + 1);
    int port = std::atoi(portStr.c_str());
    if (port <= 0 || port > 65535) return false;

    sockaddr_in* a = reinterpret_cast<sockaddr_in*>(&addr);
    std::memset(a, 0, sizeof(*a));
    a->sin_family = AF_INET;
    a->sin_port   = htons(static_cast<uint16_t>(port));

    // Try numeric IPv4 first.
    if (inet_pton(AF_INET, host.c_str(), &a->sin_addr) != 1) {
        // Fall back to getaddrinfo for hostnames ("localhost").
        addrinfo hints{};
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
            if (res) freeaddrinfo(res);
            return false;
        }
        std::memcpy(a, res->ai_addr, res->ai_addrlen);
        a->sin_port = htons(static_cast<uint16_t>(port));
        freeaddrinfo(res);
    }
    addrLen = sizeof(sockaddr_in);
    return true;
}

std::string Endpoint::toString() const {
    if (!isValid()) return "<invalid>";
    char buf[INET6_ADDRSTRLEN + 16] = {};
    uint16_t port = 0;

    if (addr.ss_family == AF_INET) {
        const sockaddr_in* a = reinterpret_cast<const sockaddr_in*>(&addr);
        port = ntohs(a->sin_port);
        inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf));
        return std::string(buf) + ":" + std::to_string(port);
    } else if (addr.ss_family == AF_INET6) {
        const sockaddr_in6* a = reinterpret_cast<const sockaddr_in6*>(&addr);
        port = ntohs(a->sin6_port);
        inet_ntop(AF_INET6, &a->sin6_addr, buf, sizeof(buf));
        return std::string("[") + buf + "]:" + std::to_string(port);
    }
    return "<unknown>";
}

uint16_t Endpoint::port() const {
    if (addr.ss_family == AF_INET) {
        return ntohs(reinterpret_cast<const sockaddr_in*>(&addr)->sin_port);
    }
    if (addr.ss_family == AF_INET6) {
        return ntohs(reinterpret_cast<const sockaddr_in6*>(&addr)->sin6_port);
    }
    return 0;
}

bool Endpoint::isIPv4() const { return addr.ss_family == AF_INET; }
bool Endpoint::isIPv6() const { return addr.ss_family == AF_INET6; }
bool Endpoint::isValid() const {
    return addr.ss_family == AF_INET || addr.ss_family == AF_INET6;
}

bool Endpoint::operator==(const Endpoint& o) const {
    if (addr.ss_family != o.addr.ss_family) return false;
    if (addrLen != o.addrLen) return false;
    return std::memcmp(&addr, &o.addr, addrLen) == 0;
}

bool Endpoint::operator<(const Endpoint& o) const {
    if (addr.ss_family != o.addr.ss_family) return addr.ss_family < o.addr.ss_family;
    return std::memcmp(&addr, &o.addr, sizeof(addr)) < 0;
}

// ---------------------------------------------------------------------------
// Socket
// ---------------------------------------------------------------------------

Socket::Socket() = default;

Socket::~Socket() {
    close();
}

void Socket::enableReuseAddress() {
    if (m_sock == TD_INVALID_SOCKET) return;
    int yes = 1;
    setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&yes), sizeof(yes));
}

bool Socket::open(uint16_t localPort) {
    if (m_sock != TD_INVALID_SOCKET) close();
    if (!ensureWsaInit()) return false;

    m_sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_sock == TD_INVALID_SOCKET) {
#if defined(TD_NET_WIN32)
        TD_LOG_ERROR("socket() failed: WSA error=%d", WSAGetLastError());
#else
        TD_LOG_ERROR("socket() failed: errno=%d", errno);
#endif
        return false;
    }

    enableReuseAddress();

    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port   = htons(localPort);
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(m_sock, reinterpret_cast<sockaddr*>(&bindAddr),
               sizeof(bindAddr)) == TD_SOCKET_ERROR) {
#if defined(TD_NET_WIN32)
        TD_LOG_ERROR("bind(port=%u) failed: WSA error=%d", localPort, WSAGetLastError());
#else
        TD_LOG_ERROR("bind(port=%u) failed: errno=%d", localPort, errno);
#endif
        closesocket(m_sock);
        m_sock = TD_INVALID_SOCKET;
        return false;
    }

    // Discover the actual bound port (in case localPort was 0).
    td_socklen_t len = sizeof(bindAddr);
    if (::getsockname(m_sock, reinterpret_cast<sockaddr*>(&bindAddr), &len) == 0) {
        m_localPort = ntohs(bindAddr.sin_port);
    } else {
        m_localPort = localPort;
    }

    setNonblocking(true);
    return true;
}

void Socket::close() {
    if (m_sock == TD_INVALID_SOCKET) return;
    closesocket(m_sock);
    m_sock = TD_INVALID_SOCKET;
    m_localPort = 0;
    m_nonblocking = false;
}

bool Socket::isOpen() const {
    return m_sock != TD_INVALID_SOCKET;
}

int Socket::sendTo(const Endpoint& ep, const void* data, int len) {
    if (m_sock == TD_INVALID_SOCKET) return -1;
    if (!ep.isValid()) return -1;
    td_ssize_t n = ::sendto(m_sock, reinterpret_cast<const char*>(data), len, 0,
                            reinterpret_cast<const sockaddr*>(&ep.addr),
                            ep.addrLen);
    if (n == TD_SOCKET_ERROR) {
#ifdef TD_NET_POSIX
        // EAGAIN / EWOULDBLOCK on non-blocking socket — benign.
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
#endif
        return -1;
    }
    return static_cast<int>(n);
}

int Socket::recvFrom(Endpoint& outFrom, void* data, int maxLen) {
    if (m_sock == TD_INVALID_SOCKET) return -1;
    td_socklen_t fromLen = sizeof(outFrom.addr);
    td_ssize_t n = ::recvfrom(m_sock, reinterpret_cast<char*>(data), maxLen, 0,
                              reinterpret_cast<sockaddr*>(&outFrom.addr),
                              &fromLen);
    if (n == TD_SOCKET_ERROR) {
#ifdef TD_NET_POSIX
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
#endif
#if defined(TD_NET_WIN32)
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return 0;
#endif
        return -1;
    }
    outFrom.addrLen = fromLen;
    return static_cast<int>(n);
}

void Socket::setNonblocking(bool nonblocking) {
    m_nonblocking = nonblocking;
    if (m_sock == TD_INVALID_SOCKET) return;
#if defined(TD_NET_WIN32)
    u_long mode = nonblocking ? 1 : 0;
    ioctlsocket(m_sock, FIONBIO, &mode);
#else
    int flags = fcntl(m_sock, F_GETFL, 0);
    if (nonblocking) flags |= O_NONBLOCK;
    else             flags &= ~O_NONBLOCK;
    fcntl(m_sock, F_SETFL, flags);
#endif
}

uint16_t Socket::localPort() const {
    return m_localPort;
}

// ---------------------------------------------------------------------------
// Packet header read/write helpers
// ---------------------------------------------------------------------------

static void writePacketHeader(uint8_t* buf, uint8_t flags, Seq seq,
                              Seq ackSeq, uint32_t ackBits) {
    buf[0] = flags;
    uint16_t s = seq;
    uint16_t a = ackSeq;
    buf[1] = static_cast<uint8_t>(s & 0xFF);
    buf[2] = static_cast<uint8_t>((s >> 8) & 0xFF);
    buf[3] = static_cast<uint8_t>(a & 0xFF);
    buf[4] = static_cast<uint8_t>((a >> 8) & 0xFF);
    buf[5] = static_cast<uint8_t>(ackBits & 0xFF);
    buf[6] = static_cast<uint8_t>((ackBits >> 8) & 0xFF);
    buf[7] = static_cast<uint8_t>((ackBits >> 16) & 0xFF);
    buf[8] = static_cast<uint8_t>((ackBits >> 24) & 0xFF);
}

static void readPacketHeader(const uint8_t* buf, uint8_t& flags, Seq& seq,
                             Seq& ackSeq, uint32_t& ackBits) {
    flags   = buf[0];
    seq     = static_cast<Seq>(buf[1] | (buf[2] << 8));
    ackSeq  = static_cast<Seq>(buf[3] | (buf[4] << 8));
    ackBits = static_cast<uint32_t>(buf[5])
            | (static_cast<uint32_t>(buf[6]) << 8)
            | (static_cast<uint32_t>(buf[7]) << 16)
            | (static_cast<uint32_t>(buf[8]) << 24);
}

[[maybe_unused]] static void writeFragmentHeader(uint8_t* buf, uint32_t groupId,
                                                uint16_t fragIdx, uint16_t fragTotal) {
    buf[0] = static_cast<uint8_t>(groupId & 0xFF);
    buf[1] = static_cast<uint8_t>((groupId >> 8) & 0xFF);
    buf[2] = static_cast<uint8_t>((groupId >> 16) & 0xFF);
    buf[3] = static_cast<uint8_t>((groupId >> 24) & 0xFF);
    buf[4] = static_cast<uint8_t>(fragIdx & 0xFF);
    buf[5] = static_cast<uint8_t>((fragIdx >> 8) & 0xFF);
    buf[6] = static_cast<uint8_t>(fragTotal & 0xFF);
    buf[7] = static_cast<uint8_t>((fragTotal >> 8) & 0xFF);
}

[[maybe_unused]] static void readFragmentHeader(const uint8_t* buf, uint32_t& groupId,
                                                uint16_t& fragIdx, uint16_t& fragTotal) {
    groupId  = static_cast<uint32_t>(buf[0])
             | (static_cast<uint32_t>(buf[1]) << 8)
             | (static_cast<uint32_t>(buf[2]) << 16)
             | (static_cast<uint32_t>(buf[3]) << 24);
    fragIdx  = static_cast<uint16_t>(buf[4] | (buf[5] << 8));
    fragTotal= static_cast<uint16_t>(buf[6] | (buf[7] << 8));
}

// ---------------------------------------------------------------------------
// ReliableChannel
// ---------------------------------------------------------------------------

ReliableChannel::ReliableChannel() = default;
ReliableChannel::~ReliableChannel() = default;

void ReliableChannel::reset() {
    m_sendNext = 0;
    m_unacked.clear();
    m_pending.clear();
    m_rttMs = 200.0f;
    m_rtoMs = 200;
    m_recvContiguous = 0xFFFF;
    m_recvBitmap = 0;
    m_pendingAck = false;
    m_lastDeliveredOrdered = 0xFFFF;
    m_dedupBitmap = 0;
    m_dedupBase  = 0xFFFF;
    m_reorder.clear();
}

bool ReliableChannel::send(Socket& sock, const Endpoint& ep, ReliabilityMode mode,
                           const void* data, int len) {
    if (len < 0 || len > NET_MTU - NET_PACKET_HEADER_SIZE) {
        return false;
    }

    uint8_t flags = 0;
    Seq seq = 0;
    switch (mode) {
        case ReliabilityMode::Unreliable:
            flags = 0;
            seq   = m_sendNext++;
            break;
        case ReliabilityMode::ReliableUnordered:
            flags = PacketFlag::RELIABLE;
            seq   = m_sendNext++;
            break;
        case ReliabilityMode::ReliableOrdered:
            flags = PacketFlag::RELIABLE | PacketFlag::ORDERED;
            seq   = m_sendNext++;
            break;
    }

    // Piggyback our pending ACK onto this outbound packet.
    Seq ackSeq = m_recvContiguous;
    uint32_t ackBits = m_recvBitmap;
    m_pendingAck = false;

    // Build the wire packet.
    uint8_t buf[NET_MTU];
    writePacketHeader(buf, flags, seq, ackSeq, ackBits);
    if (len > 0 && data) {
        std::memcpy(buf + NET_PACKET_HEADER_SIZE, data, static_cast<size_t>(len));
    }
    int totalLen = NET_PACKET_HEADER_SIZE + len;

    // For reliable packets, stash a copy for retransmit. The send window
    // bounds the in-flight (transmitted, unacked) set; if it's full we
    // queue the packet in m_pending and let update() drain it as ACKs
    // arrive. This is proper backpressure — previously the code dropped
    // the oldest unacked packet (which had already been transmitted),
    // making it impossible to retransmit if it was lost on the wire.
    if (flags & PacketFlag::RELIABLE) {
        // Bound the pending queue to prevent unbounded memory growth under
        // sustained oversend. The cap is large enough for a 256 KB
        // fragmented message (~190 fragments) plus headroom.
        if (static_cast<int>(m_unacked.size() + m_pending.size()) >=
            NET_MAX_PENDING) {
            return false;  // hard backpressure — caller must retry
        }

        UnackedPacket u;
        u.seq   = seq;
        u.flags = flags;
        u.data.assign(buf, buf + totalLen);

        if (static_cast<int>(m_unacked.size()) < NET_SEND_WINDOW) {
            // Window has space — transmit immediately and stash for retransmit.
            u.firstSentMs = monotonicNowMs();
            u.lastSentMs  = u.firstSentMs;
            sock.sendTo(ep, u.data.data(), static_cast<int>(u.data.size()));
            m_unacked.push_back(std::move(u));
        } else {
            // Window full — queue for later transmission. firstSentMs = 0
            // signals "not yet on the wire"; update() will set it when it
            // drains the pending queue.
            u.firstSentMs = 0;
            u.lastSentMs  = 0;
            m_pending.push_back(std::move(u));
        }
    } else {
        // Unreliable: transmit immediately, no queue, no retransmit.
        sock.sendTo(ep, buf, totalLen);
    }
    return true;
}

void ReliableChannel::emitPacket(Socket& sock, const Endpoint& ep,
                                 uint8_t flags, Seq seq,
                                 const void* payload, int payloadLen) {
    uint8_t buf[NET_MTU];
    writePacketHeader(buf, flags, seq, m_recvContiguous, m_recvBitmap);
    if (payloadLen > 0 && payload) {
        std::memcpy(buf + NET_PACKET_HEADER_SIZE, payload,
                    static_cast<size_t>(payloadLen));
    }
    sock.sendTo(ep, buf, NET_PACKET_HEADER_SIZE + payloadLen);
    m_pendingAck = false;
}

void ReliableChannel::sendAck(Socket& sock, const Endpoint& ep) {
    uint8_t buf[NET_PACKET_HEADER_SIZE];
    writePacketHeader(buf, PacketFlag::ACK_ONLY, 0, m_recvContiguous, m_recvBitmap);
    sock.sendTo(ep, buf, NET_PACKET_HEADER_SIZE);
    m_pendingAck = false;
}

bool ReliableChannel::onPacket(const void* data, int len) {
    if (len < NET_PACKET_HEADER_SIZE) return false;
    const uint8_t* buf = static_cast<const uint8_t*>(data);
    uint8_t flags;
    Seq seq, ackSeq;
    uint32_t ackBits;
    readPacketHeader(buf, flags, seq, ackSeq, ackBits);

    // Process the ACK info carried by this packet (regardless of whether
    // the packet itself is reliable or just an ACK).
    if (ackSeq != 0xFFFF || ackBits != 0) {
        // Remove from unacked every packet with seq <= ackSeq (forward).
        for (auto it = m_unacked.begin(); it != m_unacked.end(); ) {
            Seq s = it->seq;
            bool acked = false;
            if (s == ackSeq) {
                acked = true;
            } else if (seqLessThan(s, ackSeq)) {
                acked = true;
            } else {
                // Check the bitmap: bit i represents ackSeq + 1 + i.
                int diff = seqForwardDistance(ackSeq, s);
                if (diff >= 1 && diff <= 32) {
                    if (ackBits & (1u << (diff - 1))) acked = true;
                }
            }
            if (acked) {
                // Update RTT estimate using this packet's round-trip time.
                uint32_t rtt = monotonicNowMs() - it->firstSentMs;
                updateRto(rtt);
                it = m_unacked.erase(it);
            } else {
                ++it;
            }
        }
    }

    // If this is an ACK-only packet, no payload to deliver.
    if (flags & PacketFlag::ACK_ONLY) {
        return true;
    }

    const uint8_t* payload = buf + NET_PACKET_HEADER_SIZE;
    int payloadLen = len - NET_PACKET_HEADER_SIZE;

    // If reliable: dedup + ack. If ordered: hold for in-order delivery.
    if (flags & PacketFlag::RELIABLE) {
        if (alreadyReceived(seq)) {
            // Duplicate — re-ack (so the sender stops retransmitting) but
            // don't deliver again.
            m_pendingAck = true;
            return true;
        }
        markReceived(seq);
        m_pendingAck = true;

        if (flags & PacketFlag::ORDERED) {
            // Stash in reorder buffer; deliver in order.
            std::vector<uint8_t> copy;
            if (payloadLen > 0) copy.assign(payload, payload + payloadLen);
            m_reorder[seq] = std::move(copy);
            tryDeliverOrdered();
        } else {
            // ReliableUnordered: deliver immediately (after dedup).
            if (m_deliver) {
                DeliveredMessage msg;
                msg.mode = ReliabilityMode::ReliableUnordered;
                msg.data = payload;
                msg.len  = payloadLen;
                m_deliver(msg);
            }
        }
    } else {
        // Unreliable: deliver immediately, no dedup.
        if (m_deliver) {
            DeliveredMessage msg;
            msg.mode = ReliabilityMode::Unreliable;
            msg.data = payload;
            msg.len  = payloadLen;
            m_deliver(msg);
        }
    }
    return true;
}

void ReliableChannel::markReceived(Seq s) {
    // Update the receiver's view: m_recvContiguous is the highest seq such
    // that all seqs <= it have been received. The bitmap covers the 32 seqs
    // after that.
    //
    // Sentinel handling: m_recvContiguous starts at 0xFFFF meaning "nothing
    // received yet". The generic seqLessThan check below would interpret
    // s=0 as "older than 0xFFFF" (since 0 < 0xFFFF in normal arithmetic)
    // and return early — which would leave m_recvContiguous stuck at the
    // sentinel forever, making every ACK a no-op (ackSeq=0xFFFF means
    // "no ack info" on the wire). The sender would never see its window
    // open up and large fragmented messages would never drain.
    if (m_recvContiguous == 0xFFFF) {
        // First packet ever received on this channel. Seed the contiguous
        // cursor so that s becomes "received" — set the cursor to s, then
        // fall through so the dedup bitmap also gets seeded.
        m_recvContiguous = s;
        // No bitmap math needed: the cursor == s means s is accounted for.
        // Skip ahead to dedup update.
        if (m_dedupBase == 0xFFFF && m_dedupBitmap == 0) {
            m_dedupBase = s;
            m_dedupBitmap = 1u;
        } else {
            int ddiff = seqForwardDistance(m_dedupBase, s);
            if (ddiff >= 0 && ddiff < 32) {
                m_dedupBitmap |= (1u << ddiff);
            } else if (ddiff >= 32) {
                m_dedupBase = s;
                m_dedupBitmap = 1u;
            }
        }
        return;
    }
    if (s == m_recvContiguous) {
        // shouldn't happen (already received), ignore.
        return;
    }
    if (seqLessThan(s, m_recvContiguous)) {
        // Older than our contiguous window — already accounted for, ignore.
        return;
    }
    // Set the bit for s in the bitmap (relative to m_recvContiguous + 1).
    int diff = seqForwardDistance(m_recvContiguous, s); // s - m_recvContiguous
    if (diff >= 1 && diff <= 32) {
        m_recvBitmap |= (1u << (diff - 1));
    } else if (diff > 32) {
        // Way ahead of the window. We can't represent this in a 32-bit bitmap,
        // so we shift m_recvContiguous forward to (s - 32) and set the bit
        // for s. This loses ACK info for some intermediate seqs, but those
        // will be retransmitted.
        m_recvContiguous = static_cast<Seq>(s - 32);
        m_recvBitmap = 0;
        m_recvBitmap |= (1u << 31); // bit 31 = m_recvContiguous + 32 = s
    }
    // Now advance m_recvContiguous as far as the bitmap allows.
    while (m_recvBitmap & 1u) {
        m_recvContiguous = m_recvContiguous + 1;
        m_recvBitmap >>= 1;
    }

    // Also update the unordered dedup bitmap (independent of contiguous).
    if (m_dedupBase == 0xFFFF && m_dedupBitmap == 0) {
        m_dedupBase = s;
        m_dedupBitmap = 1u;
    } else {
        int ddiff = seqForwardDistance(m_dedupBase, s);
        if (ddiff >= 0 && ddiff < 32) {
            m_dedupBitmap |= (1u << ddiff);
        } else if (ddiff >= 32) {
            m_dedupBase = s;
            m_dedupBitmap = 1u;
        }
        // If ddiff < 0 the packet is older than dedupBase — assume not received.
    }
}

bool ReliableChannel::alreadyReceived(Seq s) const {
    if (m_dedupBase == 0xFFFF && m_dedupBitmap == 0) return false;
    if (s == m_dedupBase) return (m_dedupBitmap & 1u) != 0;
    if (seqLessThan(s, m_dedupBase)) {
        // Older than our dedup window — be conservative, treat as NOT received.
        // (Sender will retransmit if it didn't get an ACK, and we'll dedup
        // via the contiguous-ACK path then.)
        return false;
    }
    int diff = seqForwardDistance(m_dedupBase, s);
    if (diff >= 0 && diff < 32) {
        return (m_dedupBitmap & (1u << diff)) != 0;
    }
    return false;
}

void ReliableChannel::tryDeliverOrdered() {
    // Deliver in seq order, starting from (m_lastDeliveredOrdered + 1).
    // We keep pulling entries out of m_reorder until the next expected seq
    // isn't present.
    Seq expected = m_lastDeliveredOrdered + 1;
    while (true) {
        auto it = m_reorder.find(expected);
        if (it == m_reorder.end()) break;
        if (m_deliver) {
            DeliveredMessage msg;
            msg.mode = ReliabilityMode::ReliableOrdered;
            msg.data = it->second.data();
            msg.len  = static_cast<int>(it->second.size());
            m_deliver(msg);
        }
        m_reorder.erase(it);
        m_lastDeliveredOrdered = expected;
        expected = expected + 1;
    }
}

void ReliableChannel::updateRto(uint32_t rttSample) {
    // EWMA: 7/8 old + 1/8 new, with a floor.
    if (rttSample > 0 && rttSample < 5000) {
        m_rttMs = 0.875f * m_rttMs + 0.125f * static_cast<float>(rttSample);
    }
    // RTO = max(2 * RTT, 50ms), capped at 2s.
    uint32_t rto = static_cast<uint32_t>(m_rttMs * 2.0f);
    if (rto < 50)   rto = 50;
    if (rto > 2000) rto = 2000;
    m_rtoMs = rto;
}

void ReliableChannel::update(Socket& sock, const Endpoint& ep, uint32_t nowMs) {
    // Drain the pending (backpressure) queue: as ACKs free up window slots,
    // transmit queued packets and promote them into m_unacked.
    while (!m_pending.empty() &&
           static_cast<int>(m_unacked.size()) < NET_SEND_WINDOW) {
        UnackedPacket& u = m_pending.front();
        u.firstSentMs = nowMs;
        u.lastSentMs  = nowMs;
        sock.sendTo(ep, u.data.data(), static_cast<int>(u.data.size()));
        m_unacked.push_back(std::move(u));
        m_pending.pop_front();
    }

    // Retransmit timed-out packets.
    for (auto& u : m_unacked) {
        uint32_t elapsed = nowMs - u.lastSentMs;
        if (elapsed >= m_rtoMs) {
            sock.sendTo(ep, u.data.data(), static_cast<int>(u.data.size()));
            u.lastSentMs = nowMs;
        }
    }

    // Send a standalone ACK if we have pending acks and didn't piggyback.
    if (m_pendingAck) {
        sendAck(sock, ep);
    }
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

Connection::Connection() {
    m_channel.setDeliverCallback([this](const ReliableChannel::DeliveredMessage& msg) {
        // The Channel only delivers non-fragment payloads. Fragments are
        // tagged in the FRAGMENT flag and reassembled at this layer. But
        // since the channel delivers raw payload, we check the message mode
        // to decide what to do.
        //
        // Actually, the channel doesn't pass the flags through; only the
        // mode. So we re-stash fragment info via a separate header that the
        // Connection layer prepends BEFORE handing the payload to the channel.
        //
        // See Connection::send() for the framing.
        if (m_onMessage) {
            m_onMessage(msg.mode, msg.data, msg.len);
        }
    });
}

Connection::~Connection() = default;

void Connection::connect(const Endpoint& remote) {
    m_remote = remote;
    setState(State::Connecting);
    m_channel.reset();
    m_lastRecvMs = monotonicNowMs();
    m_lastSendMs = m_lastRecvMs;
    m_lastHeartbeatMs = m_lastRecvMs;
    m_wasActive = true;
}

void Connection::disconnect() {
    if (m_state == State::Disconnected) return;
    setState(State::Disconnecting);
    // We don't send an explicit disconnect packet here (UDP). The peer will
    // time us out. Real production code would send a "bye" packet.
    m_wasActive = false;
    setState(State::Disconnected);
}

void Connection::reset() {
    m_state = State::Disconnected;
    m_remote = Endpoint{};
    m_channel.reset();
    m_reassembly.clear();
    m_wasActive = false;
}

void Connection::setState(State s) {
    if (m_state == s) return;
    State old = m_state;
    m_state = s;
    if (m_onState) m_onState(old, s);
}

bool Connection::send(Socket& sock, ReliabilityMode mode,
                      const void* data, int len) {
    if (len < 0) return false;
    if (len == 0) return true;
    if (!m_remote.isValid()) return false;

    // Each fragment carries: [1 byte conn-flags][6 bytes fragment-meta][payload]
    // The conn-flags byte tells the receiver: is this a fragment? what mode?
    // Fragment-meta: groupId(4) + fragIdx(2)... but we also need fragTotal.
    // Let's use a fixed 8-byte sub-header INSIDE the channel payload:
    //   byte 0:    sub-flags (0 = whole, 1 = fragment-not-last, 2 = fragment-last)
    //   bytes 1-4: groupId
    //   bytes 5-6: fragIdx
    //   byte 7:   reserved (or fragTotal high byte — total fits in 16 bits)
    //   Actually let's use:
    //   byte 0:   sub-flags
    //   bytes 1-4: groupId (LE)
    //   bytes 5-6: fragIdx (LE)
    //   bytes 7-8: fragTotal (LE)
    // = 9 bytes sub-header.
    constexpr int SUB_HEADER = 9;
    int maxPayload = NET_MTU - NET_PACKET_HEADER_SIZE - SUB_HEADER;
    if (maxPayload <= 0) return false;

    int numFrags = (len + maxPayload - 1) / maxPayload;
    if (numFrags <= 1) {
        // Single-fragment message: sub-flags = 0 (whole), no frag meta needed.
        uint8_t sub[SUB_HEADER] = {};
        sub[0] = 0;  // whole
        // groupId / fragIdx / fragTotal left as 0.
        std::vector<uint8_t> framed(SUB_HEADER + len);
        std::memcpy(framed.data(), sub, SUB_HEADER);
        std::memcpy(framed.data() + SUB_HEADER, data, static_cast<size_t>(len));
        return m_channel.send(sock, m_remote, mode, framed.data(),
                              static_cast<int>(framed.size()));
    }

    // Multi-fragment: each fragment gets a sub-header with groupId + idx + total.
    uint32_t groupId = m_nextFragmentGroupId++;
    if (m_nextFragmentGroupId == 0) m_nextFragmentGroupId = 1;

    const uint8_t* src = static_cast<const uint8_t*>(data);
    int offset = 0;
    for (int i = 0; i < numFrags; i++) {
        int fragLen = std::min(maxPayload, len - offset);
        uint8_t sub[SUB_HEADER];
        sub[0] = (i == numFrags - 1) ? 2 : 1;  // 2 = last frag, 1 = not-last
        sub[1] = static_cast<uint8_t>(groupId & 0xFF);
        sub[2] = static_cast<uint8_t>((groupId >> 8) & 0xFF);
        sub[3] = static_cast<uint8_t>((groupId >> 16) & 0xFF);
        sub[4] = static_cast<uint8_t>((groupId >> 24) & 0xFF);
        sub[5] = static_cast<uint8_t>(i & 0xFF);
        sub[6] = static_cast<uint8_t>((i >> 8) & 0xFF);
        sub[7] = static_cast<uint8_t>(numFrags & 0xFF);
        sub[8] = static_cast<uint8_t>((numFrags >> 8) & 0xFF);

        std::vector<uint8_t> framed(SUB_HEADER + fragLen);
        std::memcpy(framed.data(), sub, SUB_HEADER);
        std::memcpy(framed.data() + SUB_HEADER, src + offset,
                    static_cast<size_t>(fragLen));
        if (!m_channel.send(sock, m_remote, mode, framed.data(),
                            static_cast<int>(framed.size()))) {
            return false;
        }
        offset += fragLen;
    }
    return true;
}

void Connection::onPacket(const void* data, int len) {
    if (len < NET_PACKET_HEADER_SIZE) return;
    m_lastRecvMs = monotonicNowMs();

    const uint8_t* buf = static_cast<const uint8_t*>(data);
    uint8_t flags = buf[0];

    // If we were in CONNECTING, this is the first packet — promote to CONNECTED.
    if (m_state == State::Connecting) {
        setState(State::Connected);
    }

    // Heartbeat packets have no payload; just update lastRecvMs (already done).
    if (flags & PacketFlag::HEARTBEAT) {
        // Strip heartbeat flag so the channel sees a normal ACK or empty packet.
        // We rebuild a minimal packet: just the header (no payload).
        uint8_t hdr[NET_PACKET_HEADER_SIZE];
        std::memcpy(hdr, buf, NET_PACKET_HEADER_SIZE);
        hdr[0] = static_cast<uint8_t>(hdr[0] & ~PacketFlag::HEARTBEAT);
        // If it was HEARTBEAT only (no other flags), treat as ACK.
        if (hdr[0] == 0) hdr[0] = PacketFlag::ACK_ONLY;
        m_channel.onPacket(hdr, NET_PACKET_HEADER_SIZE);
        return;
    }

    m_channel.onPacket(data, len);
}

void Connection::update(Socket& sock, uint32_t nowMs) {
    if (m_state == State::Disconnected) return;

    // Channel retransmits + ACKs.
    m_channel.update(sock, m_remote, nowMs);

    // Heartbeat: every 1s, send an empty packet (with HEARTBEAT flag).
    if (m_state == State::Connected &&
        (nowMs - m_lastHeartbeatMs) >= HEARTBEAT_INTERVAL_MS) {
        uint8_t buf[NET_PACKET_HEADER_SIZE];
        writePacketHeader(buf, PacketFlag::HEARTBEAT, 0,
                          m_channel.lastRecvSeq(), 0);
        sock.sendTo(m_remote, buf, NET_PACKET_HEADER_SIZE);
        m_lastHeartbeatMs = nowMs;
        m_lastSendMs = nowMs;
    }

    // Timeout: 10s of silence -> disconnected.
    if (m_wasActive && (nowMs - m_lastRecvMs) >= TIMEOUT_MS) {
        setState(State::Disconnected);
    }
}

// ---------------------------------------------------------------------------
// MessageWriter / MessageReader
// ---------------------------------------------------------------------------

void MessageWriter::writeVarint(uint64_t v) {
    while (v > 0x7F) {
        m_buf.push_back(static_cast<uint8_t>((v & 0x7F) | 0x80));
        v >>= 7;
    }
    m_buf.push_back(static_cast<uint8_t>(v));
}

void MessageWriter::writeFixed32(uint32_t v) {
    m_buf.push_back(static_cast<uint8_t>(v & 0xFF));
    m_buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    m_buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    m_buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void MessageWriter::writeFixed64(uint64_t v) {
    for (int i = 0; i < 8; i++) {
        m_buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
}

void MessageWriter::writeBytes(const void* data, int len) {
    if (len <= 0 || !data) return;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    m_buf.insert(m_buf.end(), p, p + len);
}

void MessageWriter::writeString(const char* s) {
    if (!s) { writeVarint(0); return; }
    size_t n = std::strlen(s);
    writeVarint(static_cast<uint64_t>(n));
    writeBytes(s, static_cast<int>(n));
}

void MessageWriter::writeTag(uint32_t tag, uint32_t wireType) {
    writeVarint((static_cast<uint64_t>(tag) << 3) | static_cast<uint64_t>(wireType & 0x7));
}

bool MessageReader::readVarint(uint64_t& out) {
    out = 0;
    int shift = 0;
    while (m_pos < m_len) {
        uint8_t b = m_data[m_pos++];
        out |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) return true;
        shift += 7;
        if (shift >= 64) return false;  // varint too long
    }
    return false;  // ran off the end without a terminator
}

bool MessageReader::readFixed32(uint32_t& out) {
    if (m_pos + 4 > m_len) return false;
    out = static_cast<uint32_t>(m_data[m_pos])
        | (static_cast<uint32_t>(m_data[m_pos + 1]) << 8)
        | (static_cast<uint32_t>(m_data[m_pos + 2]) << 16)
        | (static_cast<uint32_t>(m_data[m_pos + 3]) << 24);
    m_pos += 4;
    return true;
}

bool MessageReader::readFixed64(uint64_t& out) {
    if (m_pos + 8 > m_len) return false;
    out = 0;
    for (int i = 0; i < 8; i++) {
        out |= static_cast<uint64_t>(m_data[m_pos + i]) << (i * 8);
    }
    m_pos += 8;
    return true;
}

bool MessageReader::readBytes(uint8_t* out, int len) {
    if (len <= 0) return true;
    if (m_pos + len > m_len) return false;
    std::memcpy(out, m_data + m_pos, static_cast<size_t>(len));
    m_pos += len;
    return true;
}

bool MessageReader::readString(std::string& out) {
    uint64_t len;
    if (!readVarint(len)) return false;
    if (len > static_cast<uint64_t>(m_len - m_pos)) return false;
    out.assign(reinterpret_cast<const char*>(m_data + m_pos),
               static_cast<size_t>(len));
    m_pos += static_cast<int>(len);
    return true;
}

bool MessageReader::readStringView(const char*& outPtr, int& outLen) {
    uint64_t len;
    if (!readVarint(len)) return false;
    if (len > static_cast<uint64_t>(m_len - m_pos)) return false;
    outPtr = reinterpret_cast<const char*>(m_data + m_pos);
    outLen = static_cast<int>(len);
    m_pos += static_cast<int>(len);
    return true;
}

bool MessageReader::readTag(uint32_t& tag, uint32_t& wireType) {
    if (atEnd()) return false;
    uint64_t v;
    if (!readVarint(v)) return false;
    wireType = static_cast<uint32_t>(v & 0x7);
    tag = static_cast<uint32_t>(v >> 3);
    return true;
}

bool MessageReader::skipField(uint32_t wireType) {
    switch (wireType) {
        case 0: { uint64_t v; return readVarint(v); }
        case 1: { uint64_t v; return readFixed64(v); }
        case 2: {
            uint64_t len;
            if (!readVarint(len)) return false;
            if (m_pos + static_cast<int>(len) > m_len) return false;
            m_pos += static_cast<int>(len);
            return true;
        }
        case 5: { uint32_t v; return readFixed32(v); }
        default: return false;
    }
}

// ---------------------------------------------------------------------------
// RPC
// ---------------------------------------------------------------------------

RPC::RPC() = default;
RPC::~RPC() = default;

void RPC::registerHandler(const std::string& name, Handler h) {
    m_handlers[name] = std::move(h);
}

void RPC::unregisterHandler(const std::string& name) {
    m_handlers.erase(name);
}

bool RPC::hasHandler(const std::string& name) const {
    return m_handlers.find(name) != m_handlers.end();
}

void RPC::call(const Endpoint& target,
               const std::string& name,
               const void* args, int argsLen,
               uint32_t timeoutMs,
               ResponseCallback cb) {
    if (timeoutMs == 0) timeoutMs = DEFAULT_TIMEOUT_MS;
    uint32_t id = m_nextCallId++;
    if (m_nextCallId == 0) m_nextCallId = 1;

    PendingCall p;
    p.id          = id;
    p.target      = target;
    p.deadlineMs  = monotonicNowMs() + timeoutMs;
    p.cb          = std::move(cb);
    m_pending.push_back(std::move(p));

    emitRequest(target, id, name, args, argsLen);
}

void RPC::callString(const Endpoint& target,
                     const std::string& name,
                     const std::string& argStr,
                     uint32_t timeoutMs,
                     std::function<void(bool ok, const std::string& resp)> cb) {
    call(target, name, argStr.data(), static_cast<int>(argStr.size()),
         timeoutMs, [cb](bool ok, const uint8_t* data, int len) {
        std::string s;
        if (ok && len > 0) s.assign(reinterpret_cast<const char*>(data),
                                    static_cast<size_t>(len));
        cb(ok, s);
    });
}

// All RPC frames start with a 1-byte magic (0xAA) so the receiver can
// unambiguously distinguish them from user payloads.
static const uint8_t RPC_MAGIC = 0xAA;

void RPC::emitRequest(const Endpoint& target, uint32_t callId,
                      const std::string& name,
                      const void* args, int argsLen) {
    MessageWriter w;
    w.writeBytes(&RPC_MAGIC, 1);
    w.writeFixed32(static_cast<uint32_t>(MsgType::Request));
    w.writeFixed32(callId);
    w.writeString(name.c_str());
    w.writeVarint(static_cast<uint64_t>(argsLen > 0 ? argsLen : 0));
    if (argsLen > 0 && args) w.writeBytes(args, argsLen);

    if (m_sender) m_sender(target, w.data(), w.size());
}

void RPC::emitResponse(const Endpoint& target, uint32_t callId,
                       MsgType type, const void* payload, int payloadLen) {
    MessageWriter w;
    w.writeBytes(&RPC_MAGIC, 1);
    w.writeFixed32(static_cast<uint32_t>(type));
    w.writeFixed32(callId);
    w.writeVarint(static_cast<uint64_t>(payloadLen > 0 ? payloadLen : 0));
    if (payloadLen > 0 && payload) w.writeBytes(payload, payloadLen);

    if (m_sender) m_sender(target, w.data(), w.size());
}

void RPC::onPacket(const Endpoint& from, const uint8_t* data, int len) {
    if (len <= 0) return;
    // First byte is the RPC magic (0xAA); skip it.
    if (data[0] != RPC_MAGIC) return;
    MessageReader r(data + 1, len - 1);
    uint32_t typeRaw;
    if (!r.readFixed32(typeRaw)) return;
    uint32_t callId;
    if (!r.readFixed32(callId)) return;

    if (typeRaw == static_cast<uint32_t>(MsgType::Request)) {
        std::string name;
        if (!r.readString(name)) return;
        // Rest of the buffer is the request payload.
        const uint8_t* argPtr = r.data() + r.position();
        int argLen = r.remaining();
        auto it = m_handlers.find(name);
        if (it == m_handlers.end()) {
            // Unknown method — drop silently so the caller times out.
            // (This matches the "RPC timeout" test: a nonexistent RPC
            // should produce no response, and the caller's timeout fires.)
            return;
        }
        MessageWriter resp;
        MessageReader req(argPtr, argLen);
        it->second(req, resp);
        emitResponse(from, callId, MsgType::Response, resp.data(), resp.size());
    } else if (typeRaw == static_cast<uint32_t>(MsgType::Response) ||
               typeRaw == static_cast<uint32_t>(MsgType::Error)) {
        // Rest is the payload (length-prefixed in our wire format).
        uint64_t payloadLen;
        if (!r.readVarint(payloadLen)) return;
        if (static_cast<int>(payloadLen) > r.remaining()) return;
        const uint8_t* payloadPtr = r.data() + r.position();
        int payloadActualLen = static_cast<int>(payloadLen);

        bool ok = (typeRaw == static_cast<uint32_t>(MsgType::Response));
        for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
            if (it->id == callId) {
                if (it->cb) it->cb(ok, payloadPtr, payloadActualLen);
                m_pending.erase(it);
                return;
            }
        }
        // No pending call matches — drop silently.
    }
}

void RPC::update(uint32_t nowMs) {
    for (auto it = m_pending.begin(); it != m_pending.end(); ) {
        if (nowMs >= it->deadlineMs) {
            // Timeout — fire callback with success=false.
            if (it->cb) it->cb(false, nullptr, 0);
            it = m_pending.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------------------
// NetworkInterface
// ---------------------------------------------------------------------------

NetworkInterface::NetworkInterface() {
    m_rpc.setSender([this](const Endpoint& to, const void* data, int len) {
        send(to, ReliabilityMode::ReliableOrdered, data, len);
    });
}

NetworkInterface::~NetworkInterface() {
    shutdown();
}

bool NetworkInterface::init(uint16_t localPort) {
    if (!m_socket.open(localPort)) return false;
    m_lastUpdateMs = monotonicNowMs();
    return true;
}

void NetworkInterface::shutdown() {
    for (auto& kv : m_conns) {
        kv.second->reset();
    }
    m_conns.clear();
    m_socket.close();
}

Endpoint NetworkInterface::localEndpoint() const {
    Endpoint ep;
    sockaddr_in* a = reinterpret_cast<sockaddr_in*>(&ep.addr);
    std::memset(a, 0, sizeof(*a));
    a->sin_family = AF_INET;
    a->sin_port   = htons(m_socket.localPort());
    a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ep.addrLen = sizeof(*a);
    return ep;
}

void NetworkInterface::onMessage(MessageCallback cb) {
    m_callbacks.push_back(std::move(cb));
}

bool NetworkInterface::send(const Endpoint& to, ReliabilityMode mode,
                            const void* data, int len) {
    if (!m_socket.isOpen()) return false;
    Connection* c = getOrCreateConnection(to);
    if (!c) return false;
    if (c->state() == Connection::State::Disconnected) {
        c->connect(to);
    }
    uint32_t now = monotonicNowMs();
    bool ok = c->send(m_socket, mode, data, len);
    (void)now;
    return ok;
}

int NetworkInterface::poll(uint32_t timeoutMs) {
    if (!m_socket.isOpen()) return 0;
    int processed = 0;

    // Drain the socket: keep reading until empty (non-blocking) or until we
    // hit a soft cap to avoid starving the rest of the game loop.
    const int SOFT_CAP = 256;
    uint8_t buf[NET_MTU + 32];
    Endpoint from;

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeoutMs);

    for (int i = 0; i < SOFT_CAP; i++) {
        int n = m_socket.recvFrom(from, buf, sizeof(buf));
        if (n <= 0) {
            // No data right now. If timeoutMs > 0, we could sleep briefly,
            // but to keep poll() strictly non-blocking we just break and
            // rely on the caller to call again. The `timeoutMs` parameter
            // is interpreted as a soft hint; we never block.
            (void)deadline;
            break;
        }
        routePacket(from, buf, n);
        ++processed;
    }

    update();
    return processed;
}

void NetworkInterface::update() {
    uint32_t now = monotonicNowMs();
    for (auto& kv : m_conns) {
        kv.second->update(m_socket, now);
    }
    m_rpc.update(now);
    m_lastUpdateMs = now;
}

Connection* NetworkInterface::getOrCreateConnection(const Endpoint& ep) {
    auto it = m_conns.find(ep);
    if (it != m_conns.end()) return it->second.get();
    auto c = std::make_unique<Connection>();

    // Wire the connection's message callback to do fragment reassembly and
    // route the assembled message to the NI's callbacks (and to RPC if the
    // packet is RPC-flagged).
    Connection* raw = c.get();
    raw->setMessageCallback([this, raw](ReliabilityMode mode,
                                        const uint8_t* data, int len) {
        // The data here is the FRAMED payload from the channel (includes the
        // 9-byte sub-header). Strip + reassemble.
        constexpr int SUB_HEADER = 9;
        if (len < 1) return;
        uint8_t subFlag = data[0];
        if (subFlag == 0) {
            // Whole message — skip the 9-byte sub-header.
            if (len < SUB_HEADER) return;
            deliverMessage(raw->remote(), mode, data + SUB_HEADER,
                           len - SUB_HEADER);
        } else {
            // Fragment. Parse sub-header.
            if (len < SUB_HEADER) return;
            uint32_t groupId = static_cast<uint32_t>(data[1])
                             | (static_cast<uint32_t>(data[2]) << 8)
                             | (static_cast<uint32_t>(data[3]) << 16)
                             | (static_cast<uint32_t>(data[4]) << 24);
            uint16_t fragIdx  = static_cast<uint16_t>(data[5] | (data[6] << 8));
            uint16_t fragTotal= static_cast<uint16_t>(data[7] | (data[8] << 8));

            // We don't expose Connection's reassembly state to the NI. So
            // we do the reassembly here in a static-ish map keyed by
            // (connection, groupId). Since we capture `raw` (the connection
            // pointer), we can use a per-connection static map.
            //
            // To avoid static state, we'd need to expose Connection's
            // reassembly map. For simplicity, use a function-local static
            // map keyed by Connection*. This is single-threaded, so safe.
            // (Production code would store this on the Connection object.)
            struct Reassembly {
                uint16_t total = 0;
                std::map<uint16_t, std::vector<uint8_t>> frags;
                int received = 0;
                ReliabilityMode mode = ReliabilityMode::ReliableOrdered;
            };
            static std::unordered_map<const Connection*,
                                      std::unordered_map<uint32_t, Reassembly>>
                s_reassembly;

            auto& connMap = s_reassembly[raw];
            auto& r = connMap[groupId];
            r.total = fragTotal;
            r.mode  = mode;
            if (r.frags.find(fragIdx) == r.frags.end()) {
                r.frags[fragIdx].assign(data + SUB_HEADER,
                                        data + len);
                r.received++;
            }
            // Debug: log fragment progress.
            fprintf(stderr, "[frag] conn=%p group=%u idx=%u/%u received=%d/%u\n",
                    (void*)raw, groupId, fragIdx, fragTotal, r.received, r.total);
            if (r.received >= r.total) {
                // Reassemble in order.
                std::vector<uint8_t> assembled;
                for (uint16_t i = 0; i < r.total; i++) {
                    auto fit = r.frags.find(i);
                    if (fit == r.frags.end()) {
                        // Missing fragment — shouldn't happen since received == total.
                        break;
                    }
                    assembled.insert(assembled.end(), fit->second.begin(),
                                     fit->second.end());
                }
                if (static_cast<int>(assembled.size()) > 0 ||
                    (r.total == 1 && r.frags.size() == 1)) {
                    deliverMessage(raw->remote(), r.mode, assembled.data(),
                                   static_cast<int>(assembled.size()));
                }
                connMap.erase(groupId);
            }
        }
    });

    c->connect(ep);
    auto* ptr = c.get();
    m_conns[ep] = std::move(c);
    return ptr;
}

void NetworkInterface::routePacket(const Endpoint& from, const uint8_t* data,
                                   int len) {
    Connection* c = getOrCreateConnection(from);
    if (!c) return;
    c->onPacket(data, len);
}

void NetworkInterface::deliverMessage(const Endpoint& from, ReliabilityMode mode,
                                      const uint8_t* data, int len) {
    // RPC frames start with a 1-byte magic (0xAA). User messages do not.
    // This lets us unambiguously route to the RPC layer without false
    // positives on user payloads that happen to start with byte values
    // that look like a MsgType.
    if (len >= 1 && data[0] == 0xAA) {
        m_rpc.onPacket(from, data, len);
        return;
    }
    for (auto& cb : m_callbacks) {
        if (cb) cb(from, data, len, mode);
    }
}

uint32_t NetworkInterface::nowMs() {
    return monotonicNowMs();
}

} // namespace net

// ---------------------------------------------------------------------------
// Public RpcServer (declared in transport.h, implemented here)
//
// Uses the JSON wire format defined in json_rpc.h — matches the JS TDNet.RPC
// 28-test suite in tests/test_net_websocket.js exactly. A C++ server can
// dispatch frames produced by a JS client (and vice versa) with no
// translation step.
// ---------------------------------------------------------------------------

void RpcServer::registerMethod(const char* name,
                               std::function<void(int peerId,
                                                   const char* argsJson)> handler) {
    if (!name || !handler) return;
    std::string event = std::string("rpc:") + name;
    SignalBus::get().on(event.c_str(),
        [handler](const SignalPayload& p) {
            handler(p.intValue, p.strValue);
        });
}

bool RpcServer::callRemote(const char* name, const char* argsJson, int targetPeer) {
    if (!name || !m_peer) return false;
    std::string frame;
    td::net::encodeNotify(frame, name, argsJson ? argsJson : "[]");
    NetPacket pkt;
    pkt.reliability = NetReliability::Reliable;
    pkt.channel     = 0;
    pkt.data        = frame.data();
    pkt.size        = static_cast<int>(frame.size());
    pkt.targetPeer  = targetPeer;
    return m_peer->send(pkt);
}

uint32_t RpcServer::callWithReply(const char* name, const char* argsJson,
                                   int targetPeer, uint32_t timeoutMs,
                                   std::function<void(bool ok,
                                                      const char* resultJson)> cb) {
    if (!name || !m_peer || !cb) return 0;
    uint32_t id = m_nextCallId++;
    std::string frame;
    td::net::encodeRequest(frame, id, name, argsJson ? argsJson : "[]");

    // Monotonic clock: same base as td::net's monotonicNowMs() (process start).
    static auto clockStart = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    uint32_t nowMs = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - clockStart).count());

    PendingCall pc;
    pc.id          = id;
    pc.deadlineMs  = nowMs + timeoutMs;
    pc.cb          = std::move(cb);
    m_pending.push_back(std::move(pc));

    NetPacket pkt;
    pkt.reliability = NetReliability::Reliable;
    pkt.channel     = 0;
    pkt.data        = frame.data();
    pkt.size        = static_cast<int>(frame.size());
    pkt.targetPeer  = targetPeer;
    if (!m_peer->send(pkt)) {
        // Send failed — fail the call immediately.
        auto cbLocal = std::move(m_pending.back().cb);
        m_pending.pop_back();
        cbLocal(false, "send failed");
        return id;
    }
    return id;
}

bool RpcServer::sendResponse(uint32_t callId, const char* resultJson, int targetPeer) {
    if (!m_peer) return false;
    markResponded(callId);
    std::string frame;
    td::net::encodeResponse(frame, callId, resultJson ? resultJson : "null");
    NetPacket pkt;
    pkt.reliability = NetReliability::Reliable;
    pkt.channel     = 0;
    pkt.data        = frame.data();
    pkt.size        = static_cast<int>(frame.size());
    pkt.targetPeer  = targetPeer;
    return m_peer->send(pkt);
}

bool RpcServer::sendError(uint32_t callId, const char* errorMessage, int targetPeer) {
    if (!m_peer) return false;
    markResponded(callId);
    std::string frame;
    td::net::encodeError(frame, callId, errorMessage ? errorMessage : "error");
    NetPacket pkt;
    pkt.reliability = NetReliability::Reliable;
    pkt.channel     = 0;
    pkt.data        = frame.data();
    pkt.size        = static_cast<int>(frame.size());
    pkt.targetPeer  = targetPeer;
    return m_peer->send(pkt);
}

void RpcServer::dispatchPacket(int peerId, const void* data, int size) {
    if (!data || size <= 0) return;

    td::net::RpcFrame frame;
    if (!td::net::parseFrame(static_cast<const char*>(data),
                              static_cast<size_t>(size), frame)) {
        TD_LOG_WARN("RpcServer: failed to parse frame from peer %d: %s",
                    peerId, frame.errorMessage.c_str());
        return;
    }

    switch (frame.kind) {
        case td::net::RpcFrame::Kind::Request: {
            // Dispatch to registered handlers. Track whether the handler
            // calls sendResponse/sendError during dispatch so we don't
            // auto-send a duplicate null response.
            m_dispatchedCallId = frame.id;
            m_respondedDuringDispatch = false;

            std::string event = std::string("rpc:") + frame.method;
            SignalPayload p;
            p.intValue = peerId;
            p.setStr(frame.argsJson.c_str());
            SignalBus::get().emit(event.c_str(), p);

            const bool handlerExists = SignalBus::get().eventExists(event.c_str());
            const bool alreadyResponded = m_respondedDuringDispatch;

            // Clear dispatch state so future direct calls to sendResponse
            // (e.g. late async replies) aren't mistakenly tracked.
            m_dispatchedCallId = 0;
            m_respondedDuringDispatch = false;

            if (alreadyResponded) {
                // Handler sent its own Response/Error. Do nothing.
            } else if (handlerExists) {
                // Handler ran but didn't reply — send null so caller doesn't hang.
                sendResponse(frame.id, "null", peerId);
            } else {
                sendError(frame.id,
                          (std::string("unknown method: ") + frame.method).c_str(),
                          peerId);
            }
            break;
        }
        case td::net::RpcFrame::Kind::Response: {
            // Match against a pending callWithReply() call.
            for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
                if (it->id == frame.id) {
                    auto cb = std::move(it->cb);
                    m_pending.erase(it);
                    cb(true, frame.resultJson.c_str());
                    return;
                }
            }
            // No matching pending call — drop silently.
            TD_LOG_WARN("RpcServer: response for unknown call id %u", frame.id);
            break;
        }
        case td::net::RpcFrame::Kind::Error: {
            for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
                if (it->id == frame.id) {
                    auto cb = std::move(it->cb);
                    m_pending.erase(it);
                    cb(false, frame.errorMessage.c_str());
                    return;
                }
            }
            TD_LOG_WARN("RpcServer: error response for unknown call id %u",
                        frame.id);
            break;
        }
        case td::net::RpcFrame::Kind::Notify: {
            std::string event = std::string("rpc:") + frame.method;
            SignalPayload p;
            p.intValue = peerId;
            p.setStr(frame.argsJson.c_str());
            SignalBus::get().emit(event.c_str(), p);
            break;
        }
        default:
            // Invalid — already logged above.
            break;
    }
}

void RpcServer::update(uint32_t nowMs) {
    for (auto it = m_pending.begin(); it != m_pending.end(); ) {
        if (nowMs >= it->deadlineMs) {
            auto cb = std::move(it->cb);
            it = m_pending.erase(it);
            cb(false, "timeout");
        } else {
            ++it;
        }
    }
}

void RpcServer::reset() {
    m_pending.clear();
    m_nextCallId = 1;
}

} // namespace td
