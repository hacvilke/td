#pragma once
#include "math.h"

namespace td {

struct Vec2 {
    float x, y;
    
    Vec2() : x(0), y(0) {}
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    explicit Vec2(float v) : x(v), y(v) {}
    
    // Operators
    Vec2 operator+(const Vec2& v) const { return Vec2(x + v.x, y + v.y); }
    Vec2 operator-(const Vec2& v) const { return Vec2(x - v.x, y - v.y); }
    Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
    Vec2 operator/(float s) const { float inv = 1.0f / s; return Vec2(x * inv, y * inv); }
    Vec2 operator*(const Vec2& v) const { return Vec2(x * v.x, y * v.y); }
    Vec2 operator/(const Vec2& v) const { return Vec2(x / v.x, y / v.y); }
    Vec2 operator-() const { return Vec2(-x, -y); }
    
    Vec2& operator+=(const Vec2& v) { x += v.x; y += v.y; return *this; }
    Vec2& operator-=(const Vec2& v) { x -= v.x; y -= v.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    Vec2& operator/=(float s) { float inv = 1.0f / s; x *= inv; y *= inv; return *this; }
    Vec2& operator*=(const Vec2& v) { x *= v.x; y *= v.y; return *this; }
    
    bool operator==(const Vec2& v) const { return nearEqual(x, v.x) && nearEqual(y, v.y); }
    bool operator!=(const Vec2& v) const { return !(*this == v); }
    
    float& operator[](int i) { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }
    
    // Methods
    float dot(const Vec2& v) const { return x * v.x + y * v.y; }
    
    // 2D cross product (returns scalar)
    float cross(const Vec2& v) const { return x * v.y - y * v.x; }
    
    float lengthSq() const { return x * x + y * y; }
    float length() const { return sqrtF(lengthSq()); }
    
    Vec2 normalized() const {
        float len = length();
        if (len < TD_EPSILON) return Vec2(0, 0);
        return *this / len;
    }
    
    void normalize() {
        float len = length();
        if (len < TD_EPSILON) {
            x = y = 0;
        } else {
            x /= len;
            y /= len;
        }
    }
    
    Vec2 perpendicular() const { return Vec2(-y, x); }
    Vec2 perpendicularCW() const { return Vec2(y, -x); }
    
    float angle() const { return atan2F(y, x); }
    
    Vec2 rotated(float radians) const {
        float c = cosF(radians);
        float s = sinF(radians);
        return Vec2(x * c - y * s, x * s + y * c);
    }
    
    Vec2 reflect(const Vec2& normal) const {
        return *this - normal * (2.0f * dot(normal));
    }
    
    float distanceTo(const Vec2& v) const {
        return (*this - v).length();
    }
    
    float distanceToSq(const Vec2& v) const {
        return (*this - v).lengthSq();
    }
    
    Vec2 lerp(const Vec2& target, float t) const {
        return Vec2(
            td::lerp(x, target.x, t),
            td::lerp(y, target.y, t)
        );
    }
    
    Vec2 moveTowards(const Vec2& target, float maxDelta) const {
        Vec2 diff = target - *this;
        float dist = diff.length();
        if (dist <= maxDelta || dist < TD_EPSILON) {
            return target;
        }
        return *this + diff / dist * maxDelta;
    }
    
    void setZero() { x = y = 0; }
    bool isZero() const { return lengthSq() < TD_EPSILON; }
    
    // Static helpers
    static Vec2 zero() { return Vec2(0, 0); }
    static Vec2 one() { return Vec2(1, 1); }
    static Vec2 up() { return Vec2(0, 1); }
    static Vec2 down() { return Vec2(0, -1); }
    static Vec2 left() { return Vec2(-1, 0); }
    static Vec2 right() { return Vec2(1, 0); }
    
    static Vec2 fromAngle(float radians) {
        return Vec2(cosF(radians), sinF(radians));
    }
    
    static float dot(const Vec2& a, const Vec2& b) {
        return a.dot(b);
    }
    
    static float distance(const Vec2& a, const Vec2& b) {
        return (b - a).length();
    }
    
    static Vec2 min(const Vec2& a, const Vec2& b) {
        return Vec2(minF(a.x, b.x), minF(a.y, b.y));
    }
    
    static Vec2 max(const Vec2& a, const Vec2& b) {
        return Vec2(maxF(a.x, b.x), maxF(a.y, b.y));
    }
    
    static Vec2 clamp(const Vec2& v, const Vec2& lo, const Vec2& hi) {
        return Vec2(
            td::clamp(v.x, lo.x, hi.x),
            td::clamp(v.y, lo.y, hi.y)
        );
    }
};

inline Vec2 operator*(float s, const Vec2& v) {
    return Vec2(v.x * s, v.y * s);
}

} // namespace td
