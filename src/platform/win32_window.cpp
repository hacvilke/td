#include "win32_window.h"
#include <cstring>
#include <cstdio>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

// OpenGL extension loading
typedef BOOL (WINAPI *PFNWGLSWAPINTERVALEXTPROC)(int interval);
typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShareContext, const int *attribList);

// WGL constants
#define WGL_CONTEXT_MAJOR_VERSION_ARB     0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB     0x2092
#define WGL_CONTEXT_FLAGS_ARB             0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB      0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB  0x00000001

namespace td {

Win32Window* g_win32Window = nullptr;

bool Win32Window::create(const WindowConfig& config) {
    g_win32Window = this;
    
    m_hinstance = GetModuleHandle(nullptr);
    m_width = config.width;
    m_height = config.height;
    
    // Register window class
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = (WNDPROC)Win32Window::windowProc;
    wc.hInstance = (HINSTANCE)m_hinstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "TDEngineWindowClass";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    
    if (!RegisterClassExA(&wc)) {
        return false;
    }
    
    // Calculate window size to get desired client area
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!config.resizable) {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }
    
    RECT rect = { 0, 0, config.width, config.height };
    AdjustWindowRect(&rect, style, FALSE);
    
    int windowWidth = rect.right - rect.left;
    int windowHeight = rect.bottom - rect.top;
    
    // Center window on screen
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenWidth - windowWidth) / 2;
    int posY = (screenHeight - windowHeight) / 2;
    
    // Create window
    m_hwnd = CreateWindowExA(
        0,
        "TDEngineWindowClass",
        config.title,
        style,
        posX, posY,
        windowWidth, windowHeight,
        nullptr,
        nullptr,
        (HINSTANCE)m_hinstance,
        nullptr
    );
    
    if (!m_hwnd) {
        return false;
    }
    
    m_hdc = GetDC((HWND)m_hwnd);
    if (!m_hdc) {
        DestroyWindow((HWND)m_hwnd);
        return false;
    }
    
    // Create OpenGL context
    if (!createOpenGLContext()) {
        ReleaseDC((HWND)m_hwnd, (HDC)m_hdc);
        DestroyWindow((HWND)m_hwnd);
        return false;
    }
    
    // Initialize timing
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    m_perfFreq = freq.QuadPart;
    m_lastTime = counter.QuadPart;
    
    // Initialize input state
    memset(&input, 0, sizeof(InputState));
    
    // Show window
    ShowWindow((HWND)m_hwnd, SW_SHOW);
    UpdateWindow((HWND)m_hwnd);
    
    if (config.fullscreen) {
        // Toggle fullscreen
        DEVMODE dm = {};
        dm.dmSize = sizeof(dm);
        dm.dmPelsWidth = config.width;
        dm.dmPelsHeight = config.height;
        dm.dmBitsPerPel = 32;
        dm.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
        ChangeDisplaySettings(&dm, CDS_FULLSCREEN);
        SetWindowLong((HWND)m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos((HWND)m_hwnd, HWND_TOP, 0, 0, config.width, config.height, SWP_FRAMECHANGED);
    }
    
    return true;
}

bool Win32Window::createOpenGLContext() {
    // Setup pixel format
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;
    
    int pixelFormat = ChoosePixelFormat((HDC)m_hdc, &pfd);
    if (!pixelFormat) {
        return false;
    }
    
    if (!SetPixelFormat((HDC)m_hdc, pixelFormat, &pfd)) {
        return false;
    }
    
    // Create temporary context to get extension functions
    HGLRC tempContext = wglCreateContext((HDC)m_hdc);
    if (!tempContext) {
        return false;
    }
    
    if (!wglMakeCurrent((HDC)m_hdc, tempContext)) {
        wglDeleteContext(tempContext);
        return false;
    }
    
    // Try to create OpenGL 3.3 core context
    PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = 
        (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
    
    if (wglCreateContextAttribsARB) {
        int attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0
        };
        
        HGLRC coreContext = wglCreateContextAttribsARB((HDC)m_hdc, nullptr, attribs);
        
        if (coreContext) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(tempContext);
            
            if (!wglMakeCurrent((HDC)m_hdc, coreContext)) {
                wglDeleteContext(coreContext);
                return false;
            }
            
            m_hglrc = coreContext;
        } else {
            // Fall back to compatibility context
            m_hglrc = tempContext;
        }
    } else {
        m_hglrc = tempContext;
    }
    
    // Enable VSync by default
    setVSync(true);
    
    return true;
}

void Win32Window::destroyOpenGLContext() {
    if (m_hglrc) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext((HGLRC)m_hglrc);
        m_hglrc = nullptr;
    }
}

void Win32Window::destroy() {
    destroyOpenGLContext();
    
    if (m_hdc) {
        ReleaseDC((HWND)m_hwnd, (HDC)m_hdc);
        m_hdc = nullptr;
    }
    
    if (m_hwnd) {
        DestroyWindow((HWND)m_hwnd);
        m_hwnd = nullptr;
    }
    
    UnregisterClassA("TDEngineWindowClass", (HINSTANCE)m_hinstance);
    g_win32Window = nullptr;
}

void Win32Window::pollEvents() {
    // Store previous input state
    memcpy(input.keysPrev, input.keys, sizeof(input.keys));
    memcpy(input.mouseButtonsPrev, input.mouseButtons, sizeof(input.mouseButtons));
    input.scrollX = 0;
    input.scrollY = 0;
    input.mouseDeltaX = 0;
    input.mouseDeltaY = 0;
    
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            m_shouldClose = true;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    
    updateTiming();
}

void Win32Window::updateTiming() {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    
    long long currentTime = counter.QuadPart;
    long long elapsed = currentTime - m_lastTime;
    m_lastTime = currentTime;
    
    time.deltaTime = (float)elapsed / (float)m_perfFreq;
    
    // Clamp delta time to avoid spiral of death
    if (time.deltaTime > 0.25f) {
        time.deltaTime = 0.25f;
    }
    
    time.totalTime += time.deltaTime;
    time.frameCount++;
}

void Win32Window::swapBuffers() {
    SwapBuffers((HDC)m_hdc);
}

void Win32Window::setTitle(const char* title) {
    SetWindowTextA((HWND)m_hwnd, title);
}

void Win32Window::setVSync(bool enabled) {
    PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = 
        (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
    
    if (wglSwapIntervalEXT) {
        wglSwapIntervalEXT(enabled ? 1 : 0);
    }
}

long long __stdcall Win32Window::windowProc(void* hwnd, unsigned int msg, 
                                             unsigned long long wparam, long long lparam) {
    if (!g_win32Window) {
        return DefWindowProcA((HWND)hwnd, msg, wparam, lparam);
    }
    
    switch (msg) {
        case WM_CLOSE:
            g_win32Window->m_shouldClose = true;
            return 0;
            
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
            
        case WM_SIZE:
            {
                int width = LOWORD(lparam);
                int height = HIWORD(lparam);
                g_win32Window->processResizeEvent(width, height);
                
                if (wparam == SIZE_MINIMIZED) {
                    g_win32Window->m_minimized = true;
                } else if (wparam == SIZE_RESTORED || wparam == SIZE_MAXIMIZED) {
                    g_win32Window->m_minimized = false;
                }
            }
            return 0;
            
        case WM_SETFOCUS:
            g_win32Window->m_focused = true;
            return 0;
            
        case WM_KILLFOCUS:
            g_win32Window->m_focused = false;
            // Clear input state when losing focus
            memset(g_win32Window->input.keys, 0, sizeof(g_win32Window->input.keys));
            memset(g_win32Window->input.mouseButtons, 0, sizeof(g_win32Window->input.mouseButtons));
            return 0;
            
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (wparam < 256) {
                g_win32Window->processKeyEvent((unsigned int)wparam, true);
            }
            return 0;
            
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (wparam < 256) {
                g_win32Window->processKeyEvent((unsigned int)wparam, false);
            }
            return 0;
            
        case WM_MOUSEMOVE:
            {
                int x = LOWORD(lparam);
                int y = HIWORD(lparam);
                g_win32Window->processMouseMoveEvent(x, y);
            }
            return 0;
            
        case WM_LBUTTONDOWN:
            SetCapture((HWND)hwnd);
            g_win32Window->processMouseButtonEvent(0, true);
            return 0;
            
        case WM_LBUTTONUP:
            ReleaseCapture();
            g_win32Window->processMouseButtonEvent(0, false);
            return 0;
            
        case WM_RBUTTONDOWN:
            SetCapture((HWND)hwnd);
            g_win32Window->processMouseButtonEvent(1, true);
            return 0;
            
        case WM_RBUTTONUP:
            ReleaseCapture();
            g_win32Window->processMouseButtonEvent(1, false);
            return 0;
            
        case WM_MBUTTONDOWN:
            SetCapture((HWND)hwnd);
            g_win32Window->processMouseButtonEvent(2, true);
            return 0;
            
        case WM_MBUTTONUP:
            ReleaseCapture();
            g_win32Window->processMouseButtonEvent(2, false);
            return 0;
            
        case WM_MOUSEWHEEL:
            {
                int delta = GET_WHEEL_DELTA_WPARAM(wparam);
                g_win32Window->processMouseWheelEvent(delta);
            }
            return 0;
            
        case WM_CHAR:
            // Handle text input if needed
            return 0;
    }
    
    return DefWindowProcA((HWND)hwnd, msg, wparam, lparam);
}

void Win32Window::processKeyEvent(unsigned int vk, bool down) {
    if (vk < 256) {
        input.keys[vk] = down;
    }
}

void Win32Window::processMouseMoveEvent(int x, int y) {
    float fx = (float)x;
    float fy = (float)y;
    
    if (m_firstMouse) {
        m_lastMouseX = fx;
        m_lastMouseY = fy;
        m_firstMouse = false;
    }
    
    input.mouseDeltaX = fx - m_lastMouseX;
    input.mouseDeltaY = fy - m_lastMouseY;
    m_lastMouseX = fx;
    m_lastMouseY = fy;
    
    input.mouseX = fx;
    input.mouseY = fy;
}

void Win32Window::processMouseButtonEvent(int button, bool down) {
    if (button >= 0 && button < 8) {
        input.mouseButtons[button] = down;
    }
}

void Win32Window::processMouseWheelEvent(int delta) {
    input.scrollY = (float)delta / 120.0f;
}

void Win32Window::processResizeEvent(int width, int height) {
    if (width > 0 && height > 0) {
        m_width = width;
        m_height = height;
    }
}

} // namespace td
