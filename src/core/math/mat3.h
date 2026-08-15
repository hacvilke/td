// =============================================================================
// TD Engine - 3x3 Matrix (src/core/math/mat3.h)
//
// 3x3 row-major matrix used for:
//   - 3D inertia tensors (rigid body dynamics)
//   - Rotation-only transforms (basis of a 3D transform)
//   - Quaternion-to-matrix conversions
//
// Storage is row-major: m[row][col].  This matches the typical math-textbook
// convention and Bullet/Box2D.  Note this differs from Mat4 which is
// column-major for OpenGL — Mat3 is for math, not for the GPU.
//
// All operations are inline (header-only) — these are tiny enough that the
// function-call overhead would dominate.
// =============================================================================
#pragma once
#include "vec3.h"
#include "quat.h"
#include "math.h"

namespace td {

struct Mat3 {
    // m[row * 3 + col]
    float m[9];

    Mat3() {
        for (int i = 0; i < 9; i++) m[i] = 0.0f;
        m[0] = m[4] = m[8] = 1.0f;   // identity
    }

    Mat3(float m00, float m01, float m02,
         float m10, float m11, float m12,
         float m20, float m21, float m22) {
        m[0]=m00; m[1]=m01; m[2]=m02;
        m[3]=m10; m[4]=m11; m[5]=m12;
        m[6]=m20; m[7]=m21; m[8]=m22;
    }

    // Accessors: M(row, col)
    float& operator()(int row, int col) { return m[row * 3 + col]; }
    const float& operator()(int row, int col) const { return m[row * 3 + col]; }

    float& at(int row, int col) { return m[row * 3 + col]; }
    const float& at(int row, int col) const { return m[row * 3 + col]; }

    // ---- Matrix * Matrix ---------------------------------------------------
    Mat3 operator*(const Mat3& b) const {
        Mat3 r;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                float sum = 0.0f;
                for (int k = 0; k < 3; ++k) {
                    sum += (*this)(row, k) * b(k, col);
                }
                r(row, col) = sum;
            }
        }
        return r;
    }
    Mat3& operator*=(const Mat3& b) { *this = *this * b; return *this; }

    // ---- Matrix * Vector ---------------------------------------------------
    Vec3 operator*(const Vec3& v) const {
        return Vec3(
            m[0]*v.x + m[1]*v.y + m[2]*v.z,
            m[3]*v.x + m[4]*v.y + m[5]*v.z,
            m[6]*v.x + m[7]*v.y + m[8]*v.z
        );
    }

    // ---- Transpose / symmetric ---------------------------------------------
    Mat3 transposed() const {
        return Mat3(m[0], m[3], m[6],
                    m[1], m[4], m[7],
                    m[2], m[5], m[8]);
    }

    // Skew-symmetric matrix from a vector (a.k.a. cross-product matrix).
    // Given v, returns the matrix V such that V * u = v x u for any u.
    // Used in rigid body torque calculations: tau = I*alpha + omega x (I*omega).
    static Mat3 skew(const Vec3& v) {
        return Mat3(0.0f,  -v.z,   v.y,
                    v.z,   0.0f,  -v.x,
                    -v.y,   v.x,   0.0f);
    }

    // ---- Diagonal ----------------------------------------------------------
    static Mat3 diagonal(float x, float y, float z) {
        return Mat3(x, 0, 0,
                    0, y, 0,
                    0, 0, z);
    }
    static Mat3 diagonal(const Vec3& v) { return diagonal(v.x, v.y, v.z); }
    static Mat3 identity() { return Mat3(); }

    // ---- Determinant + inverse (for inertia tensor) ------------------------
    float determinant() const {
        return m[0] * (m[4]*m[8] - m[5]*m[7])
             - m[1] * (m[3]*m[8] - m[5]*m[6])
             + m[2] * (m[3]*m[7] - m[4]*m[6]);
    }

    Mat3 inverse() const {
        float det = determinant();
        if (absF(det) < TD_EPSILON) return identity();
        float invDet = 1.0f / det;
        Mat3 r;
        r(0,0) =  (m[4]*m[8] - m[5]*m[7]) * invDet;
        r(0,1) = -(m[1]*m[8] - m[2]*m[7]) * invDet;
        r(0,2) =  (m[1]*m[5] - m[2]*m[4]) * invDet;
        r(1,0) = -(m[3]*m[8] - m[5]*m[6]) * invDet;
        r(1,1) =  (m[0]*m[8] - m[2]*m[6]) * invDet;
        r(1,2) = -(m[0]*m[5] - m[2]*m[3]) * invDet;
        r(2,0) =  (m[3]*m[7] - m[4]*m[6]) * invDet;
        r(2,1) = -(m[0]*m[7] - m[1]*m[6]) * invDet;
        r(2,2) =  (m[0]*m[4] - m[1]*m[3]) * invDet;
        return r;
    }

    // ---- From quaternion (rotation-only) -----------------------------------
    // Standard formula.  Assumes q is normalized.
    static Mat3 fromQuat(const Quat& q) {
        float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

        return Mat3(
            1.0f - 2.0f*(yy + zz), 2.0f*(xy - wz),       2.0f*(xz + wy),
            2.0f*(xy + wz),        1.0f - 2.0f*(xx + zz),2.0f*(yz - wx),
            2.0f*(xz - wy),        2.0f*(yz + wx),       1.0f - 2.0f*(xx + yy)
        );
    }

    // ---- Outer product a * b^T (used by impulse solver) --------------------
    static Mat3 outerProduct(const Vec3& a, const Vec3& b) {
        return Mat3(
            a.x*b.x, a.x*b.y, a.x*b.z,
            a.y*b.x, a.y*b.y, a.y*b.z,
            a.z*b.x, a.z*b.y, a.z*b.z
        );
    }

    // ---- Scale a matrix by a scalar (used for inertia inverse) -------------
    Mat3 operator*(float s) const {
        Mat3 r;
        for (int i = 0; i < 9; i++) r.m[i] = m[i] * s;
        return r;
    }

    // ---- Add two matrices --------------------------------------------------
    Mat3 operator+(const Mat3& b) const {
        Mat3 r;
        for (int i = 0; i < 9; i++) r.m[i] = m[i] + b.m[i];
        return r;
    }
    Mat3 operator-(const Mat3& b) const {
        Mat3 r;
        for (int i = 0; i < 9; i++) r.m[i] = m[i] - b.m[i];
        return r;
    }
};

inline Mat3 operator*(float s, const Mat3& mat) { return mat * s; }

} // namespace td
