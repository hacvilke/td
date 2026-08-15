// =============================================================================
// TD Engine — End-to-end WebSocket peer integration test
//
// Verifies that the C++ WebSocketPeer (src/net/websocket_peer.{h,cpp}) speaks
// the SAME wire format as the JS TDNet.RPC layer (web/net_websocket.js),
// which is locked by the 28 tests in tests/test_net_websocket.js.
//
// How it works:
//   1. Spawns a C++ test binary that hosts a WebSocketPeer on a random port.
//   2. The C++ binary prints "LISTENING <port>" once the listener is ready.
//   3. This Node script opens a real browser-style WebSocket to that port.
//   4. Sends a JSON-RPC request frame:  {"id":1,"m":"ping","a":[]}
//   5. The C++ RpcServer has a registered "ping" handler that replies with
//      the response frame:  {"id":1,"r":"pong"}
//   6. This script asserts it receives the exact response frame.
//   7. Sends a notify frame (no id):  {"m":"echo","a":["hello"]}
//   8. C++ handler broadcasts back to all peers; this script asserts it
//      receives the echoed notify.
//   9. Closes cleanly; the C++ binary exits 0.
//
// Build the C++ binary (from repo root):
//   g++ -std=c++17 -Wall -Wextra -O2 -Isrc \
//       tests/test_websocket_peer_e2e.cpp \
//       src/net/websocket_peer.cpp \
//       src/net/transport.cpp \
//       src/net/json_rpc.cpp \
//       tests/stub_logger.cpp \
//       -lpthread -o build/test_websocket_peer_e2e
//
// Run:
//   node tests/test_websocket_peer_e2e.js
// =============================================================================

'use strict';

const { spawn } = require('child_process');
const path = require('path');
const net = require('net');

const REPO_ROOT = path.resolve(__dirname, '..');
const CPP_BIN = path.join(REPO_ROOT, 'build', 'test_websocket_peer_e2e');

let pass = 0, fail = 0;
function assert(cond, msg) {
  if (cond) { pass++; console.log('PASS:', msg); }
  else      { fail++; console.error('FAIL:', msg); }
}

function findFreePort() {
  return new Promise((resolve, reject) => {
    const srv = net.createServer();
    srv.unref();
    srv.on('error', reject);
    srv.listen(0, '127.0.0.1', () => {
      const port = srv.address().port;
      srv.close(() => resolve(port));
    });
  });
}

async function main() {
  // 1. Find a free port (so the C++ binary can listen without collision).
  const port = await findFreePort();

  // 2. Spawn the C++ test server.
  const child = spawn(CPP_BIN, [String(port)], { stdio: ['pipe', 'pipe', 'pipe'] });
  let ready = false;
  let stderrBuf = '';

  child.stdout.on('data', (buf) => {
    const text = buf.toString();
    if (!ready && text.startsWith('LISTENING')) {
      ready = true;
      runClient(port).then(() => {
        // Tell the C++ server to exit cleanly.
        child.stdin.write('quit\n');
      }).catch((e) => {
        console.error('Client failed:', e);
        child.kill('SIGKILL');
        process.exit(1);
      });
    }
  });
  child.stderr.on('data', (buf) => { stderrBuf += buf.toString(); });
  child.on('exit', (code) => {
    if (stderrBuf.trim()) console.log('[C++ stderr]:', stderrBuf.trim());
    console.log(`[C++ exit] code=${code}`);
  });

  // Timeout watchdog — if the C++ binary doesn't start in 5s, abort.
  setTimeout(() => {
    if (!ready) {
      console.error('TIMEOUT: C++ server never said LISTENING');
      console.error('stderr:', stderrBuf);
      child.kill('SIGKILL');
      process.exit(1);
    }
  }, 5000);
}

async function runClient(port) {
  // Use Node's built-in WebSocket (Node 22+) or fall back to ws package.
  let WebSocketImpl;
  try {
    WebSocketImpl = require('ws');
  } catch {
    if (typeof WebSocket !== 'undefined') {
      WebSocketImpl = WebSocket;
    } else {
      throw new Error('No WebSocket implementation available (install "ws" or use Node 22+)');
    }
  }

  await new Promise((r) => setTimeout(r, 100));  // small grace period

  const ws = new WebSocketImpl(`ws://127.0.0.1:${port}/`);
  const received = [];

  await new Promise((resolve, reject) => {
    ws.on('open', () => {
      // Step 1: send a JSON-RPC request.
      ws.send(JSON.stringify({ id: 1, m: 'ping', a: [] }));
    });
    ws.on('message', (data, isBinary) => {
      const text = typeof data === 'string' ? data : data.toString();
      received.push(text);

      // Verify the response.
      if (received.length === 1) {
        let frame;
        try { frame = JSON.parse(received[0]); } catch (e) { reject(e); return; }
        assert(frame.id === 1 && frame.r === 'pong',
               `ping response: ${received[0]}`);

        // Step 2: send a notify (no id).
        ws.send(JSON.stringify({ m: 'echo', a: ['hello'] }));
      } else if (received.length === 2) {
        let frame;
        try { frame = JSON.parse(received[1]); } catch (e) { reject(e); return; }
        // The C++ handler echoes back as a notify with method "echoed".
        assert(frame.m === 'echoed' && Array.isArray(frame.a) && frame.a[0] === 'hello',
               `echo notify: ${received[1]}`);
        ws.close();
        resolve();
      }
    });
    ws.on('error', reject);
    setTimeout(() => reject(new Error('WebSocket timeout')), 4000);
  });

  // Step 3: summary.
  console.log('---');
  console.log(`WebSocket peer E2E: passed ${pass}, failed ${fail}`);
  if (fail > 0) process.exit(1);
}

main().catch((e) => { console.error(e); process.exit(1); });
