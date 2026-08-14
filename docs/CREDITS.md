# Credits & References

TD Engine is built from scratch in C/C++ with zero external runtime
libraries. The engine's design and feature roadmap do, however, draw
inspiration from public documentation, conference talks, and community
resources for comparable engines. This file collects those references
in one place so the rest of the engine docs can stay focused on TD
Engine's own architecture and APIs.

## Rhythm mechanics

The two-half-window on-beat tolerance trick (used in
`src/ecs/beat_system.cpp` to keep the upper bound of the beat window
working after `nextBeat` advances) and the loop-point hard-reset trick
(used to prevent floating-point drift across multiple song loops) were
studied from a public game-dev walkthrough of building a BPM-synced
metronome + on-beat attack bonus for a rhythm game. The implementation
in TD Engine is original C++, but the two debugging insights above are
due to that walkthrough.

- "My Personal Process for Implementing Game Mechanics (PPAR)" — ~16-min
  walkthrough of a BPM-synced metronome + on-beat attack bonus.
  <https://youtu.be/j3f8xOv2Tpg>

## Comparable-engine documentation

TD Engine's `docs/MODULARITY_ROADMAP.md` discusses how its feature
surface compares to other production engines so contributors can see at
a glance what's already shipped and what's still on the roadmap. The
official documentation for the engines below was consulted while
drafting that comparison.

### Unity
- Unity Manual (ECS / DOTS, Scriptable Render Pipeline, Addressables,
  Netcode for GameObjects, UI Toolkit, Profiler, Prefabs):
  <https://docs.unity3d.com/>
- Unity Packages reference (com.unity.entities, com.unity.transport,
  com.unity.addressables, com.unity.shadergraph):
  <https://docs.unity3d.com/packages/>

### Godot
- Godot Documentation (Nodes / Scenes, GDExtension, RenderingDevice,
  Multiplayer, Asset Library, Animation):
  <https://docs.godotengine.org/>
- Godot asset pipeline + GDScript reference:
  <https://docs.godotengine.org/en/stable/tutorials/index.html>

The feature-comparison tables in `docs/MODULARITY_ROADMAP.md` reflect
what those docs document as of the page-fetch dates listed inline in
that file. The implementation of every TD Engine module discussed
there is original C++, written from scratch under `src/`.

## Tooling

- **Emscripten** — compiles the shared C++ source to WebAssembly +
  generates the `td-engine.js` glue. <https://emscripten.org/>
- **CMake + Ninja** — native build system (Windows / MSVC).
  <https://cmake.org/> · <https://ninja-build.org/>
- **GitHub Actions** — CI builds the WASM bundle + Windows binary on
  every push, deploys the web player to GitHub Pages, and ships a
  rolling `latest` Windows release.
  <https://docs.github.com/en/actions>
- **GitHub Pages** — hosts the live web player at
  <https://hacvilke.github.io/td/>.

## Demo assets

The four sample games shipped under `web/examples/` are original
JavaScript written for TD Engine:

- `voidrunner.js` — vertical space shooter (2D sprites + audio)
- `pong.js` — classic paddle game (ECS + physics)
- `beat_demo.js` — rhythm game (audio + beat system)
- `script_arena.js` — tdscript VM + i18n showcase (Wave 1/2 modules)

No third-party sprites, audio samples, or fonts are bundled with the
engine. Demo graphics are drawn procedurally via the engine's sprite
batcher; demo audio is synthesized in-engine via the Web Audio bridge.

## License

TD Engine source is released under the MIT License — see `LICENSE`.
The third-party documentation linked above remains the property of its
respective owners and is referenced here for attribution only.
