// =============================================================================
// TD Engine - 3D Broadphase (src/physics/broadphase_3d.h)
//
// Sweep-and-Prune (a.k.a. sort-and-sweep) on the X axis + AABB overlap test
// for Y and Z.  This is O(n log n) per frame for sort + O(n + k) for sweep
// where k is the number of overlapping pairs.
//
// For static / dynamic separation we maintain two arrays: one for static
// bodies (sorted once, never re-sorted) and one for dynamic bodies
// (re-sorted every frame with insertion sort, which is O(n) for nearly-sorted
// input — the common case for physics).
//
// References: Ericson, "Real-Time Collision Detection", chap. 2.
// =============================================================================
#pragma once
#include "../core/math/vec3.h"
#include "rigidbody3d.h"
#include "collider3d.h"
#include <vector>
#include <cstdint>

namespace td {

struct BroadphaseAABB3D {
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    int32_t bodyIndex;
    bool isStatic;
};

struct BroadphasePair3D {
    int32_t a;
    int32_t b;
};

class Broadphase3D {
public:
    void clear();
    void addBody(int32_t bodyIndex, const Vec3& min, const Vec3& max, bool isStatic);
    void updateBody(int32_t bodyIndex, const Vec3& min, const Vec3& max);

    // Returns the list of potentially-colliding pairs (broadphase candidate set).
    // The narrow phase will filter out false positives.
    int computePairs(BroadphasePair3D* outPairs, int maxPairs) const;

    // For raycasting / region queries (used by character controllers)
    int queryAABB(const Vec3& min, const Vec3& max,
                  int32_t* outIndices, int maxResults) const;

    int bodyCount() const { return (int)m_aabbs.size(); }

private:
    // We store all AABBs in a single array.  We sort by minX each frame.
    // Insertion sort works well because the order rarely changes much
    // between frames.
    mutable std::vector<BroadphaseAABB3D> m_aabbs;
    mutable bool m_dirty = true;

    void sortIfNeeded() const;
};

} // namespace td
