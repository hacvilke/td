// TD Engine - ECS Unit Tests

#include "../src/ecs/world.h"
#include "../src/ecs/entity.h"
#include "../src/ecs/component.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

void testEntityCreation() {
    printf("\n=== Entity Creation Tests ===\n");
    
    World world;
    
    // Create entity
    EntityId e1 = world.createEntity("TestEntity");
    TEST("Entity created", e1 != INVALID_ENTITY);
    TEST("Entity exists", world.entityExists(e1));
    TEST("Entity count", world.getEntityCount() == 1);
    
    // Create more entities
    EntityId e2 = world.createEntity("Entity2");
    EntityId e3 = world.createEntity("Entity3");
    TEST("Multiple entities", world.getEntityCount() == 3);
    
    // Destroy entity
    world.destroyEntity(e2);
    TEST("Entity destroyed", !world.entityExists(e2));
    TEST("Entity count after destroy", world.getEntityCount() == 2);
    
    // Find by name
    EntityId found = world.findEntityByName("TestEntity");
    TEST("Find by name", found == e1);
}

void testComponents() {
    printf("\n=== Component Tests ===\n");
    
    World world;
    EntityId e = world.createEntity("ComponentTest");
    
    // Add position component
    PositionComponent* pos = world.addComponent<PositionComponent>(e);
    TEST("Position component added", pos != nullptr);
    
    pos->x = 100.0f;
    pos->y = 200.0f;
    
    // Get position component
    PositionComponent* pos2 = world.getComponent<PositionComponent>(e);
    TEST("Get position component", pos2 != nullptr);
    TEST("Position value x", pos2->x == 100.0f);
    TEST("Position value y", pos2->y == 200.0f);
    
    // Has component
    TEST("Has position", world.hasComponent<PositionComponent>(e));
    TEST("No velocity", !world.hasComponent<VelocityComponent>(e));
    
    // Add velocity
    VelocityComponent* vel = world.addComponent<VelocityComponent>(e);
    vel->vx = 5.0f;
    vel->vy = -10.0f;
    TEST("Velocity added", world.hasComponent<VelocityComponent>(e));
    
    // Add sprite
    SpriteComponent* sprite = world.addComponent<SpriteComponent>(e);
    sprite->width = 32;
    sprite->height = 64;
    TEST("Sprite added", world.hasComponent<SpriteComponent>(e));
    
    // Remove component
    world.removeComponent<VelocityComponent>(e);
    TEST("Velocity removed", !world.hasComponent<VelocityComponent>(e));
    TEST("Position still exists", world.hasComponent<PositionComponent>(e));
}

void testQueries() {
    printf("\n=== Query Tests ===\n");
    
    World world;
    
    // Create entities with different components
    EntityId e1 = world.createEntity("Entity1");
    world.addComponent<PositionComponent>(e1);
    world.addComponent<VelocityComponent>(e1);
    
    EntityId e2 = world.createEntity("Entity2");
    world.addComponent<PositionComponent>(e2);
    
    EntityId e3 = world.createEntity("Entity3");
    world.addComponent<PositionComponent>(e3);
    world.addComponent<VelocityComponent>(e3);
    world.addComponent<SpriteComponent>(e3);
    
    // Query for position only
    EntityId results[10];
    ComponentMask posMask = componentBit(ComponentType::Position);
    int count = world.query(posMask, results, 10);
    TEST("Query position count", count == 3);
    
    // Query for position + velocity
    ComponentMask posVelMask = componentBit(ComponentType::Position) | 
                                componentBit(ComponentType::Velocity);
    count = world.query(posVelMask, results, 10);
    TEST("Query pos+vel count", count == 2);
    
    // Query for all three
    ComponentMask allMask = posVelMask | componentBit(ComponentType::Sprite);
    count = world.query(allMask, results, 10);
    TEST("Query all three count", count == 1);
    TEST("Query all three result", results[0] == e3);
}

void testMovementSimulation() {
    printf("\n=== Movement Simulation Tests ===\n");
    
    World world;
    
    // Create 100 entities with position and velocity
    EntityId entities[100];
    for (int i = 0; i < 100; i++) {
        entities[i] = world.createEntity();
        
        PositionComponent* pos = world.addComponent<PositionComponent>(entities[i]);
        pos->x = (float)i;
        pos->y = (float)i;
        
        VelocityComponent* vel = world.addComponent<VelocityComponent>(entities[i]);
        vel->vx = 1.0f;
        vel->vy = 0.5f;
    }
    
    TEST("100 entities created", world.getEntityCount() == 100);
    
    // Simulate 100 steps
    float dt = 0.016f;
    for (int step = 0; step < 100; step++) {
        EntityId queryResults[100];
        ComponentMask mask = componentBit(ComponentType::Position) | 
                             componentBit(ComponentType::Velocity);
        int count = world.query(mask, queryResults, 100);
        
        for (int i = 0; i < count; i++) {
            PositionComponent* pos = world.getComponent<PositionComponent>(queryResults[i]);
            VelocityComponent* vel = world.getComponent<VelocityComponent>(queryResults[i]);
            
            pos->x += vel->vx * dt;
            pos->y += vel->vy * dt;
        }
    }
    
    // Check that entities have moved
    PositionComponent* pos0 = world.getComponent<PositionComponent>(entities[0]);
    TEST("Entity 0 moved X", pos0->x > 0.0f);
    TEST("Entity 0 moved Y", pos0->y > 0.0f);
    
    PositionComponent* pos99 = world.getComponent<PositionComponent>(entities[99]);
    TEST("Entity 99 moved", pos99->x > 99.0f);
}

void testEntityEnable() {
    printf("\n=== Entity Enable/Disable Tests ===\n");
    
    World world;
    
    EntityId e1 = world.createEntity("Test1");
    world.addComponent<PositionComponent>(e1);
    
    EntityId e2 = world.createEntity("Test2");
    world.addComponent<PositionComponent>(e2);
    
    TEST("Both enabled", world.isEntityEnabled(e1) && world.isEntityEnabled(e2));
    
    // Disable one
    world.setEntityEnabled(e1, false);
    TEST("E1 disabled", !world.isEntityEnabled(e1));
    TEST("E2 still enabled", world.isEntityEnabled(e2));
    
    // Query active only
    EntityId results[10];
    int count = world.queryActive(componentBit(ComponentType::Position), results, 10);
    TEST("Query active count", count == 1);
    TEST("Query active result", results[0] == e2);
}

void testClear() {
    printf("\n=== Clear Tests ===\n");
    
    World world;
    
    for (int i = 0; i < 50; i++) {
        EntityId e = world.createEntity();
        world.addComponent<PositionComponent>(e);
        world.addComponent<VelocityComponent>(e);
    }
    
    TEST("50 entities created", world.getEntityCount() == 50);
    
    world.clear();
    
    TEST("World cleared", world.getEntityCount() == 0);
}

int main() {
    printf("TD Engine ECS Tests\n");
    printf("====================\n");
    
    testEntityCreation();
    testComponents();
    testQueries();
    testMovementSimulation();
    testEntityEnable();
    testClear();
    
    printf("\n====================\n");
    printf("Results: %d passed, %d failed\n", g_testsPassed, g_testsFailed);
    
    return g_testsFailed > 0 ? 1 : 0;
}
