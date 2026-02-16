// progress_monitor.cpp
#include "progress_monitor.h"
#include <algorithm>

ProgressMonitor::ProgressMonitor(int64_t total_bytes)
    : total_bytes_(total_bytes)
{
}

void ProgressMonitor::addBytes(int64_t bytes) {
    if (bytes <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    downloaded_bytes_ += bytes;

    Sample s;
    s.time = std::chrono::steady_clock::now();
    s.bytes = downloaded_bytes_;
    samples_.push_back(s);
}

ProgressInfo ProgressMonitor::snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);

    ProgressInfo info;
    info.total_bytes = total_bytes_;
    info.downloaded_bytes = downloaded_bytes_;

    // Progress percentage
    if (total_bytes_ > 0) {
        info.progress_percent =
            static_cast<double>(downloaded_bytes_) / static_cast<double>(total_bytes_) * 100.0;
    } else {
        info.progress_percent = 0.0;
    }

    // Sliding window speed calculation
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - std::chrono::seconds(WINDOW_SIZE_SEC);

    // Remove samples older than the window
    while (!samples_.empty() && samples_.front().time < cutoff) {
        samples_.pop_front();
    }

    if (samples_.size() >= 2) {
        const auto& oldest = samples_.front();
        const auto& newest = samples_.back();

        auto elapsed = std::chrono::duration<double>(newest.time - oldest.time).count();
        if (elapsed > 0.0) {
            int64_t byte_delta = newest.bytes - oldest.bytes;
            info.speed_bytes_per_sec = static_cast<double>(byte_delta) / elapsed;
        }
    }

    // Remaining time
    if (info.speed_bytes_per_sec > 0.0) {
        double remaining_bytes = static_cast<double>(total_bytes_ - downloaded_bytes_);
        info.remaining_seconds = static_cast<int>(remaining_bytes / info.speed_bytes_per_sec);
    } else {
        info.remaining_seconds = -1;
    }

    return info;
}
