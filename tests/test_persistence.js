// =============================================================================
// TD Engine — Persistence Layer Tests
// File: tests/test_persistence.js
//
// Tests for web/persistence.js (TDPersistence):
//   - registerSerializer / unregisterSerializer
//   - save / load round-trip
//   - list / delete
//   - exportJson / importJson
//   - autosave / stopAllAutosaves
//   - clearAll
//   - error handling (invalid slot, parse failure, serializer exceptions)
//
// Runs in Node via vm sandbox with fake localStorage, OR in a browser.
//   node tests/test_persistence.js
// =============================================================================

(function (global) {
  'use strict';

  if (typeof window === 'undefined' && typeof global.TDPersistence === 'undefined') {
    const fs = require('fs');
    const path = require('path');
    const vm = require('vm');

    const sandbox = {
      console: console,
      setTimeout: setTimeout,
      setInterval: function (fn, ms) { return ++_timerId; },
      clearInterval: function () {},
      Date: Date,
      Math: Math,
      JSON: JSON,
      navigator: { userAgent: 'node-test' },
      location: { href: 'http://localhost/test' },
      localStorage: {
        _s: {},
        getItem: function (k) { return this._s[k] || null; },
        setItem: function (k, v) { this._s[k] = String(v); },
        removeItem: function (k) { delete this._s[k]; },
        get length() { return Object.keys(this._s).length; },
        key: function (i) { return Object.keys(this._s)[i] || null; },
      },
    };
    var _timerId = 0;
    sandbox.window = sandbox;
    sandbox.globalThis = sandbox;
    vm.createContext(sandbox);

    const webDir = path.resolve(__dirname, '..', 'web');
    const src = fs.readFileSync(path.join(webDir, 'persistence.js'), 'utf8');
    vm.runInContext(src, sandbox, { filename: 'persistence.js' });

    global.TDPersistence = sandbox.TDPersistence;
    global.__sandbox = sandbox;
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
  function test(name, fn) {
    try { fn(); console.log('PASS:', name); }
    catch (e) { failed++; failures.push(name + ': ' + e.message); console.error('FAIL:', name, e); }
  }

  // ---- helpers --------------------------------------------------------------

  function freshRegistry() {
    // Wipe serializers between tests by unregistering everything
    const snap = global.TDPersistence.snapshot();
    snap.serializers.forEach(function (n) { global.TDPersistence.unregisterSerializer(n); });
    global.TDPersistence.clearAll();
  }

  // ===========================================================================
  // registerSerializer / unregisterSerializer
  // ===========================================================================

  test('registerSerializer: accepts valid name + functions', function () {
    freshRegistry();
    const ok = global.TDPersistence.registerSerializer('score',
      function () { return { score: 100 }; },
      function (data) { /* no-op */ }
    );
    assertEqual(ok, true, 'returns true on success');
    assert(global.TDPersistence.snapshot().serializers.includes('score'), 'serializer registered');
  });

  test('registerSerializer: rejects invalid inputs', function () {
    freshRegistry();
    assertEqual(global.TDPersistence.registerSerializer('', function(){}, function(){}), false, 'empty name rejected');
    assertEqual(global.TDPersistence.registerSerializer('x', null, function(){}), false, 'null serialize rejected');
    assertEqual(global.TDPersistence.registerSerializer('x', function(){}, null), false, 'null deserialize rejected');
    assertEqual(global.TDPersistence.registerSerializer('x', 'notfn', function(){}), false, 'non-fn serialize rejected');
  });

  test('registerSerializer: re-registration replaces existing', function () {
    freshRegistry();
    global.TDPersistence.registerSerializer('s', function () { return 1; }, function () {});
    global.TDPersistence.registerSerializer('s', function () { return 2; }, function () {});
    const snap = global.TDPersistence.snapshot();
    assertEqual(snap.serializers.length, 1, 'only one serializer after re-register');
    // Save and check the second serializer's output is used
    const env = global.TDPersistence.save('test');
    assertEqual(env.data.s, 2, 'second serializer wins');
  });

  test('unregisterSerializer: removes by name', function () {
    freshRegistry();
    global.TDPersistence.registerSerializer('a', function(){return 1;}, function(){});
    global.TDPersistence.registerSerializer('b', function(){return 2;}, function(){});
    assertEqual(global.TDPersistence.unregisterSerializer('a'), true, 'unregister a returns true');
    assertEqual(global.TDPersistence.unregisterSerializer('missing'), false, 'unregister missing returns false');
    assertEqual(global.TDPersistence.snapshot().serializers.length, 1, 'one serializer left');
  });

  // ===========================================================================
  // save / load round-trip
  // ===========================================================================

  test('save: writes envelope to localStorage with correct shape', function () {
    freshRegistry();
    global.TDPersistence.registerSerializer('score',
      function () { return { value: 42 }; },
      function () {}
    );
    const env = global.TDPersistence.save('slot1');
    assert(env !== null, 'save returns envelope');
    assertEqual(env.version, 1, 'envelope version 1');
    assertEqual(env.slot, 'slot1', 'envelope slot name correct');
    assertEqual(env.data.score.value, 42, 'serializer data included');
    assert(typeof env.savedAt === 'number', 'savedAt is number');
    assert(typeof env.engine === 'string', 'engine version is string');
  });

  test('load: restores data via deserializer', function () {
    freshRegistry();
    let restored = null;
    global.TDPersistence.registerSerializer('hp',
      function () { return { current: 75, max: 100 }; },
      function (data) { restored = data; }
    );
    global.TDPersistence.save('game1');
    // Reset state
    restored = null;
    const result = global.TDPersistence.load('game1');
    assertEqual(result.ok, true, 'load ok');
    assertEqual(result.restored.length, 1, 'one serializer restored');
    assertEqual(result.restored[0], 'hp', 'restored serializer name hp');
    assertEqual(restored.current, 75, 'hp.current restored to 75');
    assertEqual(restored.max, 100, 'hp.max restored to 100');
  });

  test('load: returns missing for serializers not in envelope', function () {
    freshRegistry();
    global.TDPersistence.registerSerializer('old', function(){return 1;}, function(){});
    global.TDPersistence.save('s1');
    // Add a new serializer that wasn't present at save time
    global.TDPersistence.registerSerializer('new', function(){return 2;}, function(){});
    const result = global.TDPersistence.load('s1');
    assert(result.restored.includes('old'), 'old restored');
    assert(result.missing.includes('new'), 'new missing from envelope');
  });

  test('load: invalid slot name returns error', function () {
    freshRegistry();
    const r1 = global.TDPersistence.load('');
    assertEqual(r1.ok, false, 'empty slot name fails');
    assertEqual(r1.error, 'invalid-slot-name', 'correct error code');
  });

  test('load: missing slot returns error', function () {
    freshRegistry();
    const r = global.TDPersistence.load('does-not-exist');
    assertEqual(r.ok, false, 'not-found fails');
    assertEqual(r.error, 'slot-not-found', 'correct error code');
  });

  test('save: serializer exception does not fail entire save', function () {
    freshRegistry();
    global.TDPersistence.registerSerializer('good', function(){return {x:1};}, function(){});
    global.TDPersistence.registerSerializer('bad', function(){ throw new Error('boom'); }, function(){});
    const env = global.TDPersistence.save('s');
    assert(env !== null, 'save still succeeds');
    assertEqual(env.data.good.x, 1, 'good serializer data saved');
    assert(!('bad' in env.data), 'bad serializer data omitted');
  });

  test('load: deserializer exception is reported as missing', function () {
    freshRegistry();
    global.TDPersistence.registerSerializer('crashy',
      function(){return {x:1};},
      function(){ throw new Error('deserialize boom'); }
    );
    global.TDPersistence.save('s');
    const result = global.TDPersistence.load('s');
    assertEqual(result.ok, true, 'load ok (others may succeed)');
    assert(result.missing.includes('crashy'), 'crashy in missing');
    assert(!result.restored.includes('crashy'), 'crashy not in restored');
  });

  test('save: serializer returning null is omitted from data', function () {
    freshRegistry();
    global.TDPersistence.registerSerializer('real', function(){return 1;}, function(){});
    global.TDPersistence.registerSerializer('nullish', function(){return null;}, function(){});
    const env = global.TDPersistence.save('s');
    assert('real' in env.data, 'real serializer in data');
    assert(!('nullish' in env.data), 'null-returning serializer omitted');
  });

  // ===========================================================================
  // list / delete
  // ===========================================================================

  test('list: returns empty array when no saves', function () {
    freshRegistry();
    const l = global.TDPersistence.list();
    assertEqual(l.length, 0, 'no saves initially');
  });

  test('list: returns saves sorted by timestamp desc', function () {
    freshRegistry();
    global.TDPersistence.registerSerializer('s', function(){return 1;}, function(){});
    global.TDPersistence.save('a');
    // Force a later timestamp
    const orig = Date.now;
    Date.now = function () { return orig() + 1000; };
    global.TDPersistence.save('b');
    Date.now = function () { return orig() + 2000; };
    global.TDPersistence.save('c');
    Date.now = orig;
    const l = global.TDPersistence.list();
    assertEqual(l.length, 3, 'three saves listed');
    assertEqual(l[0].name, 'c', 'newest first');
    assertEqual(l[2].name, 'a', 'oldest last');
    assert(typeof l[0].sizeBytes === 'number', 'sizeBytes present');
    assert(Array.isArray(l[0].slotNames), 'slotNames is array');
  });

  test('delete: removes save slot', function () {
    freshRegistry();
    global.TDPersistence.registerSerializer('s', function(){return 1;}, function(){});
    global.TDPersistence.save('toDelete');
    assertEqual(global.TDPersistence.list().length, 1, 'one save before delete');
    assertEqual(global.TDPersistence.delete('toDelete'), true, 'delete returns true');
    assertEqual(global.TDPersistence.list().length, 0, 'zero saves after delete');
    assertEqual(global.TDPersistence.delete('toDelete'), false, 'second delete returns false');
  });

  // ===========================================================================
  // exportJson / importJson
  // ===========================================================================

  test('exportJson: returns pretty JSON string', function () {
    freshRegistry();
    global.TDPersistence.registerSerializer('s', function(){return {x:1};}, function(){});
    global.TDPersistence.save('e1');
    const json = global.TDPersistence.exportJson('e1');
    assert(typeof json === 'string', 'returns string');
    assert(json.indexOf('"x": 1') >= 0, 'pretty-printed (has "x": 1)');
    assert(json.indexOf('\n') >= 0, 'multi-line (has newline)');
  });

  test('exportJson: returns null for missing slot', function () {
    freshRegistry();
    assertEqual(global.TDPersistence.exportJson('nope'), null, 'null for missing');
  });

  test('importJson: round-trips through exportJson', function () {
    freshRegistry();
    global.TDPersistence.registerSerializer('s', function(){return {x:1,y:2};}, function(){});
    global.TDPersistence.save('orig');
    const json = global.TDPersistence.exportJson('orig');
    global.TDPersistence.delete('orig');
    assertEqual(global.TDPersistence.list().length, 0, 'gone after delete');
    const ok = global.TDPersistence.importJson(json, 'imported');
    assertEqual(ok, true, 'import returns true');
    assertEqual(global.TDPersistence.list().length, 1, 'one save after import');
    assertEqual(global.TDPersistence.list()[0].name, 'imported', 'imported under new name');
    // Load it back
    let restored = null;
    global.TDPersistence.registerSerializer('s', function(){return null;}, function(d){restored=d;});
    const r = global.TDPersistence.load('imported');
    assertEqual(r.ok, true, 'load imported ok');
    assertEqual(restored.x, 1, 'restored x=1');
    assertEqual(restored.y, 2, 'restored y=2');
  });

  test('importJson: rejects invalid JSON', function () {
    freshRegistry();
    assertEqual(global.TDPersistence.importJson('not json', 'x'), false, 'invalid JSON rejected');
    assertEqual(global.TDPersistence.importJson('{}', 'x'), false, 'empty object rejected (no version/data)');
    assertEqual(global.TDPersistence.importJson('{"version":2,"data":{}}', 'x'), false, 'wrong version rejected');
    assertEqual(global.TDPersistence.importJson('{"version":1}', 'x'), false, 'missing data rejected');
  });

  test('importJson: rejects non-string inputs', function () {
    freshRegistry();
    assertEqual(global.TDPersistence.importJson(null, 'x'), false, 'null rejected');
    assertEqual(global.TDPersistence.importJson({}, 'x'), false, 'object rejected');
    assertEqual(global.TDPersistence.importJson('{"version":1,"data":{}}', ''), false, 'empty slot name rejected');
  });

  // ===========================================================================
  // autosave
  // ===========================================================================

  test('autosave: returns handle with stop()', function () {
    freshRegistry();
    global.TDPersistence.registerSerializer('s', function(){return 1;}, function(){});
    const handle = global.TDPersistence.autosave('auto1', 1000);
    assert(handle !== null, 'returns handle');
    assertEqual(handle.slotName, 'auto1', 'handle has slotName');
    assertEqual(handle.intervalMs, 1000, 'handle has intervalMs');
    assert(typeof handle.stop === 'function', 'handle has stop()');
    handle.stop();
  });

  test('autosave: floors interval to 1000ms minimum', function () {
    freshRegistry();
    global.TDPersistence.registerSerializer('s', function(){return 1;}, function(){});
    const handle = global.TDPersistence.autosave('auto2', 50);  // too small
    assertEqual(handle.intervalMs, 5000, '50ms floored to 5000ms');
    handle.stop();
  });

  test('autosave: rejects invalid slot name', function () {
    freshRegistry();
    assertEqual(global.TDPersistence.autosave('', 1000), null, 'empty slot rejected');
    assertEqual(global.TDPersistence.autosave(null, 1000), null, 'null slot rejected');
  });

  test('stopAllAutosaves: cancels all active timers', function () {
    freshRegistry();
    global.TDPersistence.registerSerializer('s', function(){return 1;}, function(){});
    const h1 = global.TDPersistence.autosave('a1', 5000);
    const h2 = global.TDPersistence.autosave('a2', 5000);
    global.TDPersistence.stopAllAutosaves();
    assertEqual(global.TDPersistence.snapshot().autosaves.length, 0, 'no active autosaves');
  });

  // ===========================================================================
  // clearAll
  // ===========================================================================

  test('clearAll: wipes all saves, returns count', function () {
    freshRegistry();
    global.TDPersistence.registerSerializer('s', function(){return 1;}, function(){});
    global.TDPersistence.save('a');
    global.TDPersistence.save('b');
    global.TDPersistence.save('c');
    const count = global.TDPersistence.clearAll();
    assertEqual(count, 3, 'cleared 3 saves');
    assertEqual(global.TDPersistence.list().length, 0, 'list empty after clearAll');
  });

  // ===========================================================================
  // snapshot (introspection)
  // ===========================================================================

  test('snapshot: returns registry + slots + autosaves', function () {
    freshRegistry();
    global.TDPersistence.registerSerializer('s1', function(){return 1;}, function(){});
    global.TDPersistence.registerSerializer('s2', function(){return 2;}, function(){});
    global.TDPersistence.save('slot-x');
    const snap = global.TDPersistence.snapshot();
    assert(Array.isArray(snap.serializers), 'serializers is array');
    assertEqual(snap.serializers.length, 2, '2 serializers registered');
    assert(Array.isArray(snap.slots), 'slots is array');
    assertEqual(snap.slots.length, 1, '1 save slot');
    assertEqual(snap.slots[0].name, 'slot-x', 'slot name correct');
    assert(Array.isArray(snap.autosaves), 'autosaves is array');
  });

  // ===========================================================================
  // Summary
  // ===========================================================================

  console.log('\n===============================================');
  console.log('  Persistence Module Test Summary');
  console.log('===============================================');
  console.log('  Passed: ' + passed);
  console.log('  Failed: ' + failed);
  if (failures.length) {
    console.log('  Failures:');
    failures.forEach(function (m) { console.log('    - ' + m); });
  }
  console.log('===============================================\n');

  if (typeof process !== 'undefined') process.exit(failed > 0 ? 1 : 0);

})(typeof window !== 'undefined' ? window : this);
