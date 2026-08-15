// =============================================================================
// TD Engine - JSON-RPC wire format implementation.
//
// See json_rpc.h for the format spec. This file is intentionally small
// (~250 LOC): the wire format is tiny, and the parser only needs to handle
// the four known fields (id, m, a, r, e). Everything else is passed through
// as raw JSON substrings.
// =============================================================================
#include "json_rpc.h"

#include <cstdio>
#include <cstdlib>

namespace td {
namespace net {

// ---------------------------------------------------------------------------
// JSON string escaper — handles the standard JSON string escapes per
// RFC 8259 §7. Control characters < 0x20 become \uXXXX.
// ---------------------------------------------------------------------------
std::string escapeJsonString(const char* s) {
    if (!s) return std::string("null");
    std::string out;
    out.reserve(std::strlen(s) + 8);
    out += '"';
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
         *p; ++p) {
        unsigned char c = *p;
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
    return out;
}

// ---------------------------------------------------------------------------
// Encoders
// ---------------------------------------------------------------------------
std::string& encodeRequest(std::string& out, uint32_t id,
                            const char* method, const char* argsJson) {
    if (!method)  method = "";
    if (!argsJson) argsJson = "[]";
    out += "{\"id\":";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u", id);
    out += buf;
    out += ",\"m\":";
    out += escapeJsonString(method);
    out += ",\"a\":";
    out += argsJson;
    out += "}";
    return out;
}

std::string& encodeResponse(std::string& out, uint32_t id,
                             const char* resultJson) {
    if (!resultJson) resultJson = "null";
    out += "{\"id\":";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u", id);
    out += buf;
    out += ",\"r\":";
    out += resultJson;
    out += "}";
    return out;
}

std::string& encodeError(std::string& out, uint32_t id,
                          const char* errorMessage) {
    out += "{\"id\":";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u", id);
    out += buf;
    out += ",\"e\":";
    out += escapeJsonString(errorMessage ? errorMessage : "");
    out += "}";
    return out;
}

std::string& encodeNotify(std::string& out, const char* method,
                           const char* argsJson) {
    if (!method)   method = "";
    if (!argsJson) argsJson = "[]";
    out += "{\"m\":";
    out += escapeJsonString(method);
    out += ",\"a\":";
    out += argsJson;
    out += "}";
    return out;
}

// ---------------------------------------------------------------------------
// Decoder — minimal streaming parser
//
// Strategy: walk the input, find each top-level key in the object, then
// depending on the key name:
//   - "id": parse as integer (positive only; we reject floats + negative)
//   - "m":  parse as string (with full escape handling)
//   - "e":  parse as string
//   - "a":  walk the value (string, number, object, or array) to find its
//           bounds, then capture the raw substring
//   - "r":  same as "a"
//
// We don't build a DOM. We don't recurse into nested objects except to
// find their bounds. We tolerate (but ignore) unknown keys.
// ---------------------------------------------------------------------------

namespace {

struct Parser {
    const char* p;
    const char* end;
    std::string err;

    Parser(const char* src, size_t len)
        : p(src), end(src + len) {}

    void skipWs() {
        while (p < end) {
            char c = *p;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p++;
            else break;
        }
    }

    bool match(char c) {
        skipWs();
        if (p < end && *p == c) { p++; return true; }
        return false;
    }

    bool expect(char c, const char* msg) {
        if (!match(c)) { err = msg; return false; }
        return true;
    }

    // Parse a JSON string with full escape handling. Returns true on success.
    bool parseString(std::string& out) {
        if (!expect('"', "expected '\"'")) return false;
        out.clear();
        while (p < end) {
            char c = *p++;
            if (c == '"') return true;
            if (c == '\\') {
                if (p >= end) { err = "unterminated escape"; return false; }
                char esc = *p++;
                switch (esc) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u': {
                        if (end - p < 4) { err = "short \\u escape"; return false; }
                        char hex[5] = { p[0], p[1], p[2], p[3], 0 };
                        p += 4;
                        unsigned codepoint = static_cast<unsigned>(
                            std::strtoul(hex, nullptr, 16));
                        if (codepoint == 0) { err = "bad \\u escape"; return false; }
                        // Encode as UTF-8.
                        if (codepoint < 0x80) {
                            out += static_cast<char>(codepoint);
                        } else if (codepoint < 0x800) {
                            out += static_cast<char>(0xC0 | (codepoint >> 6));
                            out += static_cast<char>(0x80 | (codepoint & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (codepoint >> 12));
                            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (codepoint & 0x3F));
                        }
                        break;
                    }
                    default:
                        err = "bad escape character";
                        return false;
                }
            } else {
                // Control characters in raw strings are illegal per JSON, but
                // we accept them to be permissive (matches what most JSON
                // parsers do in practice).
                out += c;
            }
        }
        err = "unterminated string";
        return false;
    }

    // Parse an integer. JSON numbers can be floats, but the JS RPC protocol
    // only ever uses integer IDs. We accept the leading '-' (and reject it,
    // since IDs must be positive) and any digits. We reject floats, exponents,
    // and leading zeros (per JSON spec).
    bool parseId(uint32_t& out, bool& hasId) {
        skipWs();
        if (p >= end) { err = "expected id value"; return false; }
        // We accept the value as an integer only.
        const char* start = p;
        if (*p == '-') {
            // Negative IDs are not allowed by our protocol (the JS side uses
            // _nextId++ starting at 1). Reject.
            err = "id must be a non-negative integer";
            return false;
        }
        if (*p < '0' || *p > '9') {
            err = "id must be a number";
            return false;
        }
        // Reject leading zero (unless the number is exactly 0).
        if (*p == '0' && p + 1 < end && p[1] >= '0' && p[1] <= '9') {
            err = "id has leading zero";
            return false;
        }
        // Consume digits.
        while (p < end && *p >= '0' && *p <= '9') p++;
        // Reject fractional / exponent parts (we only accept integers).
        if (p < end && (*p == '.' || *p == 'e' || *p == 'E')) {
            err = "id must be an integer, not a float";
            return false;
        }
        // Parse.
        char* endp = nullptr;
        unsigned long val = std::strtoul(start, &endp, 10);
        if (endp != p) { err = "id parse mismatch"; return false; }
        out = static_cast<uint32_t>(val);
        hasId = true;
        return true;
    }

    // Walk a single JSON value (string, number, object, array, true, false,
    // null) and capture its raw substring bounds. Used for "a" and "r".
    bool captureValue(const char*& valStart, int& valLen) {
        skipWs();
        if (p >= end) { err = "expected value"; return false; }
        valStart = p;
        char c = *p;
        if (c == '"') {
            std::string tmp;
            // Reset position to the opening quote and re-parse the string
            // (we don't care about its content, just its bounds).
            if (!parseString(tmp)) return false;
            // parseString consumed the closing quote. Re-compute valStart/end.
            valStart = p - (tmp.size() + 2);  // crude; instead just walk:
            // Actually, simpler: re-find the bounds.
            // We already advanced p past the closing quote. Walk back from
            // valStart to find the closing quote.
            // Simpler approach: re-set p to valStart, re-parse.
            p = valStart;
            if (!parseString(tmp)) return false;
            valLen = static_cast<int>(p - valStart);
            return true;
        }
        if (c == '{' || c == '[') {
            char open = c;
            char close = (c == '{') ? '}' : ']';
            int depth = 0;
            bool inStr = false;
            bool esc = false;
            while (p < end) {
                char ch = *p++;
                if (inStr) {
                    if (esc) esc = false;
                    else if (ch == '\\') esc = true;
                    else if (ch == '"') inStr = false;
                } else {
                    if (ch == '"') inStr = true;
                    else if (ch == open) depth++;
                    else if (ch == close) {
                        depth--;
                        if (depth == 0) {
                            valLen = static_cast<int>(p - valStart);
                            return true;
                        }
                    }
                }
            }
            err = "unterminated object/array";
            return false;
        }
        if (c == 't') {
            if (end - p < 4 || std::strncmp(p, "true", 4) != 0) {
                err = "bad literal 'true'";
                return false;
            }
            p += 4;
            valLen = 4;
            return true;
        }
        if (c == 'f') {
            if (end - p < 5 || std::strncmp(p, "false", 5) != 0) {
                err = "bad literal 'false'";
                return false;
            }
            p += 5;
            valLen = 5;
            return true;
        }
        if (c == 'n') {
            if (end - p < 4 || std::strncmp(p, "null", 4) != 0) {
                err = "bad literal 'null'";
                return false;
            }
            p += 4;
            valLen = 4;
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            // Number — consume digits, optional fractional + exponent.
            if (*p == '-') p++;
            while (p < end && *p >= '0' && *p <= '9') p++;
            if (p < end && *p == '.') {
                p++;
                while (p < end && *p >= '0' && *p <= '9') p++;
            }
            if (p < end && (*p == 'e' || *p == 'E')) {
                p++;
                if (p < end && (*p == '+' || *p == '-')) p++;
                while (p < end && *p >= '0' && *p <= '9') p++;
            }
            valLen = static_cast<int>(p - valStart);
            return true;
        }
        err = "unexpected character in value";
        return false;
    }
};

} // anonymous namespace

bool parseFrame(const char* json, size_t len, RpcFrame& frame) {
    frame = RpcFrame{};  // reset
    if (!json || len == 0) {
        frame.kind = RpcFrame::Kind::Invalid;
        frame.errorMessage = "empty input";
        return false;
    }

    Parser p(json, len);
    if (!p.expect('{', "expected '{'")) {
        frame.kind = RpcFrame::Kind::Invalid;
        frame.errorMessage = p.err;
        return false;
    }
    p.skipWs();
    if (p.match('}')) {
        // Empty object — treat as a notify with no method (weird but valid).
        frame.kind = RpcFrame::Kind::Invalid;
        frame.errorMessage = "empty object";
        return false;
    }

    while (true) {
        p.skipWs();
        std::string key;
        if (!p.parseString(key)) {
            frame.kind = RpcFrame::Kind::Invalid;
            frame.errorMessage = p.err;
            return false;
        }
        if (!p.expect(':', "expected ':' after key")) {
            frame.kind = RpcFrame::Kind::Invalid;
            frame.errorMessage = p.err;
            return false;
        }

        if (key == "id") {
            if (!p.parseId(frame.id, frame.hasId)) {
                frame.kind = RpcFrame::Kind::Invalid;
                frame.errorMessage = p.err;
                return false;
            }
        } else if (key == "m") {
            if (!p.parseString(frame.method)) {
                frame.kind = RpcFrame::Kind::Invalid;
                frame.errorMessage = p.err;
                return false;
            }
        } else if (key == "e") {
            if (!p.parseString(frame.errorMessage)) {
                frame.kind = RpcFrame::Kind::Invalid;
                frame.errorMessage = p.err;
                return false;
            }
        } else if (key == "a" || key == "r") {
            const char* valStart = nullptr;
            int valLen = 0;
            if (!p.captureValue(valStart, valLen)) {
                frame.kind = RpcFrame::Kind::Invalid;
                frame.errorMessage = p.err;
                return false;
            }
            std::string captured(valStart, static_cast<size_t>(valLen));
            if (key == "a") frame.argsJson = std::move(captured);
            else            frame.resultJson = std::move(captured);
        } else {
            // Unknown key — skip its value.
            const char* valStart = nullptr;
            int valLen = 0;
            if (!p.captureValue(valStart, valLen)) {
                frame.kind = RpcFrame::Kind::Invalid;
                frame.errorMessage = p.err;
                return false;
            }
        }

        p.skipWs();
        if (p.match('}')) break;
        if (!p.expect(',', "expected ',' or '}'")) {
            frame.kind = RpcFrame::Kind::Invalid;
            frame.errorMessage = p.err;
            return false;
        }
    }

    // Classify the frame.
    if (frame.hasId) {
        if (!frame.method.empty()) {
            frame.kind = RpcFrame::Kind::Request;
        } else if (!frame.errorMessage.empty()) {
            frame.kind = RpcFrame::Kind::Error;
        } else {
            frame.kind = RpcFrame::Kind::Response;
        }
    } else if (!frame.method.empty()) {
        frame.kind = RpcFrame::Kind::Notify;
    } else {
        frame.kind = RpcFrame::Kind::Invalid;
        frame.errorMessage = "frame has neither id nor method";
        return false;
    }
    return true;
}

} // namespace net
} // namespace td
