// Stub for the global `td::GLFunctions gl;` symbol.
//
// The real definition lives in src/renderer/gl_renderer.cpp, but that file
// #includes <windows.h> on non-Emscripten desktop builds and so cannot be
// compiled on Linux CI. This stub provides just the global so that the UI
// tests (which never actually call any GL function — they only exercise
// layout, hit-testing, and input dispatch) link cleanly.
//
// The function pointers in `gl` are all null; dereferencing them would
// crash. The UI tests are structured so that no GL code path runs.
#include "../src/renderer/gl_renderer.h"

namespace td {
GLFunctions gl;
} // namespace td
