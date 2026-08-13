#pragma once
#include "../src/ecs/world.h"

namespace td {
struct GuiContext;
struct InspectorPanel {
    bool visible = true;
    void render(GuiContext& gui, World& world, EntityId selectedEntity);
};
} // namespace td
