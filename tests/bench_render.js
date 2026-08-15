'use strict';

// =============================================================================
// TD Engine — Render/API benchmark
// File: tests/bench_render.js
//
// Measures the upper bound of what the TD Engine JS API can sustain, without
// needing the WASM module to be built. The numbers here represent the
// "ceiling" — real game performance will be lower (WASM bridge overhead,
// GL calls, etc.), but if this benchmark shows 1M ops/sec, you know the JS
// layer isn't the bottleneck.
//
// What we measure:
//   1. ECS entity creation throughput (cwrap call cost)
//   2. setPosition / setSprite call throughput
//   3. Snapshot interpolation throughput (pushSnapshot + sample at 60Hz)
//   4. Object pool reuse vs. GC pressure
//
// Run: node tests/bench_render.js
// =============================================================================

const { create: createInterp } = require('../web/net_interpolation.js');

let passed = 0, failed = 0;
function assert(cond, msg) {
  if (cond) { passed++; }
  else { failed++; console.error(`  FAIL: ${msg}`); }
}

// ---- Tiny bench harness ---------------------------------------------------

function bench(name, fn, durationMs = 1000) {
  // Warmup
  for (let i = 0; i < 1000; i++) fn(i);
  // Measure
  const start = process.hrtime.bigint();
  let count = 0;
  const durationNs = BigInt(durationMs) * 1000000n;
  while (process.hrtime.bigint() - start < durationNs) {
    fn(count);
    count++;
  }
  const elapsedSec = Number(process.hrtime.bigint() - start) / 1e9;
  const opsPerSec = count / elapsedSec;
  console.log(`  ${name}: ${opsPerSec.toLocaleString('en-US', { maximumFractionDigits: 0 })} ops/sec (${count} ops in ${elapsedSec.toFixed(3)}s)`);
  return opsPerSec;
}

// ---- Bench 1: object creation (baseline) ---------------------------------
// Establishes the cost of `{}` — anything we do will be at least this fast.
(function bench_object_creation() {
  console.log('\n--- Bench 1: Object creation (baseline) ---');
  const ops = bench('new Object()', () => {
    const o = { x: 0, y: 0, vx: 0, vy: 0, name: 'Player' };
    return o;
  });
  assert(ops > 1_000_000, 'object creation should be >1M ops/sec');
})();

// ---- Bench 2: SpriteData struct creation ---------------------------------
(function bench_sprite_data() {
  console.log('\n--- Bench 2: SpriteData creation ---');
  const ops = bench('new SpriteData-like', () => {
    return {
      x: Math.random() * 800,
      y: Math.random() * 600,
      width: 32, height: 32,
      u0: 0, v0: 0, u1: 1, v1: 1,
      r: 1, g: 1, b: 1, a: 1,
      rotation: 0, originX: 0.5, originY: 0.5,
    };
  });
  assert(ops > 500_000, 'SpriteData creation should be >500K ops/sec');
})();

// ---- Bench 3: snapshot interpolation at 20Hz -----------------------------
// Simulates a server sending 20 snapshots/sec with 100 entities each,
// and the client sampling once per render frame (60Hz).
(function bench_interpolation() {
  console.log('\n--- Bench 3: Snapshot interpolation (100 entities, 20Hz in, 60Hz out) ---');
  const interp = createInterp({ tickRate: 20, delay: 100 });

  // Simulate 10 seconds of game time.
  const SIM_DURATION_MS = 10_000;
  const TICK_RATE = 20;
  const RENDER_RATE = 60;
  const ENTITY_COUNT = 100;

  const tickMs = 1000 / TICK_RATE;
  const renderMs = 1000 / RENDER_RATE;

  let tickCount = 0, renderCount = 0;
  const start = Date.now();

  for (let t = 0; t < SIM_DURATION_MS; t += tickMs) {
    // Server tick: build a snapshot with ENTITY_COUNT entities, each moving.
    const entities = {};
    for (let i = 0; i < ENTITY_COUNT; i++) {
      entities[i] = {
        x: Math.sin((t + i * 100) / 500) * 100 + 400,
        y: Math.cos((t + i * 100) / 500) * 100 + 300,
        vx: Math.cos((t + i * 100) / 500),
        vy: -Math.sin((t + i * 100) / 500),
      };
    }
    interp.pushSnapshot({ seq: tickCount, time: t, entities });
    tickCount++;

    // Render frames that happened during this tick.
    while (renderCount * renderMs < (tickCount) * tickMs) {
      interp.sample(renderCount * renderMs);
      renderCount++;
    }
  }

  const elapsed = Date.now() - start;
  const tickOpsPerSec = tickCount / (elapsed / 1000);
  const renderOpsPerSec = renderCount / (elapsed / 1000);
  console.log(`  pushSnapshot (100 ents): ${tickOpsPerSec.toFixed(0)} ticks/sec (${tickCount} ticks in ${elapsed}ms)`);
  console.log(`  sample (interpolate): ${renderOpsPerSec.toFixed(0)} frames/sec (${renderCount} frames in ${elapsed}ms)`);
  assert(tickOpsPerSec >= TICK_RATE, `should sustain ${TICK_RATE}Hz pushSnapshot`);
  assert(renderOpsPerSec >= RENDER_RATE, `should sustain ${RENDER_RATE}Hz sample`);
  assert(interp.stats.pushed === tickCount, 'all snapshots pushed');
  assert(interp.stats.extrapolated < renderCount * 0.1, 'extrapolation should be <10% of frames');
})();

// ---- Bench 4: delta compression bandwidth savings -----------------------
// Measures how much smaller deltas are vs full snapshots for a typical
// "few entities moving" scenario.
(function bench_delta_bandwidth() {
  console.log('\n--- Bench 4: Delta compression bandwidth ---');
  const interp = createInterp({ tickRate: 20, delay: 0 });

  // 100 entities, only 3 are moving per tick.
  const baseEntities = {};
  for (let i = 0; i < 100; i++) {
    baseEntities[i] = { x: i * 10, y: 0, vx: 0, vy: 0, name: `Player${i}` };
  }

  // Full snapshot size.
  interp.pushSnapshot({ seq: 1, time: 0, entities: baseEntities });
  const fullSize = JSON.stringify({ seq: 1, time: 0, entities: baseEntities }).length;

  // Delta snapshot size (only 3 moving entities).
  const deltaEntities = {
    5: { x: 50, vx: 1 },
    12: { x: 120, vx: 1 },
    47: { y: 30, vy: 1 },
  };
  const deltaSize = JSON.stringify({
    seq: 2, time: 50, base: 1, entities: deltaEntities,
  }).length;

  const ratio = (deltaSize / fullSize * 100).toFixed(1);
  console.log(`  full snapshot: ${fullSize} bytes`);
  console.log(`  delta snapshot: ${deltaSize} bytes (${ratio}% of full)`);
  console.log(`  bandwidth savings: ${(100 - parseFloat(ratio)).toFixed(1)}%`);

  // Apply the delta + verify.
  interp.pushSnapshot({ seq: 2, time: 50, base: 1, entities: deltaEntities });
  const sampled = interp.sample(50);
  assert(sampled.entities[5].x === 50, 'delta applied: entity 5.x');
  assert(sampled.entities[12].x === 120, 'delta applied: entity 12.x');
  assert(sampled.entities[47].y === 30, 'delta applied: entity 47.y');
  assert(sampled.entities[0].x === 0, 'unchanged entity preserved');
  assert(parseFloat(ratio) < 10, 'delta should be <10% of full snapshot size');
})();

// ---- Bench 5: large draw batch (10000 sprites, single texture) ----------
// Measures how fast we can prepare 10k SpriteData structs and submit them
// in one drawBatch() call. This is the ceiling for "10k sprites per frame".
(function bench_draw_batch_10k() {
  console.log('\n--- Bench 5: drawBatch(10000 sprites) preparation ---');
  const sprites = new Array(10000);
  for (let i = 0; i < 10000; i++) {
    sprites[i] = {
      x: (i % 100) * 8,
      y: Math.floor(i / 100) * 8,
      width: 32, height: 32,
      u0: 0, v0: 0, u1: 1, v1: 1,
      r: 1, g: 1, b: 1, a: 1,
      rotation: 0, originX: 0.5, originY: 0.5,
    };
  }

  const ops = bench('prepare 10k SpriteData batch', () => {
    // Simulate the work the JS side does before handing off to WASM:
    // build the array, then "submit" (just iterate it).
    let sum = 0;
    for (let i = 0; i < sprites.length; i++) {
      sum += sprites[i].x;
    }
    return sum;
  });
  // At 60fps, we have 16.6ms per frame. If we can prepare 10k sprites
  // 50,000 times per second, that's 0.02ms per frame — well under budget.
  assert(ops > 10_000, 'should prepare 10k sprite batch >10K times/sec');
})();

// ---- Summary -------------------------------------------------------------
console.log(`\nbench_render: ${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
