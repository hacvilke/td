// =============================================================================
// TD Engine - 3D Physics World (src/physics/physics_world_3d.h)
//
// Orchestrates the full physics step:
//
//   1. Apply external forces (gravity, user forces)
//   2. Integrate velocities (semi-implicit Euler)
//   3. Broadphase: detect potential collision pairs (Sweep-and-Prune)
//   4. Narrowphase: per-pair contact manifold generation
//      (specialized fast paths for sphere/box/capsule + GJK/EPA for general)
//   5. Solve velocity constraints (sequential impulse, multiple iterations)
//      - Non-penetration constraint (normal impulse)
//      - Friction constraint (Coulomb friction, tangent impulses)
//   6. Integrate positions
//   7. Solve position constraints (Baumgarte stabilization — slop + percent)
//   8. Update broadphase AABBs
//   9. Sleeping: mark low-energy bodies as sleeping
//
// Sequential impulse solver:  This is the same algorithm used by Box2D v2/v3,
// Bullet, and ODE.  It iterates over all contact constraints N times
// (typically 8-20), accumulating impulses, converging to a solution that
// satisfies all constraints simultaneously.  Warm starting (carrying over
// last frame's impulses) makes stacks of objects stable.
//
// This implementation is real-time, deterministic, and warm-started.
// Multi-threading is left as a future optimization (the per-pair solver is
// already embarrassingly parallel — see TODO at the bottom).
//
// Physics concepts covered:
//   - Newton's 2nd law (F = ma)            [integrate]
//   - Impulse-momentum theorem (J = Δp)    [solver]
//   - Coefficient of restitution (e)       [normal impulse]
//   - Coulomb friction (μN)                [friction impulse]
//   - Conservation of momentum             [impulse exchange]
//   - Constraint forces (Baumgarte)        [position correction]
// =============================================================================
#pragma once
#include "../core/math/vec3.h"
#include "rigidbody3d.h"
#include "collider3d.h"
#include "broadphase_3d.h"
#include <vector>
#include <cstdint>

namespace td {

struct PhysicsBody3D {
    RigidBody3D body;
    Collider3D  collider;
    bool        colliderSet = false;
};

// -----------------------------------------------------------------------------
// Constraints — declared here (not in constraints_3d.h) to avoid a circular
// include: physics_world_3d.h needs Constraint3D as a member, and
// constraints_3d.h needs PhysicsWorld3D for the solver.  Putting Constraint3D
// here lets the world own a vector<Constraint3D> directly, and the solver
// (in constraints_3d.h) sees both types via the #include.
// -----------------------------------------------------------------------------
enum class ConstraintType3D : uint8_t {
    Distance = 0,
    Point    = 1,
    Hinge    = 2
};

struct Constraint3D {
    ConstraintType3D type = ConstraintType3D::Distance;
    int32_t bodyA = -1;
    int32_t bodyB = -1;

    // Anchor points in body-local space (each body has its own anchor)
    Vec3 localAnchorA = {0, 0, 0};
    Vec3 localAnchorB = {0, 0, 0};

    // Distance constraint: target distance between anchors
    float targetDistance = 1.0f;

    // Hinge constraint: allowed rotation axis in body A's local space
    // (B can rotate freely around this axis but not around the perpendiculars)
    Vec3 hingeAxisA = {1, 0, 0};
    Vec3 hingeAxisB = {1, 0, 0};

    // Constraint stiffness (0 = rigid, 1 = soft).  Soft constraints use
    // frequency + dampingRatio for spring-like behavior (e.g., ropes).
    float stiffness      = 1.0f;
    float frequency      = 0.0f;
    float dampingRatio   = 1.0f;

    // Accumulated impulse (warm starting)
    float accumulatedImpulse = 0.0f;
    Vec3  accumulatedImpulseVec = {0, 0, 0};

    // Whether to collide connected bodies (false = skip narrowphase between
    // these two bodies, true = allow contact)
    bool collideConnected = false;
};

class PhysicsWorld3D {
public:
    PhysicsWorld3D();

    // ---- World configuration ----------------------------------------------
    void setGravity(const Vec3& g) { m_gravity = g; }
    Vec3 getGravity() const { return m_gravity; }

    void setSolverIterations(int iters) { m_solverIterations = iters; }
    int  getSolverIterations() const { return m_solverIterations; }

    void setPositionIterations(int iters) { m_positionIterations = iters; }
    int  getPositionIterations() const { return m_positionIterations; }

    void setBaumgarte(float b) { m_baumgarte = b; }
    void setSlop(float s) { m_slop = s; }

    void setAllowSleeping(bool allow) { m_allowSleeping = allow; }
    bool getAllowSleeping() const { return m_allowSleeping; }

    // ---- Body management --------------------------------------------------
    // Returns the body index.  Caller then calls setCollider* on the body.
    int32_t addBody();
    int32_t addBody(const RigidBody3D& initial);
    void    removeBody(int32_t index);

    PhysicsBody3D& getBody(int32_t index) { return m_bodies[index]; }
    const PhysicsBody3D& getBody(int32_t index) const { return m_bodies[index]; }
    int32_t bodyCount() const { return (int32_t)m_bodies.size(); }

    // Convenience: configure a collider on a body
    void setSphereCollider(int32_t bodyIndex, float radius,
                            const Vec3& localOffset = {0, 0, 0});
    void setBoxCollider(int32_t bodyIndex, const Vec3& halfExtents,
                         const Vec3& localOffset = {0, 0, 0});
    void setCapsuleCollider(int32_t bodyIndex, float radius, float height,
                             int axis = 1,
                             const Vec3& localOffset = {0, 0, 0});

    // ---- Step -------------------------------------------------------------
    // Advances the simulation by dt seconds.  Recommended dt = 1/60.
    // Uses semi-implicit Euler + sequential impulse solver.
    void step(float dt);

    // ---- Constraints ------------------------------------------------------
    // Constraints are solved alongside contact constraints.  The world
    // holds the list; user adds/removes via these.
    int32_t addConstraint(const Constraint3D& c);
    void    removeConstraint(int32_t index);
    std::vector<Constraint3D>& getConstraints() { return m_constraints; }
    const std::vector<Constraint3D>& getConstraints() const { return m_constraints; }

    // ---- Queries ----------------------------------------------------------
    // Raycast against all colliders.  Returns true on hit, fills outPoint + outNormal.
    bool raycast(const Vec3& origin, const Vec3& dir, float maxDist,
                 Vec3& outPoint, Vec3& outNormal, int32_t& outBodyIndex) const;

    // Returns the list of current contact manifolds (for gameplay events).
    const std::vector<ContactManifold3D>& getContacts() const { return m_contacts; }

private:
    std::vector<PhysicsBody3D>  m_bodies;
    std::vector<ContactManifold3D> m_contacts;
    std::vector<Constraint3D>   m_constraints;
    Broadphase3D                 m_broadphase;

    Vec3  m_gravity        = Vec3(0.0f, -9.81f, 0.0f);
    int   m_solverIterations  = 10;
    int   m_positionIterations = 5;
    float m_baumgarte      = 0.2f;     // position correction strength (0-1)
    float m_slop           = 0.005f;   // allowed penetration in meters
    bool  m_allowSleeping  = true;

    // ---- Internal step helpers -------------------------------------------
    void integrateVelocities(float dt);
    void detectCollisions();
    void solveVelocityConstraints();
    void integratePositions(float dt);
    void solvePositionConstraints();
    void updateBroadphase();
    void updateSleeping(float dt);

    // Persistent pair tracking for warm starting + manifold coherence
    struct PersistentPair {
        int32_t bodyA;
        int32_t bodyB;
        ContactManifold3D manifold;
    };
    std::vector<PersistentPair> m_persistentPairs;

    void warmStartContacts();
};

} // namespace td
