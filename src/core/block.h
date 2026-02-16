#pragma once

#include <string>
#include <cstdint>
#include <atomic>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#endif

#include "meta_file.h"  // BlockInfo is defined here

// Forward declarations
class HttpEngine;
class TokenBucket;
struct HttpConfig;

using BlockProgressCallback = std::function<void(int block_id, int64_t bytes_delta)>;

class Block {
public:
    Block(BlockInfo info,
          const std::string& file_path,
          const std::string& url,
          HttpEngine* engine,
          TokenBucket* limiter,
          BlockProgressCallback on_progress);

    ~Block();

    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;

    /// Execute the download (called from a thread-pool worker).
    void execute(const HttpConfig& config);

    /// Request pause – sets a flag checked inside the data callback.
    void pause();

    /// Return a snapshot of the current block state.
    BlockInfo getInfo() const;

private:
    /// Write data at the given file offset using overlapped I/O.
    size_t writeAtOffset(const char* data, size_t size, int64_t offset);

    BlockInfo info_;
    std::string file_path_;
    std::string url_;
    HttpEngine* engine_;          // non-owning
    TokenBucket* limiter_;        // non-owning, may be nullptr
    BlockProgressCallback on_progress_;
    std::atomic<bool> paused_{false};

#ifdef _WIN32
    HANDLE file_handle_ = INVALID_HANDLE_VALUE;
#endif
};
