// =============================================================================
// TD Engine - 3D Character Controller (Task wave1-physaudio)
//
// Real implementation of CharacterController3D, declared in
// character_controller_3d.h. The existing src/physics/character_controller.h
// (Tier 1.4 skeleton) is kept byte-identical per the task constraints; this
// file + the new header form the production API.
//
// Key algorithms:
//   - Swept capsule-vs-AABB via the SAT slab method (bounding-box
//     approximation: the capsule is treated as its axis-aligned bounding box
//     for the sweep, which is conservative — the box's corners may flag a
//     collision slightly before the capsule's curve would, but it never
//     misses a collision).
//   - moveAndSlide: sweeps along the displacement, stops at the earliest
//     TOI, projects the remaining displacement onto the contact plane
//     (slide), repeats up to 4 iterations. This is the standard "slide
//     along walls" behavior players expect.
//   - Step-up: when a horizontal sweep is blocked, try sweep-up → sweep-
//     forward → sweep-down. If the obstacle's top is below stepHeight, the
//     character steps over it; otherwise the step is reverted.
//   - Ground detection: a vertical raycast from above the character's feet
//     finds the highest surface (AABB top, slope, or ground plane) at the
//     character's XZ. If the surface is within snap range (between
//     -0.2 m below and stepHeight+0.05 m above the feet), the character
//     snaps to it.
//   - Slope limit: if the ground normal's Y component is below
//     cos(slopeLimit), the character slides — gravity is projected onto
//     the slope plane and applied as tangential acceleration.
//   - Movement modes: Walk (1×), Run (2×), Crouch (0.5×, height halved),
//     Slide (run + crouch, brief 1.6× boost that decays over 1 s).
//
// No external libraries. C++17, portable, -Wall -Wextra clean.
// =============================================================================

#include "character_controller_3d.h"
#include "../core/math/math.h"
#include <cmath>

namespace td {

// ============================================================================
// Helpers — local ray-vs-AABB (slab method) with face-normal tracking.
// Used by SimpleCollisionWorld3D::raycast.
// ============================================================================
static bool rayVsAABB(const Vec3& origin, const Vec3& dir, const AABB3D& b,
                       float maxT, float& outT, Vec3& outNormal) {
    float tmin = 0.0f;
    float tmax = maxT;
    Vec3  normal(0, 0, 0);

    // Lambda per axis keeps the slab logic DRY.
    auto testAxis = [&](float o, float d, float lo, float hi,
                         const Vec3& nLo, const Vec3& nHi) -> bool {
        if (absF(d) < TD_EPSILON) {
            // Parallel ray — must already be inside the slab.
            return o >= lo && o <= hi;
        }
        float t1 = (lo - o) / d;  // entry candidate
        float t2 = (hi - o) / d;  // exit candidate
        Vec3  en1 = nLo, en2 = nHi;
        if (t1 > t2) { float t = t1; t1 = t2; t2 = t; Vec3 tn = en1; en1 = en2; en2 = tn; }
        if (t1 > tmin) { tmin = t1; normal = en1; }
        if (t2 < tmax) { tmax = t2; }
        return tmin <= tmax;
    };

    if (!testAxis(origin.x, dir.x, b.minX, b.maxX, Vec3(-1,0,0), Vec3(1,0,0))) return false;
    if (!testAxis(origin.y, dir.y, b.minY, b.maxY, Vec3(0,-1,0), Vec3(0,1,0))) return false;
    if (!testAxis(origin.z, dir.z, b.minZ, b.maxZ, Vec3(0,0,-1), Vec3(0,0,1))) return false;

    outT = tmin;
    outNormal = normal;
    return true;
}

// ============================================================================
// SimpleCollisionWorld3D
// ============================================================================

int SimpleCollisionWorld3D::queryAABBs(const AABB3D& query, AABB3D* outAabbs,
                                        int maxAabbs) const {
    int n = 0;
    for (int i = 0; i < aabbCount && n < maxAabbs; i++) {
        if (query.overlaps(aabbs[i])) {
            outAabbs[n++] = aabbs[i];
        }
    }
    return n;
}

bool SimpleCollisionWorld3D::raycast(const Vec3& origin, const Vec3& dir,
                                      float maxDist, Vec3& outPoint,
                                      Vec3& outNormal) const {
    Vec3  d = dir.normalized();
    float bestT = maxDist;
    bool  hit  = false;
    Vec3  bestPoint, bestNormal;

    // 1. Infinite ground plane at y=0 (normal +Y).
    if (hasGroundPlane && d.y < -TD_EPSILON) {
        float t = -origin.y / d.y;  // origin.y + t*d.y = 0
        if (t >= 0.0f && t < bestT) {
            bestT = t;
            hit = true;
            bestPoint  = origin + d * t;
            bestNormal = Vec3(0, 1, 0);
        }
    }

    // 2. Slopes (inclined rectangular patches).
    for (int i = 0; i < slopeCount; i++) {
        const Slope& s = slopes[i];
        Vec3 n = s.normal();
        Vec3 p0(s.xMin, s.baseY, s.zMin);
        float denom = n.dot(d);
        if (absF(denom) < TD_EPSILON) continue;
        float t = n.dot(p0 - origin) / denom;
        if (t < 0.0f || t >= bestT) continue;
        Vec3 p = origin + d * t;
        if (!s.contains(p.x, p.z)) continue;
        bestT = t;
        hit = true;
        bestPoint  = p;
        bestNormal = n;
    }

    // 3. AABBs (slab method).
    for (int i = 0; i < aabbCount; i++) {
        float t;
        Vec3  n;
        if (rayVsAABB(origin, d, aabbs[i], bestT, t, n)) {
            if (t < bestT) {
                bestT = t;
                hit = true;
                bestPoint  = origin + d * t;
                bestNormal = n;
            }
        }
    }

    if (hit) {
        outPoint  = bestPoint;
        outNormal = bestNormal;
    }
    return hit;
}

// ============================================================================
// CharacterController3D — geometry helpers
// ============================================================================

float CharacterController3D::capsuleHalfLength() const {
    // Half the cylinder portion (= total height/2 - radius).
    float h = getCurrentHeight();
    return maxF(0.0f, h * 0.5f - radius);
}

float CharacterController3D::capsuleBottomY() const {
    return m_position.y - getCurrentHeight() * 0.5f;
}

float CharacterController3D::getCurrentHeight() const {
    // Lerp between crouch and stand height (1 = standing, 0 = crouching).
    float standH  = height;
    float crouchH = height * 0.5f;
    return lerp(crouchH, standH, m_crouchLerp);
}

AABB3D CharacterController3D::capsuleAABB() const {
    float h = getCurrentHeight();
    float r = radius;
    return AABB3D(
        m_position.x - r, m_position.y - h * 0.5f, m_position.z - r,
        m_position.x + r, m_position.y + h * 0.5f, m_position.z + r
    );
}

float CharacterController3D::currentMaxSpeed() const {
    switch (movementMode) {
        case MovementMode::Run:    return maxSpeed * 2.0f;
        case MovementMode::Crouch: return maxSpeed * 0.5f;
        case MovementMode::Slide: {
            // Slide speed decays from slideInitBoost× to 1× over slideDuration.
            float k = (slideDuration > 0.0f)
                      ? (m_slideTimer / slideDuration) : 0.0f;
            k = clamp(k, 0.0f, 1.0f);
            float mult = lerp(1.0f, slideInitBoost, k);
            return maxSpeed * 2.0f * mult;
        }
        case MovementMode::Walk:
        default:                   return maxSpeed;
    }
}

// ============================================================================
// Swept collision (SAT slab method on the capsule's bounding AABB)
// ============================================================================

CharacterController3D::SweepHit
CharacterController3D::sweepCapsuleVsAABB(const Vec3& disp,
                                            const AABB3D& aabb) const {
    SweepHit result;
    AABB3D me = capsuleAABB();

    float myMin[3] = { me.minX, me.minY, me.minZ };
    float myMax[3] = { me.maxX, me.maxY, me.maxZ };
    float bbMin[3] = { aabb.minX, aabb.minY, aabb.minZ };
    float bbMax[3] = { aabb.maxX, aabb.maxY, aabb.maxZ };
    float d[3]     = { disp.x, disp.y, disp.z };

    // SAT: for each axis, find the entry and exit time. The latest entry
    // across all axes is the actual collision time; the earliest exit
    // bounds it. If latest-entry > earliest-exit, no overlap window.
    float tmin = -1e30f;
    float tmax =  1e30f;
    Vec3  entryNormal(0, 0, 0);

    for (int i = 0; i < 3; i++) {
        float entry, exit;
        Vec3  axisNormal;

        if (absF(d[i]) < TD_EPSILON) {
            // No movement on this axis. Already overlapping?
            if (myMax[i] <= bbMin[i] || myMin[i] >= bbMax[i]) {
                return result;  // No overlap on this axis → never collides.
            }
            entry = -1e30f;     // Always overlapping on this axis.
            exit  =  1e30f;
            axisNormal = Vec3(0, 0, 0);
        } else if (d[i] > 0.0f) {
            // Moving in +i: my +i face enters b's -i face first.
            entry = (bbMin[i] - myMax[i]) / d[i];
            exit  = (bbMax[i] - myMin[i]) / d[i];
            axisNormal = Vec3(i == 0 ? -1.0f : 0.0f,
                              i == 1 ? -1.0f : 0.0f,
                              i == 2 ? -1.0f : 0.0f);
        } else {  // d[i] < 0
            entry = (bbMax[i] - myMin[i]) / d[i];
            exit  = (bbMin[i] - myMax[i]) / d[i];
            axisNormal = Vec3(i == 0 ? 1.0f : 0.0f,
                              i == 1 ? 1.0f : 0.0f,
                              i == 2 ? 1.0f : 0.0f);
        }

        if (entry > tmin) {
            tmin = entry;
            entryNormal = axisNormal;
        }
        if (exit < tmax) {
            tmax = exit;
        }
        if (tmin > tmax) {
            return result;  // No overlap window.
        }
    }

    // Collision window is [tmin, tmax]. If it's outside this frame, no hit.
    if (tmin > 1.0f) return result;
    if (tmax < 0.0f) return result;

    float toi = tmin;
    if (toi < 0.0f) toi = 0.0f;  // Already penetrating — report immediate.

    result.hit      = true;
    result.toi      = toi;
    result.normal   = entryNormal;
    result.distance = toi * sqrtF(disp.x * disp.x + disp.y * disp.y + disp.z * disp.z);
    return result;
}

CharacterController3D::SweepHit
CharacterController3D::sweepWorld(const Vec3& disp,
                                   const CollisionWorld3D& world) const {
    SweepHit earliest;
    earliest.toi = 1.0f;
    earliest.hit = false;

    if (disp.x * disp.x + disp.y * disp.y + disp.z * disp.z < TD_EPSILON) {
        return earliest;
    }

    // Build the swept AABB (union of start and end positions).
    AABB3D me = capsuleAABB();
    AABB3D query(
        minF(me.minX, me.minX + disp.x),
        minF(me.minY, me.minY + disp.y),
        minF(me.minZ, me.minZ + disp.z),
        maxF(me.maxX, me.maxX + disp.x),
        maxF(me.maxY, me.maxY + disp.y),
        maxF(me.maxZ, me.maxZ + disp.z)
    );

    // Query the world for candidate AABBs, then run the precise sweep test
    // against each. Return the earliest hit.
    AABB3D hits[64];
    int n = world.queryAABBs(query, hits, 64);
    for (int i = 0; i < n; i++) {
        SweepHit h = sweepCapsuleVsAABB(disp, hits[i]);
        if (h.hit && h.toi < earliest.toi) {
            earliest = h;
        }
    }
    return earliest;
}

Vec3 CharacterController3D::moveAndSlide(const Vec3& disp,
                                          const CollisionWorld3D& world,
                                          int maxIter) {
    Vec3  remaining  = disp;
    Vec3  totalMoved(0, 0, 0);

    for (int iter = 0; iter < maxIter; iter++) {
        if (remaining.x * remaining.x + remaining.y * remaining.y +
            remaining.z * remaining.z < TD_EPSILON) {
            break;
        }
        SweepHit hit = sweepWorld(remaining, world);
        if (!hit.hit) {
            // Clear path — apply full remaining displacement.
            m_position += remaining;
            totalMoved += remaining;
            break;
        }

        // Move to TOI.
        Vec3 toMove = remaining * hit.toi;
        m_position += toMove;
        totalMoved += toMove;

        // Slide: project remaining displacement onto the contact plane.
        Vec3 n = hit.normal;
        if (n.x * n.x + n.y * n.y + n.z * n.z < TD_EPSILON) break;
        n.normalize();
        float vn = remaining.dot(n);
        if (vn >= 0.0f) break;  // Already moving away — done.
        remaining = remaining - n * vn;

        // Tiny skin offset to avoid re-collision on the next iteration.
        m_position += n * 0.0005f;
    }
    return totalMoved;
}

// ============================================================================
// Step-up
// ============================================================================
Vec3 CharacterController3D::tryStepUp(const Vec3& horizontalDisp,
                                       const CollisionWorld3D& world) {
    if (horizontalDisp.x * horizontalDisp.x +
        horizontalDisp.z * horizontalDisp.z < TD_EPSILON) {
        return Vec3(0, 0, 0);
    }

    Vec3 savedPos = m_position;

    // 1. Sweep UP by stepHeight.
    Vec3 upDisp(0, stepHeight, 0);
    SweepHit upHit = sweepWorld(upDisp, world);
    float upFrac = upHit.hit ? upHit.toi : 1.0f;
    if (upHit.hit && upFrac < 0.95f) {
        // Blocked above by a low ceiling — can't step.
        return Vec3(0, 0, 0);
    }
    m_position += upDisp * upFrac;

    // 2. Sweep FORWARD by the original horizontal displacement.
    SweepHit fwdHit = sweepWorld(horizontalDisp, world);
    float fwdFrac = fwdHit.hit ? fwdHit.toi : 1.0f;
    if (fwdHit.hit && fwdFrac < 0.05f) {
        // Still blocked — the obstacle is taller than stepHeight. Revert.
        m_position = savedPos;
        return Vec3(0, 0, 0);
    }
    Vec3 fwdMove = horizontalDisp * fwdFrac;
    m_position += fwdMove;

    // 3. Sweep DOWN by stepHeight (to land on the step surface).
    Vec3 downDisp(0, -stepHeight, 0);
    SweepHit downHit = sweepWorld(downDisp, world);
    if (downHit.hit) {
        m_position += downDisp * downHit.toi;
    } else {
        // Nothing below — let gravity pull us down next frame.
        m_position += downDisp;
    }
    return fwdMove;
}

// ============================================================================
// Ground detection (vertical raycast from above the feet)
// ============================================================================
bool CharacterController3D::detectGround(const CollisionWorld3D& world) {
    // Cast a vertical ray from 1 m above the capsule center, downward 3 m.
    // This finds the highest surface (slope / AABB top / ground plane) at
    // the character's XZ position.
    Vec3  rayOrigin(m_position.x, m_position.y + 1.0f, m_position.z);
    Vec3  rayDir(0, -1, 0);
    Vec3  hitPoint, hitNormal;
    float maxDist = 3.0f;

    if (!world.raycast(rayOrigin, rayDir, maxDist, hitPoint, hitNormal)) {
        m_grounded = false;
        return false;
    }

    // Walls (normal.y < ~0.3, i.e. steeper than ~70° from vertical) are not
    // walkable ground — don't snap to them.
    if (hitNormal.y < 0.3f) {
        m_grounded = false;
        return false;
    }

    float desiredFeetY = hitPoint.y;
    float currentFeetY = m_position.y - getCurrentHeight() * 0.5f;
    float delta = desiredFeetY - currentFeetY;

    // Ground too far above feet → we'd be inside a wall/step too tall to climb.
    if (delta > stepHeight + 0.05f) {
        m_grounded = false;
        return false;
    }
    // Ground below feet (delta < 0): only snap if we're not moving up.
    // Otherwise a jump (m_velocity.y > 0) would be canceled by the snap on
    // frame 1, when the character is still within 0.2 m of the ground.
    if (delta < -0.2f) {
        m_grounded = false;
        return false;
    }
    if (delta < 0.0f && m_velocity.y > 0.0f) {
        // We're rising (jumping) — don't snap down to the ground.
        m_grounded = false;
        return false;
    }

    // Snap to ground.
    m_position.y = desiredFeetY + getCurrentHeight() * 0.5f;
    if (m_velocity.y < 0.0f) m_velocity.y = 0.0f;
    m_groundNormal = hitNormal;
    m_grounded = true;
    return true;
}

// ============================================================================
// Mode helpers
// ============================================================================
void CharacterController3D::updateCrouch(float dt) {
    bool wantsCrouch = (movementMode == MovementMode::Crouch) ||
                       (movementMode == MovementMode::Slide);
    m_crouching = wantsCrouch;
    // Smoothly lerp crouch state (full transition in ~0.15 s).
    float target = wantsCrouch ? 0.0f : 1.0f;
    float rate = 1.0f / 0.15f;
    if (m_crouchLerp < target) {
        m_crouchLerp += rate * dt;
        if (m_crouchLerp > target) m_crouchLerp = target;
    } else if (m_crouchLerp > target) {
        m_crouchLerp -= rate * dt;
        if (m_crouchLerp < target) m_crouchLerp = target;
    }
}

void CharacterController3D::updateSlide(float dt) {
    if (m_slideTimer > 0.0f) {
        m_slideTimer -= dt;
        if (m_slideTimer <= 0.0f) {
            m_slideTimer = 0.0f;
            // Slide ends — drop to crouch.
            movementMode = MovementMode::Crouch;
        }
    }
}

// ============================================================================
// Public API
// ============================================================================
void CharacterController3D::teleport(const Vec3& pos) {
    m_position = pos;
    m_velocity.setZero();
    m_grounded = false;
    m_groundNormal = Vec3(0, 1, 0);
    m_sliding = false;
}

void CharacterController3D::jump() {
    wishJump = true;
}

void CharacterController3D::update(float dt, const CollisionWorld3D& world) {
    // 1. Update crouch / slide state.
    updateCrouch(dt);
    if (movementMode == MovementMode::Slide) {
        updateSlide(dt);
    }

    // 2. Apply gravity (always, unless grounded on a non-sliding surface).
    if (!m_grounded || m_sliding) {
        m_velocity.y += gravity * dt;
        if (m_velocity.y < terminalVelocity) m_velocity.y = terminalVelocity;
    } else {
        // Grounded + stable → cancel downward velocity (normal force).
        if (m_velocity.y < 0.0f) m_velocity.y = 0.0f;
    }

    // 3. Compute desired horizontal velocity from wishDir × mode speed.
    float targetSpeed = currentMaxSpeed();
    Vec3  targetHorizVel(wishDir.x * targetSpeed, 0.0f, wishDir.z * targetSpeed);

    // 4. Accelerate / decelerate horizontal velocity.
    float accel = m_grounded ? acceleration : airAcceleration;
    if (wishDir.x * wishDir.x + wishDir.z * wishDir.z > TD_EPSILON) {
        Vec3  delta(targetHorizVel.x - m_velocity.x, 0,
                    targetHorizVel.z - m_velocity.z);
        float deltaLen = sqrtF(delta.x * delta.x + delta.z * delta.z);
        if (deltaLen > 0.0f) {
            float maxStep = accel * dt;
            if (deltaLen <= maxStep) {
                m_velocity.x = targetHorizVel.x;
                m_velocity.z = targetHorizVel.z;
            } else {
                float scale = maxStep / deltaLen;
                m_velocity.x += delta.x * scale;
                m_velocity.z += delta.z * scale;
            }
        }
    } else if (m_grounded && !m_sliding &&
               movementMode != MovementMode::Slide) {
        // Apply friction when no input on ground.
        float speed = sqrtF(m_velocity.x * m_velocity.x +
                             m_velocity.z * m_velocity.z);
        if (speed > 0.0f) {
            float drop = friction * dt;
            float newSpeed = maxF(0.0f, speed - drop);
            float scale = newSpeed / speed;
            m_velocity.x *= scale;
            m_velocity.z *= scale;
        }
    }

    // 5. Jump (only if grounded and not sliding on a slope).
    if (wishJump && m_grounded && !m_sliding) {
        m_velocity.y = jumpSpeed;
        m_grounded = false;
    }

    // 6. Compute total displacement = velocity × dt.
    Vec3 disp(m_velocity.x * dt, m_velocity.y * dt, m_velocity.z * dt);

    // 7. Horizontal move (with slide + step-up attempt).
    Vec3 horizDisp(disp.x, 0.0f, disp.z);
    Vec3 posBeforeHoriz = m_position;
    Vec3 horizMoved = moveAndSlide(horizDisp, world);

    // 7a. If horizontal move was largely blocked, try stepping up.
    float horizDispLen = sqrtF(horizDisp.x * horizDisp.x +
                                horizDisp.z * horizDisp.z);
    float horizMovedLen = sqrtF(horizMoved.x * horizMoved.x +
                                 horizMoved.z * horizMoved.z);
    if (horizDispLen > 1e-4f && horizMovedLen < horizDispLen * 0.5f &&
        m_grounded) {
        // Revert the partial horizontal move, then try step-up.
        m_position = posBeforeHoriz;
        tryStepUp(horizDisp, world);
    }

    // 8. Vertical move (with slide).
    Vec3 vertDisp(0.0f, disp.y, 0.0f);
    moveAndSlide(vertDisp, world);

    // 9. Ground detection.
    detectGround(world);

    // 10. Slope-limit check — if grounded on a steep slope, slide.
    //     "Slide" here means: the surface is too steep to walk on, so the
    //     character loses their footing and accelerates down the slope
    //     (no friction). The input direction is IGNORED while sliding —
    //     otherwise the user's +X input (50 m/s² accel) would overpower
    //     gravity's ~17 m/s² slide force and the character would walk UP
    //     a 60° slope, which is exactly what slopeLimit is supposed to
    //     prevent. (This matches Unity's CharacterController: on a steep
    //     slope, the slide velocity completely replaces the input velocity.)
    if (m_grounded) {
        float slopeCos = cosF(degToRad(slopeLimit));
        if (m_groundNormal.y < slopeCos) {
            m_sliding = true;
            // Project gravity onto the slope plane → tangential acceleration.
            Vec3 g(0.0f, gravity, 0.0f);
            Vec3 slopeAccel = g - m_groundNormal * g.dot(m_groundNormal);
            // Override horizontal velocity with the slide direction.
            // (Vertical velocity is governed by gravity + ground snap.)
            Vec3  slideXZ(slopeAccel.x, 0.0f, slopeAccel.z);
            float slideLen = sqrtF(slideXZ.x * slideXZ.x +
                                    slideXZ.z * slideXZ.z);
            if (slideLen > TD_EPSILON) {
                // Slide at a speed proportional to gravity. The 0.5× factor
                // gives a visible-but-controlled slide (≈10 m/s for default
                // gravity) — fast enough to clearly slide down, slow enough
                // to read on screen.
                float slideSpeed = absF(gravity) * 0.5f;
                m_velocity.x = slideXZ.x / slideLen * slideSpeed;
                m_velocity.z = slideXZ.z / slideLen * slideSpeed;
            } else {
                m_velocity.x = 0.0f;
                m_velocity.z = 0.0f;
            }
        } else {
            m_sliding = false;
        }
    } else {
        m_sliding = false;
    }

    // 11. Footstep accumulator — running is noisier than walking.
    //     The gameplay layer reads getFootstepAccum(); when it crosses 1.0,
    //     fire a footstep event and call consumeFootstep().
    float speedMult = 1.0f;
    switch (movementMode) {
        case MovementMode::Run:    speedMult = 1.6f; break;
        case MovementMode::Crouch: speedMult = 0.7f; break;
        case MovementMode::Slide:  speedMult = 2.0f; break;
        default: break;
    }
    float horizSpeed = sqrtF(m_velocity.x * m_velocity.x +
                              m_velocity.z * m_velocity.z);
    // Normalize so a walk of 2 m = 1 footstep.
    m_footstepAccum += (horizSpeed * dt * speedMult) / 2.0f;

    // 12. Reset one-shot input flags.
    wishJump = false;
}

} // namespace td
