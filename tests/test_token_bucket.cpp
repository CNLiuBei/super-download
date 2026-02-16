#include <gtest/gtest.h>
#include "token_bucket.h"
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

// --- Basic construction and getRate ---

TEST(TokenBucketTest, DefaultConstructorNoRateLimit) {
    TokenBucket tb;
    EXPECT_EQ(tb.getRate(), 0);
}

TEST(TokenBucketTest, ConstructorSetsRate) {
    TokenBucket tb(1024);
    EXPECT_EQ(tb.getRate(), 1024);
}

// --- acquire() with rate=0 (no limiting) ---

TEST(TokenBucketTest, AcquireWithZeroRateReturnsImmediately) {
    TokenBucket tb(0);
    // Should return immediately regardless of token count
    EXPECT_EQ(tb.acquire(999999), 999999);
    EXPECT_EQ(tb.acquire(1), 1);
}

TEST(TokenBucketTest, AcquireZeroTokensReturnsZero) {
    TokenBucket tb(1024);
    EXPECT_EQ(tb.acquire(0), 0);
    EXPECT_EQ(tb.acquire(-1), 0);
}

// --- acquire() with rate limiting ---

TEST(TokenBucketTest, AcquireWithinBucketSucceeds) {
    // Bucket starts full with rate tokens
    TokenBucket tb(1000);
    // Requesting <= rate should succeed immediately
    EXPECT_EQ(tb.acquire(500), 500);
    EXPECT_EQ(tb.acquire(500), 500);
}

TEST(TokenBucketTest, AcquireBlocksWhenInsufficientTokens) {
    TokenBucket tb(1000);
    // Drain the bucket
    tb.acquire(1000);

    auto start = std::chrono::steady_clock::now();
    // Need to wait for refill — request a small amount
    int64_t got = tb.acquire(100);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(got, 100);
    // Should have waited at least ~100ms (100 tokens at 1000/sec)
    EXPECT_GE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 50);
}

// --- setRate() dynamic adjustment ---

TEST(TokenBucketTest, SetRateChangesRate) {
    TokenBucket tb(1000);
    tb.setRate(2000);
    EXPECT_EQ(tb.getRate(), 2000);
}

TEST(TokenBucketTest, SetRateToZeroUnlimits) {
    TokenBucket tb(100);
    // Drain the bucket
    tb.acquire(100);

    // In a separate thread, acquire should block
    std::atomic<int64_t> result{-1};
    std::thread t([&] {
        result.store(tb.acquire(50));
    });

    // Give the thread time to start waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Switch to unlimited — should wake the waiting thread
    tb.setRate(0);
    t.join();

    EXPECT_EQ(result.load(), 50);
    EXPECT_EQ(tb.getRate(), 0);
}

TEST(TokenBucketTest, SetRateImmediateEffect) {
    TokenBucket tb(100);
    tb.acquire(100); // drain

    // Increase rate significantly — refill should be faster
    tb.setRate(100000);

    auto start = std::chrono::steady_clock::now();
    int64_t got = tb.acquire(1000);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(got, 1000);
    // At 100000 bytes/sec, 1000 tokens should take ~10ms at most
    EXPECT_LE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 500);
}

// --- cancel() ---

TEST(TokenBucketTest, CancelWakesWaitingThreads) {
    TokenBucket tb(100);
    tb.acquire(100); // drain

    std::atomic<int64_t> result{-1};
    std::thread t([&] {
        result.store(tb.acquire(50));
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    tb.cancel();
    t.join();

    // Cancelled acquire returns 0
    EXPECT_EQ(result.load(), 0);
}

TEST(TokenBucketTest, AcquireAfterCancelReturnsZero) {
    TokenBucket tb(1000);
    tb.cancel();
    EXPECT_EQ(tb.acquire(100), 0);
}

// --- Concurrent access ---

TEST(TokenBucketTest, ConcurrentAcquireDoesNotCrash) {
    TokenBucket tb(10000);
    std::vector<std::thread> threads;
    std::atomic<int64_t> total{0};

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < 10; ++j) {
                int64_t got = tb.acquire(100);
                total.fetch_add(got);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // All 40 acquires of 100 tokens each should succeed
    EXPECT_EQ(total.load(), 4000);
}

// --- refill behavior ---

TEST(TokenBucketTest, RefillAddsTokensOverTime) {
    TokenBucket tb(1000);
    tb.acquire(1000); // drain

    // Wait ~200ms, should refill ~200 tokens
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Should be able to acquire some tokens without long blocking
    auto start = std::chrono::steady_clock::now();
    int64_t got = tb.acquire(100);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(got, 100);
    // Should not have needed to wait long since tokens refilled during sleep
    EXPECT_LE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 100);
}
