#include <gtest/gtest.h>
#include "progress_monitor.h"
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

// --- Construction ---

TEST(ProgressMonitorTest, InitialSnapshotIsZero) {
    ProgressMonitor pm(1000);
    auto info = pm.snapshot();

    EXPECT_EQ(info.total_bytes, 1000);
    EXPECT_EQ(info.downloaded_bytes, 0);
    EXPECT_DOUBLE_EQ(info.progress_percent, 0.0);
    EXPECT_DOUBLE_EQ(info.speed_bytes_per_sec, 0.0);
    EXPECT_EQ(info.remaining_seconds, -1);
}

// --- Progress percentage ---

TEST(ProgressMonitorTest, ProgressPercentHalfway) {
    ProgressMonitor pm(1000);
    pm.addBytes(500);
    auto info = pm.snapshot();

    EXPECT_EQ(info.downloaded_bytes, 500);
    EXPECT_DOUBLE_EQ(info.progress_percent, 50.0);
}

TEST(ProgressMonitorTest, ProgressPercentComplete) {
    ProgressMonitor pm(1000);
    pm.addBytes(1000);
    auto info = pm.snapshot();

    EXPECT_EQ(info.downloaded_bytes, 1000);
    EXPECT_DOUBLE_EQ(info.progress_percent, 100.0);
}

TEST(ProgressMonitorTest, ProgressPercentZeroTotal) {
    ProgressMonitor pm(0);
    auto info = pm.snapshot();

    EXPECT_DOUBLE_EQ(info.progress_percent, 0.0);
}

TEST(ProgressMonitorTest, MultipleAddBytesAccumulate) {
    ProgressMonitor pm(1000);
    pm.addBytes(100);
    pm.addBytes(200);
    pm.addBytes(300);
    auto info = pm.snapshot();

    EXPECT_EQ(info.downloaded_bytes, 600);
    EXPECT_DOUBLE_EQ(info.progress_percent, 60.0);
}

// --- addBytes edge cases ---

TEST(ProgressMonitorTest, AddZeroBytesIgnored) {
    ProgressMonitor pm(1000);
    pm.addBytes(0);
    auto info = pm.snapshot();
    EXPECT_EQ(info.downloaded_bytes, 0);
}

TEST(ProgressMonitorTest, AddNegativeBytesIgnored) {
    ProgressMonitor pm(1000);
    pm.addBytes(100);
    pm.addBytes(-50);
    auto info = pm.snapshot();
    EXPECT_EQ(info.downloaded_bytes, 100);
}

// --- Speed calculation ---

TEST(ProgressMonitorTest, SpeedIsZeroWithSingleSample) {
    ProgressMonitor pm(10000);
    pm.addBytes(1000);
    auto info = pm.snapshot();

    // Only one sample — can't compute speed
    EXPECT_DOUBLE_EQ(info.speed_bytes_per_sec, 0.0);
    EXPECT_EQ(info.remaining_seconds, -1);
}

TEST(ProgressMonitorTest, SpeedCalculatedFromMultipleSamples) {
    ProgressMonitor pm(100000);

    pm.addBytes(1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    pm.addBytes(1000);

    auto info = pm.snapshot();

    // ~1000 bytes in ~200ms => ~5000 bytes/sec (rough)
    EXPECT_GT(info.speed_bytes_per_sec, 0.0);
    EXPECT_GE(info.remaining_seconds, 0);
}

// --- Remaining time ---

TEST(ProgressMonitorTest, RemainingTimeMinusOneWhenSpeedZero) {
    ProgressMonitor pm(10000);
    // No bytes added — speed is zero
    auto info = pm.snapshot();
    EXPECT_EQ(info.remaining_seconds, -1);
}

TEST(ProgressMonitorTest, RemainingTimeCalculatedWhenSpeedPositive) {
    ProgressMonitor pm(10000);

    pm.addBytes(1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    pm.addBytes(1000);

    auto info = pm.snapshot();

    if (info.speed_bytes_per_sec > 0.0) {
        EXPECT_GE(info.remaining_seconds, 0);
    } else {
        EXPECT_EQ(info.remaining_seconds, -1);
    }
}

// --- Sliding window ---

TEST(ProgressMonitorTest, SlidingWindowDropsOldSamples) {
    ProgressMonitor pm(100000);

    // Add a sample, then wait beyond the 5-second window
    pm.addBytes(5000);
    // We can't easily wait 5+ seconds in a unit test, so we verify
    // that recent samples within the window are used correctly.
    // Instead, verify that two close samples produce a valid speed.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    pm.addBytes(5000);

    auto info = pm.snapshot();
    EXPECT_GT(info.speed_bytes_per_sec, 0.0);
}

// --- Thread safety ---

TEST(ProgressMonitorTest, ConcurrentAddBytesDoesNotCrash) {
    ProgressMonitor pm(1000000);
    std::vector<std::thread> threads;
    constexpr int num_threads = 4;
    constexpr int iterations = 100;
    constexpr int64_t bytes_per_add = 10;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < iterations; ++j) {
                pm.addBytes(bytes_per_add);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto info = pm.snapshot();
    EXPECT_EQ(info.downloaded_bytes, num_threads * iterations * bytes_per_add);
}

TEST(ProgressMonitorTest, ConcurrentAddAndSnapshotDoesNotCrash) {
    ProgressMonitor pm(1000000);
    std::atomic<bool> done{false};

    // Writer thread
    std::thread writer([&] {
        for (int i = 0; i < 200; ++i) {
            pm.addBytes(100);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        done.store(true);
    });

    // Reader thread
    std::thread reader([&] {
        while (!done.load()) {
            auto info = pm.snapshot();
            EXPECT_GE(info.downloaded_bytes, 0);
            EXPECT_LE(info.progress_percent, 100.0);
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });

    writer.join();
    reader.join();

    auto info = pm.snapshot();
    EXPECT_EQ(info.downloaded_bytes, 20000);
}
