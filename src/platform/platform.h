#pragma once
#include <cstdint>

namespace td {

struct WindowConfig {
    const char* title = "TD Engine";
    int width = 800;
    int height = 600;
    bool resizable = true;
    bool fullscreen = false;
};

struct InputState {
    bool keys[256] = {};
    bool keysPrev[256] = {};
    bool mouseButtons[8] = {};
    bool mouseButtonsPrev[8] = {};
    float mouseX = 0, mouseY = 0;
    float mouseDeltaX = 0, mouseDeltaY = 0;
    float scrollX = 0, scrollY = 0;
    
    bool keyPressed(int key) const { return keys[key] && !keysPrev[key]; }
    bool keyDown(int key) const { return keys[key]; }
    bool keyReleased(int key) const { return !keys[key] && keysPrev[key]; }
    bool mousePressed(int button) const { return mouseButtons[button] && !mouseButtonsPrev[button]; }
    bool mouseDown(int button) const { return mouseButtons[button]; }
    bool mouseReleased(int button) const { return !mouseButtons[button] && mouseButtonsPrev[button]; }
};

struct TimeState {
    double totalTime = 0;
    float deltaTime = 0;
    float fixedDeltaTime = 1.0f / 60.0f;
    uint64_t frameCount = 0;
};

// Virtual key codes for cross-platform compatibility
namespace Key {
    constexpr int Left = 0x25;
    constexpr int Up = 0x26;
    constexpr int Right = 0x27;
    constexpr int Down = 0x28;
    constexpr int Space = 0x20;
    constexpr int Enter = 0x0D;
    constexpr int Escape = 0x1B;
    constexpr int Tab = 0x09;
    constexpr int Backspace = 0x08;
    constexpr int Delete = 0x2E;
    constexpr int Shift = 0x10;
    constexpr int Control = 0x11;
    constexpr int Alt = 0x12;
    constexpr int A = 0x41;
    constexpr int B = 0x42;
    constexpr int C = 0x43;
    constexpr int D = 0x44;
    constexpr int E = 0x45;
    constexpr int F = 0x46;
    constexpr int G = 0x47;
    constexpr int H = 0x48;
    constexpr int I = 0x49;
    constexpr int J = 0x4A;
    constexpr int K = 0x4B;
    constexpr int L = 0x4C;
    constexpr int M = 0x4D;
    constexpr int N = 0x4E;
    constexpr int O = 0x4F;
    constexpr int P = 0x50;
    constexpr int Q = 0x51;
    constexpr int R = 0x52;
    constexpr int S = 0x53;
    constexpr int T = 0x54;
    constexpr int U = 0x55;
    constexpr int V = 0x56;
    constexpr int W = 0x57;
    constexpr int X = 0x58;
    constexpr int Y = 0x59;
    constexpr int Z = 0x5A;
    constexpr int Num0 = 0x30;
    constexpr int Num1 = 0x31;
    constexpr int Num2 = 0x32;
    constexpr int Num3 = 0x33;
    constexpr int Num4 = 0x34;
    constexpr int Num5 = 0x35;
    constexpr int Num6 = 0x36;
    constexpr int Num7 = 0x37;
    constexpr int Num8 = 0x38;
    constexpr int Num9 = 0x39;
    constexpr int F1 = 0x70;
    constexpr int F2 = 0x71;
    constexpr int F3 = 0x72;
    constexpr int F4 = 0x73;
    constexpr int F5 = 0x74;
    constexpr int F6 = 0x75;
    constexpr int F7 = 0x76;
    constexpr int F8 = 0x77;
    constexpr int F9 = 0x78;
    constexpr int F10 = 0x79;
    constexpr int F11 = 0x7A;
    constexpr int F12 = 0x7B;
}

namespace Mouse {
    constexpr int Left = 0;
    constexpr int Right = 1;
    constexpr int Middle = 2;
}

} // namespace td
