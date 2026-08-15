// =============================================================================
// TD Engine - 3D Colliders + Narrow Phase (src/physics/collider3d.h)
//
// Collider types:
//   - Sphere3D       (cheapest; broad-phase-friendly)
//   - Box3D          (AABB / OBB; SAT-based)
//   - Capsule3D      (segment + radius; great for characters)
//   - ConvexHull3D   (general convex mesh; GJK + EPA)
//
// Narrow-phase collision detection:
//   1. Specialized fast paths for primitive pairs (sphere-sphere, sphere-box,
//      sphere-capsule, box-box AABB).
//   2. General GJK (Gilbert-Johnson-Keerthi) for arbitrary convex pairs.
//   3. EPA (Expanding Polytope Algorithm) for penetration depth when GJK
//      reports overlap but no contact info.
//   4. Manifold generation: clamps contact to 1-4 points, computes contact
//      normal + penetration depth for the impulse solver.
//
// The "Physics in 25 Hours" course covers the underlying mechanics (impulse,
// momentum, collisions, restitution).  The collision-detection algorithms
// here are the engineering layer that lets those mechanics run in real time
// on thousands of bodies — they are standard results from computational
// geometry (GJK 1988, EPA 2004) used in every modern physics engine.
//
// Contact result structure (ContactManifold3D) is what the sequential
// impulse solver in physics_world_3d consumes.
// =============================================================================
#pragma once
#include "../core/math/vec3.h"
#include "../core/math/quat.h"
#include "../core/math/mat3.h"
#include "rigidbody3d.h"
#include <cstdint>

namespace td {

// -----------------------------------------------------------------------------
// Collider shapes
// -----------------------------------------------------------------------------
enum class ColliderShape3D : uint8_t {
    Sphere     = 0,
    Box        = 1,
    Capsule    = 2,
    ConvexHull = 3
};

struct SphereCollider3D {
    float radius = 0.5f;
};

struct BoxCollider3D {
    Vec3 halfExtents = {0.5f, 0.5f, 0.5f};
};

struct CapsuleCollider3D {
    float radius = 0.4f;
    float height = 1.8f;       // total height (cylinder + 2 hemispheres)
    int   axis   = 1;          // 0=X, 1=Y, 2=Z
};

struct ConvexHullCollider3D {
    static constexpr int MAX_POINTS = 64;
    Vec3  localPoints[MAX_POINTS];
    int   pointCount = 0;
};

// A collider "instance" attaches a shape to a body's pose.
struct Collider3D {
    ColliderShape3D shape = ColliderShape3D::Sphere;
    SphereCollider3D      sphere;
    BoxCollider3D         box;
    CapsuleCollider3D     capsule;
    ConvexHullCollider3D  hull;

    // Body this collider belongs to (index into PhysicsWorld3D::bodies).
    // -1 = unattached.
    int32_t bodyIndex = -1;

    // Local offset from body's center (in body-local space)
    Vec3 localOffset = {0, 0, 0};
    Quat localRotation = Quat::identity();

    // ---- Helpers ----------------------------------------------------------
    // Returns the world-space AABB (broad-phase bounds) for this collider
    // given a body pose.
    void computeWorldAABB(const Vec3& bodyPos, const Quat& bodyRot,
                          float& outMinX, float& outMinY, float& outMinZ,
                          float& outMaxX, float& outMaxY, float& outMaxZ) const;

    // Support function for GJK: returns the point on the collider surface
    // (in world space) furthest in the direction `dir` (world space).
    // Required by GJK + EPA for general convex shapes.
    Vec3 support(const Vec3& dir,
                 const Vec3& bodyPos, const Quat& bodyRot) const;
};

// -----------------------------------------------------------------------------
// Contact manifold
// -----------------------------------------------------------------------------
struct ContactPoint3D {
    Vec3  point       = {0, 0, 0};   // world-space contact point
    Vec3  normal      = {0, 1, 0};   // world-space normal, points A -> B
    float penetration = 0.0f;         // penetration depth (>=0)
    float normalImpulse = 0.0f;       // accumulated impulse (warm starting)
    float tangentImpulse1 = 0.0f;
    float tangentImpulse2 = 0.0f;
    // Target normal velocity for restitution.  Computed ONCE at the start of
    // the solver step (based on the initial approaching velocity).  The
    // iterative solver then drives velAlongNormal toward this target, rather
    // than recomputing restitution each iteration (which would kill the bounce
    // after the first iteration when the bodies start separating).
    float targetNormalVelocity = 0.0f;
};

struct ContactManifold3D {
    int32_t bodyA = -1;
    int32_t bodyB = -1;
    static constexpr int MAX_POINTS = 4;
    ContactPoint3D points[MAX_POINTS];
    int pointCount = 0;
    float friction = 0.3f;
    float restitution = 0.2f;
    // Tangent basis (perpendicular to normal) for friction
    Vec3 tangent1 = {1, 0, 0};
    Vec3 tangent2 = {0, 1, 0};
};

// -----------------------------------------------------------------------------
// Narrow-phase detector
// -----------------------------------------------------------------------------
class NarrowPhase3D {
public:
    // Detect collision between two colliders given their world poses.
    // Returns true and fills `outManifold` if they collide.
    // `manifold.bodyA` / `bodyB` are set to the collider body indices.
    static bool collide(const Collider3D& a, const Vec3& posA, const Quat& rotA,
                        const Collider3D& b, const Vec3& posB, const Quat& rotB,
                        ContactManifold3D& outManifold);

private:
    // Specialized fast paths (return false if no collision)
    static bool sphereSphere(const SphereCollider3D& sa, const Vec3& posA,
                              const SphereCollider3D& sb, const Vec3& posB,
                              ContactManifold3D& out);
    static bool sphereBox(const SphereCollider3D& s, const Vec3& posS,
                           const BoxCollider3D& b, const Vec3& posB,
                           const Quat& rotB, ContactManifold3D& out);
    static bool sphereCapsule(const SphereCollider3D& s, const Vec3& posS,
                               const CapsuleCollider3D& c, const Vec3& posC,
                               const Quat& rotC, ContactManifold3D& out);
    static bool capsuleCapsule(const CapsuleCollider3D& a, const Vec3& posA,
                                const Quat& rotA,
                                const CapsuleCollider3D& b, const Vec3& posB,
                                const Quat& rotB, ContactManifold3D& out);

    // General convex-convex via GJK + EPA
    static bool gjkEPA(const Collider3D& a, const Vec3& posA, const Quat& rotA,
                        const Collider3D& b, const Vec3& posB, const Quat& rotB,
                        ContactManifold3D& out);

    // Helpers
    static Vec3 closestPointOnSegment(const Vec3& a, const Vec3& b, const Vec3& p);
    static void  computeTangentBasis(const Vec3& normal, Vec3& t1, Vec3& t2);
};

} // namespace td
