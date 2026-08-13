#include "rigidbody.h"
#include "../core/math/math.h"

namespace td {

void RigidBody::applyForce(const Vec2& f) {
    if (isStatic || isKinematic) return;
    force = force + f;
}

void RigidBody::applyForceAtPoint(const Vec2& f, const Vec2& point) {
    if (isStatic || isKinematic) return;
    force = force + f;
    
    // Calculate torque from off-center force
    Vec2 r = point - position;
    torque += r.cross(f);
}

void RigidBody::applyImpulse(const Vec2& impulse) {
    if (isStatic) return;
    velocity = velocity + impulse * inverseMass;
}

void RigidBody::applyImpulseAtPoint(const Vec2& impulse, const Vec2& point) {
    if (isStatic) return;
    velocity = velocity + impulse * inverseMass;
    
    // Apply angular impulse
    Vec2 r = point - position;
    angularVelocity += r.cross(impulse) * inverseInertia;
}

void RigidBody::applyTorque(float t) {
    if (isStatic || isKinematic) return;
    torque += t;
}

void RigidBody::integrate(float dt) {
    if (isStatic) return;
    
    if (!isKinematic) {
        // Apply gravity
        if (useGravity) {
            force = force + Vec2(0, -GRAVITY_ACCEL * mass * gravityScale);
        }
        
        // Compute acceleration
        acceleration = force * inverseMass;
        
        // Semi-implicit Euler integration
        velocity = velocity + acceleration * dt;
        
        // Angular acceleration
        float angularAccel = torque * inverseInertia;
        angularVelocity += angularAccel * dt;
    }
    
    // Apply damping
    velocity = velocity * (1.0f - linearDamping);
    angularVelocity *= (1.0f - angularDamping);
    
    // Update position
    position = position + velocity * dt;
    rotation += angularVelocity * dt;
    
    // Wrap rotation to [-PI, PI]
    rotation = wrapAngle(rotation);
    
    // Clear forces for next frame
    clearForces();
}

void RigidBody::integrateVelocity(float dt) {
    if (isStatic) return;
    
    if (!isKinematic) {
        if (useGravity) {
            force = force + Vec2(0, -GRAVITY_ACCEL * mass * gravityScale);
        }
        
        acceleration = force * inverseMass;
        velocity = velocity + acceleration * dt;
        
        float angularAccel = torque * inverseInertia;
        angularVelocity += angularAccel * dt;
    }
    
    velocity = velocity * (1.0f - linearDamping);
    angularVelocity *= (1.0f - angularDamping);
}

void RigidBody::integratePosition(float dt) {
    if (isStatic) return;
    
    position = position + velocity * dt;
    rotation += angularVelocity * dt;
    rotation = wrapAngle(rotation);
    
    clearForces();
}

void RigidBody::setMass(float m) {
    if (m <= 0.0f) {
        mass = 0.0f;
        inverseMass = 0.0f;
        isStatic = true;
    } else {
        mass = m;
        inverseMass = 1.0f / m;
    }
}

void RigidBody::setInertia(float i) {
    if (i <= 0.0f) {
        inertia = 0.0f;
        inverseInertia = 0.0f;
    } else {
        inertia = i;
        inverseInertia = 1.0f / i;
    }
}

Vec2 RigidBody::getVelocityAtPoint(const Vec2& point) const {
    Vec2 r = point - position;
    // v = v_center + omega x r (in 2D: omega x r = (-omega * r.y, omega * r.x))
    return velocity + Vec2(-angularVelocity * r.y, angularVelocity * r.x);
}

float RigidBody::getKineticEnergy() const {
    float linearKE = 0.5f * mass * velocity.lengthSq();
    float angularKE = 0.5f * inertia * angularVelocity * angularVelocity;
    return linearKE + angularKE;
}

void RigidBody::clearForces() {
    force.setZero();
    acceleration.setZero();
    torque = 0.0f;
}

} // namespace td
