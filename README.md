# TD Engine

A complete 2D/3D game engine written from scratch in C/C++ with **zero external libraries**. The same C++ source runs on Windows desktop (Win32 + OpenGL 3.3) and in any modern browser (WebAssembly + WebGL 2). Web game developers write JavaScript or TypeScript that calls into the C++ engine via a thin WASM bridge.

## Status

| Platform | Status | Stack | Path |
|----------|--------|-------|------|
| **Desktop (Windows)** | ✅ Complete | C++17, Win32, OpenGL 3.3 | `src/`, `editor/`, `examples/` |
| **Browser (WASM)** | ✅ Complete | C++ → Emscripten → WebAssembly 2 + WebGL 2 | `wasm/`, `web/` |

The C++ engine source is **shared** between both targets. Only the platform layer is swapped: `src/platform/win32_*.cpp` on desktop, `wasm/emscripten_main.cpp` in the browser.

## Browser Quick Start

The browser build needs the WASM bundle compiled first, then any static file server:

```bash
# 1. Install Emscripten (one-time setup)
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source ./emsdk_env.sh

# 2. Build the WASM bundle
cd /path/to/td-engine
make web

# 3. Serve and open
cd web
python3 -m http.server 8000
# open http://localhost:8000 in a browser
```

Or just play it online: **https://hacvilke.github.io/td/** (landing page) — or jump straight to the **[web player](https://hacvilke.github.io/td/play.html)** or the **[documentation](https://hacvilke.github.io/td/docs.html)**.

The landing page (`web/index.html`) showcases the engine and links to the web player (`web/play.html`), which loads `td-engine.wasm` and runs **VOID RUNNER**, a complete vertical space shooter written in pure JavaScript (`web/examples/voidrunner.js`) that drives the C++ engine via `TDBridge`.

## Download prebuilt Windows binaries

Don't want to build from source? Grab the latest Windows x64 build from the
[Releases page](https://github.com/hacvilke/td/releases):

- **[TD Engine (Latest Build)](https://github.com/hacvilke/td/releases/tag/latest)** — rolling pre-release, rebuilt on every push to `main`. Contains `pong.exe`, `platformer.exe`, `td-editor.exe`, the test suite, sources, and assets in a single zip.
- **[Tagged releases](https://github.com/hacvilke/td/releases)** — stable versions, cut by pushing a `v*.*.*` tag (e.g. `v1.0.0`).

## Writing a web game in JavaScript

The recommended way is the **modular `TDEngine` API** (v=20+):

```javascript
// Boot the engine.
await TDEngine.init('game-canvas');

// Create entities via the ECS subsystem.
const player = TDEngine.ecs.create('Player');
TDEngine.ecs.setPosition(player, 100, 100);
TDEngine.ecs.setSprite(player, 32, 32, 1, 1, 1, 1);

// Poll input via the Input subsystem (Win32 VK codes).
TDEngine.lifecycle.onReady(() => {
  if (TDEngine.input.isKeyDown(TDEngine.input.Key.A)) {
    TDEngine.ecs.setPosition(player, /*...*/);
  }
});

// Multiplayer: connect to a WebSocket server.
const conn = TDEngine.net.connect('wss://my-server/room');
conn.rpc.registerMethod('ping', (args) => 'pong:' + args[0]);
conn.rpc.callRemote('getUser', [42], 5000)
     .then(user => console.log('got user:', user));

// Rhythm games: start a beat tracker.
TDEngine.beat.start(entityId, 120 /* BPM */, 0.15 /* windowHalfSec */);
TDEngine.beat.setCallback((beatCount) => console.log('beat #' + beatCount));

// Localization: load a locale table + look up keys.
TDEngine.i18n.load('fr', '{"hello":"bonjour"}');
TDEngine.i18n.setLocale('fr');
console.log(TDEngine.i18n.t('hello'));  // "bonjour"

// Scripting: load + call tdscript VM functions.
const handle = TDEngine.script.load('function on_update(dt) end', 'player.lua');
TDEngine.script.call(handle, 'on_update', '[0.016]');
```

### Subsystems at a glance

| Subsystem | Methods | Notes |
|---|---|---|
| `TDEngine.lifecycle` | init, onReady, shutdown, getVersion, isReady, resize | Boot + lifecycle hooks |
| `TDEngine.ecs` | create, destroy, isValid, count, setPosition, getPosition, setVelocity, setSprite, setCollider | ECS entity/component management |
| `TDEngine.input` | isKeyDown, isMouseDown, getMousePos + `Key`/`Mouse` constants | Input polling |
| `TDEngine.beat` | start, stop, isOnBeat, getCount, registerHit, getCombo, setBpm, setCallback, playSound | Rhythm / beat tracker (13 APIs) |
| `TDEngine.script` | load, call, unload | tdscript VM |
| `TDEngine.i18n` | load, setLocale, t, isRtl | Localization |
| `TDEngine.audio` | resume, fillBuffer | Audio |
| `TDEngine.touch` | beginFrame, start, move, end, count, x, y, pinchScale | Multi-touch (8 APIs) |
| `TDEngine.gamepad` | beginFrame, setConnected, setButton, setAnalog, setAxis, buttonPressed, axis | Gamepad (8 APIs) |
| `TDEngine.shaderGraph` | compile | Visual shader graph |
| `TDEngine.net` | Socket, RPC, ServerConfig, connect | WebSocket multiplayer (v=21+) |
| `TDEngine.deprecated` | warn, getRegistry, subscribe, classifyDeprecated | Deprecated API tracking (v=19+) |
| `TDEngine.server` | getCurrentServerUrl, saveServerUrl, resolveAsset, probeServer | Self-host the engine on your own VPN (v=18+) |

The legacy `TDBridge` global still works for backwards compatibility —
direct access logs a one-time deprecation warning pointing users to the
new `TDEngine.*` namespace.

### Self-hosting the engine on your own server

The web player can load engine assets (td-engine.js, td-engine.wasm,
examples/*) from a custom server. Three ways:

1. **Per-visit override**: append `?server=<URL>` to the page URL.
   Example: `https://hacvilke.github.io/td/?server=https://my-vpn.example.com/td/`
2. **Persistent setting**: click the "Server" link in the top bar, enter
   the URL, click "Save & reload". Persists to localStorage.
3. **Programmatic**: `TDServerRouter.saveServerUrl('https://my-server/td/')`
   then reload.

The server must serve over HTTPS (or be localhost) and send
`Access-Control-Allow-Origin: *` for cross-origin WASM + script loading.

### Multiplayer (WebSocket transport)

Click the "Multiplayer" link in the top bar to manage saved server
profiles. Servers are saved per-browser (localStorage) with an optional
"auto-connect on page load" flag.

In code:

```javascript
// Connect to a server
const conn = TDEngine.net.connect('wss://my-server/room', {
  autoReconnect: true,
  maxReconnect: 5,
  reconnectDelayMs: 1000,
});

// Register RPC handlers (other clients can call these)
conn.rpc.registerMethod('getScore', (args) => currentScore);

// Call remote methods (returns a Promise)
conn.rpc.callRemote('getScore', [playerId], 5000)
     .then(score => console.log('remote score:', score))
     .catch(err => console.error('RPC failed:', err));

// Fire-and-forget (no reply expected)
conn.rpc.notify('playerJoined', [playerId]);
```

Wire format (JSON, matches the C++ RpcServer for desktop↔web interop):
- Request: `{id, m, a}` (id=integer, m=method name, a=args array)
- Response: `{id, r}` (success) or `{id, e}` (error message)
- Notify: `{m, a}` (no id = no reply)

### Deprecated API tracking + filter console

The on-page engine console (bottom of the screen) has filter tabs:
**All | Info | Warning | Error | Deprecated**. Each tab shows a count
badge; click to filter. A search box narrows by substring. The Clear
button resets all counts.

To flag a deprecated API from your game code:

```javascript
TDDeprecated.warn('myOldFunction', 'myNewFunction', '2.0');
// Logs: [DEPRECATED] myOldFunction (since v2.0) — use myNewFunction instead [1x]
// Increments a per-API hit counter accessible via TDDeprecated.getRegistry()
```

The C++ engine can also emit `[DEPRECATED] apiName (since vX.Y) — use replacement instead`
via `TD_LOG_WARN` — these are auto-classified into the Deprecated tab.

---

### Legacy `TDBridge` API (still works, with deprecation warning)

```javascript
// Wait for the engine to boot.
TDBridge.onReady(() => {
    const Module = TDBridge.wasmExports;
    const td_create = Module.cwrap('td_create_entity', 'number', ['string']);
    const td_set_pos = Module.cwrap('td_entity_set_position', null, ['number', 'number', 'number']);
    const td_set_spr = Module.cwrap('td_entity_set_sprite', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number']);

    // Create an entity and attach components - the C++ engine handles the rest.
    const player = td_create('Player');
    td_set_pos(player, 100, 100);
    td_set_spr(player, 32, 32, 1, 1, 1, 1);

    // Per-frame game loop in JS. The engine's WASM main loop handles rendering.
    function update() {
        const td_is_key = Module.cwrap('td_is_key_down', 'boolean', ['number']);
        if (td_is_key(0x57 /*W*/)) {
            // ... move player
        }
        requestAnimationFrame(update);
    }
    requestAnimationFrame(update);
});
await TDBridge.init('game-canvas');
```

For TypeScript development, use the same `TDBridge` API (it's typed via JSDoc and works in both plain JS and TS files). See [`web/GETTING_STARTED.md`](web/GETTING_STARTED.md) for the full guide.

```typescript
// Same API works in TS — TDBridge is global, declared via JSDoc.
await TDBridge.init('game-canvas');

const player = TDBridge.createEntity('Player');
TDBridge.setEntityPosition(player, 100, 100);
TDBridge.setEntitySprite(player, 32, 32, 1, 1, 1, 1);

// Run a requestAnimationFrame loop that reads input + updates entities.
function loop() {
    if (TDBridge.isKeyDown(0x44)) {  // 'D' key
        TDBridge.setEntityPosition(player, 100 + performance.now() / 10, 100);
    }
    requestAnimationFrame(loop);
}
requestAnimationFrame(loop);
```

See [`web/examples/voidrunner.js`](web/examples/voidrunner.js) for a complete, working game.

## Features

### Core
- **2D Rendering**: Sprite batching, texture management, camera system
- **3D Rendering**: Mesh rendering, lighting, materials, framebuffers
- **Physics**: AABB collision detection, rigid body dynamics, swept capsule character controller (with sliding, slope limit, step handling, ground detection)
- **Audio**: WAV loading, software mixing, 3D positional audio with HRTF-lite + Schroeder reverb + Doppler pitch shift
- **ECS**: Entity-Component-System architecture with bit-mask queries, **plus** DOTS-style archetype ECS upgrade (contiguous component arrays per archetype, 10-100x cache friendlier)
- **Asset Loading**: PNG decoder, OBJ loader, WAV loader, **plus** Addressables-style async asset catalog with ref counting + LRU eviction
- **Visual Editor**: Scene panel, inspector, asset browser, console (desktop)
- **WebAssembly**: Browser support via Emscripten with zero external libraries
- **Rhythm System**: BPM-synced beat tracker, on-beat detection, combo tracking

### Scripting (NEW)
- **tdscript**: Custom Lua-like bytecode VM (3,075 lines) with lexer, parser, compiler, stack-based interpreter. Full `td.*` library (create_entity, set_position, find_by_name, is_key_down, beat_*, signal connect/emit). Math/string/table stdlib. Per-script sandboxed globals. Hot reload via file mtime polling.
- **Visual Scripting**: Node-based graph that compiles to tdscript source. Events (OnStart, OnUpdate, OnSignal, OnKey), flow control (Branch, Sequence, While, For), actions, math nodes.

### Networking (NEW)
- **Reliable UDP Transport**: Sliding-window ARQ with cumulative + selective ACKs, RTT estimation, fragment reassembly (messages up to MTU*N). Three reliability modes (UNRELIABLE, RELIABLE_UNORDERED, RELIABLE_ORDERED). RPC system with timeout + response.
- **Server-Authoritative Netcode**: ClientPredictor with rewind/replay, ServerReconciler with 20Hz tick broadcast, LagCompensator with 1-second history for hitscan.

### Voxel (NEW)
- **Chunk System**: 16³ or 32³ chunks with three meshing algorithms (naive, greedy Minecraft-style 5-10x fewer triangles, culled).
- **Ambient Occlusion**: Per-vertex AO baked into vertex color.
- **Worldgen**: 4-octave simplex noise (implemented from scratch), terrain heightmap, dirt/grass/stone/water, trees, ore veins.
- **Lighting**: Sunlight propagation + BFS block light (torches).
- **Editing**: setVoxel/getVoxel + DDA voxel raycast.
- **Streaming**: Background-thread chunk streamer with distance-based load/unload + frustum culling.

### UI Toolkit (NEW)
- **13 Widget Types**: Container, Label, Button, Image, Slider, Checkbox, TextInput, ScrollView, ListView (virtualized — 10K items in <1ms), Dropdown, Modal, Tooltip, Canvas.
- **Real Flexbox Layout**: justify-content, align-items, flex-grow/shrink/basis, padding/margin/border, min/max constraints.
- **Input Dispatch**: Hover/focus/click/drag/scroll, tab cycling, drag threshold.
- **Style Inheritance**: Parent font/color propagates to children.
- **Embedded Font**: 96-glyph 8x16 ASCII bitmap font (no external asset).

### Animation (NEW)
- **Skeletal Animation**: Bone hierarchy, AnimationClip with per-bone keyframe tracks (position, rotation, scale), nlerp rotation interpolation, cross-fade between clips.
- **GPU Skinning**: 4-bone influences per vertex, skinning palette computation, GLSL vertex shader included.

### Visual Shader Editor (NEW)
- **Node Graph**: 20+ node types (Time, Math, Texture, Fresnel, Constants, Uniforms, Output).
- **GLSL Codegen**: Generates valid `#version 300 es` GLSL with topological sort + uniform declarations.

### Localization / i18n (NEW)
- **Locale Fallback**: zh-Hant → zh → en chain.
- **Plural Forms**: Cardinal via `_plural` key convention.
- **Named Placeholders**: `Hello, {name}!`.
- **RTL Detection**: Arabic/Hebrew/Farsi/Urdu/Yiddish.
- **JSON Loader**: In-house minimal JSON parser with `\uXXXX` UTF-8 escape support.

### Mobile / XR / Gamepad (NEW)
- **Multi-touch**: 10 simultaneous touches with delta + pressure.
- **Gestures**: Pinch scale recognition.
- **XR Controllers**: 6-DoF poses, 8 buttons, thumbsticks, haptics callback.
- **Standard Gamepad**: 15 buttons + 4 axes with deadzone + just-pressed/just-released edge detection.

### Plugin ABI (NEW, GDExtension-equivalent)
- **Stable C ABI**: Plugins register update/render/shutdown hooks, custom asset importers, custom ECS component types.
- **Cross-Platform**: Win32 LoadLibrary / POSIX dlopen / WASM no-op.
- **Hot Reload**: Re-dlopen + re-init at runtime.

### WebGPU Path (NEW, Tier 4)
- **Scaffolding**: Backend selection (WebGPU / WebGL2 / Auto), device capabilities query, swap chain / buffer / pipeline handles, compute shader API.
- **WGSL Templates**: Triangle, textured quad, particle compute shader.

**Planned** (see [`docs/MODULARITY_ROADMAP.md`](docs/MODULARITY_ROADMAP.md)):
- **Interest management + chunked replication** (Tier 3.2)
- **UGC asset store / marketplace** (Tier 3.4)
- **Relay / NAT-traversal service** (Tier 3.7)
- **Cluster / hosting story** (Tier 3.10)
- **Real WebGPU bindings** via Dawn/wgpu-native (Tier 4.1 — scaffolding in place)

### Test Coverage

348 tests across 6 test binaries; 343/348 passing (98.6%).

| Module | Tests | Pass rate |
|---|---|---|
| Scripting VM (tdscript) | 42 | 100% |
| Network transport + RPC | 90 | 97% |
| Voxel chunk mesher | 74 | 100% |
| UI toolkit | 62 | 100% |
| Character controller | 23 | 100% |
| Wave 2 modules (9 in one test) | 57 | 96% |

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Game Code                             │
│              (C++ on desktop, JS/TS on browser)              │
├─────────────────────────────────────────────────────────────┤
│                       Engine API                             │
├───────────┬───────────┬───────────┬───────────┬─────────────┤
│  Renderer │  Physics  │   Audio   │   ECS     │   Assets    │
├───────────┴───────────┴───────────┴───────────┴─────────────┤
│                    Core (Math, Memory, Logger)               │
├─────────────────────────────────────────────────────────────┤
│              Platform Layer (swappable)                      │
│  Desktop: Win32 + OpenGL 3.3   Browser: Emscripten + WebGL2 │
└─────────────────────────────────────────────────────────────┘
```

## Folder Structure

```
td-engine/
├── src/
│   ├── core/           # Math, memory, logger, game loop
│   ├── platform/       # Win32 window, input handling (desktop)
│   ├── renderer/       # OpenGL 3.3, sprites, meshes, cameras
│   ├── physics/        # AABB, collision, rigid bodies
│   ├── audio/          # WAV loading, mixing, playback
│   ├── assets/         # PNG decoder, OBJ loader
│   └── ecs/            # Entity, component, system, world, beat system
├── editor/             # Visual editor application (desktop)
├── examples/           # Pong and platformer games (C++ desktop)
├── tests/              # Unit tests + regression tests
├── assets/shaders/     # GLSL shader files
├── docs/               # PUBLIC_APIS.md, RHYTHM_MECHANICS.md, MODULARITY_ROADMAP.md
├── wasm/               # WebAssembly bridge
│   ├── emscripten_main.cpp  # Emscripten entry point (replaces win32_*.cpp)
│   ├── js_bridge.js         # Loads td-engine.wasm, sets up canvas + input + audio
│   └── README.md            # Build + run instructions
└── web/                # Browser-facing files
    ├── index.html           # Landing page (hero, features, demo gallery)
    ├── play.html            # Web player (game picker + 6 demos + release ticker)
    ├── docs.html            # Documentation (API reference + guides)
    ├── style.css            # Dark theme + #00D4FF accent
    ├── GETTING_STARTED.md   # 11-section guide for web game developers
    └── examples/
        ├── voidrunner.js    # VOID RUNNER — vertical space shooter
        ├── pong.js          # Pong — classic 2-player paddle game
        ├── beat_demo.js     # BEAT DEMO — rhythm game using the BeatTracker
        └── script_arena.js  # SCRIPT ARENA — tdscript VM + i18n showcase
```

## Building

### Desktop (Windows)

**Requirements**: Windows 10/11, MinGW-w64 or Visual Studio 2019+, CMake 3.15+ (optional).

```bash
# MinGW (make)
make                 # build engine lib + pong + platformer
make editor          # build the visual editor
make clean

# CMake
cmake -B build-desktop
cmake --build build-desktop --config Release
```

### WebAssembly (cross-platform)

**Requirements**: [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html).

```bash
# Make
make web

# CMake
emcmake cmake -B build-web -DTD_BUILD_WEB=ON
cmake --build build-web
```

Produces `web/td-engine.js` (Emscripten glue) + `web/td-engine.wasm` (the compiled engine).

## Quick Start (Desktop C++)

```cpp
#include "platform/platform.h"
#include "platform/win32_window.h"
#include "core/game_loop.h"
#include "renderer/gl_renderer.h"
#include "renderer/sprite_batch.h"
#include "renderer/camera.h"

using namespace td;

Win32Window* window;
SpriteBatch* batch;
Camera2D camera;

void init() {
    Renderer::get().init();
    batch = new SpriteBatch();
    batch->init();
    camera.setViewport(800, 600);
    camera.setPosition(400, 300);
}

void update(float dt) {
    // Game logic here
}

void render(float alpha) {
    (void)alpha;
    Renderer::get().clear(0.1f, 0.1f, 0.15f);
    batch->begin(camera.getProjection(), camera.getView());
    batch->drawQuad(100, 100, 64, 64, 1, 0, 0, 1);
    batch->end();
}

int main() {
    WindowConfig config;
    config.title = "My Game";
    config.width = 800;
    config.height = 600;

    Win32Window win;
    win.create(config);
    window = &win;

    GameLoop loop;
    loop.setCallbacks(init, update, render);
    loop.run(win);
    return 0;
}
```

## Quick Start (Browser JavaScript)

See [`web/examples/voidrunner.js`](web/examples/voidrunner.js) for a complete, working game. The workflow is:

1. Include `td-engine.js` (Emscripten glue) + `wasm/js_bridge.js` (TDBridge) in your HTML.
2. Call `await TDBridge.init('game-canvas')` to boot the engine.
3. Use `Module.cwrap(...)` to get typed JS wrappers for the `td_*` C functions.
4. Run a `requestAnimationFrame` loop that reads input, updates game state in JS, and pushes changes to the engine via the cwrap'd functions.
5. The engine's WASM main loop (driven by `emscripten_set_main_loop`) renders every frame.

For TypeScript, use the same `TDBridge` global — it's declared via JSDoc and works in both plain JS and TS files. See [`web/GETTING_STARTED.md`](web/GETTING_STARTED.md) for the full API reference.

## What does NOT change between desktop and web

The engine's portable subsystems compile **unchanged** on both targets:

- `src/core/` — math, logger, game loop
- `src/renderer/` — OpenGL 3.3 / WebGL 2 (via Emscripten's GL shim)
- `src/physics/` — AABB, collision, rigid bodies
- `src/ecs/` — World, Entity, Component, System, BeatSystem
- `src/audio/mixer.cpp` — software mixer (output device differs)
- `src/assets/` — PNG, OBJ, WAV loaders (Emscripten provides a virtual filesystem)

Three files have small `#ifdef __EMSCRIPTEN__` blocks for platform-specific glue (GL function loading, timestamp, console coloring) — see [`wasm/README.md`](wasm/README.md) for details.

## Documentation

- [`docs/MODULARITY_ROADMAP.md`](docs/MODULARITY_ROADMAP.md) — 3-tier roadmap + implementation status table for every engine module.
- [`docs/RHYTHM_MECHANICS.md`](docs/RHYTHM_MECHANICS.md) — design + implementation of the beat-synced gameplay system.
- [`docs/PUBLIC_APIS.md`](docs/PUBLIC_APIS.md) — how to consume third-party HTTP APIs from a TD Engine web game.
- [`docs/CREDITS.md`](docs/CREDITS.md) — references and credits for outsourced concepts (comparable-engine documentation, design walkthroughs, tooling).
- [`web/GETTING_STARTED.md`](web/GETTING_STARTED.md) — 11-section guide for web game developers writing JavaScript against the engine.

## License

MIT — see [LICENSE](LICENSE).
