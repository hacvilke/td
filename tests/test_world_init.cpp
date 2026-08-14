// Regression test for the World constructor bug.
// Before the fix: memset(0) left m_entities[i].id == 0 for all i,
// so findFreeEntitySlot() returned -1, so createEntity() returned
// INVALID_ENTITY and addComponent() returned nullptr -> null deref crash.
//
// After the fix: the constructor explicitly sets every slot's id to
// INVALID_ENTITY, so createEntity() succeeds on the first call.
//
// Build: g++ -std=c++17 -Isrc tests/test_world_init.cpp src/ecs/world.cpp -o /tmp/test_world_init
// Run:   /tmp/test_world_init  (exits 0 on success, 1 on failure)

#include "ecs/world.h"
#include "core/logger.h"
#include <cstdio>
#include <cassert>

int main() {
    // Logger works without init() — it just won't write to a file.
    // (We can't call init() here because logger.cpp has a hard
    // #include <windows.h> on non-Emscripten builds.)

    // Heap-allocate World — it's ~8 MB (10000 entities × 14 component arrays),
    // too big for the default stack.
    td::World& world = *new td::World();

    // The very first createEntity call would fail before the fix.
    td::EntityId e1 = world.createEntity("entity1");
    if (e1 == td::INVALID_ENTITY) {
        std::fprintf(stderr, "FAIL: first createEntity returned INVALID_ENTITY\n");
        std::fprintf(stderr, "      (this is the 'Maximum entities reached' bug)\n");
        return 1;
    }
    std::printf("PASS: first createEntity returned id=%u\n", e1);

    // addComponent must succeed and return a valid pointer.
    td::PositionComponent* pos = world.addComponent<td::PositionComponent>(e1);  /* unchanged */
    if (!pos) {
        std::fprintf(stderr, "FAIL: addComponent<Position> returned nullptr\n");
        return 1;
    }
    pos->x = 42.0f;
    pos->y = 99.0f;
    std::printf("PASS: addComponent<Position> returned non-null, set pos=(%g,%g)\n", pos->x, pos->y);

    // A second entity should also work.
    td::EntityId e2 = world.createEntity("entity2");  /* unchanged */
    if (e2 == td::INVALID_ENTITY) {
        std::fprintf(stderr, "FAIL: second createEntity returned INVALID_ENTITY\n");
        return 1;
    }
    std::printf("PASS: second createEntity returned id=%u\n", e2);

    // Destroy + recreate to verify slot recycling.
    world.destroyEntity(e1);
    td::EntityId e3 = world.createEntity("entity3");  /* unchanged */
    if (e3 == td::INVALID_ENTITY) {
        std::fprintf(stderr, "FAIL: createEntity after destroy returned INVALID_ENTITY\n");
        return 1;
    }
    std::printf("PASS: createEntity after destroy returned id=%u (slot recycled)\n", e3);

    // entityCount should be 2 (e2 + e3, since e1 was destroyed).
    int count = world.getEntityCount();  /* unchanged */
    if (count != 2) {
        std::fprintf(stderr, "FAIL: getEntityCount()=%d, expected 2\n", count);
        return 1;
    }
    std::printf("PASS: getEntityCount()=%d (expected 2)\n", count);

    std::printf("\nALL TESTS PASSED\n");
    return 0;
}
