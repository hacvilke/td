#pragma once
#include "entity.h"
#include "component.h"
#include "system.h"
#include <cstdint>

namespace td {

#define TD_MAX_ENTITIES 10000
#define TD_MAX_SYSTEMS 32

struct EntityRecord {
    EntityId id = INVALID_ENTITY;
    ComponentMask mask = 0;
    bool active = false;
    
    // Component indices (-1 if not present)
    int positionIdx = -1;
    int velocityIdx = -1;
    int spriteIdx = -1;
    int rigidBodyIdx = -1;
    int colliderIdx = -1;
    int transform3DIdx = -1;
    int meshRendererIdx = -1;
    int lightIdx = -1;
    int cameraIdx = -1;
    int audioSourceIdx = -1;
    int scriptIdx = -1;
    int tagIdx = -1;
};

class World {
public:
    World();
    ~World();
    
    // Entity management
    EntityId createEntity(const char* name = "Entity");
    void destroyEntity(EntityId id);
    bool entityExists(EntityId id) const;
    void setEntityEnabled(EntityId id, bool enabled);
    bool isEntityEnabled(EntityId id) const;
    
    // Entity naming
    void setEntityName(EntityId id, const char* name);
    const char* getEntityName(EntityId id) const;
    void setEntityTag(EntityId id, const char* tag);
    const char* getEntityTag(EntityId id) const;
    EntityId findEntityByName(const char* name) const;
    int findEntitiesByTag(const char* tag, EntityId* outIds, int maxResults) const;
    
    // Component management (template specializations in .cpp)
    template<typename T> T* addComponent(EntityId id);
    template<typename T> T* getComponent(EntityId id);
    template<typename T> const T* getComponent(EntityId id) const;
    template<typename T> bool hasComponent(EntityId id) const;
    template<typename T> void removeComponent(EntityId id);
    
    // System management
    void addSystem(System* system);
    void removeSystem(System* system);
    void updateSystems(float dt);
    
    // Queries
    int query(ComponentMask mask, EntityId* outEntities, int maxResults) const;
    int queryActive(ComponentMask mask, EntityId* outEntities, int maxResults) const;
    int getEntityCount() const { return m_entityCount; }
    int getActiveEntityCount() const;
    
    // Component arrays (public for system access)
    PositionComponent positions[TD_MAX_ENTITIES];
    VelocityComponent velocities[TD_MAX_ENTITIES];
    SpriteComponent sprites[TD_MAX_ENTITIES];
    RigidBodyComponent rigidBodies[TD_MAX_ENTITIES];
    ColliderComponent colliders[TD_MAX_ENTITIES];
    Transform3DComponent transforms3D[TD_MAX_ENTITIES];
    MeshRendererComponent meshRenderers[TD_MAX_ENTITIES];
    LightComponent lights[TD_MAX_ENTITIES];
    CameraComponent cameras[TD_MAX_ENTITIES];
    AudioSourceComponent audioSources[TD_MAX_ENTITIES];
    ScriptComponent scripts[TD_MAX_ENTITIES];
    TagComponent tags[TD_MAX_ENTITIES];
    
    // Component counts
    int positionCount = 0;
    int velocityCount = 0;
    int spriteCount = 0;
    int rigidBodyCount = 0;
    int colliderCount = 0;
    int transform3DCount = 0;
    int meshRendererCount = 0;
    int lightCount = 0;
    int cameraCount = 0;
    int audioSourceCount = 0;
    int scriptCount = 0;
    int tagCount = 0;
    
    // Clear all entities and components
    void clear();
    
    // Get entity record
    EntityRecord* getEntityRecord(EntityId id);
    const EntityRecord* getEntityRecord(EntityId id) const;
    
private:
    int findEntityIndex(EntityId id) const;
    int findFreeEntitySlot() const;
    
    EntityRecord m_entities[TD_MAX_ENTITIES];
    int m_entityCount = 0;
    EntityId m_nextEntityId = 1;
    
    System* m_systems[TD_MAX_SYSTEMS];
    int m_systemCount = 0;
};

} // namespace td
