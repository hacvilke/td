#pragma once
#include <cstdint>

namespace td {

enum class LogLevel : uint8_t {
    Info = 0,
    Warning = 1,
    Error = 2
};

struct LogMessage {
    char text[512];
    LogLevel level;
    int hour, minute, second, millisecond;
};

class Logger {
public:
    static const int MAX_LOG_MESSAGES = 256;
    
    static Logger& get();
    
    bool init(const char* logFilePath = "td-engine.log");
    void shutdown();
    
    void log(LogLevel level, const char* format, ...);
    void info(const char* format, ...);
    void warn(const char* format, ...);
    void error(const char* format, ...);
    
    // Log buffer access for editor console
    const LogMessage* getLogBuffer() const { return m_logBuffer; }
    int getLogCount() const { return m_logCount; }
    int getLogHead() const { return m_logHead; }
    
    // Get a specific message from the ring buffer
    const LogMessage* getMessage(int index) const;
    
    // Clear logs
    void clear();
    
    // Settings
    void setFileLogging(bool enabled) { m_fileLoggingEnabled = enabled; }
    void setConsoleLogging(bool enabled) { m_consoleLoggingEnabled = enabled; }
    void setMinLevel(LogLevel level) { m_minLevel = level; }
    
private:
    Logger();
    ~Logger();
    
    void logInternal(LogLevel level, const char* message);
    void writeToFile(const char* message);
    void getTimestamp(int& hour, int& min, int& sec, int& ms);
    
    LogMessage m_logBuffer[MAX_LOG_MESSAGES];
    int m_logCount;
    int m_logHead; // Next write position in ring buffer
    
    void* m_logFile;
    bool m_fileLoggingEnabled;
    bool m_consoleLoggingEnabled;
    LogLevel m_minLevel;
    char m_logFilePath[256];
};

// Convenience macros
#define TD_LOG_INFO(...)  td::Logger::get().info(__VA_ARGS__)
#define TD_LOG_WARN(...)  td::Logger::get().warn(__VA_ARGS__)
#define TD_LOG_ERROR(...) td::Logger::get().error(__VA_ARGS__)

#ifdef TD_DEBUG
    #define TD_LOG_DEBUG(...) td::Logger::get().info(__VA_ARGS__)
#else
    #define TD_LOG_DEBUG(...)
#endif

// Assert macro
#define TD_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            td::Logger::get().error("ASSERTION FAILED: %s\n  File: %s\n  Line: %d\n  Condition: %s", \
                message, __FILE__, __LINE__, #condition); \
        } \
    } while(0)

} // namespace td
