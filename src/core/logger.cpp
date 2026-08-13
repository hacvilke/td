#include "logger.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace td {

Logger::Logger() 
    : m_logCount(0)
    , m_logHead(0)
    , m_logFile(nullptr)
    , m_fileLoggingEnabled(true)
    , m_consoleLoggingEnabled(true)
    , m_minLevel(LogLevel::Info) {
    memset(m_logBuffer, 0, sizeof(m_logBuffer));
    memset(m_logFilePath, 0, sizeof(m_logFilePath));
}

Logger::~Logger() {
    shutdown();
}

Logger& Logger::get() {
    static Logger instance;
    return instance;
}

bool Logger::init(const char* logFilePath) {
    strncpy(m_logFilePath, logFilePath, sizeof(m_logFilePath) - 1);
    
    // Open log file
    m_logFile = fopen(logFilePath, "w");
    if (!m_logFile) {
        m_fileLoggingEnabled = false;
        return false;
    }
    
    // Write header
    fprintf((FILE*)m_logFile, "=== TD Engine Log ===\n");
    fprintf((FILE*)m_logFile, "Started at: ");
    
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf((FILE*)m_logFile, "%04d-%02d-%02d %02d:%02d:%02d\n\n",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond);
    fflush((FILE*)m_logFile);
    
    info("Logger initialized");
    return true;
}

void Logger::shutdown() {
    if (m_logFile) {
        info("Logger shutting down");
        fclose((FILE*)m_logFile);
        m_logFile = nullptr;
    }
}

void Logger::getTimestamp(int& hour, int& min, int& sec, int& ms) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    hour = st.wHour;
    min = st.wMinute;
    sec = st.wSecond;
    ms = st.wMilliseconds;
}

void Logger::log(LogLevel level, const char* format, ...) {
    if (level < m_minLevel) return;
    
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logInternal(level, buffer);
}

void Logger::info(const char* format, ...) {
    if (LogLevel::Info < m_minLevel) return;
    
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logInternal(LogLevel::Info, buffer);
}

void Logger::warn(const char* format, ...) {
    if (LogLevel::Warning < m_minLevel) return;
    
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logInternal(LogLevel::Warning, buffer);
}

void Logger::error(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logInternal(LogLevel::Error, buffer);
}

void Logger::logInternal(LogLevel level, const char* message) {
    int hour, min, sec, ms;
    getTimestamp(hour, min, sec, ms);
    
    const char* levelStr;
    const char* colorCode;
    
    switch (level) {
        case LogLevel::Info:
            levelStr = "INFO";
            colorCode = "";
            break;
        case LogLevel::Warning:
            levelStr = "WARN";
            colorCode = "";
            break;
        case LogLevel::Error:
            levelStr = "ERROR";
            colorCode = "";
            break;
        default:
            levelStr = "???";
            colorCode = "";
    }
    
    // Format: [HH:MM:SS.mmm] [LEVEL] message
    char formattedMsg[1024];
    snprintf(formattedMsg, sizeof(formattedMsg), 
             "[%02d:%02d:%02d.%03d] [%s] %s",
             hour, min, sec, ms, levelStr, message);
    
    // Store in ring buffer
    LogMessage& logMsg = m_logBuffer[m_logHead];
    strncpy(logMsg.text, formattedMsg, sizeof(logMsg.text) - 1);
    logMsg.text[sizeof(logMsg.text) - 1] = '\0';
    logMsg.level = level;
    logMsg.hour = hour;
    logMsg.minute = min;
    logMsg.second = sec;
    logMsg.millisecond = ms;
    
    m_logHead = (m_logHead + 1) % MAX_LOG_MESSAGES;
    if (m_logCount < MAX_LOG_MESSAGES) {
        m_logCount++;
    }
    
    // Console output
    if (m_consoleLoggingEnabled) {
        // Set console color based on level
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        WORD color;
        switch (level) {
            case LogLevel::Warning:
                color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
                break;
            case LogLevel::Error:
                color = FOREGROUND_RED | FOREGROUND_INTENSITY;
                break;
            default:
                color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        }
        SetConsoleTextAttribute(hConsole, color);
        printf("%s\n", formattedMsg);
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }
    
    // File output
    if (m_fileLoggingEnabled && m_logFile) {
        fprintf((FILE*)m_logFile, "%s\n", formattedMsg);
        fflush((FILE*)m_logFile);
    }
}

const LogMessage* Logger::getMessage(int index) const {
    if (index < 0 || index >= m_logCount) {
        return nullptr;
    }
    
    // Calculate actual index in ring buffer
    int actualIndex;
    if (m_logCount < MAX_LOG_MESSAGES) {
        actualIndex = index;
    } else {
        actualIndex = (m_logHead + index) % MAX_LOG_MESSAGES;
    }
    
    return &m_logBuffer[actualIndex];
}

void Logger::clear() {
    m_logCount = 0;
    m_logHead = 0;
    memset(m_logBuffer, 0, sizeof(m_logBuffer));
}

} // namespace td
