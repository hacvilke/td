// TD Engine - Math Unit Tests

#include "../src/core/math/math.h"
#include "../src/core/math/vec2.h"
#include "../src/core/math/vec3.h"
#include "../src/core/math/mat4.h"
#include <cstdio>
#include <cmath>

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

void testVec2() {
    printf("\n=== Vec2 Tests ===\n");
    
    // Default constructor
    Vec2 v1;
    TEST("Vec2 default constructor", v1.x == 0 && v1.y == 0);
    
    // Value constructor
    Vec2 v2(3, 4);
    TEST("Vec2 value constructor", v2.x == 3 && v2.y == 4);
    
    // Addition
    Vec2 v3 = Vec2(1, 2) + Vec2(3, 4);
    TEST("Vec2 addition", v3.x == 4 && v3.y == 6);
    
    // Subtraction
    Vec2 v4 = Vec2(5, 7) - Vec2(2, 3);
    TEST("Vec2 subtraction", v4.x == 3 && v4.y == 4);
    
    // Scalar multiplication
    Vec2 v5 = Vec2(2, 3) * 2.0f;
    TEST("Vec2 scalar multiply", v5.x == 4 && v5.y == 6);
    
    // Dot product
    float dot = Vec2(3, 4).dot(Vec2(1, 2));
    TEST("Vec2 dot product", EXPECT_NEAR(dot, 11.0f, TD_EPSILON));
    
    // Length
    float len = Vec2(3, 4).length();
    TEST("Vec2 length", EXPECT_NEAR(len, 5.0f, TD_EPSILON));
    
    // Normalized
    Vec2 norm = Vec2(3, 4).normalized();
    TEST("Vec2 normalized", EXPECT_NEAR(norm.x, 0.6f, TD_EPSILON) && 
                            EXPECT_NEAR(norm.y, 0.8f, TD_EPSILON));
    
    // Perpendicular
    Vec2 perp = Vec2(1, 0).perpendicular();
    TEST("Vec2 perpendicular", EXPECT_NEAR(perp.x, 0.0f, TD_EPSILON) && 
                               EXPECT_NEAR(perp.y, 1.0f, TD_EPSILON));
}

void testVec3() {
    printf("\n=== Vec3 Tests ===\n");
    
    // Default constructor
    Vec3 v1;
    TEST("Vec3 default constructor", v1.x == 0 && v1.y == 0 && v1.z == 0);
    
    // Value constructor
    Vec3 v2(1, 2, 3);
    TEST("Vec3 value constructor", v2.x == 1 && v2.y == 2 && v2.z == 3);
    
    // Cross product: (1,0,0) x (0,1,0) = (0,0,1)
    Vec3 cross = Vec3(1, 0, 0).cross(Vec3(0, 1, 0));
    TEST("Vec3 cross product", EXPECT_NEAR(cross.x, 0.0f, TD_EPSILON) &&
                               EXPECT_NEAR(cross.y, 0.0f, TD_EPSILON) &&
                               EXPECT_NEAR(cross.z, 1.0f, TD_EPSILON));
    
    // Dot product
    float dot = Vec3(1, 2, 3).dot(Vec3(4, 5, 6));
    TEST("Vec3 dot product", EXPECT_NEAR(dot, 32.0f, TD_EPSILON));
    
    // Length
    float len = Vec3(2, 3, 6).length();
    TEST("Vec3 length", EXPECT_NEAR(len, 7.0f, TD_EPSILON));
    
    // Normalized
    Vec3 norm = Vec3(0, 3, 4).normalized();
    TEST("Vec3 normalized", EXPECT_NEAR(norm.y, 0.6f, TD_EPSILON) &&
                            EXPECT_NEAR(norm.z, 0.8f, TD_EPSILON));
}

void testMat4() {
    printf("\n=== Mat4 Tests ===\n");
    
    // Identity
    Mat4 identity = Mat4::identity();
    TEST("Mat4 identity diagonal", identity(0,0) == 1 && identity(1,1) == 1 &&
                                   identity(2,2) == 1 && identity(3,3) == 1);
    
    // Identity * vector = vector
    Vec4 v(1, 2, 3, 1);
    Vec4 result = identity * v;
    TEST("Mat4 identity * vec", EXPECT_NEAR(result.x, 1.0f, TD_EPSILON) &&
                                EXPECT_NEAR(result.y, 2.0f, TD_EPSILON) &&
                                EXPECT_NEAR(result.z, 3.0f, TD_EPSILON));
    
    // Translation
    Mat4 trans = Mat4::translate(5, 10, 15);
    Vec4 p(0, 0, 0, 1);
    Vec4 translated = trans * p;
    TEST("Mat4 translate", EXPECT_NEAR(translated.x, 5.0f, TD_EPSILON) &&
                           EXPECT_NEAR(translated.y, 10.0f, TD_EPSILON) &&
                           EXPECT_NEAR(translated.z, 15.0f, TD_EPSILON));
    
    // Scale
    Mat4 scale = Mat4::scale(2, 3, 4);
    Vec4 s(1, 1, 1, 1);
    Vec4 scaled = scale * s;
    TEST("Mat4 scale", EXPECT_NEAR(scaled.x, 2.0f, TD_EPSILON) &&
                       EXPECT_NEAR(scaled.y, 3.0f, TD_EPSILON) &&
                       EXPECT_NEAR(scaled.z, 4.0f, TD_EPSILON));
    
    // Rotation Y by PI/2 rotates X to Z
    Mat4 rotY = Mat4::rotateY(TD_PI / 2.0f);
    Vec4 xAxis(1, 0, 0, 0);
    Vec4 rotated = rotY * xAxis;
    TEST("Mat4 rotateY", EXPECT_NEAR(rotated.x, 0.0f, 0.001f) &&
                         EXPECT_NEAR(rotated.z, -1.0f, 0.001f));
    
    // Matrix multiplication
    Mat4 a = Mat4::translate(1, 2, 3);
    Mat4 b = Mat4::scale(2, 2, 2);
    Mat4 ab = a * b;
    Vec4 test(1, 1, 1, 1);
    Vec4 abResult = ab * test;
    // Should scale first then translate: (1,1,1)*2 = (2,2,2), then +(1,2,3) = (3,4,5)
    TEST("Mat4 multiplication", EXPECT_NEAR(abResult.x, 3.0f, TD_EPSILON) &&
                                EXPECT_NEAR(abResult.y, 4.0f, TD_EPSILON) &&
                                EXPECT_NEAR(abResult.z, 5.0f, TD_EPSILON));
    
    // Inverse
    Mat4 t = Mat4::translate(5, 10, 15);
    Mat4 tInv = t.inverse();
    Mat4 shouldBeIdentity = t * tInv;
    TEST("Mat4 inverse", EXPECT_NEAR(shouldBeIdentity(0,0), 1.0f, 0.001f) &&
                         EXPECT_NEAR(shouldBeIdentity(1,1), 1.0f, 0.001f) &&
                         EXPECT_NEAR(shouldBeIdentity(3,0), 0.0f, 0.001f));
}

void testMathFunctions() {
    printf("\n=== Math Functions Tests ===\n");
    
    // Clamp
    TEST("clamp lower", EXPECT_NEAR(clamp(-5.0f, 0.0f, 10.0f), 0.0f, TD_EPSILON));
    TEST("clamp upper", EXPECT_NEAR(clamp(15.0f, 0.0f, 10.0f), 10.0f, TD_EPSILON));
    TEST("clamp middle", EXPECT_NEAR(clamp(5.0f, 0.0f, 10.0f), 5.0f, TD_EPSILON));
    
    // Lerp
    TEST("lerp 0.0", EXPECT_NEAR(lerp(0.0f, 10.0f, 0.0f), 0.0f, TD_EPSILON));
    TEST("lerp 1.0", EXPECT_NEAR(lerp(0.0f, 10.0f, 1.0f), 10.0f, TD_EPSILON));
    TEST("lerp 0.5", EXPECT_NEAR(lerp(0.0f, 10.0f, 0.5f), 5.0f, TD_EPSILON));
    
    // Deg/Rad conversion
    TEST("degToRad 180", EXPECT_NEAR(degToRad(180.0f), TD_PI, 0.001f));
    TEST("radToDeg PI", EXPECT_NEAR(radToDeg(TD_PI), 180.0f, 0.001f));
}

int main() {
    printf("TD Engine Math Tests\n");
    printf("====================\n");
    
    testVec2();
    testVec3();
    testMat4();
    testMathFunctions();
    
    printf("\n====================\n");
    printf("Results: %d passed, %d failed\n", g_testsPassed, g_testsFailed);
    
    return g_testsFailed > 0 ? 1 : 0;
}
