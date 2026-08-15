// =============================================================================
// TD Engine - 3D Constraints Implementation
// =============================================================================
#include "constraints_3d.h"
#include "../core/math/mat3.h"

namespace td {

void ConstraintSolver3D::solve(std::vector<Constraint3D>& constraints,
                                 PhysicsWorld3D& world, int iterations) {
    for (int iter = 0; iter < iterations; iter++) {
        for (auto& c : constraints) {
            RigidBody3D& bodyA = world.getBody(c.bodyA).body;
            RigidBody3D& bodyB = world.getBody(c.bodyB).body;
            if (bodyA.isStatic && bodyB.isStatic) continue;

            float invMassA = bodyA.isStatic ? 0.0f : bodyA.inverseMass;
            float invMassB = bodyB.isStatic ? 0.0f : bodyB.inverseMass;
            Mat3 invInertiaA = bodyA.isStatic ? Mat3() : bodyA.worldInverseInertia();
            Mat3 invInertiaB = bodyB.isStatic ? Mat3() : bodyB.worldInverseInertia();

            // World-space anchor points
            Vec3 rA = bodyA.orientation.rotate(c.localAnchorA);
            Vec3 rB = bodyB.orientation.rotate(c.localAnchorB);
            Vec3 pA = bodyA.position + rA;
            Vec3 pB = bodyB.position + rB;

            if (c.type == ConstraintType3D::Distance) {
                // C = |pB - pA| - targetDistance = 0
                Vec3 delta = pB - pA;
                float dist = delta.length();
                if (dist < TD_EPSILON) continue;
                Vec3 n = delta / dist;

                // Velocity constraint: (vB + wB x rB - vA - wA x rA) . n = 0
                Vec3 vA = bodyA.linearVelocity + bodyA.angularVelocity.cross(rA);
                Vec3 vB = bodyB.linearVelocity + bodyB.angularVelocity.cross(rB);
                float relVel = (vB - vA).dot(n);

                // Effective mass along n
                Vec3 rnA = rA.cross(n);
                Vec3 rnB = rB.cross(n);
                float k = invMassA + invMassB
                        + (invInertiaA * rnA).dot(rnA)
                        + (invInertiaB * rnB).dot(rnB);
                if (k < TD_EPSILON) continue;

                // Position bias (Baumgarte) — drive toward target distance
                float C = dist - c.targetDistance;
                float bias = -0.2f * C;   // simple Baumgarte

                float lambda = -(relVel + bias) / k;
                Vec3 impulse = n * lambda;

                bodyA.linearVelocity  -= impulse * invMassA;
                bodyA.angularVelocity -= invInertiaA * rA.cross(impulse);
                bodyB.linearVelocity  += impulse * invMassB;
                bodyB.angularVelocity += invInertiaB * rB.cross(impulse);
            }
            else if (c.type == ConstraintType3D::Point) {
                // C = pB - pA = 0   (3 constraints, one per axis)
                // Jacobian: each row corresponds to an axis (x, y, z).
                // For each axis i:
                //   J_i = [ -I, -rA x e_i, +I, +rB x e_i ]  (where e_i is unit axis)
                // Effective mass is a 3x3 matrix K.
                Vec3 vA = bodyA.linearVelocity + bodyA.angularVelocity.cross(rA);
                Vec3 vB = bodyB.linearVelocity + bodyB.angularVelocity.cross(rB);
                Vec3 rv = vB - vA;

                // K = invMassA*I + invMassB*I - skew(rA) * invInertiaA * skew(rA)
                //                       - skew(rB) * invInertiaB * skew(rB)
                Mat3 skewA = Mat3::skew(rA);
                Mat3 skewB = Mat3::skew(rB);
                Mat3 K = Mat3::diagonal(invMassA + invMassB,
                                         invMassA + invMassB,
                                         invMassA + invMassB)
                       - skewA * invInertiaA * skewA.transposed()
                       - skewB * invInertiaB * skewB.transposed();
                Mat3 Kinv = K.inverse();

                // Position bias (Baumgarte)
                Vec3 C = pB - pA;
                Vec3 bias = C * -0.2f;

                Vec3 impulse = Kinv * -(rv + bias);
                bodyA.linearVelocity  -= impulse * invMassA;
                bodyA.angularVelocity -= invInertiaA * rA.cross(impulse);
                bodyB.linearVelocity  += impulse * invMassB;
                bodyB.angularVelocity += invInertiaB * rB.cross(impulse);
            }
            else if (c.type == ConstraintType3D::Hinge) {
                // Hinge = point constraint + 2 angular constraints (perpendicular
                // to the hinge axis).  The point part keeps anchors together.
                // The angular part keeps the relative rotation aligned to the axis.

                // First: point constraint (same as above)
                {
                    Vec3 vA = bodyA.linearVelocity + bodyA.angularVelocity.cross(rA);
                    Vec3 vB = bodyB.linearVelocity + bodyB.angularVelocity.cross(rB);
                    Vec3 rv = vB - vA;
                    Mat3 skewA = Mat3::skew(rA);
                    Mat3 skewB = Mat3::skew(rB);
                    Mat3 K = Mat3::diagonal(invMassA + invMassB,
                                             invMassA + invMassB,
                                             invMassA + invMassB)
                           - skewA * invInertiaA * skewA.transposed()
                           - skewB * invInertiaB * skewB.transposed();
                    Mat3 Kinv = K.inverse();
                    Vec3 C = pB - pA;
                    Vec3 bias = C * -0.2f;
                    Vec3 impulse = Kinv * -(rv + bias);
                    bodyA.linearVelocity  -= impulse * invMassA;
                    bodyA.angularVelocity -= invInertiaA * rA.cross(impulse);
                    bodyB.linearVelocity  += impulse * invMassB;
                    bodyB.angularVelocity += invInertiaB * rB.cross(impulse);
                }

                // Then: angular constraints — keep relative rotation only
                // around the hinge axis.  Compute the hinge axis in world space
                // for both bodies, find the perpendiculars in body A, and
                // require body B's hinge axis to align with body A's.
                Vec3 axisA_world = bodyA.orientation.rotate(c.hingeAxisA).normalized();
                Vec3 axisB_world = bodyB.orientation.rotate(c.hingeAxisB).normalized();

                // Pick two perpendiculars to axisA_world
                Vec3 u, v;
                if (absF(axisA_world.y) < 0.99f) {
                    u = axisA_world.cross(Vec3(0, 1, 0)).normalized();
                } else {
                    u = axisA_world.cross(Vec3(1, 0, 0)).normalized();
                }
                v = axisA_world.cross(u).normalized();

                // For each perpendicular: constraint is u . axisB_world = 0
                // Jacobian: J = [ 0, u, 0, -u ]
                // Angular velocity part:  u . (wB - wA) = 0
                Vec3 relAngVel = bodyB.angularVelocity - bodyA.angularVelocity;

                float k_u = (invInertiaA * u).dot(u) + (invInertiaB * u).dot(u);
                if (k_u > TD_EPSILON) {
                    float C_u = u.dot(axisB_world);
                    float bias_u = -0.2f * C_u;
                    float lambda_u = -(u.dot(relAngVel) + bias_u) / k_u;
                    Vec3 angImpulse = u * lambda_u;
                    bodyA.angularVelocity -= invInertiaA * angImpulse;
                    bodyB.angularVelocity += invInertiaB * angImpulse;
                }

                relAngVel = bodyB.angularVelocity - bodyA.angularVelocity;
                float k_v = (invInertiaA * v).dot(v) + (invInertiaB * v).dot(v);
                if (k_v > TD_EPSILON) {
                    float C_v = v.dot(axisB_world);
                    float bias_v = -0.2f * C_v;
                    float lambda_v = -(v.dot(relAngVel) + bias_v) / k_v;
                    Vec3 angImpulse = v * lambda_v;
                    bodyA.angularVelocity -= invInertiaA * angImpulse;
                    bodyB.angularVelocity += invInertiaB * angImpulse;
                }
            }
        }
    }
}

} // namespace td
