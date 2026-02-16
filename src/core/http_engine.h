#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

/// Information retrieved from a HEAD request.
struct FileInfo {
    int64_t content_length = -1;   // File size; -1 = unknown
    bool accept_ranges = false;     // Server supports Range requests
    std::string etag;
    std::string last_modified;
    std::string content_type;
    std::string final_url;          // URL after redirects
    std::string content_disposition; // Content-Disposition header (for filename)
};

/// Per-request HTTP configuration.
struct HttpConfig {
    int connect_timeout_sec = 30;
    int transfer_timeout_sec = 0;   // 0 = no total transfer timeout (large files need unlimited time)
    int low_speed_limit = 1000;     // abort if speed drops below 1000 bytes/sec
    int low_speed_time = 60;        // ... for 60 seconds
    int max_redirects = 10;
    int max_retries = 3;
    bool verify_ssl = true;
    std::string username;
    std::string password;
    std::string referer;            // Referer header (from browser)
    std::string cookie;             // Cookie header (from browser)
};

/// Data callback: receives a chunk, returns bytes consumed.
using DataCallback = std::function<size_t(const char* data, size_t size)>;

/// Progress callback: total bytes downloaded so far.
using ProgressCallback = std::function<void(int64_t bytes_downloaded)>;

/// Exception thrown on HTTP / network errors.
class HttpError : public std::runtime_error {
public:
    explicit HttpError(const std::string& what,
                       int curl_code = 0,
                       long http_status = 0,
                       bool retryable = false)
        : std::runtime_error(what),
          curl_code_(curl_code),
          http_status_(http_status),
          retryable_(retryable) {}

    int curlCode() const noexcept { return curl_code_; }
    long httpStatus() const noexcept { return http_status_; }
    bool isRetryable() const noexcept { return retryable_; }

private:
    int curl_code_;
    long http_status_;
    bool retryable_;
};

/// Synchronous HTTP engine wrapping a libcurl easy handle (Pimpl).
/// Each instance owns one CURL handle – not thread-safe; use one per thread.
class HttpEngine {
public:
    HttpEngine();
    ~HttpEngine();

    HttpEngine(const HttpEngine&) = delete;
    HttpEngine& operator=(const HttpEngine&) = delete;

    /// Send a HEAD request and return file metadata.
    FileInfo fetchFileInfo(const std::string& url, const HttpConfig& config);

    /// Download a byte range (or full file when range_start == range_end == -1).
    /// Data is delivered through on_data; progress through on_progress.
    void download(const std::string& url,
                  int64_t range_start,
                  int64_t range_end,
                  const HttpConfig& config,
                  DataCallback on_data,
                  ProgressCallback on_progress);

    /// Cancel the current in-flight request (safe to call from another thread).
    void cancel();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
