#include "menu_bar.h"
#include <cstring>

namespace td {

MenuBar::Action MenuBar::render(GuiContext& gui) {
    (void)gui;
    if (!visible) return Action::None;

    // In a real implementation, this draws a horizontal menu bar at the top
    // Each menu item opens a dropdown when clicked
    // Keyboard shortcuts (Ctrl+S, Ctrl+Z, etc.) are checked here

    // Simplified: just return None, the editor main handles actual GUI drawing
    return Action::None;
}

} // namespace td
