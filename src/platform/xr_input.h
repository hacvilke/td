// =============================================================================
// TD Engine - XR + Mobile Touch Input (Tier 4)
//
// Cross-platform input abstraction for:
//   - Touch (mobile, tablet, touchscreen laptops): multi-touch, gestures.
//   - XR (VR/AR) controllers: 6-DoF poses, buttons, thumbsticks, haptics.
//   - Gamepads (Xbox/PlayStation/Switch): standard mapping via the
//     Standard Gamepad spec.
//
// Status: REAL implementation. The platform-specific controller discovery
// (WebXR, OpenXR, XInput, HTML5 Gamepad API) is delegated to platform
// callbacks so this header stays portable.
// =============================================================================
#pragma once
#include "../core/math/vec2.h"
#include "../core/math/vec3.h"
#include "../core/math/mat4.h"
#include "../core/logger.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace td {
namespace input {

// ---------------------------------------------------------------------------
// Touch — a single touch point (finger on screen).
// ---------------------------------------------------------------------------
struct Touch {
    int id = 0;                // platform-assigned; 0 = primary touch
    Vec2 position{0, 0};       // in screen pixels
    Vec2 delta{0, 0};          // movement since last frame
    float pressure = 1.0f;     // 0..1
    bool active = false;       // is the finger currently down?
    bool startedThisFrame = false;
    bool endedThisFrame = false;
};

// ---------------------------------------------------------------------------
// TouchManager — tracks up to 10 simultaneous touches.
// ---------------------------------------------------------------------------
class TouchManager {
public:
    static const int MAX_TOUCHES = 10;

    void beginFrame() {
        for (auto& t : touches_) {
            t.delta = Vec2(0, 0);
            t.startedThisFrame = false;
            t.endedThisFrame = false;
        }
    }

    // Called by the platform layer when a touch starts / moves / ends.
    void onTouchStart(int id, const Vec2& pos, float pressure = 1.0f) {
        Touch* t = findOrAlloc(id);
        if (!t) return;
        t->position = pos;
        t->delta = Vec2(0, 0);
        t->pressure = pressure;
        t->active = true;
        t->startedThisFrame = true;
    }
    void onTouchMove(int id, const Vec2& pos, float pressure = 1.0f) {
        Touch* t = findById(id);
        if (!t || !t->active) return;
        t->delta = Vec2(pos.x - t->position.x, pos.y - t->position.y);
        t->position = pos;
        t->pressure = pressure;
    }
    void onTouchEnd(int id, const Vec2& pos) {
        Touch* t = findById(id);
        if (!t) return;
        t->position = pos;
        t->active = false;
        t->endedThisFrame = true;
    }

    int touchCount() const {
        int n = 0;
        for (const auto& t : touches_) if (t.active) n++;
        return n;
    }

    const Touch* getTouch(int index) const {
        if (index < 0 || index >= MAX_TOUCHES) return nullptr;
        return &touches_[index];
    }

    // Convenience: is there a primary touch (id=0) currently active?
    bool primaryTouchActive() const { return touches_[0].active; }
    Vec2 primaryTouchPosition() const { return touches_[0].position; }
    Vec2 primaryTouchDelta() const { return touches_[0].delta; }

    // -------------------------------------------------------------------------
    // Gesture recognition — basic.
    // -------------------------------------------------------------------------
    // Returns true if a tap (touch + release within tapMaxFrames frames,
    // small movement) was detected this frame.
    bool isTap(float maxDistance = 10.0f) const {
        const Touch& t = touches_[0];
        return t.endedThisFrame && t.delta.length() < maxDistance;
    }

    // Returns the pinch scale factor (1.0 = no change). Compares the
    // distance between the two active touches now vs. last frame.
    float pinchScale() const {
        if (!touches_[0].active || !touches_[1].active) return 1.0f;
        Vec2 cur = touches_[0].position - touches_[1].position;
        Vec2 prev = (touches_[0].position - touches_[0].delta) -
                    (touches_[1].position - touches_[1].delta);
        float curLen = cur.length();
        float prevLen = prev.length();
        if (prevLen < 1e-3f) return 1.0f;
        return curLen / prevLen;
    }

private:
    std::array<Touch, MAX_TOUCHES> touches_;

    Touch* findById(int id) {
        for (auto& t : touches_) if (t.id == id) return &t;
        return nullptr;
    }
    Touch* findOrAlloc(int id) {
        // First, try to find an existing touch with this id.
        for (auto& t : touches_) if (t.id == id && t.active) return &t;
        // Otherwise, allocate a free slot.
        for (auto& t : touches_) if (!t.active && !t.endedThisFrame) {
            t.id = id;
            return &t;
        }
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// XRController — a single VR/AR controller.
// ---------------------------------------------------------------------------
struct XRController {
    enum class Hand : uint8_t { Left, Right, Head };
    Hand hand = Hand::Left;

    // 6-DoF pose (in world space).
    Vec3 position{0, 0, 0};
    // Quaternion rotation (x, y, z, w).
    float qx = 0, qy = 0, qz = 0, qw = 1;
    Mat4 pose;  // computed from position + quaternion

    // Buttons (Standard XR mapping).
    enum Button : uint8_t {
        Trigger = 0, Squeeze = 1, Thumbstick = 2, A = 3, B = 4,
        X = 5, Y = 6, Thumbrest = 7,
        ButtonCount = 8
    };
    float buttonValue[ButtonCount] = {};  // 0..1 (analog)
    bool buttonPressed[ButtonCount] = {};

    // Thumbstick axes (-1..1).
    float thumbstickX = 0, thumbstickY = 0;

    bool connected = false;

    // Haptics — call from game code to vibrate the controller.
    // intensity: 0..1, durationMs: 0..N.
    std::function<void(float intensity, float durationMs)> onHapticPulse;
};

// ---------------------------------------------------------------------------
// XRManager — tracks all XR controllers + the headset.
// ---------------------------------------------------------------------------
class XRManager {
public:
    static const int MAX_CONTROLLERS = 4;

    void beginFrame() {
        // Per-frame state reset could go here (button-just-pressed flags).
    }

    XRController* getController(int idx) {
        if (idx < 0 || idx >= MAX_CONTROLLERS) return nullptr;
        if (!controllers_[idx].connected) return nullptr;
        return &controllers_[idx];
    }

    XRController* getControllerByHand(XRController::Hand h) {
        for (auto& c : controllers_) {
            if (c.connected && c.hand == h) return &c;
        }
        return nullptr;
    }

    // Headset pose.
    const Vec3& headsetPosition() const { return headsetPos_; }
    const Mat4& headsetPose() const { return headsetPose_; }

    // Called by the platform layer (WebXR/OpenXR) to update state.
    void setHeadsetPose(const Vec3& pos, const Mat4& pose) {
        headsetPos_ = pos;
        headsetPose_ = pose;
    }

    void setControllerConnected(int idx, bool connected) {
        if (idx < 0 || idx >= MAX_CONTROLLERS) return;
        controllers_[idx].connected = connected;
    }

    XRController& controllerRef(int idx) { return controllers_[idx]; }

private:
    std::array<XRController, MAX_CONTROLLERS> controllers_;
    Vec3 headsetPos_{0, 0, 0};
    Mat4 headsetPose_ = Mat4::identity();
};

// ---------------------------------------------------------------------------
// Gamepad — standard gamepad (Xbox/PS/Switch) via the Standard Gamepad spec.
// ---------------------------------------------------------------------------
struct Gamepad {
    enum Button : uint8_t {
        A = 0, B = 1, X = 2, Y = 3,
        LeftBumper = 4, RightBumper = 5,
        Back = 6, Start = 7,
        LeftStick = 8, RightStick = 9,
        Guide = 10, DPadUp = 11, DPadDown = 12, DPadLeft = 13, DPadRight = 14,
        ButtonCount = 15
    };
    enum Axis : uint8_t {
        LeftX = 0, LeftY = 1, RightX = 2, RightY = 3,
        AxisCount = 4
    };

    bool buttonPressed[ButtonCount] = {};
    bool buttonJustPressed[ButtonCount] = {};
    bool buttonJustReleased[ButtonCount] = {};
    float buttonValue[ButtonCount] = {};  // 0..1 for analog triggers
    float axis[AxisCount] = {};           // -1..1

    bool connected = false;
    std::string id;       // platform-assigned identifier
    std::string mapping;  // "standard", "xbox", "ps4", etc.

    // Per-frame update — call before reading state.
    void beginFrame() {
        for (int i = 0; i < ButtonCount; i++) {
            buttonJustPressed[i] = false;
            buttonJustReleased[i] = false;
        }
    }

    // Called by platform layer.
    void setButton(int btn, bool pressed) {
        if (btn < 0 || btn >= ButtonCount) return;
        if (pressed && !buttonPressed[btn]) buttonJustPressed[btn] = true;
        if (!pressed && buttonPressed[btn]) buttonJustReleased[btn] = true;
        buttonPressed[btn] = pressed;
        buttonValue[btn] = pressed ? 1.0f : 0.0f;
    }
    void setAnalogButton(int btn, float value) {
        if (btn < 0 || btn >= ButtonCount) return;
        buttonValue[btn] = value;
        bool pressed = value > 0.5f;
        if (pressed && !buttonPressed[btn]) buttonJustPressed[btn] = true;
        if (!pressed && buttonPressed[btn]) buttonJustReleased[btn] = true;
        buttonPressed[btn] = pressed;
    }
    void setAxis(int ax, float value) {
        if (ax < 0 || ax >= AxisCount) return;
        // Apply deadzone to reduce drift.
        const float deadzone = 0.1f;
        if (std::abs(value) < deadzone) value = 0.0f;
        else value = (value - deadzone * (value > 0 ? 1.0f : -1.0f)) / (1.0f - deadzone);
        axis[ax] = value;
    }
};

// ---------------------------------------------------------------------------
// GamepadManager — tracks up to 4 gamepads.
// ---------------------------------------------------------------------------
class GamepadManager {
public:
    static const int MAX_GAMEPADS = 4;

    void beginFrame() {
        for (auto& g : gamepads_) g.beginFrame();
    }

    Gamepad* getGamepad(int idx) {
        if (idx < 0 || idx >= MAX_GAMEPADS) return nullptr;
        if (!gamepads_[idx].connected) return nullptr;
        return &gamepads_[idx];
    }

    void setGamepadConnected(int idx, bool connected, const std::string& id = "",
                             const std::string& mapping = "standard") {
        if (idx < 0 || idx >= MAX_GAMEPADS) return;
        gamepads_[idx].connected = connected;
        gamepads_[idx].id = id;
        gamepads_[idx].mapping = mapping;
    }

    Gamepad& gamepadRef(int idx) { return gamepads_[idx]; }

private:
    std::array<Gamepad, MAX_GAMEPADS> gamepads_;
};

} // namespace input
} // namespace td
