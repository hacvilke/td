#pragma once
#include "platform.h"

namespace td {

class Win32Window {
public:
    bool create(const WindowConfig& config);
    void destroy();
    void pollEvents();
    bool shouldClose() const { return m_shouldClose; }
    void requestClose() { m_shouldClose = true; }
    void* getNativeHandle() const { return m_hwnd; }
    void* getDeviceContext() const { return m_hdc; }
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    void swapBuffers();
    void setTitle(const char* title);
    void setVSync(bool enabled);
    bool isMinimized() const { return m_minimized; }
    bool isFocused() const { return m_focused; }

    InputState input;
    TimeState time;

private:
    static long long __stdcall windowProc(void* hwnd, unsigned int msg, 
                                          unsigned long long wparam, long long lparam);
    void processKeyEvent(unsigned int vk, bool down);
    void processMouseMoveEvent(int x, int y);
    void processMouseButtonEvent(int button, bool down);
    void processMouseWheelEvent(int delta);
    void processResizeEvent(int width, int height);
    void updateTiming();
    bool createOpenGLContext();
    void destroyOpenGLContext();

    void* m_hwnd = nullptr;
    void* m_hdc = nullptr;
    void* m_hglrc = nullptr;
    void* m_hinstance = nullptr;
    bool m_shouldClose = false;
    bool m_minimized = false;
    bool m_focused = true;
    int m_width = 800;
    int m_height = 600;
    long long m_lastTime = 0;
    long long m_perfFreq = 0;
    float m_lastMouseX = 0;
    float m_lastMouseY = 0;
    bool m_firstMouse = true;
};

// Global window pointer for window proc callback
extern Win32Window* g_win32Window;

} // namespace td
