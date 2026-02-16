#include "logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <iostream>

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::setLogFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.close();
    }
    file_.open(path, std::ios::out | std::ios::app);
}

void Logger::log(LogLevel level, const std::string& message) {
    std::string ts = currentTimestamp();
    const char* lvl = levelToString(level);

    std::ostringstream oss;
    oss << "[" << ts << "] [" << lvl << "] " << message;
    std::string line = oss.str();

    std::lock_guard<std::mutex> lock(mutex_);

    // Write to file if open.
    if (file_.is_open()) {
        file_ << line << "\n";
        file_.flush();
    }

    // Store in recent logs ring buffer.
    recent_logs_.push_back(std::move(line));
    if (static_cast<int>(recent_logs_.size()) > MAX_RECENT) {
        recent_logs_.pop_front();
    }
}

void Logger::info(const std::string& message) {
    log(LogLevel::LVL_INFO, message);
}

void Logger::warn(const std::string& message) {
    log(LogLevel::LVL_WARN, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::LVL_ERROR, message);
}

std::vector<std::string> Logger::getRecentLogs(int count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    int n = static_cast<int>(recent_logs_.size());
    int start = (count >= n) ? 0 : n - count;
    return std::vector<std::string>(recent_logs_.begin() + start, recent_logs_.end());
}

const char* Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::LVL_INFO:  return "INFO";
        case LogLevel::LVL_WARN:  return "WARN";
        case LogLevel::LVL_ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

std::string Logger::currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}
