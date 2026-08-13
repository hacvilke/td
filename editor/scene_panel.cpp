#include "scene_panel.h"
#include <cstring>

namespace td {
void ScenePanel::render(GuiContext& gui, World& world) {
    (void)gui;
    if (!visible) return;
    // Scene panel draws a list of entities in the world
    // Clicking an entity selects it
    for (int i = 0; i < TD_MAX_ENTITIES; i++) {
        const EntityRecord* rec = world.getEntityRecord((EntityId)(i + 1));
        if (!rec || rec->id == INVALID_ENTITY) continue;
        const char* name = world.getEntityName(rec->id);
        if (searchFilter[0] != '\0') {
            if (!strstr(name, searchFilter)) continue;
        }
        // In a real implementation, we'd draw a selectable label here
        // For now, the editor main.cpp handles the immediate-mode UI
    }
}
} // namespace td
