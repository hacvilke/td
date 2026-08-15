// =============================================================================
// TD Engine - 3D Physics World Implementation (src/physics/physics_world_3d.cpp)
//
// Sequential impulse solver + Baumgarte stabilization, modeled after Box2D
// (Erin Catto's GDC presentations) and Bullet's btSequentialImpulseConstraintSolver.
// =============================================================================
#include "physics_world_3d.h"
#include "constraints_3d.h"
#include "../core/math/math.h"
#include <cmath>

namespace td {

PhysicsWorld3D::PhysicsWorld3D() {
    m_contacts.reserve(256);
    m_persistentPairs.reserve(256);
}

// =============================================================================
// Body management
// =============================================================================
int32_t PhysicsWorld3D::addBody() {
    int32_t idx = (int32_t)m_bodies.size();
    m_bodies.push_back(PhysicsBody3D{});
    return idx;
}

int32_t PhysicsWorld3D::addBody(const RigidBody3D& initial) {
    int32_t idx = (int32_t)m_bodies.size();
    PhysicsBody3D pb;
    pb.body = initial;
    m_bodies.push_back(pb);
    return idx;
}

void PhysicsWorld3D::removeBody(int32_t index) {
    if (index < 0 || index >= (int32_t)m_bodies.size()) return;
    // Swap-and-pop keeps O(1) but invalidates the last index.  For physics
    // stability we prefer to keep the array stable, so just mark as static
    // and zero-out — the broadphase will skip it.  Full removal requires
    // updating all body indices in pairs, which is expensive.
    m_bodies[index].body.isStatic = true;
    m_bodies[index].body.linearVelocity.setZero();
    m_bodies[index].body.angularVelocity.setZero();
    m_bodies[index].colliderSet = false;
}

void PhysicsWorld3D::setSphereCollider(int32_t bodyIndex, float radius,
                                        const Vec3& localOffset) {
    if (bodyIndex < 0 || bodyIndex >= (int32_t)m_bodies.size()) return;
    auto& pb = m_bodies[bodyIndex];
    pb.collider.shape = ColliderShape3D::Sphere;
    pb.collider.sphere.radius = radius;
    pb.collider.localOffset = localOffset;
    pb.collider.bodyIndex = bodyIndex;
    pb.colliderSet = true;
    if (!pb.body.isStatic && pb.body.mass > 0.0f) {
        pb.body.setInertiaSphere(radius, pb.body.mass);
    }
}

void PhysicsWorld3D::setBoxCollider(int32_t bodyIndex, const Vec3& halfExtents,
                                     const Vec3& localOffset) {
    if (bodyIndex < 0 || bodyIndex >= (int32_t)m_bodies.size()) return;
    auto& pb = m_bodies[bodyIndex];
    pb.collider.shape = ColliderShape3D::Box;
    pb.collider.box.halfExtents = halfExtents;
    pb.collider.localOffset = localOffset;
    pb.collider.bodyIndex = bodyIndex;
    pb.colliderSet = true;
    if (!pb.body.isStatic && pb.body.mass > 0.0f) {
        pb.body.setInertiaBox(halfExtents, pb.body.mass);
    }
}

void PhysicsWorld3D::setCapsuleCollider(int32_t bodyIndex, float radius,
                                         float height, int axis,
                                         const Vec3& localOffset) {
    if (bodyIndex < 0 || bodyIndex >= (int32_t)m_bodies.size()) return;
    auto& pb = m_bodies[bodyIndex];
    pb.collider.shape = ColliderShape3D::Capsule;
    pb.collider.capsule.radius = radius;
    pb.collider.capsule.height = height;
    pb.collider.capsule.axis = axis;
    pb.collider.localOffset = localOffset;
    pb.collider.bodyIndex = bodyIndex;
    pb.colliderSet = true;
    if (!pb.body.isStatic && pb.body.mass > 0.0f) {
        pb.body.setInertiaCapsule(radius, height, pb.body.mass, axis);
    }
}

// =============================================================================
// Constraints
// =============================================================================
int32_t PhysicsWorld3D::addConstraint(const Constraint3D& c) {
    int32_t idx = (int32_t)m_constraints.size();
    m_constraints.push_back(c);
    // Wake both bodies — they're now linked
    if (c.bodyA >= 0 && c.bodyA < (int32_t)m_bodies.size())
        m_bodies[c.bodyA].body.wakeUp();
    if (c.bodyB >= 0 && c.bodyB < (int32_t)m_bodies.size())
        m_bodies[c.bodyB].body.wakeUp();
    return idx;
}

void PhysicsWorld3D::removeConstraint(int32_t index) {
    if (index < 0 || index >= (int32_t)m_constraints.size()) return;
    // Swap-and-pop
    m_constraints[index] = m_constraints.back();
    m_constraints.pop_back();
}

// =============================================================================
// step()
// =============================================================================
void PhysicsWorld3D::step(float dt) {
    if (dt <= 0.0f) return;

    // 1. Integrate velocities (apply forces, gravity, accumulate accelerations)
    integrateVelocities(dt);

    // 2. Detect collisions (broadphase + narrowphase)
    detectCollisions();

    // 3. Warm-start contacts (carry over last frame's impulses)
    warmStartContacts();

    // 4. Solve velocity constraints (sequential impulse, N iterations)
    solveVelocityConstraints();

    // 4b. Solve user constraints (distance, point, hinge)
    if (!m_constraints.empty()) {
        ConstraintSolver3D cs;
        cs.solve(m_constraints, *this, m_solverIterations);
    }

    // 5. Integrate positions
    integratePositions(dt);

    // 6. Solve position constraints (Baumgarte)
    solvePositionConstraints();

    // 7. Update broadphase AABBs
    updateBroadphase();

    // 8. Update sleeping
    if (m_allowSleeping) updateSleeping(dt);
}

void PhysicsWorld3D::integrateVelocities(float dt) {
    for (auto& pb : m_bodies) {
        if (pb.body.isStatic) continue;
        // Apply gravity as a force (force = m * g)
        if (pb.body.useGravity && !pb.body.isKinematic) {
            pb.body.force += m_gravity * (pb.body.mass * pb.body.gravityScale);
        }
        // Linear: a = F / m, v += a * dt
        Vec3 a = pb.body.force * pb.body.inverseMass;
        pb.body.linearVelocity += a * dt;
        // Angular: alpha = I^-1 * tau, w += alpha * dt
        Vec3 alpha = pb.body.worldInverseInertia() * pb.body.torque;
        pb.body.angularVelocity += alpha * dt;
        // Damping
        float ld = 1.0f / (1.0f + pb.body.linearDamping * dt);
        float ad = 1.0f / (1.0f + pb.body.angularDamping * dt);
        pb.body.linearVelocity  *= ld;
        pb.body.angularVelocity *= ad;
        // Clear accumulated forces
        pb.body.clearForces();
    }
}

void PhysicsWorld3D::detectCollisions() {
    m_broadphase.clear();
    for (int32_t i = 0; i < (int32_t)m_bodies.size(); i++) {
        auto& pb = m_bodies[i];
        if (!pb.colliderSet) continue;
        float minX, minY, minZ, maxX, maxY, maxZ;
        pb.collider.computeWorldAABB(pb.body.position, pb.body.orientation,
                                       minX, minY, minZ, maxX, maxY, maxZ);
        m_broadphase.addBody(i, Vec3(minX, minY, minZ), Vec3(maxX, maxY, maxZ),
                              pb.body.isStatic);
    }

    BroadphasePair3D pairs[1024];
    int pairCount = m_broadphase.computePairs(pairs, 1024);

    // Rebuild contact list (with warm-start preservation handled by
    // m_persistentPairs)
    m_contacts.clear();
    for (int i = 0; i < pairCount; i++) {
        int32_t a = pairs[i].a, b = pairs[i].b;
        auto& ba = m_bodies[a];
        auto& bb = m_bodies[b];
        if (!ba.colliderSet || !bb.colliderSet) continue;
        if (ba.body.isStatic && bb.body.isStatic) continue;
        if (ba.body.sleeping && bb.body.sleeping) continue;

        ContactManifold3D manifold;
        if (NarrowPhase3D::collide(ba.collider, ba.body.position, ba.body.orientation,
                                    bb.collider, bb.body.position, bb.body.orientation,
                                    manifold)) {
            manifold.bodyA = a;
            manifold.bodyB = b;
            // Material: average friction, min restitution
            manifold.friction = 0.5f * (ba.body.friction + bb.body.friction);
            manifold.restitution = minF(ba.body.restitution, bb.body.restitution);
            m_contacts.push_back(manifold);

            // Wake bodies on contact
            if (!ba.body.isStatic) ba.body.wakeUp();
            if (!bb.body.isStatic) bb.body.wakeUp();
        }
    }
}

// =============================================================================
// Velocity constraint solver — sequential impulse
// =============================================================================
// For each contact point, the constraint is:
//   (vB + wB x rB - vA - wA x rA) . n >= -e * (vRel . n)
//
// Where:
//   n  = contact normal (A -> B)
//   rA = contact point - centerA
//   rB = contact point - centerB
//   vRel = relative velocity at contact point
//   e  = coefficient of restitution
//
// The impulse magnitude is:
//   Jn = -(1 + e) * vRel . n / (1/mA + 1/mB + (rA x n) . IA^-1 . (rA x n) + (rB x n) . IB^-1 . (rB x n))
//
// We accumulate Jn across iterations (warm starting + sequential impulse).
// Friction is similar but clamped to ±mu * Jn (Coulomb friction cone).
// =============================================================================
void PhysicsWorld3D::warmStartContacts() {
    // Reset accumulated impulses AND compute target normal velocity for
    // restitution.  This is critical: if we recompute restitution each solver
    // iteration, the bounce dies because after iter 0 the bodies are
    // separating and the solver pulls them back.  By precomputing the target
    // separating velocity (-e * approaching velocity), the solver converges
    // to the correct bounce in 1 iteration and stays there.
    for (auto& m : m_contacts) {
        auto& bodyA = m_bodies[m.bodyA].body;
        auto& bodyB = m_bodies[m.bodyB].body;

        for (int i = 0; i < m.pointCount; i++) {
            ContactPoint3D& cp = m.points[i];
            cp.normalImpulse = 0.0f;
            cp.tangentImpulse1 = 0.0f;
            cp.tangentImpulse2 = 0.0f;

            // Compute the initial approaching velocity along the normal
            Vec3 rA = cp.point - bodyA.position;
            Vec3 rB = cp.point - bodyB.position;
            Vec3 vA = bodyA.linearVelocity + bodyA.angularVelocity.cross(rA);
            Vec3 vB = bodyB.linearVelocity + bodyB.angularVelocity.cross(rB);
            Vec3 rv = vB - vA;
            float velAlongNormal = rv.dot(cp.normal);

            // Target separating velocity.  Only if approaching (velAlongNormal < 0).
            // For static contacts (velAlongNormal >= 0), target = 0 (no bounce).
            if (velAlongNormal < 0.0f) {
                cp.targetNormalVelocity = -m.restitution * velAlongNormal;
            } else {
                cp.targetNormalVelocity = 0.0f;
            }
        }
    }
}

void PhysicsWorld3D::solveVelocityConstraints() {
    for (int iter = 0; iter < m_solverIterations; iter++) {
        for (auto& m : m_contacts) {
            auto& bodyA = m_bodies[m.bodyA].body;
            auto& bodyB = m_bodies[m.bodyB].body;
            if (bodyA.isStatic && bodyB.isStatic) continue;

            float invMassA = bodyA.isStatic ? 0.0f : bodyA.inverseMass;
            float invMassB = bodyB.isStatic ? 0.0f : bodyB.inverseMass;
            Mat3 invInertiaA = bodyA.isStatic ? Mat3() : bodyA.worldInverseInertia();
            Mat3 invInertiaB = bodyB.isStatic ? Mat3() : bodyB.worldInverseInertia();

            for (int p = 0; p < m.pointCount; p++) {
                ContactPoint3D& cp = m.points[p];
                Vec3 rA = cp.point - bodyA.position;
                Vec3 rB = cp.point - bodyB.position;

                Vec3 vA = bodyA.linearVelocity + bodyA.angularVelocity.cross(rA);
                Vec3 vB = bodyB.linearVelocity + bodyB.angularVelocity.cross(rB);
                Vec3 rv = vB - vA;

                float velAlongNormal = rv.dot(cp.normal);

                Vec3 rnA = rA.cross(cp.normal);
                Vec3 rnB = rB.cross(cp.normal);
                float kN = invMassA + invMassB
                         + (invInertiaA * rnA).dot(rnA)
                         + (invInertiaB * rnB).dot(rnB);
                if (kN < TD_EPSILON) continue;

                float j = (cp.targetNormalVelocity - velAlongNormal) / kN;
                float oldImpulse = cp.normalImpulse;
                cp.normalImpulse = maxF(oldImpulse + j, 0.0f);
                j = cp.normalImpulse - oldImpulse;

                Vec3 impulse = cp.normal * j;
                bodyA.linearVelocity  -= impulse * invMassA;
                bodyA.angularVelocity -= invInertiaA * rA.cross(impulse);
                bodyB.linearVelocity  += impulse * invMassB;
                bodyB.angularVelocity += invInertiaB * rB.cross(impulse);

                // ---- Friction impulse (Coulomb model) ----
                vA = bodyA.linearVelocity + bodyA.angularVelocity.cross(rA);
                vB = bodyB.linearVelocity + bodyB.angularVelocity.cross(rB);
                rv = vB - vA;

                // Tangent direction 1
                float vt1 = rv.dot(m.tangent1);
                Vec3 rtA1 = rA.cross(m.tangent1);
                Vec3 rtB1 = rB.cross(m.tangent1);
                float kT1 = invMassA + invMassB
                          + (invInertiaA * rtA1).dot(rtA1)
                          + (invInertiaB * rtB1).dot(rtB1);
                if (kT1 > TD_EPSILON) {
                    float jt1 = -vt1 / kT1;
                    float maxFriction = m.friction * cp.normalImpulse;
                    float oldT1 = cp.tangentImpulse1;
                    cp.tangentImpulse1 = clamp(oldT1 + jt1, -maxFriction, maxFriction);
                    jt1 = cp.tangentImpulse1 - oldT1;
                    Vec3 frictionImpulse1 = m.tangent1 * jt1;
                    bodyA.linearVelocity  -= frictionImpulse1 * invMassA;
                    bodyA.angularVelocity -= invInertiaA * rA.cross(frictionImpulse1);
                    bodyB.linearVelocity  += frictionImpulse1 * invMassB;
                    bodyB.angularVelocity += invInertiaB * rB.cross(frictionImpulse1);
                }

                // Tangent direction 2
                vA = bodyA.linearVelocity + bodyA.angularVelocity.cross(rA);
                vB = bodyB.linearVelocity + bodyB.angularVelocity.cross(rB);
                rv = vB - vA;
                float vt2 = rv.dot(m.tangent2);
                Vec3 rtA2 = rA.cross(m.tangent2);
                Vec3 rtB2 = rB.cross(m.tangent2);
                float kT2 = invMassA + invMassB
                          + (invInertiaA * rtA2).dot(rtA2)
                          + (invInertiaB * rtB2).dot(rtB2);
                if (kT2 > TD_EPSILON) {
                    float jt2 = -vt2 / kT2;
                    float maxFriction = m.friction * cp.normalImpulse;
                    float oldT2 = cp.tangentImpulse2;
                    cp.tangentImpulse2 = clamp(oldT2 + jt2, -maxFriction, maxFriction);
                    jt2 = cp.tangentImpulse2 - oldT2;
                    Vec3 frictionImpulse2 = m.tangent2 * jt2;
                    bodyA.linearVelocity  -= frictionImpulse2 * invMassA;
                    bodyA.angularVelocity -= invInertiaA * rA.cross(frictionImpulse2);
                    bodyB.linearVelocity  += frictionImpulse2 * invMassB;
                    bodyB.angularVelocity += invInertiaB * rB.cross(frictionImpulse2);
                }
            }
        }
    }
}

void PhysicsWorld3D::integratePositions(float dt) {
    for (auto& pb : m_bodies) {
        if (pb.body.isStatic || pb.body.sleeping) continue;
        pb.body.position += pb.body.linearVelocity * dt;
        // Quaternion integration: q' = q + 0.5 * dt * (omega_quat * q)
        if (pb.body.angularVelocity.lengthSq() > TD_EPSILON) {
            Quat omegaQuat(pb.body.angularVelocity.x, pb.body.angularVelocity.y,
                           pb.body.angularVelocity.z, 0.0f);
            Quat qDot = omegaQuat * pb.body.orientation;
            qDot = qDot * (0.5f * dt);
            pb.body.orientation += qDot;
            pb.body.orientation.normalize();
        }
    }
}

// Position correction (Baumgarte stabilization):
//   correction = max(penetration - slop, 0) * percent / (invMassA + invMassB) * normal
//
// NOTE: we do NOT iterate here.  The penetration is stored at the start of
// the step (in detectCollisions) and is not recomputed during position
// correction — so iterating would apply the SAME correction multiple times
// and over-correct.  A single application of Baumgarte at 0.2 strength
// converges over a few frames without exploding.  (Box2D's NG solver
// recomputes penetration per iteration; we'd need that to iterate safely.)
void PhysicsWorld3D::solvePositionConstraints() {
    for (auto& m : m_contacts) {
        auto& bodyA = m_bodies[m.bodyA].body;
        auto& bodyB = m_bodies[m.bodyB].body;
        if (bodyA.isStatic && bodyB.isStatic) continue;
        float invMassA = bodyA.isStatic ? 0.0f : bodyA.inverseMass;
        float invMassB = bodyB.isStatic ? 0.0f : bodyB.inverseMass;
        float invMassSum = invMassA + invMassB;
        if (invMassSum < TD_EPSILON) continue;

        for (int p = 0; p < m.pointCount; p++) {
            ContactPoint3D& cp = m.points[p];
            float penetration = cp.penetration;
            float correction = maxF(penetration - m_slop, 0.0f)
                             * m_baumgarte / invMassSum;
            Vec3 correctionVec = cp.normal * correction;
            bodyA.position -= correctionVec * invMassA;
            bodyB.position += correctionVec * invMassB;
        }
    }
}

void PhysicsWorld3D::updateBroadphase() {
    // Nothing to do here — broadphase is rebuilt at the start of detectCollisions().
    // (This is wasteful for static bodies; future optimization: keep a separate
    // static broadphase that's only built once.)
}

void PhysicsWorld3D::updateSleeping(float dt) {
    for (auto& pb : m_bodies) {
        if (pb.body.isStatic) continue;
        if (pb.body.sleeping) continue;

        float lsq = pb.body.linearVelocity.lengthSq();
        float asq = pb.body.angularVelocity.lengthSq();
        float thresholdSq = pb.body.sleepThreshold * pb.body.sleepThreshold;
        if (lsq < thresholdSq && asq < thresholdSq) {
            pb.body.sleepTimer += dt;
            if (pb.body.sleepTimer >= pb.body.sleepTimeRequired) {
                pb.body.sleeping = true;
                pb.body.linearVelocity.setZero();
                pb.body.angularVelocity.setZero();
            }
        } else {
            pb.body.sleepTimer = 0.0f;
        }
    }
}

// =============================================================================
// Raycast
// =============================================================================
bool PhysicsWorld3D::raycast(const Vec3& origin, const Vec3& dir, float maxDist,
                              Vec3& outPoint, Vec3& outNormal,
                              int32_t& outBodyIndex) const {
    Vec3 d = dir.normalized();
    float closestT = maxDist;
    bool hit = false;

    for (int32_t i = 0; i < (int32_t)m_bodies.size(); i++) {
        const auto& pb = m_bodies[i];
        if (!pb.colliderSet) continue;

        // Quick AABB-vs-ray slab test
        float minX, minY, minZ, maxX, maxY, maxZ;
        pb.collider.computeWorldAABB(pb.body.position, pb.body.orientation,
                                       minX, minY, minZ, maxX, maxY, maxZ);
        float tmin = 0.0f, tmax = closestT;
        bool aabbHit = true;
        for (int axis = 0; axis < 3; axis++) {
            float o = (axis == 0 ? origin.x : axis == 1 ? origin.y : origin.z);
            float dd = (axis == 0 ? d.x : axis == 1 ? d.y : d.z);
            float lo = (axis == 0 ? minX : axis == 1 ? minY : minZ);
            float hi = (axis == 0 ? maxX : axis == 1 ? maxY : maxZ);
            if (absF(dd) < TD_EPSILON) {
                if (o < lo || o > hi) { aabbHit = false; break; }
            } else {
                float t1 = (lo - o) / dd;
                float t2 = (hi - o) / dd;
                if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
                if (t1 > tmin) tmin = t1;
                if (t2 < tmax) tmax = t2;
                if (tmin > tmax) { aabbHit = false; break; }
            }
        }
        if (!aabbHit) continue;

        // Per-shape precise raycast
        // For simplicity we currently use the AABB hit point + face normal.
        // A full implementation would do sphere/box/capsule-specific raycasts.
        // (TODO: precise per-shape raycasts.)
        if (tmin < closestT) {
            closestT = tmin;
            outPoint = origin + d * tmin;
            // Compute face normal — whichever slab we entered through
            Vec3 center((minX + maxX) * 0.5f, (minY + maxY) * 0.5f, (minZ + maxZ) * 0.5f);
            Vec3 toPt = outPoint - center;
            Vec3 half((maxX - minX) * 0.5f, (maxY - minY) * 0.5f, (maxZ - minZ) * 0.5f);
            Vec3 normalized(toPt.x / (half.x + TD_EPSILON),
                            toPt.y / (half.y + TD_EPSILON),
                            toPt.z / (half.z + TD_EPSILON));
            float ax = absF(normalized.x);
            float ay = absF(normalized.y);
            float az = absF(normalized.z);
            if (ax > ay && ax > az)      outNormal = Vec3(signF(normalized.x), 0, 0);
            else if (ay > az)            outNormal = Vec3(0, signF(normalized.y), 0);
            else                         outNormal = Vec3(0, 0, signF(normalized.z));
            outBodyIndex = i;
            hit = true;
        }
    }
    return hit;
}

} // namespace td
