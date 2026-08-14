// =============================================================================
// TD Engine — Network Module Tests (Node + browser)
// File: tests/test_net_websocket.js
//
// Tests TDNet.Socket (using a fake WebSocket), TDNet.RPC (request/response,
// timeout, unknown method), and TDNet.ServerConfig (localStorage profile
// management). All tests run in Node via the vm sandbox; the same file also
// runs in a browser (where the globals are already defined).
// =============================================================================

(function (global) {
  'use strict';

  if (typeof window === 'undefined' && typeof global.TDNet === 'undefined') {
    const fs = require('fs');
    const path = require('path');
    const vm = require('vm');
    const webDir = path.resolve(__dirname, '..', 'web');
    const sandbox = {
      console: console,
      setTimeout: setTimeout,
      clearTimeout: clearTimeout,
      setInterval: setInterval,
      clearInterval: clearInterval,
      performance: { now: function () { return Date.now(); } },
      localStorage: {
        _s: {},
        getItem: function (k) { return this._s[k] || null; },
        setItem: function (k, v) { this._s[k] = String(v); },
        removeItem: function (k) { delete this._s[k]; },
      },
      WebSocket: FakeWebSocket,
      document: {
        readyState: 'complete',
        addEventListener: function () {},
        _elements: {},
        getElementById: function (id) {
          if (!this._elements[id]) {
            this._elements[id] = makeFakeElement();
          }
          return this._elements[id];
        },
        createElement: function () { return makeFakeElement(); },
        querySelector: function () { return null; },
        querySelectorAll: function () { return []; },
        body: { appendChild: function () {} },
        head: { appendChild: function () {} },
      },
      alert: function () {},
      TextDecoder: (typeof TextDecoder !== 'undefined') ? TextDecoder : function () {
        this.decode = function (b) { return String.fromCharCode.apply(null, new Uint8Array(b)); };
      },
    };
    sandbox.window = sandbox;
    sandbox.globalThis = sandbox;
    vm.createContext(sandbox);

    function makeFakeElement() {
      return {
        style: {}, value: '', textContent: '', innerHTML: '',
        dataset: {}, classList: { add: function(){}, remove: function(){} },
        appendChild: function () {}, addEventListener: function () {},
        setAttribute: function () {}, querySelector: function () { return null; },
        querySelectorAll: function () { return []; },
      };
    }

    function FakeWebSocket(url) {
      this.url = url;
      this.readyState = 0;  // CONNECTING
      this.binaryType = 'blob';
      this._listeners = { open: [], close: [], error: [], message: [] };
      this._sent = [];
      // Auto-open on next tick to simulate async handshake
      const self = this;
      setTimeout(function () {
        self.readyState = 1;  // OPEN
        self._listeners.open.forEach(function (cb) { cb({}); });
      }, 0);
    }
    FakeWebSocket.prototype.addEventListener = function (evt, cb) {
      if (this._listeners[evt]) this._listeners[evt].push(cb);
    };
    FakeWebSocket.prototype.send = function (data) { this._sent.push(data); };
    FakeWebSocket.prototype.close = function () {
      this.readyState = 3;  // CLOSED
      this._listeners.close.forEach(function (cb) { cb({}); });
    };
    // Synthesize an incoming message
    FakeWebSocket.prototype._receive = function (data, isBinary) {
      const ev = { data: data };
      this._listeners.message.forEach(function (cb) { cb(ev); });
    };
    sandbox.WebSocket = FakeWebSocket;

    // Load all web modules in order
    ['server_router.js', 'deprecated_tracker.js', 'td_api.js', 'net_websocket.js'].forEach(function (f) {
      const src = fs.readFileSync(path.join(webDir, f), 'utf8');
      vm.runInContext(src, sandbox, { filename: f });
    });

    global.TDNet = sandbox.TDNet;
    global.TDEngine = sandbox.TDEngine;
    global._FakeWebSocket = FakeWebSocket;
    global._sandbox = sandbox;
  }

  let passed = 0, failed = 0;
  const failures = [];

  function assert(cond, msg) {
    if (cond) { passed++; }
    else { failed++; failures.push(msg); console.error('FAIL:', msg); }
  }

  function test(name, fn) {
    try { fn(); console.log('PASS:', name); }
    catch (e) { failed++; failures.push(name + ': ' + e.message); console.error('FAIL:', name, e); }
  }

  function asyncTest(name, fn) {
    return new Promise(function (resolve) {
      try {
        fn().then(function () {
          console.log('PASS:', name);
          resolve();
        }).catch(function (e) {
          failed++; failures.push(name + ': ' + e.message);
          console.error('FAIL:', name, e);
          resolve();
        });
      } catch (e) {
        failed++; failures.push(name + ': ' + e.message);
        console.error('FAIL:', name, e);
        resolve();
      }
    });
  }

  // ===========================================================================
  // Socket tests
  // ===========================================================================
  test('TDNet.Socket: exists + has connect() helper', function () {
    assert(typeof global.TDNet === 'object', 'TDNet is an object');
    assert(typeof global.TDNet.Socket === 'function', 'Socket is a constructor');
    assert(typeof global.TDNet.connect === 'function', 'connect() helper exists');
  });

  test('TDNet.Socket: states enum', function () {
    const S = global.TDNet.STATES;
    assertEqual(S.CONNECTING, 0, 'CONNECTING == 0');
    assertEqual(S.OPEN, 1, 'OPEN == 1');
    assertEqual(S.CLOSING, 2, 'CLOSING == 2');
    assertEqual(S.CLOSED, 3, 'CLOSED == 3');
  });

  asyncTest('TDNet.Socket: opens + send queues during CONNECTING, flushes on OPEN', function () {
    return new Promise(function (resolve) {
      const sock = new global.TDNet.Socket('ws://test.example/room');
      // Send immediately (state is CONNECTING) — should queue
      const sentImmediate = sock.sendText('queued-msg');
      // After async open, the queue should flush
      sock.onOpen = function () {
        // Now send should succeed immediately
        const sentLive = sock.sendText('live-msg');
        assert(sentLive === true, 'send after open returns true');
        // Verify both messages were sent
        assert(sock._ws._sent.indexOf('queued-msg') !== -1, 'queued message flushed');
        assert(sock._ws._sent.indexOf('live-msg') !== -1, 'live message sent');
        sock.close();
        resolve();
      };
    });
  });

  // ===========================================================================
  // RPC tests
  // ===========================================================================
  asyncTest('TDNet.RPC: registerMethod + callRemote round-trip', function () {
    return new Promise(function (resolve) {
      const conn = global.TDNet.connect('ws://rpc-test.example/room');
      const rpc = conn.rpc;

      rpc.registerMethod('add', function (args) {
        return args[0] + args[1];
      });

      conn.socket.onOpen = function () {
        rpc.callRemote('add', [3, 4], 1000).then(function (result) {
          assertEqual(result, 7, 'add(3,4) = 7');
          conn.socket.close();
          resolve();
        }).catch(function (e) {
          assert(false, 'callRemote should have succeeded: ' + e.message);
          resolve();
        });
      };

      // Simulate the server echoing back the response by intercepting sends
      const origSend = conn.socket._ws.send.bind(conn.socket._ws);
      conn.socket._ws.send = function (data) {
        origSend(data);
        // Parse the request and synthesize a response
        try {
          const msg = JSON.parse(data);
          if (msg.m === 'add' && msg.id !== undefined) {
            // Simulate server computing the result
            const result = msg.a[0] + msg.a[1];
            const response = { id: msg.id, r: result };
            // Receive the response on next tick
            const self = this;
            setTimeout(function () { self._receive(JSON.stringify(response)); }, 0);
          }
        } catch (e) {}
      };
    });
  });

  asyncTest('TDNet.RPC: timeout when no response', function () {
    return new Promise(function (resolve) {
      const conn = global.TDNet.connect('ws://rpc-timeout.example/room');
      const rpc = conn.rpc;
      rpc.setDefaultTimeout(100);  // 100ms for fast test

      conn.socket.onOpen = function () {
        rpc.callRemote('never_responds', [], 100).then(function () {
          assert(false, 'should NOT have resolved');
          resolve();
        }).catch(function (e) {
          assert(e.message.indexOf('timeout') !== -1, 'error mentions timeout');
          conn.socket.close();
          resolve();
        });
      };

      // Don't synthesize any response — let it time out
    });
  });

  asyncTest('TDNet.RPC: unknown method returns error', function () {
    return new Promise(function (resolve) {
      const conn = global.TDNet.connect('ws://rpc-unknown.example/room');
      const rpc = conn.rpc;

      conn.socket.onOpen = function () {
        rpc.callRemote('does_not_exist', [], 500).then(function () {
          assert(false, 'should NOT have resolved');
          resolve();
        }).catch(function (e) {
          assert(e.message.indexOf('unknown method') !== -1, 'error mentions unknown method');
          conn.socket.close();
          resolve();
        });
      };

      // Server receives the request and replies with an error (auto-handled by RPC)
      const origSend = conn.socket._ws.send.bind(conn.socket._ws);
      conn.socket._ws.send = function (data) {
        origSend(data);
        try {
          const msg = JSON.parse(data);
          if (msg.m && msg.id !== undefined) {
            // Server doesn't recognize the method -> send error response
            const response = { id: msg.id, e: 'unknown method: ' + msg.m };
            const self = this;
            setTimeout(function () { self._receive(JSON.stringify(response)); }, 0);
          }
        } catch (e) {}
      };
    });
  });

  test('TDNet.RPC: notify() is fire-and-forget (no reply expected)', function () {
    const conn = global.TDNet.connect('ws://rpc-notify.example/room');
    const rpc = conn.rpc;
    // notify before open — should return false
    assert(rpc.notify('foo', [1, 2]) === false, 'notify before open returns false');
    conn.socket.close();
  });

  test('TDNet.RPC: registerMethod supports async (Promise) callbacks', function () {
    return new Promise(function (resolve) {
      const conn = global.TDNet.connect('ws://rpc-async.example/room');
      const rpc = conn.rpc;
      rpc.registerMethod('asyncAdd', function (args) {
        return new Promise(function (resolve2) {
          setTimeout(function () { resolve2(args[0] * args[1]); }, 5);
        });
      });
      conn.socket.onOpen = function () {
        rpc.callRemote('asyncAdd', [6, 7], 1000).then(function (r) {
          assertEqual(r, 42, 'async add(6,7) = 42');
          conn.socket.close();
          resolve();
        }).catch(function (e) {
          assert(false, 'async call should succeed: ' + e.message);
          resolve();
        });
      };
      // Server-side: receive request, call the method (in real life the server
      // would do this; here we simulate by parsing the request and dispatching
      // it through the same RPC's registered method, then sending the response).
      const origSend = conn.socket._ws.send.bind(conn.socket._ws);
      conn.socket._ws.send = function (data) {
        origSend(data);
        try {
          const msg = JSON.parse(data);
          if (msg.m === 'asyncAdd' && msg.id !== undefined) {
            const cb = rpc._methods.get('asyncAdd');
            const result = cb(msg.a);
            result.then(function (r) {
              const response = { id: msg.id, r: r };
              conn.socket._ws._receive(JSON.stringify(response));
            });
          }
        } catch (e) {}
      };
    });
  });

  // ===========================================================================
  // ServerConfig tests
  // ===========================================================================
  test('TDNet.ServerConfig: list/add/remove/clear', function () {
    const SC = global.TDNet.ServerConfig;
    SC.clear();
    assertEqual(SC.list().length, 0, 'starts empty');

    const added = SC.add({ name: 'My VPN', url: 'wss://vpn.example.com/room', autoConnect: true });
    assert(!!added, 'add returns entry');
    assertEqual(added.name, 'My VPN', 'name stored');
    assertEqual(added.url, 'wss://vpn.example.com/room', 'url stored');
    assertEqual(added.autoConnect, true, 'autoConnect stored');
    assertEqual(SC.list().length, 1, 'list has 1 entry');

    SC.add({ url: 'wss://other.example.com/room' });
    assertEqual(SC.list().length, 2, 'list has 2 entries');

    const removed = SC.remove('wss://vpn.example.com/room');
    assert(removed, 'remove returns true');
    assertEqual(SC.list().length, 1, 'list has 1 entry after remove');
    assertEqual(SC.list()[0].url, 'wss://other.example.com/room', 'remaining entry is the other one');

    SC.clear();
    assertEqual(SC.list().length, 0, 'cleared');
  });

  test('TDNet.ServerConfig: setAutoConnect updates existing entry', function () {
    const SC = global.TDNet.ServerConfig;
    SC.clear();
    SC.add({ name: 'A', url: 'wss://a.example.com' });
    assertEqual(SC.list()[0].autoConnect, false, 'default autoConnect=false');

    const changed = SC.setAutoConnect('wss://a.example.com', true);
    assert(changed, 'setAutoConnect returns true on match');
    assertEqual(SC.list()[0].autoConnect, true, 'autoConnect now true');

    const notChanged = SC.setAutoConnect('wss://nonexistent.example.com', true);
    assert(!notChanged, 'setAutoConnect returns false on no match');
    SC.clear();
  });

  test('TDNet.ServerConfig: add requires URL', function () {
    const SC = global.TDNet.ServerConfig;
    SC.clear();
    const r = SC.add({ name: 'No URL' });
    assertEqual(r, null, 'add without url returns null');
    assertEqual(SC.list().length, 0, 'nothing added');
  });

  // ===========================================================================
  // TDEngine.net re-export
  // ===========================================================================
  test('TDEngine.net: re-exported from TDNet', function () {
    assert(global.TDEngine.net === global.TDNet, 'TDEngine.net === TDNet');
    assert(typeof global.TDEngine.net.connect === 'function', 'TDEngine.net.connect is a function');
    assert(typeof global.TDEngine.net.Socket === 'function', 'TDEngine.net.Socket is a constructor');
  });

  // ===========================================================================
  // Run async tests in sequence, then report
  // ===========================================================================
  function assertEqual(actual, expected, msg) {
    const ok = actual === expected;
    if (!ok) console.error('FAIL:', msg, '| expected:', expected, '| actual:', actual);
    assert(ok, msg);
  }

  setTimeout(function () {
    console.log('=================================');
    console.log('TD Engine Network Module Tests');
    console.log('  Passed: ' + passed);
    console.log('  Failed: ' + failed);
    if (failures.length) {
      console.log('  ---');
      failures.forEach(function (f) { console.log('  • ' + f); });
    }
    console.log('=================================');
    global.__tdNetTestResult = { passed: passed, failed: failed, failures: failures };
    // Force exit — net_websocket.js schedules timers that keep Node's
    // event loop alive; in headless tests we want a clean exit.
    if (typeof process !== 'undefined') process.exit(failed > 0 ? 1 : 0);
  }, 1500);

})(typeof window !== 'undefined' ? window : this);
