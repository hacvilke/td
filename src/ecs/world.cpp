#include "world.h"
#include "../core/logger.h"
#include "../physics/aabb.h"
#include "../physics/collision.h"
#include <cstring>

namespace td {

// Forward declarations of every explicit specialization.
// clang (emcc) requires these to be visible BEFORE the first implicit
// instantiation (GCC and MSVC are lenient about this, clang is not).
// The actual definitions live further down, emitted by the IMPL_* macros.
#define DECL_COMPONENT_SPECIALIZATION(Type) \
    template<> Type*      World::addComponent<Type>(EntityId id); \
    template<> Type*      World::getComponent<Type>(EntityId id); \
    template<> const Type* World::getComponent<Type>(EntityId id) const; \
    template<> bool       World::hasComponent<Type>(EntityId id) const; \
    template<> void       World::removeComponent<Type>(EntityId id);

DECL_COMPONENT_SPECIALIZATION(PositionComponent)
DECL_COMPONENT_SPECIALIZATION(VelocityComponent)
DECL_COMPONENT_SPECIALIZATION(SpriteComponent)
DECL_COMPONENT_SPECIALIZATION(RigidBodyComponent)
DECL_COMPONENT_SPECIALIZATION(ColliderComponent)
DECL_COMPONENT_SPECIALIZATION(Transform3DComponent)
DECL_COMPONENT_SPECIALIZATION(MeshRendererComponent)
DECL_COMPONENT_SPECIALIZATION(LightComponent)
DECL_COMPONENT_SPECIALIZATION(CameraComponent)
DECL_COMPONENT_SPECIALIZATION(AudioSourceComponent)
DECL_COMPONENT_SPECIALIZATION(ScriptComponent)
DECL_COMPONENT_SPECIALIZATION(TagComponent)
DECL_COMPONENT_SPECIALIZATION(BeatTrackerComponent)

#undef DECL_COMPONENT_SPECIALIZATION

World::World() {
    memset(m_entities, 0, sizeof(m_entities));
    memset(m_systems, 0, sizeof(m_systems));
}

World::~World() {
    clear();
}

EntityId World::createEntity(const char* name) {
    int slot = findFreeEntitySlot();
    if (slot < 0) {
        TD_LOG_ERROR("Maximum entities reached");
        return INVALID_ENTITY;
    }
    
    EntityId id = m_nextEntityId++;
    
    EntityRecord& record = m_entities[slot];
    record.id = id;
    record.mask = 0;
    record.active = true;
    record.positionIdx = -1;
    record.velocityIdx = -1;
    record.spriteIdx = -1;
    record.rigidBodyIdx = -1;
    record.colliderIdx = -1;
    record.transform3DIdx = -1;
    record.meshRendererIdx = -1;
    record.lightIdx = -1;
    record.cameraIdx = -1;
    record.audioSourceIdx = -1;
    record.scriptIdx = -1;
    record.tagIdx = -1;
    record.beatTrackerIdx = -1;
    
    m_entityCount++;
    
    // Add tag component by default
    addComponent<TagComponent>(id);
    setEntityName(id, name);
    
    return id;
}

void World::destroyEntity(EntityId id) {
    int idx = findEntityIndex(id);
    if (idx < 0) return;
    
    EntityRecord& record = m_entities[idx];
    
    // Remove all components
    removeComponent<PositionComponent>(id);
    removeComponent<VelocityComponent>(id);
    removeComponent<SpriteComponent>(id);
    removeComponent<RigidBodyComponent>(id);
    removeComponent<ColliderComponent>(id);
    removeComponent<Transform3DComponent>(id);
    removeComponent<MeshRendererComponent>(id);
    removeComponent<LightComponent>(id);
    removeComponent<CameraComponent>(id);
    removeComponent<AudioSourceComponent>(id);
    removeComponent<ScriptComponent>(id);
    removeComponent<TagComponent>(id);
    removeComponent<BeatTrackerComponent>(id);
    
    record.id = INVALID_ENTITY;
    record.mask = 0;
    record.active = false;
    
    m_entityCount--;
}

bool World::entityExists(EntityId id) const {
    return findEntityIndex(id) >= 0;
}

void World::setEntityEnabled(EntityId id, bool enabled) {
    int idx = findEntityIndex(id);
    if (idx >= 0) {
        m_entities[idx].active = enabled;
    }
}

bool World::isEntityEnabled(EntityId id) const {
    int idx = findEntityIndex(id);
    return idx >= 0 && m_entities[idx].active;
}

void World::setEntityName(EntityId id, const char* name) {
    TagComponent* tag = getComponent<TagComponent>(id);
    if (tag) {
        strncpy(tag->name, name, 63);
        tag->name[63] = '\0';
    }
}

const char* World::getEntityName(EntityId id) const {
    const TagComponent* tag = getComponent<TagComponent>(id);
    return tag ? tag->name : "Unknown";
}

void World::setEntityTag(EntityId id, const char* tag) {
    TagComponent* tc = getComponent<TagComponent>(id);
    if (tc) {
        strncpy(tc->tag, tag, 31);
        tc->tag[31] = '\0';
    }
}

const char* World::getEntityTag(EntityId id) const {
    const TagComponent* tc = getComponent<TagComponent>(id);
    return tc ? tc->tag : "Untagged";
}

EntityId World::findEntityByName(const char* name) const {
    for (int i = 0; i < TD_MAX_ENTITIES; i++) {
        if (m_entities[i].id != INVALID_ENTITY && m_entities[i].tagIdx >= 0) {
            if (strcmp(tags[m_entities[i].tagIdx].name, name) == 0) {
                return m_entities[i].id;
            }
        }
    }
    return INVALID_ENTITY;
}

int World::findEntitiesByTag(const char* tag, EntityId* outIds, int maxResults) const {
    int count = 0;
    for (int i = 0; i < TD_MAX_ENTITIES && count < maxResults; i++) {
        if (m_entities[i].id != INVALID_ENTITY && m_entities[i].tagIdx >= 0) {
            if (strcmp(tags[m_entities[i].tagIdx].tag, tag) == 0) {
                outIds[count++] = m_entities[i].id;
            }
        }
    }
    return count;
}

void World::addSystem(System* system) {
    if (m_systemCount < TD_MAX_SYSTEMS) {
        m_systems[m_systemCount++] = system;
    }
}

void World::removeSystem(System* system) {
    for (int i = 0; i < m_systemCount; i++) {
        if (m_systems[i] == system) {
            for (int j = i; j < m_systemCount - 1; j++) {
                m_systems[j] = m_systems[j + 1];
            }
            m_systemCount--;
            break;
        }
    }
}

void World::updateSystems(float dt) {
    for (int i = 0; i < m_systemCount; i++) {
        if (m_systems[i] && m_systems[i]->isEnabled()) {
            m_systems[i]->update(this, dt);
        }
    }
}

int World::query(ComponentMask mask, EntityId* outEntities, int maxResults) const {
    int count = 0;
    for (int i = 0; i < TD_MAX_ENTITIES && count < maxResults; i++) {
        if (m_entities[i].id != INVALID_ENTITY) {
            if ((m_entities[i].mask & mask) == mask) {
                outEntities[count++] = m_entities[i].id;
            }
        }
    }
    return count;
}

int World::queryActive(ComponentMask mask, EntityId* outEntities, int maxResults) const {
    int count = 0;
    for (int i = 0; i < TD_MAX_ENTITIES && count < maxResults; i++) {
        if (m_entities[i].id != INVALID_ENTITY && m_entities[i].active) {
            if ((m_entities[i].mask & mask) == mask) {
                outEntities[count++] = m_entities[i].id;
            }
        }
    }
    return count;
}

int World::getActiveEntityCount() const {
    int count = 0;
    for (int i = 0; i < TD_MAX_ENTITIES; i++) {
        if (m_entities[i].id != INVALID_ENTITY && m_entities[i].active) {
            count++;
        }
    }
    return count;
}

void World::clear() {
    for (int i = 0; i < TD_MAX_ENTITIES; i++) {
        if (m_entities[i].id != INVALID_ENTITY) {
            destroyEntity(m_entities[i].id);
        }
    }
    
    m_entityCount = 0;
    m_nextEntityId = 1;
    
    positionCount = 0;
    velocityCount = 0;
    spriteCount = 0;
    rigidBodyCount = 0;
    colliderCount = 0;
    transform3DCount = 0;
    meshRendererCount = 0;
    lightCount = 0;
    cameraCount = 0;
    audioSourceCount = 0;
    scriptCount = 0;
    tagCount = 0;
    beatTrackerCount = 0;
}

EntityRecord* World::getEntityRecord(EntityId id) {
    int idx = findEntityIndex(id);
    return idx >= 0 ? &m_entities[idx] : nullptr;
}

const EntityRecord* World::getEntityRecord(EntityId id) const {
    int idx = findEntityIndex(id);
    return idx >= 0 ? &m_entities[idx] : nullptr;
}

int World::findEntityIndex(EntityId id) const {
    if (id == INVALID_ENTITY) return -1;
    
    for (int i = 0; i < TD_MAX_ENTITIES; i++) {
        if (m_entities[i].id == id) {
            return i;
        }
    }
    return -1;
}

int World::findFreeEntitySlot() const {
    for (int i = 0; i < TD_MAX_ENTITIES; i++) {
        if (m_entities[i].id == INVALID_ENTITY) {
            return i;
        }
    }
    return -1;
}

// Template specializations
#define IMPL_ADD_COMPONENT(Type, array, countVar, idxField, compType) \
template<> Type* World::addComponent<Type>(EntityId id) { \
    int idx = findEntityIndex(id); \
    if (idx < 0 || countVar >= TD_MAX_ENTITIES) return nullptr; \
    EntityRecord& record = m_entities[idx]; \
    if (record.idxField >= 0) return &array[record.idxField]; \
    record.idxField = countVar; \
    record.mask |= componentBit(compType); \
    Type& comp = array[countVar++]; \
    comp = Type(); \
    return &comp; \
}

#define IMPL_GET_COMPONENT(Type, array, idxField) \
template<> Type* World::getComponent<Type>(EntityId id) { \
    int idx = findEntityIndex(id); \
    if (idx < 0) return nullptr; \
    int compIdx = m_entities[idx].idxField; \
    return compIdx >= 0 ? &array[compIdx] : nullptr; \
} \
template<> const Type* World::getComponent<Type>(EntityId id) const { \
    int idx = findEntityIndex(id); \
    if (idx < 0) return nullptr; \
    int compIdx = m_entities[idx].idxField; \
    return compIdx >= 0 ? &array[compIdx] : nullptr; \
}

#define IMPL_HAS_COMPONENT(Type, compType) \
template<> bool World::hasComponent<Type>(EntityId id) const { \
    int idx = findEntityIndex(id); \
    return idx >= 0 && (m_entities[idx].mask & componentBit(compType)); \
}

#define IMPL_REMOVE_COMPONENT(Type, array, countVar, idxField, compType) \
template<> void World::removeComponent<Type>(EntityId id) { \
    int idx = findEntityIndex(id); \
    if (idx < 0) return; \
    EntityRecord& record = m_entities[idx]; \
    if (record.idxField < 0) return; \
    record.mask &= ~componentBit(compType); \
    record.idxField = -1; \
}

// Position
IMPL_ADD_COMPONENT(PositionComponent, positions, positionCount, positionIdx, ComponentType::Position)
IMPL_GET_COMPONENT(PositionComponent, positions, positionIdx)
IMPL_HAS_COMPONENT(PositionComponent, ComponentType::Position)
IMPL_REMOVE_COMPONENT(PositionComponent, positions, positionCount, positionIdx, ComponentType::Position)

// Velocity
IMPL_ADD_COMPONENT(VelocityComponent, velocities, velocityCount, velocityIdx, ComponentType::Velocity)
IMPL_GET_COMPONENT(VelocityComponent, velocities, velocityIdx)
IMPL_HAS_COMPONENT(VelocityComponent, ComponentType::Velocity)
IMPL_REMOVE_COMPONENT(VelocityComponent, velocities, velocityCount, velocityIdx, ComponentType::Velocity)

// Sprite
IMPL_ADD_COMPONENT(SpriteComponent, sprites, spriteCount, spriteIdx, ComponentType::Sprite)
IMPL_GET_COMPONENT(SpriteComponent, sprites, spriteIdx)
IMPL_HAS_COMPONENT(SpriteComponent, ComponentType::Sprite)
IMPL_REMOVE_COMPONENT(SpriteComponent, sprites, spriteCount, spriteIdx, ComponentType::Sprite)

// RigidBody
IMPL_ADD_COMPONENT(RigidBodyComponent, rigidBodies, rigidBodyCount, rigidBodyIdx, ComponentType::RigidBody)
IMPL_GET_COMPONENT(RigidBodyComponent, rigidBodies, rigidBodyIdx)
IMPL_HAS_COMPONENT(RigidBodyComponent, ComponentType::RigidBody)
IMPL_REMOVE_COMPONENT(RigidBodyComponent, rigidBodies, rigidBodyCount, rigidBodyIdx, ComponentType::RigidBody)

// Collider
IMPL_ADD_COMPONENT(ColliderComponent, colliders, colliderCount, colliderIdx, ComponentType::Collider)
IMPL_GET_COMPONENT(ColliderComponent, colliders, colliderIdx)
IMPL_HAS_COMPONENT(ColliderComponent, ComponentType::Collider)
IMPL_REMOVE_COMPONENT(ColliderComponent, colliders, colliderCount, colliderIdx, ComponentType::Collider)

// Transform3D
IMPL_ADD_COMPONENT(Transform3DComponent, transforms3D, transform3DCount, transform3DIdx, ComponentType::Transform3D)
IMPL_GET_COMPONENT(Transform3DComponent, transforms3D, transform3DIdx)
IMPL_HAS_COMPONENT(Transform3DComponent, ComponentType::Transform3D)
IMPL_REMOVE_COMPONENT(Transform3DComponent, transforms3D, transform3DCount, transform3DIdx, ComponentType::Transform3D)

// MeshRenderer
IMPL_ADD_COMPONENT(MeshRendererComponent, meshRenderers, meshRendererCount, meshRendererIdx, ComponentType::MeshRenderer)
IMPL_GET_COMPONENT(MeshRendererComponent, meshRenderers, meshRendererIdx)
IMPL_HAS_COMPONENT(MeshRendererComponent, ComponentType::MeshRenderer)
IMPL_REMOVE_COMPONENT(MeshRendererComponent, meshRenderers, meshRendererCount, meshRendererIdx, ComponentType::MeshRenderer)

// Light
IMPL_ADD_COMPONENT(LightComponent, lights, lightCount, lightIdx, ComponentType::Light)
IMPL_GET_COMPONENT(LightComponent, lights, lightIdx)
IMPL_HAS_COMPONENT(LightComponent, ComponentType::Light)
IMPL_REMOVE_COMPONENT(LightComponent, lights, lightCount, lightIdx, ComponentType::Light)

// Camera
IMPL_ADD_COMPONENT(CameraComponent, cameras, cameraCount, cameraIdx, ComponentType::Camera)
IMPL_GET_COMPONENT(CameraComponent, cameras, cameraIdx)
IMPL_HAS_COMPONENT(CameraComponent, ComponentType::Camera)
IMPL_REMOVE_COMPONENT(CameraComponent, cameras, cameraCount, cameraIdx, ComponentType::Camera)

// AudioSource
IMPL_ADD_COMPONENT(AudioSourceComponent, audioSources, audioSourceCount, audioSourceIdx, ComponentType::AudioSource)
IMPL_GET_COMPONENT(AudioSourceComponent, audioSources, audioSourceIdx)
IMPL_HAS_COMPONENT(AudioSourceComponent, ComponentType::AudioSource)
IMPL_REMOVE_COMPONENT(AudioSourceComponent, audioSources, audioSourceCount, audioSourceIdx, ComponentType::AudioSource)

// Script
IMPL_ADD_COMPONENT(ScriptComponent, scripts, scriptCount, scriptIdx, ComponentType::Script)
IMPL_GET_COMPONENT(ScriptComponent, scripts, scriptIdx)
IMPL_HAS_COMPONENT(ScriptComponent, ComponentType::Script)
IMPL_REMOVE_COMPONENT(ScriptComponent, scripts, scriptCount, scriptIdx, ComponentType::Script)

// Tag
IMPL_ADD_COMPONENT(TagComponent, tags, tagCount, tagIdx, ComponentType::Tag)
IMPL_GET_COMPONENT(TagComponent, tags, tagIdx)
IMPL_HAS_COMPONENT(TagComponent, ComponentType::Tag)
IMPL_REMOVE_COMPONENT(TagComponent, tags, tagCount, tagIdx, ComponentType::Tag)

// BeatTracker
IMPL_ADD_COMPONENT(BeatTrackerComponent, beatTrackers, beatTrackerCount, beatTrackerIdx, ComponentType::BeatTracker)
IMPL_GET_COMPONENT(BeatTrackerComponent, beatTrackers, beatTrackerIdx)
IMPL_HAS_COMPONENT(BeatTrackerComponent, ComponentType::BeatTracker)
IMPL_REMOVE_COMPONENT(BeatTrackerComponent, beatTrackers, beatTrackerCount, beatTrackerIdx, ComponentType::BeatTracker)

// ==================== Built-in Systems ====================

void MovementSystem::update(World* world, float dt) {
    EntityId entities[TD_MAX_ENTITIES];
    int count = world->queryActive(getRequiredComponents(), entities, TD_MAX_ENTITIES);
    
    for (int i = 0; i < count; i++) {
        PositionComponent* pos = world->getComponent<PositionComponent>(entities[i]);
        VelocityComponent* vel = world->getComponent<VelocityComponent>(entities[i]);
        
        if (pos && vel) {
            pos->prevX = pos->x;
            pos->prevY = pos->y;
            
            vel->vx += vel->ax * dt;
            vel->vy += vel->ay * dt;
            
            pos->x += vel->vx * dt;
            pos->y += vel->vy * dt;
        }
    }
}

void PhysicsSystem::update(World* world, float dt) {
    EntityId entities[TD_MAX_ENTITIES];
    ComponentMask mask = componentBit(ComponentType::Position) | 
                         componentBit(ComponentType::Velocity) |
                         componentBit(ComponentType::RigidBody);
    int count = world->queryActive(mask, entities, TD_MAX_ENTITIES);
    
    for (int i = 0; i < count; i++) {
        PositionComponent* pos = world->getComponent<PositionComponent>(entities[i]);
        VelocityComponent* vel = world->getComponent<VelocityComponent>(entities[i]);
        RigidBodyComponent* rb = world->getComponent<RigidBodyComponent>(entities[i]);
        
        if (pos && vel && rb && !rb->isStatic) {
            // Apply gravity
            if (rb->useGravity) {
                vel->vx += m_gravityX * rb->gravityScale * dt;
                vel->vy += m_gravityY * rb->gravityScale * dt;
            }
            
            // Apply damping
            vel->vx *= (1.0f - rb->linearDamping);
            vel->vy *= (1.0f - rb->linearDamping);
        }
    }
}

void CollisionSystem::update(World* world, float dt) {
    (void)dt;
    
    EntityId entities[TD_MAX_ENTITIES];
    int count = world->queryActive(getRequiredComponents(), entities, TD_MAX_ENTITIES);
    
    CollisionDetector detector;
    
    // Reset collision flags
    for (int i = 0; i < count; i++) {
        ColliderComponent* col = world->getComponent<ColliderComponent>(entities[i]);
        if (col) {
            col->colliding = false;
            col->collidingWith = -1;
        }
    }
    
    // Check all pairs
    for (int i = 0; i < count; i++) {
        PositionComponent* posA = world->getComponent<PositionComponent>(entities[i]);
        ColliderComponent* colA = world->getComponent<ColliderComponent>(entities[i]);
        
        for (int j = i + 1; j < count; j++) {
            PositionComponent* posB = world->getComponent<PositionComponent>(entities[j]);
            ColliderComponent* colB = world->getComponent<ColliderComponent>(entities[j]);
            
            AABB aabbA = AABB::fromCenter(
                posA->x + colA->offsetX, posA->y + colA->offsetY,
                colA->width, colA->height
            );
            AABB aabbB = AABB::fromCenter(
                posB->x + colB->offsetX, posB->y + colB->offsetY,
                colB->width, colB->height
            );
            
            CollisionResult result = detector.testAABB(aabbA, aabbB);
            
            if (result.colliding) {
                colA->colliding = true;
                colA->normalX = result.normalX;
                colA->normalY = result.normalY;
                colA->collidingWith = (int)entities[j];
                
                colB->colliding = true;
                colB->normalX = -result.normalX;
                colB->normalY = -result.normalY;
                colB->collidingWith = (int)entities[i];
            }
        }
    }
}

void SpriteRenderSystem::update(World* world, float dt) {
    (void)dt;
    (void)world;
    // Rendering is typically done separately, not in system update
}

} // namespace td
