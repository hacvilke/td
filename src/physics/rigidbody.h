#pragma once
#include "../core/math/vec2.h"

namespace td {

class RigidBody {
public:
    Vec2 position;
    Vec2 velocity;
    Vec2 acceleration;
    Vec2 force;
    
    float mass = 1.0f;
    float inverseMass = 1.0f;
    float friction = 0.3f;
    float restitution = 0.2f;  // Bounciness (0-1)
    float linearDamping = 0.01f;
    float gravityScale = 1.0f;
    
    bool useGravity = true;
    bool isStatic = false;
    bool isTrigger = false;     // No physics response, just collision events
    bool isKinematic = false;   // Affected by velocity, not forces
    
    // Angular motion
    float rotation = 0.0f;          // radians
    float angularVelocity = 0.0f;
    float torque = 0.0f;
    float inertia = 1.0f;
    float inverseInertia = 1.0f;
    float angularDamping = 0.01f;
    
    void applyForce(const Vec2& f);
    void applyForceAtPoint(const Vec2& f, const Vec2& point);
    void applyImpulse(const Vec2& impulse);
    void applyImpulseAtPoint(const Vec2& impulse, const Vec2& point);
    void applyTorque(float t);
    
    void integrate(float dt);
    void integrateVelocity(float dt);
    void integratePosition(float dt);
    
    void setMass(float m);
    void setInertia(float i);
    
    Vec2 getVelocityAtPoint(const Vec2& point) const;
    float getKineticEnergy() const;
    
    void clearForces();
};

// Gravity constant
constexpr float GRAVITY_ACCEL = 9.81f;
// GRAVITY_VEC defined as inline to avoid constexpr constructor issues
inline Vec2 gravityVec() { return Vec2(0.0f, -9.81f); }

} // namespace td
