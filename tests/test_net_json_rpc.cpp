// =============================================================================
// TD Engine - JSON-RPC parity tests (mirror tests/test_net_websocket.js)
//
// These tests verify that the C++ JSON-RPC layer (src/net/json_rpc.*) speaks
// the SAME wire format as the JS TDNet.RPC layer (web/net_websocket.js).
// Every test here corresponds to a test on the JS side; if a frame parses
// in JS it MUST parse identically here, and vice versa.
//
// Wire format (locked by 28 JS tests in tests/test_net_websocket.js):
//
//   Request:  {"id":123,"m":"methodName","a":[arg1,arg2,...]}
//   Response: {"id":123,"r":<result>}
//   Error:    {"id":123,"e":"error message"}
//   Notify:   {"m":"methodName","a":[...]}      (no id = no reply)
//
// Build (manual):
//   g++ -std=c++17 -Wall -Wextra -O2 -Isrc -DTEST_STUB_LOGGER \
//       tests/test_net_json_rpc.cpp \
//       src/net/transport.cpp src/net/json_rpc.cpp \
//       tests/stub_logger.cpp \
//       -o /tmp/test_net_json_rpc
//
// Run:
//   /tmp/test_net_json_rpc  (exits 0 on success, 1 on failure)
// =============================================================================

#include "net/json_rpc.h"
#include "net/mock_peer.h"
#include "net/transport.h"
#include "core/logger.h"
#include "core/signal.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

using td::net::RpcFrame;
using td::net::encodeRequest;
using td::net::encodeResponse;
using td::net::encodeError;
using td::net::encodeNotify;
using td::net::makeRequest;
using td::net::makeResponse;
using td::net::makeError;
using td::net::makeNotify;
using td::net::parseFrame;
using td::net::escapeJsonString;
using td::MockNetPeer;

static int g_failures = 0;
static int g_passes   = 0;

#define CHECK(cond, ...) do { \
    if (cond) { \
        ++g_passes; \
        std::printf("PASS: " __VA_ARGS__); \
        std::printf("\n"); \
    } else { \
        ++g_failures; \
        std::fprintf(stderr, "FAIL: " __VA_ARGS__); \
        std::fprintf(stderr, "\n"); \
        std::fprintf(stderr, "      (%s:%d)\n", __FILE__, __LINE__); \
    } \
} while (0)

// =============================================================================
// Encoder tests — verify the C++ produces the same bytes the JS would
// =============================================================================

static void test_encode_request_basic() {
    std::printf("\n--- Test: encode request (basic) ---\n");
    std::string s = makeRequest(123, "ping", "[\"hi\"]");
    CHECK(s == "{\"id\":123,\"m\":\"ping\",\"a\":[\"hi\"]}",
          "encodeRequest matches JS format ('%s')", s.c_str());
}

static void test_encode_request_empty_args() {
    std::printf("\n--- Test: encode request (empty args) ---\n");
    std::string s = makeRequest(1, "noop", "[]");
    CHECK(s == "{\"id\":1,\"m\":\"noop\",\"a\":[]}",
          "encodeRequest with empty args ('%s')", s.c_str());
}

static void test_encode_response() {
    std::printf("\n--- Test: encode response ---\n");
    std::string s = makeResponse(123, "\"pong\"");
    CHECK(s == "{\"id\":123,\"r\":\"pong\"}",
          "encodeResponse matches JS format ('%s')", s.c_str());
}

static void test_encode_response_null() {
    std::printf("\n--- Test: encode response (null result) ---\n");
    std::string s = makeResponse(7);
    CHECK(s == "{\"id\":7,\"r\":null}",
          "encodeResponse with null ('%s')", s.c_str());
}

static void test_encode_error() {
    std::printf("\n--- Test: encode error ---\n");
    std::string s = makeError(123, "unknown method: foo");
    CHECK(s == "{\"id\":123,\"e\":\"unknown method: foo\"}",
          "encodeError matches JS format ('%s')", s.c_str());
}

static void test_encode_notify() {
    std::printf("\n--- Test: encode notify ---\n");
    std::string s = makeNotify("heartbeat", "[42]");
    CHECK(s == "{\"m\":\"heartbeat\",\"a\":[42]}",
          "encodeNotify matches JS format ('%s')", s.c_str());
}

static void test_encode_method_with_special_chars() {
    std::printf("\n--- Test: encode method with special chars ---\n");
    // Method names with quotes and backslashes must be JSON-escaped.
    std::string s = makeRequest(1, "player:say \"hi\"", "[]");
    // The JS side does NOT escape method names (it uses JSON.stringify which
    // does). So we should too.
    CHECK(s.find("\\\"hi\\\"") != std::string::npos,
          "method name with quotes is escaped ('%s')", s.c_str());
}

// =============================================================================
// Decoder tests — verify the C++ parses what the JS produces
// =============================================================================

static void test_parse_request_basic() {
    std::printf("\n--- Test: parse request (basic) ---\n");
    RpcFrame f;
    bool ok = parseFrame("{\"id\":123,\"m\":\"ping\",\"a\":[\"hi\"]}", f);
    CHECK(ok, "parse succeeded");
    CHECK(f.kind == RpcFrame::Kind::Request, "kind is Request");
    CHECK(f.hasId, "hasId is true");
    CHECK(f.id == 123, "id is 123 (got %u)", f.id);
    CHECK(f.method == "ping", "method is 'ping' ('%s')", f.method.c_str());
    CHECK(f.argsJson == "[\"hi\"]", "argsJson is '[\"hi\"]' ('%s')",
          f.argsJson.c_str());
}

static void test_parse_request_with_whitespace() {
    std::printf("\n--- Test: parse request (with whitespace) ---\n");
    RpcFrame f;
    bool ok = parseFrame("{  \"id\" : 42 ,  \"m\" :  \"foo\" , \"a\" : [1,2,3] }", f);
    CHECK(ok, "parse succeeded with whitespace");
    CHECK(f.kind == RpcFrame::Kind::Request, "kind is Request");
    CHECK(f.id == 42, "id is 42 (got %u)", f.id);
    CHECK(f.method == "foo", "method is 'foo'");
    CHECK(f.argsJson == "[1,2,3]", "argsJson is '[1,2,3]' ('%s')",
          f.argsJson.c_str());
}

static void test_parse_response() {
    std::printf("\n--- Test: parse response ---\n");
    RpcFrame f;
    bool ok = parseFrame("{\"id\":123,\"r\":\"pong\"}", f);
    CHECK(ok, "parse succeeded");
    CHECK(f.kind == RpcFrame::Kind::Response, "kind is Response");
    CHECK(f.id == 123, "id is 123");
    CHECK(f.resultJson == "\"pong\"", "resultJson is '\"pong\"' ('%s')",
          f.resultJson.c_str());
}

static void test_parse_response_number() {
    std::printf("\n--- Test: parse response (number result) ---\n");
    RpcFrame f;
    bool ok = parseFrame("{\"id\":7,\"r\":42}", f);
    CHECK(ok, "parse succeeded");
    CHECK(f.kind == RpcFrame::Kind::Response, "kind is Response");
    CHECK(f.resultJson == "42", "resultJson is '42' ('%s')",
          f.resultJson.c_str());
}

static void test_parse_response_object() {
    std::printf("\n--- Test: parse response (object result) ---\n");
    RpcFrame f;
    bool ok = parseFrame("{\"id\":7,\"r\":{\"ok\":true,\"count\":3}}", f);
    CHECK(ok, "parse succeeded");
    CHECK(f.kind == RpcFrame::Kind::Response, "kind is Response");
    CHECK(f.resultJson == "{\"ok\":true,\"count\":3}",
          "resultJson is object string ('%s')", f.resultJson.c_str());
}

static void test_parse_error() {
    std::printf("\n--- Test: parse error ---\n");
    RpcFrame f;
    bool ok = parseFrame("{\"id\":99,\"e\":\"unknown method: foo\"}", f);
    CHECK(ok, "parse succeeded");
    CHECK(f.kind == RpcFrame::Kind::Error, "kind is Error");
    CHECK(f.id == 99, "id is 99");
    CHECK(f.errorMessage == "unknown method: foo",
          "errorMessage is correct ('%s')", f.errorMessage.c_str());
}

static void test_parse_notify() {
    std::printf("\n--- Test: parse notify ---\n");
    RpcFrame f;
    bool ok = parseFrame("{\"m\":\"heartbeat\",\"a\":[42]}", f);
    CHECK(ok, "parse succeeded");
    CHECK(f.kind == RpcFrame::Kind::Notify, "kind is Notify");
    CHECK(!f.hasId, "hasId is false (notify)");
    CHECK(f.method == "heartbeat", "method is 'heartbeat'");
    CHECK(f.argsJson == "[42]", "argsJson is '[42]'");
}

static void test_parse_empty_array_args() {
    std::printf("\n--- Test: parse with empty array args ---\n");
    RpcFrame f;
    bool ok = parseFrame("{\"id\":1,\"m\":\"x\",\"a\":[]}", f);
    CHECK(ok, "parse succeeded");
    CHECK(f.argsJson == "[]", "argsJson is '[]'");
}

static void test_parse_nested_args() {
    std::printf("\n--- Test: parse nested args ---\n");
    RpcFrame f;
    bool ok = parseFrame("{\"id\":1,\"m\":\"x\",\"a\":[{\"name\":\"alice\",\"scores\":[10,20,30]}]}", f);
    CHECK(ok, "parse succeeded");
    CHECK(f.argsJson == "[{\"name\":\"alice\",\"scores\":[10,20,30]}]",
          "argsJson preserves nested structure ('%s')", f.argsJson.c_str());
}

static void test_parse_string_escapes() {
    std::printf("\n--- Test: parse string escapes ---\n");
    RpcFrame f;
    bool ok = parseFrame("{\"id\":1,\"m\":\"say\\\\nhi\",\"a\":[]}", f);
    CHECK(ok, "parse succeeded with \\\\n escape");
    CHECK(f.method == "say\\nhi",
          "method is 'say\\nhi' (got '%s')", f.method.c_str());
}

static void test_parse_unicode_escape() {
    std::printf("\n--- Test: parse \\u escape ---\n");
    RpcFrame f;
    bool ok = parseFrame("{\"id\":1,\"m\":\"snow\\u2603man\",\"a\":[]}", f);
    CHECK(ok, "parse succeeded with \\u2603 escape");
    // U+2603 (snowman) encodes to 3 UTF-8 bytes: E2 98 83
    CHECK(f.method.size() == 10, "method is 10 bytes (got %zu)",
          f.method.size());  // "snow" (4) + 3 UTF-8 bytes + "man" (3)
    CHECK(f.method.substr(0,4) == "snow", "starts with 'snow'");
    CHECK(f.method.substr(4,3) == "\xE2\x98\x83", "middle is U+2603 UTF-8");
    CHECK(f.method.substr(7) == "man", "ends with 'man'");
}

static void test_parse_trailing_data() {
    std::printf("\n--- Test: parse with trailing data (permissive) ---\n");
    // The parser should successfully parse the first frame and ignore
    // anything after the closing brace.
    RpcFrame f;
    bool ok = parseFrame("{\"id\":1,\"m\":\"x\",\"a\":[]}GARBAGE", f);
    CHECK(ok, "parse succeeded with trailing data");
    CHECK(f.kind == RpcFrame::Kind::Request, "kind is Request");
}

static void test_parse_rejects_empty() {
    std::printf("\n--- Test: parse rejects empty input ---\n");
    RpcFrame f;
    bool ok = parseFrame("", 0, f);
    CHECK(!ok, "parse rejected empty input");
    CHECK(f.kind == RpcFrame::Kind::Invalid, "kind is Invalid");
}

static void test_parse_rejects_non_object() {
    std::printf("\n--- Test: parse rejects non-object ---\n");
    RpcFrame f;
    bool ok = parseFrame("[1,2,3]", f);
    CHECK(!ok, "parse rejected array (not an object)");
}

static void test_parse_rejects_float_id() {
    std::printf("\n--- Test: parse rejects float id ---\n");
    RpcFrame f;
    bool ok = parseFrame("{\"id\":1.5,\"m\":\"x\",\"a\":[]}", f);
    CHECK(!ok, "parse rejected float id");
}

static void test_parse_rejects_negative_id() {
    std::printf("\n--- Test: parse rejects negative id ---\n");
    RpcFrame f;
    bool ok = parseFrame("{\"id\":-1,\"m\":\"x\",\"a\":[]}", f);
    CHECK(!ok, "parse rejected negative id");
}

static void test_parse_large_id() {
    std::printf("\n--- Test: parse large id ---\n");
    RpcFrame f;
    bool ok = parseFrame("{\"id\":4294967295,\"m\":\"x\",\"a\":[]}", f);
    CHECK(ok, "parse succeeded with UINT32_MAX id");
    CHECK(f.id == 4294967295u, "id is 4294967295 (got %u)", f.id);
}

// =============================================================================
// Round-trip tests — encode then parse, verify identity
// =============================================================================

static void test_roundtrip_request() {
    std::printf("\n--- Test: roundtrip request ---\n");
    std::string s = makeRequest(42, "player:move", "[\"up\",5]");
    RpcFrame f;
    bool ok = parseFrame(s, f);
    CHECK(ok, "roundtrip parse succeeded");
    CHECK(f.kind == RpcFrame::Kind::Request, "kind is Request");
    CHECK(f.id == 42, "id is 42");
    CHECK(f.method == "player:move", "method is 'player:move'");
    CHECK(f.argsJson == "[\"up\",5]", "argsJson is '[\"up\",5]'");
}

static void test_roundtrip_notify() {
    std::printf("\n--- Test: roundtrip notify ---\n");
    std::string s = makeNotify("chat", "[\"hello\"]");
    RpcFrame f;
    bool ok = parseFrame(s, f);
    CHECK(ok, "roundtrip parse succeeded");
    CHECK(f.kind == RpcFrame::Kind::Notify, "kind is Notify");
    CHECK(!f.hasId, "no id");
    CHECK(f.method == "chat", "method is 'chat'");
}

// =============================================================================
// JsonString escape tests
// =============================================================================

static void test_escape_basic() {
    std::printf("\n--- Test: escapeJsonString basic ---\n");
    std::string s = escapeJsonString("hello");
    CHECK(s == "\"hello\"", "escape basic ('%s')", s.c_str());
}

static void test_escape_quotes() {
    std::printf("\n--- Test: escapeJsonString with quotes ---\n");
    std::string s = escapeJsonString("say \"hi\"");
    CHECK(s == "\"say \\\"hi\\\"\"", "escape quotes ('%s')", s.c_str());
}

static void test_escape_newline() {
    std::printf("\n--- Test: escapeJsonString with newline ---\n");
    std::string s = escapeJsonString("line1\nline2");
    CHECK(s == "\"line1\\nline2\"", "escape newline ('%s')", s.c_str());
}

static void test_escape_control_char() {
    std::printf("\n--- Test: escapeJsonString with control char ---\n");
    // Use \x01 followed by an explicit string concat to avoid the C++
    // preprocessor eating the next char as part of the hex escape.
    std::string s = escapeJsonString(std::string("a").append(1, '\x01').append("b").c_str());
    CHECK(s == "\"a\\u0001b\"", "escape control char ('%s')", s.c_str());
}

// =============================================================================
// End-to-end RpcServer tests using MockNetPeer
// =============================================================================

static void test_rpc_register_and_call() {
    std::printf("\n--- Test: RpcServer register + notify ---\n");
    td::SignalBus::get().clearAll();
    td::RpcServer::get().reset();

    MockNetPeer server;
    MockNetPeer client;
    server.host(0, 4);
    client.connect("localhost", 0);
    server.connectTo(&client);

    td::RpcServer::get().setPeer(&client);

    std::atomic<int> calls{0};
    std::string receivedArgs;
    td::RpcServer::get().registerMethod("ping",
        [&](int /*peerId*/, const char* argsJson) {
            calls++;
            receivedArgs = argsJson ? argsJson : "";
        });

    // Client -> Server: notify (no reply)
    bool sent = td::RpcServer::get().callRemote("ping", "[\"hi\"]");
    CHECK(sent, "callRemote returned true");
    server.drain();   // server processes the notify

    CHECK(calls.load() == 1, "handler was called once (got %d)", calls.load());
    CHECK(receivedArgs == "[\"hi\"]", "argsJson matches ('%s')",
          receivedArgs.c_str());
}

static void test_rpc_request_response() {
    std::printf("\n--- Test: RpcServer request/response ---\n");
    td::SignalBus::get().clearAll();
    td::RpcServer::get().reset();

    MockNetPeer server;
    MockNetPeer client;
    server.host(0, 4);
    client.connect("localhost", 0);
    server.connectTo(&client);

    td::RpcServer::get().setPeer(&client);

    // Server-side: register a "ping" handler. The auto-response mechanism
    // in dispatchPacket() will send a `null` response after the handler
    // runs. (Handlers can also call sendResponse() explicitly with a
    // non-null result if they want to return a value.)
    std::atomic<int> handlerCalls{0};
    td::RpcServer::get().registerMethod("ping",
        [&](int /*peerId*/, const char* /*argsJson*/) {
            handlerCalls++;
        });

    std::atomic<bool> gotReply{false};
    std::atomic<bool> replyOk{false};
    std::string replyResult;
    uint32_t id = td::RpcServer::get().callWithReply(
        "ping", "[]", /*targetPeer=*/1, /*timeoutMs=*/1000,
        [&](bool ok, const char* resultJson) {
            gotReply = true;
            replyOk = ok;
            replyResult = resultJson ? resultJson : "";
        });
    CHECK(id > 0, "callWithReply returned a non-zero id");

    server.drain();   // server processes the request, sends response
    client.drain();   // client receives the response, fires callback

    CHECK(handlerCalls.load() == 1, "handler was called once");
    CHECK(gotReply.load(), "client received a reply");
    CHECK(replyOk.load(), "reply was success (ok=true)");
    CHECK(replyResult == "null", "reply result is 'null' (auto-response) ('%s')",
          replyResult.c_str());
}

static void test_rpc_unknown_method_returns_error() {
    std::printf("\n--- Test: RpcServer unknown method returns error ---\n");
    td::SignalBus::get().clearAll();
    td::RpcServer::get().reset();

    MockNetPeer server;
    MockNetPeer client;
    server.host(0, 4);
    client.connect("localhost", 0);
    server.connectTo(&client);

    td::RpcServer::get().setPeer(&client);

    // Server registers NO handlers.

    std::atomic<bool> gotReply{false};
    std::atomic<bool> replyOk{true};  // expect false
    std::string replyError;
    td::RpcServer::get().callWithReply(
        "nonexistent", "[]", 1, 1000,
        [&](bool ok, const char* resultJson) {
            gotReply = true;
            replyOk.store(ok);
            replyError = resultJson ? resultJson : "";
        });

    server.drain();
    client.drain();

    CHECK(gotReply.load(), "client received a (error) reply");
    CHECK(!replyOk.load(), "reply was rejection (ok=false)");
    // The error message should contain "unknown method"
    CHECK(replyError.find("unknown method") != std::string::npos,
          "error message contains 'unknown method' ('%s')",
          replyError.c_str());
}

static void test_rpc_timeout() {
    std::printf("\n--- Test: RpcServer callWithReply timeout ---\n");
    td::SignalBus::get().clearAll();
    td::RpcServer::get().reset();

    MockNetPeer server;
    MockNetPeer client;
    server.host(0, 4);
    client.connect("localhost", 0);
    server.connectTo(&client);

    td::RpcServer::get().setPeer(&client);

    // Server registers a handler that does NOTHING (doesn't reply).
    // The auto-null-response will still fire, so to test timeout we need
    // to NOT drain the server. The client's call will time out.
    std::atomic<bool> gotReply{false};
    std::atomic<bool> replyOk{true};
    td::RpcServer::get().callWithReply(
        "slow", "[]", 1, /*timeoutMs=*/100,
        [&](bool ok, const char* /*resultJson*/) {
            gotReply = true;
            replyOk.store(ok);
        });

    // Do NOT drain the server — the request goes into its inbox but is
    // never processed. The client should time out.
    auto t0 = std::chrono::steady_clock::now();
    while (!gotReply.load()) {
        td::RpcServer::get().update(MockNetPeer::nowMs());
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        auto t1 = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() > 2000) {
            break;  // safety
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    CHECK(gotReply.load(), "client received a (timeout) reply");
    CHECK(!replyOk.load(), "reply was rejection (ok=false)");
    CHECK(elapsedMs >= 90, "timeout fired after >= 90ms (got %lld ms)",
          (long long)elapsedMs);
    CHECK(elapsedMs < 1500, "timeout fired within 1.5s (got %lld ms)",
          (long long)elapsedMs);
}

// =============================================================================
// JS parity: verify a frame produced by the JS TDNet.RPC would parse
// correctly in C++. These are literal string snapshots of what the JS
// encoder emits (verified manually against net_websocket.js).
// =============================================================================

static void test_js_parity_request() {
    std::printf("\n--- Test: JS parity (request) ---\n");
    // What TDNet.RPC.callRemote('ping', ['hi']) produces in JS:
    //   JSON.stringify({id: 1, m: 'ping', a: ['hi']})
    RpcFrame f;
    bool ok = parseFrame("{\"id\":1,\"m\":\"ping\",\"a\":[\"hi\"]}", f);
    CHECK(ok, "parsed JS-produced request");
    CHECK(f.kind == RpcFrame::Kind::Request, "kind is Request");
    CHECK(f.id == 1, "id is 1");
    CHECK(f.method == "ping", "method is 'ping'");
    CHECK(f.argsJson == "[\"hi\"]", "argsJson is '[\"hi\"]'");
}

static void test_js_parity_response() {
    std::printf("\n--- Test: JS parity (response) ---\n");
    // What TDNet.RPC sends on success:
    //   JSON.stringify({id: 1, r: 'pong'})
    RpcFrame f;
    bool ok = parseFrame("{\"id\":1,\"r\":\"pong\"}", f);
    CHECK(ok, "parsed JS-produced response");
    CHECK(f.kind == RpcFrame::Kind::Response, "kind is Response");
    CHECK(f.id == 1, "id is 1");
    CHECK(f.resultJson == "\"pong\"", "resultJson is '\"pong\"'");
}

static void test_js_parity_error() {
    std::printf("\n--- Test: JS parity (error) ---\n");
    // What TDNet.RPC sends on unknown method:
    //   JSON.stringify({id: 1, e: 'unknown method: foo'})
    RpcFrame f;
    bool ok = parseFrame("{\"id\":1,\"e\":\"unknown method: foo\"}", f);
    CHECK(ok, "parsed JS-produced error");
    CHECK(f.kind == RpcFrame::Kind::Error, "kind is Error");
    CHECK(f.errorMessage == "unknown method: foo",
          "errorMessage matches");
}

static void test_js_parity_notify() {
    std::printf("\n--- Test: JS parity (notify) ---\n");
    // What TDNet.RPC.notify('heartbeat', [42]) produces:
    //   JSON.stringify({m: 'heartbeat', a: [42]})
    RpcFrame f;
    bool ok = parseFrame("{\"m\":\"heartbeat\",\"a\":[42]}", f);
    CHECK(ok, "parsed JS-produced notify");
    CHECK(f.kind == RpcFrame::Kind::Notify, "kind is Notify");
    CHECK(!f.hasId, "no id");
    CHECK(f.method == "heartbeat", "method is 'heartbeat'");
    CHECK(f.argsJson == "[42]", "argsJson is '[42]'");
}

// =============================================================================
// Main
// =============================================================================

int main() {
    std::printf("TD Engine - JSON-RPC parity tests (mirror JS TDNet.RPC)\n");
    std::printf("=======================================================\n");

    test_encode_request_basic();
    test_encode_request_empty_args();
    test_encode_response();
    test_encode_response_null();
    test_encode_error();
    test_encode_notify();
    test_encode_method_with_special_chars();

    test_parse_request_basic();
    test_parse_request_with_whitespace();
    test_parse_response();
    test_parse_response_number();
    test_parse_response_object();
    test_parse_error();
    test_parse_notify();
    test_parse_empty_array_args();
    test_parse_nested_args();
    test_parse_string_escapes();
    test_parse_unicode_escape();
    test_parse_trailing_data();
    test_parse_rejects_empty();
    test_parse_rejects_non_object();
    test_parse_rejects_float_id();
    test_parse_rejects_negative_id();
    test_parse_large_id();

    test_roundtrip_request();
    test_roundtrip_notify();

    test_escape_basic();
    test_escape_quotes();
    test_escape_newline();
    test_escape_control_char();

    test_rpc_register_and_call();
    test_rpc_request_response();
    test_rpc_unknown_method_returns_error();
    test_rpc_timeout();

    test_js_parity_request();
    test_js_parity_response();
    test_js_parity_error();
    test_js_parity_notify();

    std::printf("\n=======================================================\n");
    std::printf("Results: %d passed, %d failed\n", g_passes, g_failures);
    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("SOME TESTS FAILED\n");
    return 1;
}
