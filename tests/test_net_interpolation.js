'use strict';

// Tests for web/net_interpolation.js
// Run via: node tests/test_net_interpolation.js

const { create } = require('../web/net_interpolation.js');

let passed = 0, failed = 0;
function assert(cond, msg) {
  if (cond) { passed++; }
  else { failed++; console.error(`  FAIL: ${msg}`); }
}
function assertEq(a, b, msg) {
  const ok = JSON.stringify(a) === JSON.stringify(b);
  if (!ok) console.error(`  FAIL: ${msg}\n    expected: ${JSON.stringify(b)}\n    actual:   ${JSON.stringify(a)}`);
  ok ? passed++ : failed++;
}

// ---- Test 1: empty buffer samples to empty -------------------------------
(function test_empty() {
  const interp = create({ tickRate: 20, delay: 100 });
  const s = interp.sample(1000);
  assertEq(s.entities, {}, 'empty buffer samples to {}');
  assertEq(s.seq, -1, 'empty buffer has seq=-1');
})();

// ---- Test 2: single snapshot, sample at exact time -----------------------
(function test_single_snapshot() {
  const interp = create({ tickRate: 20, delay: 0 });
  interp.pushSnapshot({ seq: 1, time: 1000, entities: { 1: { x: 10, y: 20 } } });
  const s = interp.sample(1000);
  assertEq(s.entities, { 1: { x: 10, y: 20 } }, 'single snapshot at exact time');
})();

// ---- Test 3: interpolation between two snapshots -------------------------
(function test_interpolation() {
  const interp = create({ tickRate: 20, delay: 0 });
  interp.pushSnapshot({ seq: 1, time: 1000, entities: { 1: { x: 0, y: 0 } } });
  interp.pushSnapshot({ seq: 2, time: 1100, entities: { 1: { x: 100, y: 0 } } });
  const s = interp.sample(1050); // halfway between 1000 and 1100
  assert(Math.abs(s.entities[1].x - 50) < 0.01, 'x should be ~50 at halfway');
  assert(s.interpolated === true, 'should be interpolated');
})();

// ---- Test 4: extrapolation beyond newest snapshot ------------------------
(function test_extrapolation() {
  const interp = create({ tickRate: 20, delay: 0 });
  interp.pushSnapshot({ seq: 1, time: 1000, entities: { 1: { x: 0, y: 0, vx: 100, vy: 0 } } });
  const s = interp.sample(1100); // 100ms past, vx=100/s -> x should be +10
  assert(Math.abs(s.entities[1].x - 10) < 0.1, 'extrapolated x should be ~10');
  assert(s.extrapolated === true, 'should be extrapolated');
})();

// ---- Test 5: delta application -------------------------------------------
(function test_delta() {
  const interp = create({ tickRate: 20, delay: 0 });
  interp.pushSnapshot({ seq: 1, time: 1000, entities: {
    1: { x: 0, y: 0, vx: 0, vy: 0 },
    2: { x: 50, y: 50, vx: 0, vy: 0 },
  }});
  interp.pushSnapshot({
    seq: 2, time: 1050, base: 1,
    entities: { 1: { x: 5 } },  // only entity 1's x changed
    removed: [2],                // entity 2 was removed
  });
  const s = interp.sample(1050);
  assertEq(s.entities[1], { x: 5, y: 0, vx: 0, vy: 0 }, 'delta merges onto base');
  assert(!(2 in s.entities), 'removed entity is gone');
  assert(interp.stats.deltasApplied === 1, 'delta was applied');
})();

// ---- Test 6: dropped delta (missing base) --------------------------------
(function test_dropped_delta() {
  const interp = create({ tickRate: 20, delay: 0 });
  interp.pushSnapshot({ seq: 5, time: 1000, entities: { 1: { x: 0 } } });
  // Delta with base=3, which we don't have.
  interp.pushSnapshot({ seq: 6, time: 1050, base: 3, entities: { 1: { x: 99 } } });
  assert(interp.stats.dropped === 1, 'delta with missing base is dropped');
  // The buffer should still only have seq 5.
  assert(interp.newestSeq === 5, 'newestSeq is unchanged after drop');
})();

// ---- Test 7: buffer pruning ----------------------------------------------
(function test_pruning() {
  const interp = create({ tickRate: 20, delay: 0 });
  for (let i = 0; i < 40; i++) {
    interp.pushSnapshot({ seq: i, time: 1000 + i * 50, entities: { 1: { x: i } } });
  }
  assert(interp.bufferSize <= 32, 'buffer should be pruned to <= 32');
})();

// ---- Test 8: non-numeric fields snap to newest ---------------------------
(function test_snap_non_numeric() {
  const interp = create({ tickRate: 20, delay: 0 });
  interp.pushSnapshot({ seq: 1, time: 1000, entities: { 1: { x: 0, anim: 'idle' } } });
  interp.pushSnapshot({ seq: 2, time: 1100, entities: { 1: { x: 100, anim: 'run' } } });
  const s = interp.sample(1050);
  assert(s.entities[1].anim === 'run', 'non-numeric field snaps to newest');
})();

// ---- Test 9: stats tracking ----------------------------------------------
(function test_stats() {
  const interp = create({ tickRate: 20, delay: 0 });
  interp.pushSnapshot({ seq: 1, time: 1000, entities: { 1: { x: 0 } } });
  interp.pushSnapshot({ seq: 2, time: 1050, base: 1, entities: { 1: { x: 5 } } });
  interp.sample(1050);
  assert(interp.stats.pushed === 2, 'pushed=2');
  assert(interp.stats.deltasApplied === 1, 'deltasApplied=1');
})();

// ---- Summary -------------------------------------------------------------
console.log(`\nnet_interpolation: ${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
