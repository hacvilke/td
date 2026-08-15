// =============================================================================
// TD Engine - 3D Rigid Body Implementation (src/physics/rigidbody3d.cpp)
// =============================================================================
#include "rigidbody3d.h"

namespace td {

// -----------------------------------------------------------------------------
// Mass + inertia
// -----------------------------------------------------------------------------
void RigidBody3D::setMass(float m) {
    if (m <= 0.0f || isStatic) {
        mass = 0.0f;
        inverseMass = 0.0f;
        isStatic = true;
    } else {
        mass = m;
        inverseMass = 1.0f / m;
    }
}

void RigidBody3D::setInertia(const Mat3& I) {
    inertia = I;
    inverseInertia = I.inverse();
}

// Solid sphere:  I = (2/5) * m * r^2  on the diagonal
void RigidBody3D::setInertiaSphere(float radius, float mass) {
    float I = 0.4f * mass * radius * radius;     // (2/5) m r^2
    setInertia(Mat3::diagonal(I, I, I));
}

// Solid box (full extents = 2*half):  Ixx = (1/12) m (h^2 + d^2), etc.
// halfExtents = (hx, hy, hz).  Full sizes = (2hx, 2hy, 2hz).
// Ixx = (1/12) m ((2hy)^2 + (2hz)^2) = (1/3) m (hy^2 + hz^2)
void RigidBody3D::setInertiaBox(const Vec3& halfExtents, float mass) {
    float hx = halfExtents.x, hy = halfExtents.y, hz = halfExtents.z;
    float Ixx = (1.0f / 3.0f) * mass * (hy*hy + hz*hz);
    float Iyy = (1.0f / 3.0f) * mass * (hx*hx + hz*hz);
    float Izz = (1.0f / 3.0f) * mass * (hx*hx + hy*hy);
    setInertia(Mat3::diagonal(Ixx, Iyy, Izz));
}

// Solid capsule approximated as a solid cylinder + two hemispheres.
// This is the standard closed-form approximation used by Bullet and ODE.
//   axis: 0 = X, 1 = Y (default), 2 = Z
//   height = total height of the cylindrical part (NOT including hemispheres)
//   radius = capsule radius
// The principal moments: along the axis = (1/2) m r^2 (cylinder) + (2/5) m_h r^2
// perpendicular = (1/12) m (3r^2 + h^2) + (2/5) m_h r^2 + m_h * (h/2)^2 (Steiner)
// To keep this manageable and predictable, we use the standard simplified
// form assuming uniform density and the mass being mostly the cylinder.
void RigidBody3D::setInertiaCapsule(float radius, float height, float mass, int axis) {
    // Approximate as a cylinder of radius r and total length (height + 2r)
    // — close enough for game purposes; exact capsule inertia is hairy.
    float r = radius;
    float L = height + 2.0f * r;          // total length
    float I_axis = 0.5f * mass * r * r;   // about the long axis
    float I_perp = (1.0f / 12.0f) * mass * (3.0f * r * r + L * L);
    if (axis == 0)      setInertia(Mat3::diagonal(I_axis, I_perp, I_perp));
    else if (axis == 1) setInertia(Mat3::diagonal(I_perp, I_axis, I_perp));
    else                setInertia(Mat3::diagonal(I_perp, I_perp, I_axis));
}

// -----------------------------------------------------------------------------
// Force / impulse application
// -----------------------------------------------------------------------------
void RigidBody3D::applyForce(const Vec3& f) {
    if (isStatic || isKinematic) return;
    wakeUp();
    force += f;
}

void RigidBody3D::applyForceAtPoint(const Vec3& f, const Vec3& worldPoint) {
    if (isStatic || isKinematic) return;
    wakeUp();
    force += f;
    // Torque = r x F   where r = worldPoint - centerOfMass
    Vec3 r = worldPoint - position;
    torque += r.cross(f);
}

void RigidBody3D::applyTorque(const Vec3& t) {
    if (isStatic || isKinematic) return;
    wakeUp();
    torque += t;
}

void RigidBody3D::applyImpulse(const Vec3& impulse) {
    if (isStatic) return;
    wakeUp();
    linearVelocity += impulse * inverseMass;
}

// Impulse at a point:  deltaV = J/m,  deltaW = I^-1 * (r x J)
void RigidBody3D::applyImpulseAtPoint(const Vec3& impulse, const Vec3& worldPoint) {
    if (isStatic) return;
    wakeUp();
    linearVelocity += impulse * inverseMass;
    Vec3 r = worldPoint - position;
    Vec3 angularImpulse = r.cross(impulse);
    angularVelocity += worldInverseInertia() * angularImpulse;
}

void RigidBody3D::applyAngularImpulse(const Vec3& impulse) {
    if (isStatic) return;
    wakeUp();
    angularVelocity += worldInverseInertia() * impulse;
}

void RigidBody3D::wakeUp() {
    sleeping = false;
    sleepTimer = 0.0f;
}

// -----------------------------------------------------------------------------
// World-space inverse inertia:  R * I_local^-1 * R^T
// -----------------------------------------------------------------------------
Mat3 RigidBody3D::worldInverseInertia() const {
    Mat3 R = Mat3::fromQuat(orientation);
    return R * inverseInertia * R.transposed();
}

// Velocity of a point on the body in world space:
//   v_point = v_linear + omega x r   where r = worldPoint - centerOfMass
Vec3 RigidBody3D::getVelocityAtPoint(const Vec3& worldPoint) const {
    Vec3 r = worldPoint - position;
    return linearVelocity + angularVelocity.cross(r);
}

float RigidBody3D::getKineticEnergy() const {
    // KE_linear = 0.5 * m * |v|^2
    float linearKE = 0.5f * mass * linearVelocity.lengthSq();
    // KE_angular = 0.5 * omega^T * I * omega
    // For a diagonal inertia this is just sum(0.5 * I_ii * omega_i^2).
    // For a general inertia we compute L = I * omega, then 0.5 * omega . L.
    Vec3 L = inertia * angularVelocity;
    float angularKE = 0.5f * angularVelocity.dot(L);
    return linearKE + angularKE;
}

// -----------------------------------------------------------------------------
// Integration
// -----------------------------------------------------------------------------
// Semi-implicit (symplectic) Euler:
//   v_{n+1} = v_n + a_n * dt
//   x_{n+1} = x_n + v_{n+1} * dt
//
// This is more stable than explicit Euler for spring / damping systems and
// is the standard choice for real-time game physics (Bullet, Box2D, ODE).
//
// For rotation we integrate the quaternion derivative:
//   q_dot = 0.5 * omega_quat * q
// where omega_quat = (omega.x, omega.y, omega.z, 0).
// Then renormalize q to correct numerical drift.
void RigidBody3D::integrate(float dt, const Vec3& gravity) {
    if (isStatic || sleeping) return;

    if (!isKinematic) {
        // Apply gravity (force = m * g * gravityScale)
        if (useGravity) {
            force += gravity * (mass * gravityScale);
        }

        // Linear: a = F / m, v += a * dt
        Vec3 acceleration = force * inverseMass;
        linearVelocity += acceleration * dt;

        // Angular: alpha = I^-1 * tau, w += alpha * dt
        Vec3 angularAcceleration = worldInverseInertia() * torque;
        angularVelocity += angularAcceleration * dt;
    }

    // Damping (frame-rate independent): v *= exp(-damping * dt)
    // Approximated as v *= (1 - damping * dt) for small dt — stable enough
    // for damping < 1.0 and dt < 1/30.
    float linearDampFactor = 1.0f / (1.0f + linearDamping * dt);
    float angularDampFactor = 1.0f / (1.0f + angularDamping * dt);
    linearVelocity  *= linearDampFactor;
    angularVelocity *= angularDampFactor;

    // Position integration
    position += linearVelocity * dt;

    // Quaternion integration: q' = q + 0.5 * dt * (omega_quat * q)
    // omega_quat = (w.x, w.y, w.z, 0)
    if (angularVelocity.lengthSq() > TD_EPSILON) {
        Quat omegaQuat(angularVelocity.x, angularVelocity.y,
                       angularVelocity.z, 0.0f);
        Quat qDot = omegaQuat * orientation;
        qDot = qDot * (0.5f * dt);
        orientation += qDot;
        renormalizeOrientation();
    }

    // Sleep detection
    float linearSpeedSq = linearVelocity.lengthSq();
    float angularSpeedSq = angularVelocity.lengthSq();
    if (linearSpeedSq < sleepThreshold * sleepThreshold &&
        angularSpeedSq < sleepThreshold * sleepThreshold) {
        sleepTimer += dt;
        if (sleepTimer >= sleepTimeRequired) {
            sleeping = true;
            linearVelocity.setZero();
            angularVelocity.setZero();
        }
    } else {
        sleepTimer = 0.0f;
    }

    clearForces();
}

void RigidBody3D::clearForces() {
    force.setZero();
    torque.setZero();
}

void RigidBody3D::renormalizeOrientation() {
    orientation.normalize();
}

} // namespace td
