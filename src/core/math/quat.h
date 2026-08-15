// =============================================================================
// TD Engine - Quaternion (src/core/math/quat.h)
//
// Quaternion for 3D rigid body orientation. The 25-hour physics course
// (Physics Tutoring Hub) covers rigid body rotation in 3D — Euler angles
// suffer from gimbal lock and are expensive to interpolate, so every real
// physics engine (Bullet, Box2D v3, PhysX, Havok, ODE) uses quaternions
// for orientation storage and integration.
//
// Conventions (Unified Syntax Blueprint):
//   - Stored as (x, y, z, w) where w is the scalar (real) part.
//   - Unit quaternions represent rotations.
//   - Multiplication composes rotations: q1 * q2 means "rotate by q2 then q1"
//     (Hamilton product, same convention as GLM, Bullet, PhysX).
//   - Vec3 rotation: v' = q * v * q^-1 (optimized to avoid the conjugate
//     multiply via the standard 3x3 matrix conversion).
//
// This file is header-only because all operations are small and inline.
// =============================================================================
#pragma once
#include "vec3.h"
#include "math.h"

namespace td {

struct Quat {
    float x, y, z, w;

    Quat() : x(0), y(0), z(0), w(1) {}               // identity
    Quat(float x_, float y_, float z_, float w_)
        : x(x_), y(y_), z(z_), w(w_) {}

    // ---- Factory: from axis-angle (radians) --------------------------------
    static Quat fromAxisAngle(const Vec3& axis, float radians) {
        Vec3 a = axis.normalized();
        float half = radians * 0.5f;
        float s = sinF(half);
        return Quat(a.x * s, a.y * s, a.z * s, cosF(half));
    }

    // ---- Factory: from Euler angles (radians, YXZ order — same as Mat4) ----
    static Quat fromEuler(float pitch, float yaw, float roll) {
        float cy = cosF(yaw   * 0.5f), sy = sinF(yaw   * 0.5f);
        float cp = cosF(pitch * 0.5f), sp = sinF(pitch * 0.5f);
        float cr = cosF(roll  * 0.5f), sr = sinF(roll  * 0.5f);
        return Quat(
            cy * sp * cr + sy * cp * sr,
            sy * cp * cr - cy * sp * sr,
            cy * cp * sr - sy * sp * cr,
            cy * cp * cr + sy * sp * sr
        );
    }

    // ---- Factory: rotate one vector onto another --------------------------
    // Produces the shortest rotation that takes `from` to `to`. Both vectors
    // MUST be normalized; behavior is undefined if either is zero.
    static Quat fromToRotation(const Vec3& from, const Vec3& to) {
        float d = from.dot(to);
        if (d >= 1.0f - TD_EPSILON) return Quat();           // parallel
        if (d <= -1.0f + TD_EPSILON) {
            // Exactly opposite — pick any perpendicular axis
            Vec3 axis = (absF(from.y) < 0.99f ? Vec3(0, 1, 0) : Vec3(1, 0, 0)).cross(from).normalized();
            return fromAxisAngle(axis, TD_PI);
        }
        Vec3 axis = from.cross(to);
        float s = sqrtF((1.0f + d) * 2.0f);
        float inv = 1.0f / s;
        Quat q(axis.x * inv, axis.y * inv, axis.z * inv, s * 0.5f);
        return q.normalized();
    }

    // ---- Algebra -----------------------------------------------------------
    Quat operator+(const Quat& q) const { return Quat(x+q.x, y+q.y, z+q.z, w+q.w); }
    Quat operator-(const Quat& q) const { return Quat(x-q.x, y-q.y, z-q.z, w-q.w); }
    Quat operator*(float s)       const { return Quat(x*s, y*s, z*s, w*s); }
    Quat operator/(float s)       const { float inv = 1.0f/s; return Quat(x*inv, y*inv, z*inv, w*inv); }
    Quat operator-()              const { return Quat(-x, -y, -z, -w); }

    // Hamilton product.  q1 * q2 composes "rotate by q2 then q1".
    Quat operator*(const Quat& q) const {
        return Quat(
            w*q.x + x*q.w + y*q.z - z*q.y,
            w*q.y - x*q.z + y*q.w + z*q.x,
            w*q.z + x*q.y - y*q.x + z*q.w,
            w*q.w - x*q.x - y*q.y - z*q.z
        );
    }

    Quat& operator*=(const Quat& q) { *this = *this * q; return *this; }
    Quat& operator+=(const Quat& q) { x+=q.x; y+=q.y; z+=q.z; w+=q.w; return *this; }
    Quat& operator*=(float s)       { x*=s; y*=s; z*=s; w*=s; return *this; }

    // ---- Conjugate / inverse -----------------------------------------------
    Quat conjugate() const { return Quat(-x, -y, -z, w); }
    Quat inverse()   const {
        float n = normSq();
        if (n < TD_EPSILON) return Quat();
        return conjugate() / n;
    }

    // ---- Norms -------------------------------------------------------------
    float normSq() const { return x*x + y*y + z*z + w*w; }
    float norm()   const { return sqrtF(normSq()); }

    Quat normalized() const {
        float n = norm();
        if (n < TD_EPSILON) return Quat();
        return *this / n;
    }
    void normalize() { *this = normalized(); }

    bool isUnit(float eps = 1e-4f) const {
        return absF(normSq() - 1.0f) < eps;
    }

    // ---- Rotation of a vector by this quaternion ---------------------------
    // Uses the optimized form (avoiding the q*v*q^-1 conjugate multiply):
    //   v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
    Vec3 rotate(const Vec3& v) const {
        Vec3 u(x, y, z);
        Vec3 t = u.cross(v) * 2.0f;
        return v + t * w + u.cross(t);
    }

    // Inverse rotation
    Vec3 inverseRotate(const Vec3& v) const {
        return conjugate().rotate(v);
    }

    // ---- Axis / angle extraction -------------------------------------------
    Vec3 axis() const {
        float s2 = 1.0f - w * w;
        if (s2 < TD_EPSILON) return Vec3(1, 0, 0);
        float inv = 1.0f / sqrtF(s2);
        return Vec3(x * inv, y * inv, z * inv);
    }
    float angle() const {
        // Clamp w to [-1, 1] to avoid NaN from acos
        float aw = clamp(w, -1.0f, 1.0f);
        return 2.0f * acosF(aw);
    }

    // ---- Spherical linear interpolation ------------------------------------
    // Standard SLERP with the "shortest path" correction.  t in [0, 1].
    static Quat slerp(const Quat& a, const Quat& b, float t) {
        float cosTheta = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
        Quat bAdj = b;
        if (cosTheta < 0.0f) {            // take shortest path
            bAdj = -b;
            cosTheta = -cosTheta;
        }
        if (cosTheta > 1.0f - 1e-6f) {
            // Quaternions are nearly identical — fall back to lerp + normalize
            Quat r = Quat(
                a.x + (bAdj.x - a.x) * t,
                a.y + (bAdj.y - a.y) * t,
                a.z + (bAdj.z - a.z) * t,
                a.w + (bAdj.w - a.w) * t
            );
            return r.normalized();
        }
        float theta = acosF(cosTheta);
        float sinTheta = sinF(theta);
        float wa = sinF((1.0f - t) * theta) / sinTheta;
        float wb = sinF(t * theta) / sinTheta;
        return Quat(
            a.x*wa + bAdj.x*wb,
            a.y*wa + bAdj.y*wb,
            a.z*wa + bAdj.z*wb,
            a.w*wa + bAdj.w*wb
        );
    }

    // ---- Dot product -------------------------------------------------------
    static float dot(const Quat& a, const Quat& b) {
        return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    }

    // ---- Conversion to Mat4 (for renderer / model matrix) ------------------
    // Returns a 4x4 rotation matrix representing this quaternion.
    // Caller includes mat4.h if needed; we inline the math here.
    // We avoid the include cycle by emitting raw floats via a helper below.
    // See quat.cpp / inline users that call toMat4 via #include "mat4.h".

    // Identity
    static Quat identity() { return Quat(); }
};

inline Quat operator*(float s, const Quat& q) { return q * s; }

} // namespace td
