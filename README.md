# TD Engine

A complete 2D/3D game engine written from scratch in C/C++ with zero external dependencies, plus a TypeScript port that runs in any modern browser.

## Status

| Platform | Status | Path |
|----------|--------|------|
| **Native (Windows)** | C++ source — builds with MinGW / Visual Studio | `src/`, `editor/`, `examples/` |
| **Browser (WebGL2)** | TypeScript port + complete game | `web/engine/`, `web/game/`, `src/` |

## Browser Quick Start

```bash
npm install
npm run dev      # Vite dev server
npm run build    # single-file production build to dist/
npm run preview  # preview the production build
```

Then open the printed URL and click **Play Pong:Rush**. The game runs entirely client-side on the engine's TypeScript port — ECS, SpriteBatch, AABB physics, particles, AI.

## Bug fix — WASM bridge

The original `wasm/js_bridge.js` shipped with a `_loadWASM()` method that never actually loaded a WASM module — it just simulated a progress bar and stored the config object as `this._module`. Every `_td_init` / `_td_update` call was therefore a silent no-op and the browser canvas stayed blank.

We replaced the stub with a real TypeScript implementation of the engine's public API under `web/engine/`:

```
web/engine/
├── math.ts       # Vec2/Vec3/Vec4/Mat4/Color — 1:1 port of src/core/math/
├── ecs.ts        # World / Entity / Component / System — port of src/ecs/
├── renderer.ts   # WebGL2 SpriteBatch — port of src/renderer/sprite_batch.{h,cpp}
├── camera.ts     # Camera2D — port of src/renderer/camera.{h,cpp}
├── input.ts      # Input + Key enum — port of src/platform/win32_input.{h,cpp}
└── engine.ts     # GameLoop + top-level Engine — port of src/core/game_loop.{h,cpp}
```

The C++ engine in `src/` is preserved for native builds; the TypeScript port mirrors its architecture so the same game logic can target both.

## Features

- **2D Rendering**: Sprite batching, texture management, camera system
- **3D Rendering**: Mesh rendering, lighting, materials, framebuffers
- **Physics**: AABB collision detection, rigid body dynamics, spatial hashing
- **Audio**: WAV loading, software mixing, waveOut API
- **Networking**: TCP/UDP via Winsock2, client/server architecture, interpolation
- **ECS**: Entity-Component-System architecture for game objects
- **TD Scripting**: Custom scripting language with lexer, parser, compiler, and VM
- **Asset Loading**: PNG decoder, OBJ loader, asset caching
- **Visual Editor**: Scene panel, inspector, asset browser, console
- **WebAssembly**: Browser support via Emscripten

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Game Code                             │
├─────────────────────────────────────────────────────────────┤
│                       Engine API                             │
├───────────┬───────────┬───────────┬───────────┬─────────────┤
│  Renderer │  Physics  │   Audio   │ Networking│   Assets    │
├───────────┴───────────┴───────────┴───────────┴─────────────┤
│                    Core (Math, Memory, Logger)               │
├─────────────────────────────────────────────────────────────┤
│                    Platform (Win32, OpenGL)                  │
└─────────────────────────────────────────────────────────────┘
```

## Folder Structure

```
td-engine/
├── src/
│   ├── core/           # Math, memory, logger, game loop
│   ├── platform/       # Win32 window, input handling
│   ├── renderer/       # OpenGL 3.3, sprites, meshes, cameras
│   ├── physics/        # AABB, collision, rigid bodies
│   ├── audio/          # WAV loading, mixing, playback
│   ├── net/            # Sockets, server, client
│   ├── assets/         # PNG decoder, OBJ loader
│   ├── ecs/            # Entity, component, system, world
│   └── td/             # Scripting language
├── editor/             # Visual editor application
├── examples/           # Pong and platformer games
├── tests/              # Unit tests
├── assets/shaders/     # GLSL shader files
├── wasm/               # WebAssembly/Emscripten support
└── web/                # HTML5 launcher
```

## Building

### Requirements

- Windows 10/11
- MinGW-w64 or Visual Studio 2019+
- CMake 3.15+ (optional)

### Using MinGW (make)

```bash
# Build everything
make

# Build debug version
make debug

# Run examples
make run-pong
make run-platformer

# Clean
make clean
```

### Using CMake

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### Using Visual Studio

Open `CMakeLists.txt` with Visual Studio 2019+ which has built-in CMake support.

## Quick Start

```cpp
#include "td/platform/win32_window.h"
#include "td/core/game_loop.h"
#include "td/renderer/gl_renderer.h"
#include "td/renderer/sprite_batch.h"

using namespace td;

Win32Window* window;
SpriteBatch* batch;

void init() {
    Renderer::get().init();
    batch = new SpriteBatch();
    batch->init();
}

void update(float dt) {
    // Game logic here
}

void render(float alpha) {
    Renderer::get().clear(0.1f, 0.1f, 0.1f);
    
    Mat4 proj = Mat4::orthographic(0, 800, 600, 0, -1, 1);
    batch->begin(proj);
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

## TD Scripting Language

TD is a simple scripting language for game logic:

```td
// Entity definition
entity Player {
    let health: int = 100;
    let speed: float = 200.0;
    
    fn update(dt: float) {
        if input.key("left") {
            this.x -= speed * dt;
        }
        if input.key("right") {
            this.x += speed * dt;
        }
    }
    
    fn onCollision(other: Entity) {
        if other.tag == "enemy" {
            health -= 10;
        }
    }
}
```

### Keywords

`let`, `fn`, `if`, `else`, `while`, `for`, `return`, `true`, `false`, `null`, `struct`, `entity`, `this`

### Types

`int`, `float`, `string`, `bool`, `void`

### Operators

`+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`, `!`

## Networking

### Server

```cpp
#include "td/net/server.h"

Server server;
server.start(7777);

server.setClientConnectCallback([](uint8_t playerId) {
    printf("Player %d connected\n", playerId);
});

while (server.isRunning()) {
    server.update(1.0f / 60.0f);
}
```

### Client

```cpp
#include "td/net/client.h"

Client client;
client.connect("127.0.0.1", 7777);

while (client.isConnected()) {
    client.update(1.0f / 60.0f);
    
    ClientInput input;
    input.keys[0] = window->input.keys[Key::Left];
    input.keys[1] = window->input.keys[Key::Right];
    client.sendInput(input);
}
```

## License

MIT License - see LICENSE file for details.

## Acknowledgments

- OpenGL for graphics rendering
- Microsoft for Win32 API documentation
- RFC 1950/1951 for PNG/zlib decompression algorithms
