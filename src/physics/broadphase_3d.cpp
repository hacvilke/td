// =============================================================================
// TD Engine - 3D Broadphase Implementation
// =============================================================================
#include "broadphase_3d.h"
#include <algorithm>

namespace td {

void Broadphase3D::clear() {
    m_aabbs.clear();
    m_dirty = true;
}

void Broadphase3D::addBody(int32_t bodyIndex, const Vec3& min, const Vec3& max,
                            bool isStatic) {
    BroadphaseAABB3D aabb;
    aabb.minX = min.x; aabb.minY = min.y; aabb.minZ = min.z;
    aabb.maxX = max.x; aabb.maxY = max.y; aabb.maxZ = max.z;
    aabb.bodyIndex = bodyIndex;
    aabb.isStatic = isStatic;
    m_aabbs.push_back(aabb);
    m_dirty = true;
}

void Broadphase3D::updateBody(int32_t bodyIndex, const Vec3& min, const Vec3& max) {
    for (auto& aabb : m_aabbs) {
        if (aabb.bodyIndex == bodyIndex) {
            aabb.minX = min.x; aabb.minY = min.y; aabb.minZ = min.z;
            aabb.maxX = max.x; aabb.maxY = max.y; aabb.maxZ = max.z;
            m_dirty = true;
            return;
        }
    }
}

void Broadphase3D::sortIfNeeded() const {
    if (!m_dirty) return;
    // Sort by minX ascending.  For nearly-sorted input this is fast.
    std::sort(m_aabbs.begin(), m_aabbs.end(),
              [](const BroadphaseAABB3D& a, const BroadphaseAABB3D& b) {
                  return a.minX < b.minX;
              });
    m_dirty = false;
}

int Broadphase3D::computePairs(BroadphasePair3D* outPairs, int maxPairs) const {
    sortIfNeeded();

    int pairCount = 0;
    int n = (int)m_aabbs.size();
    for (int i = 0; i < n; i++) {
        const BroadphaseAABB3D& a = m_aabbs[i];
        // Sweep forward along X
        for (int j = i + 1; j < n; j++) {
            const BroadphaseAABB3D& b = m_aabbs[j];
            // If b's minX > a's maxX, no further overlaps possible
            if (b.minX > a.maxX) break;
            // Skip if both are static (no point testing static-static pairs
            // every frame — they're already in the persistent pair list)
            if (a.isStatic && b.isStatic) continue;
            // Y / Z overlap test
            if (a.maxY < b.minY || a.minY > b.maxY) continue;
            if (a.maxZ < b.minZ || a.minZ > b.maxZ) continue;
            if (pairCount < maxPairs) {
                outPairs[pairCount].a = a.bodyIndex;
                outPairs[pairCount].b = b.bodyIndex;
                pairCount++;
            }
        }
    }
    return pairCount;
}

int Broadphase3D::queryAABB(const Vec3& min, const Vec3& max,
                              int32_t* outIndices, int maxResults) const {
    sortIfNeeded();
    int count = 0;
    for (const auto& aabb : m_aabbs) {
        if (aabb.maxX < min.x || aabb.minX > max.x) continue;
        if (aabb.maxY < min.y || aabb.minY > max.y) continue;
        if (aabb.maxZ < min.z || aabb.minZ > max.z) continue;
        if (count < maxResults) {
            outIndices[count++] = aabb.bodyIndex;
        }
    }
    return count;
}

} // namespace td
