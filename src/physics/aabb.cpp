#include "aabb.h"
#include "../core/math/math.h"

namespace td {

bool AABB::overlaps(const AABB& other) const {
    // Not overlapping if separated along any axis
    if (maxX <= other.minX || minX >= other.maxX) return false;
    if (maxY <= other.minY || minY >= other.maxY) return false;
    return true;
}

bool AABB::contains(const AABB& other) const {
    return minX <= other.minX && maxX >= other.maxX &&
           minY <= other.minY && maxY >= other.maxY;
}

bool AABB::containsPoint(float x, float y) const {
    return x >= minX && x <= maxX && y >= minY && y <= maxY;
}

AABB AABB::intersection(const AABB& other) const {
    AABB result;
    result.minX = maxF(minX, other.minX);
    result.minY = maxF(minY, other.minY);
    result.maxX = minF(maxX, other.maxX);
    result.maxY = minF(maxY, other.maxY);
    return result;
}

AABB AABB::merged(const AABB& other) const {
    AABB result;
    result.minX = minF(minX, other.minX);
    result.minY = minF(minY, other.minY);
    result.maxX = maxF(maxX, other.maxX);
    result.maxY = maxF(maxY, other.maxY);
    return result;
}

void AABB::expand(float amount) {
    minX -= amount;
    minY -= amount;
    maxX += amount;
    maxY += amount;
}

void AABB::translate(float dx, float dy) {
    minX += dx;
    minY += dy;
    maxX += dx;
    maxY += dy;
}

void AABB::setCenter(float cx, float cy) {
    float hw = width() * 0.5f;
    float hh = height() * 0.5f;
    minX = cx - hw;
    minY = cy - hh;
    maxX = cx + hw;
    maxY = cy + hh;
}

void AABB::setSize(float w, float h) {
    Vec2 c = center();
    float hw = w * 0.5f;
    float hh = h * 0.5f;
    minX = c.x - hw;
    minY = c.y - hh;
    maxX = c.x + hw;
    maxY = c.y + hh;
}

// ==================== AABB3D ====================

bool AABB3D::overlaps(const AABB3D& other) const {
    if (maxX <= other.minX || minX >= other.maxX) return false;
    if (maxY <= other.minY || minY >= other.maxY) return false;
    if (maxZ <= other.minZ || minZ >= other.maxZ) return false;
    return true;
}

bool AABB3D::containsPoint(float x, float y, float z) const {
    return x >= minX && x <= maxX && 
           y >= minY && y <= maxY &&
           z >= minZ && z <= maxZ;
}

Vec3 AABB3D::center() const {
    return Vec3(
        (minX + maxX) * 0.5f,
        (minY + maxY) * 0.5f,
        (minZ + maxZ) * 0.5f
    );
}

Vec3 AABB3D::size() const {
    return Vec3(maxX - minX, maxY - minY, maxZ - minZ);
}

float AABB3D::volume() const {
    return (maxX - minX) * (maxY - minY) * (maxZ - minZ);
}

} // namespace td
