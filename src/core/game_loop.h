#pragma once

// Forward-declare Win32Window so this header is portable. The full
// #include "../platform/win32_window.h" pulls in <windows.h> which uses
// __stdcall (undefined on Linux/macOS plain desktop builds). The .cpp file
// includes win32_window.h where the definition is needed.
namespace td {
class Win32Window;
}

namespace td {

class GameLoop {
public:
    using InitCallback   = void(*)();
    using UpdateCallback = void(*)(float dt);
    using RenderCallback = void(*)(float alpha);
    using ShutdownCallback = void(*)();

    void setCallbacks(InitCallback init, UpdateCallback update, RenderCallback render);
    void setShutdownCallback(ShutdownCallback shutdown);
    void run(Win32Window& window);
    void stop();

    void setFixedStep(float step) { m_fixedStep = step; }
    float getFixedStep() const { return m_fixedStep; }

    bool isRunning() const { return m_running; }
    double getAccumulator() const { return m_accumulator; }

private:
    InitCallback     m_init     = nullptr;
    UpdateCallback   m_update   = nullptr;
    RenderCallback   m_render   = nullptr;
    ShutdownCallback m_shutdown = nullptr;

    float m_fixedStep = 1.0f / 60.0f;
    double m_accumulator = 0;
    bool m_running = false;
};

} // namespace td
