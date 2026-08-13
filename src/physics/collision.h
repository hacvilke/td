#pragma once
#include "aabb.h"
#include "rigidbody.h"

namespace td {

struct CollisionResult {
    bool colliding = false;
    float normalX = 0;
    float normalY = 0;
    float penetration = 0;
    Vec2 contactPoint;
    
    Vec2 getNormal() const { return Vec2(normalX, normalY); }
};

struct CollisionPair {
    int entityA = -1;
    int entityB = -1;
    CollisionResult result;
};

class CollisionDetector {
public:
    // AABB vs AABB
    CollisionResult testAABB(const AABB& a, const AABB& b) const;
    
    // Circle vs Circle
    CollisionResult testCircles(const Vec2& posA, float radiusA,
                                 const Vec2& posB, float radiusB) const;
    
    // AABB vs Circle
    CollisionResult testAABBCircle(const AABB& aabb, const Vec2& circlePos, 
                                    float radius) const;
    
    // Point vs AABB
    bool testPointAABB(const Vec2& point, const AABB& aabb) const;
    
    // Point vs Circle
    bool testPointCircle(const Vec2& point, const Vec2& circlePos, 
                         float radius) const;
    
    // Ray vs AABB
    bool raycastAABB(const Vec2& origin, const Vec2& direction,
                     const AABB& aabb, float& outT, Vec2& outNormal) const;
    
    // Resolve collision between two rigid bodies
    void resolveCollision(RigidBody& a, RigidBody& b, 
                          const CollisionResult& result);
    
    // Resolve collision with position correction
    void resolveCollisionWithCorrection(RigidBody& a, RigidBody& b,
                                        const CollisionResult& result,
                                        float positionCorrectionPercent = 0.2f,
                                        float slop = 0.01f);
};

// Spatial hash for broad phase collision detection
class SpatialHash {
public:
    static const int MAX_OBJECTS = 1024;
    static const int HASH_SIZE = 256;
    static const int MAX_PER_CELL = 16;
    
    struct Entry {
        int objectId;
        AABB bounds;
    };
    
    struct Cell {
        int objectIds[MAX_PER_CELL];
        int count;
    };
    
    void clear();
    void setCellSize(float size);
    void insert(int objectId, const AABB& bounds);
    
    // Query all objects that might overlap with the given bounds
    int query(const AABB& bounds, int* outIds, int maxResults) const;
    
    // Get potential collision pairs (broad phase)
    int getPotentialPairs(CollisionPair* outPairs, int maxPairs) const;
    
private:
    int hashPosition(float x, float y) const;
    void insertIntoCell(int cellIndex, int objectId);
    
    Cell m_cells[HASH_SIZE];
    Entry m_entries[MAX_OBJECTS];
    int m_entryCount = 0;
    float m_cellSize = 64.0f;
    float m_invCellSize = 1.0f / 64.0f;
};

} // namespace td
