#include "win32_input.h"
#include <cstring>

namespace td {

void InputManager::update(InputState& input) {
    // Copy previous state
    memcpy(m_keysPrev, m_keys, sizeof(m_keys));
    memcpy(m_mouseButtonsPrev, m_mouseButtons, sizeof(m_mouseButtons));
    
    // Copy current state from InputState
    memcpy(m_keys, input.keys, sizeof(m_keys));
    memcpy(m_mouseButtons, input.mouseButtons, sizeof(m_mouseButtons));
    
    m_mouseX = input.mouseX;
    m_mouseY = input.mouseY;
    m_mouseDeltaX = input.mouseDeltaX;
    m_mouseDeltaY = input.mouseDeltaY;
    m_scrollX = input.scrollX;
    m_scrollY = input.scrollY;
}

void InputManager::reset() {
    memset(m_keys, 0, sizeof(m_keys));
    memset(m_keysPrev, 0, sizeof(m_keysPrev));
    memset(m_mouseButtons, 0, sizeof(m_mouseButtons));
    memset(m_mouseButtonsPrev, 0, sizeof(m_mouseButtonsPrev));
    m_mouseX = 0;
    m_mouseY = 0;
    m_mouseDeltaX = 0;
    m_mouseDeltaY = 0;
    m_scrollX = 0;
    m_scrollY = 0;
}

bool InputManager::isKeyPressed(int key) const {
    if (key < 0 || key >= 256) return false;
    return m_keys[key] && !m_keysPrev[key];
}

bool InputManager::isKeyDown(int key) const {
    if (key < 0 || key >= 256) return false;
    return m_keys[key];
}

bool InputManager::isKeyReleased(int key) const {
    if (key < 0 || key >= 256) return false;
    return !m_keys[key] && m_keysPrev[key];
}

bool InputManager::isMousePressed(int button) const {
    if (button < 0 || button >= 8) return false;
    return m_mouseButtons[button] && !m_mouseButtonsPrev[button];
}

bool InputManager::isMouseDown(int button) const {
    if (button < 0 || button >= 8) return false;
    return m_mouseButtons[button];
}

bool InputManager::isMouseReleased(int button) const {
    if (button < 0 || button >= 8) return false;
    return !m_mouseButtons[button] && m_mouseButtonsPrev[button];
}

float InputManager::getHorizontalAxis() const {
    float axis = 0.0f;
    
    // Arrow keys
    if (m_keys[Key::Left] || m_keys[Key::A]) {
        axis -= 1.0f;
    }
    if (m_keys[Key::Right] || m_keys[Key::D]) {
        axis += 1.0f;
    }
    
    return axis;
}

float InputManager::getVerticalAxis() const {
    float axis = 0.0f;
    
    // Arrow keys  
    if (m_keys[Key::Down] || m_keys[Key::S]) {
        axis -= 1.0f;
    }
    if (m_keys[Key::Up] || m_keys[Key::W]) {
        axis += 1.0f;
    }
    
    return axis;
}

} // namespace td
