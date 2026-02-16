// progress_monitor.h
#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <chrono>

struct ProgressInfo {
    int64_t total_bytes = 0;
    int64_t downloaded_bytes = 0;
    double speed_bytes_per_sec = 0.0;
    double progress_percent = 0.0;
    int remaining_seconds = -1;    // -1 means "calculating"
};

class ProgressMonitor {
public:
    explicit ProgressMonitor(int64_t total_bytes);

    // Add downloaded bytes (thread-safe)
    void addBytes(int64_t bytes);

    // Get current progress snapshot
    ProgressInfo snapshot();

private:
    mutable std::mutex mutex_;
    int64_t total_bytes_;
    int64_t downloaded_bytes_ = 0;

    // Sliding window: record (timestamp, cumulative bytes) for last 5 seconds
    static constexpr int WINDOW_SIZE_SEC = 5;
    struct Sample {
        std::chrono::steady_clock::time_point time;
        int64_t bytes;
    };
    std::deque<Sample> samples_;
};
