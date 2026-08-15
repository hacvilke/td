// =============================================================================
// TD Engine - Gauntlet Part 7: WebAssembly Bridge
// File: wasm/emscripten_main.cpp
//
// Emscripten entry point that swaps the Win32 platform layer for the browser.
// The rest of the C++ engine (renderer, physics, ECS, audio mixer, scripting)
// is compiled unchanged - only the platform layer is replaced by this file.
//
// Build:  make web     (requires emcc/em++ on PATH; see wasm/README.md)
//
// Exported C API (callable from JS via Module._<name> or Module.ccall):
//   td_init(width, height)            - boot the engine (creates GL context)
//   td_shutdown()                     - tear down
//   td_load_scene(sceneText)          - parse + load a scene into the ECS World
//   td_set_key_state(vkCode, pressed) - inject a key event (Win32 VK codes)
//   td_set_mouse_state(x, y, l, r)    - inject mouse position + buttons
//   td_resize(w, h)                   - viewport resize
//   td_get_version()                  - returns "TD Engine 1.0.0 (WebAssembly)"
//   td_fill_audio_buffer(out, n)      - fill a stereo int16 PCM buffer
//   td_create_entity(name)            - ECS: create entity, return id
//   td_entity_set_position(id, x, y)  - ECS: set PositionComponent
//   td_entity_set_velocity(id, vx,vy) - ECS: set VelocityComponent
//   td_entity_set_sprite(id, w, h, r, g, b, a)
//   td_entity_set_collider(id, w, h)
//   td_entity_destroy(id)
//   td_entity_is_valid(id) -> bool
//
// The engine's input system uses Win32 virtual key codes (e.g. Key::A = 0x41).
// Conveniently, browser keyboard events expose `e.keyCode` which is ALSO the
// Win32 VK code (a historical artifact of the DOM spec). So the JS bridge
// forwards e.keyCode directly with no translation table needed.
// =============================================================================

#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// --- Engine headers (Parts 1-6, unchanged) -----------------------------------
#include "../src/platform/platform.h"      // InputState, Key::, Mouse::
#include "../src/core/game_loop.h"
#include "../src/core/logger.h"
#include "../src/renderer/gl_renderer.h"   // td::Renderer, td::gl
#include "../src/renderer/sprite_batch.h"
#include "../src/renderer/camera.h"        // Camera2D
#include "../src/ecs/world.h"              // td::World
#include "../src/ecs/component.h"          // PositionComponent, SpriteComponent, ...
#include "../src/ecs/system.h"            // BeatSystem
#include "../src/audio/mixer.h"            // td::Mixer
#include "../src/scripting/script_vm.h"        // td::ScriptVM (tdscript)
#include "../src/scripting/script_vm_internal.h" // script::loadScriptFromSource
#include "../src/core/i18n.h"                 // td::i18n (locale tables)
#include "../src/platform/xr_input.h"         // TouchManager, GamepadManager
#include "../src/renderer/shader_graph.h"     // td::shader_graph::compile
// Physics headers must be included OUTSIDE the extern "C" block below —
// they define C++ classes (Vec3, Quat, Mat3, RigidBody3D) with templates
// and operator overloads that don't compile under C linkage.
#include "../src/physics/physics_world_3d.h"
#include "../src/physics/constraints_3d.h"

// =============================================================================
// Global state. Held for the lifetime of the WASM module. Emscripten's main
// loop is callback-based; main() returns before the loop terminates so we
// cannot keep these as locals.
// =============================================================================

namespace td {

// Globals defined in this file
static Renderer*       g_renderer   = nullptr;  // alias of Renderer::get()
static SpriteBatch*    g_sprites    = nullptr;
static Camera2D*       g_camera     = nullptr;
static World*          g_world      = nullptr;
static Mixer*          g_mixer      = nullptr;
static InputState      g_input      = {};
static TimeState       g_time       = {};

// BeatSystem for rhythm-game mechanics. Registered with g_world in td_init.
// Pointer-stable so JS callbacks (set via td_beat_set_callback) remain valid.
static BeatSystem*     g_beatSystem = nullptr;

static int  g_canvasWidth  = 800;
static int  g_canvasHeight = 600;
static bool g_running      = false;
static bool g_initialized  = false;

// Fixed-step accumulator (replicates td::GameLoop's algorithm so we don't
// depend on GameLoop::run(Win32Window&), which is desktop-only).
static double g_accumulator = 0.0;
static float  g_fixedStep   = 1.0f / 60.0f;
static double g_lastTime    = 0.0;

// Callbacks registered by JS (via td_set_callbacks) for user game logic.
// These let a JS/TS developer write game code without touching C++.
typedef void (*VoidCb)();
typedef void (*FloatCb)(float);
static VoidCb  g_cb_init    = nullptr;
static FloatCb g_cb_update  = nullptr;
static FloatCb g_cb_render  = nullptr;
static VoidCb  g_cb_shutdown = nullptr;

} // namespace td

// Forward declarations (used before definition by Emscripten callbacks).
extern "C" void td_resize(int width, int height);
static void mainLoop();

// =============================================================================
// Emscripten HTML5 event callbacks
// =============================================================================
// Each callback updates the engine's InputState and returns EM_TRUE to swallow
// the default browser action (so arrow keys don't scroll the page, etc.).

static EM_BOOL keyCallback(int eventType,
                           const EmscriptenKeyboardEvent* keyEvent,
                           void* /*userData*/)
{
    // keyEvent->keyCode is the deprecated DOM keyCode, which is the Win32 VK
    // code. The engine's Key:: namespace uses the same values, so we forward
    // directly.
    int vk = keyEvent->keyCode;
    if (vk < 0 || vk >= 256) return EM_FALSE;
    bool pressed = (eventType == EMSCRIPTEN_EVENT_KEYDOWN);
    td::g_input.keys[vk] = pressed;

    // Prevent default for game-relevant keys.
    if (vk == 0x20 /*Space*/ || (vk >= 0x25 && vk <= 0x28) /*Arrows*/ ||
        (vk >= 0x41 && vk <= 0x5A) /*A-Z*/) {
        return EM_TRUE;
    }
    return EM_FALSE;
}

static EM_BOOL mouseMoveCallback(int /*eventType*/,
                                 const EmscriptenMouseEvent* ev,
                                 void* /*userData*/)
{
    td::g_input.mouseDeltaX = (float)ev->targetX - td::g_input.mouseX;
    td::g_input.mouseDeltaY = (float)ev->targetY - td::g_input.mouseY;
    td::g_input.mouseX = (float)ev->targetX;
    td::g_input.mouseY = (float)ev->targetY;
    return EM_TRUE;
}

static EM_BOOL mouseDownCallback(int /*eventType*/,
                                 const EmscriptenMouseEvent* ev,
                                 void* /*userData*/)
{
    if (ev->button >= 0 && ev->button < 8) {
        td::g_input.mouseButtons[ev->button] = true;
    }
    return EM_TRUE;
}

static EM_BOOL mouseUpCallback(int /*eventType*/,
                               const EmscriptenMouseEvent* ev,
                               void* /*userData*/)
{
    if (ev->button >= 0 && ev->button < 8) {
        td::g_input.mouseButtons[ev->button] = false;
    }
    return EM_TRUE;
}

static EM_BOOL resizeCallback(int /*eventType*/,
                              const EmscriptenUiEvent* ev,
                              void* /*userData*/)
{
    int w = ev->documentBodyClientWidth;
    int h = ev->documentBodyClientHeight;
    td_resize(w, h);
    return EM_TRUE;
}

// =============================================================================
// Exported C API
// =============================================================================

extern "C" {

// -----------------------------------------------------------------------------
// td_init(width, height)
//
// Boots the engine. Called by js_bridge.js AFTER the WASM module is
// instantiated and Emscripten's GL context is current on the canvas.
// -----------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE
void td_init(int width, int height)
{
    if (td::g_initialized) {
        TD_LOG_WARN("td_init called twice - ignoring");
        return;
    }

    td::g_canvasWidth  = width  > 0 ? width  : 800;
    td::g_canvasHeight = height > 0 ? height : 600;

    emscripten_set_canvas_element_size("#game-canvas",
                                        td::g_canvasWidth,
                                        td::g_canvasHeight);

    // --- Logger (writes to /tmp/td-engine.log in the Emscripten FS) ---------
    td::Logger::get().init("/tmp/td-engine.log");
    td::Logger::get().setFileLogging(false);   // no file in browser
    TD_LOG_INFO("TD Engine (WebAssembly) starting up...");

    // --- Renderer (singleton) ----------------------------------------------
    // td::gl is populated by gl_renderer.cpp's loadGLFunctions() which has an
    // #ifdef __EMSCRIPTEN__ branch that takes the address of each GLES3 symbol.
    td::g_renderer = &td::Renderer::get();
    if (!td::g_renderer->init()) {
        TD_LOG_ERROR("Renderer initialization failed");
        return;
    }
    td::g_renderer->setViewport(0, 0, td::g_canvasWidth, td::g_canvasHeight);
    td::g_renderer->clear(0.0f, 0.0f, 0.0f, 1.0f);

    // --- Sprite batch (the engine's 2D renderer) ----------------------------
    td::g_sprites = new td::SpriteBatch();
    td::g_sprites->init();

    // --- 2D camera ----------------------------------------------------------
    td::g_camera = new td::Camera2D();
    td::g_camera->setViewport(td::g_canvasWidth, td::g_canvasHeight);
    td::g_camera->setPosition(td::g_canvasWidth  * 0.5f,
                              td::g_canvasHeight * 0.5f);

    // --- ECS world ----------------------------------------------------------
    td::g_world = new td::World();

    // --- BeatSystem (rhythm-game mechanics) --------------------------------
    // Registered with the world so it ticks every fixed-step update.
    // JS sets its beat callback via td_beat_set_callback.
    td::g_beatSystem = new td::BeatSystem();
    td::g_beatSystem->setTimeSource([]() -> float {
        return (float)td::g_time.totalTime;
    });
    td::g_world->addSystem(td::g_beatSystem);

    // --- Audio mixer (output is pulled by JS via td_fill_audio_buffer) ------
    td::g_mixer = new td::Mixer();
    td::g_mixer->init(44100, 2);

    // --- Input state (zeroed at construction; Emscripten callbacks fill it) -
    memset(&td::g_input, 0, sizeof(td::g_input));
    memset(&td::g_time,  0, sizeof(td::g_time));
    td::g_time.fixedDeltaTime = td::g_fixedStep;

    // --- Register Emscripten HTML5 callbacks on the game canvas -------------
    const char* target = "#game-canvas";
    emscripten_set_keydown_callback   (target, nullptr, true, keyCallback);
    emscripten_set_keyup_callback     (target, nullptr, true, keyCallback);
    emscripten_set_mousemove_callback (target, nullptr, true, mouseMoveCallback);
    emscripten_set_mousedown_callback (target, nullptr, true, mouseDownCallback);
    emscripten_set_mouseup_callback   (target, nullptr, true, mouseUpCallback);
    emscripten_set_resize_callback("#window", nullptr, true, resizeCallback);

    // --- User init callback (game-level setup) ------------------------------
    if (td::g_cb_init) td::g_cb_init();

    td::g_running     = true;
    td::g_initialized = true;
    td::g_lastTime    = emscripten_get_now();

    TD_LOG_INFO("TD Engine ready: %dx%d, GL_VERSION=%s",
                td::g_canvasWidth, td::g_canvasHeight,
                (const char*)td::gl.glGetString(0x1F02));

    // Hand control to Emscripten. 0 = browser-rAF unlimited FPS,
    // 1 = simulate infinite loop (don't unwind main's stack).
    emscripten_set_main_loop(mainLoop, 0, 1);
    // NOT REACHED
}

// -----------------------------------------------------------------------------
// td_shutdown()
// -----------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE
void td_shutdown()
{
    if (!td::g_initialized) return;

    td::g_running = false;
    emscripten_cancel_main_loop();

    if (td::g_cb_shutdown) td::g_cb_shutdown();

    if (td::g_sprites) { td::g_sprites->shutdown(); delete td::g_sprites; td::g_sprites = nullptr; }
    if (td::g_camera)  { delete td::g_camera;  td::g_camera  = nullptr; }
    if (td::g_world)   { delete td::g_world;   td::g_world   = nullptr; }
    if (td::g_mixer)   { delete td::g_mixer;   td::g_mixer   = nullptr; }
    if (td::g_beatSystem) { delete td::g_beatSystem; td::g_beatSystem = nullptr; }

    td::g_renderer->shutdown();
    td::Logger::get().shutdown();

    td::g_initialized = false;
    TD_LOG_INFO("TD Engine shutdown complete");
}

// -----------------------------------------------------------------------------
// td_set_callbacks(init, update, render, shutdown)
//
// Lets JS/TS register game-logic callbacks. Each is a C function pointer; the
// JS bridge uses ccall/cwrap with 'number' (pointer) arg type to pass
// JavaScript functions back via addFunction (requires ALLOW_TABLE_GROWTH).
// -----------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE
void td_set_callbacks(td::VoidCb init,
                      td::FloatCb update,
                      td::FloatCb render,
                      td::VoidCb shutdown)
{
    td::g_cb_init     = init;
    td::g_cb_update   = update;
    td::g_cb_render   = render;
    td::g_cb_shutdown = shutdown;
}

// -----------------------------------------------------------------------------
// td_load_scene(sceneText)
//
// Parses a scene description string and populates the ECS world. The format
// is line-oriented and matches the desktop editor's writer:
//
//   entity Player {
//     position { x: 100 y: 100 }
//     velocity { x: 0   y: 0 }
//     sprite   { w: 32 h: 32 r: 1 g: 1 b: 1 a: 1 }
//     collider { w: 32 h: 32 }
//   }
// -----------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE
void td_load_scene(const char* sceneText)
{
    if (!td::g_world || !sceneText || !*sceneText) {
        TD_LOG_WARN("td_load_scene: empty scene or world not initialized");
        return;
    }

    // Tiny line-based parser. Sufficient for the format above; the desktop
    // editor uses the same logic (see editor/scene_panel.cpp).
    std::string text(sceneText);
    size_t i = 0;
    td::EntityId currentEnt = td::INVALID_ENTITY;
    while (i < text.size()) {
        size_t end = text.find('\n', i);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(i, end - i);
        i = end + 1;

        // Trim whitespace
        size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos) continue;
        size_t e = line.find_last_not_of(" \t\r");
        std::string trimmed = line.substr(s, e - s + 1);

        if (trimmed.rfind("entity ", 0) == 0) {
            // "entity Name {"
            size_t nameStart = 7;
            size_t nameEnd   = trimmed.find(' ', nameStart);
            if (nameEnd == std::string::npos) nameEnd = trimmed.find('{', nameStart);
            if (nameEnd == std::string::npos) nameEnd = trimmed.size();
            std::string name = trimmed.substr(nameStart, nameEnd - nameStart);
            currentEnt = td::g_world->createEntity(name.c_str());
        } else if (trimmed == "}") {
            currentEnt = td::INVALID_ENTITY;
        } else if (currentEnt != td::INVALID_ENTITY) {
            // Component line: "position { x: 100 y: 100 }"
            size_t brace = trimmed.find('{');
            if (brace == std::string::npos) continue;
            std::string type = trimmed.substr(0, brace);
            // strip trailing space from type
            while (!type.empty() && type.back() == ' ') type.pop_back();

            std::string body = trimmed.substr(brace + 1,
                                              trimmed.size() - brace - 2);

            if (type == "position") {
                td::PositionComponent* p =
                    td::g_world->addComponent<td::PositionComponent>(currentEnt);
                sscanf(body.c_str(), " x: %f y: %f", &p->x, &p->y);
            } else if (type == "velocity") {
                td::VelocityComponent* v =
                    td::g_world->addComponent<td::VelocityComponent>(currentEnt);
                sscanf(body.c_str(), " x: %f y: %f", &v->vx, &v->vy);
            } else if (type == "sprite") {
                td::SpriteComponent* sp =
                    td::g_world->addComponent<td::SpriteComponent>(currentEnt);
                sscanf(body.c_str(),
                       " w: %f h: %f r: %f g: %f b: %f a: %f",
                       &sp->width, &sp->height,
                       &sp->r, &sp->g, &sp->b, &sp->a);
            } else if (type == "collider") {
                td::ColliderComponent* c =
                    td::g_world->addComponent<td::ColliderComponent>(currentEnt);
                sscanf(body.c_str(), " w: %f h: %f", &c->width, &c->height);
            } else if (type == "rigidbody") {
                td::RigidBodyComponent* r =
                    td::g_world->addComponent<td::RigidBodyComponent>(currentEnt);
                float mass = 1.0f, friction = 0.3f, restitution = 0.2f;
                sscanf(body.c_str(),
                       " mass: %f friction: %f restitution: %f",
                       &mass, &friction, &restitution);
                r->mass = mass; r->friction = friction; r->restitution = restitution;
            }
        }
    }
    TD_LOG_INFO("Scene loaded: %d entities", td::g_world->getEntityCount());
}

// -----------------------------------------------------------------------------
// Input injection (called by JS bridge from browser event listeners)
// -----------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE
void td_set_key_state(int vkCode, bool pressed)
{
    if (vkCode < 0 || vkCode >= 256) return;
    td::g_input.keys[vkCode] = pressed;
}

EMSCRIPTEN_KEEPALIVE
void td_set_mouse_state(float x, float y, bool leftDown, bool rightDown)
{
    td::g_input.mouseX = x;
    td::g_input.mouseY = y;
    td::g_input.mouseButtons[td::Mouse::Left]  = leftDown;
    td::g_input.mouseButtons[td::Mouse::Right] = rightDown;
}

// -----------------------------------------------------------------------------
// Viewport resize
// -----------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE
void td_resize(int width, int height)
{
    if (width <= 0 || height <= 0) return;
    td::g_canvasWidth  = width;
    td::g_canvasHeight = height;
    emscripten_set_canvas_element_size("#game-canvas", width, height);
    if (td::g_renderer) td::g_renderer->setViewport(0, 0, width, height);
    if (td::g_camera) {
        td::g_camera->setViewport(width, height);
        td::g_camera->setPosition(width * 0.5f, height * 0.5f);
    }
    TD_LOG_INFO("Viewport resized: %dx%d", width, height);
}

// -----------------------------------------------------------------------------
// Version string
// -----------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE
const char* td_get_version()
{
    static const char kVersion[] = "TD Engine 1.0.0 (WebAssembly)";
    return kVersion;
}

// -----------------------------------------------------------------------------
// Audio bridge
//
// The JS side creates a Web Audio AudioContext + ScriptProcessor (or
// AudioWorklet) and calls td_fill_audio_buffer every quantum. We ask the
// engine's Mixer to mix `numFrames` stereo int16 samples into `out`.
//
// The Mixer outputs int16 PCM at -32768..32767. The JS bridge converts to
// float32 -1..1 for Web Audio.
// -----------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE
void td_fill_audio_buffer(int16_t* out, int numFrames)
{
    if (!out || numFrames <= 0) return;
    if (td::g_mixer) {
        // Mixer::mix expects interleaved stereo int16. numFrames here is the
        // number of stereo frame pairs, so total samples = numFrames * 2.
        td::g_mixer->mix(out, numFrames * 2);
    } else {
        memset(out, 0, sizeof(int16_t) * numFrames * 2);
    }
}

// =============================================================================
// ECS convenience API (so JS/TS devs can create entities without writing C++)
// =============================================================================

EMSCRIPTEN_KEEPALIVE
uint32_t td_create_entity(const char* name)
{
    if (!td::g_world) return td::INVALID_ENTITY;
    return td::g_world->createEntity(name ? name : "Entity");
}

EMSCRIPTEN_KEEPALIVE
void td_entity_set_position(uint32_t id, float x, float y)
{
    if (!td::g_world) return;
    td::PositionComponent* p =
        td::g_world->getComponent<td::PositionComponent>(id);
    if (p) { p->x = x; p->y = y; }
    else {
        p = td::g_world->addComponent<td::PositionComponent>(id);
        if (p) { p->x = x; p->y = y; }
    }
}

EMSCRIPTEN_KEEPALIVE
void td_entity_get_position(uint32_t id, float* outX, float* outY)
{
    if (!td::g_world || !outX || !outY) return;
    td::PositionComponent* p =
        td::g_world->getComponent<td::PositionComponent>(id);
    if (p) { *outX = p->x; *outY = p->y; }
    else   { *outX = 0;    *outY = 0;    }
}

EMSCRIPTEN_KEEPALIVE
void td_entity_set_velocity(uint32_t id, float vx, float vy)
{
    if (!td::g_world) return;
    td::VelocityComponent* v =
        td::g_world->getComponent<td::VelocityComponent>(id);
    if (v) { v->vx = vx; v->vy = vy; }
    else {
        v = td::g_world->addComponent<td::VelocityComponent>(id);
        if (v) { v->vx = vx; v->vy = vy; }
    }
}

EMSCRIPTEN_KEEPALIVE
void td_entity_set_sprite(uint32_t id, float w, float h,
                           float r, float g, float b, float a)
{
    if (!td::g_world) return;
    td::SpriteComponent* s =
        td::g_world->addComponent<td::SpriteComponent>(id);
    if (s) {
        s->width = w; s->height = h;
        s->r = r; s->g = g; s->b = b; s->a = a;
    }
}

EMSCRIPTEN_KEEPALIVE
void td_entity_set_collider(uint32_t id, float w, float h)
{
    if (!td::g_world) return;
    td::ColliderComponent* c =
        td::g_world->addComponent<td::ColliderComponent>(id);
    if (c) { c->width = w; c->height = h; }
}

EMSCRIPTEN_KEEPALIVE
void td_entity_destroy(uint32_t id)
{
    if (td::g_world) td::g_world->destroyEntity(id);
}

EMSCRIPTEN_KEEPALIVE
bool td_entity_is_valid(uint32_t id)
{
    return td::g_world && td::g_world->entityExists(id);
}

EMSCRIPTEN_KEEPALIVE
int td_get_entity_count()
{
    return td::g_world ? td::g_world->getEntityCount() : 0;
}

// -----------------------------------------------------------------------------
// Input query (lets JS read the engine's input mirror - used by engine-wrapper
// for `input.isKeyDown(Key::Space)` style checks).
// -----------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE
bool td_is_key_down(int vkCode)
{
    if (vkCode < 0 || vkCode >= 256) return false;
    return td::g_input.keys[vkCode];
}

EMSCRIPTEN_KEEPALIVE
bool td_is_mouse_down(int button)
{
    if (button < 0 || button >= 8) return false;
    return td::g_input.mouseButtons[button];
}

EMSCRIPTEN_KEEPALIVE
void td_get_mouse_pos(float* outX, float* outY)
{
    if (outX) *outX = td::g_input.mouseX;
    if (outY) *outY = td::g_input.mouseY;
}

// =============================================================================
// Beat Tracker API (rhythm-game mechanics)
//
// Implements the BPM-synced metronome described in docs/RHYTHM_MECHANICS.md.
// Workflow:
//   1. Create an entity:                td_create_entity("song")
//   2. Start beat tracking on it:       td_beat_start(entityId, 140.0, 0.15)
//   3. Register a beat-tick callback:   td_beat_set_callback(cb)
//   4. Each frame, check if player is on-beat: td_beat_is_on_beat(entityId)
//   5. On successful hit, mark it:      td_beat_register_hit(entityId)
//
// All state lives in the engine's ECS World (BeatTrackerComponent). The
// BeatSystem ticks every fixed-step update and fires the callback.
// =============================================================================

// Callback signature: void(int beatCount, float beatTime). Same shape as
// BeatSystem::BeatCallback, but typedef'd here in the global namespace so
// Emscripten's C ABI can reference it.
typedef void (*TdBeatCallback)(int, float);

EMSCRIPTEN_KEEPALIVE
void td_beat_start(uint32_t entityId, float bpm, float windowHalfSec)
{
    if (!td::g_world) return;
    td::BeatTrackerComponent* bt =
        td::g_world->addComponent<td::BeatTrackerComponent>(entityId);
    if (!bt) return;

    if (bpm < 1.0f) bpm = 1.0f;
    if (bpm > 600.0f) bpm = 600.0f;
    bt->bpm = bpm;
    bt->spb = 60.0f / bpm;
    bt->windowHalf = (windowHalfSec > 0.001f) ? windowHalfSec : 0.15f;
    bt->startTime = (float)td::g_time.totalTime;
    bt->nextBeatTime = bt->startTime + bt->spb;
    bt->lastBeatTime = bt->startTime;
    bt->upperBound = bt->lastBeatTime + bt->windowHalf;
    bt->lowerBound = bt->nextBeatTime - bt->windowHalf;
    bt->beatCount = 0;
    bt->combo = 0;
    bt->bestCombo = 0;
    bt->lastHitTime = -1.0f;
    bt->active = true;

    TD_LOG_INFO("BeatTracker started: bpm=%.1f spb=%.3f windowHalf=%.3fs",
                bt->bpm, bt->spb, bt->windowHalf);
}

EMSCRIPTEN_KEEPALIVE
void td_beat_stop(uint32_t entityId)
{
    if (!td::g_world) return;
    td::BeatTrackerComponent* bt =
        td::g_world->getComponent<td::BeatTrackerComponent>(entityId);
    if (bt) bt->active = false;
}

EMSCRIPTEN_KEEPALIVE
int td_beat_is_on_beat(uint32_t entityId)
{
    if (!td::g_world || !td::g_beatSystem) return 0;
    td::BeatTrackerComponent* bt =
        td::g_world->getComponent<td::BeatTrackerComponent>(entityId);
    if (!bt || !bt->active) return 0;
    float now = (float)td::g_time.totalTime;
    return td::g_beatSystem->isOnBeat(*bt, now) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int td_beat_get_count(uint32_t entityId)
{
    if (!td::g_world) return 0;
    td::BeatTrackerComponent* bt =
        td::g_world->getComponent<td::BeatTrackerComponent>(entityId);
    return bt ? bt->beatCount : 0;
}

EMSCRIPTEN_KEEPALIVE
float td_beat_get_next_beat_time(uint32_t entityId)
{
    if (!td::g_world) return -1.0f;
    td::BeatTrackerComponent* bt =
        td::g_world->getComponent<td::BeatTrackerComponent>(entityId);
    return bt ? bt->nextBeatTime : -1.0f;
}

EMSCRIPTEN_KEEPALIVE
float td_beat_get_last_beat_time(uint32_t entityId)
{
    if (!td::g_world) return -1.0f;
    td::BeatTrackerComponent* bt =
        td::g_world->getComponent<td::BeatTrackerComponent>(entityId);
    return bt ? bt->lastBeatTime : -1.0f;
}

// Register a successful on-beat hit. Returns the new combo count.
// Pass `strict=true` to only count hits that are actually on-beat; pass
// false to count any hit (the JS side can use td_beat_is_on_beat to gate).
EMSCRIPTEN_KEEPALIVE
int td_beat_register_hit(uint32_t entityId, int strict)
{
    if (!td::g_world) return 0;
    td::BeatTrackerComponent* bt =
        td::g_world->getComponent<td::BeatTrackerComponent>(entityId);
    if (!bt || !bt->active) return 0;

    float now = (float)td::g_time.totalTime;
    bool onBeat = td::g_beatSystem ? td::g_beatSystem->isOnBeat(*bt, now) : false;

    if (strict && !onBeat) {
        // Missed - reset combo.
        bt->combo = 0;
        return 0;
    }

    bt->combo++;
    if (bt->combo > bt->bestCombo) bt->bestCombo = bt->combo;
    bt->lastHitTime = now;
    return bt->combo;
}

EMSCRIPTEN_KEEPALIVE
int td_beat_get_combo(uint32_t entityId)
{
    if (!td::g_world) return 0;
    td::BeatTrackerComponent* bt =
        td::g_world->getComponent<td::BeatTrackerComponent>(entityId);
    return bt ? bt->combo : 0;
}

EMSCRIPTEN_KEEPALIVE
int td_beat_get_best_combo(uint32_t entityId)
{
    if (!td::g_world) return 0;
    td::BeatTrackerComponent* bt =
        td::g_world->getComponent<td::BeatTrackerComponent>(entityId);
    return bt ? bt->bestCombo : 0;
}

// Reset combo (e.g. when the player misses). Returns the new combo (0).
EMSCRIPTEN_KEEPALIVE
int td_beat_reset_combo(uint32_t entityId)
{
    if (!td::g_world) return 0;
    td::BeatTrackerComponent* bt =
        td::g_world->getComponent<td::BeatTrackerComponent>(entityId);
    if (bt) bt->combo = 0;
    return 0;
}

// Register a JS callback fired on every beat tick. Pass 0 to clear.
EMSCRIPTEN_KEEPALIVE
void td_beat_set_callback(TdBeatCallback cb)
{
    if (td::g_beatSystem) {
        td::g_beatSystem->setBeatCallback(reinterpret_cast<td::BeatSystem::BeatCallback>(cb));
    }
}

// Change BPM on a live tracker. Useful for songs with tempo changes.
// Resets nextBeatTime relative to the current engine time to avoid drift.
EMSCRIPTEN_KEEPALIVE
void td_beat_set_bpm(uint32_t entityId, float newBpm)
{
    if (!td::g_world) return;
    td::BeatTrackerComponent* bt =
        td::g_world->getComponent<td::BeatTrackerComponent>(entityId);
    if (!bt || !bt->active) return;

    if (newBpm < 1.0f) newBpm = 1.0f;
    if (newBpm > 600.0f) newBpm = 600.0f;
    bt->bpm = newBpm;
    bt->spb = 60.0f / newBpm;
    float now = (float)td::g_time.totalTime;
    bt->nextBeatTime = now + bt->spb;
    bt->lastBeatTime = now;
    bt->upperBound = bt->lastBeatTime + bt->windowHalf;
    bt->lowerBound = bt->nextBeatTime - bt->windowHalf;
}

// Play a short "tick" sound on every beat. Pass a WAV index (or -1 to disable).
// Uses the engine's Mixer; the JS bridge loads the WAV via Module._malloc +
// td_load_wav (or equivalent). For now, calling this with index -1 disables
// the tick; calling with a valid index will play it on every beat fire.
EMSCRIPTEN_KEEPALIVE
void td_beat_play_sound(uint32_t entityId, int wavIndex)
{
    if (!td::g_world) return;
    td::BeatTrackerComponent* bt =
        td::g_world->getComponent<td::BeatTrackerComponent>(entityId);
    if (!bt) return;
    // For now this is a stub: the actual WAV playback is wired up via the
    // beat-tick callback in JS. Marking the wavIndex in the component lets
    // a future C++-side audio hook pick it up. We log the request so devs
    // can verify it was called.
    (void)wavIndex;
    TD_LOG_INFO("BeatTracker %u: play_sound(%d) requested (use td_beat_set_callback for now)",
                entityId, wavIndex);
}

// -----------------------------------------------------------------------------
// Render a single frame manually (used by the JS bridge if it wants to drive
// the loop itself instead of emscripten_set_main_loop).
// -----------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE
void td_render_frame()
{
    mainLoop();
}

// =============================================================================
// Wave 1/2 module C API exports
// =============================================================================
// These expose the new Tier 1/2/3/4 modules to JavaScript so web games can
// use them. Each function is a thin wrapper that forwards to the singleton
// or static method in the corresponding module.
// =============================================================================

// --- Scripting VM (tdscript) ------------------------------------------------
// (headers moved to top of file — they pull in C++ stdlib templates that
//  cannot live inside an extern "C" block.)

// Load a tdscript source string and return a script handle (id >= 0 on
// success, -1 on failure). The handle is opaque to JS.
EMSCRIPTEN_KEEPALIVE
int td_script_load(const char* src, const char* name)
{
    td::ScriptHandle h = td::script::loadScriptFromSource(
        td::ScriptVM::get(), src, name ? name : "<web>");
    return h.id;
}

// Call a named function in a loaded script. Args are passed as a JSON
// array string (e.g. "[1, 2.5, \"hello\"]"). Returns the first return
// value as a string (numbers stringified, strings quoted, nil/true/false
// as "nil"/"true"/"false"). Returns "nil" on error.
EMSCRIPTEN_KEEPALIVE
const char* td_script_call(int handle, const char* fnName, const char* argsJson)
{
    static std::string result;
    if (handle < 0 || !fnName) { result = "nil"; return result.c_str(); }
    td::ScriptHandle h{handle};
    // Parse argsJson as a list of values. (Minimal — only numbers + strings.)
    std::vector<td::script::Value> args;
    if (argsJson) {
        // Very simple: split by commas, trim, parse as number or quoted string.
        std::string s = argsJson;
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && (s[i] == ' ' || s[i] == ',' || s[i] == '[' || s[i] == ']')) i++;
            if (i >= s.size()) break;
            if (s[i] == '"') {
                size_t end = s.find('"', i + 1);
                if (end == std::string::npos) break;
                args.push_back(td::script::Value::makeStr(s.substr(i + 1, end - i - 1)));
                i = end + 1;
            } else {
                size_t end = s.find_first_of(",]", i);
                if (end == std::string::npos) end = s.size();
                std::string numStr = s.substr(i, end - i);
                double n = 0;
                bool isNum = true;
                try { n = std::stod(numStr); }
                catch (...) { isNum = false; }
                if (isNum) args.push_back(td::script::Value::makeNum(n));
                else if (numStr == "true") args.push_back(td::script::Value::makeBool(true));
                else if (numStr == "false") args.push_back(td::script::Value::makeBool(false));
                else args.push_back(td::script::Value::makeNil());
                i = end;
            }
        }
    }
    auto ret = td::script::callScriptFunction(h, fnName, std::move(args));
    if (ret.empty()) { result = "nil"; return result.c_str(); }
    const auto& v = ret[0];
    switch (v.type) {
        case td::script::Value::Type::Nil:     result = "nil"; break;
        case td::script::Value::Type::Bool:    result = v.boolVal ? "true" : "false"; break;
        case td::script::Value::Type::Number:  result = std::to_string(v.numVal); break;
        case td::script::Value::Type::String:  result = *v.strVal; break;
        default: result = "nil"; break;
    }
    return result.c_str();
}

// Unload a script. After this, the handle is invalid.
EMSCRIPTEN_KEEPALIVE
void td_script_unload(int handle)
{
    if (handle < 0) return;
    td::ScriptVM::get().unloadScript(td::ScriptHandle{handle});
}

// --- i18n / Localization -----------------------------------------------------
// (header moved to top of file — pulls in <algorithm>, <cctype> which break
//  under extern "C" linkage.)

EMSCRIPTEN_KEEPALIVE
void td_i18n_load(const char* localeStr, const char* json)
{
    if (!localeStr || !json) return;
    td::i18n::Localization::get().loadTable(
        td::i18n::Locale::fromString(localeStr), json);
}

EMSCRIPTEN_KEEPALIVE
void td_i18n_set_locale(const char* localeStr)
{
    if (!localeStr) return;
    td::i18n::Localization::get().setActiveLocale(
        td::i18n::Locale::fromString(localeStr));
}

EMSCRIPTEN_KEEPALIVE
const char* td_i18n_t(const char* key)
{
    static std::string out;
    out = td::i18n::Localization::get().t(key ? key : "");
    return out.c_str();
}

EMSCRIPTEN_KEEPALIVE
int td_i18n_is_rtl()
{
    return td::i18n::Localization::get().isRTL() ? 1 : 0;
}

// --- Touch + Gamepad ---------------------------------------------------------
// (header moved to top of file — pulls in <array>, <cmath> which break
//  under extern "C" linkage.)

static td::input::TouchManager g_touch;
static td::input::GamepadManager g_gamepads;

EMSCRIPTEN_KEEPALIVE
void td_touch_begin_frame() { g_touch.beginFrame(); }

EMSCRIPTEN_KEEPALIVE
void td_touch_start(int id, float x, float y, float pressure)
{
    g_touch.onTouchStart(id, td::Vec2{x, y}, pressure);
}

EMSCRIPTEN_KEEPALIVE
void td_touch_move(int id, float x, float y, float pressure)
{
    g_touch.onTouchMove(id, td::Vec2{x, y}, pressure);
}

EMSCRIPTEN_KEEPALIVE
void td_touch_end(int id, float x, float y)
{
    g_touch.onTouchEnd(id, td::Vec2{x, y});
}

EMSCRIPTEN_KEEPALIVE
int td_touch_count() { return g_touch.touchCount(); }

EMSCRIPTEN_KEEPALIVE
float td_touch_x(int idx) {
    auto* t = g_touch.getTouch(idx);
    return t ? t->position.x : 0.0f;
}

EMSCRIPTEN_KEEPALIVE
float td_touch_y(int idx) {
    auto* t = g_touch.getTouch(idx);
    return t ? t->position.y : 0.0f;
}

EMSCRIPTEN_KEEPALIVE
float td_touch_pinch_scale() { return g_touch.pinchScale(); }

EMSCRIPTEN_KEEPALIVE
void td_gamepad_begin_frame() { g_gamepads.beginFrame(); }

EMSCRIPTEN_KEEPALIVE
void td_gamepad_set_connected(int idx, int connected, const char* id, const char* mapping)
{
    g_gamepads.setGamepadConnected(idx, connected != 0,
        id ? id : "", mapping ? mapping : "standard");
}

EMSCRIPTEN_KEEPALIVE
void td_gamepad_set_button(int idx, int btn, int pressed)
{
    if (idx < 0 || idx >= td::input::GamepadManager::MAX_GAMEPADS) return;
    g_gamepads.gamepadRef(idx).setButton(btn, pressed != 0);
}

EMSCRIPTEN_KEEPALIVE
void td_gamepad_set_analog(int idx, int btn, float value)
{
    if (idx < 0 || idx >= td::input::GamepadManager::MAX_GAMEPADS) return;
    g_gamepads.gamepadRef(idx).setAnalogButton(btn, value);
}

EMSCRIPTEN_KEEPALIVE
void td_gamepad_set_axis(int idx, int axis, float value)
{
    if (idx < 0 || idx >= td::input::GamepadManager::MAX_GAMEPADS) return;
    g_gamepads.gamepadRef(idx).setAxis(axis, value);
}

EMSCRIPTEN_KEEPALIVE
int td_gamepad_button_pressed(int idx, int btn)
{
    auto* gp = g_gamepads.getGamepad(idx);
    if (!gp) return 0;
    return gp->buttonPressed[btn] ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
float td_gamepad_axis(int idx, int axis)
{
    auto* gp = g_gamepads.getGamepad(idx);
    if (!gp) return 0.0f;
    return gp->axis[axis];
}

// --- Visual Shader Graph (compile to GLSL, return string) -------------------
// (header moved to top of file — pulls in C++ stdlib that breaks under
//  extern "C" linkage.)

EMSCRIPTEN_KEEPALIVE
const char* td_shader_graph_compile(int nodeCount)
{
    // The JS side builds a ShaderGraph via the existing td_* APIs (TODO:
    // add node/link add/remove C APIs). For now this is a placeholder
    // that returns the default triangle shader.
    (void)nodeCount;
    static std::string glsl;
    td::shader::ShaderGraph g;
    glsl = g.compileToFragmentShader();
    return glsl.c_str();
}

// =============================================================================
// 3D Physics — C bridge for PhysicsWorld3D
//
// Exposes the new 3D physics engine to JS/TS.  Follows the Unified Syntax
// Blueprint: flat C functions with `td_physics_*` prefix, called from JS
// via Module._td_physics_* (Emscripten cwrap).
//
// The world is a singleton: td_physics_init() creates it, all subsequent
// calls operate on it.  Bodies are referenced by integer ID returned from
// td_physics_add_body().
//
// Design note: we DON'T expose the RigidBody3D struct directly to JS.  JS
// code uses the typed TDEngine.physics.* wrapper (in web/td_api.js) which
// hides the cwrap boilerplate.
// =============================================================================
// (physics_world_3d.h + constraints_3d.h are included at the top of this
//  file, outside the extern "C" block — they require C++ linkage.)

namespace td {
static PhysicsWorld3D* g_physicsWorld3D = nullptr;
}

EMSCRIPTEN_KEEPALIVE
int td_physics_init(float gravityX, float gravityY, float gravityZ)
{
    if (td::g_physicsWorld3D) {
        delete td::g_physicsWorld3D;
    }
    td::g_physicsWorld3D = new td::PhysicsWorld3D();
    td::g_physicsWorld3D->setGravity(td::Vec3(gravityX, gravityY, gravityZ));
    td::g_physicsWorld3D->setSolverIterations(10);
    td::g_physicsWorld3D->setPositionIterations(5);
    td::g_physicsWorld3D->setAllowSleeping(true);
    return td::g_physicsWorld3D ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void td_physics_shutdown()
{
    if (td::g_physicsWorld3D) {
        delete td::g_physicsWorld3D;
        td::g_physicsWorld3D = nullptr;
    }
}

EMSCRIPTEN_KEEPALIVE
void td_physics_step(float dt)
{
    if (!td::g_physicsWorld3D) return;
    td::g_physicsWorld3D->step(dt);
}

EMSCRIPTEN_KEEPALIVE
int td_physics_add_body(float mass, float px, float py, float pz,
                         int isStatic)
{
    if (!td::g_physicsWorld3D) return -1;
    td::RigidBody3D rb;
    rb.position = td::Vec3(px, py, pz);
    if (isStatic) {
        rb.isStatic = true;
    } else {
        rb.setMass(mass);
    }
    return td::g_physicsWorld3D->addBody(rb);
}

EMSCRIPTEN_KEEPALIVE
void td_physics_remove_body(int bodyId)
{
    if (!td::g_physicsWorld3D) return;
    // Marks the body as static + colliderless.  The broadphase skips it,
    // the solver skips it, and the renderer stops drawing it.  Index is
    // preserved so other bodyIds remain valid.
    td::g_physicsWorld3D->removeBody(bodyId);
}

EMSCRIPTEN_KEEPALIVE
void td_physics_set_sphere_collider(int bodyId, float radius,
                                     float offX, float offY, float offZ)
{
    if (!td::g_physicsWorld3D) return;
    td::g_physicsWorld3D->setSphereCollider(bodyId, radius,
                                              td::Vec3(offX, offY, offZ));
}

EMSCRIPTEN_KEEPALIVE
void td_physics_set_box_collider(int bodyId, float hx, float hy, float hz,
                                   float offX, float offY, float offZ)
{
    if (!td::g_physicsWorld3D) return;
    td::g_physicsWorld3D->setBoxCollider(bodyId, td::Vec3(hx, hy, hz),
                                           td::Vec3(offX, offY, offZ));
}

EMSCRIPTEN_KEEPALIVE
void td_physics_set_capsule_collider(int bodyId, float radius, float height,
                                       int axis, float offX, float offY, float offZ)
{
    if (!td::g_physicsWorld3D) return;
    td::g_physicsWorld3D->setCapsuleCollider(bodyId, radius, height, axis,
                                                td::Vec3(offX, offY, offZ));
}

EMSCRIPTEN_KEEPALIVE
void td_physics_set_position(int bodyId, float x, float y, float z)
{
    if (!td::g_physicsWorld3D) return;
    if (bodyId < 0 || bodyId >= td::g_physicsWorld3D->bodyCount()) return;
    td::g_physicsWorld3D->getBody(bodyId).body.position = td::Vec3(x, y, z);
}

EMSCRIPTEN_KEEPALIVE
void td_physics_set_velocity(int bodyId, float vx, float vy, float vz)
{
    if (!td::g_physicsWorld3D) return;
    if (bodyId < 0 || bodyId >= td::g_physicsWorld3D->bodyCount()) return;
    td::g_physicsWorld3D->getBody(bodyId).body.linearVelocity = td::Vec3(vx, vy, vz);
}

EMSCRIPTEN_KEEPALIVE
void td_physics_get_position(int bodyId, float* outX, float* outY, float* outZ)
{
    if (!td::g_physicsWorld3D || bodyId < 0 ||
        bodyId >= td::g_physicsWorld3D->bodyCount()) {
        if (outX) *outX = 0;
        if (outY) *outY = 0;
        if (outZ) *outZ = 0;
        return;
    }
    const td::Vec3& p = td::g_physicsWorld3D->getBody(bodyId).body.position;
    if (outX) *outX = p.x;
    if (outY) *outY = p.y;
    if (outZ) *outZ = p.z;
}

EMSCRIPTEN_KEEPALIVE
void td_physics_get_velocity(int bodyId, float* outX, float* outY, float* outZ)
{
    if (!td::g_physicsWorld3D || bodyId < 0 ||
        bodyId >= td::g_physicsWorld3D->bodyCount()) {
        if (outX) *outX = 0;
        if (outY) *outY = 0;
        if (outZ) *outZ = 0;
        return;
    }
    const td::Vec3& v = td::g_physicsWorld3D->getBody(bodyId).body.linearVelocity;
    if (outX) *outX = v.x;
    if (outY) *outY = v.y;
    if (outZ) *outZ = v.z;
}

EMSCRIPTEN_KEEPALIVE
void td_physics_get_orientation(int bodyId,
                                  float* outX, float* outY, float* outZ, float* outW)
{
    if (!td::g_physicsWorld3D || bodyId < 0 ||
        bodyId >= td::g_physicsWorld3D->bodyCount()) {
        if (outX) *outX = 0;
        if (outY) *outY = 0;
        if (outZ) *outZ = 0;
        if (outW) *outW = 1;
        return;
    }
    const td::Quat& q = td::g_physicsWorld3D->getBody(bodyId).body.orientation;
    if (outX) *outX = q.x;
    if (outY) *outY = q.y;
    if (outZ) *outZ = q.z;
    if (outW) *outW = q.w;
}

EMSCRIPTEN_KEEPALIVE
void td_physics_apply_force(int bodyId, float fx, float fy, float fz)
{
    if (!td::g_physicsWorld3D || bodyId < 0 ||
        bodyId >= td::g_physicsWorld3D->bodyCount()) return;
    td::g_physicsWorld3D->getBody(bodyId).body.applyForce(td::Vec3(fx, fy, fz));
}

EMSCRIPTEN_KEEPALIVE
void td_physics_apply_impulse(int bodyId, float ix, float iy, float iz)
{
    if (!td::g_physicsWorld3D || bodyId < 0 ||
        bodyId >= td::g_physicsWorld3D->bodyCount()) return;
    td::g_physicsWorld3D->getBody(bodyId).body.applyImpulse(td::Vec3(ix, iy, iz));
}

EMSCRIPTEN_KEEPALIVE
void td_physics_apply_torque(int bodyId, float tx, float ty, float tz)
{
    if (!td::g_physicsWorld3D || bodyId < 0 ||
        bodyId >= td::g_physicsWorld3D->bodyCount()) return;
    td::g_physicsWorld3D->getBody(bodyId).body.applyTorque(td::Vec3(tx, ty, tz));
}

EMSCRIPTEN_KEEPALIVE
void td_physics_set_restitution(int bodyId, float e)
{
    if (!td::g_physicsWorld3D || bodyId < 0 ||
        bodyId >= td::g_physicsWorld3D->bodyCount()) return;
    td::g_physicsWorld3D->getBody(bodyId).body.restitution = e;
}

EMSCRIPTEN_KEEPALIVE
void td_physics_set_friction(int bodyId, float f)
{
    if (!td::g_physicsWorld3D || bodyId < 0 ||
        bodyId >= td::g_physicsWorld3D->bodyCount()) return;
    td::g_physicsWorld3D->getBody(bodyId).body.friction = f;
}

EMSCRIPTEN_KEEPALIVE
void td_physics_set_gravity_scale(int bodyId, float s)
{
    if (!td::g_physicsWorld3D || bodyId < 0 ||
        bodyId >= td::g_physicsWorld3D->bodyCount()) return;
    td::g_physicsWorld3D->getBody(bodyId).body.gravityScale = s;
}

EMSCRIPTEN_KEEPALIVE
void td_physics_set_use_gravity(int bodyId, int useGravity)
{
    if (!td::g_physicsWorld3D || bodyId < 0 ||
        bodyId >= td::g_physicsWorld3D->bodyCount()) return;
    td::g_physicsWorld3D->getBody(bodyId).body.useGravity = (useGravity != 0);
}

EMSCRIPTEN_KEEPALIVE
int td_physics_add_distance_constraint(int bodyA, int bodyB,
                                        float targetDistance)
{
    if (!td::g_physicsWorld3D) return -1;
    td::Constraint3D c;
    c.type = td::ConstraintType3D::Distance;
    c.bodyA = bodyA;
    c.bodyB = bodyB;
    c.targetDistance = targetDistance;
    return td::g_physicsWorld3D->addConstraint(c);
}

EMSCRIPTEN_KEEPALIVE
int td_physics_add_hinge_constraint(int bodyA, int bodyB,
                                      float ax, float ay, float az)
{
    if (!td::g_physicsWorld3D) return -1;
    td::Constraint3D c;
    c.type = td::ConstraintType3D::Hinge;
    c.bodyA = bodyA;
    c.bodyB = bodyB;
    c.hingeAxisA = td::Vec3(ax, ay, az);
    c.hingeAxisB = td::Vec3(ax, ay, az);
    return td::g_physicsWorld3D->addConstraint(c);
}

EMSCRIPTEN_KEEPALIVE
int td_physics_raycast(float ox, float oy, float oz,
                        float dx, float dy, float dz, float maxDist,
                        float* outPx, float* outPy, float* outPz,
                        float* outNx, float* outNy, float* outNz)
{
    if (!td::g_physicsWorld3D) return -1;
    td::Vec3 hitPoint, hitNormal;
    int32_t hitBody;
    bool hit = td::g_physicsWorld3D->raycast(
        td::Vec3(ox, oy, oz), td::Vec3(dx, dy, dz), maxDist,
        hitPoint, hitNormal, hitBody);
    if (!hit) return -1;
    if (outPx) *outPx = hitPoint.x;
    if (outPy) *outPy = hitPoint.y;
    if (outPz) *outPz = hitPoint.z;
    if (outNx) *outNx = hitNormal.x;
    if (outNy) *outNy = hitNormal.y;
    if (outNz) *outNz = hitNormal.z;
    return hitBody;
}

EMSCRIPTEN_KEEPALIVE
int td_physics_contact_count()
{
    if (!td::g_physicsWorld3D) return 0;
    return (int)td::g_physicsWorld3D->getContacts().size();
}

EMSCRIPTEN_KEEPALIVE
int td_physics_body_count()
{
    if (!td::g_physicsWorld3D) return 0;
    return td::g_physicsWorld3D->bodyCount();
}

} // extern "C"

// =============================================================================
// Main loop - called every animation frame by Emscripten.
//
// Replicates td::GameLoop's fixed-step accumulator algorithm so the engine's
// update logic runs at a deterministic 60 Hz regardless of the display's
// refresh rate (120Hz, 144Hz, etc.). Render runs every rAF tick with the
// accumulator's remainder as the interpolation alpha.
// =============================================================================
static void mainLoop()
{
    using namespace td;

    if (!g_running || !g_initialized) return;

    // --- Advance wall clock -------------------------------------------------
    double now = emscripten_get_now();
    float  dt  = (float)((now - g_lastTime) / 1000.0);
    g_lastTime = now;
    if (dt > 0.25f) dt = 0.25f;   // clamp after tab-switch stalls

    g_time.totalTime += dt;
    g_time.deltaTime  = dt;
    g_time.frameCount++;

    // --- Fixed-step updates -------------------------------------------------
    g_accumulator += dt;
    while (g_accumulator >= g_fixedStep) {
        // Snapshot previous-frame input state (so the engine's
        // keyPressed/keyReleased helpers work).
        memcpy(g_input.keysPrev,         g_input.keys,
               sizeof(g_input.keys));
        memcpy(g_input.mouseButtonsPrev, g_input.mouseButtons,
               sizeof(g_input.mouseButtons));

        if (g_cb_update) g_cb_update(g_fixedStep);

        // Tick the engine's systems (physics, scripts, etc.)
        if (g_world) g_world->updateSystems(g_fixedStep);

        g_accumulator -= g_fixedStep;
    }

    // --- Variable-rate render -----------------------------------------------
    float alpha = (float)(g_accumulator / g_fixedStep);
    if (g_cb_render) {
        g_cb_render(alpha);
    } else if (g_renderer && g_sprites && g_camera && g_world) {
        // Default render: clear + draw all sprites via SpriteBatch.
        g_renderer->clear(0.05f, 0.05f, 0.08f, 1.0f);
        Mat4 proj = g_camera->getProjection();
        Mat4 view = g_camera->getView();
        g_sprites->begin(proj, view);

        // Query all entities that have Position + Sprite.
        EntityId ents[1024];
        ComponentMask mask = componentBit(ComponentType::Position) |
                             componentBit(ComponentType::Sprite);
        int n = g_world->queryActive(mask, ents, 1024);
        for (int i = 0; i < n; i++) {
            PositionComponent* p = g_world->getComponent<PositionComponent>(ents[i]);
            SpriteComponent*    s = g_world->getComponent<SpriteComponent>(ents[i]);
            if (!p || !s || !s->visible) continue;
            SpriteData d;
            d.x = p->x - s->width  * s->originX;
            d.y = p->y - s->height * s->originY;
            d.width  = s->width;
            d.height = s->height;
            d.r = s->r; d.g = s->g; d.b = s->b; d.a = s->a;
            d.rotation = s->rotation;
            d.originX  = s->originX;
            d.originY  = s->originY;
            g_sprites->draw(d, s->texture);
        }
        g_sprites->end();
    } else {
        // Bare-minimum fallback: just clear the screen so the canvas isn't
        // stuck on the previous frame.
        if (g_renderer) g_renderer->clear(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // WebGL doesn't require an explicit swap - the browser composites the
    // default framebuffer at the end of each rAF tick.
}

// =============================================================================
// WASM module entry point.
//
// On the web, main() runs once when the WASM module is instantiated. We do
// NOT create a window or GL context here - the JS bridge does that before
// calling td_init(). main()'s only job is to log readiness.
// =============================================================================
int main()
{
    TD_LOG_INFO("TD Engine WASM module loaded. Awaiting td_init()...");
    return 0;
}
