# TD Engine - WebAssembly Bridge (Part 7)

This directory contains the Emscripten entry point that swaps the Win32
platform layer for the browser. The rest of the C++ engine (renderer,
physics, ECS, audio mixer, scripting) is compiled **unchanged** - only the
platform layer is replaced by `emscripten_main.cpp`.

## Files

| File | Role |
|------|------|
| `emscripten_main.cpp` | Emscripten entry point. Replaces `src/platform/win32_window.cpp` + `win32_input.cpp`. Exports the `td_*` C API that JS calls into. |
| `js_bridge.js` | Loads the `td-engine.wasm` module (via Emscripten's `td-engine.js` glue), sets up WebGL2 canvas, forwards browser input, and runs the Web Audio bridge. Exposes `window.TDBridge`. |

The TypeScript public API lives at [`../web/engine-wrapper.ts`](../web/engine-wrapper.ts) - that's the file game developers import.

## Build

### Prerequisites

1. Install the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html):
   ```sh
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   ./emsdk install latest
   ./emsdk activate latest
   source ./emsdk_env.sh
   ```

2. Verify `emcc` is on your PATH:
   ```sh
   emcc --version
   ```

### Build with Make

```sh
make web
```

Produces:
- `web/td-engine.js` - Emscripten glue (loaded by `web/index.html`)
- `web/td-engine.wasm` - the compiled engine binary

### Build with CMake

```sh
emcmake cmake -B build-web -DTD_BUILD_WEB=ON
cmake --build build-web
```

Output goes to `web/td-engine.js` + `web/td-engine.wasm`.

## Run

The web player needs to be served over HTTP (not `file://`) because
WebAssembly `fetch()` is blocked on `file://` URLs.

```sh
cd web
python3 -m http.server 8000
# open http://localhost:8000 in a browser
```

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│ Browser                                                 │
│                                                         │
│  ┌──────────────┐   ┌──────────────────────────────┐   │
│  │ web/         │   │ wasm/js_bridge.js            │   │
│  │ index.html   ├───┤  TDBridge.init(canvas)       │   │
│  │ style.css    │   │  - canvas.getContext('webgl2')│   │
│  │ examples/    │   │  - load td-engine.js glue    │   │
│  │   voidrunner │   │  - forward key/mouse events  │   │
│  │ engine-      │   │  - Web Audio ScriptProcessor │   │
│  │ wrapper.ts   │   │  - exposes window.TDBridge   │   │
│  └──────────────┘   └──────────────┬───────────────┘   │
│                                    │                    │
│                     Emscripten glue│td-engine.js        │
│                                    │                    │
│                     ┌──────────────▼───────────────┐   │
│                     │ td-engine.wasm                │   │
│                     │  (compiled C++ engine)        │   │
│                     │                               │   │
│                     │  src/renderer/  (WebGL2)      │   │
│                     │  src/physics/   (AABB)        │   │
│                     │  src/ecs/       (World)       │   │
│                     │  src/audio/     (Mixer)       │   │
│                     │  src/td/        (scripting)   │   │
│                     │  wasm/emscripten_main.cpp     │   │
│                     │   (platform layer swap-in)    │   │
│                     └───────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

## What's excluded from the WASM build (and why)

| File | Reason | Replacement |
|------|--------|-------------|
| `src/platform/win32_window.cpp` | Win32 `CreateWindow` + `wglCreateContext` | Browser canvas + `getContext('webgl2')` (in `js_bridge.js`) |
| `src/platform/win32_input.cpp` | Win32 message pump | Emscripten HTML5 callbacks (`emscripten_set_keydown_callback`, etc.) |
| `src/audio/audio_engine.cpp` | `waveOut` API | Web Audio API (`AudioContext` + `ScriptProcessor` in `js_bridge.js`); the C++ `Mixer` is still used - `td_fill_audio_buffer` calls `Mixer::mix()` |
| `src/net/socket.cpp` | Winsock2 | WebSocket (browser-side). A future `#ifdef __EMSCRIPTEN__` branch in `socket.cpp` could use `emscripten_websocket_*` directly. |

## What's #ifdef'd (and why)

Three engine `.cpp` files have small `#ifdef __EMSCRIPTEN__` blocks because they contain platform-specific code that has no portable equivalent:

| File | What's #ifdef'd |
|------|-----------------|
| `src/renderer/gl_renderer.cpp` | `loadGLFunctions()` - desktop uses `wglGetProcAddress` + `GetProcAddress(opengl32.dll)`; Emscripten provides GL symbols directly from `<GLES3/gl3.h>`. |
| `src/renderer/framebuffer.cpp` | `readPixels()` - desktop loads `glReadPixels` from `opengl32.dll`; Emscripten calls `::glReadPixels` directly. |
| `src/core/logger.cpp` | `getTimestamp()` - desktop uses `GetLocalTime(SYSTEMTIME*)`; Emscripten uses POSIX `gettimeofday` + `localtime_r`. Console coloring: desktop uses `SetConsoleTextAttribute`; Emscripten uses ANSI escape codes. |

The desktop path is **completely unchanged** - the `#ifdef` only adds an Emscripten branch.

## The C API (exported to JS)

Every `EMSCRIPTEN_KEEPALIVE` function in `emscripten_main.cpp` is callable from JavaScript via `Module.ccall` or `Module.cwrap`:

```javascript
const td_init    = Module.cwrap('td_init',         null,   ['number', 'number']);
const td_create  = Module.cwrap('td_create_entity','number',['string']);
const td_set_pos = Module.cwrap('td_entity_set_position', null,
                                ['number', 'number', 'number']);
```

| Function | Signature | Purpose |
|----------|-----------|---------|
| `td_init` | `(int w, int h) -> void` | Boot the engine (creates renderer, camera, world, mixer) |
| `td_shutdown` | `() -> void` | Tear down |
| `td_load_scene` | `(const char* text) -> void` | Parse + load a scene into the ECS World |
| `td_set_key_state` | `(int vk, bool pressed) -> void` | Inject a key event (Win32 VK code) |
| `td_set_mouse_state` | `(float x, float y, bool l, bool r) -> void` | Inject mouse position + buttons |
| `td_resize` | `(int w, int h) -> void` | Viewport resize |
| `td_get_version` | `() -> const char*` | Returns `"TD Engine 1.0.0 (WebAssembly)"` |
| `td_fill_audio_buffer` | `(int16_t* out, int n) -> void` | Fill a stereo PCM buffer |
| `td_create_entity` | `(const char* name) -> uint32_t` | Create an entity, return its id |
| `td_entity_set_position` | `(uint32_t id, float x, float y) -> void` | Set PositionComponent |
| `td_entity_get_position` | `(uint32_t id, float* x, float* y) -> void` | Read PositionComponent |
| `td_entity_set_velocity` | `(uint32_t id, float vx, float vy) -> void` | Set VelocityComponent |
| `td_entity_set_sprite` | `(uint32_t id, float w, float h, float r, g, b, a) -> void` | Attach SpriteComponent |
| `td_entity_set_collider` | `(uint32_t id, float w, float h) -> void` | Attach ColliderComponent |
| `td_entity_destroy` | `(uint32_t id) -> void` | Destroy an entity |
| `td_entity_is_valid` | `(uint32_t id) -> bool` | Check existence |
| `td_get_entity_count` | `() -> int` | World entity count |
| `td_is_key_down` | `(int vk) -> bool` | Query input mirror |
| `td_is_mouse_down` | `(int btn) -> bool` | Query input mirror |
| `td_get_mouse_pos` | `(float* x, float* y) -> void` | Query input mirror |
| `td_render_frame` | `() -> void` | Manually render one frame (advanced) |
| `td_set_callbacks` | `(init, update, render, shutdown) -> void` | Register C function pointers (advanced) |

## Writing a game in JavaScript

See [`web/examples/voidrunner.js`](../web/examples/voidrunner.js) for a complete space shooter game written in pure JS that runs on the C++ engine via WASM. The workflow:

1. Wait for `TDBridge.onReady()`.
2. Use `Module.cwrap('td_create_entity', ...)` to create entities.
3. Use `td_entity_set_position`, `td_entity_set_sprite`, etc. to configure them.
4. Run a `requestAnimationFrame` loop that reads input via `td_is_key_down`, updates game logic in JS, and pushes position/velocity changes back to the engine.
5. The engine's WASM main loop (driven by `emscripten_set_main_loop`) handles rendering every frame.

For TypeScript development, import `TDEngine` from [`web/engine-wrapper.ts`](../web/engine-wrapper.ts) for a clean, typed API.
