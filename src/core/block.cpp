#include "block.h"
#include "http_engine.h"
#include "token_bucket.h"

#include <stdexcept>

Block::Block(BlockInfo info,
             const std::string& file_path,
             const std::string& url,
             HttpEngine* engine,
             TokenBucket* limiter,
             BlockProgressCallback on_progress)
    : info_(std::move(info))
    , file_path_(file_path)
    , url_(url)
    , engine_(engine)
    , limiter_(limiter)
    , on_progress_(std::move(on_progress))
{
}

Block::~Block()
{
#ifdef _WIN32
    if (file_handle_ != INVALID_HANDLE_VALUE) {
        ::CloseHandle(file_handle_);
        file_handle_ = INVALID_HANDLE_VALUE;
    }
#endif
}

void Block::execute(const HttpConfig& config)
{
    if (info_.completed) {
        return;
    }

    paused_.store(false);

#ifdef _WIN32
    // Open file for overlapped writing, shared for reading
    file_handle_ = ::CreateFileA(
        file_path_.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr);

    if (file_handle_ == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Block: failed to open file for writing: " + file_path_);
    }
#endif

    // Current write offset = range_start + already downloaded bytes
    int64_t current_offset = info_.range_start + info_.downloaded;

    // Range for the HTTP request: resume from where we left off
    int64_t range_start = current_offset;
    int64_t range_end = info_.range_end;

    // Data callback: acquire tokens, write at offset, report progress
    DataCallback on_data = [this, &current_offset](const char* data, size_t size) -> size_t {
        if (paused_.load(std::memory_order_relaxed)) {
            return 0;  // returning 0 aborts the transfer
        }

        size_t remaining = size;
        size_t total_written = 0;
        const char* ptr = data;

        while (remaining > 0) {
            if (paused_.load(std::memory_order_relaxed)) {
                return 0;
            }

            size_t chunk = remaining;

            // Acquire tokens from the rate limiter before writing
            if (limiter_) {
                int64_t granted = limiter_->acquire(static_cast<int64_t>(chunk));
                if (granted == 0) {
                    // Limiter was cancelled
                    return 0;
                }
                chunk = static_cast<size_t>(granted);
            }

            size_t written = writeAtOffset(ptr, chunk, current_offset);
            if (written == 0) {
                return 0;  // write error
            }

            current_offset += static_cast<int64_t>(written);
            info_.downloaded += static_cast<int64_t>(written);
            total_written += written;
            ptr += written;
            remaining -= written;

            // Report incremental progress to the Task
            if (on_progress_) {
                on_progress_(info_.block_id, static_cast<int64_t>(written));
            }
        }

        return total_written;
    };

    ProgressCallback on_progress = [](int64_t /*bytes_downloaded*/) {
        // Progress tracking is handled via the data callback above
    };

    try {
        engine_->download(url_, range_start, range_end, config, on_data, on_progress);

        // If we reach here without being paused, the block is complete
        if (!paused_.load(std::memory_order_relaxed)) {
            info_.completed = true;
            // Notify Task so it can detect all-blocks-done
            if (on_progress_) {
                on_progress_(info_.block_id, 0);
            }
        }
    } catch (const HttpError& e) {
        // Re-throw; the caller (Task) decides retry policy
        // Close the file handle before propagating
#ifdef _WIN32
        if (file_handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
        }
#endif
        throw;
    }

#ifdef _WIN32
    if (file_handle_ != INVALID_HANDLE_VALUE) {
        ::CloseHandle(file_handle_);
        file_handle_ = INVALID_HANDLE_VALUE;
    }
#endif
}

void Block::pause()
{
    paused_.store(true, std::memory_order_relaxed);
    if (engine_) {
        engine_->cancel();
    }
}

BlockInfo Block::getInfo() const
{
    return info_;
}

size_t Block::writeAtOffset(const char* data, size_t size, int64_t offset)
{
#ifdef _WIN32
    OVERLAPPED ov = {};
    ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
    ov.OffsetHigh = static_cast<DWORD>((offset >> 32) & 0xFFFFFFFF);

    DWORD bytes_written = 0;
    BOOL ok = ::WriteFile(file_handle_, data, static_cast<DWORD>(size), &bytes_written, &ov);
    if (!ok) {
        // For overlapped I/O, ERROR_IO_PENDING means we need to wait
        DWORD err = ::GetLastError();
        if (err == ERROR_IO_PENDING) {
            ok = ::GetOverlappedResult(file_handle_, &ov, &bytes_written, TRUE);
            if (!ok) {
                return 0;
            }
        } else {
            return 0;
        }
    }
    return static_cast<size_t>(bytes_written);
#else
    // Fallback for non-Windows (e.g. testing on Linux)
    (void)data;
    (void)size;
    (void)offset;
    return 0;
#endif
}
