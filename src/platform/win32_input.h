#pragma once
#include "platform.h"

namespace td {

class InputManager {
public:
    void update(InputState& input);
    void reset();
    
    bool isKeyPressed(int key) const;
    bool isKeyDown(int key) const;
    bool isKeyReleased(int key) const;
    
    bool isMousePressed(int button) const;
    bool isMouseDown(int button) const;
    bool isMouseReleased(int button) const;
    
    float getMouseX() const { return m_mouseX; }
    float getMouseY() const { return m_mouseY; }
    float getMouseDeltaX() const { return m_mouseDeltaX; }
    float getMouseDeltaY() const { return m_mouseDeltaY; }
    float getScrollDelta() const { return m_scrollY; }
    
    // Axis helpers for game input
    float getHorizontalAxis() const;
    float getVerticalAxis() const;
    
private:
    bool m_keys[256] = {};
    bool m_keysPrev[256] = {};
    bool m_mouseButtons[8] = {};
    bool m_mouseButtonsPrev[8] = {};
    float m_mouseX = 0;
    float m_mouseY = 0;
    float m_mouseDeltaX = 0;
    float m_mouseDeltaY = 0;
    float m_scrollX = 0;
    float m_scrollY = 0;
};

} // namespace td
