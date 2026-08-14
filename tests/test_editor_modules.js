// =============================================================================
// TD Engine — Inspector / Profiler / ErrorBoundary Tests
// File: tests/test_editor_modules.js
//
// Tests for the three Wave 4 editor modules:
//   - web/inspector.js      (TDInspector)
//   - web/profiler.js       (TDProfiler)
//   - web/error_boundary.js (TDErrorBoundary)
//
// Runs in Node.js via vm sandbox with a fake DOM, OR in a browser.
// Run:
//   node tests/test_editor_modules.js
// =============================================================================

(function (global) {
  'use strict';

  // ---- Node bootstrap -------------------------------------------------------

  if (typeof window === 'undefined' && typeof global.TDInspector === 'undefined') {
    const fs = require('fs');
    const path = require('path');
    const vm = require('vm');

    // Build a richer fake DOM than test_web_modules.js since inspector + error
    // boundary need createElement returning objects whose appendChild actually
    // tracks children (so we can assert on rendered structure).
    function makeEl(tag) {
      const el = {
        tagName: (tag || 'div').toUpperCase(),
        style: {},
        value: '',
        textContent: '',
        innerHTML: '',
        dataset: {},
        classList: { add: function(){}, remove: function(){}, contains: function(){return false;} },
        children: [],
        parentNode: null,
        attributes: {},
        setAttribute: function (k, v) { this.attributes[k] = v; },
        getAttribute: function (k) { return this.attributes[k] || null; },
        appendChild: function (c) { c.parentNode = this; this.children.push(c); return c; },
        removeChild: function (c) {
          const i = this.children.indexOf(c);
          if (i >= 0) this.children.splice(i, 1);
          c.parentNode = null;
          return c;
        },
        addEventListener: function () {},
        querySelector: function () { return null; },
        querySelectorAll: function () { return []; },
        getContext: function () { return fakeCtx; },
      };
      return el;
    }

    const fakeCtx = {
      clearRect: function () {},
      strokeStyle: '', lineWidth: 1, setLineDash: function () {},
      beginPath: function () {}, moveTo: function () {}, lineTo: function () {}, stroke: function () {},
      fillText: function () {}, fillRect: function () {},
    };

    const elements = {};
    const sandbox = {
      console: console,
      setTimeout: setTimeout,
      setInterval: function (fn, ms) { return 1; /* don't actually fire */ },
      clearInterval: function () {},
      requestAnimationFrame: function () { return 1; },
      performance: { now: function () { return Date.now(); }, memory: null },
      Date: Date,
      Math: Math,
      JSON: JSON,
      TextEncoder: typeof TextEncoder !== 'undefined' ? TextEncoder : null,
      navigator: { userAgent: 'node-test' },
      location: { href: 'http://localhost/test', reload: function () {} },
      localStorage: {
        _s: {},
        getItem: function (k) { return this._s[k] || null; },
        setItem: function (k, v) { this._s[k] = String(v); },
        removeItem: function (k) { delete this._s[k]; },
      },
      document: {
        readyState: 'complete',
        addEventListener: function (ev, cb) { if (ev === 'DOMContentLoaded') { /* already complete */ } },
        body: makeEl('body'),
        head: makeEl('head'),
        createElement: makeEl,
        getElementById: function (id) {
          if (!elements[id]) elements[id] = makeEl('div');
          return elements[id];
        },
        querySelector: function () { return null; },
        querySelectorAll: function () { return []; },
      },
      fetch: function () { return Promise.resolve({ ok: true }); },
    };
    sandbox.window = sandbox;
    sandbox.globalThis = sandbox;
    vm.createContext(sandbox);

    const webDir = path.resolve(__dirname, '..', 'web');
    ['inspector.js', 'profiler.js', 'error_boundary.js'].forEach(function (f) {
      const src = fs.readFileSync(path.join(webDir, f), 'utf8');
      vm.runInContext(src, sandbox, { filename: f });
    });

    global.TDInspector    = sandbox.TDInspector;
    global.TDProfiler     = sandbox.TDProfiler;
    global.TDErrorBoundary = sandbox.TDErrorBoundary;
    global.__sandbox       = sandbox;
  }

  // ---- test framework -------------------------------------------------------

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
  function test(name, fn) {
    try { fn(); console.log('PASS:', name); }
    catch (e) { failed++; failures.push(name + ': ' + e.message); console.error('FAIL:', name, e); }
  }

  // ===========================================================================
  // TDInspector
  // ===========================================================================

  test('TDInspector.snapshot: returns not-ready when TDEngine is absent', function () {
    const snap = global.TDInspector.snapshot();
    assertEqual(snap.entityCount, 0, 'no entities when engine absent');
    assertEqual(snap.error, 'not-ready', 'error flag is not-ready');
    assert(typeof snap.createdAt === 'number', 'createdAt is a number');
  });

  test('TDInspector.snapshot: enumerates entities when TDEngine present', function () {
    const sbox = global.__sandbox;
    // Inject a fake TDEngine
    sbox.TDEngine = {
      module: { /* truthy */ },
      ecs: {
        count: function () { return 2; },
        isValid: function (id) { return id === 1 || id === 2; },
        getPosition: function (id) { return { x: id * 10, y: id * 20 }; },
      },
    };
    try {
      const snap = global.TDInspector.snapshot();
      assertEqual(snap.error, null, 'no error');
      assertEqual(snap.entityCount, 2, 'entity count 2');
      assertEqual(snap.entities.length, 2, 'two entities enumerated');
      assertEqual(snap.entities[0].id, 1, 'first entity id 1');
      assertEqual(snap.entities[0].position.x, 10, 'first entity pos.x 10');
      assertEqual(snap.entities[1].position.y, 40, 'second entity pos.y 40');
    } finally {
      delete sbox.TDEngine;
    }
  });

  test('TDInspector.snapshot: skips invalid IDs', function () {
    const sbox = global.__sandbox;
    sbox.TDEngine = {
      module: {},
      ecs: {
        count: function () { return 5; },
        isValid: function (id) { return id === 2 || id === 4; },
        getPosition: function (id) { return { x: id, y: id }; },
      },
    };
    try {
      const snap = global.TDInspector.snapshot();
      assertEqual(snap.entities.length, 2, 'only valid entities included');
      assertEqual(snap.entities[0].id, 2, 'first valid id 2');
      assertEqual(snap.entities[1].id, 4, 'second valid id 4');
    } finally {
      delete sbox.TDEngine;
    }
  });

  test('TDInspector.mount: returns handle with expected methods', function () {
    const sbox = global.__sandbox;
    const container = sbox.document.createElement('div');
    const handle = global.TDInspector.mount(container, { refreshMs: 50 });
    assert(typeof handle.unmount === 'function', 'has unmount()');
    assert(typeof handle.refresh === 'function', 'has refresh()');
    assert(typeof handle.select === 'function', 'has select()');
    assert(typeof handle.snapshot === 'function', 'has snapshot()');
    assert(typeof handle.refreshRate === 'function', 'has refreshRate()');
    handle.unmount();
  });

  test('TDInspector.mount: refreshRate setter validates input', function () {
    const sbox = global.__sandbox;
    const container = sbox.document.createElement('div');
    const handle = global.TDInspector.mount(container);
    assertEqual(handle.refreshRate(50), 50, '50ms accepted');
    assertEqual(handle.refreshRate(1000), 1000, '1000ms accepted');
    // 10ms is below the 50ms floor — should be ignored, return current value
    assertEqual(handle.refreshRate(10), 1000, '10ms rejected, kept 1000');
    handle.unmount();
  });

  // ===========================================================================
  // TDProfiler
  // ===========================================================================

  test('TDProfiler._createCore: starts empty', function () {
    const core = global.TDProfiler._createCore(120);
    assertEqual(core.history.length, 0, 'no history');
    assertEqual(core.frames, 0, 'no frames');
    assertEqual(core.maxMs, 0, 'maxMs starts at 0');
  });

  test('TDProfiler._recordFrame: updates min/max/sum', function () {
    const core = global.TDProfiler._createCore(10);
    global.TDProfiler._recordFrame(core, 16);
    global.TDProfiler._recordFrame(core, 33);
    global.TDProfiler._recordFrame(core, 8);
    assertEqual(core.frames, 3, '3 frames recorded');
    assertEqual(core.maxMs, 33, 'max is 33');
    assertEqual(core.minMs, 8, 'min is 8');
    assertEqual(core.sumMs, 57, 'sum is 57');
    assertEqual(core.history.length, 3, 'history has 3 entries');
  });

  test('TDProfiler._recordFrame: clamps negative dt to 0', function () {
    const core = global.TDProfiler._createCore(10);
    global.TDProfiler._recordFrame(core, -50);
    assertEqual(core.frameMs, 0, 'negative dt clamped to 0');
    assertEqual(core.maxMs, 0, 'maxMs still 0');
  });

  test('TDProfiler._recordFrame: ring buffer respects historySize', function () {
    const core = global.TDProfiler._createCore(3);
    for (let i = 1; i <= 10; i++) global.TDProfiler._recordFrame(core, i);
    assertEqual(core.history.length, 3, 'history capped at 3');
    // Most recent 3 are 8, 9, 10
    assertEqual(core.history[0].dt, 8, 'oldest kept is 8');
    assertEqual(core.history[2].dt, 10, 'newest kept is 10');
  });

  test('TDProfiler._setCounter + _increment', function () {
    const core = global.TDProfiler._createCore(10);
    global.TDProfiler._setCounter(core, 'drawCalls', 42);
    assertEqual(core.counters.get('drawCalls'), 42, 'drawCalls set to 42');
    global.TDProfiler._increment(core, 'drawCalls', 3);
    assertEqual(core.counters.get('drawCalls'), 45, 'drawCalls incremented by 3');
    global.TDProfiler._increment(core, 'drawCalls');  // default delta=1
    assertEqual(core.counters.get('drawCalls'), 46, 'default increment by 1');
  });

  test('TDProfiler._mark: stores markers, capped at 64', function () {
    const core = global.TDProfiler._createCore(10);
    for (let i = 0; i < 100; i++) global.TDProfiler._mark(core, 'm' + i);
    assertEqual(core.markers.length, 64, 'markers capped at 64');
  });

  test('TDProfiler._snapshotOf: computes avg correctly', function () {
    const core = global.TDProfiler._createCore(10);
    global.TDProfiler._recordFrame(core, 10);
    global.TDProfiler._recordFrame(core, 20);
    global.TDProfiler._recordFrame(core, 30);
    const snap = global.TDProfiler._snapshotOf(core);
    assertEqual(snap.frames, 3, 'snapshot frames 3');
    // avg = 60/3 = 20
    assert(Math.abs(snap.avgMs - 20) < 0.001, 'avg is 20');
    assertEqual(snap.maxMs, 30, 'snapshot maxMs 30');
    assertEqual(snap.minMs, 10, 'snapshot minMs 10');
  });

  test('TDProfiler.mount: returns handle with frame/counter/mark/unmount', function () {
    const sbox = global.__sandbox;
    const container = sbox.document.createElement('div');
    const handle = global.TDProfiler.mount(container, { refreshMs: 50 });
    assert(typeof handle.frame === 'function', 'has frame()');
    assert(typeof handle.counter === 'function', 'has counter()');
    assert(typeof handle.increment === 'function', 'has increment()');
    assert(typeof handle.mark === 'function', 'has mark()');
    assert(typeof handle.reset === 'function', 'has reset()');
    assert(typeof handle.unmount === 'function', 'has unmount()');
    handle.frame(16);
    handle.counter('drawCalls', 5);
    handle.increment('drawCalls');
    const snap = handle.snapshot();
    assertEqual(snap.frameMs, 16, 'snapshot frameMs 16');
    assertEqual(snap.counters.drawCalls, 6, 'drawCalls 6 after increment');
    handle.unmount();
  });

  // ===========================================================================
  // TDErrorBoundary
  // ===========================================================================

  test('TDErrorBoundary.snapshot: starts uninstalled with 0 reports', function () {
    global.TDErrorBoundary.clearReports();
    const s = global.TDErrorBoundary.snapshot();
    assertEqual(s.installed, false, 'not installed initially');
    assertEqual(s.reportCount, 0, '0 reports');
    assertEqual(s.lastReport, null, 'no last report');
  });

  test('TDErrorBoundary.report: stores payload with required fields', function () {
    global.TDErrorBoundary.clearReports();
    const err = new Error('test error');
    err.name = 'TestError';
    const payload = global.TDErrorBoundary.report(err, { scene: 'pong' });
    assert(typeof payload.id === 'string', 'payload has id');
    assert(payload.id.startsWith('err_'), 'payload id starts with err_');
    assertEqual(payload.message, 'test error', 'payload message correct');
    assertEqual(payload.name, 'TestError', 'payload name correct');
    assertEqual(payload.context.scene, 'pong', 'context merged');
    assert(typeof payload.timestamp === 'number', 'timestamp is number');
    assert(typeof payload.stack === 'string', 'stack captured');
    // Snapshot now reflects the stored report
    const s = global.TDErrorBoundary.snapshot();
    assertEqual(s.reportCount, 1, '1 report after report()');
    assertEqual(s.lastReport.id, payload.id, 'lastReport is our payload');
  });

  test('TDErrorBoundary.listReports: returns array', function () {
    global.TDErrorBoundary.clearReports();
    global.TDErrorBoundary.report(new Error('a'));
    global.TDErrorBoundary.report(new Error('b'));
    const list = global.TDErrorBoundary.listReports();
    assert(Array.isArray(list), 'list is array');
    assertEqual(list.length, 2, 'list has 2 entries');
    assertEqual(list[0].message, 'a', 'first report is "a"');
    assertEqual(list[1].message, 'b', 'second report is "b"');
  });

  test('TDErrorBoundary.clearReports: wipes storage', function () {
    global.TDErrorBoundary.report(new Error('x'));
    global.TDErrorBoundary.report(new Error('y'));
    global.TDErrorBoundary.clearReports();
    assertEqual(global.TDErrorBoundary.snapshot().reportCount, 0, '0 reports after clear');
    assertEqual(global.TDErrorBoundary.listReports().length, 0, 'listReports empty');
  });

  test('TDErrorBoundary.install: marks installed, returns handle', function () {
    const sbox = global.__sandbox;
    const handle = global.TDErrorBoundary.install({ showStackTrace: true });
    assertEqual(global.TDErrorBoundary.snapshot().installed, true, 'installed after install()');
    assert(typeof handle.uninstall === 'function', 'handle has uninstall()');
    assert(typeof handle.report === 'function', 'handle has report()');
    handle.uninstall();
    assertEqual(global.TDErrorBoundary.snapshot().installed, false, 'uninstalled after uninstall()');
  });

  test('TDErrorBoundary.install: global onerror is wrapped', function () {
    const sbox = global.__sandbox;
    const prev = sbox.onerror;
    const handle = global.TDErrorBoundary.install();
    assert(sbox.onerror !== prev, 'onerror replaced');
    // Simulate an error: should store a report
    global.TDErrorBoundary.clearReports();
    try {
      sbox.onerror('boom', 'test.js', 1, 2, new Error('boom'));
    } catch (e) { /* swallow */ }
    assertEqual(global.TDErrorBoundary.snapshot().reportCount, 1, '1 report after onerror fire');
    handle.uninstall();
    assertEqual(sbox.onerror, prev, 'onerror restored');
  });

  test('TDErrorBoundary.onReport: subscriber fires on report()', function () {
    global.TDErrorBoundary.clearReports();
    let received = null;
    const cb = function (r) { received = r; };
    global.TDErrorBoundary.onReport(cb);
    global.TDErrorBoundary.report(new Error('subscribed'));
    assert(received !== null, 'subscriber received a report');
    assertEqual(received.message, 'subscribed', 'subscriber got correct message');
  });

  test('TDErrorBoundary._buildPayload: includes UA + URL', function () {
    const payload = global.TDErrorBoundary._buildPayload(new Error('x'), {});
    assert(typeof payload.ua === 'string', 'ua is string');
    assert(typeof payload.url === 'string', 'url is string');
  });

  // ===========================================================================
  // Summary
  // ===========================================================================

  console.log('\n===============================================');
  console.log('  Editor Modules Test Summary');
  console.log('===============================================');
  console.log('  Passed: ' + passed);
  console.log('  Failed: ' + failed);
  if (failures.length) {
    console.log('  Failures:');
    failures.forEach(function (m) { console.log('    - ' + m); });
  }
  console.log('===============================================\n');

  if (failed > 0 && typeof process !== 'undefined') process.exitCode = 1;

})(typeof window !== 'undefined' ? window : this);
