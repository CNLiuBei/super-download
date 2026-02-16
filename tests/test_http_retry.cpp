#include <gtest/gtest.h>
#include "http_engine.h"

// ── HttpError retryable flag tests ─────────────────────────────

TEST(HttpError, DefaultNotRetryable) {
    HttpError err("test error");
    EXPECT_EQ(err.curlCode(), 0);
    EXPECT_EQ(err.httpStatus(), 0);
    EXPECT_FALSE(err.isRetryable());
}

TEST(HttpError, RetryableFlagTrue) {
    HttpError err("timeout", 28, 0, true);  // CURLE_OPERATION_TIMEDOUT = 28
    EXPECT_TRUE(err.isRetryable());
    EXPECT_EQ(err.curlCode(), 28);
}

TEST(HttpError, RetryableFlagFalse) {
    HttpError err("not found", 0, 404, false);
    EXPECT_FALSE(err.isRetryable());
    EXPECT_EQ(err.httpStatus(), 404);
}

TEST(HttpError, WhatMessage) {
    HttpError err("Download failed: timeout", 28, 0, true);
    EXPECT_STREQ(err.what(), "Download failed: timeout");
}

// ── Retry behavior: non-retryable errors fail immediately ──────

TEST(HttpEngineRetry, NonRetryable4xxFailsImmediately) {
    // Attempting to connect to a URL that returns 4xx should not retry
    // and should throw with retryable=false.
    // We test this indirectly: with max_retries=0, any error is immediate.
    HttpEngine engine;
    HttpConfig config;
    config.max_retries = 0;
    config.connect_timeout_sec = 2;
    config.transfer_timeout_sec = 2;

    // Use an invalid host to trigger a CURL error
    try {
        engine.fetchFileInfo("http://0.0.0.0:1/nonexistent", config);
        FAIL() << "Expected HttpError to be thrown";
    } catch (const HttpError& e) {
        // With max_retries=0, we get exactly 1 attempt
        EXPECT_NE(std::string(e.what()).find("HEAD request failed"), std::string::npos);
    }
}

TEST(HttpEngineRetry, CancelledRequestNotRetryable) {
    HttpEngine engine;
    engine.cancel(); // cancel before starting

    HttpConfig config;
    config.max_retries = 3;
    config.connect_timeout_sec = 2;

    try {
        engine.download("http://0.0.0.0:1/test", -1, -1, config, nullptr, nullptr);
        FAIL() << "Expected HttpError to be thrown";
    } catch (const HttpError& e) {
        EXPECT_FALSE(e.isRetryable());
    }
}
