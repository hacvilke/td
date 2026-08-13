#include "inspector_panel.h"
#include <cstring>

namespace td {
void InspectorPanel::render(GuiContext& gui, World& world, EntityId selectedEntity) {
    (void)gui;
    if (!visible) return;
    if (selectedEntity == INVALID_ENTITY) return;
    // Display and edit components for the selected entity
    // Position, Velocity, Sprite, RigidBody, etc.
    // In a real implementation, each component would have editable fields
    if (world.hasComponent<PositionComponent>(selectedEntity)) {
        PositionComponent* pos = world.getComponent<PositionComponent>(selectedEntity);
        (void)pos; // GUI would render DragFloat for pos->x, pos->y
    }
    if (world.hasComponent<SpriteComponent>(selectedEntity)) {
        SpriteComponent* spr = world.getComponent<SpriteComponent>(selectedEntity);
        (void)spr; // GUI would render color pickers, size fields, etc.
    }
}
} // namespace td
