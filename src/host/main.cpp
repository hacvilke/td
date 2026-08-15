// =============================================================================
//  TD Engine — WebView2 Host Shell (entry point)
// =============================================================================
//  This is the main() for the td-host.exe target. It parses CLI args and
//  dispatches to either:
//    - td::host::run()              (normal launch)
//    - td::host::runPostInstallHook() (called by the installer's [Run] section)
//    - td::host::runPreUninstallHook() (called by the installer's [UninstallRun])
//
//  The installer writes registry values that tell it whether the host supports
//  these hooks — see the [Code] block in installer/td-game-template.iss.
// =============================================================================

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)

#include "webview_host.h"
#include <cstring>
#include <string>
#include <windows.h>

// ---- Minimal arg parser ----------------------------------------------------

static bool hasFlag(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0) return true;
    }
    return false;
}

static const char* getFlagValue(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], flag) == 0) return argv[i + 1];
    }
    return nullptr;
}

// ---- Entry point -----------------------------------------------------------

int main(int argc, char** argv) {
    // ---- Installer hooks (exit quickly, don't open a window) ---------------
    if (const char* p = getFlagValue(argc, argv, "--post-install")) {
        return td::host::runPostInstallHook(p);
    }
    if (const char* p = getFlagValue(argc, argv, "--pre-uninstall")) {
        return td::host::runPreUninstallHook(p);
    }

    // ---- Normal launch -----------------------------------------------------
    td::host::HostConfig config;

    // Allow overriding paths via CLI (useful for dev — in production the
    // installer puts runtime/ and game/ next to the .exe)
    if (const char* p = getFlagValue(argc, argv, "--game-dir")) config.gameDir = p;
    if (const char* p = getFlagValue(argc, argv, "--runtime-dir")) config.runtimeDir = p;
    if (const char* p = getFlagValue(argc, argv, "--entry")) config.entryPoint = p;
    if (const char* p = getFlagValue(argc, argv, "--title")) config.title = p;
    if (const char* p = getFlagValue(argc, argv, "--virtual-host")) config.virtualHost = p;

    // Dev tools toggle (default off for shipping, on for dev builds)
    config.allowDevTools = hasFlag(argc, argv, "--dev-tools");

    // Load-save / load-scene launch hooks (for file associations)
    // These are passed through to the JS game via window.__TD_LAUNCH_ARG__
    // (the JS side can read them on boot)
    if (const char* p = getFlagValue(argc, argv, "--load-save")) {
        SetEnvironmentVariableA("TD_LOAD_SAVE", p);
    }
    if (const char* p = getFlagValue(argc, argv, "--load-scene")) {
        SetEnvironmentVariableA("TD_LOAD_SCENE", p);
    }

    return td::host::run(config);
}

#else
// Non-Windows: this target isn't built. See CMakeLists.txt.
int main() { return 0; }
#endif
