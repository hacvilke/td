// =============================================================================
// TD Engine - Character Controller Unit Tests (Task wave1-physaudio)
//
// Verifies the 6 required behaviors:
//   1. Empty world: character falls under gravity, lands at y=0 (ground plane).
//   2. Wall in front: character moving forward stops at wall, doesn't penetrate.
//   3. Slope 30° (below limit): character walks up.
//   4. Slope 60° (above limit): character slides back down.
//   5. Step height 0.3 m: walks up a 0.25 m step (success); blocked by a
//      0.5 m step (fail).
//   6. Jump: grounded character jumps, lands back on ground after gravity arc.
//
// Also tests movement modes (walk vs run speed) as a 7th assertion set.
//
// Build:
//   g++ -std=c++17 -Wall -Wextra -O2 -Isrc  tests/test_character_controller.cpp
//       src/physics/character_controller.cpp  src/physics/aabb.cpp
//       -o /tmp/test_character_controller
// =============================================================================
#include "physics/character_controller_3d.h"
#include "physics/aabb.h"
#include "core/math/math.h"
#include <cstdio>
#include <cmath>

using namespace td;

static int g_passed = 0;
static int g_failed = 0;

#define TEST(name, cond)                                              \
    do {                                                             \
        if (cond) {                                                  \
            std::printf("PASS: %s\n", name);                         \
            g_passed++;                                              \
        } else {                                                     \
            std::printf("FAIL: %s  (file %s, line %d)\n",            \
                         name, __FILE__, __LINE__);                  \
            g_failed++;                                              \
        }                                                            \
    } while (0)

#define EXPECT_NEAR(a, b, eps) (absF((a) - (b)) < (eps))

// Run the controller for `seconds` at 60 Hz, optionally setting wishDir each frame.
static void runFor(CharacterController3D& cc, const CollisionWorld3D& world,
                    float seconds, const Vec3& wishDir) {
    const float dt = 1.0f / 60.0f;
    int   frames = (int)(seconds / dt);
    for (int i = 0; i < frames; i++) {
        cc.wishDir = wishDir;
        cc.update(dt, world);
    }
}

// =============================================================================
int main() {
    const float eps = 0.05f;   // 5 cm tolerance — generous for 60 Hz sim
    const float dt   = 1.0f / 60.0f;

    // -------------------------------------------------------------------------
    // Test 1: Empty world — character falls under gravity, lands on ground.
    // -------------------------------------------------------------------------
    {
        SimpleCollisionWorld3D world;   // hasGroundPlane = true by default
        CharacterController3D cc;
        cc.teleport(Vec3(0, 5.0f, 0));  // start 5 m above ground
        // Run for 2 s — plenty of time to fall 4.1 m and settle.
        runFor(cc, world, 2.0f, Vec3(0, 0, 0));

        float feetY = cc.getPosition().y - cc.getCurrentHeight() * 0.5f;
        TEST("1a. Empty world: character falls and lands at y=0 (feet)",
             EXPECT_NEAR(feetY, 0.0f, eps));
        TEST("1b. Empty world: character is grounded after landing",
             cc.isGrounded());
        TEST("1c. Empty world: velocity is zero after landing",
             EXPECT_NEAR(cc.getVelocity().y, 0.0f, 0.1f));
    }

    // -------------------------------------------------------------------------
    // Test 2: Wall in front — character stops at wall, doesn't penetrate.
    // -------------------------------------------------------------------------
    {
        SimpleCollisionWorld3D world;
        // Wall: 1 m thick, 5 m tall, centered at x=2.
        world.addAABB(AABB3D(1.5f, 0.0f, -0.5f, 2.5f, 5.0f, 0.5f));

        CharacterController3D cc;
        cc.teleport(Vec3(0, 0.9f, 0));
        // Walk forward (+X) for 1 s.
        runFor(cc, world, 1.0f, Vec3(1, 0, 0));

        float x = cc.getPosition().x;
        // Wall starts at x=1.5; capsule radius is 0.4 → character center
        // should stop at x ≈ 1.1 (touching but not penetrating).
        TEST("2a. Wall: character stops before wall (x < 1.5)",
             x < 1.5f - 0.01f);
        TEST("2b. Wall: character reaches the wall (x > 0.9)",
             x > 0.9f);
        TEST("2c. Wall: character is grounded",
             cc.isGrounded());
    }

    // -------------------------------------------------------------------------
    // Test 3: Slope 30° (below limit) — character walks up.
    // -------------------------------------------------------------------------
    {
        SimpleCollisionWorld3D world;
        // 30° slope rising in +X. tan(30°) ≈ 0.577.
        Slope s;
        s.xMin = 2.0f; s.xMax = 12.0f;
        s.zMin = -2.0f; s.zMax = 2.0f;
        s.baseY = 0.0f;
        s.riseX = tanF(degToRad(30.0f));   // 0.577
        s.riseZ = 0.0f;
        world.addSlope(s);

        CharacterController3D cc;
        cc.teleport(Vec3(0, 0.9f, 0));
        runFor(cc, world, 3.0f, Vec3(1, 0, 0));

        // Character should have walked up the slope. After 3 s at ~5 m/s,
        // they've crossed the slope (which ends at x=12). Final X should be
        // well past 5, and Y should be > 1 (capsule center above the start
        // height of 0.9 — at x=5, slope height = 0.577*3 = 1.73, so capsule
        // center ≈ 1.73 + 0.9 = 2.63).
        TEST("3a. Slope 30°: character walks UP (X > 5)",
             cc.getPosition().x > 5.0f);
        TEST("3b. Slope 30°: character's Y increased (Y > 1)",
             cc.getPosition().y > 1.0f);
    }

    // -------------------------------------------------------------------------
    // Test 4: Slope 60° (above limit) — character slides back down.
    // -------------------------------------------------------------------------
    {
        SimpleCollisionWorld3D world;
        // 60° slope rising in +X. tan(60°) ≈ 1.732.
        Slope s;
        s.xMin = 2.0f; s.xMax = 12.0f;
        s.zMin = -2.0f; s.zMax = 2.0f;
        s.baseY = 0.0f;
        s.riseX = tanF(degToRad(60.0f));   // 1.732
        s.riseZ = 0.0f;
        world.addSlope(s);

        CharacterController3D cc;
        cc.teleport(Vec3(0, 0.9f, 0));
        runFor(cc, world, 3.0f, Vec3(1, 0, 0));

        // Character should NOT have climbed the slope. After 3 s they should
        // be at or before the slope's base (x ≤ 2.5 — small overshoot allowed
        // for the slide-back-and-forth oscillation). Critically, they must
        // not reach the top half of the slope (x > 7).
        TEST("4a. Slope 60°: character does NOT climb to top (X < 7)",
             cc.getPosition().x < 7.0f);
        TEST("4b. Slope 60°: character stays near slope base (X < 3)",
             cc.getPosition().x < 3.0f);
    }

    // -------------------------------------------------------------------------
    // Test 5: Step handling — 0.25 m step (success), 0.5 m step (blocked).
    // -------------------------------------------------------------------------
    {
        // 5a. 0.25 m step — character walks up.
        {
            SimpleCollisionWorld3D world;
            // Wide step (x = 1.5 .. 20) so the character stays on top
            // during the 2 s test run (no walking off the far edge).
            world.addAABB(AABB3D(1.5f, 0.0f, -1.0f, 20.0f, 0.25f, 1.0f));

            CharacterController3D cc;
            cc.stepHeight = 0.35f;   // > 0.25, so step is climbable
            cc.teleport(Vec3(0, 0.9f, 0));
            runFor(cc, world, 2.0f, Vec3(1, 0, 0));

            // Character should be on TOP of the step (x > 1.5, Y > 0.9).
            // Capsule center on top of step = 0.25 (step height) + 0.9 (half
            // capsule) = 1.15 m.
            TEST("5a. Step 0.25m: character climbs on top (X > 1.5)",
                 cc.getPosition().x > 1.5f);
            TEST("5a. Step 0.25m: character is higher (Y > 1.0)",
                 cc.getPosition().y > 1.0f);
        }

        // 5b. 0.5 m step — character is blocked.
        {
            SimpleCollisionWorld3D world;
            world.addAABB(AABB3D(1.5f, 0.0f, -1.0f, 20.0f, 0.5f, 1.0f));

            CharacterController3D cc;
            cc.stepHeight = 0.35f;   // < 0.5, so step is NOT climbable
            cc.teleport(Vec3(0, 0.9f, 0));
            runFor(cc, world, 2.0f, Vec3(1, 0, 0));

            // Character should be BLOCKED at the step (x ≈ 1.1, NOT on top).
            TEST("5b. Step 0.5m: character is blocked (X < 1.5)",
                 cc.getPosition().x < 1.5f);
            TEST("5b. Step 0.5m: character NOT on top (Y < 1.0)",
                 cc.getPosition().y < 1.0f);
        }
    }

    // -------------------------------------------------------------------------
    // Test 6: Jump — grounded character jumps, lands back on ground.
    // -------------------------------------------------------------------------
    {
        SimpleCollisionWorld3D world;
        CharacterController3D cc;
        cc.teleport(Vec3(0, 0.9f, 0));
        // Settle for 0.2 s to ensure grounded.
        runFor(cc, world, 0.2f, Vec3(0, 0, 0));
        TEST("6a. Jump: character is grounded before jump",
             cc.isGrounded());

        // Jump.
        cc.jump();
        float yAtJump = cc.getPosition().y;

        // Track max Y during the jump arc.
        float maxY = yAtJump;
        for (int i = 0; i < 60 * 2; i++) {  // 2 s
            cc.wishDir = Vec3(0, 0, 0);
            cc.update(dt, world);
            if (cc.getPosition().y > maxY) maxY = cc.getPosition().y;
        }

        // Character should have risen (jump apex > start).
        // jumpSpeed=7, gravity=19.6 → apex height = 7²/(2×19.6) ≈ 1.25 m.
        TEST("6b. Jump: character rises at least 0.5 m above start",
             maxY - yAtJump > 0.5f);
        // After 2 s, character should be back on ground.
        TEST("6c. Jump: character lands back on ground (feet ≈ 0)",
             EXPECT_NEAR(cc.getPosition().y - cc.getCurrentHeight() * 0.5f,
                          0.0f, eps));
        TEST("6d. Jump: character is grounded after landing",
             cc.isGrounded());
    }

    // -------------------------------------------------------------------------
    // Test 7 (bonus): Movement modes — run is 2× walk speed, crouch is 0.5×.
    // -------------------------------------------------------------------------
    {
        SimpleCollisionWorld3D world;
        // No obstacles — just measure horizontal speed.

        // Walk.
        CharacterController3D ccWalk;
        ccWalk.movementMode = CharacterController3D::MovementMode::Walk;
        ccWalk.teleport(Vec3(0, 0.9f, 0));
        ccWalk.maxSpeed = 5.0f;
        // Accelerate to terminal vel.
        runFor(ccWalk, world, 0.5f, Vec3(1, 0, 0));
        float walkSpeed = sqrtF(ccWalk.getVelocity().x * ccWalk.getVelocity().x +
                                 ccWalk.getVelocity().z * ccWalk.getVelocity().z);

        // Run.
        CharacterController3D ccRun;
        ccRun.movementMode = CharacterController3D::MovementMode::Run;
        ccRun.teleport(Vec3(0, 0.9f, 0));
        ccRun.maxSpeed = 5.0f;
        runFor(ccRun, world, 0.5f, Vec3(1, 0, 0));
        float runSpeed = sqrtF(ccRun.getVelocity().x * ccRun.getVelocity().x +
                                ccRun.getVelocity().z * ccRun.getVelocity().z);

        // Crouch.
        CharacterController3D ccCrouch;
        ccCrouch.movementMode = CharacterController3D::MovementMode::Crouch;
        ccCrouch.teleport(Vec3(0, 0.9f, 0));
        ccCrouch.maxSpeed = 5.0f;
        runFor(ccCrouch, world, 0.5f, Vec3(1, 0, 0));
        float crouchSpeed = sqrtF(ccCrouch.getVelocity().x * ccCrouch.getVelocity().x +
                                    ccCrouch.getVelocity().z * ccCrouch.getVelocity().z);

        TEST("7a. Walk speed ≈ maxSpeed (5)",
             EXPECT_NEAR(walkSpeed, 5.0f, 0.5f));
        TEST("7b. Run speed ≈ 2× maxSpeed (10)",
             EXPECT_NEAR(runSpeed, 10.0f, 0.5f));
        TEST("7c. Crouch speed ≈ 0.5× maxSpeed (2.5)",
             EXPECT_NEAR(crouchSpeed, 2.5f, 0.5f));
        TEST("7d. Run is faster than walk",
             runSpeed > walkSpeed * 1.5f);
        TEST("7e. Crouch is slower than walk",
             crouchSpeed < walkSpeed * 0.7f);
    }

    // -------------------------------------------------------------------------
    // Summary.
    // -------------------------------------------------------------------------
    std::printf("\n========================================\n");
    std::printf(" Character Controller Tests: %d passed, %d failed\n",
                g_passed, g_failed);
    std::printf("========================================\n");
    return g_failed == 0 ? 0 : 1;
}
