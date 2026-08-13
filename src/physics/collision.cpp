#include "collision.h"
#include "../core/math/math.h"
#include <cstring>

namespace td {

CollisionResult CollisionDetector::testAABB(const AABB& a, const AABB& b) const {
    CollisionResult result;
    
    // Calculate overlap on each axis
    float overlapX = minF(a.maxX, b.maxX) - maxF(a.minX, b.minX);
    float overlapY = minF(a.maxY, b.maxY) - maxF(a.minY, b.minY);
    
    // If any overlap is negative, no collision
    if (overlapX <= 0 || overlapY <= 0) {
        return result;
    }
    
    result.colliding = true;
    
    // Find minimum translation vector (MTV)
    Vec2 centerA = a.center();
    Vec2 centerB = b.center();
    
    if (overlapX < overlapY) {
        // Separate on X axis
        result.penetration = overlapX;
        if (centerA.x < centerB.x) {
            result.normalX = -1.0f;
            result.normalY = 0.0f;
        } else {
            result.normalX = 1.0f;
            result.normalY = 0.0f;
        }
    } else {
        // Separate on Y axis
        result.penetration = overlapY;
        if (centerA.y < centerB.y) {
            result.normalX = 0.0f;
            result.normalY = -1.0f;
        } else {
            result.normalX = 0.0f;
            result.normalY = 1.0f;
        }
    }
    
    // Calculate contact point (center of overlap region)
    AABB overlap = a.intersection(b);
    result.contactPoint = overlap.center();
    
    return result;
}

CollisionResult CollisionDetector::testCircles(const Vec2& posA, float radiusA,
                                                const Vec2& posB, float radiusB) const {
    CollisionResult result;
    
    Vec2 diff = posB - posA;
    float distSq = diff.lengthSq();
    float radiusSum = radiusA + radiusB;
    
    if (distSq >= radiusSum * radiusSum) {
        return result;
    }
    
    result.colliding = true;
    
    float dist = sqrtF(distSq);
    
    if (dist > TD_EPSILON) {
        result.penetration = radiusSum - dist;
        Vec2 normal = diff / dist;
        result.normalX = normal.x;
        result.normalY = normal.y;
        result.contactPoint = posA + normal * radiusA;
    } else {
        // Circles are at the same position
        result.penetration = radiusSum;
        result.normalX = 1.0f;
        result.normalY = 0.0f;
        result.contactPoint = posA;
    }
    
    return result;
}

CollisionResult CollisionDetector::testAABBCircle(const AABB& aabb, 
                                                   const Vec2& circlePos,
                                                   float radius) const {
    CollisionResult result;
    
    // Find closest point on AABB to circle center
    Vec2 closest;
    closest.x = clamp(circlePos.x, aabb.minX, aabb.maxX);
    closest.y = clamp(circlePos.y, aabb.minY, aabb.maxY);
    
    Vec2 diff = circlePos - closest;
    float distSq = diff.lengthSq();
    
    if (distSq >= radius * radius) {
        return result;
    }
    
    result.colliding = true;
    result.contactPoint = closest;
    
    float dist = sqrtF(distSq);
    
    if (dist > TD_EPSILON) {
        result.penetration = radius - dist;
        Vec2 normal = diff / dist;
        result.normalX = normal.x;
        result.normalY = normal.y;
    } else {
        // Circle center is inside AABB
        // Find the closest edge
        Vec2 center = aabb.center();
        Vec2 toCenter = circlePos - center;
        Vec2 halfSize = aabb.size() * 0.5f;
        
        float dx = halfSize.x - absF(toCenter.x);
        float dy = halfSize.y - absF(toCenter.y);
        
        if (dx < dy) {
            result.normalX = signF(toCenter.x);
            result.normalY = 0;
            result.penetration = dx + radius;
        } else {
            result.normalX = 0;
            result.normalY = signF(toCenter.y);
            result.penetration = dy + radius;
        }
    }
    
    return result;
}

bool CollisionDetector::testPointAABB(const Vec2& point, const AABB& aabb) const {
    return aabb.containsPoint(point.x, point.y);
}

bool CollisionDetector::testPointCircle(const Vec2& point, const Vec2& circlePos,
                                         float radius) const {
    return point.distanceToSq(circlePos) < radius * radius;
}

bool CollisionDetector::raycastAABB(const Vec2& origin, const Vec2& direction,
                                     const AABB& aabb, float& outT, 
                                     Vec2& outNormal) const {
    Vec2 invDir(1.0f / direction.x, 1.0f / direction.y);
    
    float tx1 = (aabb.minX - origin.x) * invDir.x;
    float tx2 = (aabb.maxX - origin.x) * invDir.x;
    float ty1 = (aabb.minY - origin.y) * invDir.y;
    float ty2 = (aabb.maxY - origin.y) * invDir.y;
    
    float tmin = maxF(minF(tx1, tx2), minF(ty1, ty2));
    float tmax = minF(maxF(tx1, tx2), maxF(ty1, ty2));
    
    if (tmax < 0 || tmin > tmax) {
        return false;
    }
    
    outT = tmin < 0 ? tmax : tmin;
    
    // Calculate hit normal
    if (tmin == tx1) outNormal = Vec2(-1, 0);
    else if (tmin == tx2) outNormal = Vec2(1, 0);
    else if (tmin == ty1) outNormal = Vec2(0, -1);
    else outNormal = Vec2(0, 1);
    
    return true;
}

void CollisionDetector::resolveCollision(RigidBody& a, RigidBody& b,
                                          const CollisionResult& result) {
    if (!result.colliding) return;
    if (a.isStatic && b.isStatic) return;
    if (a.isTrigger || b.isTrigger) return;
    
    Vec2 normal = result.getNormal();
    
    // Calculate relative velocity
    Vec2 relVel = b.velocity - a.velocity;
    float velAlongNormal = relVel.dot(normal);
    
    // Don't resolve if objects are separating
    if (velAlongNormal > 0) return;
    
    // Calculate restitution (use minimum)
    float e = minF(a.restitution, b.restitution);
    
    // Calculate impulse scalar
    float invMassSum = a.inverseMass + b.inverseMass;
    if (invMassSum == 0) return;
    
    float j = -(1.0f + e) * velAlongNormal / invMassSum;
    
    // Apply impulse
    Vec2 impulse = normal * j;
    
    if (!a.isStatic) {
        a.velocity = a.velocity - impulse * a.inverseMass;
    }
    if (!b.isStatic) {
        b.velocity = b.velocity + impulse * b.inverseMass;
    }
    
    // Apply friction
    Vec2 tangent = relVel - normal * velAlongNormal;
    float tangentLen = tangent.length();
    
    if (tangentLen > TD_EPSILON) {
        tangent = tangent / tangentLen;
        
        float jt = -relVel.dot(tangent) / invMassSum;
        
        // Coulomb friction
        float mu = (a.friction + b.friction) * 0.5f;
        
        Vec2 frictionImpulse;
        if (absF(jt) < j * mu) {
            frictionImpulse = tangent * jt;
        } else {
            frictionImpulse = tangent * (-j * mu);
        }
        
        if (!a.isStatic) {
            a.velocity = a.velocity - frictionImpulse * a.inverseMass;
        }
        if (!b.isStatic) {
            b.velocity = b.velocity + frictionImpulse * b.inverseMass;
        }
    }
}

void CollisionDetector::resolveCollisionWithCorrection(RigidBody& a, RigidBody& b,
                                                        const CollisionResult& result,
                                                        float positionCorrectionPercent,
                                                        float slop) {
    resolveCollision(a, b, result);
    
    if (!result.colliding) return;
    if (a.isStatic && b.isStatic) return;
    if (a.isTrigger || b.isTrigger) return;
    
    // Positional correction to prevent sinking
    Vec2 normal = result.getNormal();
    float invMassSum = a.inverseMass + b.inverseMass;
    
    if (invMassSum == 0) return;
    
    float correction = maxF(result.penetration - slop, 0.0f) / invMassSum * positionCorrectionPercent;
    Vec2 correctionVec = normal * correction;
    
    if (!a.isStatic) {
        a.position = a.position - correctionVec * a.inverseMass;
    }
    if (!b.isStatic) {
        b.position = b.position + correctionVec * b.inverseMass;
    }
}

// ==================== SpatialHash ====================

void SpatialHash::clear() {
    for (int i = 0; i < HASH_SIZE; i++) {
        m_cells[i].count = 0;
    }
    m_entryCount = 0;
}

void SpatialHash::setCellSize(float size) {
    m_cellSize = size;
    m_invCellSize = 1.0f / size;
}

int SpatialHash::hashPosition(float x, float y) const {
    int ix = (int)(x * m_invCellSize);
    int iy = (int)(y * m_invCellSize);
    // Simple hash combining x and y
    int hash = ((ix * 73856093) ^ (iy * 19349663)) % HASH_SIZE;
    if (hash < 0) hash += HASH_SIZE;
    return hash;
}

void SpatialHash::insertIntoCell(int cellIndex, int objectId) {
    Cell& cell = m_cells[cellIndex];
    if (cell.count < MAX_PER_CELL) {
        cell.objectIds[cell.count++] = objectId;
    }
}

void SpatialHash::insert(int objectId, const AABB& bounds) {
    if (m_entryCount >= MAX_OBJECTS) return;
    
    m_entries[m_entryCount].objectId = objectId;
    m_entries[m_entryCount].bounds = bounds;
    m_entryCount++;
    
    // Insert into all cells the AABB overlaps
    int minCellX = (int)(bounds.minX * m_invCellSize);
    int maxCellX = (int)(bounds.maxX * m_invCellSize);
    int minCellY = (int)(bounds.minY * m_invCellSize);
    int maxCellY = (int)(bounds.maxY * m_invCellSize);
    
    for (int cy = minCellY; cy <= maxCellY; cy++) {
        for (int cx = minCellX; cx <= maxCellX; cx++) {
            int hash = ((cx * 73856093) ^ (cy * 19349663)) % HASH_SIZE;
            if (hash < 0) hash += HASH_SIZE;
            insertIntoCell(hash, objectId);
        }
    }
}

int SpatialHash::query(const AABB& bounds, int* outIds, int maxResults) const {
    int count = 0;
    bool checked[MAX_OBJECTS] = {};
    
    int minCellX = (int)(bounds.minX * m_invCellSize);
    int maxCellX = (int)(bounds.maxX * m_invCellSize);
    int minCellY = (int)(bounds.minY * m_invCellSize);
    int maxCellY = (int)(bounds.maxY * m_invCellSize);
    
    for (int cy = minCellY; cy <= maxCellY; cy++) {
        for (int cx = minCellX; cx <= maxCellX; cx++) {
            int hash = ((cx * 73856093) ^ (cy * 19349663)) % HASH_SIZE;
            if (hash < 0) hash += HASH_SIZE;
            
            const Cell& cell = m_cells[hash];
            for (int i = 0; i < cell.count; i++) {
                int objId = cell.objectIds[i];
                
                if (!checked[objId]) {
                    checked[objId] = true;
                    
                    // Find entry and check actual AABB overlap
                    for (int e = 0; e < m_entryCount; e++) {
                        if (m_entries[e].objectId == objId) {
                            if (bounds.overlaps(m_entries[e].bounds)) {
                                if (count < maxResults) {
                                    outIds[count++] = objId;
                                }
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    
    return count;
}

int SpatialHash::getPotentialPairs(CollisionPair* outPairs, int maxPairs) const {
    int pairCount = 0;
    bool checked[MAX_OBJECTS][MAX_OBJECTS] = {};
    
    for (int i = 0; i < HASH_SIZE; i++) {
        const Cell& cell = m_cells[i];
        
        // Check all pairs within this cell
        for (int a = 0; a < cell.count; a++) {
            for (int b = a + 1; b < cell.count; b++) {
                int idA = cell.objectIds[a];
                int idB = cell.objectIds[b];
                
                // Ensure idA < idB for consistent ordering
                if (idA > idB) {
                    int tmp = idA;
                    idA = idB;
                    idB = tmp;
                }
                
                if (!checked[idA][idB]) {
                    checked[idA][idB] = true;
                    
                    if (pairCount < maxPairs) {
                        outPairs[pairCount].entityA = idA;
                        outPairs[pairCount].entityB = idB;
                        pairCount++;
                    }
                }
            }
        }
    }
    
    return pairCount;
}

} // namespace td
