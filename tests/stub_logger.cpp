// Minimal Logger stub for the regression test on Linux.
// The real logger.cpp has a hard #include <windows.h> on non-Emscripten
// builds, which we can't satisfy here. This stub provides the same symbols
// so world.cpp links cleanly.
#include "core/logger.h"
#include <cstdarg>
#include <cstdio>

namespace td {

Logger::Logger()
    : m_logCount(0), m_logHead(0), m_logFile(nullptr),
      m_fileLoggingEnabled(false), m_consoleLoggingEnabled(true),
      m_minLevel(LogLevel::Info) {
    for (int i = 0; i < MAX_LOG_MESSAGES; i++) {
        m_logBuffer[i] = LogMessage{};
    }
    m_logFilePath[0] = '\0';
}

Logger::~Logger() {}

Logger& Logger::get() {
    static Logger instance;
    return instance;
}

bool Logger::init(const char*) { return true; }
void Logger::shutdown() {}

const LogMessage* Logger::getMessage(int index) const {
    if (index < 0 || index >= m_logCount) return nullptr;
    return &m_logBuffer[index];
}

void Logger::clear() {
    m_logCount = 0;
    m_logHead = 0;
}

void Logger::log(LogLevel, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
}
void Logger::info(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
}
void Logger::warn(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
}
void Logger::error(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
}

void Logger::logInternal(LogLevel, const char* message) {
    fprintf(stderr, "%s\n", message);
}

void Logger::getTimestamp(int& h, int& m, int& s, int& ms) {
    h = 0; m = 0; s = 0; ms = 0;
}

} // namespace td
