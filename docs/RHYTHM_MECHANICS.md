# Rhythm Game Mechanics — TD Engine Design

This document describes the design and implementation of TD Engine's
beat-synced gameplay system. The system lives in `src/ecs/beat_system.cpp`
and is exposed to web games through the `td_beat_*` family of C functions
in `wasm/emscripten_main.cpp`. A complete working demo ships at
`web/examples/beat_demo.js`.

## 1. Overview

A rhythm game needs the engine to do three things on time, every time:

1. **Play a backing track** with sample-accurate loop detection.
2. **Fire a beat tick** at deterministic intervals derived from the track's
   BPM (beats-per-minute) setting.
3. **Judge player input** against the current beat — was the key press
   inside the on-beat tolerance window?

TD Engine already had the primitives for (1): the `td::Mixer` plays WAV
samples with loop support, and the `GameLoop`'s fixed-step accumulator
provides a deterministic clock. The beat system layers (2) and (3) on top
as a small ECS component + system, exposed to JavaScript through five
`EMSCRIPTEN_KEEPALIVE` C functions.

## 2. Architecture

The system is split into three pieces, matching the natural granularity
of the work:

### 2.1 Song-level state (the music handler)
- The backing track is played by the existing `td::Mixer::play(wav, volume, loop=true)`.
- `get_seconds_per_beat()` returns `60.0 / BPM` — the canonical beat
  interval in seconds.
- `get_playback_time()` returns the mixer channel's current sample index,
  converted to seconds.
- `get_song_time()` returns playback time AND detects loops: if the new
  `songTime` is less than the previous frame's value (i.e., time went
  backward), the song looped — reset the metronome to the loop point.

### 2.2 Beat-level state (the metronome)
The `BeatTrackerComponent` (in `src/ecs/beat_system.cpp`) holds:
- `nextBeat` — engine time of the next metronome tick.
- `onBeatNiceness` — half-width of the on-beat tolerance window, in seconds.
- `metroTween` — fade-out animation for the debug indicator.
- `upperBound` / `lowerBound` — the on-beat hit window (see §3 below for
  why this is two values, not one).

Per-frame `update(world, engineTime)`:
1. Read `songTime`, `spb`, `loopPoint` via the getters above.
2. If `songTime >= nextBeat`: fire the beat event, advance
   `nextBeat += spb`, recompute the bounds, fade the debug text.
3. If the song looped: play the metronome tick, hard-reset `nextBeat` to
   the loop point. This prevents floating-point drift from accumulating
   across multiple loops.

### 2.3 Input judgement (`td_beat_is_on_beat` + `td_beat_register_hit`)
Returns true if the current `songTime` is inside the on-beat window. The
window is actually a union of two half-windows around each beat — see §3.

## 3. The two-half-window trick (the most valuable insight)

A naive implementation of the on-beat window is:

```
lowerBound = nextBeat - tolerance
upperBound = nextBeat + tolerance
```

This has a subtle bug. When the beat fires, `nextBeat` immediately jumps
to the **next** beat time, so `upperBound` is now far in the future —
the player can never be "after the current beat but before
`upperBound`". The lower half of the window works; the upper half does
not.

The fix is to split into two ranges:

```
upperBound = currentBeat + tolerance   (forward-looking from the beat that just fired)
lowerBound = nextBeat    - tolerance   (backward-looking from the upcoming beat)
```

The window the player sees is the union of these two half-windows around
each beat. Reads as one symmetric window to the player; under the hood
it's two adjacent half-windows. This is the implementation in
`BeatTrackerComponent::recompute_bounds()`.

## 4. Loop-point desync fix

A naive implementation of loop handling just keeps advancing
`nextBeat += spb` as if nothing happened when the song loops.

The problem: `nextBeat` drifts relative to the actual song position over
multiple loops because of accumulated floating-point error and imperfect
loop-point alignment. After 30 loops at 120 BPM, the metronome can be
audibly off from the beat.

The fix: on loop detection, hard-reset `nextBeat = loopPoint` and fire
the beat immediately. Tiny timing imperfection at the loop boundary, but
no drift accumulation. This is the implementation in
`BeatSystem::update()`'s loop-detection branch.

## 5. C API exports

Defined in `wasm/emscripten_main.cpp`, all `EMSCRIPTEN_KEEPALIVE`:

| Function | Purpose |
|---|---|
| `td_beat_start(entityId, bpm, windowHalfSec)` | Attach a `BeatTrackerComponent` to an entity, set BPM + tolerance, start tracking. |
| `td_beat_stop(entityId)` | Detach the component, stop tracking. |
| `td_beat_is_on_beat(entityId)` | Returns 1 if inside the on-beat window, else 0. Uses the two-half-window trick from §3. |
| `td_beat_get_count(entityId)` | Total beats elapsed since `td_beat_start`. |
| `td_beat_get_next_beat_time(entityId)` | Engine time of the next beat tick. |
| `td_beat_get_last_beat_time(entityId)` | Engine time of the most recent beat tick. |
| `td_beat_register_hit(entityId, strict)` | Record a player hit; returns the score delta (100 + combo × 10 in non-strict mode, 100 + combo × 20 in strict). Resets combo on miss. |
| `td_beat_get_combo(entityId)` | Current hit-combo count. |
| `td_beat_get_best_combo(entityId)` | Best combo this session. |
| `td_beat_reset_combo(entityId)` | Reset combo to 0 (called on miss). |
| `td_beat_set_callback(cb)` | Register a JS callback invoked on every beat tick. Signature: `function(beatCount, beatTime)`. |
| `td_beat_set_bpm(entityId, newBpm)` | Change BPM mid-song. Recomputes `spb` and the bounds. |
| `td_beat_play_sound(entityId, wavIndex)` | Play a one-shot metronome tick through the mixer. |

## 6. JavaScript usage

A minimal rhythm game in JavaScript:

```javascript
TDBridge.onReady(() => {
  const M = TDBridge.wasmExports;
  const td_create     = M.cwrap('td_create_entity',      'number', ['string']);
  const td_set_pos    = M.cwrap('td_entity_set_position', null,     ['number','number','number']);
  const td_beat_start = M.cwrap('td_beat_start',          null,     ['number','number','number']);
  const td_beat_is    = M.cwrap('td_beat_is_on_beat',     'number', ['number']);
  const td_beat_hit   = M.cwrap('td_beat_register_hit',   'number', ['number','number']);
  const td_beat_cb    = M.cwrap('td_beat_set_callback',   null,     ['number']);

  // Create a "song" entity that owns the beat tracker.
  const songEntity = td_create('song');
  td_set_pos(songEntity, 0, 0);

  // 140 BPM, 150ms half-window (300ms total on-beat tolerance).
  td_beat_start(songEntity, 140.0, 0.15);

  // Spawn a note on every beat.
  td_beat_cb(M.addFunction((beatCount, beatTime) => {
    spawnNote(beatCount);
  }, 'vii'));

  document.addEventListener('keydown', (e) => {
    if (e.keyCode === 0x20 /* Space */) {
      const onBeat = td_beat_is(songEntity);
      if (onBeat) {
        score += td_beat_hit(songEntity, 0);  // bonus + combo
        flashBeatIndicator();
      } else {
        score += 10;  // weak hit
      }
    }
  });
});
```

A complete, working demo is in `web/examples/beat_demo.js` — a 2-key
"tap on the beat" mini-game playable in the live web player.

## 7. Why this matters for the engine

A rhythm-game demo showcases three things the other demos don't:

1. **The audio system as a gameplay-critical primitive.** VOID RUNNER
   uses the Mixer only for one-shot SFX (laser, explosion). A rhythm
   game uses the Mixer as a *gameplay-critical* system — the song
   playback position drives every mechanic. This proves the Mixer is
   real, not just decoration.

2. **Cross-platform timing precision.** Rhythm games are notoriously
   timing-sensitive (humans can perceive 20-30ms of audio delay). A
   working beat demo proves the engine's fixed-step accumulator +
   Web Audio bridge are tight enough for music-driven gameplay.

3. **A new genre with minimal code.** Once `BeatSystem` + the `td_beat_*`
   exports exist, a web developer can clone the demo and ship their own
   rhythm game by swapping the song + sprites. Compare to building a
   rhythm game from scratch — this saves weeks.

## 8. Implementation status

| Piece | Status | File |
|---|---|---|
| `BeatTrackerComponent` | ✅ Shipped | `src/ecs/beat_system.cpp` |
| `BeatSystem::update` (per-frame tick + loop detect) | ✅ Shipped | `src/ecs/beat_system.cpp` |
| Two-half-window bounds | ✅ Shipped | `src/ecs/beat_system.cpp` |
| Loop-point hard-reset | ✅ Shipped | `src/ecs/beat_system.cpp` |
| 13 `td_beat_*` C exports | ✅ Shipped | `wasm/emscripten_main.cpp` |
| `TDBridge.onBeat(cb)` JS bridge | ✅ Shipped | `wasm/js_bridge.js` |
| `beat_demo.js` sample game | ✅ Shipped | `web/examples/beat_demo.js` |
| `test_beat_*` regression tests | ✅ Shipped | `tests/test_*.cpp` |

## 9. Open work

- **AudioWorklet migration.** The current bridge uses `ScriptProcessor`
  with a 4096-sample buffer, which is ~93ms at 44.1kHz. Switching to
  `AudioWorklet` would lower this to <10ms — important for serious
  rhythm games where the player's perception window is 20-30ms.
- **MP3 / OGG decoding.** The current Mixer accepts only WAV. For real
  songs (typically MP3/OGG), we'd decode in the browser via Web Audio's
  `decodeAudioData` and feed PCM into the Mixer.
- **Explicit loop callback on `MixerChannel`.** Today, loop detection
  happens in the `BeatSystem` by comparing the previous and current
  `songTime`. Cleaner would be an explicit `onLoop` callback fired by
  the mixer itself when it wraps.

These are tracked as roadmap items, not blockers for the demo.
