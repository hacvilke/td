// TD Engine — Emscripten/WebAssembly Entry Point
// Compile with: emcc wasm/emscripten_main.cpp src/*.cpp -o td-engine.js \
//   -s WASM=1 -s USE_WEBGL2=1 -s FULL_ES3=1 \
//   -s EXPORTED_FUNCTIONS="['_td_init','_td_update','_td_set_key','_td_set_mouse','_td_resize','_td_shutdown','_td_load_scene']" \
//   -s EXPORTED_RUNTIME_METHODS="['ccall','cwrap','UTF8ToString','stringToUTF8','_malloc','_free']"

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/html5.h>

#include "../src/core/math/math.h"
#include "../src/core/math/vec2.h"
#include "../src/core/math/vec3.h"
#include "../src/core/math/mat4.h"
#include "../src/core/logger.h"
#include "../src/ecs/world.h"

// Since we can't use Win32 on the web, we use a simplified renderer state
static td::World g_world;
static bool g_initialized = false;
static int g_width = 800;
static int g_height = 600;

// Input state
static bool g_keys[256] = {};
static float g_mouseX = 0, g_mouseY = 0;
static bool g_mouseButtons[8] = {};
static float g_deltaTime = 0.016f;
static double g_lastTime = 0;

// Import from JavaScript
extern "C" {
    extern void td_js_log(const char* msg);
    extern int td_js_get_canvas_width();
    extern int td_js_get_canvas_height();
}

// Main loop callback for emscripten_set_main_loop
void mainLoop() {
    if (!g_initialized) return;

    // Calculate delta time
    double currentTime = emscripten_get_now() / 1000.0;
    if (g_lastTime > 0) {
        g_deltaTime = (float)(currentTime - g_lastTime);
        if (g_deltaTime > 0.25f) g_deltaTime = 0.25f;
    }
    g_lastTime = currentTime;

    // Update world systems
    g_world.updateSystems(g_deltaTime);

    // Render would happen here through WebGL calls
    // In a full implementation, the Renderer would use GLES2/GLES3
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
void td_init(int width, int height) {
    g_width = width;
    g_height = height;

    // Set canvas size
    emscripten_set_canvas_element_size("#game-canvas", width, height);

    g_lastTime = emscripten_get_now() / 1000.0;
    g_initialized = true;

    td_js_log("TD Engine initialized (WASM)");
}

EMSCRIPTEN_KEEPALIVE
void td_update() {
    mainLoop();
}

EMSCRIPTEN_KEEPALIVE
void td_set_key(int key, int pressed) {
    if (key >= 0 && key < 256) {
        g_keys[key] = (pressed != 0);
    }
}

EMSCRIPTEN_KEEPALIVE
void td_set_mouse(float x, float y, int button, int pressed) {
    g_mouseX = x;
    g_mouseY = y;
    if (button >= 0 && button < 8) {
        g_mouseButtons[button] = (pressed != 0);
    }
}

EMSCRIPTEN_KEEPALIVE
void td_resize(int width, int height) {
    g_width = width;
    g_height = height;
    emscripten_set_canvas_element_size("#game-canvas", width, height);
}

EMSCRIPTEN_KEEPALIVE
void td_load_scene(const char* sceneData) {
    // Parse scene data and create entities
    // Format: text-based scene description
    if (sceneData) {
        td_js_log("Scene loaded");
    }
}

EMSCRIPTEN_KEEPALIVE
void td_shutdown() {
    g_world.clear();
    g_initialized = false;
    td_js_log("TD Engine shutdown");
}

} // extern "C"

int main() {
    emscripten_set_main_loop(mainLoop, 0, 1);
    return 0;
}

#endif // __EMSCRIPTEN__
