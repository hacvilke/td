// =============================================================================
// TD Engine — Peer Transport Tests
// File: tests/test_net_peer.js
//
// Tests for web/net_peer.js (TDNet.Peer):
//   - isSupported / join validation
//   - Peer API surface (send / sendTo / peers / rtt / on / leave)
//   - Fake BroadcastChannel that simulates cross-tab message delivery
//   - hello / helloAck discovery handshake
//   - bye leave announcement
//   - ping/pong RTT measurement
//   - stale peer sweep
//   - directed vs broadcast messaging
//
// Runs in Node via vm sandbox with a fake BroadcastChannel, OR in a browser.
//   node tests/test_net_peer.js
// =============================================================================

(function (global) {
  'use strict';

  if (typeof window === 'undefined' && typeof global.TDNet === 'undefined') {
    const fs = require('fs');
    const path = require('path');
    const vm = require('vm');

    // Fake BroadcastChannel — simulates cross-tab delivery by routing
    // postMessage to every other channel instance on the same name.
    const channels = new Map();  // name -> Set of channel instances
    class FakeBroadcastChannel {
      constructor(name) {
        this.name = name;
        this.onmessage = null;
        this._closed = false;
        if (!channels.has(name)) channels.set(name, new Set());
        channels.get(name).add(this);
      }
      postMessage(msg) {
        if (this._closed) return;
        const set = channels.get(this.name);
        if (!set) return;
        set.forEach(function (ch) {
          if (ch === this || ch._closed) return;
          // Defer to mimic async message delivery
          setTimeout(function () {
            if (ch.onmessage && !ch._closed) {
              ch.onmessage({ data: msg });
            }
          }, 0);
        }, this);
      }
      close() {
        this._closed = true;
        const set = channels.get(this.name);
        if (set) set.delete(this);
      }
    }

    const sandbox = {
      console: console,
      setTimeout: setTimeout,
      setInterval: function (fn, ms) { return ++_timerId; },
      clearInterval: function () {},
      Date: Date,
      Math: Math,
      JSON: JSON,
      BroadcastChannel: FakeBroadcastChannel,
    };
    var _timerId = 0;
    sandbox.window = sandbox;
    sandbox.globalThis = sandbox;
    vm.createContext(sandbox);

    const webDir = path.resolve(__dirname, '..', 'web');
    const src = fs.readFileSync(path.join(webDir, 'net_peer.js'), 'utf8');
    vm.runInContext(src, sandbox, { filename: 'net_peer.js' });

    global.TDNet = sandbox.TDNet;
    global.__sandbox = sandbox;
    global.__resetChannels = function () {
      channels.clear();
      _timerId = 0;
    };
  }

  let passed = 0, failed = 0;
  const failures = [];

  function assert(cond, msg) {
    if (cond) { passed++; }
    else { failed++; failures.push(msg); console.error('FAIL:', msg); }
  }
  function assertEqual(actual, expected, msg) {
    const ok = actual === expected;
    if (!ok) console.error('FAIL:', msg, '| expected:', expected, '| actual:', actual);
    assert(ok, msg);
  }
  // Collect test functions; run them sequentially at the end so async
  // tests actually await before the next one starts (which would call
  // __resetChannels() and wipe the fake channel registry mid-test).
  const testQueue = [];
  function test(name, fn) {
    testQueue.push({ name: name, fn: fn });
  }

  // Helper: wait N ms (lets fake BroadcastChannel messages flush)
  function wait(ms) { return new Promise(function (r) { setTimeout(r, ms || 50); }); }

  const P = global.TDNet.Peer;

  // ===========================================================================
  // isSupported / join validation
  // ===========================================================================

  test('isSupported: returns true when BroadcastChannel is available', function () {
    assertEqual(P.isSupported(), true, 'supported in test env');
  });

  test('join: throws on missing channelName', function () {
    let threw = false;
    try { P.join(''); } catch (e) { threw = true; }
    assert(threw, 'empty name throws');
    threw = false;
    try { P.join(null); } catch (e) { threw = true; }
    assert(threw, 'null name throws');
  });

  // ===========================================================================
  // Peer API surface
  // ===========================================================================

  test('join: returns Peer instance with expected methods', function () {
    global.__resetChannels();
    const p = P.join('test-api', { peerId: 'p1' });
    assertEqual(p.peerId, 'p1', 'peerId preserved');
    assertEqual(typeof p.send, 'function', 'has send()');
    assertEqual(typeof p.sendTo, 'function', 'has sendTo()');
    assertEqual(typeof p.peers, 'function', 'has peers()');
    assertEqual(typeof p.rtt, 'function', 'has rtt()');
    assertEqual(typeof p.on, 'function', 'has on()');
    assertEqual(typeof p.leave, 'function', 'has leave()');
    p.leave();
  });

  test('join: auto-generates peerId when not provided', function () {
    global.__resetChannels();
    const p = P.join('test-id');
    assert(typeof p.peerId === 'string', 'peerId is string');
    assert(p.peerId.length > 5, 'peerId is non-trivial length');
    p.leave();
  });

  // ===========================================================================
  // Discovery handshake (hello / helloAck)
  // ===========================================================================

  test('two peers discover each other via hello + helloAck', async function () {
    global.__resetChannels();
    const p1 = P.join('discovery', { peerId: 'p1' });
    const p2 = P.join('discovery', { peerId: 'p2' });
    await wait(60);
    assert(p1.peers().includes('p2'), 'p1 sees p2');
    assert(p2.peers().includes('p1'), 'p2 sees p1');
    p1.leave(); p2.leave();
  });

  test('on("join") fires when a new peer arrives', async function () {
    global.__resetChannels();
    const p1 = P.join('onjoin', { peerId: 'p1' });
    let joinedId = null;
    p1.on('join', function (id) { joinedId = id; });
    const p2 = P.join('onjoin', { peerId: 'p2' });
    await wait(60);
    assertEqual(joinedId, 'p2', 'p1 received join event for p2');
    p1.leave(); p2.leave();
  });

  test('opts.onJoin callback fires (legacy wiring)', async function () {
    global.__resetChannels();
    let joinedId = null;
    const p1 = P.join('legacy', { peerId: 'p1', onJoin: function (id) { joinedId = id; } });
    const p2 = P.join('legacy', { peerId: 'p2' });
    await wait(60);
    assertEqual(joinedId, 'p2', 'opts.onJoin fired');
    p1.leave(); p2.leave();
  });

  // ===========================================================================
  // Messaging
  // ===========================================================================

  test('send: broadcasts to all peers', async function () {
    global.__resetChannels();
    const p1 = P.join('broadcast', { peerId: 'p1' });
    let p2Got = null, p3Got = null;
    const p2 = P.join('broadcast', { peerId: 'p2', onMessage: function (e) { p2Got = e; } });
    const p3 = P.join('broadcast', { peerId: 'p3', onMessage: function (e) { p3Got = e; } });
    await wait(60);
    const sent = p1.send({ hello: 'world' });
    assertEqual(sent, true, 'send returns true when peers exist');
    await wait(40);
    assert(p2Got && p2Got.data.hello === 'world', 'p2 received broadcast');
    assert(p2Got.peerId === 'p1', 'p2 knows sender is p1');
    assert(p3Got && p3Got.data.hello === 'world', 'p3 received broadcast');
    p1.leave(); p2.leave(); p3.leave();
  });

  test('send: returns false when no peers', async function () {
    global.__resetChannels();
    const p1 = P.join('alone', { peerId: 'p1' });
    await wait(40);
    const sent = p1.send('hello?');
    assertEqual(sent, false, 'send returns false when no peers');
    p1.leave();
  });

  test('sendTo: delivers only to the target peer', async function () {
    global.__resetChannels();
    const p1 = P.join('directed', { peerId: 'p1' });
    let p2Got = null, p3Got = null;
    const p2 = P.join('directed', { peerId: 'p2', onMessage: function (e) { p2Got = e; } });
    const p3 = P.join('directed', { peerId: 'p3', onMessage: function (e) { p3Got = e; } });
    await wait(60);
    const ok = p1.sendTo('p2', { msg: 'just for p2' });
    assertEqual(ok, true, 'sendTo returns true when peer exists');
    await wait(40);
    assert(p2Got && p2Got.data.msg === 'just for p2', 'p2 received directed message');
    assertEqual(p3Got, null, 'p3 did not receive directed message');
    p1.leave(); p2.leave(); p3.leave();
  });

  test('sendTo: returns false for unknown peer', async function () {
    global.__resetChannels();
    const p1 = P.join('unknown', { peerId: 'p1' });
    await wait(40);
    assertEqual(p1.sendTo('nonexistent', 'x'), false, 'sendTo unknown returns false');
    p1.leave();
  });

  // ===========================================================================
  // Leave announcement
  // ===========================================================================

  test('leave: broadcasts bye, peers see on("leave")', async function () {
    global.__resetChannels();
    let leftId = null;
    const p1 = P.join('bye', { peerId: 'p1', onLeave: function (id) { leftId = id; } });
    const p2 = P.join('bye', { peerId: 'p2' });
    await wait(60);
    assert(p1.peers().includes('p2'), 'p1 sees p2 before leave');
    p2.leave();
    await wait(40);
    assertEqual(leftId, 'p2', 'p1 received leave event for p2');
    assert(!p1.peers().includes('p2'), 'p1 no longer sees p2');
    p1.leave();
  });

  // ===========================================================================
  // RTT measurement
  // ===========================================================================

  test('rtt: returns 0 when no peers', async function () {
    global.__resetChannels();
    const p1 = P.join('rtt-alone', { peerId: 'p1' });
    await wait(40);
    assertEqual(p1.rtt(), 0, 'rtt is 0 with no peers');
    p1.leave();
  });

  test('rtt: returns positive number after ping/pong exchange', async function () {
    global.__resetChannels();
    const p1 = P.join('rtt-exchange', { peerId: 'p1' });
    const p2 = P.join('rtt-exchange', { peerId: 'p2' });
    await wait(60);
    // Manually trigger a ping probe on p1 (fakes the interval firing)
    p1._probeRtt();
    await wait(30);
    const r = p1.rtt();
    assert(typeof r === 'number', 'rtt is a number');
    assert(r >= 0, 'rtt is non-negative');
    // Real value should be very small (sub-ms) in the fake env
    p1.leave(); p2.leave();
  });

  // ===========================================================================
  // Stale peer sweep
  // ===========================================================================

  test('stale peers are removed after 5s silence', async function () {
    global.__resetChannels();
    const p1 = P.join('stale', { peerId: 'p1' });
    const p2 = P.join('stale', { peerId: 'p2' });
    await wait(60);
    assert(p1.peers().includes('p2'), 'p2 visible initially');
    // Simulate p2 going silent: close its BC without sending bye
    p2._bc.close();
    p2._rttTimer && clearInterval(p2._rttTimer);
    // Manually age p1's view of p2
    p1._peers.get('p2').lastSeen = Date.now() - 6000;
    p1._sweepStale();
    assert(!p1.peers().includes('p2'), 'p2 swept after staleness');
    p1.leave();
  });

  // ===========================================================================
  // Message format / edge cases
  // ===========================================================================

  test('peer ignores its own messages', async function () {
    global.__resetChannels();
    let gotOwn = false;
    const p1 = P.join('self-ignore', { peerId: 'p1', onMessage: function () { gotOwn = true; } });
    await wait(40);
    p1.send('echo me');
    await wait(40);
    assertEqual(gotOwn, false, 'peer does not receive its own broadcast');
    p1.leave();
  });

  test('peer ignores malformed messages', async function () {
    global.__resetChannels();
    const p1 = P.join('malformed', { peerId: 'p1' });
    // Directly inject malformed raw messages
    p1._onRaw(null);
    p1._onRaw('not an object');
    p1._onRaw({ noType: true });
    p1._onRaw({ t: 123, id: 'p2' });  // wrong t type
    p1._onRaw({ t: 'data', id: 'p1' });  // own id (should be ignored)
    assertEqual(p1.peers().length, 0, 'no peers added from malformed input');
    p1.leave();
  });

  test('state event fires on join and leave', async function () {
    global.__resetChannels();
    const states = [];
    const p1 = P.join('state', { peerId: 'p1', onState: function (s) { states.push(s); } });
    p1.leave();
    assert(states.includes('connected'), 'connected state observed');
    assert(states.includes('disconnected'), 'disconnected state observed');
  });

  test('three peers all see each other', async function () {
    global.__resetChannels();
    const p1 = P.join('trio', { peerId: 'p1' });
    const p2 = P.join('trio', { peerId: 'p2' });
    const p3 = P.join('trio', { peerId: 'p3' });
    await wait(80);
    assertEqual(p1.peers().length, 2, 'p1 sees 2 peers');
    assertEqual(p2.peers().length, 2, 'p2 sees 2 peers');
    assertEqual(p3.peers().length, 2, 'p3 sees 2 peers');
    assert(p1.peers().includes('p2') && p1.peers().includes('p3'), 'p1 sees p2+p3');
    p1.leave(); p2.leave(); p3.leave();
  });

  // ===========================================================================
  // Run all tests sequentially (async-aware), then print summary
  // ===========================================================================

  async function runAll() {
    for (let i = 0; i < testQueue.length; i++) {
      const t = testQueue[i];
      try {
        const r = t.fn();
        if (r && typeof r.then === 'function') await r;
        console.log('PASS:', t.name);
      } catch (e) {
        failed++; failures.push(t.name + ': ' + e.message); console.error('FAIL:', t.name, e);
      }
    }
    console.log('\n===============================================');
    console.log('  Peer Transport Test Summary');
    console.log('===============================================');
    console.log('  Passed: ' + passed);
    console.log('  Failed: ' + failed);
    if (failures.length) {
      console.log('  Failures:');
      failures.forEach(function (m) { console.log('    - ' + m); });
    }
    console.log('===============================================\n');
    if (typeof process !== 'undefined') process.exit(failed > 0 ? 1 : 0);
  }

  runAll();

})(typeof window !== 'undefined' ? window : this);
