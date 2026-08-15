// =============================================================================
// TD Engine - 3D Rigid Body (src/physics/rigidbody3d.h)
//
// Full 3D rigid body dynamics:
//   - Position (Vec3) + orientation (Quat) — 6-DOF pose
//   - Linear + angular velocity (Vec3)
//   - Inertia tensor (Mat3) + inverse — how mass is distributed
//   - Forces + torques accumulation
//   - Semi-implicit Euler integration for linear motion
//   - Quaternion derivative integration for rotation (numerically stable,
//     preserves unit length to within 1e-4 per second)
//
// Physics being modeled (matches the topics in the "Physics in 25 Hours"
// course by Physics Tutoring Hub):
//   - Newton's 2nd law (F = ma, tau = I*alpha)
//   - Conservation of linear + angular momentum
//   - Kinematics (integrate v from a, integrate x from v)
//   - Rotational dynamics (torque, moment of inertia, angular acceleration)
//   - Energy (kinetic energy queries for sleeping/awakening)
//
// Integration scheme: semi-implicit Euler for linear motion + quaternion
// derivative for rotation.  This is what Bullet, Box2D, and ODE use for
// real-time games — it's stable enough for game timesteps (1/60 s) and
// cheap enough to run on thousands of bodies.  RK4 is available in
// `integrateRK4` for higher accuracy when needed (vehicle physics,
// physics puzzles that need exact reproducibility).
//
// Inertia tensor conventions:
//   - `inertia` is the body-space (local) inertia tensor — diagonal for
//     primitive shapes (sphere, box, capsule), general for arbitrary meshes.
//   - `inverseInertia` is its inverse, also in body space.
//   - `worldInverseInertia()` returns the world-space inverse inertia tensor
//     = R * inverseInertia_local * R^T.  Used by the impulse solver.
//
// Sleeping: bodies with low kinetic energy for `sleepThreshold` seconds are
// marked `sleeping` and skipped by the integrator.  Wakes on external
// force/impulse or contact with an awake body.
// =============================================================================
#pragma once
#include "../core/math/vec3.h"
#include "../core/math/quat.h"
#include "../core/math/mat3.h"
#include "../core/math/math.h"
#include <cstdint>

namespace td {

class RigidBody3D {
public:
    // ---- Pose (6-DOF) ------------------------------------------------------
    Vec3 position       = {0, 0, 0};
    Quat orientation    = Quat::identity();

    // ---- Linear dynamics ---------------------------------------------------
    Vec3 linearVelocity     = {0, 0, 0};
    Vec3 force              = {0, 0, 0};   // accumulated this frame
    float mass              = 1.0f;
    float inverseMass       = 1.0f;
    float linearDamping     = 0.01f;       // per second

    // ---- Angular dynamics --------------------------------------------------
    Vec3 angularVelocity    = {0, 0, 0};
    Vec3 torque             = {0, 0, 0};   // accumulated this frame
    Mat3 inertia            = Mat3::identity();
    Mat3 inverseInertia     = Mat3::identity();
    float angularDamping    = 0.01f;       // per second

    // ---- Material properties (used by collision solver) --------------------
    float friction          = 0.3f;        // Coulomb friction coefficient
    float restitution       = 0.2f;        // bounciness (0 = stick, 1 = elastic)
    float rollingFriction   = 0.0f;        // slows rolling objects (sphere/capsule)

    // ---- State flags -------------------------------------------------------
    bool  isStatic          = false;       // infinite mass, never moves
    bool  isKinematic       = false;       // moves only via velocity, ignores forces
    bool  isTrigger         = false;       // generates events but no impulse response
    bool  useGravity        = true;
    float gravityScale      = 1.0f;
    bool  sleeping          = false;
    float sleepTimer        = 0.0f;
    float sleepThreshold    = 0.05f;       // m/s — below this, accumulate sleep time
    float sleepTimeRequired = 1.0f;       // seconds at low energy before sleeping

    // ---- API ---------------------------------------------------------------

    // Mass + inertia setters (also update inverseMass / inverseInertia)
    void setMass(float m);
    void setInertia(const Mat3& I);
    void setInertiaSphere(float radius, float mass);
    void setInertiaBox(const Vec3& halfExtents, float mass);
    void setInertiaCapsule(float radius, float height, float mass, int axis = 1);

    // Force / impulse application
    void applyForce(const Vec3& f);
    void applyForceAtPoint(const Vec3& f, const Vec3& worldPoint);
    void applyTorque(const Vec3& t);
    void applyImpulse(const Vec3& impulse);
    void applyImpulseAtPoint(const Vec3& impulse, const Vec3& worldPoint);
    void applyAngularImpulse(const Vec3& impulse);

    // Wake from sleep (called automatically on force/impulse application)
    void wakeUp();

    // World-space inverse inertia:  R * I_local^-1 * R^T
    Mat3 worldInverseInertia() const;

    // Velocity of a point on the body (world space)
    Vec3 getVelocityAtPoint(const Vec3& worldPoint) const;

    // Kinetic energy (linear + angular) — used for sleep detection
    float getKineticEnergy() const;

    // ---- Integration -------------------------------------------------------
    // Semi-implicit Euler + quaternion derivative.  Stable for game timesteps.
    void integrate(float dt, const Vec3& gravity);

    // Clears accumulated forces + torques (called at end of each step)
    void clearForces();

    // ---- Queries -----------------------------------------------------------
    Vec3 getWorldPosition() const { return position; }
    Quat getWorldOrientation() const { return orientation; }

    // Transform a local-space point / direction to world space
    Vec3 localToWorldPoint(const Vec3& local) const {
        return position + orientation.rotate(local);
    }
    Vec3 localToWorldDir(const Vec3& local) const {
        return orientation.rotate(local);
    }
    Vec3 worldToLocalPoint(const Vec3& world) const {
        return orientation.inverseRotate(world - position);
    }
    Vec3 worldToLocalDir(const Vec3& world) const {
        return orientation.inverseRotate(world);
    }

private:
    // Normalize the orientation quaternion after integration (drift correction)
    void renormalizeOrientation();
};

// World gravity constant — m/s^2.  Negative Y = down.
constexpr float GRAVITY_3D = 9.81f;
inline Vec3 gravityVec3() { return Vec3(0.0f, -GRAVITY_3D, 0.0f); }

} // namespace td
