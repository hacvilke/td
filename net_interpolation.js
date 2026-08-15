// =============================================================================
// TD Engine — Snapshot Interpolation (client-side)
// File: web/net_interpolation.js
//
// Smooths the visual position of remote entities despite network jitter.
// This is the standard technique used by every authoritative-server browser
// game (diep.io, agar.io, .io games generally) — see:
//   https://www.gabrielgambetta.com/client-server-game-architecture.html
//
// THE PROBLEM
//   The server sends a snapshot every 50ms (20 Hz). The client renders at
//   60Hz. If you snap to the latest snapshot, the remote players "stutter"
//   because each visible frame is from a different network packet. If you
//   average them, you reintroduce lag.
//
// THE FIX
//   Keep a short buffer of the last N snapshots. Render entities ONE snapshot
//   in the past, interpolating between snapshot[i-1] and snapshot[i]. This
//   adds exactly one tick of latency (~50ms) but produces perfectly smooth
//   motion. If a snapshot is missing (packet loss), extrapolate briefly then
//   snap when the next arrives.
//
// USAGE
//   const interp = TDNet.Interpolation.create({ tickRate: 20, delay: 100 });
//   interp.pushSnapshot({ seq: 42, time: now, entities: {
//     1: { x: 100, y: 200, vx: 0, vy: 0 },
//     2: { x: 50, y: 75, vx: 1, vy: 0 },
//   }});
//
//   // In your render loop:
//   const state = interp.sample(now);
//   for (const [id, e] of Object.entries(state.entities)) {
//     // Render entity `id` at (e.x, e.y) — already interpolated.
//   }
//
// DELTA COMPRESSION
//   The server can send DELTAS instead of full snapshots: only entities that
//   changed since the last acknowledged snapshot. pushSnapshot() accepts
//   { base: seq, entities: {...}, removed: [ids] } — if `base` is set, the
//   delta is applied on top of the cached snapshot at seq==base. This cuts
//   bandwidth ~10x in a room with 50 players but only 3 are moving.
// =============================================================================

(function (global) {
  'use strict';

  // ---------------------------------------------------------------------------
  // Constants
  // ---------------------------------------------------------------------------

  const DEFAULT_TICK_RATE = 20;       // server sends 20 snapshots/sec
  const DEFAULT_DELAY_MS  = 100;      // render 100ms behind the latest snapshot
  const MAX_BUFFER_LEN    = 32;       // keep last 32 snapshots (~1.6s at 20Hz)
  const EXTRAPOLATE_MAX_MS = 200;     // cap extrapolation to 200ms
  const MAX_ENTITY_BYTES   = 1024;    // cap per-entity state size

  // ---------------------------------------------------------------------------
  // Create an interpolation buffer
  // ---------------------------------------------------------------------------

  function create(opts) {
    opts = opts || {};
    const tickRate = opts.tickRate || DEFAULT_TICK_RATE;
    const delayMs  = opts.delay  != null ? opts.delay  : DEFAULT_DELAY_MS;

    // Ring buffer of snapshots, keyed by sequence number.
    const buffer = new Map();         // seq -> snapshot
    let newestSeq = -1;
    let baseSnapshots = new Map();    // seq -> full snapshot (for delta application)

    // Stats for the inspector / profiler.
    const stats = {
      pushed: 0,
      deltasApplied: 0,
      dropped: 0,
      extrapolated: 0,
      lastSampleSeq: -1,
    };

    // ---- Push a snapshot (full or delta) -----------------------------------
    //
    // Snapshot shape:
    //   { seq: 42, time: 12345.6, entities: { id: {x,y,...}, ... },
    //     removed: [id, ...]  // optional, entities to delete
    //     base: 41             // optional, if this is a delta on top of seq 41
    //   }
    //
    function pushSnapshot(snap) {
      if (!snap || typeof snap.seq !== 'number') return false;
      stats.pushed++;

      // If this is a delta, apply it on top of the base snapshot.
      let fullSnap;
      if (snap.base != null) {
        const baseFull = baseSnapshots.get(snap.base);
        if (!baseFull) {
          // We don't have the base; drop the delta. The server should
          // periodically send full snapshots to resync.
          stats.dropped++;
          return false;
        }
        fullSnap = applyDelta(baseFull, snap);
        stats.deltasApplied++;
      } else {
        fullSnap = {
          seq: snap.seq,
          time: snap.time,
          entities: Object.assign({}, snap.entities || {}),
        };
        // Track as a base for future deltas.
        baseSnapshots.set(snap.seq, fullSnap);
        // Prune base cache — keep last 10 to allow out-of-order deltas.
        if (baseSnapshots.size > 10) {
          const oldest = Array.from(baseSnapshots.keys()).sort((a, b) => a - b)[0];
          baseSnapshots.delete(oldest);
        }
      }

      // Apply removals.
      if (Array.isArray(snap.removed)) {
        for (const id of snap.removed) {
          delete fullSnap.entities[id];
        }
      }

      buffer.set(snap.seq, fullSnap);
      if (snap.seq > newestSeq) newestSeq = snap.seq;

      // Prune buffer.
      if (buffer.size > MAX_BUFFER_LEN) {
        const seqs = Array.from(buffer.keys()).sort((a, b) => a - b);
        for (let i = 0; i < seqs.length - MAX_BUFFER_LEN; i++) {
          buffer.delete(seqs[i]);
        }
      }
      return true;
    }

    // Apply a delta onto a base snapshot, returns a NEW full snapshot.
    function applyDelta(base, delta) {
      const merged = {
        seq: delta.seq,
        time: delta.time != null ? delta.time : base.time,
        entities: Object.assign({}, base.entities),
      };
      if (delta.entities) {
        for (const [id, fields] of Object.entries(delta.entities)) {
          if (fields === null) {
            delete merged.entities[id];
          } else {
            // Per-field delta: merge individual fields rather than replace.
            const existing = merged.entities[id] || {};
            merged.entities[id] = Object.assign({}, existing, fields);
          }
        }
      }
      return merged;
    }

    // ---- Sample the interpolated state at a given time ---------------------
    //
    // `now` is in the same timebase as snapshot.time (server time, ideally).
    // Returns { entities, seq, interpolated, extrapolated }.
    //
    function sample(now) {
      if (buffer.size === 0) {
        return { entities: {}, seq: -1, interpolated: false, extrapolated: false };
      }

      const targetTime = now - delayMs;

      // Find the two snapshots to interpolate between: snapBefore (older)
      // and snapAfter (newer) such that snapBefore.time <= targetTime < snapAfter.time.
      const sorted = Array.from(buffer.values()).sort((a, b) => a.time - b.time);
      let before = null;
      let after = null;
      for (let i = 0; i < sorted.length; i++) {
        if (sorted[i].time <= targetTime) {
          before = sorted[i];
        } else {
          after = sorted[i];
          break;
        }
      }

      if (!before && !after) {
        return { entities: {}, seq: -1, interpolated: false, extrapolated: false };
      }

      // Case 1: targetTime is before the oldest snapshot — snap to oldest.
      if (!before) {
        stats.lastSampleSeq = after.seq;
        return { entities: after.entities, seq: after.seq, interpolated: false, extrapolated: false };
      }

      // Case 2: targetTime is after the newest snapshot — extrapolate briefly.
      if (!after) {
        const ageMs = targetTime - before.time;
        if (ageMs > EXTRAPOLATE_MAX_MS) {
          // Too old — just snap to the latest.
          stats.lastSampleSeq = before.seq;
          return { entities: before.entities, seq: before.seq, interpolated: false, extrapolated: false };
        }
        stats.extrapolated++;
        stats.lastSampleSeq = before.seq;
        const ext = extrapolate(before, ageMs);
        return { entities: ext, seq: before.seq, interpolated: false, extrapolated: true };
      }

      // Case 3: normal interpolation between before and after.
      const span = after.time - before.time;
      if (span <= 0) {
        stats.lastSampleSeq = after.seq;
        return { entities: after.entities, seq: after.seq, interpolated: false, extrapolated: false };
      }
      const t = (targetTime - before.time) / span;
      stats.lastSampleSeq = before.seq + Math.floor(t * (after.seq - before.seq + 1));
      return { entities: lerpEntities(before.entities, after.entities, t),
               seq: before.seq, interpolated: true, extrapolated: false };
    }

    function lerpEntities(a, b, t) {
      const out = {};
      // Entities present in both: lerp numeric fields, snap non-numeric.
      for (const [id, ea] of Object.entries(a)) {
        const eb = b[id];
        if (!eb) {
          // Entity was in `before` but not `after` — keep it (may be removed
          // next snapshot; better to over-render than flicker).
          out[id] = ea;
          continue;
        }
        out[id] = lerpEntity(ea, eb, t);
      }
      // Entities present only in `after`: include them at their full state
      // (they just appeared — don't interpolate from origin).
      for (const [id, eb] of Object.entries(b)) {
        if (!a[id]) out[id] = eb;
      }
      return out;
    }

    function lerpEntity(a, b, t) {
      const out = {};
      for (const [k, va] of Object.entries(a)) {
        const vb = b[k];
        if (typeof va === 'number' && typeof vb === 'number') {
          out[k] = va + (vb - va) * t;
        } else {
          // Non-numeric (e.g. animation name, sprite index) — snap to newest.
          out[k] = vb != null ? vb : va;
        }
      }
      // Pick up any new fields added in `b`.
      for (const [k, vb] of Object.entries(b)) {
        if (out[k] === undefined) out[k] = vb;
      }
      return out;
    }

    function extrapolate(snap, ageMs) {
      // Linear extrapolation: pos += vel * dt.
      const dt = ageMs / 1000;
      const out = {};
      for (const [id, e] of Object.entries(snap.entities)) {
        const c = Object.assign({}, e);
        if (typeof c.vx === 'number' && typeof c.x === 'number') c.x += c.vx * dt;
        if (typeof c.vy === 'number' && typeof c.y === 'number') c.y += c.vy * dt;
        if (typeof c.vz === 'number' && typeof c.z === 'number') c.z += c.vz * dt;
        out[id] = c;
      }
      return out;
    }

    // ---- Drop old snapshots (call when server confirms ack) ----------------
    function ack(seq) {
      // Inform the server we received up to `seq` (used for delta compression).
      // The base snapshot cache is already pruned in pushSnapshot.
    }

    // ---- Clear --------------------------------------------------------------
    function reset() {
      buffer.clear();
      baseSnapshots.clear();
      newestSeq = -1;
      stats.pushed = 0;
      stats.deltasApplied = 0;
      stats.dropped = 0;
      stats.extrapolated = 0;
      stats.lastSampleSeq = -1;
    }

    return {
      pushSnapshot,
      sample,
      ack,
      reset,
      stats,
      get newestSeq() { return newestSeq; },
      get bufferSize() { return buffer.size; },
      get tickRate() { return tickRate; },
      get delayMs() { return delayMs; },
    };
  }

  // ---------------------------------------------------------------------------
  // Export
  // ---------------------------------------------------------------------------

  global.TDNet = global.TDNet || {};
  global.TDNet.Interpolation = { create };

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = { create };
  }

})(typeof globalThis !== 'undefined' ? globalThis
   : typeof self !== 'undefined' ? self
   : typeof window !== 'undefined' ? window
   : typeof global !== 'undefined' ? global : this);
