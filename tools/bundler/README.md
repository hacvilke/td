# TD Engine — EXE Bundler (Inno Setup-style installer generator)

Takes a finished web game (your `index.html` + JS + assets + the engine WASM runtime) and produces a Windows installer `.exe` using [Inno Setup](https://jrsoftware.org/isinfo.php).

The installer is a real Inno Setup installer — not a self-extracting zip. It has:

- A modern wizard UI (language selection, install location, shortcuts)
- Per-machine and per-user install scope
- Start Menu + Desktop shortcuts
- Registry entries (install path, version, file associations)
- File associations for `.tdsave` and `.tdscene`
- Custom install hooks (`--post-install`) and uninstall hooks (`--pre-uninstall`)
- Optional save-data cleanup on uninstall (user-prompted, never silent)
- WebView2 runtime detection + auto-download if missing
- Optional WebView2 bootstrapper bundling for offline install
- LZMA2 ultra compression (typical installers are 60-70% smaller than the raw files)

---

## Quick start

### 1. Install prerequisites

**Inno Setup 6+** (Windows-only, free):
- Download: <https://jrsoftware.org/isdl.php>
- Default install path: `C:\Program Files (x86)\Inno Setup 6\`
- `bundle.py` auto-detects ISCC.exe on PATH or at the default path.

**Build the host shell** (the native `.exe` that runs your game in WebView2):
```bash
cmake -B build -S . -DTD_BUILD_HOST=ON
cmake --build build --target td-host
# Output: build/bin/td-host.exe
```

**Build the web runtime** (the WASM engine + JS bridge):
```bash
make web
# Output: web/td-engine.js + web/td-engine.wasm
```

### 2. Create a game project

Your game folder needs at minimum an `index.html`:

```
my-game/
├── index.html         ← loads runtime/td-engine.js + runtime/js_bridge.js
├── game.js            ← your game code
├── assets/
│   └── ship.png
└── game.tdproj        ← optional project file (see below)
```

Optional `game.tdproj`:
```json
{
  "name": "My Cool Game",
  "version": "1.0.0",
  "publisher": "Some Studio",
  "id": "my-cool-game",
  "url": "https://example.com",
  "icon": "assets/icon.ico",
  "bundle_runtime": false
}
```

### 3. Run the bundler

```bash
python tools/bundler/bundle.py \
    --game my-game \
    --name "My Cool Game" \
    --version 1.0.0 \
    --publisher "Some Studio" \
    --icon my-game/assets/icon.ico \
    --out MyCoolGame-Setup.exe
```

Output: `MyCoolGame-Setup.exe` — a complete Windows installer.

### 4. Test the installer

Double-click `MyCoolGame-Setup.exe`. The wizard will:
1. Check for WebView2 runtime (offer to install if missing)
2. Ask for install location
3. Install files to `C:\Program Files\My Cool Game\`
4. Create Start Menu + optional Desktop shortcuts
5. Register file associations (if you kept those tasks)
6. Run the post-install hook (if the host supports it)
7. Offer to launch the game

---

## Installed layout

```
C:\Program Files\My Cool Game\
├── MyCoolGame.exe              ← the WebView2 host shell (renamed from td-host.exe)
├── runtime\
│   ├── td-engine.js            ← Emscripten glue
│   ├── td-engine.wasm          ← compiled C++ engine (~1 MB)
│   ├── js_bridge.js            ← TDBridge global
│   ├── td_api.js               ← TDEngine.* namespace
│   ├── net_websocket.js        ← (only if your game imports it)
│   ├── net_peer.js
│   └── ...                     ← other web/ modules your game uses
├── game\
│   ├── index.html              ← your game's entry point
│   ├── game.js
│   └── assets\
│       └── ship.png
└── unins000.exe                ← Inno Setup's uninstaller
```

Per-user save data lives in `%APPDATA%\Some Studio\My Cool Game\saves\` — this is NOT touched during install or update, only optionally during uninstall (with a user prompt).

---

## How it works

```
┌─ Your game folder ──────────────────┐
│  index.html + game.js + assets/      │
└──────────────┬───────────────────────┘
               │
               ▼
┌─ tools/bundler/bundle.py ────────────────────────────┐
│  1. Validate game folder (needs index.html)           │
│  2. Stage files:                                      │
│       staging/host/MyCoolGame.exe  ← from build/bin/  │
│       staging/runtime/*            ← from web/        │
│       staging/game/*               ← from your folder │
│  3. Substitute {{placeholders}} in the .iss template  │
│  4. Invoke ISCC.exe on the generated .iss             │
│  5. Copy the output to --out                          │
└──────────────┬───────────────────────────────────────┘
               │
               ▼
       MyCoolGame-Setup.exe
```

### The host shell (`src/host/`)

The host is a ~300-line C++ program that:
1. Creates a Win32 window (reusing `td::Win32Window` from the engine)
2. Dynamically loads `WebView2Loader.dll` (no link-time dependency)
3. Creates a WebView2 environment + controller on the window's HWND
4. Maps a virtual HTTPS host (`https://td-game.local/`) to the on-disk `game/` folder
5. Navigates to `https://td-game.local/index.html`

The virtual host mapping is the key trick: WebView2 serves files from disk over HTTPS, which makes `fetch()` work for the WASM file. (`file://` URLs block `fetch()` in modern browsers for security reasons — this is the documented Microsoft-blessed workaround.)

### The installer template (`installer/td-game-template.iss`)

A standard Inno Setup script with placeholder substitution. The template has:

| Section | What it does |
|---|---|
| `[Setup]` | App identity, compression (LZMA2 ultra), wizard style, architecture |
| `[Files]` | Stages host exe, runtime/, game/, optional WebView2 bootstrapper |
| `[Icons]` | Start Menu, Desktop (optional), Quick Launch (legacy) |
| `[Registry]` | Install record + `.tdsave` / `.tdscene` file associations |
| `[Run]` | Post-install: launch game, run `--post-install` hook |
| `[UninstallRun]` | Run `--pre-uninstall` hook before files are deleted |
| `[Code]` | Pascal: Win10 1809+ check, WebView2 runtime check, save-data cleanup prompt |

### Custom install/uninstall steps

Two ways to add custom logic:

**1. Custom `.iss`** — drop an `installer.iss` in your game folder. `bundle.py` will use it instead of the default template. Copy the default template and modify.

**2. Host hooks** — implement `--post-install` and `--pre-uninstall` in your host build. The default `src/host/webview_host.cpp` has stubs that you can extend:

```cpp
int runPostInstallHook(const char* installPath) {
    // Pre-warm WASM cache, register with Game Explorer,
    // set up cloud sync, etc.
    return 0;  // must exit 0 quickly — installer blocks on this
}

int runPreUninstallHook(const char* installPath) {
    // Flush saves to cloud, deregister from multiplayer,
    // kill background daemons.
    return 0;
}
```

The installer probes the registry to see if your host supports these hooks before invoking them, so you don't need to worry about calling a host that doesn't implement them.

---

## CLI reference

```
python tools/bundler/bundle.py --game <dir> [options]

Required:
  --game <dir>              Path to the game folder (must contain index.html)

Game identity (or set in game.tdproj):
  --name <str>              Game display name (e.g. "My Cool Game")
  --version <str>           Version string (e.g. "1.0.0")
  --publisher <str>         Publisher name
  --id <slug>               App ID slug (default: slugified name)
  --icon <path>             Path to .ico file
  --url <str>               Publisher URL

Build paths:
  --host-exe <path>         Path to td-host.exe (default: build/bin/td-host.exe)
  --web-dir <path>          Path to web/ folder (default: ./web)
  --staging <path>          Staging directory (default: build/bundle-<id>/)
  --template <path>         Path to .iss template (default: installer/...)

Installer:
  --iscc <path>             Path to ISCC.exe (default: search PATH + common paths)
  --bundle-runtime          Bundle the WebView2 bootstrapper (~2MB) for offline install
  --webview2-bootstrapper <path>  Path to MicrosoftEdgeWebview2Setup.exe (with --bundle-runtime)
  --license <path>          Path to LICENSE file (shown in wizard)
  --readme <path>           Path to README file (shown after install)

Output:
  --out <path>              Output .exe path (default: ./MyGame-Setup.exe)
  --dry-run                 Stage + generate .iss but don't invoke ISCC
  --keep-staging            Don't delete staging dir after build (for debugging)
```

---

## Production build: WebView2 SDK

The default `src/host/webview_host.cpp` is an architectural skeleton. It dynamically loads `WebView2Loader.dll` and declares the minimal COM interfaces inline, so the build has **zero external dependencies**.

For a production build with the full WebView2 SDK (recommended for shipped games):

1. **Install via vcpkg** (cleanest):
   ```bash
   vcpkg install microsoft-webview2:x64-windows
   cmake -B build -S . -DTD_BUILD_HOST=ON -DCMAKE_TOOLCHAIN_FILE=path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
   ```

2. **Or download the NuGet package** manually:
   - Download `Microsoft.Web.WebView2` from <https://www.nuget.org/packages/Microsoft.Web.WebView2>
   - Extract `build/native/include/WebView2.h` to a path CMake can find
   - Extract `build/native/x64/WebView2LoaderStatic.lib` for linking

3. **Replace the inline COM declarations** in `webview_host.cpp` with `#include <WebView2.h>` and link `WebView2LoaderStatic.lib`.

The production build gives you:
- Compile-time type checking on all WebView2 calls
- Access to the full WebView2 API surface (not just the minimal subset)
- Better error messages from the compiler

---

## CI integration

To build installers in GitHub Actions, add this to `.github/workflows/native.yml`:

```yaml
- name: Install Inno Setup
  run: choco install innosetup -y --no-progress

- name: Build host shell
  run: |
    cmake -B build -S . -DTD_BUILD_HOST=ON
    cmake --build build --target td-host --config Release

- name: Build web runtime
  run: make web

- name: Bundle example game
  run: |
    python tools/bundler/bundle.py \
      --game examples/web-game \
      --out td-bouncing-ball-setup.exe

- name: Upload installer
  uses: actions/upload-artifact@v4
  with:
    name: td-bouncing-ball-installer
    path: td-bouncing-ball-setup.exe
```

---

## Troubleshooting

**"ISCC.exe not found"**
Install Inno Setup 6+ from <https://jrsoftware.org/isdl.php>, or pass `--iscc /path/to/ISCC.exe`.

**"Host .exe not found"**
Build it first: `cmake --build build --target td-host`. Or pass `--host-exe /path/to/td-host.exe`.

**"Required runtime file missing: td-engine.wasm"**
Build the web runtime first: `make web`. This compiles the C++ engine to WebAssembly.

**"WebView2 Runtime was not detected" (at install time)**
The installer offers to download it from Microsoft. If you want offline install, pass `--bundle-runtime` to `bundle.py` (requires `--webview2-bootstrapper PATH` pointing to `MicrosoftEdgeWebview2Setup.exe`, which you can download from <https://developer.microsoft.com/microsoft-edge/webview2/>).

**SmartScreen warns users**
This is expected — the installer isn't code-signed (code signing certs cost money). Users click "More info → Run anyway". For commercial distribution, purchase an EV code signing certificate.

**Installer is huge**
The WASM file is typically 1-3 MB. LZMA2 ultra compression brings the installer down to ~60-70% of the raw file size. If your game ships lots of PNG/WAV assets, consider converting to WebP/Opus before bundling.

---

## Architecture decisions

**Why Inno Setup, not WiX/NSIS?**
Inno Setup is free, scriptable, well-documented, runs headless in CI, and supports every installer feature we need. WiX (MSI) is heavier and overkill for indie games. NSIS works but is less polished and has a worse default UI.

**Why WebView2, not CEF/Electron/Tauri?**
WebView2 ships with Windows 10/11 (it's the Edge runtime). The loader DLL is ~150 KB vs CEF's ~150 MB or Electron's ~80 MB. It's Microsoft-supported and actively maintained. Tauri needs Rust, which isn't in our stack.

**Why a virtual HTTPS host, not a real HTTP server?**
WebView2's `SetVirtualHostNameToFolderMapping` maps a virtual host to an on-disk folder. This makes `fetch()` work for the WASM file without bundling a real HTTP server. It's the documented Microsoft-blessed pattern for exactly this use case.

**Why dynamic DLL loading, not static linking?**
The repo has a "zero external libraries" rule. WebView2 is a platform feature of Windows, not a third-party lib. Dynamic loading means the build has no SDK dependency — you can build the host without installing anything extra. For production, you can switch to static linking (see "Production build" above).

**Why per-user save data in `%APPDATA%`, not in the install dir?**
Install dirs are often read-only for standard users (Program Files). Save data must go to `%APPDATA%` (per-user, always writable). The installer cleans this up on uninstall — with a user prompt, never silently.
