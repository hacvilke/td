// =============================================================================
// TD Engine - JSON-RPC wire format (C++ side, matches web/net_websocket.js)
//
// The browser-side RPC layer (web/net_websocket.js, class TDNet.RPC) uses a
// tiny JSON wire format for all RPC traffic. The 28-test suite in
// tests/test_net_websocket.js locks that format. This module makes the C++
// side speak the SAME format so a C++ server can dispatch frames produced by
// a JS client (and vice versa) without any translation step.
//
// Wire format (JSON, all messages are objects):
//
//   Request (client -> server, expects a response):
//     { "id": 123, "m": "methodName", "a": [arg1, arg2, ...] }
//
//   Response (server -> client, success):
//     { "id": 123, "r": <result> }
//
//   Response (server -> client, error):
//     { "id": 123, "e": "error message" }
//
//   Notify (either direction, no reply expected):
//     { "m": "methodName", "a": [...] }
//
// All frames are sent as a single JSON object. There is no length prefix, no
// magic byte, no envelope. The transport (WebSocket text frame, UDP packet,
// stdio pipe, whatever) is responsible for framing boundaries.
//
// This module is dependency-free: it does not pull in the scene serializer's
// JSON reader (which is SAX-style and tied to component registration). It
// has its own minimal extractor that handles exactly the keys we care about
// (id, m, a, r, e) and treats their values as opaque JSON substrings. This
// is intentional: it lets the caller (RpcServer) pass the args JSON verbatim
// to gameplay handlers, which can decode it however they want.
//
// Status: REAL. Backs td::RpcServer::dispatchPacket / callRemote.
// =============================================================================
#pragma once
#include "../core/logger.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace td {
namespace net {

// A parsed RPC frame. Exactly one of (request, response, error, notify) is
// meaningful per frame, identified by `kind`.
struct RpcFrame {
    enum class Kind : uint8_t {
        Invalid     = 0,  // parse failed
        Request     = 1,  // has id + method + args
        Response    = 2,  // has id + result
        Error       = 3,  // has id + error message
        Notify      = 4,  // has method + args, NO id (fire-and-forget)
    };

    Kind         kind        = Kind::Invalid;
    uint32_t     id          = 0;        // 0 means "no id" (notify)
    bool         hasId       = false;
    std::string  method;                 // "m" field, for Request + Notify
    // argsJson / resultJson / errorMessage store the raw JSON substring for
    // the corresponding field, with surrounding whitespace trimmed. The
    // caller is responsible for further decoding.
    //
    // Examples:
    //   argsJson    = `[1, "hello", { "x": 5 }]`
    //   resultJson  = `"pong"`  or  `42`  or  `{ "ok": true }`
    //   errorMessage= "unknown method: foo"  (a JSON string with quotes)
    std::string  argsJson;
    std::string  resultJson;
    std::string  errorMessage;
};

// ---------------------------------------------------------------------------
// Encoder: produces a JSON string matching the JS wire format.
//
// These functions append to `out` (so the caller can build a batch or reuse
// a buffer). They return a reference to `out` for chaining.
//
// The `argsJson` / `resultJson` arguments must already be valid JSON. They
// are inserted verbatim — no escaping, no validation. This is intentional:
// the caller typically has a pre-serialized payload from JsonWriter or from
// the JS bridge.
//
// `errorMessage` is a plain C string; it gets JSON-escaped automatically.
// ---------------------------------------------------------------------------

std::string& encodeRequest(std::string& out, uint32_t id,
                            const char* method, const char* argsJson);

std::string& encodeResponse(std::string& out, uint32_t id,
                             const char* resultJson);

std::string& encodeError(std::string& out, uint32_t id,
                          const char* errorMessage);

std::string& encodeNotify(std::string& out, const char* method,
                           const char* argsJson);

// Convenience: return a fresh std::string with the encoded frame.
inline std::string makeRequest(uint32_t id, const char* method,
                                const char* argsJson = "[]") {
    std::string s; return encodeRequest(s, id, method, argsJson);
}
inline std::string makeResponse(uint32_t id, const char* resultJson = "null") {
    std::string s; return encodeResponse(s, id, resultJson);
}
inline std::string makeError(uint32_t id, const char* errorMessage) {
    std::string s; return encodeError(s, id, errorMessage);
}
inline std::string makeNotify(const char* method, const char* argsJson = "[]") {
    std::string s; return encodeNotify(s, method, argsJson);
}

// ---------------------------------------------------------------------------
// Decoder: parse a single JSON frame into an RpcFrame.
//
// Returns true on success. On failure, `frame.kind` is set to Invalid and
// `frame.errorMessage` contains a short parser diagnostic.
//
// The parser is intentionally permissive about whitespace and tolerates
// trailing data after the closing brace (so a stream of frames can be fed
// in chunks without splitting first).
//
// What it parses:
//   - Top-level object: { ... }
//   - String values: standard JSON escapes (\" \\ \/ \b \f \n \r \t \uXXXX)
//   - The "id" field: integer only. Floats and strings are rejected.
//   - The "m" field: string.
//   - The "a", "r" fields: captured as a raw JSON substring (we walk the
//     value to find its bounds, but don't interpret it).
//   - The "e" field: string.
//
// What it does NOT do:
//   - Build a DOM. We extract only the four known fields.
//   - Validate that "a" is an array. (The JS spec says it should be, but
//     some legacy callers send a bare object — we accept either.)
//   - Enforce key uniqueness. If a frame has two "id" keys, the last wins.
// ---------------------------------------------------------------------------
bool parseFrame(const char* json, size_t len, RpcFrame& frame);

inline bool parseFrame(const std::string& s, RpcFrame& frame) {
    return parseFrame(s.data(), s.size(), frame);
}

// ---------------------------------------------------------------------------
// JSON string escaper (used by encodeError; also exposed for callers that
// want to build their own argsJson from raw strings).
// ---------------------------------------------------------------------------
std::string escapeJsonString(const char* s);

} // namespace net
} // namespace td
