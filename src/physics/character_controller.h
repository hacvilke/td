// =============================================================================
// TD Engine - Character Controller v1 (Tier 2.2)
//
// Kinematic character controller for 3D games. Inspired by Godot's
// CharacterBody3D and Unity's CharacterController. Does NOT use the full
// rigidbody dynamics solver — instead does swept AABB vs world collision,
// which is what Minecraft, Roblox, and most FPS games actually need.
//
// Features:
//   - Walk, run, jump, crouch
//   - Slope handling (sliding on steep slopes, walking on gentle ones)
//   - Step-up (auto-climb small ledges like stairs)
//   - Gravity, terminal velocity
//   - Manual velocity control (for knockback, dash, teleport)
//
// Status: SKELETON. The CharacterControllerComponent struct + the step()
// algorithm are here; the swept-AABB-vs-world query is delegated to a
// CollisionWorld interface (TODO Tier 2.2). For 2D games, the existing
// ColliderComponent + CollisionSystem is enough; this is for 3D.
//
// CharacterControllerComponent is NOT registered in the ECS ComponentType
// enum yet (to avoid touching the core ECS files for every new component
// during the skeleton phase). The CharacterControllerSystem below is a
// standalone updater that gameplay code drives manually by calling
// step() on each (entity, CC) pair. When this graduates from skeleton
// to production, register CharacterController in component.h + world.cpp
// and switch CharacterControllerSystem to inherit from System + use
// World::query() like the other systems.
// =============================================================================
#pragma once
#include "../ecs/world.h"
#include "../ecs/component.h"
#include "../core/math/vec3.h"
#include "../core/logger.h"
#include "../core/profiler.h"
#include <cstdint>

namespace td {

// Capsule-shaped collider for character controllers. Capsule > AABB for
// characters because it doesn't catch on edges when sliding along walls.
struct CharacterControllerComponent {
    float radius = 0.4f;       // capsule radius (XY)
    float height = 1.8f;       // capsule total height (cylinder + 2 hemispheres)
    float stepHeight = 0.35f;  // max ledge height the controller can step up
    float slopeLimit = 50.0f;  // degrees; steeper = slide
    float maxSpeed = 5.0f;
    float jumpSpeed = 7.0f;
    float gravity = -19.6f;    // m/s^2 (2x earth = snappy platformer feel)
    float terminalVelocity = -50.0f;

    // Input state (set by gameplay code each frame)
    Vec3  wishDir = {0, 0, 0};     // normalized desired movement direction
    bool  wishJump = false;
    bool  wishCrouch = false;

    // Computed velocity (gravity + input applied)
    Vec3  velocity = {0, 0, 0};

    // Runtime state
    bool  grounded = false;
    bool  crouching = false;
    bool  sliding = false;       // true when on a slope > slopeLimit
};

// CharacterControllerSystem: standalone updater (NOT an ECS System yet).
// Gameplay code calls step() for each entity that has a CharacterController.
//
// When CharacterControllerComponent is registered in the ECS (TODO Tier 2.2),
// this becomes:
//   class CharacterControllerSystem : public System {
//     void update(World* world, float dt) override { ... query + step ... }
//   };
class CharacterControllerSystem {
public:
    // Step a single character controller forward by dt seconds.
    // Reads cc->wishDir + cc->wishJump, applies gravity + movement,
    // updates t->position. Collision is a stub for now (TODO Tier 2.2).
    void step(World* world, EntityId id, float dt) {
        TD_PROFILE_SCOPE("CharacterControllerSystem::step");
        Transform3DComponent* t = world->getComponent<Transform3DComponent>(id);
        // CharacterControllerComponent is not yet registered in the ECS, so
        // gameplay code passes it via a side table or directly. For the
        // skeleton, we accept that getComponent<CharacterControllerComponent>
        // won't link and instead receive the CC as a parameter:
        (void)t;
        (void)dt;
        // Real implementation goes here once CC is registered.
    }

    // Convenience: step all (entity, CC) pairs in a parallel array.
    // Used by gameplay code that maintains its own CC list until the
    // ECS registration lands.
    void stepAll(World* world, EntityId* ids, CharacterControllerComponent* ccs,
                 int count, float dt) {
        TD_PROFILE_SCOPE("CharacterControllerSystem::stepAll");
        for (int i = 0; i < count; i++) {
            stepOne(world, ids[i], ccs[i], dt);
        }
    }

private:
    void stepOne(World* world, EntityId id, CharacterControllerComponent& cc, float dt) {
        Transform3DComponent* t = world->getComponent<Transform3DComponent>(id);
        if (!t) return;

        // 1. Apply gravity.
        cc.velocity.y += cc.gravity * dt;
        if (cc.velocity.y < cc.terminalVelocity) cc.velocity.y = cc.terminalVelocity;

        // 2. Compute desired horizontal move from wishDir + maxSpeed.
        Vec3 horizMove = {
            cc.wishDir.x * cc.maxSpeed * dt,
            0,
            cc.wishDir.z * cc.maxSpeed * dt
        };

        // 3. Jump.
        if (cc.wishJump && cc.grounded) {
            cc.velocity.y = cc.jumpSpeed;
            cc.grounded = false;
        }

        // 4. Vertical move.
        Vec3 vertMove = { 0, cc.velocity.y * dt, 0 };

        // 5. Sweep + resolve collisions.
        //    STUB: real impl does swept-AABB-vs-world query against voxels
        //    + other colliders, then resolves per-axis (horizontal first,
        //    then vertical, so we can detect ground contact).
        //    For now, just integrate position.
        t->prevPosition = t->position;
        t->position.x += horizMove.x;
        t->position.y += vertMove.y;
        t->position.z += horizMove.z;

        // 6. Ground check (stub: assume grounded if moving down and y <= 0).
        if (cc.velocity.y <= 0 && t->position.y <= 0.0f) {
            t->position.y = 0.0f;
            cc.velocity.y = 0;
            cc.grounded = true;
        } else {
            cc.grounded = false;
        }

        // 7. Reset input flags for next frame.
        cc.wishJump = false;
    }
};

} // namespace td
