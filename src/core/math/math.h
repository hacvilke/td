#pragma once
#include <cmath>

namespace td {

constexpr float TD_PI        = 3.14159265358979323846f;
constexpr float TD_TAU       = 6.28318530717958647692f;
constexpr float TD_DEG2RAD   = 0.01745329251994329577f;
constexpr float TD_RAD2DEG   = 57.2957795130823208768f;
constexpr float TD_EPSILON   = 1e-6f;

inline float degToRad(float d) { return d * TD_DEG2RAD; }
inline float radToDeg(float r) { return r * TD_RAD2DEG; }

inline float clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline int clampInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

inline float maxF(float a, float b) { return a > b ? a : b; }
inline float minF(float a, float b) { return a < b ? a : b; }
inline float absF(float v) { return v < 0 ? -v : v; }

inline float inverseLerp(float a, float b, float value) {
    if (absF(b - a) < TD_EPSILON) return 0.0f;
    return (value - a) / (b - a);
}

inline float remap(float value, float fromLo, float fromHi, float toLo, float toHi) {
    float t = inverseLerp(fromLo, fromHi, value);
    return lerp(toLo, toHi, t);
}

inline float signF(float v) {
    if (v > 0) return 1.0f;
    if (v < 0) return -1.0f;
    return 0.0f;
}

inline float sqrtF(float v) { return sqrtf(v); }
inline float sinF(float v) { return sinf(v); }
inline float cosF(float v) { return cosf(v); }
inline float tanF(float v) { return tanf(v); }
inline float asinF(float v) { return asinf(v); }
inline float acosF(float v) { return acosf(v); }
inline float atan2F(float y, float x) { return atan2f(y, x); }
inline float powF(float base, float exp) { return powf(base, exp); }
inline float floorF(float v) { return floorf(v); }
inline float ceilF(float v) { return ceilf(v); }
inline float roundF(float v) { return roundf(v); }
inline float fmodF(float x, float y) { return fmodf(x, y); }

inline bool nearZero(float v) { return absF(v) < TD_EPSILON; }
inline bool nearEqual(float a, float b) { return absF(a - b) < TD_EPSILON; }

// Smooth step interpolation
inline float smoothStep(float edge0, float edge1, float x) {
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Smoother step (Ken Perlin's improved version)
inline float smootherStep(float edge0, float edge1, float x) {
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Move towards target with max delta
inline float moveTowards(float current, float target, float maxDelta) {
    if (absF(target - current) <= maxDelta) {
        return target;
    }
    return current + signF(target - current) * maxDelta;
}

// Wrap angle to [-PI, PI]
inline float wrapAngle(float angle) {
    while (angle > TD_PI) angle -= TD_TAU;
    while (angle < -TD_PI) angle += TD_TAU;
    return angle;
}

// Delta angle between two angles
inline float deltaAngle(float current, float target) {
    float delta = fmodF(target - current, TD_TAU);
    if (delta > TD_PI) delta -= TD_TAU;
    if (delta < -TD_PI) delta += TD_TAU;
    return delta;
}

// Lerp angle properly handling wrap-around
inline float lerpAngle(float a, float b, float t) {
    float delta = deltaAngle(a, b);
    return a + delta * t;
}

} // namespace td
