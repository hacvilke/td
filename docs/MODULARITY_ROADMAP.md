# TD Engine — Unity & Godot Feature Research for Roblox-like + Minecraft-like Roadmap

**Task ID:** `16-research`
**Author:** research sub-agent
**Date:** 2025
**Goal:** Survey Unity (docs.unity3d.com) and Godot (docs.godotengine.org) official documentation to identify the systems TD Engine must add in order to eventually support Roblox-style UGC + multiplayer + scripting and Minecraft-style voxel worlds / chunk streaming / heavy simulation.

---

## 0. Implementation Status (updated post-gauntlet)

After the Tier 1/2/3 gauntlet pass, the API surface for every workstream below
is now in place. Modules marked **SHIPPED** have working implementations;
modules marked **SKELETON** have a clean API + stubs that compile + link on
both desktop and WASM, with the real algorithm left as TODO. Gameplay code
can target every API today; SKELETON modules graduate to SHIPPED by replacing
the stub body without changing the header.

| # | Workstream | Tier | Status | Source file |
|---|---|---|---|---|
| 1.1 | Scene graph / node hierarchy | 1 | **SHIPPED** | `src/scene/scene.h` |
| 1.2 | Serialization format (.tdscene JSON) | 1 | **SHIPPED** (writer) / **SKELETON** (reader) | `src/serialization/serializer.h` |
| 1.3 | Lua scripting VM + hot reload | 1 | **SKELETON** | `src/scripting/script_vm.{h,cpp}` |
| 1.4 | Signals / event bus | 1 | **SHIPPED** | `src/core/signal.h` |
| 1.5 | Network transport + RPC | 1 | **SKELETON** (interface only) | `src/net/transport.h` |
| 1.6 | Profiler v1 | 1 | **SHIPPED** | `src/core/profiler.h` |
| 1.7 | Asset browser + importers v1 | 1 | TODO | (replace `editor/asset_browser.cpp`) |
| 2.1 | UI toolkit v1 | 2 | **SKELETON** (layout + hit-test; draw stub) | `src/ui/ui.h` |
| 2.2 | 3D character controller | 2 | **SKELETON** (swept collision TODO) | `src/physics/character_controller.h` |
| 2.3 | Voxel chunk system v1 | 2 | **SKELETON** (chunk storage + streamer; meshing TODO) | `src/voxel/chunk.h` |
| 2.4 | GPU skinning / animation | 2 | TODO | — |
| 2.5 | Asset catalog / Addressables | 2 | TODO | — |
| 2.6 | Native plugin ABI (GDExtension-equivalent) | 2 | TODO | — |
| 2.7 | 3D positional audio | 2 | TODO | (extend `src/audio/mixer.{h,cpp}`) |
| 2.8 | Visual shader editor v1 | 2 | TODO | — |
| 3.1 | Server-authoritative netcode + prediction | 3 | **SKELETON** (interfaces + replay buffer) | `src/net/server_authoritative.h` |
| 3.2 | Interest management + chunked replication | 3 | TODO | — |
| 3.3 | Script sandboxing + UGC permissions | 3 | TODO | (depends on 1.3 Lua VM) |
| 3.4 | UGC asset store / marketplace | 3 | TODO | — |
| 3.5 | Visual scripting graph | 3 | TODO | — |
| 3.6 | DOTS-style archetype ECS upgrade | 3 | TODO | — |
| 3.7 | Relay / NAT-traversal service | 3 | TODO | — |
| 3.8 | GPU-driven rendering + greedy meshing | 3 | TODO | — |
| 3.9 | Localization, XR, mobile touch | 3 | TODO | — |
| 3.10 | Cluster / hosting story | 3 | TODO | — |

Also shipped alongside the gauntlet:
- **Component slot leak fix** — `World::removeComponent<T>()` now does a
  swap-back pop, reclaiming the slot in the component array. Previously,
  removeComponent left the slot in the array and never decremented the count,
  so after 10000 add/remove cycles the array was full and addComponent
  returned nullptr. Regression test: `tests/test_slot_leak.cpp`.
- **4 new ECS component types** — HierarchyComponent, LocalTransformComponent,
  WorldTransformComponent (1.1), LuaScriptComponent (1.3).
- **Critical WASM loader bug fix** — the live site was broken because
  `td-engine.js` was loaded twice (once statically, once dynamically by the
  bridge), causing `Uncaught SyntaxError: Identifier 'EmscriptenEH' has
  already been declared`. Fixed by removing the static `<script>` tag and
  having the bridge set `global.Module` BEFORE dynamically injecting the
  glue script. See commit `1d46e68`.
- **Boat logo** — the TD brand mark is now an SVG with T upright as the
  sail and D rotated 90° CCW as the boat hull, with a gentle ±3° rocking
  animation.

The next priority is to fill in the SKELETONs in priority order: Lua VM
(1.3) → network transport (1.5) → voxel mesher (2.3) → UI draw bridge (2.1).

---

## 1. Executive Summary

TD Engine already has a respectable foundation: an ECS World, OpenGL 3.3 + WebGL/WASM renderer, basic 2D physics, an ImGui editor, and a ~50-function JS bridge. What it is **missing** vs. Unity and Godot is the entire "production stack": a node/scene hierarchy, a serializable asset format (Unity `.prefab` / Godot `.tres`), a high-level networking layer, a scripting VM with hot reload, a UI toolkit, a real profiler, and a voxel/chunk-streaming layer.

The research below is organized engine-by-engine, then synthesized into a 3-tier roadmap. The headline conclusion is:

> **Tier 1 (Foundation, 0–6 months)** must deliver **scene-graph + serialization + Lua scripting + hot reload + a network transport with RPC**. Without these five, neither a Roblox-like nor a Minecraft-like is possible.
>
> **Tier 2 (Productivity, 6–12 months)** adds **UI toolkit, asset browser/importers, 3D rigidbody physics, GPU skinning/animation, profiler, and a basic voxel chunk system**.
>
> **Tier 3 (Advanced, 12+ months)** adds **server-authoritative netcode with client prediction, asset streaming / Addressables-style runtime loading, shader graph, visual scripting, and a UGC sandbox + asset store**.

---

## 2. TD Engine — Current State Recap

| Area | Status |
|------|--------|
| ECS | ✅ `World`, `Position/Velocity/Sprite/Collider/RigidBody/BeatTracker` components; `Movement/Collision/Physics/Render/Beat` systems (flat, no hierarchy) |
| Renderer | ✅ OpenGL 3.3 desktop + WebGL/WASM via Emscripten; `SpriteBatch`; basic 3D mesh; 2D/3D camera |
| Audio | ✅ Mixer (pitch/volume/looping); ❌ no 3D positional audio |
| Physics | ✅ AABB collision + custom RigidBody integration; ❌ no 3D dynamics, no joints, no character controller |
| WASM bridge | ✅ `cwrap` + ~50 exported `td_*` functions |
| Build | ✅ Makefile (WASM) + CMake (Windows MSVC) |
| Sample games | ✅ VOID RUNNER, Pong, Beat Demo |
| Editor | ✅ Native ImGui-based |
| Networking | ❌ (socket.h/.cpp stubs only) |
| Scripting | ❌ (no Lua/Python/C# binding) — *note: a custom `td` language exists in `src/td/` (lexer/parser/vm), but it is not exposed to the engine as a gameplay scripting layer* |
| Asset pipeline / serialization | ❌ (no `.prefab` / `.tres` equivalent) |
| Scene graph / node hierarchy | ❌ (entities are flat) |
| Hot reload | ❌ |
| UI system | ❌ (no Canvas / DOM / IMGUI exposed to JS) |
| 3D physics | ❌ |
| Voxel support | ❌ |
| GPU skinning / animation | ❌ |
| Asset browser / importers | ❌ |

---

## 3. Unity Survey

All URLs below were fetched from `docs.unity3d.com` via web-search + web-reader.

### 3.1 DOTS / ECS architecture
- **What it does:** Unity's Data-Oriented Technology Stack. `com.unity.entities` provides a data-oriented ECS that separates identity (entities), data (components) and behavior (systems); `Entities Graphics` bridges it to the renderer; **SubScenes** trigger baking + streaming of a referenced scene for chunk-based world streaming.
- **Why it matters:** A Minecraft-like world has millions of voxel-block entities; a flat object-oriented model cannot iterate them. TD Engine's ECS is already aligned with this idea — but Unity's *archetypes + chunk streaming* is the missing piece for huge worlds.
- **Complexity:** **High** (archetype storage, query planner, baking, SubScene streaming).
- **Sources:**
  - https://docs.unity3d.com/6000.3/Documentation/Manual/ECSFeature.html
  - https://docs.unity3d.com/Packages/com.unity.entities@1.0/manual/index.html
  - https://docs.unity3d.com/Packages/com.unity.entities@1.0/manual/conversion-subscenes.html
  - https://docs.unity3d.com/Packages/com.unity.entities.graphics@1.0/manual/overview.html

### 3.2 Scriptable Render Pipeline (URP / HDRP)
- **What it does:** SRP lets you write the render loop in C#. URP targets mobile/cross-platform; HDRP targets AAA desktop/console. Shader Graph provides a node-based shader editor that compiles to HLSL.
- **Why it matters:** A Roblox-like needs a single renderer that scales from web/low-end phones to high-end desktop; a Minecraft-like benefits from custom block-lighting and post-processing passes.
- **Complexity:** **High** — but a stripped-down "scriptable" pass list (vs. Unity's full SRP) is **Medium**.
- **Sources:**
  - https://docs.unity3d.com/6000.5/Documentation/Manual/SRP.html
  - https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@latest
  - https://docs.unity3d.com/Packages/com.unity.shadergraph@14.0/manual/Shader-Graph-Sample-Feature-Examples.html

### 3.3 Package Manager / Addressables / Asset Store
- **What it does:** Package Manager distributes engine modules; **Addressables** provides runtime asset loading by address (instead of path), asset groups, content build system, async load/unload; the Asset Store is the UGC marketplace.
- **Why it matters:** A Roblox-like *is* a content marketplace; users upload meshes, scripts, audio — they must be addressable, versionable, sandboxed and loadable on demand. Critical for Minecraft-style chunk streaming too (chunks are addressable asset groups).
- **Complexity:** **High** (catalog system, async loading, dependency graphs, remote updates).
- **Sources:**
  - https://docs.unity3d.com/Packages/com.unity.addressables@latest
  - https://docs.unity3d.com/6000.5/Documentation/Manual/com.unity.visualscripting.html (PM overview)

### 3.4 Netcode for GameObjects + Netcode for Entities + Unity Transport
- **What it does:** Three-layer multiplayer stack. `Unity Transport` is the low-level UDP/relay backbone; `Netcode for GameObjects` (NGO) is the high-level server-authoritative SDK for the GameObject workflow (NetworkObject, NetworkBehaviour, NetworkManager, player prefabs, spawning/despawning, object pooling, visibility, client-side interpolation, client anticipation); `Netcode for Entities` brings the same to DOTS with server-authoritative + client prediction. **Unity Relay** handles NAT traversal and WebGL distributed-authority quickstart.
- **Why it matters:** A Roblox-like is inherently multiplayer with thousands of concurrent rooms; a Minecraft-like is server-authoritative with chunk streaming and client prediction. TD Engine currently has only `src/net/socket.{h,cpp}` stubs.
- **Complexity:** **High** — transport (medium) + RPC/serialization (medium) + interest management + prediction/reconciliation (very high).
- **Sources:**
  - https://docs.unity3d.com/Packages/com.unity.netcode.gameobjects@latest
  - https://docs.unity3d.com/6000.5/Documentation/Manual/com.unity.netcode.gameobjects.html
  - https://docs.unity3d.com/Packages/com.unity.netcode@latest
  - https://docs.unity3d.com/Packages/com.unity.transport@latest
  - https://docs.unity3d.com/Packages/com.unity.netcode.gameobjects@2.5/manual/tutorials/get-started-with-ngo.html

### 3.5 UI Toolkit vs uGUI vs IMGUI
- **What it does:** Three coexisting UI systems. **IMGUI** is immediate-mode, editor-only; **uGUI** is the legacy Canvas-based runtime UI; **UI Toolkit** is the modern HTML/CSS-like (UXML + USS) system with retained-mode layout, runtime + editor, world-space UI, and an event system.
- **Why it matters:** Roblox's UI is one of its strongest UGC surfaces (every game has custom HUDs/menus). TD Engine exposes ImGui to the native editor but nothing to JS/game scripts — this is a top-tier gap.
- **Complexity:** **Medium** for an immediate-mode API exposed to JS; **High** for a full retained-mode UI Toolkit with layout engine + style sheets.
- **Sources:**
  - https://docs.unity3d.com/6000.5/Documentation/Manual/UI-systems.html
  - https://docs.unity3d.com/6000.5/Documentation/Manual/com.unity.visualscripting.html (UI Toolkit section)

### 3.6 Scripting (C# MonoBehaviour) + hot reload
- **What it does:** C# is the gameplay language; `MonoBehaviour` provides `Start/Update/OnEnable/...` lifecycle callbacks; the editor watches script files and **recompiles + reloads assemblies** while keeping scene state ("domain reload" can be disabled via "Configurable Enter Play Mode"). Visual Scripting is a node-based alternative.
- **Why it matters:** Hot reload is the single biggest productivity feature for a UGC platform — Roblox Studio reloads scripts every save. TD Engine currently requires a full rebuild for any gameplay change.
- **Complexity:** **High** for a full C# integration (Mono/CoreCLR hosting); **Medium** for Lua/Wren/sandboxed-JS hot reload.
- **Sources:**
  - https://docs.unity3d.com/6000.5/Documentation/Manual/ConfigurableEnterPlayMode.html
  - https://docs.unity3d.com/6000.5/Documentation/Manual/DomainReloading.html
  - https://docs.unity3d.com/6000.5/Documentation/Manual/com.unity.visualscripting.html

### 3.7 Asset serialization / Prefabs / ScriptableObjects
- **What it does:** **Prefabs** are reusable GameObject templates supporting nesting, variants and overrides; **ScriptableObjects** are data-only assets; the serializer handles refs, arrays, polymorphism; everything saves to YAML `.prefab` / `.asset` files (text, VCS-friendly).
- **Why it matters:** Without a prefab/scene format TD Engine cannot save a level, share a creature template, or expose a UGC asset catalog. This is *the* foundational gap.
- **Complexity:** **Medium** (reflection-based serializer + binary/text format + variant override resolution).
- **Sources:**
  - https://docs.unity3d.com/6000.5/Documentation/Manual/Prefabs.html
  - https://docs.unity3d.com/6000.5/Documentation/Manual/script-ScriptableObject.html

### 3.8 Profiler + diagnostics
- **What it does:** Built-in Profiler window with per-frame CPU/GPU/memory/audio modules; deep profiling; **Standalone Profiler** runs in its own process; can connect to devices on the network; the `Profiler` API lets gameplay code push custom markers.
- **Why it matters:** A voxel world with millions of blocks or a 50-player Roblox room *cannot* be tuned without per-system timings. TD Engine has no profiling today.
- **Complexity:** **Low–Medium** (per-system timing ring buffer + ImGui viewer; GPU timer queries are Medium).
- **Sources:**
  - https://docs.unity3d.com/6000.5/Documentation/Manual/Profiler.html
  - https://docs.unity3d.com/6000.5/Documentation/Manual/ProfilerWindow.html

### 3.9 Voxel / large-world features
- **What it does:** Unity ships **no first-party voxel system**. The community pattern is: combine block meshes via `Mesh.CombineMeshes` at runtime, stream chunks via **additive scene loading** or **DOTS SubScenes** (which can stream entities in/out around the player), and use `Addressables` for chunk data.
- **Why it matters:** This is exactly the architecture TD Engine will need — chunked storage, mesh-greedy meshing, frustum + distance streaming.
- **Complexity:** **High** (greedy meshing, chunk paging, threading, serialization).
- **Sources:**
  - https://docs.unity3d.com/Packages/com.unity.entities@1.0/manual/conversion-subscenes.html
  - https://docs.unity3d.com/Packages/com.unity.entities@1.0/manual/streaming-loading-scenes.html
  - Community: https://github.com/JSKF/Luxelith (high-perf Unity voxel renderer that streams chunks around the player)

### Unity summary table

| Feature | What it does | Why it matters for Roblox/MC | C++ complexity |
|---|---|---|---|
| DOTS / ECS + SubScenes | Data-oriented ECS + chunk streaming | MC: millions of blocks; flat OO can't scale | High |
| SRP (URP/HDRP) + Shader Graph | Scriptable render passes, node shaders | One renderer scaling web→AAA; custom block lighting | High |
| Package Manager + Addressables | Addressable runtime asset loading + catalog | Roblox UGC marketplace; MC chunk data | High |
| Netcode for GameObjects/Entities + Transport | 3-layer server-authoritative multiplayer + RPC + Relay | Both targets are inherently multiplayer | High |
| UI Toolkit (UXML/USS) | Modern retained-mode HTML/CSS-like UI | Roblox-quality HUDs/menus in JS | High |
| C# MonoBehaviour + hot reload | Gameplay language with live reload | Critical UGC productivity | Medium–High |
| Prefabs + ScriptableObjects + YAML serialization | Reusable templates + data assets | Foundation for levels, UGC catalog | Medium |
| Profiler + Standalone Profiler | Per-module CPU/GPU/memory timing | Required for voxel/50-player tuning | Low–Medium |
| (No first-party voxel) | SubScenes + additive scene loading used instead | Architectural template for chunk streaming | High |

---

## 4. Godot Survey

All URLs below were fetched from `docs.godotengine.org` via web-search + web-reader.

### 4.1 Node + Scene system (everything is a Node, scene inheritance)
- **What it does:** A Godot game is a tree of scenes, each scene is a tree of **Nodes**. Every node has a name, editable properties, frame callbacks, can be extended, and can be parented to another node. A saved scene becomes a new node type you can instance; **scene inheritance** lets a child scene override properties of its parent scene.
- **Why it matters:** This is the single biggest architectural gap in TD Engine — entities are flat. Roblox Studio and Godot both model the world as a transform hierarchy; without it, parenting, UI layout, and instanced prefabs are all impossible.
- **Complexity:** **Medium** (tree of entities + transform inheritance + scene save/load + instance overrides).
- **Sources:**
  - https://docs.godotengine.org/en/stable/getting_started/step_by_step/nodes_and_scenes.html
  - https://docs.godotengine.org/en/stable/classes/class_node.html

### 4.2 Signals (event system)
- **What it does:** A first-class `Signal` type. Any `Node` can declare and emit signals; other nodes connect callables to them — fully decoupled, no direct references. The Signal class supports typed parameters, one-shot connections, deferred calls, and bound arguments.
- **Why it matters:** Decoupled events are how a UGC platform lets user scripts react to engine events (`onTouched`, `onDied`, `onPlayerJoin`) without patching the engine. TD Engine's systems currently call each other directly.
- **Complexity:** **Low–Medium** (slot/observer pattern with marshalling; typed signals via reflection are Medium).
- **Sources:**
  - https://docs.godotengine.org/en/stable/classes/class_signal.html
  - https://docs.godotengine.org/en/stable/getting_started/step_by_step/signals.html
  - https://docs.godotengine.org/en/stable/tutorials/scripting/c_sharp/c_sharp_signals.html

### 4.3 GDScript vs C# vs GDExtension
- **What it does:** **GDScript** is a high-level, indentation-based, gradually-typed language purpose-built for Godot — tight engine integration, fast iteration. **C#** is supported via .NET (full Mono/CoreCLR). **GDExtension** is a Godot-specific technology that lets the engine interact with **native shared libraries at runtime** — no engine recompilation, distributable via the Asset Library; bindings exist for C++, Rust, Swift, D, etc.
- **Why it matters:** Roblox uses Luau (a sandboxed Lua derivative). The right move for TD Engine is to ship a sandboxed scripting VM (Lua or Wren) as the "GDScript equivalent" and to keep C++ as the "GDExtension equivalent" via the existing WASM bridge.
- **Complexity:** **Medium** for Lua embedding; **High** for a custom language like GDScript/Luau.
- **Sources:**
  - https://docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_basics.html
  - https://docs.godotengine.org/en/4.4/tutorials/scripting/gdextension/what_is_gdextension.html
  - https://docs.godotengine.org/en/4.7/classes/class_gdextension.html

### 4.4 Asset Library
- **What it does:** A community-curated catalog of free assets/plugins. Submitted as ZIP archives, installed from inside the editor or via the web; supports plugins, scripts, shaders, templates, complete projects.
- **Why it matters:** This is the open-source equivalent of the Roblox catalog / Unity Asset Store. A future TD Engine UGC marketplace will look like this.
- **Complexity:** **Medium** (web catalog + ZIP import + dependency resolution; sandboxing is the hard part).
- **Sources:**
  - https://docs.godotengine.org/en/stable/community/asset_library/submitting_to_assetlib.html
  - https://godotassetlibrary.com

### 4.5 High-level multiplayer API (RPC, ENet)
- **What it does:** Built on a modified **ENet** (reliable-UDP library with IPv6 support). High-level API lets you annotate methods on `Node`-derived classes as RPCs (`@rpc`); the `MultiplayerAPI` synchronizes state across the scene tree, handles peer management, and supports custom `MultiplayerPeer` implementations (WebRTC, WebSocket, ENet). Low-level `PacketPeer`/`StreamPeer` APIs are also available.
- **Why it matters:** This is the simplest viable model for TD Engine: a transport (ENet or WebRTC-over-WASM) + RPC attribute + scene-replicated nodes. WebRTC is critical because raw UDP is not available in browsers — TD Engine's WASM target needs WebRTC data channels.
- **Complexity:** **Medium** for RPC layer; **High** for browser transport (WebRTC data channels + Emscripten).
- **Sources:**
  - https://docs.godotengine.org/en/stable/tutorials/networking/high_level_multiplayer.html
  - https://docs.godotengine.org/en/3.5/classes/class_networkedmultiplayerenet.html
  - https://docs.godotengine.org/en/3.2/classes/class_multiplayerapi.html

### 4.6 Physics servers (GodotPhysics, Bullet removed in Godot 4)
- **What it does:** A separate `PhysicsServer3D` (and `PhysicsServer2D`) that creates physics objects **without inserting them on the node tree** — the high-level `RigidBody3D`/`CharacterBody3D` nodes are thin wrappers. In Godot 4 the Bullet backend was dropped in favor of the in-house **GodotPhysics** for both 2D and 3D.
- **Why it matter:** A server-backed decoupled design lets TD Engine keep its ECS and add a real 3D solver behind it. Roblox and Minecraft both rely on a custom character controller + raycasts + AABB-vs-world — not a full Havok-style solver.
- **Complexity:** **High** for full rigidbody dynamics; **Medium** for a character controller + raycast + AABB-vs-voxel layer.
- **Sources:**
  - https://docs.godotengine.org/en/3.5/classes/class_physicsserver.html
  - https://docs.godotengine.org/en/4.7/tutorials/migrating/upgrading_to_godot_4.html (Bullet removed)
  - https://docs.godotengine.org/en/3.5/classes/class_rigidbody.html

### 4.7 Visual shaders / shading language
- **What it does:** Godot has its own GLSL-like shading language (vertex + fragment + light processors, render modes) **and** a visual shader graph that compiles to the same. Shader types exist for `canvas_item`, `spatial`, `particles`, `sky`, `fog`.
- **Why it matters:** Roblox lets users write shaders; a voxel world needs cheap block-light + fog. TD Engine already has `assets/shaders/*.vert/*.frag`; adding a tiny custom shader language is plausible.
- **Complexity:** **Medium** for the language (you already have GLSL); **High** for the visual graph editor.
- **Sources:**
  - https://docs.godotengine.org/en/stable/tutorials/shaders/shaders_style_guide.html
  - https://docs.godotengine.org/en/stable/tutorials/shaders/index.html

### 4.8 Resource system + `.tres` serialization
- **What it does:** `Resource` is the base class for all data containers (textures, materials, curves, fonts, custom data). Resources auto-serialize to **`.tres`** (text, VCS-friendly) and to **`.res`** (binary) on export. A separate `Variant`-based **binary serialization API** exists for network/save data. `ResourceSaver`/`ResourceLoader` discover format handlers via `ResourceFormatSaver` plugins.
- **Why it matters:** This is Godot's prefab equivalent. TD Engine needs exactly this — a typed, versionable, text-format data file that can hold a prefab, a material, a chunk, a script's serialized state.
- **Complexity:** **Medium** (Variant-style tagged format + `ResourceFormatSaver` plugin registry).
- **Sources:**
  - https://docs.godotengine.org/en/stable/tutorials/scripting/resources.html
  - https://docs.godotengine.org/en/stable/classes/class_resource.html
  - https://docs.godotengine.org/en/stable/classes/class_resourcesaver.html
  - https://docs.godotengine.org/en/stable/tutorials/io/binary_serialization_api.html

### 4.9 Voxel support (third-party)
- **What it does:** Godot ships **no first-party voxel module**. The de-facto solution is **`Zylann/godot_voxel`**, a C++ module/GDExtension for volumetric terrain: infinite streaming, in-game editing, overhangs/tunnels, smooth voxels, multi-threaded meshing. Used as a module (compiled into the engine) or as a GDExtension.
- **Why it matters:** Confirms the architectural pattern: chunk paging, greedy/naive meshing, threaded mesh build, distance-based streaming — all needed for TD Engine's Minecraft target.
- **Complexity:** **High**.
- **Sources:**
  - https://github.com/Zylann/godot_voxel
  - https://voxel-tools.readthedocs.io/en/latest/quick_start

### 4.10 Module system / GDExtension for native code
- **What it does:** Two ways to add native code. **C++ Modules** are statically compiled into the engine for deep integration (access to internals, no extra files at runtime). **GDExtension** loads shared libraries at runtime — no engine rebuild, ideal for distributing high-perf add-ons via the Asset Library; the same binary runs in editor and exported project.
- **Why it matters:** TD Engine's WASM bridge already plays the role of GDExtension for the browser target (JS calls into C++ via `cwrap`); the same abstraction is needed for the native editor (dynamic shared library plugins).
- **Complexity:** **Medium** (ABI-stable C interface + plugin discovery; mirroring Godot's `gdextension` interface is plausible).
- **Sources:**
  - https://docs.godotengine.org/en/4.4/tutorials/scripting/gdextension/what_is_gdextension.html
  - https://docs.godotengine.org/en/4.7/classes/class_gdextension.html
  - https://godotengine.org/article/introducing-gd-extensions

### Godot summary table

| Feature | What it does | Why it matters for Roblox/MC | C++ complexity |
|---|---|---|---|
| Node + Scene system | Tree of nodes, scene inheritance | Parenting, instanced prefabs, UI layout | Medium |
| Signals | First-class decoupled events | UGC script reactions to engine events | Low–Medium |
| GDScript / C# / GDExtension | Built-in language + native plugin ABI | Sandboxed VM + native plugin story | Medium / High |
| Asset Library | Community asset catalog | Open-source equivalent of Roblox catalog | Medium |
| High-level multiplayer (ENet + RPC) | Reliable UDP + RPC + scene replication | Multiplayer for both targets; WebRTC for WASM | Medium–High |
| PhysicsServer3D (GodotPhysics) | Backend solver decoupled from nodes | 3D dynamics + raycasts behind ECS | Medium–High |
| Shading language + visual shaders | Custom GLSL-like + node editor | UGC shaders; cheap voxel lighting | Medium / High |
| Resource + .tres serialization | Typed, text/binary data assets | Prefab/material/chunk save format | Medium |
| (No first-party voxel) Zylann module | Chunk streaming + greedy meshing | MC target template | High |
| Modules + GDExtension | Static vs runtime native plugins | Plugin story for native editor | Medium |

---

## 5. Cross-cutting gaps for Roblox-like + Minecraft-like

Synthesizing both engines against TD Engine's current state, the **mandatory** gaps are:

1. **Scene graph / node hierarchy** — neither target is buildable on flat entities. (Godot's tree of Nodes; Unity's Transform hierarchy.)
2. **Serialization format (`.prefab` / `.tres`)** — cannot save levels, share templates, or persist UGC without it.
3. **Sandboxed scripting VM with hot reload** — Roblox is *defined* by user scripts; no VM, no UGC.
4. **Networking: transport + RPC + scene replication** — both targets are inherently multiplayer; WASM target needs WebRTC data channels (UDP not available in browsers).
5. **UI toolkit exposed to scripts** — Roblox's HUD/menu UGC surface.
6. **3D physics: character controller + raycasts + AABB-vs-world** — Minecraft doesn't need Havok, but it needs a solid character controller and voxel collision.
7. **Voxel chunk system: chunk storage + greedy meshing + distance streaming** — the Minecraft target, full stop.
8. **Profiler + asset browser/importers** — productivity gating items for any non-trivial project.
9. **Asset catalog / Addressables-style runtime loading** — needed for UGC + chunk streaming + remote updates.
10. **GPU skinning / animation** — needed for any character-driven game (Roblox avatars, Minecraft mobs).
11. **Server-authoritative netcode + client prediction** — needed for responsive multiplayer at scale (Tier 3).
12. **Shader graph + visual scripting** — UGC-friendly authoring surfaces (Tier 3).

---

## 6. Prioritized Roadmap

Complexity key: 🟢 Low · 🟡 Medium · 🔴 High

### Tier 1 — Foundation (≈ 3–6 months)
*Must exist before any "heavy" game is possible.*

| # | Workstream | Complexity | Inspired by | Deliverable |
|---|---|---|---|---|
| 1.1 | **Scene graph / node hierarchy** — parent/child entities, world+local transform inheritance, scene save/load | 🟡 | Godot Nodes & Scenes; Unity Transform | `Scene`/`Node` types in ECS; serialize to JSON |
| 1.2 | **Serialization format** — typed `.tdscene` / `.tdprefab` text files with refs, arrays, nested prefabs, overrides | 🟡 | Godot `.tres`; Unity `.prefab` | `Serializer` + `ResourceFormatSaver` plugin registry |
| 1.3 | **Lua scripting VM + hot reload** — embed Lua/Luau, bind ECS query + transform + input APIs, file-watch & reload | 🟡 | Godot GDScript; Unity C# hot reload | `td_script_*` exports + ScriptComponent |
| 1.4 | **Signals / events** — first-class decoupled event bus with typed signals | 🟢 | Godot Signal | `Signal` template + `connect/emit` API |
| 1.5 | **Network transport + RPC** — ENet (native) + WebRTC data channel (WASM), `@rpc`-style method replication | 🟡 | Godot high-level multiplayer; Unity Transport | `td_net_*` API; first sample: 2-player Pong over net |
| 1.6 | **Profiler v1** — per-system CPU timing ring buffer + ImGui viewer | 🟢 | Unity Profiler modules | `TD_PROFILE()` macro + Profiler panel |
| 1.7 | **Asset browser + importers v1** — PNG/OBJ/WAV already exist; add glTF + OGG + directory browser in ImGui editor | 🟡 | Godot FileSystem dock; Unity Project window | Replace stub `editor/asset_browser.cpp` with real importer pipeline |

**Tier 1 exit criteria:** A user can save a scene, attach a Lua script with hot reload, press Play in the editor, host a 2-player networked match, and tune it with the profiler.

### Tier 2 — Productivity (≈ 6–12 months)
*Make development actually usable; first vertical slices of both target genres.*

| # | Workstream | Complexity | Inspired by | Deliverable |
|---|---|---|---|---|
| 2.1 | **UI toolkit v1** — retained-mode Canvas + layout + style, exposed to JS and Lua; immediate-mode fallback | 🔴 | Unity UI Toolkit; Godot Control | `td_ui_*` API; HUD in sample games |
| 2.2 | **3D physics: character controller + raycasts + AABB-vs-world** — no full solver, just enough for MC + Roblox characters | 🟡 | Godot `PhysicsServer3D`; Unity CharacterController | `CharacterController` + `Raycast` API |
| 2.3 | **Voxel chunk system v1** — chunk storage, naive meshing, frustum + distance streaming, in-place editing | 🔴 | Zylann `godot_voxel`; Unity SubScene streaming | Demo: walk an infinite Minecraft-style world |
| 2.4 | **GPU skinning / skeletal animation** — glTF skin + joints, GPU vertex skinning in shader | 🟡 | Unity Animator; Godot Skeleton3D | Animated character in editor + WASM |
| 2.5 | **Asset catalog / Addressables-style runtime loading** — async load-by-address, dependency graph, hot-reloadable | 🔴 | Unity Addressables | `td_assets_load("res://…")` async API |
| 2.6 | **Native plugin ABI (GDExtension-equivalent)** — stable C interface, dynamic shared libraries on desktop | 🟡 | Godot GDExtension | Plugin discovery + sample plugin |
| 2.7 | **3D positional audio** — HRTF / pan-by-distance | 🟢 | Unity Audio Spatializer; Godot AudioStreamPlayer3D | Extend existing `Mixer` |
| 2.8 | **Visual shader editor v1** — node graph → GLSL | 🔴 | Godot VisualShaders; Unity Shader Graph | Editor panel + compiled pipeline |

**Tier 2 exit criteria:** A user can build a Minecraft-like vertical slice (infinite voxel world, character controller, animated mob, networked 2-player co-op) and a Roblox-like vertical slice (custom HUD, user Lua scripts, asset-loaded avatars).

### Tier 3 — Advanced (12+ months)
*True AAA / Roblox-scale UGC.*

| # | Workstream | Complexity | Inspired by | Deliverable |
|---|---|---|---|---|
| 3.1 | **Server-authoritative netcode + client prediction + reconciliation** | 🔴 | Unity Netcode for Entities; Godot high-level MultiplayerAPI | Lag-compensated hit detection; 50-player room |
| 3.2 | **Interest management + chunked scene replication** — only replicate what each client can see | 🔴 | Unity NGO NetworkVisibility | Spatial hash + per-client visibility |
| 3.3 | **Script sandboxing + UGC permissions** — capability-based API, CPU/memory limits, asset isolation | 🔴 | Roblox Luau sandbox | Sandboxed `ScriptContext` per asset |
| 3.4 | **UGC asset store / marketplace** — upload, version, dependencies, moderation | 🔴 | Unity Asset Store; Godot Asset Library | Web catalog + in-editor installer |
| 3.5 | **Visual scripting graph** (non-programmer UGC authoring) | 🔴 | Unity Visual Scripting; Godot VisualScript (legacy) | Node graph → Lua bytecode |
| 3.6 | **DOTS-style archetype ECS upgrade** — chunked storage + query planner for millions of entities | 🔴 | Unity DOTS / Entities | Sub-1ms iteration over 1M entities |
| 3.7 | **Relay / NAT-traversal service + WebGL distributed authority** | 🔴 | Unity Relay | Self-hostable relay server |
| 3.8 | **GPU-driven rendering + greedy meshing for voxels** | 🔴 | Community Unity voxel engines (Luxelith) | 60 FPS at 16-chunk render distance |
| 3.9 | **Localization, XR (VR/AR), mobile touch input** | 🟡 | Unity + Godot feature matrices | Platform parity |
| 3.10 | **Cluster/hosting story** (dedicated server build, headless mode, lobby service) | 🔴 | Unity Game Server Hosting | `td-server` headless binary |

**Tier 3 exit criteria:** A Roblox-scale UGC game (1000+ concurrent rooms, user Lua + visual scripting, marketplace assets) and a Minecraft-scale world (infinite voxel streaming, 50-player co-op, dedicated server) both run end-to-end.

---

## 7. Concrete 0–6 month sequence (Tier 1 detail)

A realistic ordering that unblocks the most subsequent work:

1. **Week 1–4:** Scene graph + transform inheritance (1.1). Refactor ECS `World` to support parent/child + local/world matrices. Touches `camera.cpp`, `sprite_batch.cpp`, `gl_renderer.cpp`.
2. **Week 5–10:** Serialization format + prefab (1.2). Decide between JSON (simple) and a custom text format (faster, typed). Mirror Godot's `Resource`/`ResourceSaver` split.
3. **Week 11–18:** Lua VM embedding + hot reload (1.3). Bind `World.query`, `Transform`, `Input`, `Audio`, `Signal`. Expose via new `td_script_*` JS exports.
4. **Week 6–8 (parallel):** Signals (1.4) — small, decoupled, lands early.
5. **Week 14–22:** Network transport (1.5). Native: ENet. WASM: WebRTC data channels via Emscripten. RPC layer mirrors Godot's `@rpc`.
6. **Week 2–6 (parallel):** Profiler v1 (1.6) — needed from day one to measure everything else.
7. **Week 18–24:** Asset browser + glTF/OGG importers (1.7) — replaces the stub `editor/asset_browser.cpp`.

---

## 8. Sources (verified fetched)

### Unity (docs.unity3d.com)
- ECS feature set — https://docs.unity3d.com/6000.3/Documentation/Manual/ECSFeature.html
- Entities package overview — https://docs.unity3d.com/Packages/com.unity.entities@1.0/manual/index.html
- Subscenes overview — https://docs.unity3d.com/Packages/com.unity.entities@1.0/manual/conversion-subscenes.html
- Load a scene (streaming) — https://docs.unity3d.com/Packages/com.unity.entities@1.0/manual/streaming-loading-scenes.html
- Entities Graphics overview — https://docs.unity3d.com/Packages/com.unity.entities.graphics@1.0/manual/overview.html
- Scriptable Render Pipeline — https://docs.unity3d.com/6000.5/Documentation/Manual/SRP.html
- HDRP overview — https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@latest
- Shader Graph — https://docs.unity3d.com/Packages/com.unity.shadergraph@14.0/manual/Shader-Graph-Sample-Feature-Examples.html
- Addressables package — https://docs.unity3d.com/Packages/com.unity.addressables@latest
- Netcode for GameObjects (manual) — https://docs.unity3d.com/6000.5/Documentation/Manual/com.unity.netcode.gameobjects.html
- Netcode for GameObjects (package) — https://docs.unity3d.com/Packages/com.unity.netcode.gameobjects@latest
- Netcode for Entities — https://docs.unity3d.com/Packages/com.unity.netcode@latest
- Unity Transport — https://docs.unity3d.com/Packages/com.unity.transport@latest
- NGO client-server quickstart — https://docs.unity3d.com/Packages/com.unity.netcode.gameobjects@2.5/manual/tutorials/get-started-with-ngo.html
- UI systems comparison — https://docs.unity3d.com/6000.5/Documentation/Manual/UI-systems.html
- Configurable Enter Play Mode / domain reload — https://docs.unity3d.com/6000.5/Documentation/Manual/ConfigurableEnterPlayMode.html
- Visual Scripting / PM — https://docs.unity3d.com/6000.5/Documentation/Manual/com.unity.visualscripting.html
- Prefabs — https://docs.unity3d.com/6000.5/Documentation/Manual/Prefabs.html
- Unity Profiler — https://docs.unity3d.com/6000.5/Documentation/Manual/Profiler.html
- WebGL native plug-ins (Emscripten) — https://docs.unity3d.com/2022.1/Documentation/Manual/webgl-native-plugins-with-emscripten.html
- Community voxel engine Luxelith — https://github.com/JSKF/Luxelith

### Godot (docs.godotengine.org)
- Nodes and Scenes — https://docs.godotengine.org/en/stable/getting_started/step_by_step/nodes_and_scenes.html
- Node class reference — https://docs.godotengine.org/en/stable/classes/class_node.html
- Using signals (step by step) — https://docs.godotengine.org/en/stable/getting_started/step_by_step/signals.html
- Signal class reference — https://docs.godotengine.org/en/stable/classes/class_signal.html
- C# signals — https://docs.godotengine.org/en/stable/tutorials/scripting/c_sharp/c_sharp_signals.html
- GDScript reference — https://docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_basics.html
- What is GDExtension — https://docs.godotengine.org/en/4.4/tutorials/scripting/gdextension/what_is_gdextension.html
- GDExtension class reference — https://docs.godotengine.org/en/4.7/classes/class_gdextension.html
- GDExtension intro article — https://godotengine.org/article/introducing-gd-extensions
- Submitting to Asset Library — https://docs.godotengine.org/en/stable/community/asset_library/submitting_to_assetlib.html
- High-level multiplayer — https://docs.godotengine.org/en/stable/tutorials/networking/high_level_multiplayer.html
- NetworkedMultiplayerENet — https://docs.godotengine.org/en/3.5/classes/class_networkedmultiplayerenet.html
- MultiplayerAPI — https://docs.godotengine.org/en/3.2/classes/class_multiplayerapi.html
- PhysicsServer — https://docs.godotengine.org/en/3.5/classes/class_physicsserver.html
- RigidBody — https://docs.godotengine.org/en/3.5/classes/class_rigidbody.html
- Upgrading to Godot 4 (Bullet removed) — https://docs.godotengine.org/en/4.7/tutorials/migrating/upgrading_to_godot_4.html
- Shaders style guide — https://docs.godotengine.org/en/stable/tutorials/shaders/shaders_style_guide.html
- Resources tutorial — https://docs.godotengine.org/en/stable/tutorials/scripting/resources.html
- Resource class — https://docs.godotengine.org/en/stable/classes/class_resource.html
- ResourceSaver — https://docs.godotengine.org/en/stable/classes/class_resourcesaver.html
- Binary serialization API — https://docs.godotengine.org/en/stable/tutorials/io/binary_serialization_api.html
- Control (UI base class) — https://docs.godotengine.org/en/stable/classes/class_control.html
- UI tutorial index — https://docs.godotengine.org/en/stable/tutorials/ui/index.html
- Zylann godot_voxel module — https://github.com/Zylann/godot_voxel
- Voxel tools quick start — https://voxel-tools.readthedocs.io/en/latest/quick_start

---

## 9. One-paragraph conclusion

Unity and Godot converge on the same eight foundational systems: a transform-hierarchy scene graph, a typed serializable asset format, a sandboxed scripting language with hot reload, a high-level multiplayer stack (reliable UDP + RPC + scene replication), a UI toolkit exposed to scripts, a real 3D physics backend, a profiling tool, and an asset catalog/Addressables system. Neither ships a first-party voxel engine — both communities build one out of chunked ECS data + mesh combining + distance streaming, which is precisely what TD Engine must add in Tier 2. The 3-tier roadmap above sequences these so that the Tier 1 deliverables (scene graph, serialization, Lua VM + hot reload, RPC networking, profiler, asset browser) unblock *both* target genres in parallel, Tier 2 ships vertical slices of each, and Tier 3 reaches true Roblox/Minecraft scale (server authority, UGC sandbox + marketplace, archetype ECS for millions of entities, dedicated server hosting).
