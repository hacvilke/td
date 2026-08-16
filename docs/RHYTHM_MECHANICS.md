# Rhythm Game Mechanics — TD Engine Design

This document describes the design and implementation of TD Engine's
beat-synced gameplay system. The system lives in `src/ecs/beat_system.cpp`
(the `BeatSystem` and its per-frame update logic) and `src/ecs/component.h`
(the `BeatTrackerComponent` struct). It is exposed to web games through
the `td_beat_*` family of C functions in `wasm/emscripten_main.cpp` and
wrapped by the `TDEngine.beat` namespace in `web/td_api.js`. A working
example ships at `examples/3d-showcase/game/beat.js` (beat-synced visual
pulse inside the 3D showcase).

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
as a small ECS component + system, exposed to JavaScript through thirteen
`EMSCRIPTEN_KEEPALIVE` C functions.

## 2. Architecture

The system is split into three pieces, matching the natural granularity
of the work:

### 2.1 Song-level state (the music handler)
- The backing track is played by the existing `td::Mixer::play(wav, volume, loop=true)`.
- `BeatTrackerComponent::spb` (seconds-per-beat) is computed once at
  start time as `60.0f / bpm` and cached on the component. It is the
  canonical beat interval used by every other calculation.
- Engine time (`td::g_time.totalTime`) is the clock the beat system
  reads. The mixer's playback position is intentionally NOT used as the
  metronome clock — engine time is deterministic across fixed-step
  updates, while the mixer's sample index can drift or jump. The beat
  system instead tracks beats relative to `startTime` (engine time when
  `td_beat_start` was called).
- Loop detection is implicit: if the engine time appears to go backward
  relative to `lastBeatTime` (i.e. `engineTime < lastBeatTime`), the
  beat system treats it as a loop / reset and recomputes `nextBeatTime`
  and `lastBeatTime` from `startTime` so no drift accumulates.

### 2.2 Beat-level state (the metronome)
The `BeatTrackerComponent` (declared in `src/ecs/component.h`, updated
by `BeatSystem` in `src/ecs/beat_system.cpp`) holds:

- `bpm` — beats per minute (set by `td_beat_start` or `td_beat_set_bpm`).
- `spb` — seconds per beat (cached `60.0f / bpm`).
- `startTime` — engine time when tracking started.
- `nextBeatTime` — engine time of the next metronome tick.
- `lastBeatTime` — engine time of the most recent metronome tick.
- `windowHalf` — half-width of the on-beat tolerance window, in seconds.
- `upperBound` / `lowerBound` — the on-beat hit window (see §3 below
  for why this is two values, not one).
- `combo` / `bestCombo` / `beatCount` / `lastHitTime` / `active`.

Per-frame `BeatSystem::update(world, engineTime)`:
1. If `engineTime < lastBeatTime` (loop / reset detected), recompute
   `nextBeatTime` and `lastBeatTime` from `startTime` so the metronome
   stays aligned with the song's loop point.
2. Recompute the two-half-window bounds (`upperBound` / `lowerBound`).
3. Catch-up loop: while `engineTime >= nextBeatTime`, fire the beat event
   (advancing `lastBeatTime = nextBeatTime`, then
   `nextBeatTime += spb`), recompute the bounds, and (if a JS callback
   is registered) invoke `td_beat_set_callback`'s callback with the new
   beat count and the beat time. The catch-up loop is bounded by a
   safety counter so a tiny `spb` set by mistake cannot lock the engine.

### 2.3 Input judgement (`td_beat_is_on_beat` + `td_beat_register_hit`)
`td_beat_is_on_beat(entityId)` returns 1 if the current engine time is
inside the on-beat window. The window is actually a union of two
half-windows around each beat — see §3.

`td_beat_register_hit(entityId, strict)` records a player hit. With
`strict=1`, a hit outside the on-beat window resets the combo to 0 and
returns 0. With `strict=0`, every call increments the combo (the JS
side is expected to gate with `td_beat_is_on_beat` itself). The return
value is the **new combo count** (not a score delta — the JS game
decides how to convert combo into score).

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

The fix is to split into two ranges, computed inside `BeatSystem::update`
immediately after each beat fires:

```
upperBound = lastBeatTime + windowHalf   (forward-looking from the beat that just fired)
lowerBound = nextBeatTime - windowHalf   (backward-looking from the upcoming beat)
```

The window the player sees is the union of these two half-windows around
each beat. Reads as one symmetric window to the player; under the hood
it is two adjacent half-windows. The check in
`BeatSystem::isOnBeat` is:

```
isOnBeat = (engineTime <= upperBound) || (engineTime >= lowerBound)
```

Wait — that looks reversed. The trick is that the "off-beat" gap is the
range `(upperBound, lowerBound)` between the two half-windows. So
"on-beat" is "not in the gap": `engineTime <= upperBound OR
engineTime >= lowerBound`. When `upperBound` and `lowerBound` overlap
or touch (i.e. `spb <= 2 * windowHalf`), every moment counts as on-beat
— which is the correct degenerate behavior for very high BPMs or very
wide windows.

## 4. Loop-point desync fix

A naive implementation of loop handling just keeps advancing
`nextBeatTime += spb` as if nothing happened when the song loops.

The problem: `nextBeatTime` drifts relative to the actual song position
over multiple loops because of accumulated floating-point error and
imperfect loop-point alignment. After 30 loops at 120 BPM, the metronome
can be audibly off from the beat.

The fix: on loop detection (`engineTime < lastBeatTime`), recompute
both `nextBeatTime` and `lastBeatTime` directly from `startTime` and
the integer beat count:

```
beatsElapsed = floor((engineTime - startTime) / spb)
lastBeatTime = startTime + beatsElapsed * spb
nextBeatTime = startTime + (beatsElapsed + 1) * spb
```

This snaps the metronome back to the mathematically correct position
relative to the start of the song. Tiny timing imperfection at the loop
boundary, but no drift accumulation. This is the implementation in
`BeatSystem::update()`'s loop-detection branch.

## 5. C API exports

Defined in `wasm/emscripten_main.cpp`, all `EMSCRIPTEN_KEEPALIVE`. The
full table (13 functions):

| Function | Purpose |
|---|---|
| `td_beat_start(entityId, bpm, windowHalfSec)` | Attach a `BeatTrackerComponent` to an entity, set BPM + tolerance, start tracking. |
| `td_beat_stop(entityId)` | Detach the component, stop tracking. |
| `td_beat_is_on_beat(entityId)` | Returns 1 if inside the on-beat window, else 0. Uses the two-half-window trick from §3. |
| `td_beat_get_count(entityId)` | Total beats elapsed since `td_beat_start`. |
| `td_beat_get_next_beat_time(entityId)` | Engine time of the next beat tick. |
| `td_beat_get_last_beat_time(entityId)` | Engine time of the most recent beat tick. |
| `td_beat_get_combo(entityId)` | Current hit-combo count. |
| `td_beat_get_best_combo(entityId)` | Best combo this session. |
| `td_beat_register_hit(entityId, strict)` | Record a player hit. Returns the new combo count (not a score delta). With `strict=1`, a hit outside the on-beat window resets combo and returns 0. |
| `td_beat_reset_combo(entityId)` | Reset combo to 0. Returns the previous combo count. |
| `td_beat_set_callback(cb)` | Register a JS callback invoked on every beat tick. Signature: `function(beatCount, beatTime)`. |
| `td_beat_set_bpm(entityId, newBpm)` | Change BPM mid-song. Recomputes `spb` and the bounds. |
| `td_beat_play_sound(entityId, wavIndex)` | Play a one-shot metronome tick through the mixer. |

## 6. JavaScript usage

The recommended way to use the beat system from a web game is the
`TDEngine.beat` namespace in `web/td_api.js`. A minimal rhythm game:

```javascript
await TDEngine.lifecycle.init('game-canvas');

// Create a "song" entity that owns the beat tracker.
const songEntity = TDEngine.ecs.create('song');
TDEngine.entity.setPosition(songEntity, 0, 0);

// 140 BPM, 150ms half-window (300ms total on-beat tolerance).
TDEngine.beat.start(songEntity, 140.0, 0.15);

// Spawn a note on every beat. The callback receives (beatCount, beatTime).
TDEngine.beat.setCallback((beatCount, beatTime) => {
  spawnNote(beatCount, beatTime);
});

document.addEventListener('keydown', (e) => {
  if (e.code === 'Space') {
    const onBeat = TDEngine.beat.isOnBeat(songEntity);
    if (onBeat) {
      const newCombo = TDEngine.beat.registerHit(songEntity, /*strict=*/false);
      score += 100 + newCombo * 10;   // game decides the scoring formula
      flashBeatIndicator();
    } else {
      score += 10;                    // weak hit
    }
  }
});
```

For the raw C-API path (when you need a function the `TDEngine.beat`
wrapper does not expose), use `TDEngine.bridge.wasmExports` directly:

```javascript
const M = TDEngine.bridge.wasmExports;
const td_beat_get_next_beat_time = M.cwrap('td_beat_get_next_beat_time',
                                            'number', ['number']);
const tPlus = td_beat_get_next_beat_time(songEntity) - TDEngine.clock.now();
```

A working example of beat-synced visuals ships at
`examples/3d-showcase/game/beat.js` — a pulsing-color card that reacts
to each beat tick inside the 3D showcase demo.

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
| `BeatTrackerComponent` struct | ✅ Shipped | `src/ecs/component.h` |
| `BeatSystem::update` (per-frame tick + loop detect) | ✅ Shipped | `src/ecs/beat_system.cpp` |
| `BeatSystem::isOnBeat` (two-half-window check) | ✅ Shipped | `src/ecs/beat_system.cpp` |
| Loop-point recomputation from `startTime` | ✅ Shipped | `src/ecs/beat_system.cpp` |
| 13 `td_beat_*` C exports | ✅ Shipped | `wasm/emscripten_main.cpp` |
| `TDEngine.beat.*` JS wrapper (lazy cwrap) | ✅ Shipped | `web/td_api.js` |
| Beat-synced visual example | ✅ Shipped | `examples/3d-showcase/game/beat.js` |

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
  `lastBeatTime`. Cleaner would be an explicit `onLoop` callback fired
  by the mixer itself when it wraps.
- **Per-entity beat callbacks.** Today `td_beat_set_callback` is a
  single global callback. If two entities each have a
  `BeatTrackerComponent` with different BPMs, the global callback fires
  for both beats with no way to distinguish which entity ticked. A
  per-entity callback (`td_beat_set_callback_for(entityId, cb)`) would
  fix this.

These are tracked as roadmap items, not blockers for the demo.
