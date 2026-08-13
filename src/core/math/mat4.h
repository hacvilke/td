#pragma once
#include "vec3.h"
#include "vec2.h"

namespace td {

// Column-major 4x4 matrix for OpenGL
// m[col][row] or m[col * 4 + row]
struct Mat4 {
    float m[16];
    
    Mat4() {
        for (int i = 0; i < 16; i++) m[i] = 0;
    }
    
    Mat4(float diagonal) {
        for (int i = 0; i < 16; i++) m[i] = 0;
        m[0] = m[5] = m[10] = m[15] = diagonal;
    }
    
    float& operator()(int row, int col) { return m[col * 4 + row]; }
    const float& operator()(int row, int col) const { return m[col * 4 + row]; }
    
    float* data() { return m; }
    const float* data() const { return m; }
    
    // Matrix multiplication
    Mat4 operator*(const Mat4& other) const {
        Mat4 result;
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                float sum = 0;
                for (int k = 0; k < 4; k++) {
                    sum += (*this)(row, k) * other(k, col);
                }
                result(row, col) = sum;
            }
        }
        return result;
    }
    
    Mat4& operator*=(const Mat4& other) {
        *this = *this * other;
        return *this;
    }
    
    // Transform Vec4
    Vec4 operator*(const Vec4& v) const {
        return Vec4(
            m[0] * v.x + m[4] * v.y + m[8]  * v.z + m[12] * v.w,
            m[1] * v.x + m[5] * v.y + m[9]  * v.z + m[13] * v.w,
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
            m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w
        );
    }
    
    // Transform Vec3 as point (w=1)
    Vec3 transformPoint(const Vec3& v) const {
        Vec4 result = *this * Vec4(v, 1.0f);
        return result.perspectiveDivide();
    }
    
    // Transform Vec3 as direction (w=0)
    Vec3 transformDirection(const Vec3& v) const {
        return Vec3(
            m[0] * v.x + m[4] * v.y + m[8]  * v.z,
            m[1] * v.x + m[5] * v.y + m[9]  * v.z,
            m[2] * v.x + m[6] * v.y + m[10] * v.z
        );
    }
    
    // Static factory methods
    static Mat4 identity() {
        return Mat4(1.0f);
    }
    
    static Mat4 orthographic(float left, float right, float bottom, float top, 
                              float near, float far) {
        Mat4 result;
        
        float rl = right - left;
        float tb = top - bottom;
        float fn = far - near;
        
        result.m[0]  = 2.0f / rl;
        result.m[5]  = 2.0f / tb;
        result.m[10] = -2.0f / fn;
        result.m[12] = -(right + left) / rl;
        result.m[13] = -(top + bottom) / tb;
        result.m[14] = -(far + near) / fn;
        result.m[15] = 1.0f;
        
        return result;
    }
    
    static Mat4 perspective(float fovRadians, float aspect, float near, float far) {
        Mat4 result;
        
        float tanHalfFov = tanF(fovRadians * 0.5f);
        float fn = far - near;
        
        result.m[0]  = 1.0f / (aspect * tanHalfFov);
        result.m[5]  = 1.0f / tanHalfFov;
        result.m[10] = -(far + near) / fn;
        result.m[11] = -1.0f;
        result.m[14] = -(2.0f * far * near) / fn;
        
        return result;
    }
    
    static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& worldUp) {
        Vec3 forward = (target - eye).normalized();
        Vec3 right = forward.cross(worldUp).normalized();
        Vec3 up = right.cross(forward);
        
        Mat4 result;
        
        result.m[0]  = right.x;
        result.m[1]  = up.x;
        result.m[2]  = -forward.x;
        result.m[3]  = 0;
        
        result.m[4]  = right.y;
        result.m[5]  = up.y;
        result.m[6]  = -forward.y;
        result.m[7]  = 0;
        
        result.m[8]  = right.z;
        result.m[9]  = up.z;
        result.m[10] = -forward.z;
        result.m[11] = 0;
        
        result.m[12] = -right.dot(eye);
        result.m[13] = -up.dot(eye);
        result.m[14] = forward.dot(eye);
        result.m[15] = 1;
        
        return result;
    }
    
    static Mat4 translate(const Vec3& v) {
        Mat4 result = identity();
        result.m[12] = v.x;
        result.m[13] = v.y;
        result.m[14] = v.z;
        return result;
    }
    
    static Mat4 translate(float x, float y, float z) {
        return translate(Vec3(x, y, z));
    }
    
    static Mat4 scale(float sx, float sy, float sz) {
        Mat4 result;
        result.m[0]  = sx;
        result.m[5]  = sy;
        result.m[10] = sz;
        result.m[15] = 1.0f;
        return result;
    }
    
    static Mat4 scale(const Vec3& s) {
        return scale(s.x, s.y, s.z);
    }
    
    static Mat4 scale(float s) {
        return scale(s, s, s);
    }
    
    static Mat4 rotateX(float radians) {
        Mat4 result = identity();
        float c = cosF(radians);
        float s = sinF(radians);
        result.m[5]  = c;
        result.m[6]  = s;
        result.m[9]  = -s;
        result.m[10] = c;
        return result;
    }
    
    static Mat4 rotateY(float radians) {
        Mat4 result = identity();
        float c = cosF(radians);
        float s = sinF(radians);
        result.m[0]  = c;
        result.m[2]  = -s;
        result.m[8]  = s;
        result.m[10] = c;
        return result;
    }
    
    static Mat4 rotateZ(float radians) {
        Mat4 result = identity();
        float c = cosF(radians);
        float s = sinF(radians);
        result.m[0] = c;
        result.m[1] = s;
        result.m[4] = -s;
        result.m[5] = c;
        return result;
    }
    
    // Rotate around arbitrary axis
    static Mat4 rotate(const Vec3& axis, float radians) {
        Vec3 a = axis.normalized();
        float c = cosF(radians);
        float s = sinF(radians);
        float t = 1.0f - c;
        
        Mat4 result;
        
        result.m[0]  = t * a.x * a.x + c;
        result.m[1]  = t * a.x * a.y + s * a.z;
        result.m[2]  = t * a.x * a.z - s * a.y;
        result.m[3]  = 0;
        
        result.m[4]  = t * a.x * a.y - s * a.z;
        result.m[5]  = t * a.y * a.y + c;
        result.m[6]  = t * a.y * a.z + s * a.x;
        result.m[7]  = 0;
        
        result.m[8]  = t * a.x * a.z + s * a.y;
        result.m[9]  = t * a.y * a.z - s * a.x;
        result.m[10] = t * a.z * a.z + c;
        result.m[11] = 0;
        
        result.m[12] = 0;
        result.m[13] = 0;
        result.m[14] = 0;
        result.m[15] = 1;
        
        return result;
    }
    
    // Euler angles rotation (YXZ order for typical FPS camera)
    static Mat4 rotateEuler(float pitch, float yaw, float roll) {
        return rotateY(yaw) * rotateX(pitch) * rotateZ(roll);
    }
    
    // Transpose
    Mat4 transposed() const {
        Mat4 result;
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                result(row, col) = (*this)(col, row);
            }
        }
        return result;
    }
    
    // Determinant (for 4x4 matrix)
    float determinant() const {
        float a = m[0], b = m[1], c = m[2], d = m[3];
        float e = m[4], f = m[5], g = m[6], h = m[7];
        float i = m[8], j = m[9], k = m[10], l = m[11];
        float mm = m[12], n = m[13], o = m[14], p = m[15];
        
        float kp_lo = k * p - l * o;
        float jp_ln = j * p - l * n;
        float jo_kn = j * o - k * n;
        float ip_lm = i * p - l * mm;
        float io_km = i * o - k * mm;
        float in_jm = i * n - j * mm;
        
        return a * (f * kp_lo - g * jp_ln + h * jo_kn)
             - b * (e * kp_lo - g * ip_lm + h * io_km)
             + c * (e * jp_ln - f * ip_lm + h * in_jm)
             - d * (e * jo_kn - f * io_km + g * in_jm);
    }
    
    // Inverse
    Mat4 inverse() const {
        float det = determinant();
        if (absF(det) < TD_EPSILON) {
            return identity();
        }
        
        Mat4 result;
        float invDet = 1.0f / det;
        
        float a = m[0], b = m[1], c = m[2], d = m[3];
        float e = m[4], f = m[5], g = m[6], h = m[7];
        float i = m[8], j = m[9], k = m[10], l = m[11];
        float mm = m[12], n = m[13], o = m[14], p = m[15];
        
        float kp_lo = k * p - l * o;
        float jp_ln = j * p - l * n;
        float jo_kn = j * o - k * n;
        float ip_lm = i * p - l * mm;
        float io_km = i * o - k * mm;
        float in_jm = i * n - j * mm;
        
        float gp_ho = g * p - h * o;
        float fp_hn = f * p - h * n;
        float fo_gn = f * o - g * n;
        float ep_hm = e * p - h * mm;
        float eo_gm = e * o - g * mm;
        float en_fm = e * n - f * mm;
        
        float gl_hk = g * l - h * k;
        float fl_hj = f * l - h * j;
        float fk_gj = f * k - g * j;
        float el_hi = e * l - h * i;
        float ek_gi = e * k - g * i;
        float ej_fi = e * j - f * i;
        
        result.m[0]  =  (f * kp_lo - g * jp_ln + h * jo_kn) * invDet;
        result.m[1]  = -(b * kp_lo - c * jp_ln + d * jo_kn) * invDet;
        result.m[2]  =  (b * gp_ho - c * fp_hn + d * fo_gn) * invDet;
        result.m[3]  = -(b * gl_hk - c * fl_hj + d * fk_gj) * invDet;
        
        result.m[4]  = -(e * kp_lo - g * ip_lm + h * io_km) * invDet;
        result.m[5]  =  (a * kp_lo - c * ip_lm + d * io_km) * invDet;
        result.m[6]  = -(a * gp_ho - c * ep_hm + d * eo_gm) * invDet;
        result.m[7]  =  (a * gl_hk - c * el_hi + d * ek_gi) * invDet;
        
        result.m[8]  =  (e * jp_ln - f * ip_lm + h * in_jm) * invDet;
        result.m[9]  = -(a * jp_ln - b * ip_lm + d * in_jm) * invDet;
        result.m[10] =  (a * fp_hn - b * ep_hm + d * en_fm) * invDet;
        result.m[11] = -(a * fl_hj - b * el_hi + d * ej_fi) * invDet;
        
        result.m[12] = -(e * jo_kn - f * io_km + g * in_jm) * invDet;
        result.m[13] =  (a * jo_kn - b * io_km + c * in_jm) * invDet;
        result.m[14] = -(a * fo_gn - b * eo_gm + c * en_fm) * invDet;
        result.m[15] =  (a * fk_gj - b * ek_gi + c * ej_fi) * invDet;
        
        return result;
    }
    
    // Get upper 3x3 as normal matrix (inverse transpose for normals)
    Mat4 normalMatrix() const {
        // For the upper 3x3, compute inverse transpose
        Mat4 inv = inverse();
        Mat4 result;
        
        // Transpose of the upper-left 3x3
        result.m[0] = inv.m[0];
        result.m[1] = inv.m[4];
        result.m[2] = inv.m[8];
        result.m[3] = 0;
        
        result.m[4] = inv.m[1];
        result.m[5] = inv.m[5];
        result.m[6] = inv.m[9];
        result.m[7] = 0;
        
        result.m[8]  = inv.m[2];
        result.m[9]  = inv.m[6];
        result.m[10] = inv.m[10];
        result.m[11] = 0;
        
        result.m[12] = 0;
        result.m[13] = 0;
        result.m[14] = 0;
        result.m[15] = 1;
        
        return result;
    }
    
    // Extract translation
    Vec3 getTranslation() const {
        return Vec3(m[12], m[13], m[14]);
    }
    
    // Extract scale (assuming no shearing)
    Vec3 getScale() const {
        Vec3 x(m[0], m[1], m[2]);
        Vec3 y(m[4], m[5], m[6]);
        Vec3 z(m[8], m[9], m[10]);
        return Vec3(x.length(), y.length(), z.length());
    }
    
    // TRS composition
    static Mat4 TRS(const Vec3& translation, const Vec3& rotationEuler, const Vec3& scaleVec) {
        return translate(translation) * 
               rotateEuler(rotationEuler.x, rotationEuler.y, rotationEuler.z) * 
               scale(scaleVec);
    }
};

} // namespace td
