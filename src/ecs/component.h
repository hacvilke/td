#pragma once
#include "../core/math/vec2.h"
#include "../core/math/vec3.h"
#include "entity.h"
#include "../renderer/texture.h"
#include <cstdint>

namespace td {

enum class ComponentType : uint8_t {
    Position = 0,
    Velocity,
    Sprite,
    RigidBody,
    Collider,
    Light,
    Camera,
    AudioSource,
    Transform3D,
    MeshRenderer,
    Script,
    Tag,
    BeatTracker,
    // ---- Tier 1.1: Scene graph components ----
    Hierarchy,         // parent/child linked-list pointers + depth + dirty flag
    LocalTransform,    // 2D TRS relative to parent
    WorldTransform,    // cached world TRS (computed by Scene::updateTransforms)
    // ---- Tier 1.3: Scripting (Lua/JS VM) ----
    LuaScript,         // path to .lua file + VM-side ref handle
    COUNT
};

using ComponentMask = uint32_t;

inline ComponentMask componentBit(ComponentType type) {
    return 1u << (uint8_t)type;
}

// ---- 2D Components ----

struct PositionComponent {
    float x = 0, y = 0;
    float prevX = 0, prevY = 0;  // For interpolation
};

struct VelocityComponent {
    float vx = 0, vy = 0;
    float ax = 0, ay = 0;  // Acceleration
};

struct SpriteComponent {
    const Texture* texture = nullptr;
    float width = 32, height = 32;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    float r = 1, g = 1, b = 1, a = 1;
    float rotation = 0;
    float originX = 0.5f, originY = 0.5f;
    int layer = 0;
    bool visible = true;
    bool flipX = false, flipY = false;
};

struct RigidBodyComponent {
    float mass = 1.0f;
    float friction = 0.3f;
    float restitution = 0.2f;
    float linearDamping = 0.01f;
    float gravityScale = 1.0f;
    bool useGravity = true;
    bool isStatic = false;
    bool isKinematic = false;
    bool isTrigger = false;
};

struct ColliderComponent {
    enum class Type : uint8_t { AABB, Circle };
    
    Type type = Type::AABB;
    float offsetX = 0, offsetY = 0;
    float width = 32, height = 32;  // For AABB
    float radius = 16;              // For Circle
    
    // Runtime collision info
    bool colliding = false;
    float normalX = 0, normalY = 0;
    int collidingWith = -1;
};

// ---- 3D Components ----

struct Transform3DComponent {
    Vec3 position;
    Vec3 rotation;  // Euler angles in radians
    Vec3 scale = {1, 1, 1};
    
    // Previous state for interpolation
    Vec3 prevPosition;
    Vec3 prevRotation;
};

struct MeshRendererComponent {
    uint32_t meshIndex = 0;
    uint32_t textureIndex = 0;
    Vec3 color = {1, 1, 1};
    float metallic = 0.0f;
    float roughness = 0.5f;
    bool castShadow = true;
    bool receiveShadow = true;
    bool useTexture = false;
    bool visible = true;
};

struct LightComponent {
    enum class Type : uint8_t { Directional, Point, Spot };
    
    Type type = Type::Point;
    Vec3 color = {1, 1, 1};
    float intensity = 1.0f;
    float range = 10.0f;
    float attenuation = 0.1f;
    float spotAngle = 45.0f;    // Degrees
    float spotSoftness = 0.5f;
    bool castShadows = false;
};

struct CameraComponent {
    enum class Type : uint8_t { Perspective, Orthographic };
    
    Type type = Type::Perspective;
    float fov = 60.0f;
    float near = 0.1f;
    float far = 1000.0f;
    float orthoSize = 5.0f;
    bool active = false;
    bool mainCamera = false;
};

// ---- Other Components ----

struct AudioSourceComponent {
    uint32_t soundIndex = 0;
    int playingId = -1;
    float volume = 1.0f;
    float pitch = 1.0f;
    float pan = 0.0f;
    bool loop = false;
    bool playOnStart = false;
    bool spatial = false;
    float minDistance = 1.0f;
    float maxDistance = 100.0f;
};

struct ScriptComponent {
    char scriptPath[256] = {};
    void* vmInstance = nullptr;
    bool initialized = false;
};

struct TagComponent {
    char name[64] = "Entity";
    char tag[32] = "Untagged";
    bool enabled = true;
};

// ---- Rhythm / Beat Components ----------------------------------------------
// Implements the BPM-synced metronome + on-beat detection described in
// docs/RHYTHM_MECHANICS.md. Attach to any entity; the BeatSystem will tick
// it every frame. Multiple entities can each have their own BeatTracker
// (e.g. one per song layer, one per player for combo tracking).
//
// Key insight from the source video: the on-beat window must be implemented
// as TWO half-windows, not one symmetric window, because nextBeat advances
// the moment the beat fires:
//   upperBound = currentBeat + windowHalfSec   (forward-looking from last beat)
//   lowerBound = nextBeat   - windowHalfSec    (backward-looking from next beat)
// "On beat" = (songTime >= upperBound_from_prev) OR (songTime <= lowerBound_to_next)
struct BeatTrackerComponent {
    float bpm = 120.0f;              // beats per minute
    float spb = 0.5f;                // seconds per beat (cached: 60/bpm)
    float startTime = 0.0f;          // engine time when tracking started
    float nextBeatTime = 0.0f;       // engine time of next beat tick
    float lastBeatTime = 0.0f;       // engine time of most recent beat tick
    float windowHalf = 0.15f;        // half-width of on-beat tolerance (sec)
    float upperBound = 0.0f;         // lastBeatTime + windowHalf
    float lowerBound = 0.0f;         // nextBeatTime - windowHalf
    int   beatCount = 0;             // total beats elapsed since start
    float lastHitTime = -1.0f;       // engine time of last successful on-beat press
    int   combo = 0;                 // consecutive on-beat hits (resets on miss)
    int   bestCombo = 0;             // highest combo reached
    bool  active = false;            // set true by td_start_beat_track
};

// ---- Tier 1.1: Scene Graph Components --------------------------------------
// See src/scene/scene.h for the Scene class that drives these. Hierarchy is
// the parent/child linked-list pointers + depth + dirty flag. LocalTransform
// is the TRS relative to parent. WorldTransform is the cached world TRS,
// recomputed each frame by Scene::updateTransforms().
//
// Why store these as components rather than in a parallel Scene-owned array?
//   - The existing ECS query machinery (World::query(mask, ...)) can find
//     "all entities with a parent" in one call — no separate iteration.
//   - Serialization (Tier 1.2) can walk components uniformly without
//     knowing about the Scene class.
//   - The editor's inspector already knows how to edit any component via
//     the template API; adding a parallel storage would require a separate
//     editor code path.
struct HierarchyComponent {
    EntityId parent       = INVALID_ENTITY;  // INVALID_ENTITY if root
    EntityId firstChild   = INVALID_ENTITY;  // head of children linked list
    EntityId nextSibling  = INVALID_ENTITY;  // next sibling in parent's list
    EntityId prevSibling  = INVALID_ENTITY;  // prev sibling (doubly-linked for O(1) remove)
    int      depth        = 0;               // 0 for root, 1 for top-level child, ...
    bool     transformDirty = true;          // true if local transform changed and world needs recompute
};

struct LocalTransformComponent {
    float x = 0, y = 0;        // local position relative to parent
    float scaleX = 1, scaleY = 1;
    float rotation = 0;        // radians, CCW
};

struct WorldTransformComponent {
    float x = 0, y = 0;
    float scaleX = 1, scaleY = 1;
    float rotation = 0;
};

// ---- Tier 1.3: Scripting Component -----------------------------------------
// Opaque handle to a script instance in the embedded Lua VM (Tier 1.3).
// The ScriptVM owns the actual Lua state; this component just holds the
// file path + a ref handle so the ScriptSystem can call update() on it
// every frame.
struct LuaScriptComponent {
    char     scriptPath[256] = {};     // e.g. "scripts/player_controller.lua"
    int      vmRef           = -1;     // Lua registry ref to the script's env table
    bool     initialized     = false;  // true after the script's init() ran
    bool     enabled         = true;   // false = skip update() calls
};

} // namespace td
