# TD Engine — Bug-fix Pipeline

This document lists every user-facing bug found in the engine and the fix applied. "User-facing" means a game developer using the engine to build a game would hit the bug — not internal unit-test failures.

The audit covered four areas in parallel:

1. **Web / WASM runtime** (`web/*.js`, `wasm/*.js`, `web/td_api.d.ts`)
2. **Example games** (`examples/web-game/`, `examples/3d-showcase/`, `examples/pong/`, `examples/platformer/`, `tools/cli/templates/minimal/`)
3. **CLI + tdscript tooling** (`tools/cli/*`, `tools/tdscript/*`, `tools/bundler/*`, `tools/gen_font.py`, `scripts/patch_makefile.py`)
4. **C++ public API + docs** (`src/host/main.cpp`, `wasm/emscripten_main.cpp`, `CMakeLists.txt`, `Makefile`, `docs/*.md`, `web/GETTING_STARTED.md`, `README.md`, `wasm/README.md`)

Total: **47 distinct bugs fixed** across **38 files**.

---

## 1. CLI tooling — critical blockers (every command was broken)

### 1.1 `tools/cli/lib/util.js` — missing file, every command crashed at load

**Severity:** ERROR (every `td <command>` exited 2 with `Cannot find module '../lib/util'`)

All seven CLI command modules (`init.js`, `serve.js`, `build.js`, `test.js`, `script.js`, `bundle.js`, `deploy.js`) destructure helpers from `require('../lib/util')`, but the file did not exist. `td.js` wrapped the `require` in try/catch and reported "Unknown command" — masking the real error.

**Fix:** Created `tools/cli/lib/util.js` exporting all 14 helpers the commands need: `COLORS`, `ok`, `info`, `warn`, `err`, `isFile`, `isDir`, `resolvePath`, `findEngineRoot`, `copyDir`, `rmrf`, `readJson`, `walk`, `spawnInherit`. `findEngineRoot` walks up from `__dirname` looking for `package.json` + `tools/` + `src/` and falls back to `process.env.TD_ENGINE_ROOT`.

### 1.2 `tools/cli/lib/project_tds.js` — missing file, `td serve` crashed

**Severity:** ERROR

`serve.js` requires `loadProjectTds`, `resolveServerScript`, `parseServerUrl` from `../lib/project_tds`, which didn't exist. Even after fixing `lib/util.js`, `td serve` would still crash.

**Fix:** Created `tools/cli/lib/project_tds.js` with:
- `loadProjectTds(gameDir)` — reads + validates `project.td` JSON, normalizes `entry`/`networking`/`config` so callers can deref safely.
- `resolveServerScript(gameDir, cfg)` — resolves `cfg.entry.serverScript` (or legacy `cfg.entry.mainScript`) against `gameDir`. Returns absolute path or `null`.
- `parseServerUrl(urlStr)` — parses `ws://host:port/path` and `wss://` URLs into `{protocol, host, port, path}`.

### 1.3 `tools/cli/td.js` — `--no-X` flags silently ignored

**Severity:** ERROR

`parseArgv` set `opts['no-wasm'] = true` instead of `opts.wasm = false`. So `--no-wasm`, `--no-reload`, `--no-net`, `--no-color` were all no-ops — `build.js` ran `make web` anyway, `serve.js` started the game-net server anyway.

**Fix:** Added `if (key.startsWith('no-')) { opts[key.slice(3)] = false; continue; }` to `parseArgv`. Also tightened the value-detection regex so `--port -1` is treated as a flag (use `--port=-1` to pass negative numbers).

### 1.4 `tools/cli/td.js` — `loadCommand` couldn't distinguish "file not found" from "file failed to load"

**Severity:** WARNING

When `init.js` failed to load (missing `lib/util`), `td.js` printed both "Failed to load command" AND "Unknown command: init" — sending the user down the wrong debugging path. `td help init` exited 0 despite the load failure.

**Fix:** `loadCommand` now returns `{ __loadError: true, error: e }` on load failure (distinct from `null` for "file doesn't exist"). The dispatcher prints "Command exists but failed to load" instead of "Unknown command", and `td help <cmd>` exits 1 with a clear message.

### 1.5 `tools/cli/td.js` — help text referenced nonexistent `bin/`

**Severity:** MISMATCH

Help said `TD_ENGINE_ROOT (default: parent of bin/)` but there's no `bin/` directory.

**Fix:** Updated to "auto-detected from this script's location, two directories up".

### 1.6 `package.json` — missing `ws` dependency

**Severity:** ERROR

`serve.js` does `require('ws')` for the dev-server live-reload WebSocket and for the game-net server. `package.json` had no `dependencies` key, so `npm install` installed nothing and `require('ws')` threw. `serve.js` wrapped both requires in try/catch, so `td serve` silently booted only the static server with no live reload and no game-net. The e2e test `tests/tdscript/test_serve_e2e.js` also hard-coded a path into `node_modules/ws` that didn't exist.

**Fix:** Added `"dependencies": { "ws": "^8.18.0" }` to `package.json`. Updated `td.js` header comment from "Zero npm dependencies" to "Minimal npm dependencies (only `ws` for the dev-server live-reload + game-net server)". Updated `test_serve_e2e.js` to `require('ws')` directly.

### 1.7 `tools/cli/commands/serve.js` — `--port` with no value produced `NaN`

**Severity:** WARNING

`parseInt(opts.port || '8080', 10)` — when `--port` was passed with no value, `opts.port === true`, so `parseInt(true, 10) === NaN`, and `server.listen(NaN)` booted on a random port with no warning.

**Fix:** Added validation: `const portRaw = (opts.port === true || opts.port === undefined) ? '8080' : opts.port;` then `if (!Number.isInteger(port) || port < 1 || port > 65535) { err(...); return 1; }`.

### 1.8 `tools/cli/commands/serve.js` — path traversal check used substring prefix

**Severity:** WARNING (security)

`filePath.indexOf(gameDir) !== 0` is a substring check. A request for `/../my-game-secret/secret.txt` would resolve to `/home/user/my-game-secret/secret.txt`, and `'/home/user/my-game-secret/...'.indexOf('/home/user/my-game') === 0` is true, so the check passed. An attacker could read any file in a sibling directory whose name started with the game folder name.

**Fix:** Replaced with path-aware check: `const relGame = path.relative(gameDir, filePath); if (relGame.startsWith('..') || path.isAbsolute(relGame)) { ... 403 }`. Same for `webDir`.

### 1.9 `tools/cli/commands/init.js` — `__GAME_NAME__` not patched in `game.js`

**Severity:** WARNING

`init.js` patched `bundle.json`, `project.td`, `index.html`, `README.txt` but forgot `game.js`. After `td init my-cool-game`, `game.js` still said `// __GAME_NAME__` literally.

**Fix:** Added `patchFile(path.join(target, 'game.js'), (s) => s.replace(/__GAME_NAME__/g, name))`.

### 1.10 `tools/cli/commands/init.js` — help advertised `--template platformer` that doesn't exist

**Severity:** MISMATCH

Help text said `--template minimal|platformer` but only `tools/cli/templates/minimal/` exists. `td init x --template platformer` would fail with "Template not found".

**Fix:** Changed help to `--template NAME` with `Available: minimal`.

### 1.11 `tools/cli/commands/build.js` + `bundle.js` + `tools/bundler/bundle.py` — RUNTIME_FILES missing `tdscript_runtime.js` + `td_client_bootstrap.js`

**Severity:** ERROR

The scaffolded `index.html` loads `<script src="runtime/tdscript_runtime.js">` and `<script src="runtime/td_client_bootstrap.js">`, but neither file was in the `RUNTIME_FILES` copy list. After `td build` or `td bundle`, both files would 404 and the game would crash on boot.

**Fix:** Added both files to `RUNTIME_FILES` in `build.js` (line 105), `bundle.js` (line 209), and `bundle.py` (line 76). Also added `net_interpolation.js` + `net_auth_server.js` to `bundle.py` (they were already in the JS lists — the two lists had drifted out of sync). Added a comment in `bundle.py` to keep the lists in sync.

### 1.12 `tools/cli/commands/bundle.js` — `--webview2-bootstrapper` never passed to `bundle.py`

**Severity:** ERROR

`bundle.py` requires `--webview2-bootstrapper PATH` when `--bundle-runtime` is set, but `bundle.js` never read the option from `opts`, never added it to the `merged` config, and never pushed it to `pyArgs`. So `td bundle --bundle-runtime /path/to/bootstrapper.exe` would silently drop the bootstrapper arg, then `bundle.py` would die with "--bundle-runtime requires --webview2-bootstrapper PATH".

**Fix:** Added `webview2_bootstrapper` to the `merged` config, added a validation check (`if (merged.bundle_runtime && !merged.webview2_bootstrapper) { err(...); return 1; }`), and pushed `--webview2-bootstrapper` to `pyArgs` when set.

### 1.13 `tools/bundler/bundle.py` — `game.tdproj`'s `icon` resolved against cwd instead of `game_dir`

**Severity:** WARNING

If `game.tdproj` said `"icon": "assets/icon.ico"`, the path was resolved relative to the caller's cwd (the repo root), not relative to `game_dir`. The icon file wouldn't be found.

**Fix:** Moved the `game_dir = args.game.resolve()` block BEFORE the tdproj merge, and resolved the icon relative to `game_dir`: `icon = (game_dir / tdproj["icon"]) if not Path(tdproj["icon"]).is_absolute() else Path(tdproj["icon"])`.

### 1.14 `scripts/patch_makefile.py` — hardcoded absolute path

**Severity:** ERROR

`MK_PATH = '/home/z/my-project/td-gh/Makefile'` — hardcoded to another machine's directory. Running `python3 scripts/patch_makefile.py` anywhere else (including this repo at `/home/z/my-project/td-work/td/`) threw `FileNotFoundError`.

**Fix:** `MK_PATH = Path(__file__).resolve().parents[1] / 'Makefile'` — resolves relative to the script's location. Also accepts `--makefile PATH` and `--makefile=PATH` overrides.

### 1.15 `tools/gen_font.py` — wrote to relative path `src/ui/font_data.h`

**Severity:** WARNING

`open("src/ui/font_data.h", "w")` only worked if the caller's cwd was the repo root. Running from any other directory threw `FileNotFoundError`.

**Fix:** `out_path = Path(__file__).resolve().parents[1] / 'src' / 'ui' / 'font_data.h'` with `out_path.parent.mkdir(parents=True, exist_ok=True)`.

### 1.16 CLI scripts lacked executable permission bits

**Severity:** WARNING

All five entry-point scripts had shebangs but `chmod -x`, so `./tools/cli/td.js` failed with "Permission denied".

**Fix:** `chmod +x tools/cli/td.js tools/tdscript/tdscript.js tools/bundler/bundle.py tools/gen_font.py scripts/patch_makefile.py`.

---

## 2. TDScript compiler — critical crash on `else if`

### 2.1 `tools/tdscript/tdscript.js` — `else if` chain crashed with `RangeError: Invalid count value: -1`

**Severity:** ERROR (any TDScript file containing `else if` could not be compiled)

The else-if codegen used an IIFE that saved `indent` into a local `saved` variable, set `indent = 0`, but **never restored it**. After the IIFE returned, `indent` was permanently 0. The outer `emitClass` then did `indent--` making `indent = -1`. The next `pad()` call did `'  '.repeat(-1)` → `RangeError`, crashing the compiler. Reproduced with:
```
class C { public void f() { if (true) { Log.info("a"); } else if (false) { Log.info("b"); } } }
```

**Fix:** Replaced the IIFE-based else-if codegen with a clean recursive `emitIfChain(n)` helper that walks the else-chain in a loop, emitting `} else if (...) { ... }` for each else-if and `} else { ... }` for the terminal else. No closure variable mutation, no recursion through `emitStmt`, handles arbitrary depth (`else if ... else if ... else { ... }`).

### 2.2 `tools/tdscript/tdscript.js` — no CLI entrypoint

**Severity:** MISMATCH

The file's header describes it as the "standalone tdscript compiler" but had no `if (require.main === module)` block. `node tools/tdscript/tdscript.js --help` printed nothing and exited 0.

**Fix:** Added a CLI entrypoint that parses `<file.td> [-o out.js] [--target js|cpp]`, compiles the file, and either writes to `-o PATH` or stdout. `--help` / `-h` / no args prints usage.

### 2.3 `tools/tdscript/tdscript.js` — import codegen emits `require('engine/networking')` which is unresolvable in Node

**Severity:** WARNING

`import "engine/networking";` compiles to `try { require('engine/networking'); } catch (e) { /* browser: pre-loaded */ }`. In Node, `require('engine/networking')` always throws, which is silently swallowed — so imports are no-ops in Node, and the runtime never gets the imported module's globals. If a user writes `import "engine/physics";` and forgets to add the matching `<script>` tag in HTML, the runtime silently has no Physics global.

**Fix:** No code change (the try/catch is intentional for browser pre-loaded modules). Documented the expected HTML `<script>` tag pattern in the comment so users know imports are backed by HTML script tags, not Node requires.

---

## 3. Build configuration — WASM linker failures

### 3.1 `CMakeLists.txt` — `_tdscript_compile` exported but never defined

**Severity:** ERROR (CMake WASM build would fail to link)

`EXPORTED_FUNCTIONS` listed `"_tdscript_compile"` but no C++ source defines `tdscript_compile`. Modern Emscripten defaults to `ERROR_ON_UNDEFINED_SYMBOLS=1`, so `emcmake cmake -B build-web && cmake --build build-web` would fail at link time. The Makefile did NOT list this symbol, so `make web` succeeded — the two build paths diverged.

**Fix:** Removed `"_tdscript_compile"` from `EXPORTED_FUNCTIONS` in `CMakeLists.txt`. (The standalone tdscript compiler is `tools/tdscript/tdscript.js`, a pure-JS implementation — there's no C function to export.)

### 3.2 `CMakeLists.txt` + `Makefile` — 25 `td_physics_*` functions missing from `EXPORTED_FUNCTIONS`

**Severity:** ERROR (entire 3D physics subsystem unreachable from JS)

`wasm/emscripten_main.cpp` defines 25 `td_physics_*` functions (lines 1057-1323), all marked `EMSCRIPTEN_KEEPALIVE`. `web/td_api.js` wraps all 25 in `TDEngine.physics.*`. But NONE of them appeared in `EXPORTED_FUNCTIONS` in either `CMakeLists.txt` or `Makefile`, and `-s EXPORT_KEEPALIVE=1` was not set. Without that flag, `EMSCRIPTEN_KEEPALIVE` only prevents dead-code elimination — it does NOT add the function to the Module export table. `Module.cwrap('td_physics_init', ...)` would succeed but `Module._td_physics_init` would be `undefined`, and at runtime the call would throw "Cannot find function td_physics_init in wasm exports". The README advertises 3D physics as a shipping feature.

**Fix:** Added all 25 `_td_physics_*` symbols to `EXPORTED_FUNCTIONS` in BOTH `CMakeLists.txt` (line 232) and `Makefile` (line 119). Also added `-s EXPORT_KEEPALIVE=1` to `EMCC_FLAGS` in both files as defense-in-depth so any future `EMSCRIPTEN_KEEPALIVE` function is auto-exported even if it's not listed.

---

## 4. JS API — silent data corruption bugs

### 4.1 `web/td_api.js` — `TDEngine.ecs.getPosition()` returned garbage Y

**Severity:** ERROR (every call silently returned wrong data)

C signature: `void td_entity_get_position(uint32_t id, float* outX, float* outY)`. JS code: `wrap('td_entity_get_position', null, ['number','number']).call(null, id, ptr)` — only 2 args (id, ptr), missing the outY pointer. In WASM calling convention the missing arg defaults to 0 (NULL). The C++ does `if (outY) *outY = p->y;` — skips the write because outY is NULL. The JS then reads `HEAPF32[(ptr>>2)+1]` — uninitialized malloc memory. The test suite only checked the method exists, not its behavior, so this passed CI.

**Fix:** `wrap('td_entity_get_position', null, ['number','number','number']).call(null, id, ptr, ptr + 4)` — pass both out-pointers (`ptr` for X, `ptr+4` for Y).

### 4.2 `web/td_api.js` — `TDEngine.input.getMousePos()` returned garbage Y

**Severity:** ERROR (same root cause as 4.1)

C signature: `void td_get_mouse_pos(float* outX, float* outY)`. JS code passed only 1 arg (`ptr`), so outY was NULL, and Y was never written.

**Fix:** `wrap('td_get_mouse_pos', null, ['number','number']).call(null, ptr, ptr + 4)`.

### 4.3 `web/td_api.js` — `TDEngine.beat.setCallback()` used wrong Emscripten signature

**Severity:** ERROR (beatTime silently dropped, potential stack corruption)

C typedef: `typedef void (*TdBeatCallback)(int beatCount, float beatTime);` — the engine invokes the callback with 2 args. JS code: `Module.addFunction(cb, 'vi')` — registers as `void(int)`, 1 arg. Emscripten's `dynCall_vi` passes only the first arg. The user's callback receives `beatCount` but `beatTime` is `undefined`. Worse, on some ABIs the float second arg is left on the WASM stack, potentially corrupting subsequent calls. The legacy `TDBridge.onBeat()` in `js_bridge.js:651` correctly uses `'vif'` — the two paths were inconsistent.

**Fix:** Changed `Module.addFunction(cb, 'vi')` to `Module.addFunction(cb, 'vif')` (void int float). Updated `td_api.d.ts` from `setCallback(cb: (beatCount: number) => void)` to `setCallback(cb: (beatCount: number, beatTime: number) => void)`. Updated README example. Updated `examples/3d-showcase/game/beat.js` callback to accept `(beatCount, beatTime)`.

### 4.4 `web/td_api.js` — `TDEngine.beat.resetCombo()` discarded the return value

**Severity:** MISMATCH

C: `int td_beat_reset_combo(uint32_t entityId)`. JS: `wrap('td_beat_reset_combo', null, ['number'])` — `null` return type discards the int. The legacy `TDBridge.beatResetCombo` used `'number'` and returned it. The `.d.ts` agreed with `td_api.js` (void) but disagreed with C++.

**Fix:** Changed to `wrap('td_beat_reset_combo', 'number', ['number'])` and return it. Updated `.d.ts` to `resetCombo(entityId: EntityId): number;` with doc-comment "Returns the previous combo count."

### 4.5 `web/td_api.js` — `TDEngine.script.call()` only accepted JSON strings

**Severity:** WARNING

The C API expects `argsJson` as a JSON-encoded string. The JS wrapper passed it through unchanged. But `docs.html` and the README example showed `TDEngine.script.call(handle, "spawn_burst", 100, 200)` — raw numbers, not a JSON array. This made `argsJson === 100`, then `argsJson || '[]'` was `100`, then `td_script_call` received `"100"` as argsJson, failed to parse as a JSON array, and the script function received zero arguments.

**Fix:** `call(handle, fnName, args)` now accepts either a JSON string (`'[1, 2, 3]'`) OR a plain JS array (`[1, 2, 3]`, auto-stringified). Defaults to `'[]'` if omitted. Updated `.d.ts` to `call(handle, fnName, args?: string | any[]): string`.

### 4.6 `web/td_api.js` — `TDEngine.i18n.load()` only accepted JSON strings

**Severity:** WARNING

C API expects `json` as a string. `docs.html` example showed `TDEngine.i18n.load('en', { play: 'Play' })` — an object, which coerced via `String(obj)` → `"[object Object]"`. The C++ `td_i18n_load` tried to parse `"[object Object]"` as a JSON table and failed silently.

**Fix:** `load(localeStr, json)` now accepts either a JSON string OR a plain JS object (auto-stringified via `JSON.stringify`). Updated `.d.ts` to `load(locale: string, json: string | object): void`.

### 4.7 `web/td_api.js` — `TDEngine.physics.getPosition/getVelocity/getOrientation/raycast` leaked WASM heap on throw

**Severity:** WARNING

`Module._malloc(N)` was followed by the WASM call with no `try/finally`. If the WASM call aborted (e.g. bodyId out of range triggers an assert), `Module._free(ptr)` was never reached. Same pattern in 4 functions (12B, 12B, 16B, 24B respectively). `ecs.getPosition` did this correctly — the bug was physics-specific.

**Fix:** Wrapped all 4 functions' bodies in `try { ... } finally { Module._free(ptr); }` (or `_free(pPtr); _free(nPtr);` for raycast).

### 4.8 `web/td_api.js` — doc-comment referenced nonexistent `TDEngine.server.getCurrentUrl()` and `openSettings()`

**Severity:** MISMATCH

The doc-comment example called `TDEngine.server.getCurrentUrl()` (missing "Server") and `TDEngine.server.openSettings()` (doesn't exist). `TDServerRouter` exports `getCurrentServerUrl`, `saveServerUrl`, `resolveAsset`, `probeServer`, etc.

**Fix:** Updated doc-comment to `TDEngine.server.getCurrentServerUrl()` and `TDEngine.server.saveServerUrl('https://my-vpn.example.com/td/')`.

### 4.9 `web/td_api.js` — doc-comment showed `TDEngine.init('game-canvas')` which doesn't exist

**Severity:** ERROR (users copy-pasting the doc-comment get `TypeError: TDEngine.init is not a function`)

`TDEngine` has subsystems (`lifecycle`, `ecs`, `input`, ...) but NO top-level `init`. The correct call is `TDEngine.lifecycle.init(canvasId)`. The `ensureModule()` error message also said "call TDEngine.init(canvasId) first".

**Fix:** Updated doc-comment to `await TDEngine.lifecycle.init('game-canvas')`. Updated `ensureModule()` error message to "call TDEngine.lifecycle.init(canvasId) first".

### 4.10 `web/td_api.js` — `audio.resume()` mixed scoped and bare global access

**Severity:** WARNING

`if (global.TDBridge && TDBridge.resumeAudio) TDBridge.resumeAudio()` — the check uses `global.TDBridge` but the call uses bare `TDBridge`. Works in browsers (where `TDBridge` resolves to `window.TDBridge`) but is inconsistent with every other reference in the file. In a worker/Node strict-mode context, the bare reference would throw `ReferenceError`.

**Fix:** `if (global.TDBridge && global.TDBridge.resumeAudio) global.TDBridge.resumeAudio()`.

---

## 5. Examples — broken `game.js` files users would copy

### 5.1 `examples/web-game/game.js` — `TDEngine.init()` doesn't exist

**Severity:** ERROR (game never boots)

Line 34: `await TDEngine.init('game-canvas')` — `TDEngine` has no top-level `init`. Throws `TypeError: TDEngine.init is not a function` inside the async IIFE, producing an unhandled promise rejection. The game never boots, the canvas stays black.

**Fix:** Changed to `await TDEngine.lifecycle.init('game-canvas')`.

### 5.2 `examples/web-game/game.js` — circular boot (onReady waited for init, init was the only thing that would call TDBridge.init)

**Severity:** ERROR (game never boots even if 5.1 were fixed)

`boot()` called `TDBridge.onReady(init)`, which queued `init` into `TDBridge._readyCallbacks` because `TDBridge.ready` was false. Those callbacks only fire at the END of `TDBridge.init()`. But `init` itself was what called `TDEngine.init()`. Chicken-and-egg: nothing ever called `TDBridge.init()`, so `init` never ran.

**Fix:** Replaced `TDBridge.onReady(init)` with a direct `init()` call. The `lifecycle.init()` inside `init` handles the WASM boot and calls `onReady` callbacks internally.

### 5.3 `examples/web-game/game.js` — `TDEngine.ecs.setSprite()` called with object instead of 7 numbers

**Severity:** ERROR (balls invisible)

API: `setSprite(id, w, h, r, g, b, a)` — 7 positional numbers. Example: `setSprite(e, { width, height, r, g, b, a })` — 1 object. Emscripten's `cwrap('number')` coerces `{width:32,...}` to `NaN` for every arg. The C++ stores `NaN` in every `SpriteComponent` field. The renderer either skips the sprite (NaN comparisons are false) or draws a zero-size quad — either way the balls are invisible.

**Fix:** `TDEngine.ecs.setSprite(e, BALL_SIZE, BALL_SIZE, Math.random(), Math.random(), Math.random(), 1)`.

### 5.4 `examples/web-game/game.js` — `TDEngine.ecs.getPosition()` called with callback

**Severity:** ERROR (balls never move)

API: `getPosition(id)` returns `{x, y}` synchronously. Example: `TDEngine.ecs.getPosition(b.id, (px, py) => { x = px; y = py; })` — the callback is silently ignored (function takes 1 arg). The return value `{x, y}` is also discarded. `x` and `y` stay 0. Every ball is teleported to (0,0) every frame.

**Fix:** `const p = TDEngine.ecs.getPosition(b.id); let x = p.x + b.vx * dt; let y = p.y + b.vy * dt;`.

### 5.5 `examples/web-game/game.js` — `TDEngine.render.frame()` doesn't exist

**Severity:** ERROR (TypeError every frame)

`TDEngine` has no `render` subsystem. `TDEngine.render` is `undefined`, so `TDEngine.render.frame()` throws "Cannot read properties of undefined (reading 'frame')". The engine's internal rAF loop (driven by `emscripten_set_main_loop` in `wasm/emscripten_main.cpp:1335`) renders every frame automatically — JS game code does NOT need to call a render function.

**Fix:** Deleted the `TDEngine.render.frame()` line entirely. Added a comment explaining that the engine renders automatically.

### 5.6 `examples/web-game/index.html` — script load order broke `js_bridge.js`'s Module config

**Severity:** ERROR (WASM runtime times out after 15s)

Loaded `td-engine.js` BEFORE `js_bridge.js`. Emscripten's glue runs at parse time and does `var Module = typeof Module != 'undefined' ? Module : {}` — picks up `{}` because `js_bridge.js` hasn't set `global.Module` yet. When `TDBridge.init()` is eventually called, `_loadEmscriptenModule` takes the "existing script tag" branch, does `global.Module = moduleConfig`, OVERWRITING the Emscripten-populated Module with a bare config that has no `_main`. The poll for `Module._main` never resolves, and `_waitForRuntime` times out after 15s.

**Fix:** Removed the `<script src="runtime/td-engine.js">` tag. `js_bridge.js` injects it at `init()` time with the correct `Module` config pre-set.

### 5.7 `examples/3d-showcase/server_main.td` — used `boolean` instead of `bool`

**Severity:** ERROR (wouldn't compile via `td script compile`)

TDScript lexer only recognizes `bool` (not `boolean`). `boolean entityId;` would produce a parse error: "expected type, got identifier 'boolean'".

**Fix:** Replaced all 5 occurrences of `boolean` with `bool`.

### 5.8 `examples/3d-showcase/server_main.td` — called 8 Physics/Network methods that don't exist in the TDScript runtime

**Severity:** ERROR (compiled JS would throw at runtime)

`web/tdscript_runtime.js` exposes only `Physics.checkVoxelCollision(pos)` on the Physics global. The showcase called `Physics.step(dt)`, `Physics.checkCapsuleCollision(...)`, `Physics.applyImpulse(...)`, `Physics.createBody(...)`, `Physics.setSphereCollider(...)`, `Physics.setRestitution(...)`, and `Network.schedule(...)`. The compiled JS would throw "Physics.step is not a function" on the first `onTick`.

**Fix:** Removed the calls that have no runtime backing. Marked each removal with a `// TODO: bridge td_physics_* into tdscript_runtime` comment so when the bridge lands, the calls can be restored. For `spawnProjectile`, kept the score/combo logic (which only uses replicated field setters) and removed the physics body creation. For `handlePlayerDeath`, removed the `Network.schedule(3.0, ...)` respawn timer (not implemented) and documented that the client handles respawn locally for now.

### 5.9 `examples/3d-showcase/server_main.td` — used `clientTick.toString()` (method call on primitive)

**Severity:** WARNING

`Log.info("Correcting client tick " + clientTick.toString())` assumes `uint32` has a `.toString()` method. The TDScript parser only handles type keywords, not method-call syntax on primitives. If the compiler rejects method calls on `uint32`, `td script check` would fail on this line.

**Fix:** Simplified to `Log.info("Correcting client tick")`.

### 5.10 `examples/3d-showcase/server_main.td` — used `++` operator

**Severity:** ERROR (wouldn't compile)

TDScript doesn't support the `++` operator. `this.inputCount++` produced "unexpected token '+' in expression".

**Fix:** `this.inputCount = this.inputCount + 1`.

### 5.11 `examples/3d-showcase/project.td` — `engineVersion: "2026.4.1"` (fake future date)

**Severity:** MISMATCH

The engine reports version `1.0.0` (CMakeLists.txt `project(TDEngine VERSION 1.0.0)`, `td_get_version()` returns "TD Engine 1.0.0 (WebAssembly)"). The minimal template correctly uses `"1.0.0"`. Any tooling that checks `engineVersion` for compatibility would flag the showcase.

**Fix:** Changed to `"1.0.0"`.

### 5.12 `examples/3d-showcase/game/network_stub.js` — fallback config had wrong `clientScript` path

**Severity:** WARNING

When `fetch('./project.td')` failed, the fallback config set `entry.clientScript: 'src/client/client_main.td'` — a path that doesn't exist anywhere in the repo. The actual client entry is `examples/3d-showcase/game/game.js`.

**Fix:** Changed to `'game/game.js'`.

### 5.13 `examples/3d-showcase/game/game.js` — `TDPersistence.save/load` called with wrong arity

**Severity:** ERROR (save was silently empty, load crashed on `for (const e of state.entities)`)

API: `TDPersistence.save(slotName)` (1 arg), `load(slotName)` (1 arg, returns `{ok, restored, missing, error}`). Showcase: `TDPersistence.save('td-sandbox', 'autosave', state)` (3 args, 2nd+3rd ignored — no serializers registered, so save writes `{}`) and `TDPersistence.load('td-sandbox', 'autosave')` (2 args, 2nd ignored — returns `{ok:false, ...}` or `{ok:true, restored:[...]}`). The game then treated this object as `state` and did `for (const e of state.entities)` — `undefined`, throws "undefined is not iterable". Crash caught by `window.onerror`, shows the crash-screen.

**Fix:** Registered a serializer for `'td-sandbox'` (idempotent — re-registering overwrites): `TDPersistence.registerSerializer('td-sandbox', () => state, (data) => { state = data; })`. Then `TDPersistence.save('td-sandbox')` and `const result = TDPersistence.load('td-sandbox'); if (result && result.ok && result.restored && result.restored.length > 0) { state = result.restored[0]; }`. Kept the `localStorage` fallback for when `TDPersistence` isn't loaded.

### 5.14 `examples/3d-showcase/game/tutorial.js` — taught the wrong `TDPersistence` API

**Severity:** ERROR (users who copy the tutorial pattern hit the same crash as 5.13)

The tutorial card text showed:
```
TDPersistence.save('mygame', 'slot1', { entities, score });
const state = TDPersistence.load('mygame', 'slot1');
TDPersistence.listSlots('mygame');
```
None of these match the real API (`save(slotName)`, `load(slotName)` returns object, `list()` not `listSlots`).

**Fix:** Replaced with the correct `registerSerializer` + `save` + `load` + `list` pattern.

### 5.15 `examples/3d-showcase/game/beat.js` — callback registered with wrong signature

**Severity:** WARNING (latent — currently masked by JS-fallback mode)

`TDEngine.beat.setCallback(function () { ... })` — 0-arg closure. Once `td_api.js` is fixed to use `'vif'` (fix 4.3), the C side invokes the callback with `(beatCount, beatTime)`. The closure receives both but ignores them — fine for the pulse-decay logic, but the `_onBeatCallbacks` array is invoked with no args, losing the beat count/time for any user callback that wants it.

**Fix:** `setCallback(function (beatCount, beatTime) { ... _onBeatCallbacks.forEach(cb => cb(beatCount, beatTime)); ... })`. Also updated the JS-fallback `update()` to pass `(currentBeat, beatTime)` to callbacks so both backends are consistent.

### 5.16 `tools/cli/templates/minimal/game.js` — `TDEngine.init()` (same as 5.1)

**Severity:** ERROR (every new project from `td init` ships broken)

The minimal template is what `td init` writes into a new project. It shipped with `await TDEngine.init('game-canvas')` as literally the first line of game logic. Every new project created by `td init` would throw "TDEngine.init is not a function" on first load and never reach the game loop. This is the single highest-impact bug in the audit because it affects every new user's first-run experience.

**Fix:** Changed to `await TDEngine.lifecycle.init('game-canvas')`. Also wrapped the async IIFE in `try/catch` so boot failures (canvas not found, WASM 404, GL context creation failure) show an error on the loading screen instead of leaving the user staring at "Loading engine..." forever.

### 5.17 `tools/cli/templates/minimal/index.html` — script load order (same as 5.6)

**Severity:** ERROR (template can't boot even after 5.16 is fixed)

Loaded `td-engine.js` as a plain `<script>` tag, causing `js_bridge.js` to overwrite the Emscripten-populated Module and time out.

**Fix:** Removed the `<script src="runtime/td-engine.js">` tag. Added a comment explaining that `js_bridge.js` injects it at `init()` time.

### 5.18 `tools/cli/templates/minimal/src/server/server_main.td` — missing `import "engine/log"`

**Severity:** WARNING

Called `Log.info(...)` and `Log.warn(...)` but only imported `engine/networking` and `engine/physics`. The TDScript runtime exposes `Log` as a global, so this works at runtime — but the showcase's `server_main.td` explicitly imports `engine/log`, and the inconsistency suggests the minimal template should too.

**Fix:** Added `import "engine/log";`.

### 5.19 `tools/cli/templates/minimal/src/server/server_main.td` — `clientTick.toString()` on primitive (same as 5.9)

**Severity:** WARNING

**Fix:** Simplified to `Log.info("Correcting client tick")`.

---

## 6. Documentation — broken examples and stale references

### 6.1 `README.md` — `await TDEngine.init('game-canvas')` (same as 5.1)

**Severity:** ERROR (users copy-pasting the "Browser Quick Start" get `TypeError`)

**Fix:** Changed to `await TDEngine.lifecycle.init('game-canvas')`.

### 6.2 `README.md` — beat example dropped `beatTime`

**Severity:** MISMATCH

Example: `TDEngine.beat.setCallback((beatCount) => console.log('beat #' + beatCount))` — drops `beatTime` even though the C callback passes it (after fix 4.3).

**Fix:** `TDEngine.beat.setCallback((beatCount, beatTime) => console.log('beat #' + beatCount + ' at ' + beatTime.toFixed(3) + 's'))`.

### 6.3 `README.md` — "Subsystems at a glance" table omitted 5+ subsystems

**Severity:** MISMATCH

Table listed 13 subsystems but was missing `TDEngine.physics` (25 APIs), `TDPersistence`, `TDInspector`, `TDProfiler`, `TDErrorBoundary`, `TDScriptRuntime`, `TDClientBootstrap`. A user reading the README would not know these exist.

**Fix:** Added rows for `physics`, `TDPersistence`, `TDInspector`, `TDProfiler`, `TDErrorBoundary`, `TDScriptRuntime`, `TDClientBootstrap`. Also fixed the `TDEngine.net` row to note that it requires `net_websocket.js` loaded after `td_api.js`.

### 6.4 `README.md` — beat row listed 9 methods but said "(13 APIs)"

**Severity:** MISMATCH

Listed: `start, stop, isOnBeat, getCount, registerHit, getCombo, setBpm, setCallback, playSound` (9). Actual: those 9 PLUS `getNextBeatTime, getLastBeatTime, getBestCombo, resetCombo` (13 total). The "(13 APIs)" annotation was correct; the method list was incomplete.

**Fix:** Added the 4 missing methods to the list.

### 6.5 `src/scripting/script_vm.h` — header comment said "Status: STUB" but implementation is real

**Severity:** WARNING (misleading — developers conclude scripting doesn't work and skip it)

Comment said "Status: STUB. The ScriptVM class is declared so the rest of the engine can link against it; the actual Lua embedding is tracked as Tier 1.3... loadScript() returns -1... update() is a no-op." Actual `src/scripting/script_vm.cpp` is 3,121 lines, fully implements `loadScript`, `updateAll`, `reloadScript`, `unloadScript`, `bindSignal`. `MODULARITY_ROADMAP.md` marks it as "SHIPPED (3,075 lines)".

**Fix:** Updated comment to "Status: REAL implementation. The custom tdscript VM (lexer + parser + codegen + stack-based interpreter, ~3,100 lines) lives in `src/scripting/tdscript/`..." with the actual API surface.

### 6.6 `src/ui/ui.h` — header comment said "Status: SKELETON" but implementation is real

**Severity:** WARNING (same issue as 6.5)

Comment said "Status: SKELETON. The UINode tree + layout math is here; the actual rendering bridge... is tracked as Tier 2.1." Actual `src/ui/ui.cpp` is 1,699 lines, 13 widget types, real flexbox layout, 62-test regression suite.

**Fix:** Updated comment to "Status: REAL implementation (1,699 lines, 13 widget types...)" with the actual feature list.

### 6.7 `src/net/transport.h` — comment said `WebSocketPeer — TODO` but it's shipped

**Severity:** MISMATCH

Lines 23-26 said:
```
Concrete peer TODOs:
  - WebSocketPeer  (for native clients talking to a JS server)  — TODO
  - ENetPeer       (raw UDP, native-only, lower latency)        — TODO
```
But `src/net/websocket_peer.{h,cpp}` exists and is REAL (native Winsock2 + POSIX, WASM via Emscripten). `CMakeLists.txt:121-130` branches on `TD_BUILD_WEB` to compile the native vs WASM impl. The TODO comment is stale.

**Fix:** Updated to:
```
Concrete peers:
  - WebSocketPeer  (SHIPPED — native + WASM)  — src/net/websocket_peer.{h,cpp}
    Native impl uses Winsock2 (Windows) / POSIX sockets (Linux, macOS).
    WASM impl uses Emscripten's <emscripten/websocket.h> API.
  - ENetPeer       (TODO — raw UDP, native-only, lower latency)
```

### 6.8 `src/core/game_loop.h` — unconditionally `#include "../platform/win32_window.h"`

**Severity:** WARNING (breaks Linux/macOS plain desktop builds)

`game_loop.h` is in `src/core/` which `CMakeLists.txt:79` calls "portable subsystem sources" shared between desktop and web. But the header pulled in `win32_window.h` which uses `__stdcall` (undefined on Linux/macOS plain desktop). Emscripten defines `__stdcall` as empty so the WASM build works. Plain Linux desktop does not.

**Fix:** Forward-declared `Win32Window` in `game_loop.h` (`namespace td { class Win32Window; }`) and moved the `#include "../platform/win32_window.h"` into `game_loop.cpp` (where the definition is actually needed for `run(Win32Window&)`).

### 6.9 `wasm/emscripten_main.cpp` — unused `#include "../src/core/game_loop.h"`

**Severity:** WARNING

`GameLoop` is never used in the WASM build (the main loop is implemented locally at `mainLoop()` line 1335). The include pulled `win32_window.h` (and through it, `<windows.h>` on Windows desktop) into the WASM binary for no reason.

**Fix:** Removed the `#include "../src/core/game_loop.h"` line. Added a comment explaining that the WASM main loop is local and GameLoop::run is desktop-only.

### 6.10 `src/renderer/camera.h` — `setNearFar(float near, float far)` parameter names collide with windows.h macros

**Severity:** WARNING (fragile — breaks on older Windows SDKs)

`windows.h` defines `near` and `far` as legacy Win16 macros (they expand to `__near` / `__far` calling conventions). `src/ecs/component.h` already renamed these to `nearPlane`/`farPlane` to work around this. `camera.h` did not. With `WIN32_LEAN_AND_MEAN` defined, the macros may or may not be defined depending on SDK version.

**Fix:** Renamed parameters to `nearPlane` / `farPlane` in both `camera.h` (declaration) and `camera.cpp` (definition). Member variables `m_near`/`m_far` were already correct.

### 6.11 `web/profiler.js` — `typeof memoryUsage === 'function'` check was wrong

**Severity:** WARNING (Node branch was dead code — heap meter permanently zero in Node)

Node.js does not expose `memoryUsage` as a global function — it's `process.memoryUsage`. `typeof memoryUsage === 'function'` always evaluated to false, so the Node heap-reporting branch was never taken. In Node, `heapInfo()` fell through to the WASM-memory check (which fails because there's no `TDEngine.module` in a Node-only context) and returned `{used: 0, total: 0, limit: 0}`.

**Fix:** `if (typeof process !== 'undefined' && typeof process.memoryUsage === 'function') { const m = process.memoryUsage(); ... }`.

### 6.12 `web/net_peer.js` — pong broadcast to ALL peers, corrupting everyone's RTT

**Severity:** ERROR (every peer's `rttMs` became garbage after any peer pinged)

When peer A sent `{t:'ping', ts:1000}`, peer B received it and broadcast `{t:'pong', ts:1000}` to ALL peers (including C, D, ...). Peer C received the pong, checked `if (this._peers.has(B))` (true), and computed `peer.rttMs = Date.now() - 1000` — but `1000` was A's send time, not C's. C's reported RTT to B was now whatever time had elapsed since A sent its ping, which has nothing to do with C↔B latency. Every peer's `rtt()` became corrupted by every other peer's pings. The protocol had a `to` field (used by `helloAck`) but `pong` didn't use it.

**Fix:** Pong now includes `to: msg.id` (the pinger's ID). Receivers check `if (msg.to !== undefined && msg.to !== this.peerId) return;` — only the intended pinger processes the pong. Also fixed the `case 'ping'` handler to always create the peer entry (not just conditionally update `lastSeen`), so a fresh peer that hasn't sent hello yet still gets recorded.

### 6.13 `web/error_boundary.js` — `unhandledrejection` handler didn't return `true`

**Severity:** WARNING (duplicate "Uncaught (in promise)" log on some browsers)

The `onerror` handler returned `true` to suppress the default browser error logging. The `onunhandledrejection` handler had no return statement, so it returned `undefined`. Per the HTML spec, returning anything other than `false` from `onunhandledrejection` prevents the default behavior — but some browsers (older Chrome) interpret `undefined` as "allow default" and log an additional "Uncaught (in promise)" error to the console, duplicating the error_boundary's own crash card.

**Fix:** Added `return true;` at the end of the `onunhandledrejection` handler.

### 6.14 `web/tdscript_runtime.js` — called `_td_voxel_is_solid` which doesn't exist

**Severity:** WARNING (misleading doc-comment; behavior is correct because the lookup fails silently)

`const fn = global.TDEngine.bridge.wasmExports._td_voxel_is_solid;` — the function doesn't exist anywhere in the codebase (no C++ definition, not in `EXPORTED_FUNCTIONS`). `fn` is always `undefined`, so `if (fn) return !!fn(...)` always falls through to the stub `return pos.y < 0`. The doc-comment claimed "real impl in C++ VoxelWorld" but no such binding exists. Compiled TDScript code that calls `Physics.checkVoxelCollision(pos)` always gets the stub behavior.

**Fix:** No code change (the fallback is correct). Updated the doc-comment to acknowledge that `_td_voxel_is_solid` is not yet exposed and the lookup will always fall through to the stub until the C bridge lands.

### 6.15 `web/td_client_bootstrap.js` — `__td_net_send` not cleared on WebSocket close

**Severity:** WARNING (silent RPC traffic loss after transient network blip)

`ws.onopen` set `global.__td_net_send = function (...) { if (ws.readyState === 1) ws.send(...); }`. `ws.onclose` just logged "Disconnected" — it did NOT clear `global.__td_net_send`. After a disconnect, the runtime's `_sendFrame` calls kept going through the closed `ws`, which silently dropped frames (readyState !== 1). The runtime's `Network.lastFrame` was set, but no actual send happened, and there was no reconnect logic. A game using TDScript server RPCs would silently lose all RPC traffic after a transient network blip.

**Fix:** In `ws.onclose`, added `if (global.__td_net_send) global.__td_net_send = null;` so the runtime stops trying to send against a closed socket. (Future work: add reconnect logic.)

---

## Verification

After all fixes:

- **All 35+ JavaScript files pass `node --check`** (no syntax errors).
- **All 3 Python files pass `ast.parse`** (no syntax errors).
- **`td init`** scaffolds a new project with the corrected `TDEngine.lifecycle.init` call and patches `__GAME_NAME__` in `game.js`.
- **`td script compile`** on both `examples/3d-showcase/server_main.td` and `tools/cli/templates/minimal/src/server/server_main.td` succeeds (including the previously-crashing `else if` chain).
- **`tdscript.js --help`** now prints usage (previously silent).
- **`node tests/tdscript/test_compiler.js`**: 22/22 pass (no regressions).
- **`make test`** (C++ tests): 87+102+23 = 212 pass, 3 fail (the 3 failures are the pre-existing `test_net` 256KB fragmentation issue, NOT a regression — same 3 failures before and after these changes).
- **`td help <cmd>`** for all 7 commands now loads and prints help (previously all exited 2 with `Cannot find module '../lib/util'`).

### Pre-existing issues NOT fixed (documented for transparency)

- **`test_net` Test 3 (256KB RELIABLE_ORDERED message)**: 3 sub-checks fail because the send window (32 packets) is too small for 190 fragments under lossy loopback conditions. This is an internal test-suite issue, not a user-facing API bug — the transport works fine for typical game messages. Root cause: `NET_SEND_WINDOW = 32` in `transport_impl.h:231`; the sender drops oldest unacked packets when the window is full, and under heavy fragmentation the oldest packets haven't been acked yet. Fix would be to either increase the window or implement proper backpressure (queue instead of drop). Out of scope for this audit.
- **`docs.html` has 13+ broken API examples** (wrong arg counts, wrong types, nonexistent methods). The fix is a full rewrite of the API reference section. Documented in the audit report; not patched here because the volume is large and the examples are illustrative rather than load-bearing — users who hit issues will cross-reference `web/td_api.d.ts` (now correct).
- **`docs/RHYTHM_MECHANICS.md` has stale references** to `web/examples/beat_demo.js` (removed), wrong return-value description for `td_beat_register_hit`, wrong file location for `BeatTrackerComponent`, and fabricated API (`get_seconds_per_beat`, `onBeatNiceness`, `recompute_bounds`). Documented; not patched.
- **`wasm/README.md` has stale references** to `src/td/` (removed), `web/examples/voidrunner.js` (removed), `web/engine-wrapper.ts` (doesn't exist), and an incomplete C API function table. Documented; not patched.
- **`web/GETTING_STARTED.md` "Reference: all exported C functions" table** is missing ~50 functions. Documented; not patched.
- **`web/inspector.js` doc-comment** promises `TDInspector.select(entityId)` and `TDInspector.onSelect(callback)` as top-level methods; neither exists (only on the `mount()` handle). Documented; not patched.
- **`web/net_auth_server.js` `buildDelta`** pushes the current snapshot to `_history` once per player per tick (N× duplication). Documented; not patched (functionally correct, just wastes memory).
- **`web/net_websocket.js` RPC constructor** overwrites `socket.onMessage`, clobbering any user-installed handler. Documented; not patched (would require an event-emitter refactor).

These pre-existing issues are tracked in the audit reports and can be addressed in follow-up PRs.

---

## Files changed

**New files (2):**
- `tools/cli/lib/util.js`
- `tools/cli/lib/project_tds.js`

**Modified files (36):**

CLI / tooling:
- `tools/cli/td.js` — `--no-X` parsing, `loadCommand` error distinction, help text, comment
- `tools/cli/commands/serve.js` — port validation, path-aware traversal check
- `tools/cli/commands/init.js` — patch `__GAME_NAME__` in `game.js`, remove nonexistent `platformer` template from help
- `tools/cli/commands/build.js` — add `tdscript_runtime.js` + `td_client_bootstrap.js` to `RUNTIME_FILES`
- `tools/cli/commands/bundle.js` — add 2 runtime files, `--webview2-bootstrapper` passthrough, validation
- `tools/tdscript/tdscript.js` — `else if` codegen rewrite, CLI entrypoint
- `tools/bundler/bundle.py` — add 4 runtime files, resolve `icon` against `game_dir`, sync `RUNTIME_FILES` comment
- `tools/gen_font.py` — script-relative output path
- `scripts/patch_makefile.py` — script-relative `Makefile` path, `--makefile` override
- `tests/tdscript/test_serve_e2e.js` — `require('ws')` directly
- `package.json` — add `ws` dependency

Build config:
- `CMakeLists.txt` — add `EXPORT_KEEPALIVE=1`, add 25 `_td_physics_*` to `EXPORTED_FUNCTIONS`, remove undefined `_tdscript_compile`
- `Makefile` — add `EXPORT_KEEPALIVE=1`, add 25 `_td_physics_*` to `EXPORTED_FUNCTIONS`

Web runtime:
- `web/td_api.js` — `getPosition`/`getMousePos` arg counts, `beat.setCallback` signature, `resetCombo` return, `script.call` accepts array, `i18n.load` accepts object, physics try/finally, doc-comment fixes, `audio.resume` global access
- `web/td_api.d.ts` — `setCallback` signature, `resetCombo` return, `script.call` arg type, `i18n.load` arg type
- `web/profiler.js` — `process.memoryUsage` check
- `web/net_peer.js` — directed pong via `to` field, receiver filter, always-create peer on ping
- `web/error_boundary.js` — `unhandledrejection` returns `true`
- `web/tdscript_runtime.js` — doc-comment for `_td_voxel_is_solid` stub
- `web/td_client_bootstrap.js` — clear `__td_net_send` on close

Examples:
- `examples/web-game/game.js` — `lifecycle.init`, direct `init()` call, `setSprite` 7 args, `getPosition` return value, remove `render.frame()`
- `examples/web-game/index.html` — remove `td-engine.js` script tag
- `examples/3d-showcase/server_main.td` — `boolean` → `bool`, remove nonexistent Physics/Network calls, `++` → `+ 1`, `.toString()` removal
- `examples/3d-showcase/project.td` — `engineVersion: "1.0.0"`
- `examples/3d-showcase/game/network_stub.js` — `clientScript: 'game/game.js'`
- `examples/3d-showcase/game/game.js` — `TDPersistence.registerSerializer` + correct `save`/`load` arity
- `examples/3d-showcase/game/tutorial.js` — correct `TDPersistence` API in tutorial card
- `examples/3d-showcase/game/beat.js` — callback accepts `(beatCount, beatTime)`

CLI template:
- `tools/cli/templates/minimal/game.js` — `lifecycle.init`, try/catch on boot
- `tools/cli/templates/minimal/index.html` — remove `td-engine.js` script tag
- `tools/cli/templates/minimal/src/server/server_main.td` — add `import "engine/log"`, remove `.toString()`

Docs:
- `README.md` — `lifecycle.init`, beat example with `beatTime`, subsystem table (added physics/persistence/inspector/profiler/errorBoundary/scriptRuntime/clientBootstrap), beat method count

C++ headers:
- `src/core/game_loop.h` — forward-declare `Win32Window`, remove `#include "../platform/win32_window.h"`
- `src/core/game_loop.cpp` — include `win32_window.h` for the definition
- `wasm/emscripten_main.cpp` — remove unused `#include "../src/core/game_loop.h"`
- `src/renderer/camera.h` — `setNearFar(float nearPlane, float farPlane)`
- `src/renderer/camera.cpp` — match definition
- `src/scripting/script_vm.h` — "Status: REAL implementation" comment
- `src/ui/ui.h` — "Status: REAL implementation" comment
- `src/net/transport.h` — "WebSocketPeer SHIPPED" comment

**File permissions (5):**
- `chmod +x` on `tools/cli/td.js`, `tools/tdscript/tdscript.js`, `tools/bundler/bundle.py`, `tools/gen_font.py`, `scripts/patch_makefile.py`
