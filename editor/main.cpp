// TD Engine Editor
// Visual editor for creating and editing game scenes

#include "../src/platform/platform.h"
#include "../src/platform/win32_window.h"
#include "../src/core/game_loop.h"
#include "../src/core/logger.h"
#include "../src/renderer/gl_renderer.h"
#include "../src/renderer/gl_shader.h"
#include "../src/renderer/sprite_batch.h"
#include "../src/renderer/camera.h"
#include "../src/renderer/mesh.h"
#include "../src/renderer/framebuffer.h"
#include "../src/ecs/world.h"
#include "../src/core/math/mat4.h"

#include "scene_panel.h"
#include "inspector_panel.h"
#include "asset_browser.h"
#include "console_panel.h"
#include "menu_bar.h"

#include <cstdio>
#include <cstring>

using namespace td;

// Immediate mode GUI context for the editor.
// Defined inside namespace td so it matches the forward declarations in
// the panel headers (scene_panel.h, inspector_panel.h, etc.). Without this,
// MSVC sees both ::GuiContext (local) and td::GuiContext (forward-declared)
// as ambiguous when 'using namespace td' is in effect.
namespace td {
struct GuiContext {
    float mouseX, mouseY;
    bool mouseDown, mouseClicked;
    int hotItem, activeItem;
    float layoutX, layoutY;
    float layoutW;
    float cursorY;
    int widgetId;
    bool keys[256];
    char textInput[32];
    int textInputLen;

    void beginFrame(const InputState& input) {
        mouseX = input.mouseX;
        mouseY = input.mouseY;
        mouseClicked = input.mousePressed(Mouse::Left);
        mouseDown = input.mouseDown(Mouse::Left);
        hotItem = 0;
        widgetId = 0;
        textInputLen = 0;
        memcpy(keys, input.keys, sizeof(keys));
    }

    void endFrame() {
        if (!mouseDown) activeItem = 0;
    }

    int nextId() { return ++widgetId; }

    bool regionHit(float x, float y, float w, float h) {
        return mouseX >= x && mouseX <= x + w &&
               mouseY >= y && mouseY <= y + h;
    }
};
} // namespace td

// Editor state
static Win32Window* g_window = nullptr;
static SpriteBatch* g_batch = nullptr;
static Camera3D g_editorCamera;
static World g_world;
static GuiContext g_gui;

static ScenePanel g_scenePanel;
static InspectorPanel g_inspectorPanel;
static AssetBrowser g_assetBrowser;
static ConsolePanel g_consolePanel;
static MenuBar g_menuBar;

static Shader g_3dShader;
static Mesh g_cubeMesh;
static Mesh g_planeMesh;

static EntityId g_selectedEntity = INVALID_ENTITY;
static bool g_playMode = false;
static char g_scenePath[256] = "";

// Editor layout constants
static const int SCENE_PANEL_W = 220;
static const int INSPECTOR_W = 280;
static const int CONSOLE_H = 180;
static const int MENU_BAR_H = 25;

void drawQuadColor(float x, float y, float w, float h, float r, float g, float b, float a) {
    g_batch->drawQuad(x, y, w, h, r, g, b, a);
}

void editorInit() {
    TD_LOG_INFO("TD Engine Editor starting...");

    Renderer::get().init();

    g_batch = new SpriteBatch();
    g_batch->init();

    // Setup editor camera
    g_editorCamera.setViewport(g_window->getWidth(), g_window->getHeight());
    g_editorCamera.setPosition(Vec3(0, 5, 10));
    g_editorCamera.setTarget(Vec3(0, 0, 0));
    g_editorCamera.setFOV(60.0f);

    // Create default scene
    EntityId floor = g_world.createEntity("Floor");
    {
        Transform3DComponent* t = g_world.addComponent<Transform3DComponent>(floor);
        t->position = Vec3(0, 0, 0);
        t->scale = Vec3(10, 1, 10);
        MeshRendererComponent* m = g_world.addComponent<MeshRendererComponent>(floor);
        m->color = Vec3(0.4f, 0.5f, 0.4f);
    }

    EntityId cube = g_world.createEntity("Cube");
    {
        Transform3DComponent* t = g_world.addComponent<Transform3DComponent>(cube);
        t->position = Vec3(0, 1, 0);
        MeshRendererComponent* m = g_world.addComponent<MeshRendererComponent>(cube);
        m->color = Vec3(0.8f, 0.3f, 0.3f);
    }

    EntityId light = g_world.createEntity("DirectionalLight");
    {
        Transform3DComponent* t = g_world.addComponent<Transform3DComponent>(light);
        t->position = Vec3(5, 10, 5);
        LightComponent* l = g_world.addComponent<LightComponent>(light);
        l->type = LightComponent::Type::Directional;
        l->color = Vec3(1, 1, 0.9f);
        l->intensity = 1.0f;
    }

    TD_LOG_INFO("Editor initialized with default scene");
}

void editorUpdate(float dt) {
    const InputState& input = g_window->input;

    // Editor camera controls
    if (!g_playMode) {
        // Middle mouse orbit
        if (input.mouseDown(Mouse::Middle)) {
            g_editorCamera.orbit(input.mouseDeltaX * 0.005f, input.mouseDeltaY * 0.005f);
        }

        // Right mouse pan
        if (input.mouseDown(Mouse::Right)) {
            g_editorCamera.pan(input.mouseDeltaX * 0.01f, input.mouseDeltaY * 0.01f);
        }

        // Scroll zoom
        if (input.scrollY != 0) {
            g_editorCamera.zoom(input.scrollY * 0.5f);
        }
    }

    // Keyboard shortcuts
    if (input.keys[Key::Control]) {
        if (input.keyPressed(Key::S)) {
            TD_LOG_INFO("Scene saved");
        }
        if (input.keyPressed(Key::Z)) {
            TD_LOG_INFO("Undo");
        }
    }

    // Toggle play mode
    if (input.keyPressed(Key::F5)) {
        g_playMode = !g_playMode;
        TD_LOG_INFO(g_playMode ? "Play mode started" : "Play mode stopped");
    }

    // Update systems in play mode
    if (g_playMode) {
        g_world.updateSystems(dt);
    }
}

void editorRender(float alpha) {
    (void)alpha;

    int w = g_window->getWidth();
    int h = g_window->getHeight();

    Renderer& renderer = Renderer::get();
    renderer.clear(0.18f, 0.18f, 0.22f, 1.0f);
    renderer.setViewport(0, 0, w, h);

    // === Draw 2D Editor GUI with SpriteBatch ===
    Mat4 ortho = Mat4::orthographic(0, (float)w, (float)h, 0, -100, 100);
    g_batch->begin(ortho);

    g_gui.beginFrame(g_window->input);

    // Menu bar background
    drawQuadColor(0, 0, (float)w, (float)MENU_BAR_H, 0.15f, 0.15f, 0.18f, 1.0f);

    // Menu items
    const char* menus[] = {"File", "Edit", "View", "GameObject", "Build", "Help"};
    float mx = 10;
    for (int i = 0; i < 6; i++) {
        float mw = 70;
        bool hover = g_gui.regionHit(mx, 0, mw, (float)MENU_BAR_H);
        if (hover) {
            drawQuadColor(mx, 0, mw, (float)MENU_BAR_H, 0.3f, 0.3f, 0.35f, 1.0f);
        }
        mx += mw + 5;
    }

    // Play/Stop button
    float playBtnX = (float)w / 2 - 30;
    bool playHover = g_gui.regionHit(playBtnX, 2, 60, 20);
    drawQuadColor(playBtnX, 2, 60, 20,
                  g_playMode ? 0.8f : 0.2f,
                  g_playMode ? 0.2f : 0.6f,
                  0.2f, 1.0f);

    // Scene panel background (left)
    float scenePanelY = (float)MENU_BAR_H;
    float scenePanelH = (float)h - MENU_BAR_H - CONSOLE_H;
    drawQuadColor(0, scenePanelY, (float)SCENE_PANEL_W, scenePanelH,
                  0.12f, 0.12f, 0.15f, 1.0f);

    // Scene panel header
    drawQuadColor(0, scenePanelY, (float)SCENE_PANEL_W, 28,
                  0.18f, 0.18f, 0.22f, 1.0f);

    // Draw entity list
    float entityY = scenePanelY + 30;
    for (int i = 1; i <= 20 && entityY < scenePanelY + scenePanelH - 10; i++) {
        EntityId eid = (EntityId)i;
        if (!g_world.entityExists(eid)) continue;

        const char* name = g_world.getEntityName(eid);
        (void)name;

        bool selected = (eid == g_selectedEntity);
        bool hover = g_gui.regionHit(2, entityY, (float)SCENE_PANEL_W - 4, 22);

        if (selected) {
            drawQuadColor(2, entityY, (float)SCENE_PANEL_W - 4, 22,
                          0.2f, 0.35f, 0.55f, 1.0f);
        } else if (hover) {
            drawQuadColor(2, entityY, (float)SCENE_PANEL_W - 4, 22,
                          0.2f, 0.2f, 0.25f, 1.0f);
        }

        if (hover && g_gui.mouseClicked) {
            g_selectedEntity = eid;
        }

        entityY += 24;
    }

    // Inspector panel background (right)
    float inspX = (float)(w - INSPECTOR_W);
    drawQuadColor(inspX, scenePanelY, (float)INSPECTOR_W, scenePanelH,
                  0.12f, 0.12f, 0.15f, 1.0f);

    // Inspector header
    drawQuadColor(inspX, scenePanelY, (float)INSPECTOR_W, 28,
                  0.18f, 0.18f, 0.22f, 1.0f);

    // Draw inspector content
    if (g_selectedEntity != INVALID_ENTITY && g_world.entityExists(g_selectedEntity)) {
        float iy = scenePanelY + 35;
        float iw = (float)INSPECTOR_W - 20;

        // Entity name
        drawQuadColor(inspX + 10, iy, iw, 24, 0.2f, 0.2f, 0.25f, 1.0f);
        iy += 30;

        // Component sections
        if (g_world.hasComponent<Transform3DComponent>(g_selectedEntity)) {
            drawQuadColor(inspX + 10, iy, iw, 20, 0.18f, 0.22f, 0.28f, 1.0f);
            iy += 24;
            // Position fields
            for (int j = 0; j < 3; j++) {
                drawQuadColor(inspX + 15, iy, iw - 10, 18, 0.15f, 0.15f, 0.18f, 1.0f);
                iy += 20;
            }
            iy += 5;
        }

        if (g_world.hasComponent<MeshRendererComponent>(g_selectedEntity)) {
            drawQuadColor(inspX + 10, iy, iw, 20, 0.22f, 0.18f, 0.18f, 1.0f);
            iy += 24;
            for (int j = 0; j < 3; j++) {
                drawQuadColor(inspX + 15, iy, iw - 10, 18, 0.15f, 0.15f, 0.18f, 1.0f);
                iy += 20;
            }
            iy += 5;
        }

        if (g_world.hasComponent<LightComponent>(g_selectedEntity)) {
            drawQuadColor(inspX + 10, iy, iw, 20, 0.22f, 0.22f, 0.18f, 1.0f);
            iy += 24;
            for (int j = 0; j < 3; j++) {
                drawQuadColor(inspX + 15, iy, iw - 10, 18, 0.15f, 0.15f, 0.18f, 1.0f);
                iy += 20;
            }
        }
    }

    // Console panel background (bottom)
    float consoleY = (float)(h - CONSOLE_H);
    drawQuadColor(0, consoleY, (float)w, (float)CONSOLE_H,
                  0.1f, 0.1f, 0.12f, 1.0f);

    // Console header
    drawQuadColor(0, consoleY, (float)w, 24, 0.15f, 0.15f, 0.18f, 1.0f);

    // Console log lines
    Logger& logger = Logger::get();
    float logY = consoleY + 28;
    int logCount = logger.getLogCount();
    int startLog = logCount > 8 ? logCount - 8 : 0;
    for (int i = startLog; i < logCount && logY < (float)h - 30; i++) {
        const LogMessage* msg = logger.getMessage(i);
        if (!msg) continue;

        float r = 0.7f, g = 0.7f, b = 0.7f;
        if (msg->level == LogLevel::Warning) { r = 0.9f; g = 0.8f; b = 0.2f; }
        if (msg->level == LogLevel::Error) { r = 0.9f; g = 0.2f; b = 0.2f; }

        // Draw colored indicator
        drawQuadColor(5, logY + 2, 8, 12, r, g, b, 1.0f);
        logY += 18;
    }

    // Console input bar
    drawQuadColor(0, (float)h - 26, (float)w, 26, 0.13f, 0.13f, 0.16f, 1.0f);

    // Viewport area (center)
    float vpX = (float)SCENE_PANEL_W;
    float vpY = (float)MENU_BAR_H;
    float vpW = (float)(w - SCENE_PANEL_W - INSPECTOR_W);
    float vpH = scenePanelH;

    // Draw viewport grid lines
    float gridStep = 40.0f;
    for (float gx = vpX; gx < vpX + vpW; gx += gridStep) {
        drawQuadColor(gx, vpY, 1, vpH, 0.2f, 0.2f, 0.22f, 0.3f);
    }
    for (float gy = vpY; gy < vpY + vpH; gy += gridStep) {
        drawQuadColor(vpX, gy, vpW, 1, 0.2f, 0.2f, 0.22f, 0.3f);
    }

    // Draw viewport border
    drawQuadColor(vpX, vpY, vpW, 2, 0.25f, 0.25f, 0.3f, 1.0f);
    drawQuadColor(vpX, vpY + vpH - 2, vpW, 2, 0.25f, 0.25f, 0.3f, 1.0f);
    drawQuadColor(vpX, vpY, 2, vpH, 0.25f, 0.25f, 0.3f, 1.0f);
    drawQuadColor(vpX + vpW - 2, vpY, 2, vpH, 0.25f, 0.25f, 0.3f, 1.0f);

    // Status bar (very bottom)
    char statusText[256];
    snprintf(statusText, sizeof(statusText), "Entities: %d | %s",
             g_world.getEntityCount(), g_playMode ? "PLAYING" : "EDITOR");
    (void)statusText;

    g_gui.endFrame();
    g_batch->end();
}

int main() {
    Logger::get().init("editor.log");

    WindowConfig config;
    config.title = "TD Engine Editor";
    config.width = 1280;
    config.height = 720;
    config.resizable = true;

    Win32Window window;
    if (!window.create(config)) {
        TD_LOG_ERROR("Failed to create editor window");
        return 1;
    }

    g_window = &window;

    GameLoop loop;
    loop.setCallbacks(editorInit, editorUpdate, editorRender);
    loop.setFixedStep(1.0f / 60.0f);
    loop.run(window);

    if (g_batch) {
        g_batch->shutdown();
        delete g_batch;
    }

    Renderer::get().shutdown();
    window.destroy();
    Logger::get().shutdown();

    return 0;
}
