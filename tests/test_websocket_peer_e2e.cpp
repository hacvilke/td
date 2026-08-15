// =============================================================================
// TD Engine — WebSocketPeer end-to-end test (C++ server side)
//
// Pairs with tests/test_websocket_peer_e2e.js. The JS script spawns this
// binary, opens a WebSocket to it, and verifies the JSON-RPC round-trip.
//
// Build (manual, for now — not in CMakeLists to keep CI build time short):
//   g++ -std=c++17 -Wall -Wextra -O2 -Isrc \
//       tests/test_websocket_peer_e2e.cpp \
//       src/net/websocket_peer.cpp \
//       src/net/transport.cpp \
//       src/net/json_rpc.cpp \
//       tests/stub_logger.cpp \
//       -lpthread -o build/test_websocket_peer_e2e
//
// Usage:
//   ./build/test_websocket_peer_e2e <port>
//
// Behavior:
//   1. Hosts a WebSocketPeer on <port>.
//   2. Registers a "ping" RPC handler that returns "pong".
//   3. Registers an "echo" notify handler that broadcasts "echoed" to all
//      peers with the original arg.
//   4. Prints "LISTENING <port>" to stdout so the JS client knows to connect.
//   5. Polls in a loop. Exits when stdin receives "quit" or after 10s.
// =============================================================================

#include "net/websocket_peer.h"
#include "net/transport.h"
#include "net/json_rpc.h"
#include "core/logger.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#ifdef _WIN32
    #include <winsock2.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <unistd.h>
    #include <sys/select.h>
#endif

using td::WebSocketPeer;
using td::NetPacket;
using td::NetReliability;
using td::RpcServer;

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);

    // Start the WebSocketPeer server.
    WebSocketPeer peer;
    if (!peer.host(port, 8)) {
        fprintf(stderr, "[server] host(%d) failed: %s\n",
                port, peer.lastError().c_str());
        return 1;
    }

    // Register RPC handlers.
    RpcServer::get().setPeer(&peer);

    RpcServer::get().registerMethod("ping",
        [](int peerId, const char* /*argsJson*/) {
            // Reply with a Response frame.
            RpcServer::get().sendResponse(1, "\"pong\"", peerId);
        });

    RpcServer::get().registerMethod("echo",
        [](int peerId, const char* argsJson) {
            // Echo back as a Notify to ALL peers (broadcast).
            // argsJson looks like `["hello"]` — we just pass it through.
            RpcServer::get().callRemote("echoed", argsJson, /*targetPeer=*/-1);
        });

    // Signal readiness so the JS test client can connect.
    printf("LISTENING %d\n", port);
    fflush(stdout);

    // Poll loop. Exit on stdin "quit" or after 10 seconds.
    auto start = std::chrono::steady_clock::now();
    for (;;) {
        // Check stdin for "quit".
#ifdef _WIN32
        // On Windows, use select() on stdin (handle 0).
        // Simpler: just poll with timeout, no stdin check.
        Sleep(10);
#else
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        timeval tv = {0, 10000};  // 10ms
        if (select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &tv) > 0) {
            char buf[64];
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = 0;
                if (strstr(buf, "quit")) break;
            }
        }
#endif

        peer.poll();
        RpcServer::get().update(
            (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count());

        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 10) {
            fprintf(stderr, "[server] timeout\n");
            break;
        }
    }

    peer.disconnect();
    return 0;
}
