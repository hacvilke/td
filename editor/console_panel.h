#pragma once
#include "../src/core/logger.h"

namespace td {
struct GuiContext;
struct ConsolePanel {
    bool visible = true;
    bool showInfo = true;
    bool showWarnings = true;
    bool showErrors = true;
    bool autoScroll = true;
    int scrollPos = 0;
    char inputBuffer[256] = {};
    void render(GuiContext& gui);
    void clear();
};
} // namespace td
