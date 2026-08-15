#pragma once
// =============================================================================
//  TD Engine — WebView2 Host Shell
// =============================================================================
//  A minimal native window that embeds Microsoft Edge WebView2 to run a
//  finished JS game (the same JS + WASM that runs in a browser tab).
//
//  Why this exists:
//    The engine compiles to WebAssembly and ships a JS bridge. In the browser,
//    the user just opens index.html. To sell a game outside the browser
//    (Steam / itch.io), we need a Windows .exe that hosts the same web runtime.
//    This is that host. It is NOT a game engine — it's a thin shell.
//
//  What it does:
//    1. Creates a Win32 window (reusing td::Win32Window).
//    2. Dynamically loads WebView2Loader.dll (no link-time dependency).
//    3. Creates a WebView2 environment + controller on the window's HWND.
//    4. Maps a virtual HTTPS host (https://td-game.local/) to the on-disk
//       game/ folder, so WASM fetch() works (file:// is blocked for fetch).
//    5. Navigates to https://td-game.local/index.html.
//
//  What it does NOT do:
//    - Render anything itself (WebView2 + WebGL2 does all rendering)
//    - Implement game logic (the JS game does that)
//    - Ship the WebView2 runtime (the installer handles that)
//
//  CLI flags (for the installer's [Run]/[UninstallRun] hooks):
//    --post-install <path>   First-run setup hook (exit 0 quickly)
//    --pre-uninstall <path>  Pre-uninstall cleanup hook
//    --load-save <file>      Launch and immediately load a .tdsave file
//    --load-scene <file>     Launch and immediately load a .tdscene file
//
//  Build:
//    cmake -B build -S . -DTD_BUILD_HOST=ON
//    cmake --build build --target td-host
// =============================================================================

#include "../platform/win32_window.h"

namespace td {
namespace host {

// ---- Configuration ---------------------------------------------------------

struct HostConfig {
    // Window
    const char* title = "TD Game";
    int width = 1280;
    int height = 720;
    bool resizable = true;

    // Paths (relative to the .exe, or absolute)
    // The host .exe ships next to `runtime/` and `game/` folders.
    const char* runtimeDir = "runtime";  // td-engine.js/.wasm + JS bridge
    const char* gameDir = "game";        // user's index.html + JS + assets
    const char* entryPoint = "index.html";

    // Virtual host name that maps to gameDir on disk. WebView2 serves files
    // from this folder over HTTPS, so fetch() works without a real HTTP server.
    const char* virtualHost = "td-game.local";

    // User data folder for WebView2 (cookies, cache, IndexedDB). Defaults to
    // %LOCALAPPDATA%\<exe-name>\webview-data. Set this to keep the engine's
    // WASM cache separate from other WebView2 apps.
    const char* userDataFolder = nullptr;  // nullptr = default location

    // Whether to allow WebView2 dev tools (F12). Set false for shipping.
    bool allowDevTools = false;
};

// ---- Lifecycle -------------------------------------------------------------

// Run the host. Blocks until the window is closed. Returns the process exit code.
int run(const HostConfig& config);

// ---- Installer hooks -------------------------------------------------------
// These are called from main() when --post-install / --pre-uninstall is passed.
// They let the host do first-run setup / pre-uninstall cleanup without
// launching the full window. Both exit 0 quickly so the installer doesn't hang.
int runPostInstallHook(const char* installPath);
int runPreUninstallHook(const char* installPath);

} // namespace host
} // namespace td
