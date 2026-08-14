# Building a Web Game on TD Engine — Getting Started

This guide walks you through writing a browser game in JavaScript that runs on
the C++ TD Engine via WebAssembly. No C++ knowledge required — the engine
exposes a clean JS API through `TDBridge`.

**Time to first game:** ~10 minutes.

---

## 1. The mental model

```
┌─────────────────────────────────────────────────────────────┐
│  Browser                                                    │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Your JS game (web/examples/your_game.js)           │   │
│  │  - Game rules, scoring, state machine               │   │
│  │  - Reads input, calls TDBridge.beatStart(...) etc.  │   │
│  └────────────────┬────────────────────────────────────┘   │
│                   │ TDBridge (window.TDBridge)              │
│                   │ - createEntity, setEntitySprite, ...    │
│                   │ - beatStart, beatIsOnBeat, onBeat, ...  │
│  ┌────────────────▼────────────────────────────────────┐   │
│  │  WASM module (web/td-engine.wasm)                   │   │
│  │  - C++ engine: ECS, Renderer, Mixer, BeatSystem     │   │
│  │  - Exposes td_* C functions to JS via Emscripten    │   │
│  └─────────────────────────────────────────────────────┘   │
│  WebGL2 canvas + Web Audio output                          │
└─────────────────────────────────────────────────────────────┘
```

**You write JS.** The engine handles rendering, the fixed-step game loop,
audio mixing, and ECS bookkeeping. Your JS calls into the engine via
`TDBridge` (a global on `window`).

---

## 2. Minimum viable game (10 lines)

```html
<!-- web/my_game.html -->
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>My TD Game</title>
</head>
<body>
  <canvas id="game-canvas" width="800" height="600"></canvas>

  <!-- 1. Load the engine -->
  <script src="td-engine.js"></script>
  <script src="js_bridge.js"></script>

  <!-- 2. Your game -->
  <script>
    TDBridge.onReady(async () => {
      await TDBridge.init('game-canvas');

      // Create a player entity and put it on screen.
      const player = TDBridge.createEntity('player');
      TDBridge.setEntityPosition(player, 400, 300);
      TDBridge.setEntitySprite(player, 32, 32, 0.4, 0.9, 1.0, 1.0);

      // Move right while Space is held.
      function frame() {
        const isDown = TDBridge.wasmExports.cwrap('td_is_key_down', 'number', ['number']);
        if (isDown(0x20 /* Space */)) {
          TDBridge.setEntityPosition(player, 410, 300);
        }
        requestAnimationFrame(frame);
      }
      requestAnimationFrame(frame);
    });
  </script>
</body>
</html>
```

That's it. The engine renders the player as a cyan 32×32 quad. You can run it
locally with:

```bash
make web
cd web && python3 -m http.server 8000
# open http://localhost:8000/my_game.html
```

---

## 3. The TDBridge API at a glance

### Lifecycle

| Function | Description |
|---|---|
| `TDBridge.onReady(cb)` | Calls `cb` once when the WASM module is loaded |
| `await TDBridge.init('canvas-id')` | Boots the engine; returns a Promise |
| `TDBridge.resumeAudio()` | Resumes the Web Audio context (call after a user gesture) |
| `TDBridge.shutdown()` | Tears down the engine |

### Entities

| Function | Description |
|---|---|
| `TDBridge.createEntity(name?)` → `entityId` | Creates a new ECS entity |
| `TDBridge.setEntityPosition(id, x, y)` | Attaches/updates PositionComponent |
| `TDBridge.setEntitySprite(id, w, h, r?, g?, b?, a?)` | Attaches/updates SpriteComponent |
| `TDBridge.destroyEntity(id)` | Destroys the entity + all its components |
| `TDBridge.isEntityValid(id)` → `bool` | False if the entity was destroyed |

### Input (Win32 VK codes — same as browser `e.keyCode`)

| Function | Description |
|---|---|
| `TDBridge.wasmExports.cwrap('td_is_key_down', 'number', ['number'])(vkCode)` | True if key currently held |
| `TDBridge.wasmExports.cwrap('td_is_mouse_down', 'number', ['number'])(button)` | True if mouse button held |
| `TDBridge.wasmExports.cwrap('td_get_mouse_pos', null, ['number','number'])(outXPtr, outYPtr)` | Reads mouse X,Y into WASM heap |

Common VK codes: `0x20` Space, `0x1B` Escape, `0x25-0x28` Arrow keys,
`0x41-0x5A` A-Z, `0x30-0x39` 0-9.

### Beat Tracker (rhythm games)

| Function | Description |
|---|---|
| `TDBridge.beatStart(entityId, bpm, windowHalfSec)` | Starts BPM-synced metronome on an entity |
| `TDBridge.beatIsOnBeat(entityId)` → `bool` | True if current time is inside the on-beat window |
| `TDBridge.onBeat(callback)` | Calls `cb(beatCount, beatTime)` on every beat tick |
| `TDBridge.beatRegisterHit(entityId, strict)` → `combo` | Records a hit; if `strict=true`, off-beat hits reset combo |
| `TDBridge.beatGetCombo(entityId)` / `beatGetBestCombo(entityId)` | Read combo state |
| `TDBridge.beatResetCombo(entityId)` | Resets combo to 0 |
| `TDBridge.beatSetBpm(entityId, newBpm)` | Live BPM change (for songs with tempo shifts) |
| `TDBridge.beatStop(entityId)` | Deactivates the tracker |
| `TDBridge.beatGetCount(entityId)` | Total beats elapsed since start |
| `TDBridge.beatGetNextBeatTime(entityId)` / `beatGetLastBeatTime(entityId)` | Timestamps for visual sync |

### Scene loading

| Function | Description |
|---|---|
| `TDBridge.loadScene(sceneText)` | Parses a scene description string into the World |

Scene format:
```
entity Player {
  position { x: 100 y: 100 }
  velocity { x: 0   y: 0 }
  sprite   { w: 32 h: 32 r: 1 g: 1 b: 1 a: 1 }
  collider { w: 32 h: 32 }
}
```

---

## 4. Recipe: a rhythm game in 30 lines

```javascript
TDBridge.onReady(async () => {
  await TDBridge.init('game-canvas');
  TDBridge.resumeAudio();

  // 1. Create the "song" entity that owns the beat tracker.
  const song = TDBridge.createEntity('song');

  // 2. Start at 120 BPM, 150ms half-window (300ms total on-beat zone).
  TDBridge.beatStart(song, 120, 0.15);

  // 3. Spawn a note on every beat tick.
  TDBridge.onBeat((beatCount, beatTime) => {
    const note = TDBridge.createEntity('note');
    TDBridge.setEntityPosition(note, 100 + Math.random() * 600, 0);
    TDBridge.setEntitySprite(note, 40, 40, 1, 0.5, 0.5, 1);
    // (Animate the note falling in your own rAF loop.)
  });

  // 4. On Space, check if the player is on-beat.
  const isKeyDown = TDBridge.wasmExports.cwrap('td_is_key_down', 'number', ['number']);
  let lastSpace = false;
  function frame() {
    const space = !!isKeyDown(0x20);
    if (space && !lastSpace) {  // edge-triggered
      if (TDBridge.beatIsOnBeat(song)) {
        const combo = TDBridge.beatRegisterHit(song, /*strict=*/true);
        console.log(`On beat! Combo: ${combo}`);
      } else {
        TDBridge.beatResetCombo(song);
        console.log('Missed!');
      }
    }
    lastSpace = space;
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
});
```

See `web/examples/beat_demo.js` for the complete, polished version with
falling notes, particles, scoring, and HUD.

---

## 5. The beat-hit window (the trick that took the source video author hours to debug)

The on-beat window is **two adjacent half-windows**, not one symmetric window:

```
       lastBeat          nextBeat
         │                  │
         ▼                  ▼
◄──────┤████████████████████├──────►
   off  │       on-beat     │   off
        │                   │
        upperBound    lowerBound
        (lastBeat+    (nextBeat-
         windowHalf)   windowHalf)
```

The reason: when a beat fires, `nextBeat` immediately advances to the NEXT
beat. If the upper bound were `nextBeat + windowHalf`, it would always be
far in the future and unreachable. So the engine uses:

- `upperBound = lastBeatTime + windowHalf` (forward-looking from the beat
  that just fired)
- `lowerBound = nextBeatTime - windowHalf` (backward-looking from the
  upcoming beat)

`beatIsOnBeat()` returns true if the current time is within `windowHalf` of
**either** `lastBeatTime` **or** `nextBeatTime`. You don't need to worry
about this — the engine handles it — but it's worth understanding if you
want to tune the difficulty.

**Tuning guide:**
- `windowHalf = 0.20` → very forgiving (400ms total window; casual players)
- `windowHalf = 0.15` → balanced (300ms; default)
- `windowHalf = 0.10` → tight (200ms; experienced players)
- `windowHalf = 0.05` → hardcore (100ms; rhythm-game veterans)

---

## 6. Loop detection (why your song shouldn't drift)

If your song loops, the engine's `BeatSystem` detects when `engineTime` goes
backward (the loop point) and hard-resets `nextBeatTime` to avoid drift
accumulation. You don't need to do anything — just keep feeding audio and
the engine stays in sync.

The original bug (from the source video): naive metronomes keep advancing
`nextBeat += spb` across loops, which compounds floating-point error and
imperfect loop-point alignment. After a few loops, the beat ticks drift
seconds away from the actual song beats. Our implementation avoids this by
hard-resetting on loop detection.

---

## 7. Performance tips

- **Cache `cwrap` handles.** Calling `Module.cwrap('td_is_key_down', ...)`
  inside your rAF loop is expensive. Call it once on init and reuse the
  returned function:

  ```javascript
  let api = null;
  function cacheApi() {
    const M = TDBridge.wasmExports;
    api = {
      isKeyDown: M.cwrap('td_is_key_down', 'number', ['number']),
      // ...
    };
  }
  ```

- **Don't create entities per frame.** ECS entity creation involves a
  linear scan for a free slot. Pool your entities (pre-allocate 50 bullets
  on init, reuse them).

- **Keep `onBeat` callbacks fast (<1ms).** They run inside the engine's
  fixed-step update. If they take too long, the simulation slows down.
  Spawn the note entity and get out — do the animation in your rAF loop.

- **`setEntityPosition` is cheap.** It writes directly to the component
  array; no allocation. Call it every frame for moving entities.

---

## 8. Debugging

- **Open the browser console.** The engine logs everything via `td::Logger`,
  and `js_bridge.js` forwards those logs to `console.log`/`console.warn`/
  `console.error` with a `[TD]` prefix.

- **The on-screen console.** `web/play.html` has a hidden `#engine-console`
  div that mirrors the engine's log output. It auto-shows only on warn/error/
  deprecated logs (info-level boot noise is suppressed to avoid covering the
  game canvas). Click the "Console" link in the top bar to toggle it manually.

- **Check the engine version.** `TDBridge.wasmExports.cwrap('td_get_version', 'string')()`
  should return `"TD Engine 1.0.0 (WebAssembly)"`.

- **Verify the beat tracker is running.** After `beatStart(songEntity, ...)`,
  the engine logs `"BeatTracker started: bpm=120.0 spb=0.500 windowHalf=0.150s"`.

---

## 9. Full examples to learn from

| File | What it demonstrates |
|---|---|
| `web/examples/pong.js` | Minimal: 2 paddles, a ball, AI, scoring. ~300 lines. |
| `web/examples/voidrunner.js` | Full game: 3 enemy types, power-ups, particles, parallax, waves. ~520 lines. |
| `web/examples/beat_demo.js` | Rhythm game: beat tracker, on-beat detection, combo, falling notes. ~250 lines. |

Read them. Copy them. Modify them. That's the fastest way to learn.

---

## 10. Reference: all exported C functions

These are the raw C functions exposed by the WASM module. You usually don't
need to call them directly — `TDBridge` wraps them. But if you want to
call one that isn't wrapped yet, use `cwrap`:

```javascript
const td_my_fn = TDBridge.wasmExports.cwrap('td_my_fn', returnType, [argTypes]);
td_my_fn(...args);
```

| C function | JS return | JS args |
|---|---|---|
| `td_init` | `void` | `[width, height]` |
| `td_shutdown` | `void` | `[]` |
| `td_load_scene` | `void` | `[sceneText]` |
| `td_set_key_state` | `void` | `[vkCode, pressed]` |
| `td_set_mouse_state` | `void` | `[x, y, leftDown, rightDown]` |
| `td_resize` | `void` | `[w, h]` |
| `td_get_version` | `string` | `[]` |
| `td_fill_audio_buffer` | `void` | `[outPtr, numFrames]` |
| `td_create_entity` | `number` | `[name]` |
| `td_entity_set_position` | `void` | `[id, x, y]` |
| `td_entity_get_position` | `void` | `[id, outXPtr, outYPtr]` |
| `td_entity_set_velocity` | `void` | `[id, vx, vy]` |
| `td_entity_set_sprite` | `void` | `[id, w, h, r, g, b, a]` |
| `td_entity_set_collider` | `void` | `[id, w, h]` |
| `td_entity_destroy` | `void` | `[id]` |
| `td_entity_is_valid` | `number` | `[id]` |
| `td_get_entity_count` | `number` | `[]` |
| `td_is_key_down` | `number` | `[vkCode]` |
| `td_is_mouse_down` | `number` | `[button]` |
| `td_get_mouse_pos` | `void` | `[outXPtr, outYPtr]` |
| `td_render_frame` | `void` | `[]` |
| `td_set_callbacks` | `void` | `[initCb, updateCb, renderCb, shutdownCb]` |
| `td_beat_start` | `void` | `[entityId, bpm, windowHalfSec]` |
| `td_beat_stop` | `void` | `[entityId]` |
| `td_beat_is_on_beat` | `number` | `[entityId]` |
| `td_beat_get_count` | `number` | `[entityId]` |
| `td_beat_get_next_beat_time` | `number` | `[entityId]` |
| `td_beat_get_last_beat_time` | `number` | `[entityId]` |
| `td_beat_register_hit` | `number` | `[entityId, strict]` |
| `td_beat_get_combo` | `number` | `[entityId]` |
| `td_beat_get_best_combo` | `number` | `[entityId]` |
| `td_beat_reset_combo` | `number` | `[entityId]` |
| `td_beat_set_callback` | `void` | `[cbPtr]` |
| `td_beat_set_bpm` | `void` | `[entityId, newBpm]` |
| `td_beat_play_sound` | `void` | `[entityId, wavIndex]` |

---

## 11. Where to go next

1. **Read `docs/RHYTHM_MECHANICS.md`** for the design rationale behind the
   beat tracker (including the two debugging insights from the source video
   that informed the implementation).

2. **Read `docs/PUBLIC_APIS.md`** for 25 free, CORS-enabled public APIs you
   can fetch from your web game (Star Wars data, weather, avatars, jokes,
   quotes, etc.) — no API key required for most.

3. **Read `wasm/README.md`** for the technical details of how the WASM
   bridge works (architecture diagram, what's ported vs excluded, the
   Emscripten build flags).

4. **Build something.** The fastest way to learn is to copy `beat_demo.js`,
   change the BPM, swap the sprites, and ship your own rhythm game.
