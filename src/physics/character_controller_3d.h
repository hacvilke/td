// =============================================================================
// TD Engine - 3D Character Controller v2 (Task wave1-physaudio)
//
// Real capsule-based kinematic character controller with swept collision,
// sliding, step-up, ground detection, slope handling, and movement modes
// (walk / run / crouch / slide). Sits alongside the existing
// `src/physics/character_controller.h` (the Tier 1.4 skeleton), which is
// kept byte-identical per the task constraints. This new header declares the
// production API.
//
// Design influences:
//   - Godot's CharacterBody3D (slide + floor detection)
//   - Unity's CharacterController (capsule + step offset)
//   - Eric Lengyel's "3D Math Primer" SAT formulation
//
// The controller is KINEMATIC — it does NOT participate in the rigidbody
// impulse solver. It moves itself via swept queries against an
// `CollisionWorld3D` interface (which gameplay code implements for voxels,
// meshes, or simple AABB lists). This is what Minecraft, Roblox, and most
// FPS character controllers actually do.
//
// File layout:
//   - This header declares CharacterController3D + CollisionWorld3D +
//     SimpleCollisionWorld3D + Slope helper.
//   - src/physics/character_controller.cpp implements everything.
// =============================================================================
#pragma once
#include "../core/math/vec3.h"
#include "../core/math/math.h"
#include "../physics/aabb.h"
#include <cstdint>

namespace td {

// -----------------------------------------------------------------------------
// CollisionWorld3D — interface the character controller queries each frame.
//
// Two query types:
//   1. queryAABBs()  — returns all axis-aligned boxes that overlap the query
//                      AABB. Used for swept capsule-vs-AABB collision.
//   2. raycast()     — closest intersection of a ray with the world
//                      (AABB tops + slopes + an optional ground plane).
//                      Used for ground detection (downward ray) + ground
//                      normal lookup (for slope-limit tests).
//
// Gameplay code can implement this directly, or use SimpleCollisionWorld3D
// (below) which is a concrete list-of-AABBs + list-of-slopes impl that's
// good enough for tests and simple games.
// -----------------------------------------------------------------------------
class CollisionWorld3D {
public:
    virtual ~CollisionWorld3D() = default;

    // Return up to `maxAabbs` AABBs that overlap `query`. Returns count.
    virtual int queryAABBs(const AABB3D& query, AABB3D* outAabbs,
                            int maxAabbs) const = 0;

    // Raycast against the world. Returns true if anything was hit within
    // maxDist. outPoint and outNormal are filled with the closest hit.
    // The normal points AWAY from the surface (toward the ray origin).
    virtual bool raycast(const Vec3& origin, const Vec3& dir, float maxDist,
                         Vec3& outPoint, Vec3& outNormal) const = 0;
};

// -----------------------------------------------------------------------------
// Slope — an inclined rectangular patch (for the slope-limit tests + simple
// outdoor terrain). The patch lies in the XZ plane and rises by `riseX` per
// unit X and `riseZ` per unit Z. The surface normal is
//   normalize(-riseX, 1, -riseZ)
// so a flat patch has normal (0, 1, 0) and a 45° slope rising in +X has
// normal (-1, 1, 0) normalized → (-0.707, 0.707, 0).
// -----------------------------------------------------------------------------
struct Slope {
    float xMin = 0, xMax = 0;
    float zMin = 0, zMax = 0;
    float baseY = 0;     // Y at (xMin, zMin)
    float riseX = 0;     // dY/dX
    float riseZ = 0;     // dY/dZ

    bool contains(float x, float z) const {
        return x >= xMin && x <= xMax && z >= zMin && z <= zMax;
    }
    float heightAt(float x, float z) const {
        return baseY + (x - xMin) * riseX + (z - zMin) * riseZ;
    }
    Vec3 normal() const {
        // Cross product of tangent vectors gives the surface normal.
        // Tangent in X: (1, riseX, 0); tangent in Z: (0, riseZ, 1).
        // Cross = (riseX*1 - 0*riseZ, 0*0 - 1*1, 1*riseZ - riseX*0)
        //       = (riseX, -1, riseZ) — that points DOWN. Flip sign:
        Vec3 n(-riseX, 1.0f, -riseZ);
        return n.normalized();
    }
};

// -----------------------------------------------------------------------------
// SimpleCollisionWorld3D — concrete CollisionWorld3D for tests + simple games.
// Holds an explicit list of AABBs and Slopes + an optional ground plane at
// y=0. queryAABBs does a linear scan; raycast checks AABB tops, slopes, and
// the ground plane, returning the closest hit.
// -----------------------------------------------------------------------------
class SimpleCollisionWorld3D : public CollisionWorld3D {
public:
    static const int MAX_AABBS  = 256;
    static const int MAX_SLOPES = 16;

    AABB3D aabbs[MAX_AABBS];
    int    aabbCount = 0;

    Slope  slopes[MAX_SLOPES];
    int    slopeCount = 0;

    bool   hasGroundPlane = true;  // infinite plane at y=0, normal (0,1,0)

    void clear() {
        aabbCount = 0;
        slopeCount = 0;
        hasGroundPlane = true;
    }

    void addAABB(const AABB3D& a) {
        if (aabbCount < MAX_AABBS) aabbs[aabbCount++] = a;
    }
    void addSlope(const Slope& s) {
        if (slopeCount < MAX_SLOPES) slopes[slopeCount++] = s;
    }

    // CollisionWorld3D
    int queryAABBs(const AABB3D& query, AABB3D* outAabbs,
                    int maxAabbs) const override;
    bool raycast(const Vec3& origin, const Vec3& dir, float maxDist,
                 Vec3& outPoint, Vec3& outNormal) const override;
};

// -----------------------------------------------------------------------------
// CharacterController3D
//
// The controller's "position" is the CENTER of the capsule. The capsule is
// Y-aligned: a cylinder of height (height - 2*radius) capped with two
// hemispheres of radius `radius` at the top and bottom.
//
// Each frame:
//   controller.wishDir = ...;     // normalized desired move direction
//   controller.wishJump = ...;
//   controller.movementMode = ...;
//   controller.update(dt, world);
//   Vec3 p = controller.getPosition();
// -----------------------------------------------------------------------------
class CharacterController3D {
public:
    // ---- Capsule + physics tuning -------------------------------------------
    float radius           = 0.4f;
    float height           = 1.8f;   // total height (cylinder + 2 hemispheres)
    float stepHeight       = 0.35f;  // max ledge the controller auto-steps
    float slopeLimit       = 50.0f;  // degrees; steeper = slide
    float maxSpeed         = 5.0f;   // m/s (walk speed)
    float jumpSpeed        = 7.0f;
    float gravity          = -19.6f; // m/s^2 (2× earth = snappy platformer)
    float terminalVelocity = -50.0f;
    float acceleration     = 50.0f;  // how fast horizontal vel reaches wishDir
    float airAcceleration  = 5.0f;   // reduced control in air
    float friction         = 10.0f;  // how fast horizontal vel decays to zero
    float slideDecel       = 8.0f;   // slide-mode deceleration (m/s^2)
    float slideInitBoost   = 1.6f;   // initial slide speed multiplier
    float slideDuration    = 1.0f;   // seconds before slide ends

    // ---- Input (set by gameplay code each frame) ----------------------------
    Vec3  wishDir   = {0, 0, 0};     // normalized desired movement direction
    bool  wishJump  = false;
    bool  wishCrouch= false;
    bool  wishRun   = false;

    // ---- Movement mode ------------------------------------------------------
    enum class MovementMode : uint8_t { Walk, Run, Crouch, Slide };
    MovementMode movementMode = MovementMode::Walk;

    // ---- Public API (must match the task spec) ------------------------------
    void  update(float dt, const CollisionWorld3D& world);
    void  teleport(const Vec3& pos);
    void  jump();                   // sets wishJump=true (consumed next update)
    bool  isGrounded()      const { return m_grounded; }
    Vec3  getVelocity()     const { return m_velocity; }
    Vec3  getPosition()     const { return m_position; }
    Vec3  getGroundNormal() const { return m_groundNormal; }
    bool  isCrouching()     const { return m_crouching; }
    bool  isSliding()       const { return m_sliding; }

    // Misc accessors for tests / debug overlay.
    float getCurrentHeight() const;   // crouch-aware
    float getCurrentRadius() const { return radius; }
    float getSlideTimer()    const { return m_slideTimer; }
    float getFootstepAccum() const { return m_footstepAccum; }

    // Footstep signal: when this accumulates past 1.0 the gameplay layer
    // fires a footstep event (and resets it). The controller advances it
    // based on distance traveled × mode multiplier (walk=1, run=1.6,
    // crouch=0.7, slide=2.0). Tests can read it to verify "running is
    // noisier than walking".
    void  consumeFootstep() { m_footstepAccum = 0.0f; }

private:
    // ---- Runtime state ------------------------------------------------------
    Vec3  m_position     = {0, 1.0f, 0};
    Vec3  m_velocity     = {0, 0, 0};
    Vec3  m_groundNormal = {0, 1, 0};
    bool  m_grounded     = false;
    bool  m_crouching    = false;
    bool  m_sliding      = false;
    float m_slideTimer   = 0.0f;
    float m_footstepAccum= 0.0f;
    float m_crouchLerp   = 1.0f;  // 1 = standing, 0 = crouching (height blend)

    // ---- Capsule geometry helpers ------------------------------------------
    // Returns Y offset from the capsule center to the center of the bottom
    // hemisphere. (= (height - 2*radius) / 2)
    float capsuleHalfLength() const;

    // Returns the bottom of the capsule (lowest Y), in world space.
    float capsuleBottomY() const;

    // Returns the AABB that bounds the capsule (using current height).
    AABB3D capsuleAABB() const;

    // ---- Collision primitives ----------------------------------------------
    struct SweepHit {
        bool  hit      = false;
        float toi      = 1.0f;       // time of impact, in [0, 1]
        Vec3  normal   = {0, 0, 0};  // contact normal, points AWAY from AABB
        float distance = 0.0f;       // distance traveled along disp before hit
    };

    // Swept capsule-vs-AABB via SAT slab method (with Minkowski expansion).
    // `disp` is the displacement this frame (in meters).
    // Returns the EARLIEST TOI across all 3 axes; normal is set to the axis
    // of minimum penetration.
    SweepHit sweepCapsuleVsAABB(const Vec3& disp, const AABB3D& aabb) const;

    // Sweeps the capsule against every AABB in the world (within the query
    // region) and returns the EARLIEST hit.
    SweepHit sweepWorld(const Vec3& disp, const CollisionWorld3D& world) const;

    // Moves the capsule by `disp`, sliding along walls. Up to `maxIter`
    // iterations. Returns the actual displacement applied.
    Vec3 moveAndSlide(const Vec3& disp, const CollisionWorld3D& world,
                      int maxIter = 4);

    // ---- Step-up handling ---------------------------------------------------
    // If `horizontalDisp` is blocked by a low obstacle (height < stepHeight),
    // try to step over it: sweep up, sweep forward, sweep down.
    // Returns the actual displacement applied (zero if step failed).
    Vec3 tryStepUp(const Vec3& horizontalDisp,
                   const CollisionWorld3D& world);

    // ---- Ground detection ---------------------------------------------------
    // Cast a short ray (0.05 m) downward from the capsule base. Updates
    // m_grounded + m_groundNormal. Returns true if grounded.
    bool detectGround(const CollisionWorld3D& world);

    // ---- Mode helpers -------------------------------------------------------
    float currentMaxSpeed() const;     // walk/run/crouch/slide multiplier
    void  updateCrouch(float dt);
    void  updateSlide(float dt);
};

} // namespace td
