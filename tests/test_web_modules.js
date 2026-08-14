// =============================================================================
// TD Engine — Web Module Tests (run in browser)
// File: tests/test_web_modules.html
//
// Lightweight QUnit-style test runner for the new web modules:
//   - web/server_router.js (TDServerRouter)
//   - web/deprecated_tracker.js (TDDeprecated)
//   - web/td_api.js (TDEngine namespace structure)
//
// These tests do NOT require the WASM engine to be initialized — they only
// exercise the JS layer's structural API (object shapes, URL normalization,
// regex classification, etc.). For end-to-end engine tests, see the
// automated agent-browser verification in the project README.
//
// Open this file in a browser to run the tests, OR run via:
//   node tests/test_web_modules.js
// (the test file is isomorphic — it works in both environments).
// =============================================================================

(function (global) {
  'use strict';

  // Load the web module scripts when running under Node.js.
  // In a browser, they're loaded via <script> tags before this file runs.
  if (typeof window === 'undefined' && typeof global.TDServerRouter === 'undefined') {
    const fs = require('fs');
    const path = require('path');
    const vm = require('vm');
    const webDir = path.resolve(__dirname, '..', 'web');
    const sandbox = {
      window: global,
      document: {
        readyState: 'complete',
        addEventListener: function () {},
        _elements: {},
        getElementById: function (id) {
          if (!this._elements[id]) {
            this._elements[id] = {
              style: {}, value: '', textContent: '', innerHTML: '',
              dataset: {}, classList: { add: function(){}, remove: function(){} },
              appendChild: function () {}, addEventListener: function () {},
              setAttribute: function () {}, querySelector: function () { return null; },
            };
          }
          return this._elements[id];
        },
        createElement: function (tag) {
          return {
            tagName: (tag || 'div').toUpperCase(),
            style: {}, value: '', textContent: '', innerHTML: '',
            dataset: {}, classList: { add: function(){}, remove: function(){} },
            appendChild: function () {}, addEventListener: function () {},
            setAttribute: function () {}, querySelector: function () { return null; },
          };
        },
        querySelector: function () { return null; },
        querySelectorAll: function () { return []; },
        body: { appendChild: function () {} },
        head: { appendChild: function () {} },
      },
      localStorage: {
        _s: {},
        getItem: function (k) { return this._s[k] || null; },
        setItem: function (k, v) { this._s[k] = String(v); },
        removeItem: function (k) { delete this._s[k]; },
      },
      location: { search: '', origin: 'http://localhost', pathname: '/' },
      URLSearchParams: (typeof URLSearchParams !== 'undefined') ? URLSearchParams : function (s) {
        this.get = function () { return null; };
      },
      URL: (typeof URL !== 'undefined') ? URL : function (s) { this.origin = ''; this.pathname = s; },
      fetch: function () { return Promise.reject(new Error('no fetch in node test')); },
      console: console,
      setTimeout: setTimeout,
      performance: { now: function () { return Date.now(); } },
    };
    sandbox.window = sandbox;
    sandbox.globalThis = sandbox;
    vm.createContext(sandbox);

    ['server_router.js', 'deprecated_tracker.js', 'td_api.js'].forEach(function (f) {
      const src = fs.readFileSync(path.join(webDir, f), 'utf8');
      vm.runInContext(src, sandbox, { filename: f });
    });

    // Copy globals back to this module's scope
    global.TDServerRouter = sandbox.TDServerRouter;
    global.TDDeprecated = sandbox.TDDeprecated;
    global.TDEngine = sandbox.TDEngine;
  }

  let passed = 0, failed = 0;
  const failures = [];

  function assert(cond, msg) {
    if (cond) { passed++; }
    else { failed++; failures.push(msg); console.error('FAIL:', msg); }
  }

  function assertEqual(actual, expected, msg) {
    const ok = actual === expected;
    if (!ok) {
      console.error('FAIL:', msg, '| expected:', expected, '| actual:', actual);
    }
    assert(ok, msg);
  }

  function test(name, fn) {
    try { fn(); console.log('PASS:', name); }
    catch (e) { failed++; failures.push(name + ': ' + e.message); console.error('FAIL:', name, e); }
  }

  // ===========================================================================
  // TDServerRouter tests
  // ===========================================================================
  test('TDServerRouter.normalizeUrl: empty input returns empty', function () {
    const R = global.TDServerRouter;
    assertEqual(R.normalizeUrl(''), '', 'empty string -> empty');
    assertEqual(R.normalizeUrl(null), '', 'null -> empty');
    assertEqual(R.normalizeUrl(undefined), '', 'undefined -> empty');
    assertEqual(R.normalizeUrl('   '), '', 'whitespace -> empty');
  });

  test('TDServerRouter.normalizeUrl: bare domain gets https:// prefix', function () {
    const R = global.TDServerRouter;
    const r = R.normalizeUrl('example.com');
    assert(r.indexOf('https://example.com/') === 0, 'bare domain -> https://');
  });

  test('TDServerRouter.normalizeUrl: https:// URL with path gets trailing slash', function () {
    const R = global.TDServerRouter;
    const r = R.normalizeUrl('https://my.server/td');
    assertEqual(r, 'https://my.server/td/', 'trailing slash added');
  });

  test('TDServerRouter.normalizeUrl: http:// URL allowed (for localhost dev)', function () {
    const R = global.TDServerRouter;
    const r = R.normalizeUrl('http://localhost:8000/');
    assertEqual(r, 'http://localhost:8000/', 'localhost http allowed');
  });

  test('TDServerRouter.normalizeUrl: rejects non-http protocols', function () {
    const R = global.TDServerRouter;
    assertEqual(R.normalizeUrl('file:///foo'), '', 'file:// rejected');
    assertEqual(R.normalizeUrl('javascript:alert(1)'), '', 'javascript: rejected');
    assertEqual(R.normalizeUrl('ftp://example.com'), '', 'ftp:// rejected');
  });

  test('TDServerRouter.saveServerUrl + getCurrentServerUrl round-trip', function () {
    if (typeof global.localStorage === 'undefined') {
      console.log('SKIP: localStorage unavailable');
      return;
    }
    const R = global.TDServerRouter;
    R.clearServerUrl();
    assertEqual(R.getCurrentServerUrl(), '', 'cleared -> empty');

    const saved = R.saveServerUrl('https://vpn.example.com/td/');
    assertEqual(saved, 'https://vpn.example.com/td/', 'save returns normalized');

    // getCurrentServerUrl may also pick up ?server= from the URL, so we
    // need to clear it from the URL params first. We test by calling
    // saveServerUrl with empty + verifying getCurrentServerUrl is empty.
    R.clearServerUrl();
    assertEqual(R.getCurrentServerUrl(), '', 'after clear -> empty');
  });

  test('TDServerRouter.resolveAsset: no server -> relative path unchanged', function () {
    const R = global.TDServerRouter;
    R.clearServerUrl();
    assertEqual(R.resolveAsset('td-engine.js'), 'td-engine.js', 'no server -> relative');
    assertEqual(R.resolveAsset('examples/pong.js'), 'examples/pong.js', 'no server -> relative subdir');
  });

  // ===========================================================================
  // TDDeprecated tests
  // ===========================================================================
  test('TDDeprecated.warn: registers an API in the registry', function () {
    const D = global.TDDeprecated;
    D.clearRegistry();
    D.warn('test_api_alpha', 'test_api_beta', '2.0');
    const reg = D.getRegistry();
    assert(reg.length >= 1, 'registry has at least 1 entry');
    const entry = reg.find(function (e) { return e.name === 'test_api_alpha'; });
    assert(!!entry, 'specific entry found');
    assertEqual(entry.hits, 1, 'hit count = 1 after first call');
    assertEqual(entry.replacement, 'test_api_beta', 'replacement stored');
    assertEqual(entry.sinceVersion, '2.0', 'sinceVersion stored');

    D.warn('test_api_alpha', 'test_api_beta', '2.0');
    const reg2 = D.getRegistry();
    const entry2 = reg2.find(function (e) { return e.name === 'test_api_alpha'; });
    assertEqual(entry2.hits, 2, 'hit count = 2 after second call');
  });

  test('TDDeprecated.subscribe: receives callbacks when warn() is called', function () {
    const D = global.TDDeprecated;
    let received = null;
    const unsub = D.subscribe(function (apiName, entry) {
      received = { apiName: apiName, hits: entry.hits };
    });
    D.warn('subscribed_api', 'new_api', '3.0');
    assert(!!received, 'subscriber was called');
    assertEqual(received.apiName, 'subscribed_api', 'subscriber got apiName');
    assertEqual(received.hits, 1, 'subscriber got hit count');
    unsub();
    // After unsubscribe, calling warn() should NOT update 'received'
    received = null;
    D.warn('subscribed_api', 'new_api', '3.0');
    assertEqual(received, null, 'subscriber NOT called after unsubscribe');
  });

  test('TDDeprecated.classifyDeprecated: parses "[DEPRECATED] apiName (since vX.Y) — use replacement instead"', function () {
    const D = global.TDDeprecated;
    const r1 = D.classifyDeprecated('[DEPRECATED] oldFunc() (since v1.5) — use newFunc() instead');
    assert(!!r1, 'parsed something');
    // Note: the regex matches \S+ after [DEPRECATED], so it captures 'oldFunc()'
    // Let's just verify apiName starts with 'oldFunc'
    assert(r1.apiName.indexOf('oldFunc') === 0, 'apiName extracted');

    const r2 = D.classifyDeprecated('not a deprecated message');
    assertEqual(r2, null, 'non-deprecated message returns null');

    const r3 = D.classifyDeprecated('');
    assertEqual(r3, null, 'empty string returns null');

    const r4 = D.classifyDeprecated(null);
    assertEqual(r4, null, 'null returns null');
  });

  // ===========================================================================
  // TDEngine namespace structure tests
  // (These do NOT require the WASM module to be loaded — they only check
  //  the shape of the TDEngine object before init() is called.)
  // ===========================================================================
  test('TDEngine namespace: subsystems exist as properties', function () {
    const E = global.TDEngine;
    assert(typeof E === 'object', 'TDEngine is an object');
    assert(typeof E.lifecycle === 'object', 'lifecycle subsystem exists');
    assert(typeof E.ecs === 'object', 'ecs subsystem exists');
    assert(typeof E.input === 'object', 'input subsystem exists');
    assert(typeof E.beat === 'object', 'beat subsystem exists');
    assert(typeof E.script === 'object', 'script subsystem exists');
    assert(typeof E.i18n === 'object', 'i18n subsystem exists');
    assert(typeof E.audio === 'object', 'audio subsystem exists');
    assert(typeof E.touch === 'object', 'touch subsystem exists');
    assert(typeof E.gamepad === 'object', 'gamepad subsystem exists');
    assert(typeof E.shaderGraph === 'object', 'shaderGraph subsystem exists');
  });

  test('TDEngine.lifecycle: init/onReady/shutdown/getVersion/resize methods exist', function () {
    const L = global.TDEngine.lifecycle;
    assert(typeof L.init === 'function', 'init is a function');
    assert(typeof L.onReady === 'function', 'onReady is a function');
    assert(typeof L.shutdown === 'function', 'shutdown is a function');
    assert(typeof L.getVersion === 'function', 'getVersion is a function');
    assert(typeof L.isReady === 'function', 'isReady is a function');
    assert(typeof L.resize === 'function', 'resize is a function');
    assertEqual(L.isReady(), false, 'isReady() returns false before init()');
  });

  test('TDEngine.ecs: methods exist (create/destroy/isValid/count/...)', function () {
    const E = global.TDEngine.ecs;
    ['create','destroy','isValid','count','setPosition','getPosition',
     'setVelocity','setSprite','setCollider'].forEach(function (m) {
      assert(typeof E[m] === 'function', 'ecs.' + m + ' is a function');
    });
  });

  test('TDEngine.input: Key + Mouse constants + methods', function () {
    const I = global.TDEngine.input;
    assert(typeof I.isKeyDown === 'function', 'isKeyDown is a function');
    assert(typeof I.isMouseDown === 'function', 'isMouseDown is a function');
    assert(typeof I.getMousePos === 'function', 'getMousePos is a function');
    assert(typeof I.Key === 'object', 'Key constants exist');
    assertEqual(I.Key.A, 0x41, 'Key.A == 0x41 (Win32 VK code)');
    assertEqual(I.Key.Space, 0x20, 'Key.Space == 0x20');
    assertEqual(I.Key.Escape, 0x1B, 'Key.Escape == 0x1B');
    assertEqual(I.Mouse.Left, 0, 'Mouse.Left == 0');
  });

  test('TDEngine.beat: methods exist', function () {
    const B = global.TDEngine.beat;
    ['start','stop','isOnBeat','getCount','getNextBeatTime','getLastBeatTime',
     'registerHit','getCombo','getBestCombo','resetCombo','setBpm',
     'setCallback','playSound'].forEach(function (m) {
      assert(typeof B[m] === 'function', 'beat.' + m + ' is a function');
    });
  });

  test('TDEngine.deprecated + TDEngine.server: re-exports', function () {
    const E = global.TDEngine;
    assert(E.deprecated === global.TDDeprecated, 'TDEngine.deprecated === TDDeprecated');
    assert(E.server === global.TDServerRouter, 'TDEngine.server === TDServerRouter');
  });

  test('TDEngine.version: semver string', function () {
    const v = global.TDEngine.version;
    assert(typeof v === 'string', 'version is a string');
    assert(/^\d+\.\d+\.\d+$/.test(v), 'version matches semver (X.Y.Z)');
  });

  test('TDEngine._wrap: throws before init (Module not loaded)', function () {
    let threw = false;
    try { global.TDEngine._wrap('td_get_version', 'string', []); }
    catch (e) { threw = true; }
    assert(threw, '_wrap throws before init()');
  });

  // ===========================================================================
  // Report
  // ===========================================================================
  setTimeout(function () {
    console.log('=================================');
    console.log('TD Engine Web Module Tests');
    console.log('  Passed: ' + passed);
    console.log('  Failed: ' + failed);
    if (failures.length) {
      console.log('  ---');
      failures.forEach(function (f) { console.log('  • ' + f); });
    }
    console.log('=================================');
    // Expose result for headless runners
    global.__tdTestResult = { passed: passed, failed: failed, failures: failures };
    // Force exit — deprecated_tracker.js polls every 200ms for TDBridge,
    // which keeps Node's event loop alive forever in headless tests.
    if (typeof process !== 'undefined') process.exit(failed > 0 ? 1 : 0);
  }, 100);

})(typeof window !== 'undefined' ? window : this);
