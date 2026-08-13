#pragma once
#include "../src/ecs/world.h"

namespace td {
struct GuiContext;
struct ScenePanel {
    EntityId selectedEntity = INVALID_ENTITY;
    char searchFilter[128] = {};
    bool visible = true;
    void render(GuiContext& gui, World& world);
};
} // namespace td
