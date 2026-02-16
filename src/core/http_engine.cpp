#include "http_engine.h"

#include <atomic>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>
#include <curl/curl.h>

// ── Pimpl ──────────────────────────────────────────────────────

struct HttpEngine::Impl {
    CURL* curl = nullptr;
    std::atomic<bool> cancelled{false};

    Impl() {
        curl = curl_easy_init();
        if (!curl) {
            throw HttpError("Failed to initialise CURL easy handle");
        }
    }

    ~Impl() {
        if (curl) {
            curl_easy_cleanup(curl);
        }
    }

    void reset() {
        curl_easy_reset(curl);
        cancelled.store(false);
    }

    // ── Common configuration applied to every request ──────────
    void applyConfig(const HttpConfig& config) {
        // User-Agent (many servers reject requests without one)
        curl_easy_setopt(curl, CURLOPT_USERAGENT,
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36");

        // Accept headers (mimic browser)
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: */*");
        headers = curl_slist_append(headers, "Accept-Language: en-US,en;q=0.9,zh-CN;q=0.8,zh;q=0.7");
        headers = curl_slist_append(headers, "Connection: keep-alive");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Enable TCP keep-alive for connection reuse
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 60L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 30L);

        // Redirects
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, static_cast<long>(config.max_redirects));

        // TLS
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, config.verify_ssl ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, config.verify_ssl ? 2L : 0L);

        // Timeouts
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(config.connect_timeout_sec));
        if (config.transfer_timeout_sec > 0)
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(config.transfer_timeout_sec));

        // Low-speed abort: detect stalled connections
        if (config.low_speed_limit > 0) {
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, static_cast<long>(config.low_speed_limit));
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, static_cast<long>(config.low_speed_time));
        }

        // HTTP basic auth
        if (!config.username.empty()) {
            std::string userpwd = config.username + ":" + config.password;
            curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        }

        // Referer header (from browser interception)
        if (!config.referer.empty()) {
            curl_easy_setopt(curl, CURLOPT_REFERER, config.referer.c_str());
        }

        // Cookie header (from browser interception)
        if (!config.cookie.empty()) {
            curl_easy_setopt(curl, CURLOPT_COOKIE, config.cookie.c_str());
        }
    }
};

// ── Static helpers ─────────────────────────────────────────────

namespace {

/// Trim leading/trailing whitespace.
std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

/// Case-insensitive header name comparison.
bool headerNameEquals(const std::string& header, const std::string& name) {
    if (header.size() < name.size()) return false;
    for (size_t i = 0; i < name.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(header[i])) !=
            std::tolower(static_cast<unsigned char>(name[i]))) {
            return false;
        }
    }
    return true;
}

// ── HEAD-request header callback context ───────────────────────

struct HeadContext {
    FileInfo info;
};

size_t headHeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t total = size * nitems;
    auto* ctx = static_cast<HeadContext*>(userdata);
    std::string line(buffer, total);

    auto colon = line.find(':');
    if (colon == std::string::npos) return total;

    std::string name = line.substr(0, colon);
    std::string value = trim(line.substr(colon + 1));

    if (headerNameEquals(name, "Content-Length")) {
        try { ctx->info.content_length = std::stoll(value); }
        catch (...) { /* ignore parse errors */ }
    } else if (headerNameEquals(name, "Accept-Ranges")) {
        ctx->info.accept_ranges = (value != "none");
    } else if (headerNameEquals(name, "ETag")) {
        ctx->info.etag = value;
    } else if (headerNameEquals(name, "Last-Modified")) {
        ctx->info.last_modified = value;
    } else if (headerNameEquals(name, "Content-Type")) {
        ctx->info.content_type = value;
    } else if (headerNameEquals(name, "Content-Disposition")) {
        ctx->info.content_disposition = value;
    }

    return total;
}

// ── Download write / progress callback contexts ────────────────

struct DownloadContext {
    DataCallback on_data;
    ProgressCallback on_progress;
    int64_t bytes_downloaded = 0;
    std::atomic<bool>* cancelled = nullptr;
};

size_t downloadWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<DownloadContext*>(userdata);
    size_t total = size * nmemb;

    if (ctx->cancelled && ctx->cancelled->load(std::memory_order_relaxed)) {
        return 0; // returning 0 aborts the transfer
    }

    size_t consumed = 0;
    if (ctx->on_data) {
        consumed = ctx->on_data(ptr, total);
    } else {
        consumed = total; // discard if no callback
    }

    ctx->bytes_downloaded += static_cast<int64_t>(consumed);

    if (ctx->on_progress) {
        ctx->on_progress(ctx->bytes_downloaded);
    }

    return consumed;
}

/// CURL progress callback – used solely to check the cancel flag.
int progressFunction(void* clientp,
                     curl_off_t /*dltotal*/, curl_off_t /*dlnow*/,
                     curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* cancelled = static_cast<std::atomic<bool>*>(clientp);
    if (cancelled && cancelled->load(std::memory_order_relaxed)) {
        return 1; // non-zero aborts the transfer
    }
    return 0;
}

/// Determine whether a CURL error code represents a transient/retryable failure.
bool isRetryableCurlCode(CURLcode code) {
    switch (code) {
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_COULDNT_CONNECT:
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_GOT_NOTHING:
        case CURLE_RECV_ERROR:
        case CURLE_SEND_ERROR:
        case CURLE_PARTIAL_FILE:
            return true;
        default:
            return false;
    }
}

/// Determine whether an HTTP status code is non-retryable (client errors).
bool isNonRetryableHttpStatus(long http_code) {
    return http_code >= 400 && http_code < 500;
}

/// Determine whether a CURL error code is a TLS certificate failure (non-retryable).
bool isTlsCertError(CURLcode code) {
    switch (code) {
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_PEER_FAILED_VERIFICATION:  // same as CURLE_SSL_CACERT in newer libcurl
            return true;
        default:
            return false;
    }
}

/// Backoff intervals in seconds for retry attempts: 1s, 2s, 4s.
constexpr int kRetryBackoffSec[] = {1, 2, 4};

} // anonymous namespace

// ── HttpEngine public API ──────────────────────────────────────

HttpEngine::HttpEngine() : impl_(std::make_unique<Impl>()) {}

HttpEngine::~HttpEngine() = default;

FileInfo HttpEngine::fetchFileInfo(const std::string& url, const HttpConfig& config) {
    // Try HEAD first, fall back to GET if HEAD returns 403/405
    for (int method = 0; method < 2; ++method) {
        bool use_get = (method == 1);

        const int max_attempts = config.max_retries + 1;
        HttpError last_error("Unknown error");
        bool got_forbidden = false;

        for (int attempt = 0; attempt < max_attempts; ++attempt) {
            if (attempt > 0) {
                int backoff_index = std::min(attempt - 1,
                    static_cast<int>(sizeof(kRetryBackoffSec) / sizeof(kRetryBackoffSec[0])) - 1);
                std::this_thread::sleep_for(std::chrono::seconds(kRetryBackoffSec[backoff_index]));

                if (impl_->cancelled.load(std::memory_order_relaxed)) {
                    throw HttpError("Request cancelled", 0, 0, false);
                }
            }

            try {
                impl_->reset();
                CURL* curl = impl_->curl;

                HeadContext ctx;

                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headHeaderCallback);
                curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ctx);

                if (use_get) {
                    // GET request but abort after receiving headers
                    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
                    // Use a write callback that aborts immediately
                    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                        +[](char*, size_t size, size_t nmemb, void*) -> size_t {
                            // Return 0 to abort the transfer after we got headers
                            (void)size; (void)nmemb;
                            return 0;
                        });
                } else {
                    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);  // HEAD
                }

                curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
                curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressFunction);
                curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &impl_->cancelled);

                impl_->applyConfig(config);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

                CURLcode res = curl_easy_perform(curl);

                long http_code = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

                // For GET fallback, CURLE_WRITE_ERROR is expected (we abort on purpose)
                // Treat as success if HTTP status is OK, or check HTTP status if it's an error
                if (use_get && res == CURLE_WRITE_ERROR) {
                    if (http_code > 0 && http_code < 400) {
                        res = CURLE_OK;  // success — we got headers
                    } else if (http_code >= 400) {
                        // Server returned an error, handle via http_code check below
                        res = CURLE_OK;
                    }
                }

                if (res != CURLE_OK) {
                    if (impl_->cancelled.load()) {
                        throw HttpError("Request cancelled", static_cast<int>(res), http_code, false);
                    }

                    bool retryable = isRetryableCurlCode(res);

                    if (isTlsCertError(res)) {
                        throw HttpError(std::string("Request failed: ") + curl_easy_strerror(res),
                                        static_cast<int>(res), http_code, false);
                    }

                    std::string msg = use_get ? "GET info failed: " : "HEAD request failed: ";
                    msg += curl_easy_strerror(res);
                    last_error = HttpError(msg, static_cast<int>(res), http_code, retryable);

                    if (!retryable) throw last_error;
                    continue;
                }

                if (http_code >= 400) {
                    // HEAD returned 403 or 405 → try GET fallback
                    if (!use_get && (http_code == 403 || http_code == 405)) {
                        got_forbidden = true;
                        break;  // break retry loop, try GET
                    }
                    bool retryable = !isNonRetryableHttpStatus(http_code);
                    last_error = HttpError("HTTP error " + std::to_string(http_code),
                                           0, http_code, retryable);
                    if (!retryable) throw last_error;
                    continue;
                }

                // Success — extract info
                char* effective_url = nullptr;
                curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective_url);
                if (effective_url) {
                    ctx.info.final_url = effective_url;
                }

                if (ctx.info.content_length < 0) {
                    curl_off_t cl = -1;
                    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
                    if (cl >= 0) {
                        ctx.info.content_length = static_cast<int64_t>(cl);
                    }
                }

                return ctx.info;
            } catch (const HttpError&) {
                throw;
            } catch (const std::exception& e) {
                last_error = HttpError(e.what(), 0, 0, false);
                throw last_error;
            }
        }

        // If HEAD got 403/405, continue to GET fallback
        if (got_forbidden && !use_get) continue;

        // All retries exhausted for this method
        if (!got_forbidden) throw last_error;
    }

    throw HttpError("Failed to fetch file info", 0, 403, false);
}

void HttpEngine::download(const std::string& url,
                          int64_t range_start,
                          int64_t range_end,
                          const HttpConfig& config,
                          DataCallback on_data,
                          ProgressCallback on_progress) {
    const int max_attempts = config.max_retries + 1; // first attempt + retries
    HttpError last_error("Unknown error");

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        // Check cancellation before each attempt (including the first)
        if (impl_->cancelled.load(std::memory_order_relaxed)) {
            throw HttpError("Download cancelled", 0, 0, false);
        }

        // Wait before retry (not before the first attempt)
        if (attempt > 0) {
            int backoff_index = std::min(attempt - 1,
                static_cast<int>(sizeof(kRetryBackoffSec) / sizeof(kRetryBackoffSec[0])) - 1);
            std::this_thread::sleep_for(std::chrono::seconds(kRetryBackoffSec[backoff_index]));

            // Check cancellation after sleeping
            if (impl_->cancelled.load(std::memory_order_relaxed)) {
                throw HttpError("Download cancelled", 0, 0, false);
            }
        }

        try {
            impl_->reset();
            CURL* curl = impl_->curl;

            DownloadContext ctx;
            ctx.on_data = on_data;       // copy, not move – needed across retries
            ctx.on_progress = on_progress;
            ctx.cancelled = &impl_->cancelled;

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, downloadWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

            // Cancel support
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressFunction);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &impl_->cancelled);

            // Range header
            if (range_start >= 0) {
                std::string range = std::to_string(range_start) + "-";
                if (range_end >= 0) {
                    range += std::to_string(range_end);
                }
                curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
            }

            impl_->applyConfig(config);

            CURLcode res = curl_easy_perform(curl);

            if (res != CURLE_OK) {
                long http_code = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

                if (impl_->cancelled.load()) {
                    throw HttpError("Download cancelled", static_cast<int>(res), http_code, false);
                }

                bool retryable = isRetryableCurlCode(res);

                // TLS cert errors are never retryable
                if (isTlsCertError(res)) {
                    throw HttpError(std::string("Download failed: ") + curl_easy_strerror(res),
                                    static_cast<int>(res), http_code, false);
                }

                std::string msg = "Download failed: ";
                msg += curl_easy_strerror(res);
                last_error = HttpError(msg, static_cast<int>(res), http_code, retryable);

                if (!retryable) {
                    throw last_error;
                }
                continue; // retry
            }

            // Check HTTP status
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code >= 400) {
                bool retryable = !isNonRetryableHttpStatus(http_code);
                last_error = HttpError("HTTP error " + std::to_string(http_code),
                                       0, http_code, retryable);
                if (!retryable) {
                    throw last_error;
                }
                continue; // retry on 5xx
            }

            return; // success
        } catch (const HttpError&) {
            throw; // re-throw HttpErrors (already handled above)
        } catch (const std::exception& e) {
            last_error = HttpError(e.what(), 0, 0, false);
            throw last_error;
        }
    }

    // All retries exhausted
    throw last_error;
}

void HttpEngine::cancel() {
    impl_->cancelled.store(true, std::memory_order_relaxed);
}
