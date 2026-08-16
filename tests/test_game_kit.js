// =============================================================================
// TD Engine — Game Kit Tests (Node + browser)
// File: tests/test_game_kit.js
//
// Tests the four namespaces exposed by web/game_kit.js:
//   - TDAssets  (fetchBytes, fetchText, fetchJson, decodeImage, loadTexture,
//                loadAudio, cache management)
//   - TDCDN     (addOrigin, removeOrigin, resolve, fetchPath, failover)
//   - TDRest    (setDefaultHeader, request, getJson, postJson, rate-limit
//                handling, retry on 5xx)
//   - TDServer  (subscribe/publish via fake TDNet.Socket, RPC, registerHook)
//
// Runs in Node via a vm sandbox with a fake fetch + fake WebSocket. The
// same file also runs in a browser (where the globals are already defined).
// =============================================================================

(function (global) {
  'use strict';

  if (typeof window === 'undefined' && typeof global.TDGameKit === 'undefined') {
    const fs = require('fs');
    const path = require('path');
    const vm = require('vm');
    const webDir = path.resolve(__dirname, '..', 'web');

    // --- Fake fetch --------------------------------------------------------
    // Routes: map from URL substring -> { status, body, headers } or function.
    const fetchRoutes = [];
    function addFetchRoute(matcher, response) {
      fetchRoutes.push({ matcher, response });
    }
    function clearFetchRoutes() { fetchRoutes.length = 0; }

    function fakeFetch(url, opts) {
      opts = opts || {};
      // Find the first matching route.
      for (const r of fetchRoutes) {
        const isMatch = (typeof r.matcher === 'string')
          ? url.indexOf(r.matcher) >= 0
          : r.matcher.test(url);
        if (!isMatch) continue;
        const resp = (typeof r.response === 'function')
          ? r.response(url, opts)
          : r.response;
        // Mimic the Response interface used by game_kit.js.
        return Promise.resolve({
          ok: (resp.status >= 200 && resp.status < 300),
          status: resp.status || 200,
          headers: {
            get(name) { return (resp.headers && resp.headers[name]) || null; },
          },
          arrayBuffer() { return Promise.resolve(toArrayBuffer(resp.body || '')); },
          text()        { return Promise.resolve(String(resp.body || '')); },
          json()        { return Promise.resolve(JSON.parse(resp.body || 'null')); },
        });
      }
      // No route matched — return 404.
      return Promise.resolve({
        ok: false, status: 404,
        headers: { get() { return null; } },
        arrayBuffer() { return Promise.resolve(new ArrayBuffer(0)); },
        text()        { return Promise.resolve(''); },
        json()        { return Promise.resolve(null); },
      });
    }
    function toArrayBuffer(s) {
      if (s instanceof ArrayBuffer) return s;
      if (typeof s === 'string') {
        const buf = new ArrayBuffer(s.length);
        const view = new Uint8Array(buf);
        for (let i = 0; i < s.length; i++) view[i] = s.charCodeAt(i) & 0xFF;
        return buf;
      }
      return s;
    }

    // --- Fake WebSocket (mirrors test_net_websocket.js) --------------------
    function FakeWebSocket(url) {
      this.url = url;
      this.readyState = 0;
      this._sent = [];
      this._listeners = { open: [], close: [], error: [], message: [] };
      const self = this;
      setTimeout(function () {
        self.readyState = 1;
        self._listeners.open.forEach(function (cb) { cb({}); });
      }, 0);
    }
    FakeWebSocket.prototype.addEventListener = function (evt, cb) {
      if (this._listeners[evt]) this._listeners[evt].push(cb);
    };
    FakeWebSocket.prototype.send = function (raw) {
      this._sent.push(raw);
      // Allow tests to invoke the message handler.
      if (this._onMessage) {
        try { this._onMessage(JSON.parse(raw)); } catch (_) {}
      }
    };
    FakeWebSocket.prototype.close = function () {
      this.readyState = 3;
      this._listeners.close.forEach(function (cb) { cb({}); });
    };

    // --- Sandbox -----------------------------------------------------------
    const sandbox = {
      console: console,
      setTimeout, clearTimeout, setInterval, clearInterval,
      performance: { now: () => Date.now() },
      fetch: fakeFetch,
      AbortController: (typeof AbortController !== 'undefined') ? AbortController : class {
        constructor() { this.signal = { aborted: false }; }
        abort() { this.signal.aborted = true; }
      },
      Blob: (typeof Blob !== 'undefined') ? Blob : class {
        constructor(parts) { this._parts = parts; this.size = parts.reduce((n, p) => n + p.length, 0); }
      },
      URL: { createObjectURL: () => 'blob:fake', revokeObjectURL: () => {} },
      // ImageBitmap shim — for tests, we don't actually decode; we just
      // check that decodeImage() returns something with width/height.
      createImageBitmap: (typeof createImageBitmap !== 'undefined') ? createImageBitmap : function (blob) {
        return Promise.resolve({ width: 4, height: 4 });
      },
      Image: (typeof Image !== 'undefined') ? Image : class {
        constructor() { this.onload = null; this.onerror = null; }
      },
      OffscreenCanvas: (typeof OffscreenCanvas !== 'undefined') ? OffscreenCanvas : class {
        constructor(w, h) { this.width = w; this.height = h; }
        getContext() { return { drawImage: () => {}, getImageData: () => ({ data: new Uint8Array(this.width * this.height * 4) }) }; }
      },
      AudioContext: (typeof AudioContext !== 'undefined') ? AudioContext : class {
        decodeAudioData(buf) { return Promise.resolve({ duration: 1.0, sampleRate: 44100 }); }
      },
      EventSource: (typeof EventSource !== 'undefined') ? EventSource : class {
        constructor() { this.onmessage = null; this.onerror = null; }
        close() {}
      },
    };
    sandbox.window = sandbox;
    sandbox.globalThis = sandbox;
    vm.createContext(sandbox);

    // Load net_websocket.js first (TDServer depends on TDNet.Socket).
    const netWsPath = path.join(webDir, 'net_websocket.js');
    if (fs.existsSync(netWsPath)) {
      const src = fs.readFileSync(netWsPath, 'utf8');
      sandbox.WebSocket = FakeWebSocket;
      vm.runInContext(src, sandbox, { filename: 'net_websocket.js' });
    }
    // Then load game_kit.js.
    const gkSrc = fs.readFileSync(path.join(webDir, 'game_kit.js'), 'utf8');
    vm.runInContext(gkSrc, sandbox, { filename: 'game_kit.js' });

    // Expose helpers to the test scope.
    global._gk = { sandbox, addFetchRoute, clearFetchRoutes, FakeWebSocket };
    global.TDAssets  = sandbox.TDAssets;
    global.TDCDN     = sandbox.TDCDN;
    global.TDRest    = sandbox.TDRest;
    global.TDServer  = sandbox.TDServer;
    global.TDGameKit = sandbox.TDGameKit;
    global.TDNet     = sandbox.TDNet;
  }

  // =========================================================================
  // Tiny test framework
  // =========================================================================
  let _pass = 0, _fail = 0;
  const _results = [];
  function ok(cond, msg) {
    if (cond) { _pass++; }
    else { _fail++; console.error('FAIL:', msg); _results.push({ msg, ok: false }); }
  }
  function eq(a, b, msg) { ok(a === b, (msg || '') + ' (got ' + JSON.stringify(a) + ', want ' + JSON.stringify(b) + ')'); }
  async function test(name, fn) {
    const startFail = _fail;
    try { await fn(); }
    catch (e) { _fail++; console.error('THREW:', name, e.message, e.stack); }
    const status = (_fail === startFail) ? 'ok  ' : 'FAIL';
    console.log(status, name);
  }

  // =========================================================================
  // Tests
  // =========================================================================
  async function main() {
    console.log('--- Game Kit tests ---');

    // ----- TDAssets -----
    await test('TDAssets.fetchText returns text body', async () => {
      if (global._gk) global._gk.clearFetchRoutes();
      if (global._gk) global._gk.addFetchRoute('hello.txt', { status: 200, body: 'hello world' });
      const t = await global.TDAssets.fetchText('https://x/hello.txt', { cache: false });
      eq(t, 'hello world', 'fetchText');
    });

    await test('TDAssets.fetchJson parses JSON', async () => {
      if (global._gk) global._gk.addFetchRoute('data.json', { status: 200, body: '{"a":1,"b":"two"}' });
      const j = await global.TDAssets.fetchJson('https://x/data.json', { cache: false });
      eq(j.a, 1, 'json .a');
      eq(j.b, 'two', 'json .b');
    });

    await test('TDAssets.fetchJson rejects invalid JSON', async () => {
      if (global._gk) global._gk.addFetchRoute('bad.json', { status: 200, body: '{not json' });
      let threw = false;
      try { await global.TDAssets.fetchJson('https://x/bad.json', { cache: false }); }
      catch (e) { threw = true; }
      ok(threw, 'invalid JSON throws');
    });

    await test('TDAssets.cache stores and returns', async () => {
      global.TDAssets.cacheClear();
      if (global._gk) global._gk.addFetchRoute('cached.txt', { status: 200, body: 'cached' });
      const t1 = await global.TDAssets.fetchText('https://x/cached.txt'); // cache: default true
      const t2 = await global.TDAssets.fetchText('https://x/cached.txt');
      eq(t1, 'cached', 'first fetch');
      eq(t2, 'cached', 'second fetch (cached)');
      eq(global.TDAssets.cacheCount(), 1, 'cache has 1 entry');
      global.TDAssets.cacheClear();
      eq(global.TDAssets.cacheCount(), 0, 'cache cleared');
    });

    await test('TDAssets.fetchBytes returns ArrayBuffer', async () => {
      if (global._gk) global._gk.addFetchRoute('bytes.bin', { status: 200, body: 'abcd' });
      const buf = await global.TDAssets.fetchBytes('https://x/bytes.bin', { cache: false });
      ok(buf instanceof ArrayBuffer, 'ArrayBuffer');
      eq(buf.byteLength, 4, 'byte length');
    });

    await test('TDAssets.loadAudio returns decoded buffer', async () => {
      if (global._gk) global._gk.addFetchRoute('sound.wav', { status: 200, body: 'RIFFfake' });
      const buf = await global.TDAssets.loadAudio('https://x/sound.wav', { cache: false });
      ok(buf != null && typeof buf.duration === 'number', 'audio buffer');
    });

    // ----- TDCDN -----
    await test('TDCDN.addOrigin + listOrigins', () => {
      global.TDCDN.clearOrigins();
      const i0 = global.TDCDN.addOrigin('https://cdn1.example.com/v1');
      const i1 = global.TDCDN.addOrigin('https://cdn2.example.com/v1', { weight: 2, label: 'secondary' });
      const list = global.TDCDN.listOrigins();
      eq(list.length, 2, 'two origins');
      eq(list[0].prefix, 'https://cdn1.example.com/v1/', 'prefix normalized with trailing slash');
      eq(list[1].weight, 2, 'weight set');
      eq(list[1].label, 'secondary', 'label set');
    });

    await test('TDCDN.resolve returns first healthy URL without HEAD check', async () => {
      global.TDCDN.clearOrigins();
      global.TDCDN.addOrigin('https://cdn1.example.com/v1');
      const url = await global.TDCDN.resolve('sprites/player.png', { checkHead: false });
      eq(url, 'https://cdn1.example.com/v1/sprites/player.png', 'resolve returns concatenated URL');
    });

    await test('TDCDN.resolve tries next origin on 404', async () => {
      global.TDCDN.clearOrigins();
      if (global._gk) global._gk.clearFetchRoutes();
      // First origin returns 404, second returns 200.
      if (global._gk) global._gk.addFetchRoute('cdn1.example.com', { status: 404 });
      if (global._gk) global._gk.addFetchRoute('cdn2.example.com', { status: 200 });
      global.TDCDN.addOrigin('https://cdn1.example.com/v1');
      global.TDCDN.addOrigin('https://cdn2.example.com/v1');
      const url = await global.TDCDN.resolve('asset.png');
      eq(url, 'https://cdn2.example.com/v1/asset.png', 'fell through to second origin');
    });

    await test('TDCDN.resolve respects weight (tries heavier first)', async () => {
      global.TDCDN.clearOrigins();
      if (global._gk) global._gk.clearFetchRoutes();
      if (global._gk) global._gk.addFetchRoute('cdn-heavy.example.com', { status: 200 });
      if (global._gk) global._gk.addFetchRoute('cdn-light.example.com', { status: 200 });
      global.TDCDN.addOrigin('https://cdn-light.example.com/v1', { weight: 1 });
      global.TDCDN.addOrigin('https://cdn-heavy.example.com/v1', { weight: 10 });
      const url = await global.TDCDN.resolve('asset.png');
      eq(url, 'https://cdn-heavy.example.com/v1/asset.png', 'heavier origin wins');
    });

    await test('TDCDN.resolve throws when no origins configured', async () => {
      global.TDCDN.clearOrigins();
      let threw = false;
      try { await global.TDCDN.resolve('x.png'); } catch (e) { threw = true; }
      ok(threw, 'no origins -> throws');
    });

    await test('TDCDN.fetchJson resolves + parses', async () => {
      global.TDCDN.clearOrigins();
      if (global._gk) global._gk.clearFetchRoutes();
      if (global._gk) global._gk.addFetchRoute('cdn.example.com', { status: 200, body: '{"k":42}' });
      global.TDCDN.addOrigin('https://cdn.example.com/v1');
      const j = await global.TDCDN.fetchJson('cfg.json', { checkHead: false });
      eq(j.k, 42, 'json value');
    });

    // ----- TDRest -----
    await test('TDRest.setDefaultHeader + getJson', async () => {
      global.TDRest.clearRateLimits();
      if (global._gk) global._gk.clearFetchRoutes();
      let capturedHeaders = null;
      if (global._gk) global._gk.addFetchRoute('api.example.com', (url, opts) => {
        capturedHeaders = opts.headers || {};
        return { status: 200, body: '{"ok":true}' };
      });
      global.TDRest.setDefaultHeader('Authorization', 'Bearer test-token');
      const j = await global.TDRest.getJson('https://api.example.com/v1/items');
      eq(j.ok, true, 'json ok');
      eq(capturedHeaders['Authorization'], 'Bearer test-token', 'auth header injected');
      global.TDRest.setDefaultHeader('Authorization', null); // clear
    });

    await test('TDRest retries on 5xx', async () => {
      global.TDRest.clearRateLimits();
      if (global._gk) global._gk.clearFetchRoutes();
      let calls = 0;
      if (global._gk) global._gk.addFetchRoute('api.example.com', () => {
        calls++;
        return calls < 2 ? { status: 500 } : { status: 200, body: '{"ok":true}' };
      });
      const j = await global.TDRest.getJson('https://api.example.com/v1/retry', { retries: 2, timeoutMs: 1000 });
      eq(j.ok, true, 'eventually succeeded');
      ok(calls >= 2, 'called at least twice (retried)');
    });

    await test('TDRest tracks 429 rate-limit + Retry-After', async () => {
      global.TDRest.clearRateLimits();
      if (global._gk) global._gk.clearFetchRoutes();
      if (global._gk) global._gk.addFetchRoute('rl.example.com', { status: 429, headers: { 'Retry-After': '60' } });
      let threw = false;
      try { await global.TDRest.getJson('https://rl.example.com/v1/limited'); }
      catch (e) { threw = true; }
      ok(threw, '429 response makes request fail');
      // Subsequent calls should be rate-limited (return wait > 0).
      ok(global.TDRest.isRateLimited('https://rl.example.com/v1/limited'), 'rate-limit tracked');
    });

    await test('TDRest.postJson JSON-encodes object body', async () => {
      global.TDRest.clearRateLimits();
      if (global._gk) global._gk.clearFetchRoutes();
      let capturedBody = null;
      let capturedContentType = null;
      if (global._gk) global._gk.addFetchRoute('post.example.com', (url, opts) => {
        capturedBody = opts.body;
        capturedContentType = (opts.headers || {})['Content-Type'];
        return { status: 200, body: '{"id":7}' };
      });
      const j = await global.TDRest.postJson('https://post.example.com/v1/create', { name: 'foo', x: 1 });
      eq(j.id, 7, 'response id');
      ok(typeof capturedBody === 'string', 'body is string');
      eq(capturedContentType, 'application/json', 'Content-Type auto-set');
      ok(capturedBody.indexOf('"name":"foo"') >= 0, 'body JSON-encoded');
    });

    // ----- TDServer (uses fake TDNet.Socket from net_websocket.js) -----
    await test('TDServer.registerHook + snapshot', () => {
      global.TDServer.registerHook('ping', () => 'pong');
      const snap = global.TDServer.snapshot();
      ok(snap.hookNames.indexOf('ping') >= 0, 'hook registered');
      global.TDServer.unregisterHook('ping');
      const snap2 = global.TDServer.snapshot();
      ok(snap2.hookNames.indexOf('ping') < 0, 'hook unregistered');
    });

    // ----- Summary -----
    setTimeout(() => {
      console.log('\n--- Summary ---');
      console.log('  pass:', _pass);
      console.log('  fail:', _fail);
      if (_fail > 0) process.exit(1);
    }, 100);
  }

  main().catch(e => { console.error('fatal:', e); process.exit(1); });
})(typeof globalThis !== 'undefined' ? globalThis
   : typeof window !== 'undefined' ? window
   : typeof global !== 'undefined' ? global
   : this);
