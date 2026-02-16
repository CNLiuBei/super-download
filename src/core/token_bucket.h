// token_bucket.h
#pragma once
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <cstdint>

class TokenBucket {
public:
    // rate_bytes_per_sec = 0 means no rate limiting
    explicit TokenBucket(int64_t rate_bytes_per_sec = 0);

    // Acquire the specified number of tokens, blocking when insufficient.
    // Returns the number of tokens actually acquired (0 when cancelled).
    int64_t acquire(int64_t tokens);

    // Dynamically adjust the rate. 0 means no rate limiting.
    void setRate(int64_t rate_bytes_per_sec);

    // Get the current rate.
    int64_t getRate() const;

    // Cancel all waiting threads.
    void cancel();

private:
    void refill();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    int64_t rate_;              // token generation rate (bytes/sec)
    int64_t tokens_;            // currently available tokens
    int64_t max_tokens_;        // bucket capacity (= rate, i.e. 1 second worth)
    std::chrono::steady_clock::time_point last_refill_;
    std::atomic<bool> cancelled_{false};
};
