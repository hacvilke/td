// =============================================================================
// TD Engine — Self-Hosted Server Tests (Node only)
// File: tests/test_td_server.js
//
// Tests the standalone Node.js server at tools/server/td_server.js:
//   - Config parsing (parseArgs, loadConfig)
//   - TdServer class: peer lifecycle, room management, channel pub/sub,
//     RPC dispatch (built-in + game-registered), client hooks, save sync,
//     presence broadcast.
//   - Wire protocol conformance (frame dispatch from JSON messages).
//
// Uses the real `ws` library on a random port + a real WebSocket client
// (no mocks) so the full server stack is exercised end-to-end.
// =============================================================================

'use strict';

const path = require('path');
const fs = require('fs');
const os = require('os');
const WebSocket = require('ws');

const { TdServer, startServer, parseArgs, loadConfig } =
  require(path.resolve(__dirname, '..', 'tools', 'server', 'td_server.js'));

let _pass = 0, _fail = 0;
function ok(cond, msg) { if (cond) _pass++; else { _fail++; console.error('FAIL:', msg); } }
function eq(a, b, msg) { ok(a === b, (msg || '') + ' (got ' + JSON.stringify(a) + ', want ' + JSON.stringify(b) + ')'); }
async function test(name, fn) {
  const startFail = _fail;
  try { await fn(); }
  catch (e) { _fail++; console.error('THREW:', name, e.message, e.stack); }
  const status = (_fail === startFail) ? 'ok  ' : 'FAIL';
  console.log(status, name);
}

// Helper: open a real ws connection and wait for helloAck.
function connectClient(url, helloFrame) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(url);
    const received = [];
    ws.on('open', () => {
      ws.send(JSON.stringify(helloFrame || { t: 'hello' }));
    });
    ws.on('message', (raw) => {
      const f = JSON.parse(raw);
      received.push(f);
      if (f.t === 'helloAck') {
        resolve({ ws, frames: received, playerId: f.id, room: f.room });
      }
    });
    ws.on('error', reject);
    setTimeout(() => reject(new Error('connect timeout')), 3000);
  });
}

function send(ws, obj) { ws.send(JSON.stringify(obj)); }
function waitForFrame(frames, type, timeoutMs) {
  timeoutMs = timeoutMs || 1000;
  return new Promise((resolve, reject) => {
    const start = Date.now();
    function check() {
      const f = frames.find(x => x.t === type);
      if (f) return resolve(f);
      if (Date.now() - start > timeoutMs) return reject(new Error('timeout waiting for ' + type));
      setTimeout(check, 20);
    }
    check();
  });
}

async function main() {
  console.log('--- Self-hosted server tests ---');

  // -- parseArgs --
  await test('parseArgs parses --key value pairs', () => {
    const a = parseArgs(['node', 'x', '--port', '8080', '--static', './pub']);
    eq(a.port, '8080', 'port');
    eq(a.static, './pub', 'static');
  });
  await test('parseArgs handles boolean flags', () => {
    const a = parseArgs(['node', 'x', '--verbose']);
    eq(a.verbose, true, 'verbose flag');
  });

  // -- loadConfig --
  await test('loadConfig applies defaults', () => {
    const cfg = loadConfig({});
    eq(cfg.port, 8080, 'default port');
    eq(cfg.maxPlayersPerRoom, 16, 'default maxPlayers');
    eq(cfg.cors.origins[0], '*', 'default CORS');
  });
  await test('loadConfig reads CLI args', () => {
    const cfg = loadConfig({ port: '9999', static: './x' });
    eq(cfg.port, 9999, 'port from CLI');
    eq(cfg.staticDir, './x', 'staticDir from CLI');
  });
  await test('loadConfig reads env vars', () => {
    process.env.TD_PORT = '7777';
    process.env.TD_SAVES_DIR = '/tmp/test-saves';
    const cfg = loadConfig({});
    eq(cfg.port, 7777, 'port from env');
    eq(cfg.savesDir, '/tmp/test-saves', 'savesDir from env');
    delete process.env.TD_PORT;
    delete process.env.TD_SAVES_DIR;
  });

  // -- TdServer class (no network) --
  await test('TdServer.addPeer + removePeer', () => {
    const s = new TdServer({ maxPlayersPerRoom: 4, presenceIntervalMs: 60000 });
    const id = s.addPeer({ readyState: 1, send: () => {} });
    eq(s.peers.size, 1, 'one peer after add');
    s.removePeer(id);
    eq(s.peers.size, 0, 'zero peers after remove');
  });

  await test('TdServer.registerRpc + dispatch returns result', async () => {
    const s = new TdServer({ maxPlayersPerRoom: 4, presenceIntervalMs: 60000 });
    s.registerRpc('add', (args) => args[0] + args[1]);
    const id = s.addPeer({ readyState: 1, send: (raw) => {
      const f = JSON.parse(raw);
      // Capture the result.
      if (f.t === 'rpcResult') s._test_result = f;
    }});
    s._onFrame(id, { t: 'rpc', id: 1, method: 'add', argsJson: '[3,4]' });
    // _handleRpc is async; wait a tick.
    await new Promise(r => setTimeout(r, 50));
    ok(s._test_result && s._test_result.ok === true, 'rpc ok');
    eq(s._test_result.result, 7, 'rpc returns 7');
  });

  await test('TdServer RPC: unknown method returns error', async () => {
    const s = new TdServer({ maxPlayersPerRoom: 4, presenceIntervalMs: 60000 });
    const id = s.addPeer({ readyState: 1, send: (raw) => {
      const f = JSON.parse(raw);
      if (f.t === 'rpcResult') s._test_result = f;
    }});
    s._onFrame(id, { t: 'rpc', id: 2, method: 'nonexistent', argsJson: '[]' });
    await new Promise(r => setTimeout(r, 50));
    ok(s._test_result && s._test_result.ok === false, 'rpc not ok');
    ok(s._test_result.error.indexOf('no such method') >= 0, 'error message');
  });

  await test('TdServer rooms: hello creates/joins room', () => {
    const s = new TdServer({ maxPlayersPerRoom: 4, presenceIntervalMs: 60000 });
    const sent = [];
    const id = s.addPeer({ readyState: 1, send: (raw) => sent.push(JSON.parse(raw)) });
    s._onFrame(id, { t: 'hello', room: 'arena-1' });
    eq(s.peers.get(id).room, 'arena-1', 'peer joined arena-1');
    eq(s.rooms.get('arena-1').size, 1, 'room has 1 peer');
    const ack = sent.find(f => f.t === 'helloAck');
    ok(ack, 'helloAck sent');
    eq(ack.id, id, 'ack has peerId');
    eq(ack.room, 'arena-1', 'ack has room');
  });

  await test('TdServer channels: sub + publish routes to subscribers', () => {
    const s = new TdServer({ maxPlayersPerRoom: 4, presenceIntervalMs: 60000 });
    const sentA = [], sentB = [];
    const a = s.addPeer({ readyState: 1, send: (raw) => sentA.push(JSON.parse(raw)) });
    const b = s.addPeer({ readyState: 1, send: (raw) => sentB.push(JSON.parse(raw)) });
    s._onFrame(a, { t: 'hello', room: 'r' });
    s._onFrame(b, { t: 'hello', room: 'r' });
    // A subscribes to 'explosions'
    s._onFrame(a, { t: 'sub', topic: 'explosions' });
    // B publishes to 'explosions'
    s._onFrame(b, { t: 'channel', topic: 'explosions', payload: { x: 1, y: 2 } });
    const chan = sentA.find(f => f.t === 'channel' && f.topic === 'explosions');
    ok(chan, 'A received channel message');
    eq(chan.payload.x, 1, 'payload x');
    eq(chan.from, b, 'from is B');
    // B should NOT have received its own message.
    const bChan = sentB.find(f => f.t === 'channel' && f.topic === 'explosions');
    ok(!bChan, 'B did not receive own message');
  });

  await test('TdServer channels: directed (to=peerId) only reaches target', () => {
    const s = new TdServer({ maxPlayersPerRoom: 4, presenceIntervalMs: 60000 });
    const sentA = [], sentB = [], sentC = [];
    const a = s.addPeer({ readyState: 1, send: (raw) => sentA.push(JSON.parse(raw)) });
    const b = s.addPeer({ readyState: 1, send: (raw) => sentB.push(JSON.parse(raw)) });
    const c = s.addPeer({ readyState: 1, send: (raw) => sentC.push(JSON.parse(raw)) });
    s._onFrame(a, { t: 'hello', room: 'r' });
    s._onFrame(b, { t: 'hello', room: 'r' });
    s._onFrame(c, { t: 'hello', room: 'r' });
    // A sends a directed message to C (no sub needed for directed).
    s._onFrame(a, { t: 'channel', topic: 'whisper', payload: 'hi', to: c });
    const cChan = sentC.find(f => f.t === 'channel' && f.topic === 'whisper');
    const bChan = sentB.find(f => f.t === 'channel' && f.topic === 'whisper');
    ok(cChan, 'C received directed message');
    ok(!bChan, 'B did not receive directed message');
  });

  await test('TdServer save sync: push + pull + list', async () => {
    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'td-saves-'));
    const s = new TdServer({ savesDir: tmpDir, maxPlayersPerRoom: 4, presenceIntervalMs: 60000 });
    const id = s.addPeer({ readyState: 1, send: () => {} });
    s._onFrame(id, { t: 'hello', room: 'r' });

    // Push
    let result = await s._handleRpcDirect(id, 1, 'savePush',
                                          ['slot1', '{"version":1,"data":{"x":1}}']);
    eq(result.ok, true, 'savePush ok');
    ok(fs.existsSync(path.join(tmpDir, id, 'slot1.json')), 'file written');

    // Pull
    result = await s._handleRpcDirect(id, 2, 'savePull', ['slot1']);
    ok(result.json.indexOf('"x":1') >= 0, 'savePull returns json');

    // List
    result = await s._handleRpcDirect(id, 3, 'saveList', []);
    eq(result.slots.length, 1, 'saveList returns 1 slot');
    eq(result.slots[0].name, 'slot1', 'slot name');

    // Cleanup.
    fs.rmSync(tmpDir, { recursive: true, force: true });
  });

  await test('TdServer callClientHook: invokes client + receives result', async () => {
    const s = new TdServer({ maxPlayersPerRoom: 4, presenceIntervalMs: 60000 });
    const sent = [];
    const id = s.addPeer({ readyState: 1, send: (raw) => sent.push(JSON.parse(raw)) });
    s._onFrame(id, { t: 'hello', room: 'r' });

    // Fire the hook call (don't await yet — we need to simulate the client's reply).
    const hookP = s.callClientHook(id, 'isReady', [], { timeoutMs: 1000 });
    await new Promise(r => setTimeout(r, 20));
    const hookFrame = sent.find(f => f.t === 'hook');
    ok(hookFrame, 'server sent hook frame');
    eq(hookFrame.name, 'isReady', 'hook name');

    // Simulate client reply.
    s._handleHookResult(id, { t: 'hookResult', id: hookFrame.id, ok: true, resultJson: '{"ready":true}' });
    const result = await hookP;
    eq(result.ready, true, 'hook result');
  });

  await test('TdServer callClientHook: timeout rejects', async () => {
    const s = new TdServer({ maxPlayersPerRoom: 4, presenceIntervalMs: 60000 });
    const id = s.addPeer({ readyState: 1, send: () => {} });
    s._onFrame(id, { t: 'hello', room: 'r' });
    let threw = false;
    try { await s.callClientHook(id, 'neverReplies', [], { timeoutMs: 50 }); }
    catch (e) { threw = true; }
    ok(threw, 'hook call timed out');
  });

  await test('TdServer presence: hello triggers presence broadcast', () => {
    const s = new TdServer({ maxPlayersPerRoom: 4, presenceIntervalMs: 60000 });
    const sentA = [], sentB = [];
    const a = s.addPeer({ readyState: 1, send: (raw) => sentA.push(JSON.parse(raw)) });
    const b = s.addPeer({ readyState: 1, send: (raw) => sentB.push(JSON.parse(raw)) });
    s._onFrame(a, { t: 'hello', room: 'r' });
    s._onFrame(b, { t: 'hello', room: 'r' });
    // After B joins, A should have received a presence frame with 2 peers.
    const presenceA = sentA.filter(f => f.t === 'presence');
    ok(presenceA.length >= 1, 'A got at least one presence frame');
    const last = presenceA[presenceA.length - 1];
    eq(last.peers.length, 2, 'last presence has 2 peers');
  });

  // -- End-to-end via real WebSocket --
  await test('E2E: real ws client connects + receives helloAck', async () => {
    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'td-e2e-'));
    const { server, httpServer } = startServer({
      port: 0, savesDir: tmpDir, maxPlayersPerRoom: 4, presenceIntervalMs: 60000,
    });
    // Wait one tick for httpServer to start listening.
    await new Promise(r => setTimeout(r, 50));
    const port = httpServer.address().port;
    const url = 'ws://localhost:' + port;
    try {
      const { ws, frames, playerId, room } = await connectClient(url, { t: 'hello', room: 'arena-1' });
      ok(playerId, 'playerId assigned');
      eq(room, 'arena-1', 'room echoed back');
      ws.close();
    } finally {
      httpServer.close();
      fs.rmSync(tmpDir, { recursive: true, force: true });
    }
  });

  await test('E2E: two clients in same room receive each other presence', async () => {
    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'td-e2e-'));
    const { server, httpServer } = startServer({
      port: 0, savesDir: tmpDir, maxPlayersPerRoom: 4, presenceIntervalMs: 60000,
    });
    await new Promise(r => setTimeout(r, 50));
    const port = httpServer.address().port;
    const url = 'ws://localhost:' + port;
    try {
      const c1 = await connectClient(url, { t: 'hello', room: 'r' });
      const c2 = await connectClient(url, { t: 'hello', room: 'r' });
      // Wait for c1 to receive a presence frame that has 2 peers (after c2 joins).
      // The server sends a presence broadcast right after each hello, but the
      // message might still be in flight; poll for up to 1s.
      const start = Date.now();
      let last = null;
      while (Date.now() - start < 1000) {
        const presences = c1.frames.filter(f => f.t === 'presence');
        if (presences.length > 0) {
          last = presences[presences.length - 1];
          if (last.peers.length === 2) break;
        }
        await new Promise(r => setTimeout(r, 30));
      }
      ok(last, 'c1 received at least one presence frame');
      eq(last.peers.length, 2, 'last presence has 2 peers (after c2 joined)');
      c1.ws.close();
      c2.ws.close();
    } finally {
      httpServer.close();
      fs.rmSync(tmpDir, { recursive: true, force: true });
    }
  });

  await test('E2E: RPC round-trip (roomList)', async () => {
    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'td-e2e-'));
    const { server, httpServer } = startServer({
      port: 0, savesDir: tmpDir, maxPlayersPerRoom: 4, presenceIntervalMs: 60000,
    });
    await new Promise(r => setTimeout(r, 50));
    const port = httpServer.address().port;
    const url = 'ws://localhost:' + port;
    try {
      const c = await connectClient(url, { t: 'hello', room: 'lobby' });
      // Send RPC.
      send(c.ws, { t: 'rpc', id: 1, method: 'roomList', argsJson: '[]' });
      const rpcResult = await waitForFrame(c.frames, 'rpcResult', 1000);
      eq(rpcResult.ok, true, 'rpc ok');
      ok(Array.isArray(rpcResult.result.rooms), 'result.rooms is array');
      c.ws.close();
    } finally {
      httpServer.close();
      fs.rmSync(tmpDir, { recursive: true, force: true });
    }
  });

  await test('E2E: channel pub/sub across two clients', async () => {
    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'td-e2e-'));
    const { server, httpServer } = startServer({
      port: 0, savesDir: tmpDir, maxPlayersPerRoom: 4, presenceIntervalMs: 60000,
    });
    await new Promise(r => setTimeout(r, 50));
    const port = httpServer.address().port;
    const url = 'ws://localhost:' + port;
    try {
      const c1 = await connectClient(url, { t: 'hello', room: 'r' });
      const c2 = await connectClient(url, { t: 'hello', room: 'r' });
      // c1 subscribes to 'explosions'.
      send(c1.ws, { t: 'sub', topic: 'explosions' });
      // c2 publishes.
      send(c2.ws, { t: 'channel', topic: 'explosions', payload: { x: 5, y: 6 } });
      const chan = await waitForFrame(c1.frames, 'channel', 1000).catch(() => null);
      // Filter for the right topic (the first 'channel' might be a different one).
      const explosion = c1.frames.find(f => f.t === 'channel' && f.topic === 'explosions');
      ok(explosion, 'c1 received explosions channel message');
      eq(explosion.payload.x, 5, 'payload x');
      c1.ws.close();
      c2.ws.close();
    } finally {
      httpServer.close();
      fs.rmSync(tmpDir, { recursive: true, force: true });
    }
  });

  await test('E2E: game-registered RPC handler', async () => {
    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'td-e2e-'));
    const { server, httpServer } = startServer({
      port: 0, savesDir: tmpDir, maxPlayersPerRoom: 4, presenceIntervalMs: 60000,
    });
    server.registerRpc('rollDice', () => Math.floor(Math.random() * 6) + 1);
    await new Promise(r => setTimeout(r, 50));
    const port = httpServer.address().port;
    const url = 'ws://localhost:' + port;
    try {
      const c = await connectClient(url, { t: 'hello', room: 'lobby' });
      send(c.ws, { t: 'rpc', id: 1, method: 'rollDice', argsJson: '[]' });
      const rpcResult = await waitForFrame(c.frames, 'rpcResult', 1000);
      eq(rpcResult.ok, true, 'rollDice ok');
      ok(rpcResult.result >= 1 && rpcResult.result <= 6, 'dice in [1,6]');
      c.ws.close();
    } finally {
      httpServer.close();
      fs.rmSync(tmpDir, { recursive: true, force: true });
    }
  });

  // -- Summary --
  setTimeout(() => {
    console.log('\n--- Summary ---');
    console.log('  pass:', _pass);
    console.log('  fail:', _fail);
    if (_fail > 0) process.exit(1);
  }, 200);
}

// Helper: invoke an RPC handler directly (bypassing the wire) for unit tests.
// Returns the result (or throws on error).
TdServer.prototype._handleRpcDirect = async function (peerId, callId, method, args) {
  const handler = this.rpcHandlers.get(method);
  if (!handler) throw new Error('no such method: ' + method);
  const peer = this.peers.get(peerId);
  const ctx = { peerId, room: peer ? peer.room : null, server: this };
  return await Promise.resolve().then(() => handler(args, ctx));
};

main().catch(e => { console.error('fatal:', e); process.exit(1); });
