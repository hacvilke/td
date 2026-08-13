// TD Engine - Physics Unit Tests

#include "../src/physics/aabb.h"
#include "../src/physics/collision.h"
#include "../src/physics/rigidbody.h"
#include "../src/core/math/math.h"
#include <cstdio>

using namespace td;

int g_testsPassed = 0;
int g_testsFailed = 0;

#define TEST(name, condition) \
    do { \
        if (condition) { \
            printf("PASS: %s\n", name); \
            g_testsPassed++; \
        } else { \
            printf("FAIL: %s\n", name); \
            g_testsFailed++; \
        } \
    } while(0)

#define EXPECT_NEAR(a, b, epsilon) (absF((a) - (b)) < (epsilon))

void testAABB() {
    printf("\n=== AABB Tests ===\n");
    
    // Overlapping AABBs
    AABB a(0, 0, 10, 10);
    AABB b(5, 5, 15, 15);
    TEST("AABB overlap true", a.overlaps(b));
    
    // Non-overlapping AABBs
    AABB c(20, 20, 30, 30);
    TEST("AABB overlap false", !a.overlaps(c));
    
    // Touching edges (not overlapping)
    AABB d(10, 10, 20, 20);
    TEST("AABB touching edges", !a.overlaps(d));
    
    // Contains
    AABB outer(0, 0, 10, 10);
    AABB inner(2, 2, 8, 8);
    TEST("AABB contains true", outer.contains(inner));
    TEST("AABB contains false", !inner.contains(outer));
    
    // Contains point
    TEST("AABB containsPoint inside", outer.containsPoint(5, 5));
    TEST("AABB containsPoint outside", !outer.containsPoint(15, 5));
    TEST("AABB containsPoint edge", outer.containsPoint(0, 0));
    
    // Size and center
    AABB e(10, 20, 30, 50);
    TEST("AABB width", EXPECT_NEAR(e.width(), 20.0f, TD_EPSILON));
    TEST("AABB height", EXPECT_NEAR(e.height(), 30.0f, TD_EPSILON));
    Vec2 center = e.center();
    TEST("AABB center", EXPECT_NEAR(center.x, 20.0f, TD_EPSILON) && 
                        EXPECT_NEAR(center.y, 35.0f, TD_EPSILON));
    
    // Merged
    AABB f(0, 0, 5, 5);
    AABB g(10, 10, 15, 15);
    AABB merged = f.merged(g);
    TEST("AABB merged", merged.minX == 0 && merged.minY == 0 &&
                        merged.maxX == 15 && merged.maxY == 15);
}

void testCollisionDetection() {
    printf("\n=== Collision Detection Tests ===\n");
    
    CollisionDetector detector;
    
    // AABB collision with normal and penetration
    AABB a(0, 0, 10, 10);
    AABB b(8, 0, 18, 10);
    
    CollisionResult result = detector.testAABB(a, b);
    TEST("Collision detected", result.colliding);
    TEST("Penetration positive", result.penetration > 0);
    
    // Normal should point in separation direction
    TEST("Normal valid", EXPECT_NEAR(absF(result.normalX) + absF(result.normalY), 1.0f, TD_EPSILON));
    
    // Non-collision
    AABB c(0, 0, 10, 10);
    AABB d(20, 0, 30, 10);
    
    CollisionResult result2 = detector.testAABB(c, d);
    TEST("No collision", !result2.colliding);
    
    // Circle collision
    Vec2 circleA(0, 0);
    Vec2 circleB(3, 0);
    
    CollisionResult circleResult = detector.testCircles(circleA, 5.0f, circleB, 5.0f);
    TEST("Circle collision", circleResult.colliding);
    TEST("Circle penetration", EXPECT_NEAR(circleResult.penetration, 7.0f, 0.01f));
    
    // Non-colliding circles
    CollisionResult circleResult2 = detector.testCircles(circleA, 2.0f, Vec2(10, 0), 2.0f);
    TEST("Circle no collision", !circleResult2.colliding);
    
    // Point tests
    TEST("Point in AABB", detector.testPointAABB(Vec2(5, 5), AABB(0, 0, 10, 10)));
    TEST("Point outside AABB", !detector.testPointAABB(Vec2(15, 5), AABB(0, 0, 10, 10)));
    TEST("Point in circle", detector.testPointCircle(Vec2(0, 0), Vec2(1, 1), 5.0f));
    TEST("Point outside circle", !detector.testPointCircle(Vec2(10, 10), Vec2(0, 0), 5.0f));
}

void testRigidBody() {
    printf("\n=== RigidBody Tests ===\n");
    
    RigidBody body;
    body.setMass(2.0f);
    body.useGravity = true;
    body.position = Vec2(0, 0);
    body.velocity = Vec2(0, 0);
    
    TEST("Mass set", EXPECT_NEAR(body.mass, 2.0f, TD_EPSILON));
    TEST("Inverse mass", EXPECT_NEAR(body.inverseMass, 0.5f, TD_EPSILON));
    
    // Apply force and integrate
    body.applyForce(Vec2(20, 0));
    body.integrate(1.0f);
    
    // F = ma, so a = F/m = 20/2 = 10 (but gravity also applies)
    // With gravity: ay = -9.81 (from constant), ax = 10
    // After 1 second: vx = 10, vy = -9.81 (approximately)
    TEST("Velocity after force X", body.velocity.x > 0);
    
    // Static body shouldn't move
    RigidBody staticBody;
    staticBody.isStatic = true;
    staticBody.position = Vec2(100, 100);
    staticBody.applyForce(Vec2(1000, 1000));
    staticBody.integrate(1.0f);
    TEST("Static body position", staticBody.position.x == 100 && staticBody.position.y == 100);
    
    // Impulse
    RigidBody impBody;
    impBody.setMass(1.0f);
    impBody.velocity = Vec2(0, 0);
    impBody.applyImpulse(Vec2(10, 5));
    TEST("Impulse applied", EXPECT_NEAR(impBody.velocity.x, 10.0f, TD_EPSILON) &&
                            EXPECT_NEAR(impBody.velocity.y, 5.0f, TD_EPSILON));
}

void testCollisionResolution() {
    printf("\n=== Collision Resolution Tests ===\n");
    
    CollisionDetector detector;
    
    // Two bodies colliding
    RigidBody a, b;
    a.setMass(1.0f);
    b.setMass(1.0f);
    a.position = Vec2(0, 0);
    b.position = Vec2(8, 0);
    a.velocity = Vec2(10, 0);  // Moving right
    b.velocity = Vec2(-10, 0); // Moving left
    a.restitution = 1.0f;
    b.restitution = 1.0f;
    
    AABB aabbA = AABB::fromCenter(a.position.x, a.position.y, 10, 10);
    AABB aabbB = AABB::fromCenter(b.position.x, b.position.y, 10, 10);
    
    CollisionResult result = detector.testAABB(aabbA, aabbB);
    TEST("Pre-resolution collision", result.colliding);
    
    detector.resolveCollisionWithCorrection(a, b, result);
    
    // After elastic collision, velocities should reverse (approximately)
    TEST("A velocity reversed", a.velocity.x < 0);
    TEST("B velocity reversed", b.velocity.x > 0);
}

void testSpatialHash() {
    printf("\n=== Spatial Hash Tests ===\n");
    
    SpatialHash hash;
    hash.setCellSize(64.0f);
    hash.clear();
    
    // Insert objects
    for (int i = 0; i < 100; i++) {
        float x = (float)(i % 10) * 100;
        float y = (float)(i / 10) * 100;
        AABB bounds = AABB::fromMinSize(x, y, 32, 32);
        hash.insert(i, bounds);
    }
    
    // Query a region
    int results[100];
    AABB queryRegion(50, 50, 250, 250);
    int count = hash.query(queryRegion, results, 100);
    
    TEST("Spatial hash query count", count > 0);
    TEST("Spatial hash not too many", count < 50);
    
    // Get potential pairs
    CollisionPair pairs[1000];
    int pairCount = hash.getPotentialPairs(pairs, 1000);
    TEST("Spatial hash pairs found", pairCount > 0);
}

int main() {
    printf("TD Engine Physics Tests\n");
    printf("========================\n");
    
    testAABB();
    testCollisionDetection();
    testRigidBody();
    testCollisionResolution();
    testSpatialHash();
    
    printf("\n========================\n");
    printf("Results: %d passed, %d failed\n", g_testsPassed, g_testsFailed);
    
    return g_testsFailed > 0 ? 1 : 0;
}
