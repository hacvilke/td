// =============================================================================
// TD Engine - 3D Constraints (src/physics/constraints_3d.h)
//
// Constraints restrict the motion of bodies relative to each other.  Each
// constraint defines a function C(x) that should equal zero; the solver
// applies impulses to drive C toward zero.
//
// Implemented constraints:
//   - DistanceConstraint3D   : keeps two anchor points at a fixed distance
//                              (e.g., rope, chain, rigid rod)
//   - PointConstraint3D       : keeps two anchor points coincident (welded)
//   - HingeConstraint3D       : welds two anchor points AND restricts
//                              rotation to a single axis (e.g., door hinge,
//                              wheel axle, ragdoll joints)
//
// All constraints use the same sequential impulse framework as the contact
// solver — they're solved alongside contact constraints in the velocity
// correction phase.  This is the same approach used by Bullet and ODE.
//
// Constraint derivation (Lagrange multiplier method, simplified):
//   1. Define C(x) = 0
//   2. Compute the velocity constraint: dC/dt = J * v = 0, where J is the
//      Jacobian and v is the velocity vector of both bodies.
//   3. Compute the effective mass: K = J * M^-1 * J^T
//   4. Compute the impulse: lambda = -J*v / K
//   5. Apply the impulse: v += M^-1 * J^T * lambda
//
// References: Catto, "Iterative Dynamics with Temporal Coherence" (GDC 2005).
// =============================================================================
#pragma once
#include "physics_world_3d.h"

namespace td {

class ConstraintSolver3D {
public:
    // Solve velocity constraints for the given list of constraints.
    // Bodies are read/written via the world.
    void solve(std::vector<Constraint3D>& constraints,
               PhysicsWorld3D& world, int iterations);
};

} // namespace td
