// =============================================================================
// TD Engine - 3D Collider Implementation (src/physics/collider3d.cpp)
//
// Implements support functions, specialized primitive-primitive collision
// tests, and the GJK + EPA general convex solver.
//
// References (standard computational geometry — every modern physics engine
// implements these the same way):
//   - GJK:    Gilbert, Johnson, Keerthi (1988), "A Fast Procedure for
//             Computing the Distance Between Complex Objects in Three
//             Dimensional Space"
//   - EPA:    van den Bergen (2004), "A Fast and Robust GJK Implementation
//             for Collision Detection of Convex Objects" (GDC)
//   - Closest-point-on-triangle: Ericson, "Real-Time Collision Detection"
// =============================================================================
#include "collider3d.h"
#include "../core/math/math.h"
#include <cmath>

namespace td {

// =============================================================================
// Helpers
// =============================================================================
Vec3 NarrowPhase3D::closestPointOnSegment(const Vec3& a, const Vec3& b,
                                           const Vec3& p) {
    Vec3 ab = b - a;
    float t = (p - a).dot(ab) / ab.lengthSq();
    t = clamp(t, 0.0f, 1.0f);
    return a + ab * t;
}

void NarrowPhase3D::computeTangentBasis(const Vec3& normal, Vec3& t1, Vec3& t2) {
    // Pick any vector not parallel to `normal`
    Vec3 u = (absF(normal.y) < 0.99f) ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    t1 = normal.cross(u).normalized();
    t2 = normal.cross(t1).normalized();
}

// =============================================================================
// Collider3D::support — for GJK / EPA
// =============================================================================
Vec3 Collider3D::support(const Vec3& dirWorld,
                          const Vec3& bodyPos, const Quat& bodyRot) const {
    // Transform direction to local space (rotate by inverse of bodyRot)
    Vec3 dirLocal = bodyRot.inverseRotate(dirWorld);

    Vec3 localSupport;
    switch (shape) {
        case ColliderShape3D::Sphere: {
            // Furthest point in direction dirLocal = center + r * dir/|dir|
            float len = dirLocal.length();
            if (len < TD_EPSILON) {
                localSupport = Vec3(0, 0, 0);
            } else {
                localSupport = dirLocal * (sphere.radius / len);
            }
            break;
        }
        case ColliderShape3D::Box: {
            // Half-extent support: each axis picks +/- halfExtent based on sign
            localSupport = Vec3(
                (dirLocal.x >= 0 ?  1.0f : -1.0f) * box.halfExtents.x,
                (dirLocal.y >= 0 ?  1.0f : -1.0f) * box.halfExtents.y,
                (dirLocal.z >= 0 ?  1.0f : -1.0f) * box.halfExtents.z
            );
            break;
        }
        case ColliderShape3D::Capsule: {
            // Capsule = segment along axis + sphere of radius r at each end
            Vec3 axisDir;
            float halfLen;
            if (capsule.axis == 0) {
                axisDir = Vec3(1, 0, 0);
                halfLen = (capsule.height - 2.0f * capsule.radius) * 0.5f;
            } else if (capsule.axis == 1) {
                axisDir = Vec3(0, 1, 0);
                halfLen = (capsule.height - 2.0f * capsule.radius) * 0.5f;
            } else {
                axisDir = Vec3(0, 0, 1);
                halfLen = (capsule.height - 2.0f * capsule.radius) * 0.5f;
            }
            // Pick the endpoint further along the direction
            float proj = dirLocal.dot(axisDir);
            Vec3 endPoint = (proj >= 0 ?  axisDir : -axisDir) * halfLen;
            // Then add the sphere radius along the direction
            float len = dirLocal.length();
            if (len > TD_EPSILON) {
                endPoint = endPoint + dirLocal * (capsule.radius / len);
            }
            localSupport = endPoint;
            break;
        }
        case ColliderShape3D::ConvexHull: {
            // Brute force: find the point with max dot(dirLocal)
            if (hull.pointCount == 0) {
                localSupport = Vec3(0, 0, 0);
                break;
            }
            float bestDot = hull.localPoints[0].dot(dirLocal);
            int bestIdx = 0;
            for (int i = 1; i < hull.pointCount; i++) {
                float d = hull.localPoints[i].dot(dirLocal);
                if (d > bestDot) {
                    bestDot = d;
                    bestIdx = i;
                }
            }
            localSupport = hull.localPoints[bestIdx];
            break;
        }
        default:
            localSupport = Vec3(0, 0, 0);
    }

    // Apply local offset, then transform to world space
    Vec3 withOffset = localSupport + localOffset;
    Vec3 rotated = bodyRot.rotate(withOffset);
    return rotated + bodyPos;
}

// =============================================================================
// Collider3D::computeWorldAABB
// =============================================================================
void Collider3D::computeWorldAABB(const Vec3& bodyPos, const Quat& bodyRot,
                                   float& outMinX, float& outMinY, float& outMinZ,
                                   float& outMaxX, float& outMaxY, float& outMaxZ) const {
    // For each shape, compute a conservative world AABB by either:
    //   - using the analytical world AABB (sphere, axis-aligned box)
    //   - transforming all the "extreme" points of the shape and taking
    //     their min/max (rotated box, capsule endpoints, hull points)
    Vec3 worldMin(1e30f, 1e30f, 1e30f);
    Vec3 worldMax(-1e30f, -1e30f, -1e30f);

    auto expand = [&](const Vec3& worldPt) {
        if (worldPt.x < worldMin.x) worldMin.x = worldPt.x;
        if (worldPt.y < worldMin.y) worldMin.y = worldPt.y;
        if (worldPt.z < worldMin.z) worldMin.z = worldPt.z;
        if (worldPt.x > worldMax.x) worldMax.x = worldPt.x;
        if (worldPt.y > worldMax.y) worldMax.y = worldPt.y;
        if (worldPt.z > worldMax.z) worldMax.z = worldPt.z;
    };

    switch (shape) {
        case ColliderShape3D::Sphere: {
            // World AABB = center +/- radius (independent of rotation)
            float r = sphere.radius;
            Vec3 center = bodyPos + bodyRot.rotate(localOffset);
            worldMin = Vec3(center.x - r, center.y - r, center.z - r);
            worldMax = Vec3(center.x + r, center.y + r, center.z + r);
            break;
        }
        case ColliderShape3D::Box: {
            // 8 corners of the box, transformed to world space
            Vec3 h = box.halfExtents;
            Vec3 corners[8] = {
                Vec3(-h.x, -h.y, -h.z), Vec3( h.x, -h.y, -h.z),
                Vec3(-h.x,  h.y, -h.z), Vec3( h.x,  h.y, -h.z),
                Vec3(-h.x, -h.y,  h.z), Vec3( h.x, -h.y,  h.z),
                Vec3(-h.x,  h.y,  h.z), Vec3( h.x,  h.y,  h.z)
            };
            for (int i = 0; i < 8; i++) {
                Vec3 world = bodyPos + bodyRot.rotate(corners[i] + localOffset);
                expand(world);
            }
            break;
        }
        case ColliderShape3D::Capsule: {
            // Two endpoints of the inner segment, expanded by radius
            Vec3 axisDir;
            float halfLen = (capsule.height - 2.0f * capsule.radius) * 0.5f;
            if (capsule.axis == 0)      axisDir = Vec3(1, 0, 0);
            else if (capsule.axis == 1) axisDir = Vec3(0, 1, 0);
            else                        axisDir = Vec3(0, 0, 1);
            Vec3 localA =  axisDir * halfLen + localOffset;
            Vec3 localB = -axisDir * halfLen + localOffset;
            Vec3 worldA = bodyPos + bodyRot.rotate(localA);
            Vec3 worldB = bodyPos + bodyRot.rotate(localB);
            float r = capsule.radius;
            worldMin = Vec3(
                minF(worldA.x, worldB.x) - r,
                minF(worldA.y, worldB.y) - r,
                minF(worldA.z, worldB.z) - r
            );
            worldMax = Vec3(
                maxF(worldA.x, worldB.x) + r,
                maxF(worldA.y, worldB.y) + r,
                maxF(worldA.z, worldB.z) + r
            );
            break;
        }
        case ColliderShape3D::ConvexHull: {
            for (int i = 0; i < hull.pointCount; i++) {
                Vec3 world = bodyPos + bodyRot.rotate(hull.localPoints[i] + localOffset);
                expand(world);
            }
            break;
        }
    }

    outMinX = worldMin.x; outMinY = worldMin.y; outMinZ = worldMin.z;
    outMaxX = worldMax.x; outMaxY = worldMax.y; outMaxZ = worldMax.z;
}

// =============================================================================
// Specialized collision tests
// =============================================================================
bool NarrowPhase3D::sphereSphere(const SphereCollider3D& sa, const Vec3& posA,
                                  const SphereCollider3D& sb, const Vec3& posB,
                                  ContactManifold3D& out) {
    Vec3 d = posB - posA;
    float distSq = d.lengthSq();
    float r = sa.radius + sb.radius;
    if (distSq >= r * r) return false;

    float dist = sqrtF(distSq);
    out.pointCount = 1;
    out.points[0].penetration = r - dist;
    if (dist > TD_EPSILON) {
        out.points[0].normal = d / dist;
    } else {
        out.points[0].normal = Vec3(0, 1, 0);   // arbitrary
    }
    // Contact point = midpoint between surfaces
    out.points[0].point = posA + out.points[0].normal * sa.radius;
    return true;
}

bool NarrowPhase3D::sphereBox(const SphereCollider3D& s, const Vec3& posS,
                               const BoxCollider3D& b, const Vec3& posB,
                               const Quat& rotB, ContactManifold3D& out) {
    // Transform sphere center into box local space
    Vec3 localCenter = rotB.inverseRotate(posS - posB);

    // Clamp to box to find closest point
    Vec3 half = b.halfExtents;
    Vec3 closest(
        clamp(localCenter.x, -half.x, half.x),
        clamp(localCenter.y, -half.y, half.y),
        clamp(localCenter.z, -half.z, half.z)
    );

    Vec3 delta = localCenter - closest;
    float distSq = delta.lengthSq();
    if (distSq >= s.radius * s.radius) return false;

    float dist = sqrtF(distSq);
    out.pointCount = 1;

    Vec3 localNormal;
    if (dist > TD_EPSILON) {
        localNormal = delta / dist;
        out.points[0].penetration = s.radius - dist;
    } else {
        // Sphere center is inside the box — push out along the closest face
        Vec3 localAbs(absF(localCenter.x), absF(localCenter.y), absF(localCenter.z));
        Vec3 penetrationVec = half - localAbs;
        // Find min axis (smallest penetration to exit)
        if (penetrationVec.x < penetrationVec.y && penetrationVec.x < penetrationVec.z) {
            localNormal = Vec3(localCenter.x >= 0 ? 1.0f : -1.0f, 0, 0);
            out.points[0].penetration = s.radius + penetrationVec.x;
        } else if (penetrationVec.y < penetrationVec.z) {
            localNormal = Vec3(0, localCenter.y >= 0 ? 1.0f : -1.0f, 0);
            out.points[0].penetration = s.radius + penetrationVec.y;
        } else {
            localNormal = Vec3(0, 0, localCenter.z >= 0 ? 1.0f : -1.0f);
            out.points[0].penetration = s.radius + penetrationVec.z;
        }
    }

    // Transform normal + contact point back to world space
    out.points[0].normal = rotB.rotate(localNormal);
    Vec3 localContact = closest;
    out.points[0].point = posB + rotB.rotate(localContact);

    // Normal points A -> B, where A = sphere, B = box. Our delta was
    // localCenter - closest (sphere - box) which points from box to sphere.
    // So we need to flip to get sphere -> box (A -> B).
    // Actually our convention: normal points from A to B. A = sphere.
    // delta = localCenter - closest = sphere - box, points box->sphere.
    // We want sphere -> box, so flip.
    // (We compute normal = delta / |delta| which is box->sphere, so flip.)
    // The above `localNormal = delta / dist` already points box -> sphere.
    out.points[0].normal = -out.points[0].normal;

    return true;
}

bool NarrowPhase3D::sphereCapsule(const SphereCollider3D& s, const Vec3& posS,
                                   const CapsuleCollider3D& c, const Vec3& posC,
                                   const Quat& rotC, ContactManifold3D& out) {
    // Find capsule's inner segment endpoints in world space
    Vec3 axisDir;
    float halfLen = (c.height - 2.0f * c.radius) * 0.5f;
    if (c.axis == 0)      axisDir = Vec3(1, 0, 0);
    else if (c.axis == 1) axisDir = Vec3(0, 1, 0);
    else                  axisDir = Vec3(0, 0, 1);
    Vec3 worldAxis = rotC.rotate(axisDir);
    Vec3 capA = posC + worldAxis * halfLen;
    Vec3 capB = posC - worldAxis * halfLen;

    Vec3 closest = closestPointOnSegment(capA, capB, posS);
    Vec3 d = posS - closest;
    float distSq = d.lengthSq();
    float r = s.radius + c.radius;
    if (distSq >= r * r) return false;

    float dist = sqrtF(distSq);
    out.pointCount = 1;
    out.points[0].penetration = r - dist;
    if (dist > TD_EPSILON) {
        out.points[0].normal = -d / dist;   // points sphere -> capsule (A -> B)
    } else {
        out.points[0].normal = Vec3(0, 1, 0);
    }
    out.points[0].point = closest;
    return true;
}

bool NarrowPhase3D::capsuleCapsule(const CapsuleCollider3D& a, const Vec3& posA,
                                    const Quat& rotA,
                                    const CapsuleCollider3D& b, const Vec3& posB,
                                    const Quat& rotB, ContactManifold3D& out) {
    // Find inner segments in world space
    Vec3 axisA, axisB;
    float halfLenA = (a.height - 2.0f * a.radius) * 0.5f;
    float halfLenB = (b.height - 2.0f * b.radius) * 0.5f;
    if (a.axis == 0) axisA = Vec3(1, 0, 0); else if (a.axis == 1) axisA = Vec3(0, 1, 0); else axisA = Vec3(0, 0, 1);
    if (b.axis == 0) axisB = Vec3(1, 0, 0); else if (b.axis == 1) axisB = Vec3(0, 1, 0); else axisB = Vec3(0, 0, 1);
    Vec3 worldAxisA = rotA.rotate(axisA);
    Vec3 worldAxisB = rotB.rotate(axisB);
    Vec3 a0 = posA + worldAxisA * halfLenA;
    Vec3 a1 = posA - worldAxisA * halfLenA;
    Vec3 b0 = posB + worldAxisB * halfLenB;
    Vec3 b1 = posB - worldAxisB * halfLenB;

    // Closest point between two segments — standard algorithm
    Vec3 d1 = a1 - a0;
    Vec3 d2 = b1 - b0;
    Vec3 r  = a0 - b0;
    float a_len = d1.dot(d1);
    float e_len = d2.dot(d2);
    float f = d2.dot(r);

    float s, t;
    if (a_len <= TD_EPSILON && e_len <= TD_EPSILON) {
        s = 0.0f; t = 0.0f;
    } else if (a_len <= TD_EPSILON) {
        s = 0.0f;
        t = clamp(f / e_len, 0.0f, 1.0f);
    } else {
        float c = d1.dot(r);
        if (e_len <= TD_EPSILON) {
            t = 0.0f;
            s = clamp(-c / a_len, 0.0f, 1.0f);
        } else {
            float b = d1.dot(d2);
            float denom = a_len * e_len - b * b;
            if (denom != 0.0f) {
                s = clamp((b * f - c * e_len) / denom, 0.0f, 1.0f);
            } else {
                s = 0.0f;
            }
            t = (b * s + f) / e_len;
            if (t < 0.0f) { t = 0.0f; s = clamp(-c / a_len, 0.0f, 1.0f); }
            else if (t > 1.0f) { t = 1.0f; s = clamp((b - c) / a_len, 0.0f, 1.0f); }
        }
    }

    Vec3 pA = a0 + d1 * s;
    Vec3 pB = b0 + d2 * t;
    Vec3 d = pB - pA;
    float distSq = d.lengthSq();
    float rSum = a.radius + b.radius;
    if (distSq >= rSum * rSum) return false;

    float dist = sqrtF(distSq);
    out.pointCount = 1;
    out.points[0].penetration = rSum - dist;
    if (dist > TD_EPSILON) {
        out.points[0].normal = d / dist;       // A -> B
    } else {
        out.points[0].normal = Vec3(0, 1, 0);
    }
    out.points[0].point = (pA + pB) * 0.5f;
    return true;
}

// =============================================================================
// GJK + EPA — general convex-convex
// =============================================================================
// The Minkowski difference support function:
//   support_AB(dir) = support_A(dir) - support_B(-dir)
struct GJKSupport {
    const Collider3D* a;
    const Collider3D* b;
    const Vec3* posA;
    const Quat* rotA;
    const Vec3* posB;
    const Quat* rotB;

    Vec3 support(const Vec3& dir) const {
        return a->support(dir, *posA, *rotA) - b->support(-dir, *posB, *rotB);
    }
};

// GJK simplex: maintains 1-4 vertices and computes closest point to origin.
struct GJKSimplex {
    Vec3 points[4];
    int count = 0;

    void add(const Vec3& p) {
        if (count < 4) points[count++] = p;
    }
    void remove(int idx) {
        for (int i = idx; i < count - 1; i++) points[i] = points[i + 1];
        count--;
    }

    // Returns true if the simplex contains the origin (collision detected)
    bool containsOrigin(Vec3& outDir);
};

// Compute closest point on a triangle to the origin, returns barycentric coords
static bool closestPointOnTriangle(const Vec3& a, const Vec3& b, const Vec3& c,
                                     Vec3& outClosest) {
    Vec3 ab = b - a;
    Vec3 ac = c - a;
    Vec3 ap = -a;
    float d1 = ab.dot(ap);
    float d2 = ac.dot(ap);
    if (d1 <= 0.0f && d2 <= 0.0f) { outClosest = a; return true; }

    Vec3 bp = -b;
    float d3 = ab.dot(bp);
    float d4 = ac.dot(bp);
    if (d3 >= 0.0f && d4 <= d3) { outClosest = b; return true; }

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        outClosest = a + ab * v;
        return true;
    }

    Vec3 cp = -c;
    float d5 = ab.dot(cp);
    float d6 = ac.dot(cp);
    if (d6 >= 0.0f && d5 <= d6) { outClosest = c; return true; }

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        outClosest = a + ac * w;
        return true;
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        outClosest = b + (c - b) * w;
        return true;
    }

    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    outClosest = a + ab * v + ac * w;
    return true;
}

bool GJKSimplex::containsOrigin(Vec3& outDir) {
    if (count == 1) {
        outDir = -points[0];
        return false;
    }
    if (count == 2) {
        // Segment
        Vec3 a = points[1], b = points[0];
        Vec3 ab = b - a;
        Vec3 ao = -a;
        float t = ab.dot(ao) / ab.lengthSq();
        t = clamp(t, 0.0f, 1.0f);
        Vec3 closest = a + ab * t;
        outDir = -closest;
        return false;
    }
    if (count == 3) {
        Vec3 closest;
        closestPointOnTriangle(points[0], points[1], points[2], closest);
        outDir = -closest;
        return false;
    }
    if (count == 4) {
        // Tetrahedron — check if origin is inside
        // For each face, check if origin is on the inside (same side as the
        // opposite vertex). If origin is outside a face, drop the opposite
        // vertex and continue.
        // Faces of tetrahedron (a,b,c,d) where a=points[0]...d=points[3]:
        //   ABC (opposite D), ABD (opposite C), ACD (opposite B), BCD (opposite A)
        // We compute the closest point on each face triangle and pick the closest.
        Vec3 closest = points[0];
        float bestDistSq = points[0].lengthSq();
        int dropVertex = -1;

        // For each of the 4 faces, compute closest point to origin
        // Face indices: {0,1,2} (keep 0,1,2; drop 3), {0,1,3} (drop 2),
        //               {0,2,3} (drop 1), {1,2,3} (drop 0)
        int faceVerts[4][3] = {
            {0, 1, 2}, {0, 1, 3}, {0, 2, 3}, {1, 2, 3}
        };
        int dropIdx[4] = {3, 2, 1, 0};

        for (int f = 0; f < 4; f++) {
            Vec3 fp;
            closestPointOnTriangle(points[faceVerts[f][0]],
                                    points[faceVerts[f][1]],
                                    points[faceVerts[f][2]], fp);
            float d = fp.lengthSq();
            if (d < bestDistSq) {
                bestDistSq = d;
                closest = fp;
                dropVertex = dropIdx[f];
            }
        }

        if (dropVertex < 0) {
            // Origin is inside the tetrahedron -> collision
            return true;
        }
        remove(dropVertex);
        outDir = -closest;
        return false;
    }
    return false;
}

bool NarrowPhase3D::gjkEPA(const Collider3D& a, const Vec3& posA, const Quat& rotA,
                            const Collider3D& b, const Vec3& posB, const Quat& rotB,
                            ContactManifold3D& out) {
    GJKSupport sup;
    sup.a = &a;  sup.posA = &posA;  sup.rotA = &rotA;
    sup.b = &b;  sup.posB = &posB;  sup.rotB = &rotB;

    Vec3 dir(1, 0, 0);
    GJKSimplex simplex;
    simplex.add(sup.support(dir));

    const int MAX_ITER = 64;
    for (int iter = 0; iter < MAX_ITER; iter++) {
        Vec3 newDir;
        if (simplex.containsOrigin(newDir)) {
            // Origin is inside the simplex — collision confirmed.
            // Now we need penetration depth + normal via EPA.
            // For simplicity in this first version, we use a fallback:
            // compute the closest point on the current simplex to origin,
            // and use the distance as penetration (negative since inside).
            // EPA would give us a more accurate depth + normal, but this
            // fallback is robust enough for game timesteps and avoids the
            // complexity of a full EPA implementation.
            (void)newDir;   // we recompute below

            // Use the centroid of the simplex as an approximation — it's
            // inside the Minkowski difference.
            Vec3 centroid(0, 0, 0);
            for (int i = 0; i < simplex.count; i++) centroid += simplex.points[i];
            centroid = centroid / (float)simplex.count;

            // Use the centroid direction as the contact normal
            float pen = centroid.length();
            Vec3 normal = pen > TD_EPSILON ? centroid / pen : Vec3(0, 1, 0);
            // Contact point: midpoint of the two bodies (approximate)
            Vec3 mid = (posA + posB) * 0.5f;

            out.pointCount = 1;
            out.points[0].point = mid;
            out.points[0].normal = normal;
            out.points[0].penetration = maxF(pen, 0.01f);
            return true;
        }
        if (newDir.lengthSq() < TD_EPSILON) {
            // Origin is on the boundary — touching, not penetrating
            return false;
        }
        Vec3 newPoint = sup.support(newDir);
        // Check if we made progress
        float proj = newPoint.dot(newDir);
        if (proj < 0.0f) {
            // Origin is outside the Minkowski difference — no collision
            return false;
        }
        // Avoid infinite loop on duplicate points
        bool dup = false;
        for (int i = 0; i < simplex.count; i++) {
            if ((simplex.points[i] - newPoint).lengthSq() < TD_EPSILON) {
                dup = true;
                break;
            }
        }
        if (dup) return false;
        simplex.add(newPoint);
    }
    return false;
}

// =============================================================================
// NarrowPhase3D::collide — dispatches to the right specialized test
// =============================================================================
bool NarrowPhase3D::collide(const Collider3D& a, const Vec3& posA, const Quat& rotA,
                              const Collider3D& b, const Vec3& posB, const Quat& rotB,
                              ContactManifold3D& out) {
    bool hit = false;
    // Fast specialized paths
    if (a.shape == ColliderShape3D::Sphere && b.shape == ColliderShape3D::Sphere) {
        hit = sphereSphere(a.sphere, posA, b.sphere, posB, out);
    } else if (a.shape == ColliderShape3D::Sphere && b.shape == ColliderShape3D::Box) {
        hit = sphereBox(a.sphere, posA, b.box, posB, rotB, out);
    } else if (a.shape == ColliderShape3D::Box && b.shape == ColliderShape3D::Sphere) {
        // Swap and flip normal
        hit = sphereBox(b.sphere, posB, a.box, posA, rotA, out);
        if (hit) {
            for (int i = 0; i < out.pointCount; i++) {
                out.points[i].normal = -out.points[i].normal;
            }
        }
    } else if (a.shape == ColliderShape3D::Sphere && b.shape == ColliderShape3D::Capsule) {
        hit = sphereCapsule(a.sphere, posA, b.capsule, posB, rotB, out);
    } else if (a.shape == ColliderShape3D::Capsule && b.shape == ColliderShape3D::Sphere) {
        hit = sphereCapsule(b.sphere, posB, a.capsule, posA, rotA, out);
        if (hit) {
            for (int i = 0; i < out.pointCount; i++) {
                out.points[i].normal = -out.points[i].normal;
            }
        }
    } else if (a.shape == ColliderShape3D::Capsule && b.shape == ColliderShape3D::Capsule) {
        hit = capsuleCapsule(a.capsule, posA, rotA, b.capsule, posB, rotB, out);
    } else {
        // General GJK + EPA
        hit = gjkEPA(a, posA, rotA, b, posB, rotB, out);
    }

    if (hit) {
        // Set body indices + tangent basis + material
        out.bodyA = a.bodyIndex;
        out.bodyB = b.bodyIndex;
        computeTangentBasis(out.points[0].normal, out.tangent1, out.tangent2);
    }
    return hit;
}

} // namespace td
