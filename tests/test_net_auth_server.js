'use strict';

// Tests for web/net_auth_server.js
// Run via: node tests/test_net_auth_server.js
//
// Spins up a real auth server on a random port, connects a real WebSocket
// client, and asserts on the wire protocol.

const { createServer } = require('../web/net_auth_server.js');
const WebSocket = require('ws');

let passed = 0, failed = 0;
function assert(cond, msg) {
  if (cond) { passed++; }
  else { failed++; console.error(`  FAIL: ${msg}`); }
}

function waitForMs(ms) { return new Promise((r) => setTimeout(r, ms)); }

async function withServer(def, fn) {
  const port = 30000 + Math.floor(Math.random() * 10000);
  const server = createServer({ port, tickRate: 50 }); // 20Hz tight loop for tests
  server.defineRoom('test', def);
  server.start();
  await waitForMs(50);
  try {
    await fn(port, server);
  } finally {
    server.stop();
    await waitForMs(50);
  }
}

function connect(port, roomId) {
  return new WebSocket(`ws://localhost:${port}/room/${roomId || 'test'}`);
}

function nextMessage(ws, timeoutMs = 1000) {
  return new Promise((resolve, reject) => {
    const to = setTimeout(() => reject(new Error('timeout')), timeoutMs);
    ws.once('message', (raw) => {
      clearTimeout(to);
      try { resolve(JSON.parse(raw.toString())); } catch (e) { reject(e); }
    });
  });
}

// ---- Test 1: welcome message on connect ----------------------------------
(async function test_welcome() {
  await withServer({
    onPlayerJoin: (id, p) => ({ x: 0, y: 0, name: p.name }),
  }, async (port) => {
    const ws = connect(port);
    await new Promise((r) => ws.on('open', r));
    ws.send(JSON.stringify({ t: 'hello', profile: { name: 'Alice' } }));
    const msg = await nextMessage(ws);
    assert(msg.t === 'welcome', 'expected welcome');
    assert(typeof msg.peerId === 'number', 'peerId is a number');
    assert(msg.roomId === 'test', 'roomId is test');
    assert(msg.tickRate === 50, 'tickRate is 50');
    ws.close();
  });
})();

// ---- Test 2: snapshot arrives after tick ---------------------------------
(async function test_snapshot() {
  await withServer({
    onPlayerJoin: (id) => ({ x: 0, y: 0, vx: 0, vy: 0 }),
    onInput: (pid, input, state) => {
      if (input.dx) state.x += input.dx;
    },
  }, async (port) => {
    const ws = connect(port);
    await new Promise((r) => ws.on('open', r));
    ws.send(JSON.stringify({ t: 'hello', profile: {} }));
    await nextMessage(ws); // welcome
    // Send an input.
    ws.send(JSON.stringify({ t: 'input', input: { dx: 10 } }));
    // Wait for next snapshot.
    const snap = await nextMessage(ws, 2000);
    assert(snap.t === 'snapshot', 'expected snapshot');
    assert(snap.snapshot.entities, 'snapshot has entities');
    ws.close();
  });
})();

// ---- Test 3: RPC call returns result -------------------------------------
(async function test_rpc() {
  await withServer({
    onPlayerJoin: () => ({ x: 0 }),
    rpc: {
      add: (pid, args) => args[0] + args[1],
    },
  }, async (port) => {
    const ws = connect(port);
    await new Promise((r) => ws.on('open', r));
    ws.send(JSON.stringify({ t: 'hello', profile: {} }));
    await nextMessage(ws); // welcome
    ws.send(JSON.stringify({ t: 'rpc', id: 42, m: 'add', a: [3, 4] }));
    const resp = await nextMessage(ws, 2000);
    // Skip any snapshots that arrive first.
    let msg = resp;
    while (msg.t === 'snapshot') msg = await nextMessage(ws, 2000);
    assert(msg.t === 'rpc', 'expected rpc response');
    assert(msg.id === 42, 'rpc id matches');
    assert(msg.r === 7, 'rpc result is 7');
    ws.close();
  });
})();

// ---- Test 4: unknown RPC method returns error ---------------------------
(async function test_rpc_unknown() {
  await withServer({
    onPlayerJoin: () => ({ x: 0 }),
    rpc: {},
  }, async (port) => {
    const ws = connect(port);
    await new Promise((r) => ws.on('open', r));
    ws.send(JSON.stringify({ t: 'hello', profile: {} }));
    await nextMessage(ws); // welcome
    ws.send(JSON.stringify({ t: 'rpc', id: 99, m: 'nope', a: [] }));
    let msg = await nextMessage(ws, 2000);
    while (msg.t === 'snapshot') msg = await nextMessage(ws, 2000);
    assert(msg.t === 'rpc', 'expected rpc response');
    assert(msg.id === 99, 'id matches');
    assert(typeof msg.e === 'string', 'error message is string');
    assert(msg.e.includes('Unknown'), 'error says Unknown');
    ws.close();
  });
})();

// ---- Test 5: input applied + state mutated across ticks -----------------
(async function test_input_accumulates() {
  let lastX = -1;
  await withServer({
    onPlayerJoin: () => ({ x: 0, y: 0 }),
    onInput: (pid, input, state) => { state.x += input.dx; },
    onTick: (dt, players) => {
      for (const p of Object.values(players)) lastX = p.x;
    },
  }, async (port) => {
    const ws = connect(port);
    await new Promise((r) => ws.on('open', r));
    ws.send(JSON.stringify({ t: 'hello', profile: {} }));
    await nextMessage(ws); // welcome
    ws.send(JSON.stringify({ t: 'input', input: { dx: 5 } }));
    ws.send(JSON.stringify({ t: 'input', input: { dx: 5 } }));
    await waitForMs(150);
    assert(lastX === 10, `expected x=10 after two +5 inputs, got ${lastX}`);
    ws.close();
  });
})();

// ---- Test 6: unknown room kicks the client ------------------------------
(async function test_unknown_room() {
  await withServer({
    onPlayerJoin: () => ({ x: 0 }),
  }, async (port) => {
    const ws = connect(port, 'nonexistent');
    await new Promise((r) => ws.on('open', r));
    const msg = await nextMessage(ws, 2000);
    assert(msg.t === 'kick', 'expected kick');
    assert(msg.reason.includes('Unknown room'), 'reason says Unknown room');
    ws.close();
  });
})();

// ---- Run sequentially + summary ------------------------------------------
(async () => {
  // Wait a moment for any in-flight async tests to settle.
  await waitForMs(200);
  console.log(`\nnet_auth_server: ${passed} passed, ${failed} failed`);
  process.exit(failed === 0 ? 0 : 1);
})();
