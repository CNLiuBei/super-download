// token_bucket.cpp
#include "token_bucket.h"
#include <algorithm>

TokenBucket::TokenBucket(int64_t rate_bytes_per_sec)
    : rate_(rate_bytes_per_sec)
    , tokens_(rate_bytes_per_sec)  // start with a full bucket
    , max_tokens_(rate_bytes_per_sec)
    , last_refill_(std::chrono::steady_clock::now())
{
}

void TokenBucket::refill() {
    // Must be called with mutex_ held.
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        now - last_refill_).count();

    if (elapsed <= 0 || rate_ <= 0) {
        return;
    }

    // tokens to add = rate * elapsed_seconds = rate * elapsed_us / 1'000'000
    int64_t new_tokens = static_cast<int64_t>(
        static_cast<double>(rate_) * static_cast<double>(elapsed) / 1'000'000.0);

    if (new_tokens > 0) {
        tokens_ = std::min(tokens_ + new_tokens, max_tokens_);
        last_refill_ = now;
    }
}

int64_t TokenBucket::acquire(int64_t tokens) {
    if (tokens <= 0) {
        return 0;
    }

    std::unique_lock<std::mutex> lock(mutex_);

    // No rate limiting — pass through immediately
    if (rate_ == 0) {
        return tokens;
    }

    while (true) {
        if (cancelled_.load(std::memory_order_relaxed)) {
            return 0;
        }

        refill();

        if (tokens_ >= tokens) {
            tokens_ -= tokens;
            return tokens;
        }

        // Not enough tokens — figure out how long to wait for them.
        int64_t deficit = tokens - tokens_;
        // wait_us = deficit / rate * 1'000'000
        auto wait_us = static_cast<int64_t>(
            static_cast<double>(deficit) / static_cast<double>(rate_) * 1'000'000.0);
        if (wait_us < 1000) {
            wait_us = 1000; // minimum 1 ms to avoid busy-spin
        }

        cv_.wait_for(lock, std::chrono::microseconds(wait_us), [this] {
            return cancelled_.load(std::memory_order_relaxed) || rate_ == 0;
        });

        // After waking up, re-check: rate may have changed to 0 (unlimited)
        if (rate_ == 0) {
            return tokens;
        }
    }
}

void TokenBucket::setRate(int64_t rate_bytes_per_sec) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Refill with the old rate before switching
    refill();

    rate_ = rate_bytes_per_sec;
    max_tokens_ = rate_bytes_per_sec;

    // Clamp current tokens to new capacity
    if (max_tokens_ > 0 && tokens_ > max_tokens_) {
        tokens_ = max_tokens_;
    }

    // Wake up all waiters so they re-evaluate with the new rate
    cv_.notify_all();
}

int64_t TokenBucket::getRate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rate_;
}

void TokenBucket::cancel() {
    cancelled_.store(true, std::memory_order_relaxed);
    cv_.notify_all();
}
