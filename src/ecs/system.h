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

// -----------------------------------------------------------------------------
// BeatSystem - ticks every BeatTrackerComponent, fires on-beat callbacks,
// advances nextBeatTime, and exposes isOnBeat() for game code.
//
// Implementation follows the design in docs/RHYTHM_MECHANICS.md:
//   - Per frame: if engineTime >= nextBeatTime, fire beat event + advance.
//   - On-beat window is TWO half-ranges (not one symmetric window) so the
//     forward-looking half stays reachable after nextBeatTime advances.
//   - Loop detection: if engineTime goes backward (e.g. song looped),
//     hard-reset nextBeatTime to avoid drift accumulation.
// -----------------------------------------------------------------------------
class BeatSystem : public System {
public:
    void update(World* world, float dt) override;
    ComponentMask getRequiredComponents() const override {
        return componentBit(ComponentType::BeatTracker);
    }

    // Returns true if the given tracker's songTime is inside the on-beat window.
    // Uses the two-half-window trick.
    bool isOnBeat(const struct BeatTrackerComponent& tracker, float engineTime) const;

    // Register a beat-tick callback. Called with (beatCount, beatTime) on each
    // beat fire. The callback is invoked from within update(); it must not
    // mutate the World structure (read-only is fine).
    using BeatCallback = void (*)(int beatCount, float beatTime);
    void setBeatCallback(BeatCallback cb) { m_callback = cb; }

    // Engine-time source. Override in tests; production uses td::g_time.
    // We accept a function pointer so the system has no hard dependency on
    // the global TimeState (which is in platform.h, not always available).
    using TimeSource = float (*)();
    void setTimeSource(TimeSource ts) { m_timeSource = ts; }

private:
    BeatCallback m_callback = nullptr;
    TimeSource   m_timeSource = nullptr;
};

} // namespace td
