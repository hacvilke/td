#pragma once
#include "component.h"
#include "entity.h"

namespace td {

class World;

class System {
public:
    virtual ~System() = default;
    virtual void update(World* world, float dt) = 0;
    virtual ComponentMask getRequiredComponents() const = 0;
    
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    
    int getPriority() const { return m_priority; }
    void setPriority(int priority) { m_priority = priority; }
    
protected:
    ComponentMask m_requiredMask = 0;
    bool m_enabled = true;
    int m_priority = 0;
};

// Built-in systems

class MovementSystem : public System {
public:
    void update(World* world, float dt) override;
    ComponentMask getRequiredComponents() const override {
        return componentBit(ComponentType::Position) | 
               componentBit(ComponentType::Velocity);
    }
};

class PhysicsSystem : public System {
public:
    void update(World* world, float dt) override;
    ComponentMask getRequiredComponents() const override {
        return componentBit(ComponentType::Position) | 
               componentBit(ComponentType::RigidBody);
    }
    
    void setGravity(float x, float y) { m_gravityX = x; m_gravityY = y; }
    
private:
    float m_gravityX = 0;
    float m_gravityY = -9.81f;
};

class CollisionSystem : public System {
public:
    void update(World* world, float dt) override;
    ComponentMask getRequiredComponents() const override {
        return componentBit(ComponentType::Position) | 
               componentBit(ComponentType::Collider);
    }
};

class SpriteRenderSystem : public System {
public:
    void update(World* world, float dt) override;
    ComponentMask getRequiredComponents() const override {
        return componentBit(ComponentType::Position) | 
               componentBit(ComponentType::Sprite);
    }
};

} // namespace td
