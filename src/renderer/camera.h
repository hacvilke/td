#pragma once
#include "../core/math/vec2.h"
#include "../core/math/vec3.h"
#include "../core/math/mat4.h"

namespace td {

class Camera2D {
public:
    void setViewport(int width, int height);
    void setPosition(float x, float y);
    void setPosition(const Vec2& pos);
    void setZoom(float zoom);
    void setRotation(float radians);
    
    Mat4 getProjection() const;
    Mat4 getView() const;
    Mat4 getProjectionView() const;
    
    void move(float dx, float dy);
    void zoomBy(float amount);
    
    Vec2 screenToWorld(float screenX, float screenY) const;
    Vec2 worldToScreen(float worldX, float worldY) const;
    
    Vec2 getPosition() const { return m_position; }
    float getZoom() const { return m_zoom; }
    float getRotation() const { return m_rotation; }
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    
private:
    Vec2 m_position;
    float m_zoom = 1.0f;
    float m_rotation = 0.0f;
    int m_width = 800;
    int m_height = 600;
    float m_near = -1000.0f;
    float m_far = 1000.0f;
};

class Camera3D {
public:
    void setViewport(int width, int height);
    void setPosition(const Vec3& pos);
    void setTarget(const Vec3& target);
    void setFOV(float fovDeg);
    void setNearFar(float near, float far);
    
    Mat4 getProjection() const;
    Mat4 getView() const;
    Mat4 getViewProjection() const;
    
    // Camera movement
    void orbit(float deltaYaw, float deltaPitch);
    void pan(float dx, float dy);
    void zoom(float amount);
    void moveForward(float amount);
    void moveRight(float amount);
    void moveUp(float amount);
    
    // Direction vectors
    Vec3 getForward() const;
    Vec3 getRight() const;
    Vec3 getUp() const;
    
    Vec3 getPosition() const { return m_position; }
    Vec3 getTarget() const { return m_target; }
    float getFOV() const { return m_fov; }
    float getAspect() const { return (float)m_width / (float)m_height; }
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    
    // Ray from screen position
    Vec3 screenToWorldRay(float screenX, float screenY) const;
    
private:
    void updateOrbitAngles();
    void updatePositionFromOrbit();
    
    Vec3 m_position = {0, 2, 5};
    Vec3 m_target = {0, 0, 0};
    Vec3 m_up = {0, 1, 0};
    
    float m_fov = 60.0f;      // degrees
    float m_near = 0.1f;
    float m_far = 1000.0f;
    int m_width = 800;
    int m_height = 600;
    
    // For orbit camera
    float m_yaw = 0.0f;       // radians
    float m_pitch = 0.0f;     // radians
    float m_distance = 5.0f;
};

} // namespace td
