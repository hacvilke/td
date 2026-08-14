// =============================================================================
// Regression test for the component slot leak fix.
//
// Before the fix, World::removeComponent<T>() cleared the entity's idxField
// and the mask bit, but did NOT decrement the component count or reclaim
// the slot in the array. So every addComponent<T>() after a destroyEntity
// bumped the count by 1, and after TD_MAX_ENTITIES cycles the array was
// full and addComponent<T>() silently returned nullptr.
//
// After the fix, removeComponent<T>() does a swap-back pop: moves the last
// live component into the freed slot, updates the moved component's owner
// entity to point at the new slot, and decrements the count. This test
// verifies:
//   1. Repeated add/remove cycles don't grow the count.
//   2. The moved component's data is preserved.
//   3. The moved component's owner entity still sees the correct data.
//   4. After a destroy, addComponent returns a valid pointer.
// =============================================================================
#include "ecs/world.h"
#include "ecs/component.h"
#include <cstdio>
#include <cstdlib>
#include <cassert>

// Use the same stub logger as test_world_init.cpp so this builds without
// windows.h on Linux CI.
// (Logger is included transitively via world.h -> logger.h; the stub
// implementations are linked in via stub_logger.cpp in the CMake target.)

int main() {
    // World is ~9MB now (with the new scene-graph + script components),
    // so heap-allocate to avoid stack overflow.
    td::World* world = new td::World();

    // ---- Test 1: add/remove cycle doesn't grow count ----------------------
    // Create an entity, add a PositionComponent, remove it, repeat 100 times.
    // Before the fix, positionCount would end at 100; after, it should be 0.
    {
        td::EntityId e = world->createEntity("test");
        assert(e != td::INVALID_ENTITY);
        for (int i = 0; i < 100; i++) {
            td::PositionComponent* p = world->addComponent<td::PositionComponent>(e);
            assert(p != nullptr);
            p->x = (float)i;
            p->y = (float)i * 2;
            world->removeComponent<td::PositionComponent>(e);
        }
        int countAfter = world->positionCount;
        if (countAfter != 0) {
            printf("FAIL: positionCount after 100 add/remove cycles = %d (expected 0)\n", countAfter);
            delete world;
            return 1;
        }
        printf("PASS: 100 add/remove cycles left positionCount = 0\n");
    }

    // ---- Test 2: swap-back preserves the moved component's data -----------
    // Add Position to e1, add Position to e2 (different data), remove e1's.
    // e2's Position should now live in slot 0 (where e1's was), and the
    // data should be intact.
    {
        td::EntityId e1 = world->createEntity("e1");
        td::EntityId e2 = world->createEntity("e2");
        td::PositionComponent* p1 = world->addComponent<td::PositionComponent>(e1);
        p1->x = 111.0f; p1->y = 222.0f;
        td::PositionComponent* p2 = world->addComponent<td::PositionComponent>(e2);
        p2->x = 333.0f; p2->y = 444.0f;

        // Sanity: both components exist before remove.
        assert(world->hasComponent<td::PositionComponent>(e1));
        assert(world->hasComponent<td::PositionComponent>(e2));

        // Remove e1's Position. The swap-back should move e2's Position into
        // e1's old slot, and repoint e2's positionIdx to that slot.
        world->removeComponent<td::PositionComponent>(e1);

        // e1 no longer has Position.
        assert(!world->hasComponent<td::PositionComponent>(e1));

        // e2 should still have Position with the SAME data.
        td::PositionComponent* p2_after = world->getComponent<td::PositionComponent>(e2);
        assert(p2_after != nullptr);
        if (p2_after->x != 333.0f || p2_after->y != 444.0f) {
            printf("FAIL: after swap-back, e2's Position = (%f, %f) (expected 333, 444)\n",
                   p2_after->x, p2_after->y);
            delete world;
            return 1;
        }
        printf("PASS: swap-back preserved e2's Position data after e1's remove\n");

        // The slot count should be 1 (only e2's component remains).
        if (world->positionCount != 1) {
            printf("FAIL: positionCount after swap-back = %d (expected 1)\n", world->positionCount);
            delete world;
            return 1;
        }
        printf("PASS: positionCount is 1 after one remove\n");
    }

    // ---- Test 3: large-scale add/remove doesn't exhaust the array --------
    // Before the fix, this would fill the array after TD_MAX_ENTITIES
    // cycles and the next addComponent would return nullptr.
    //
    // We track the BASELINE positionCount (which may be > 0 if earlier
    // tests left components behind) and verify it doesn't GROW across
    // many create/destroy cycles.
    {
        // We can't actually do TD_MAX_ENTITIES (10000) iterations here
        // because the test would take too long. 1000 is enough to prove
        // the leak is fixed.
        const int N = 1000;
        int baselinePositionCount = world->positionCount;
        int baselineTagCount      = world->tagCount;
        for (int i = 0; i < N; i++) {
            td::EntityId e = world->createEntity("leak_test");
            assert(e != td::INVALID_ENTITY);
            td::PositionComponent* p = world->addComponent<td::PositionComponent>(e);
            if (p == nullptr) {
                printf("FAIL: addComponent returned nullptr at iteration %d "
                       "(component array exhausted — slot leak regression)\n", i);
                delete world;
                return 1;
            }
            world->destroyEntity(e);
        }
        // After destroying all entities, component counts should match the
        // baseline (no growth from the cycles).
        if (world->positionCount != baselinePositionCount) {
            printf("FAIL: positionCount after %d create/destroy cycles = %d "
                   "(baseline was %d — slot leak regression)\n",
                   N, world->positionCount, baselinePositionCount);
            delete world;
            return 1;
        }
        if (world->tagCount != baselineTagCount) {
            printf("FAIL: tagCount after %d create/destroy cycles = %d "
                   "(baseline was %d — slot leak regression)\n",
                   N, world->tagCount, baselineTagCount);
            delete world;
            return 1;
        }
        printf("PASS: %d create/destroy cycles left no leaked component slots "
               "(positionCount=%d, tagCount=%d)\n",
               N, world->positionCount, world->tagCount);
    }

    delete world;
    printf("\nAll slot-leak regression tests PASSED.\n");
    return 0;
}
