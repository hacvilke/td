#include "console_panel.h"
#include <cstring>

namespace td {
void ConsolePanel::render(GuiContext& gui) {
    (void)gui;
    if (!visible) return;

    Logger& logger = Logger::get();
    int count = logger.getLogCount();

    for (int i = 0; i < count; i++) {
        const LogMessage* msg = logger.getMessage(i);
        if (!msg) continue;

        // Filter by level
        if (msg->level == LogLevel::Info && !showInfo) continue;
        if (msg->level == LogLevel::Warning && !showWarnings) continue;
        if (msg->level == LogLevel::Error && !showErrors) continue;

        // In a real implementation, draw the log text with appropriate color
        // INFO = white, WARN = yellow, ERROR = red
        (void)msg->text;
    }
}

void ConsolePanel::clear() {
    Logger::get().clear();
    scrollPos = 0;
}
} // namespace td
