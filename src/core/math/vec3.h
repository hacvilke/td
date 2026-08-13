#pragma once
#include "math.h"

namespace td {

struct Vec3 {
    float x, y, z;
    
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit Vec3(float v) : x(v), y(v), z(v) {}
    
    // Operators
    Vec3 operator+(const Vec3& v) const { return Vec3(x + v.x, y + v.y, z + v.z); }
    Vec3 operator-(const Vec3& v) const { return Vec3(x - v.x, y - v.y, z - v.z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator/(float s) const { float inv = 1.0f / s; return Vec3(x * inv, y * inv, z * inv); }
    Vec3 operator*(const Vec3& v) const { return Vec3(x * v.x, y * v.y, z * v.z); }
    Vec3 operator/(const Vec3& v) const { return Vec3(x / v.x, y / v.y, z / v.z); }
    Vec3 operator-() const { return Vec3(-x, -y, -z); }
    
    Vec3& operator+=(const Vec3& v) { x += v.x; y += v.y; z += v.z; return *this; }
    Vec3& operator-=(const Vec3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator/=(float s) { float inv = 1.0f / s; x *= inv; y *= inv; z *= inv; return *this; }
    Vec3& operator*=(const Vec3& v) { x *= v.x; y *= v.y; z *= v.z; return *this; }
    
    bool operator==(const Vec3& v) const { 
        return nearEqual(x, v.x) && nearEqual(y, v.y) && nearEqual(z, v.z); 
    }
    bool operator!=(const Vec3& v) const { return !(*this == v); }
    
    float& operator[](int i) { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }
    
    // Methods
    float dot(const Vec3& v) const { return x * v.x + y * v.y + z * v.z; }
    
    Vec3 cross(const Vec3& v) const {
        return Vec3(
            y * v.z - z * v.y,
            z * v.x - x * v.z,
            x * v.y - y * v.x
        );
    }
    
    float lengthSq() const { return x * x + y * y + z * z; }
    float length() const { return sqrtF(lengthSq()); }
    
    Vec3 normalized() const {
        float len = length();
        if (len < TD_EPSILON) return Vec3(0, 0, 0);
        return *this / len;
    }
    
    void normalize() {
        float len = length();
        if (len < TD_EPSILON) {
            x = y = z = 0;
        } else {
            x /= len;
            y /= len;
            z /= len;
        }
    }
    
    Vec3 reflect(const Vec3& normal) const {
        return *this - normal * (2.0f * dot(normal));
    }
    
    Vec3 project(const Vec3& onto) const {
        float d = onto.dot(onto);
        if (d < TD_EPSILON) return Vec3(0, 0, 0);
        return onto * (dot(onto) / d);
    }
    
    float distanceTo(const Vec3& v) const {
        return (*this - v).length();
    }
    
    float distanceToSq(const Vec3& v) const {
        return (*this - v).lengthSq();
    }
    
    Vec3 lerp(const Vec3& target, float t) const {
        return Vec3(
            td::lerp(x, target.x, t),
            td::lerp(y, target.y, t),
            td::lerp(z, target.z, t)
        );
    }
    
    Vec3 moveTowards(const Vec3& target, float maxDelta) const {
        Vec3 diff = target - *this;
        float dist = diff.length();
        if (dist <= maxDelta || dist < TD_EPSILON) {
            return target;
        }
        return *this + diff / dist * maxDelta;
    }
    
    void setZero() { x = y = z = 0; }
    bool isZero() const { return lengthSq() < TD_EPSILON; }
    
    // XY, XZ, YZ projections
    struct Vec2;
    
    // Static helpers
    static Vec3 zero() { return Vec3(0, 0, 0); }
    static Vec3 one() { return Vec3(1, 1, 1); }
    static Vec3 up() { return Vec3(0, 1, 0); }
    static Vec3 down() { return Vec3(0, -1, 0); }
    static Vec3 left() { return Vec3(-1, 0, 0); }
    static Vec3 right() { return Vec3(1, 0, 0); }
    static Vec3 forward() { return Vec3(0, 0, 1); }
    static Vec3 back() { return Vec3(0, 0, -1); }
    
    static float dot(const Vec3& a, const Vec3& b) {
        return a.dot(b);
    }
    
    static Vec3 cross(const Vec3& a, const Vec3& b) {
        return a.cross(b);
    }
    
    static float distance(const Vec3& a, const Vec3& b) {
        return (b - a).length();
    }
    
    static Vec3 min(const Vec3& a, const Vec3& b) {
        return Vec3(minF(a.x, b.x), minF(a.y, b.y), minF(a.z, b.z));
    }
    
    static Vec3 max(const Vec3& a, const Vec3& b) {
        return Vec3(maxF(a.x, b.x), maxF(a.y, b.y), maxF(a.z, b.z));
    }
    
    static Vec3 clamp(const Vec3& v, const Vec3& lo, const Vec3& hi) {
        return Vec3(
            td::clamp(v.x, lo.x, hi.x),
            td::clamp(v.y, lo.y, hi.y),
            td::clamp(v.z, lo.z, hi.z)
        );
    }
    
    // Get angle between two vectors in radians
    static float angle(const Vec3& from, const Vec3& to) {
        float denom = sqrtF(from.lengthSq() * to.lengthSq());
        if (denom < TD_EPSILON) return 0.0f;
        float d = td::clamp(from.dot(to) / denom, -1.0f, 1.0f);
        return acosF(d);
    }
};

inline Vec3 operator*(float s, const Vec3& v) {
    return Vec3(v.x * s, v.y * s, v.z * s);
}

// Vec4 for homogeneous coordinates
struct Vec4 {
    float x, y, z, w;
    
    Vec4() : x(0), y(0), z(0), w(0) {}
    Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    explicit Vec4(float v) : x(v), y(v), z(v), w(v) {}
    
    Vec4 operator+(const Vec4& v) const { return Vec4(x + v.x, y + v.y, z + v.z, w + v.w); }
    Vec4 operator-(const Vec4& v) const { return Vec4(x - v.x, y - v.y, z - v.z, w - v.w); }
    Vec4 operator*(float s) const { return Vec4(x * s, y * s, z * s, w * s); }
    Vec4 operator/(float s) const { float inv = 1.0f / s; return Vec4(x * inv, y * inv, z * inv, w * inv); }
    
    float& operator[](int i) { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }
    
    float dot(const Vec4& v) const { return x * v.x + y * v.y + z * v.z + w * v.w; }
    
    Vec3 xyz() const { return Vec3(x, y, z); }
    
    // Perspective divide
    Vec3 perspectiveDivide() const {
        if (absF(w) < TD_EPSILON) return Vec3(x, y, z);
        return Vec3(x / w, y / w, z / w);
    }
};

} // namespace td
