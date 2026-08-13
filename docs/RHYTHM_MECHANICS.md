# Rhythm Game Mechanics — Design Proposal for TD Engine

**Source video:** ["My Personal Process for Implementing Game Mechanics (PPAR)"](https://youtu.be/j3f8xOv2Tpg) — a ~16-minute walkthrough of building a BPM-synced metronome + on-beat attack bonus for a rhythm game.

**Verdict:** Yes, this is very suitable for our engine. The mechanic is small (a few hundred lines of C++), fits cleanly into our existing ECS + Mixer + GameLoop primitives, and a rhythm demo would showcase the audio system in a way that VOID RUNNER does not.

---

## 1. What the video actually implements

The creator builds a "hit-on-beat" mechanic for a dog-themed rhythm game: attacks deal bonus damage if the player presses the attack key within a small window around each beat of the backing track.

The implementation breaks down into three pieces:

### 1.1 Music handler (song-level state)
- Holds an `AudioStreamPlayer`, a `MusicInfo` resource (BPM + audio file + loop offset), and a `loopChecker` for loop detection.
- `play_song(resource)` sets the audio stream and starts playback.
- `get_seconds_per_beat()` returns `60.0 / BPM`.
- `get_playback_time()` returns the audio player's current time position.
- `get_song_time()` returns playback time AND detects loops: if `songTime < loopChecker` (i.e., time went backward), the song looped — reset accordingly.

### 1.2 Metronome (beat-level state)
Five fields:
- `nextBeat` — timestamp of the next metronome hit.
- `onBeatNiceness` — half-width of the on-beat tolerance window.
- `metroTween` — fade-out animation for the debug indicator.
- `upperBound` / `lowerBound` — the on-beat hit window.

Per-frame `process(delta)`:
1. Read `songTime`, `spb`, `loopPoint` via getters.
2. If `songTime >= nextBeat`: fire the beat event, advance `nextBeat += spb`, recompute the bounds, fade the debug text.
3. If the song looped (same check as the music handler): play the metronome tick, reset `nextBeat` to the loop point.

### 1.3 `check_on_beat()` (used by the player attack script)
Returns true if the current `songTime` is inside the beat-hit window.

### 1.4 The two debugging insights (the most valuable parts of the video)

**Bug 1: Only the lower bound of the beat window was working.**
- Naive implementation: `lowerBound = nextBeat - tolerance`, `upperBound = nextBeat + tolerance`.
- Problem: when the beat fires, `nextBeat` immediately jumps to the NEXT beat, so `upperBound` is now far in the future — the player can never be "after the current beat but before upperBound".
- Fix: split into two ranges.
  - `upperBound = currentBeat + tolerance` (forward-looking from the beat that just fired).
  - `lowerBound = nextBeat - tolerance` (backward-looking from the upcoming beat).
- The window the player sees is the union of these two half-windows around each beat. Reads as one symmetric window to the player; under the hood it's two adjacent half-windows.

**Bug 2: Audio desynced every time the song looped.**
- Naive implementation: on loop detection, just keep advancing `nextBeat += spb` as if nothing happened.
- Problem: `nextBeat` drifts relative to the actual song position over multiple loops because of accumulated floating-point error and imperfect loop-point alignment.
- Fix: on loop detection, hard-reset `nextBeat = loopPoint` and fire the beat immediately. Tiny timing imperfection at the loop boundary, but no drift accumulation.

---

## 2. Why this fits TD Engine

| Video's need | TD Engine primitive | Status |
|---|---|---|
| Play a song with loop support | `td::Mixer::play(wav, volume, loop=true)` | ✅ Already exists |
| Read song playback position | `MixerChannel::position` (sample index) | ✅ Already exists, but needs an exposed getter |
| Per-frame update tick | `GameLoop`'s fixed-step accumulator | ✅ Already exists |
| Track per-entity beat state | ECS component on an entity | ✅ World + component system exists |
| Per-frame system that scans for beat ticks | ECS system registered with the World | ✅ Already exists |
| Trigger a sound on each beat | `Mixer::play(metronomeTickWav, ...)` | ✅ Already exists |
| Expose to web games | Add a few `td_*` C functions to the WASM export list | ✅ Pattern already established |

**Net new code needed:**
- ~80 lines of C++ for a `BeatTrackerComponent` + `BeatSystem`.
- ~30 lines of C in `wasm/emscripten_main.cpp` to expose `td_start_beat_track`, `td_is_on_beat`, `td_get_beat_count`, `td_on_beat`.
- ~250 lines of JS for a sample rhythm game (`web/examples/beat_demo.js`).

Total: roughly half the size of VOID RUNNER. Very tractable.

---

## 3. Proposed API surface

### 3.1 C++ side (engine)

```cpp
// src/audio/beat_tracker.h  (new file)
#pragma once
#include <cstdint>

namespace td {

struct BeatTrackerComponent {
    float bpm = 120.0f;              // beats per minute
    float spb = 0.5f;                // seconds per beat (cached: 60/bpm)
    float songStartTime = 0.0f;      // engine time when playback started
    float nextBeatTime = 0.0f;       // engine time of next beat tick
    float beatWindow = 0.15f;        // half-width of on-beat tolerance (seconds)
    float upperBound = 0.0f;         // currentBeat + beatWindow
    float lowerBound = 0.0f;         // nextBeat - beatWindow
    int   beatCount = 0;             // total beats elapsed since start
    float lastBeatHitTime = -1.0f;   // engine time of last successful on-beat press
    bool  looped = false;            // flag set when song loop detected
    float loopPoint = -1.0f;         // if >=0, hard-reset nextBeat here on loop
};

class BeatSystem {
public:
    // Call once per frame from your game's update().
    // Fires onBeat callbacks, advances nextBeat, handles loop detection.
    void update(class World& world, float engineTime);
};

} // namespace td
```

### 3.2 C API exports (for WASM bridge)

```c
// Start beat tracking on entity with given BPM and tolerance (in milliseconds).
void td_start_beat_track(uint32_t entityId, float bpm, float windowMs);

// Returns 1 if the current song time is inside the on-beat window, else 0.
// Uses the two-half-window trick from the video.
int td_is_on_beat(uint32_t entityId);

// Total beats elapsed since td_start_beat_track was called.
int td_get_beat_count(uint32_t entityId);

// Register a JS callback to be invoked on each beat tick.
// Callback signature: function(beatCount: number, beatTime: number).
void td_on_beat(void (*callback)(int, float));
```

### 3.3 JavaScript usage (sample web game)

```javascript
// web/examples/beat_demo.js  (new file)
TDBridge.onReady(() => {
  const Module = TDBridge.wasmExports;
  const td_create      = Module.cwrap('td_create_entity', 'number', ['string']);
  const td_set_pos     = Module.cwrap('td_entity_set_position', null, ['number','number','number']);
  const td_start_beat  = Module.cwrap('td_start_beat_track', null, ['number','number','number']);
  const td_is_on_beat  = Module.cwrap('td_is_on_beat', 'number', ['number']);
  const td_on_beat     = Module.cwrap('td_on_beat', null, ['number']);

  // Create a "song" entity that owns the beat tracker.
  const songEntity = td_create('song');
  td_set_pos(songEntity, 0, 0);

  // 140 BPM, 150ms tolerance window.
  td_start_beat(songEntity, 140.0, 150.0);

  // Spawn a note on every beat; player taps Space to hit it.
  td_on_beat(Module.addFunction((beatCount, beatTime) => {
    spawnNote(beatCount);
  }, 'vii'));

  // In the main rAF loop:
  document.addEventListener('keydown', (e) => {
    if (e.keyCode === 0x20 /* Space */) {
      const onBeat = td_is_on_beat(songEntity);
      if (onBeat) {
        score += 100;  // bonus
        flashBeatIndicator();
      } else {
        score += 10;   // weak hit
      }
    }
  });
});
```

---

## 4. Why bother — what does this unlock?

A rhythm-game demo would showcase three things the current VOID RUNNER doesn't:

1. **The audio system.** VOID RUNNER uses the Mixer only for one-shot SFX (laser, explosion). A rhythm game uses the Mixer as a *gameplay-critical* system — the song playback position drives every mechanic. This proves the Mixer is real, not just decoration.

2. **Cross-platform timing precision.** Rhythm games are notoriously timing-sensitive (humans can perceive 20-30ms of audio delay). A working beat demo proves the engine's fixed-step accumulator + Web Audio bridge are tight enough for music-driven gameplay.

3. **A new genre with minimal code.** Once `BeatSystem` + the four `td_*` exports exist, a web developer can clone the demo and ship their own rhythm game by swapping the song + sprites. Compare to building a rhythm game from scratch — this saves weeks.

---

## 5. Risks / open questions

- **WASM audio latency.** The current bridge uses `ScriptProcessor` with a 4096-sample buffer, which is ~93ms at 44.1kHz. That's too laggy for a serious rhythm game. Would need to switch to `AudioWorklet` (lower latency, but more complex setup) before shipping a rhythm demo. Worth doing once, not now.
- **Loop point detection in the Mixer.** The current Mixer exposes `position` (sample index) but no "song just looped" event. The video's loop-detection trick (`songTime < loopChecker`) would work, but it's cleaner to add an explicit `onLoop` callback to `MixerChannel`.
- **Song asset loading.** We have a WAV loader, but most songs are MP3/OGG. For a real rhythm demo we'd want to decode MP3 in the browser (via Web Audio's `decodeAudioData`) and feed PCM into the Mixer. Out of scope for the first version — just use a short WAV loop for the demo.

---

## 6. Recommendation

**Build it.** Start with the minimal version:
1. Add `BeatTrackerComponent` + `BeatSystem` (~80 lines).
2. Expose the four `td_*` functions in `emscripten_main.cpp` (~30 lines).
3. Write `web/examples/beat_demo.js` — a 2-key "tap on the beat" mini-game (~250 lines).
4. Don't worry about AudioWorklet / MP3 / loop callbacks yet — use a short WAV loop and the existing ScriptProcessor bridge.

This is roughly a half-day of work and would give the engine a second demo game that showcases a completely different subsystem (audio + timing) than VOID RUNNER (ECS + rendering + input).

If the user wants this built, the next step is to greenlight the design and we can implement it in the next session.
