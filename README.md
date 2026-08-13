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

The web player (`web/index.html`) loads `td-engine.wasm` and runs a complete Pong game written in pure JavaScript (`web/examples/pong.js`) that drives the C++ engine via `TDBridge`.

## Writing a web game in JavaScript

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

For TypeScript development, import the typed wrapper:

```typescript
import { TDEngine, Key } from './web/engine-wrapper';

const engine = new TDEngine();
await engine.init('game-canvas');

const player = engine.createEntity('Player');
player.setPosition(100, 100);
player.setSprite(32, 32, 1, 1, 1, 1);

engine.onUpdate((dt, input) => {
    if (input.isKeyDown(Key.D)) player.setPosition(player.x + dt * 200, player.y);
});
```

See [`web/examples/pong.js`](web/examples/pong.js) for a complete, working game.

## Features

- **2D Rendering**: Sprite batching, texture management, camera system
- **3D Rendering**: Mesh rendering, lighting, materials, framebuffers
- **Physics**: AABB collision detection, rigid body dynamics
- **Audio**: WAV loading, software mixing (Web Audio on browser)
- **Networking**: TCP/UDP via Winsock2 (WebSocket on browser via JS bridge)
- **ECS**: Entity-Component-System architecture with bit-mask queries
- **TD Scripting**: Custom scripting language with lexer, parser, compiler, and VM
- **Asset Loading**: PNG decoder, OBJ loader, WAV loader
- **Visual Editor**: Scene panel, inspector, asset browser, console (desktop)
- **WebAssembly**: Browser support via Emscripten with zero external libraries

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Game Code                             │
│              (C++ on desktop, JS/TS on browser)              │
├─────────────────────────────────────────────────────────────┤
│                       Engine API                             │
├───────────┬───────────┬───────────┬───────────┬─────────────┤
│  Renderer │  Physics  │   Audio   │ Networking│   Assets    │
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
│   ├── net/            # Sockets, server, client (desktop)
│   ├── assets/         # PNG decoder, OBJ loader
│   ├── ecs/            # Entity, component, system, world
│   └── td/             # Scripting language
├── editor/             # Visual editor application (desktop)
├── examples/           # Pong and platformer games (C++ desktop)
├── tests/              # Unit tests
├── assets/shaders/     # GLSL shader files
├── wasm/               # WebAssembly bridge (Part 7)
│   ├── emscripten_main.cpp  # Emscripten entry point (replaces win32_*.cpp)
│   ├── js_bridge.js         # Loads td-engine.wasm, sets up canvas + input + audio
│   └── README.md            # Build + run instructions
└── web/                # Browser-facing files
    ├── index.html           # Standalone web player
    ├── style.css            # Dark theme + #00D4FF accent
    ├── engine-wrapper.ts    # TypeScript public API for web game devs
    └── examples/
        └── pong.js          # Complete Pong game in pure JS
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

See [`web/examples/pong.js`](web/examples/pong.js) for a complete, working game. The workflow is:

1. Include `td-engine.js` (Emscripten glue) + `wasm/js_bridge.js` (TDBridge) in your HTML.
2. Call `await TDBridge.init('game-canvas')` to boot the engine.
3. Use `Module.cwrap(...)` to get typed JS wrappers for the `td_*` C functions.
4. Run a `requestAnimationFrame` loop that reads input, updates game state in JS, and pushes changes to the engine via the cwrap'd functions.
5. The engine's WASM main loop (driven by `emscripten_set_main_loop`) renders every frame.

For TypeScript, import `TDEngine` from [`web/engine-wrapper.ts`](web/engine-wrapper.ts) for a clean, typed API with `EntityHandle`, `InputState`, and `Key` enums.

## What does NOT change between desktop and web

The engine's portable subsystems compile **unchanged** on both targets:

- `src/core/` — math, logger, game loop
- `src/renderer/` — OpenGL 3.3 / WebGL 2 (via Emscripten's GL shim)
- `src/physics/` — AABB, collision, rigid bodies
- `src/ecs/` — World, Entity, Component, System
- `src/audio/mixer.cpp` — software mixer (output device differs)
- `src/assets/` — PNG, OBJ, WAV loaders (Emscripten provides a virtual filesystem)
- `src/td/` — scripting language VM

Three files have small `#ifdef __EMSCRIPTEN__` blocks for platform-specific glue (GL function loading, timestamp, console coloring) — see [`wasm/README.md`](wasm/README.md) for details.

## License

MIT — see [LICENSE](LICENSE).
