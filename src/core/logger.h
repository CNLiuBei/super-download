#pragma once
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <fstream>

// NOTE: Avoid bare ERROR – it conflicts with the ERROR macro from <windows.h>.
enum class LogLevel { LVL_INFO, LVL_WARN, LVL_ERROR };

class Logger {
public:
    /// Get the singleton instance.
    static Logger& instance();

    /// Set (or change) the log output file path.
    /// Opens the file in append mode. Closes any previously opened file.
    void setLogFile(const std::string& path);

    /// Log a message at the given level.
    /// Format: "[YYYY-MM-DD HH:MM:SS] [LEVEL] message\n"
    void log(LogLevel level, const std::string& message);

    /// Convenience helpers.
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);

    /// Return the most recent log lines (up to count).
    std::vector<std::string> getRecentLogs(int count = 100) const;

    // Non-copyable / non-movable (singleton).
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() = default;

    static const char* levelToString(LogLevel level);
    static std::string currentTimestamp();

    mutable std::mutex mutex_;
    std::ofstream file_;
    std::deque<std::string> recent_logs_;
    static constexpr int MAX_RECENT = 1000;
};
