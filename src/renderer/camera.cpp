#include "camera.h"
#include "../core/math/math.h"

namespace td {

// ==================== Camera2D ====================

void Camera2D::setViewport(int width, int height) {
    m_width = width;
    m_height = height;
}

void Camera2D::setPosition(float x, float y) {
    m_position.x = x;
    m_position.y = y;
}

void Camera2D::setPosition(const Vec2& pos) {
    m_position = pos;
}

void Camera2D::setZoom(float zoom) {
    m_zoom = maxF(zoom, 0.01f);
}

void Camera2D::setRotation(float radians) {
    m_rotation = radians;
}

Mat4 Camera2D::getProjection() const {
    float halfW = (float)m_width * 0.5f / m_zoom;
    float halfH = (float)m_height * 0.5f / m_zoom;
    
    return Mat4::orthographic(-halfW, halfW, -halfH, halfH, m_near, m_far);
}

Mat4 Camera2D::getView() const {
    // View = Rotation * Translation
    Mat4 view = Mat4::rotateZ(-m_rotation);
    view = view * Mat4::translate(-m_position.x, -m_position.y, 0);
    return view;
}

Mat4 Camera2D::getProjectionView() const {
    return getProjection() * getView();
}

void Camera2D::move(float dx, float dy) {
    // Move in local space (rotated)
    float c = cosF(m_rotation);
    float s = sinF(m_rotation);
    m_position.x += dx * c - dy * s;
    m_position.y += dx * s + dy * c;
}

void Camera2D::zoomBy(float amount) {
    m_zoom *= (1.0f + amount);
    m_zoom = maxF(m_zoom, 0.01f);
}

Vec2 Camera2D::screenToWorld(float screenX, float screenY) const {
    // Convert screen coords (0,0 top-left) to normalized device coords
    float ndcX = (screenX / (float)m_width) * 2.0f - 1.0f;
    float ndcY = 1.0f - (screenY / (float)m_height) * 2.0f;
    
    // Apply inverse projection and view
    float halfW = (float)m_width * 0.5f / m_zoom;
    float halfH = (float)m_height * 0.5f / m_zoom;
    
    float worldX = ndcX * halfW;
    float worldY = ndcY * halfH;
    
    // Apply inverse rotation
    float c = cosF(m_rotation);
    float s = sinF(m_rotation);
    float rx = worldX * c + worldY * s;
    float ry = -worldX * s + worldY * c;
    
    // Add camera position
    return Vec2(rx + m_position.x, ry + m_position.y);
}

Vec2 Camera2D::worldToScreen(float worldX, float worldY) const {
    // Subtract camera position
    float x = worldX - m_position.x;
    float y = worldY - m_position.y;
    
    // Apply rotation
    float c = cosF(m_rotation);
    float s = sinF(m_rotation);
    float rx = x * c - y * s;
    float ry = x * s + y * c;
    
    // Apply projection
    float halfW = (float)m_width * 0.5f / m_zoom;
    float halfH = (float)m_height * 0.5f / m_zoom;
    
    float ndcX = rx / halfW;
    float ndcY = ry / halfH;
    
    // Convert to screen coords
    float screenX = (ndcX + 1.0f) * 0.5f * (float)m_width;
    float screenY = (1.0f - ndcY) * 0.5f * (float)m_height;
    
    return Vec2(screenX, screenY);
}

// ==================== Camera3D ====================

void Camera3D::setViewport(int width, int height) {
    m_width = width;
    m_height = height;
}

void Camera3D::setPosition(const Vec3& pos) {
    m_position = pos;
    updateOrbitAngles();
}

void Camera3D::setTarget(const Vec3& target) {
    m_target = target;
    updateOrbitAngles();
}

void Camera3D::setFOV(float fovDeg) {
    m_fov = clamp(fovDeg, 1.0f, 179.0f);
}

void Camera3D::setNearFar(float near, float far) {
    m_near = near;
    m_far = far;
}

void Camera3D::updateOrbitAngles() {
    Vec3 diff = m_position - m_target;
    m_distance = diff.length();
    
    if (m_distance > TD_EPSILON) {
        Vec3 dir = diff / m_distance;
        m_yaw = atan2F(dir.x, dir.z);
        m_pitch = asinF(clamp(dir.y, -1.0f, 1.0f));
    }
}

void Camera3D::updatePositionFromOrbit() {
    float cp = cosF(m_pitch);
    float sp = sinF(m_pitch);
    float cy = cosF(m_yaw);
    float sy = sinF(m_yaw);
    
    m_position = m_target + Vec3(
        m_distance * cp * sy,
        m_distance * sp,
        m_distance * cp * cy
    );
}

Mat4 Camera3D::getProjection() const {
    float aspect = (float)m_width / (float)m_height;
    return Mat4::perspective(degToRad(m_fov), aspect, m_near, m_far);
}

Mat4 Camera3D::getView() const {
    return Mat4::lookAt(m_position, m_target, m_up);
}

Mat4 Camera3D::getViewProjection() const {
    return getProjection() * getView();
}

Vec3 Camera3D::getForward() const {
    return (m_target - m_position).normalized();
}

Vec3 Camera3D::getRight() const {
    return getForward().cross(m_up).normalized();
}

Vec3 Camera3D::getUp() const {
    return getRight().cross(getForward());
}

void Camera3D::orbit(float deltaYaw, float deltaPitch) {
    m_yaw += deltaYaw;
    m_pitch += deltaPitch;
    
    // Clamp pitch to avoid flipping
    m_pitch = clamp(m_pitch, -TD_PI * 0.49f, TD_PI * 0.49f);
    
    updatePositionFromOrbit();
}

void Camera3D::pan(float dx, float dy) {
    Vec3 right = getRight();
    Vec3 up = getUp();
    
    Vec3 offset = right * (-dx) + up * dy;
    m_position = m_position + offset;
    m_target = m_target + offset;
}

void Camera3D::zoom(float amount) {
    m_distance = maxF(0.1f, m_distance - amount);
    updatePositionFromOrbit();
}

void Camera3D::moveForward(float amount) {
    Vec3 forward = getForward();
    m_position = m_position + forward * amount;
    m_target = m_target + forward * amount;
}

void Camera3D::moveRight(float amount) {
    Vec3 right = getRight();
    m_position = m_position + right * amount;
    m_target = m_target + right * amount;
}

void Camera3D::moveUp(float amount) {
    m_position.y += amount;
    m_target.y += amount;
}

Vec3 Camera3D::screenToWorldRay(float screenX, float screenY) const {
    // Convert to NDC
    float ndcX = (screenX / (float)m_width) * 2.0f - 1.0f;
    float ndcY = 1.0f - (screenY / (float)m_height) * 2.0f;
    
    // Get inverse matrices
    Mat4 invProj = getProjection().inverse();
    Mat4 invView = getView().inverse();
    
    // Near and far points in NDC
    Vec4 nearPoint(ndcX, ndcY, -1.0f, 1.0f);
    Vec4 farPoint(ndcX, ndcY, 1.0f, 1.0f);
    
    // Transform to view space
    Vec4 nearView = invProj * nearPoint;
    Vec4 farView = invProj * farPoint;
    nearView = nearView / nearView.w;
    farView = farView / farView.w;
    
    // Transform to world space
    Vec4 nearWorld = invView * nearView;
    Vec4 farWorld = invView * farView;
    
    // Ray direction
    Vec3 rayDir = (farWorld.xyz() - nearWorld.xyz()).normalized();
    
    return rayDir;
}

} // namespace td
