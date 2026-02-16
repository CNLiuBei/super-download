#include "task.h"
#include "http_engine.h"
#include "block_splitter.h"
#include "thread_pool.h"
#include "token_bucket.h"
#include "file_classifier.h"
#include "logger.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

// ── Constructor ────────────────────────────────────────────────

Task::Task(int task_id,
           const std::string& url,
           const std::string& save_dir,
           int max_blocks,
           ThreadPool* pool,
           TokenBucket* limiter,
           FileClassifier* classifier,
           TaskStateCallback on_state_change,
           const std::string& referer,
           const std::string& cookie)
    : task_id_(task_id)
    , url_(url)
    , save_dir_(save_dir)
    , max_blocks_(std::clamp(max_blocks, 1, 32))
    , pool_(pool)
    , limiter_(limiter)
    , classifier_(classifier)
    , on_state_change_(std::move(on_state_change))
    , referer_(referer)
    , cookie_(cookie)
{
    file_name_ = extractFileName(url_);
    file_path_ = (fs::path(save_dir_) / file_name_).string();
    meta_path_ = buildMetaPath();
}

// ── fromMeta (static factory) ──────────────────────────────────

std::unique_ptr<Task> Task::fromMeta(
    const std::string& meta_path,
    ThreadPool* pool,
    TokenBucket* limiter,
    FileClassifier* classifier,
    TaskStateCallback on_state_change)
{
    auto meta_opt = MetaFile::load(meta_path);
    if (!meta_opt) {
        return nullptr;
    }

    const TaskMeta& meta = *meta_opt;

    // Extract save_dir from file_path
    std::string save_dir = fs::path(meta.file_path).parent_path().string();

    auto task = std::unique_ptr<Task>(new Task(
        0,  // task_id will be assigned by DownloadManager
        meta.url,
        save_dir,
        meta.max_blocks,
        pool,
        limiter,
        classifier,
        std::move(on_state_change)));

    // Restore state from meta
    task->file_name_ = meta.file_name;
    task->file_path_ = meta.file_path;
    task->file_size_ = meta.file_size;
    task->etag_ = meta.etag;
    task->last_modified_ = meta.last_modified;
    task->meta_path_ = meta_path;
    task->accept_ranges_ = true;  // if we have blocks, range was supported

    // Calculate already-downloaded bytes
    int64_t already_downloaded = 0;
    for (const auto& bi : meta.blocks) {
        already_downloaded += bi.downloaded;
    }

    // Create progress monitor with existing progress
    task->progress_ = std::make_unique<ProgressMonitor>(meta.file_size);
    if (already_downloaded > 0) {
        task->progress_->addBytes(already_downloaded);
    }

    task->state_.store(TaskState::Paused);

    return task;
}

// ── start ──────────────────────────────────────────────────────

void Task::start()
{
    TaskState expected = TaskState::Queued;
    if (!state_.compare_exchange_strong(expected, TaskState::Downloading)) {
        return;  // not in Queued state
    }
    setState(TaskState::Downloading);

    // Submit the fetch+start sequence to the thread pool so we don't block
    pool_->submit([this]() {
        try {
            fetchFileInfoAndStart();
        } catch (const HttpError& e) {
            error_message_ = std::string(e.what())
                + " (HTTP " + std::to_string(e.httpStatus()) + ")";
            Logger::instance().error("Task " + std::to_string(task_id_)
                + " failed: " + e.what()
                + " (curl=" + std::to_string(e.curlCode())
                + " http=" + std::to_string(e.httpStatus()) + ")");

            // Auto-retry on retryable errors
            if (e.isRetryable() && auto_retry_count_ < kMaxAutoRetries) {
                ++auto_retry_count_;
                Logger::instance().info("Task " + std::to_string(task_id_)
                    + " auto-retry " + std::to_string(auto_retry_count_)
                    + "/" + std::to_string(kMaxAutoRetries));
                state_.store(TaskState::Queued);
                std::this_thread::sleep_for(std::chrono::seconds(2 * auto_retry_count_));
                start();
                return;
            }
            setState(TaskState::Failed);
        } catch (const std::exception& e) {
            error_message_ = e.what();
            Logger::instance().error("Task " + std::to_string(task_id_)
                + " failed: " + e.what());
            setState(TaskState::Failed);
        }
    });
}

// ── fetchFileInfoAndStart ──────────────────────────────────────

void Task::fetchFileInfoAndStart()
{
    Logger::instance().info("Task " + std::to_string(task_id_)
        + " fetching file info: " + url_);

    // Create a temporary HttpEngine for the HEAD request
    HttpEngine head_engine;
    HttpConfig config;
    config.referer = referer_;
    config.cookie = cookie_;

    FileInfo info = head_engine.fetchFileInfo(url_, config);

    Logger::instance().info("Task " + std::to_string(task_id_)
        + " HEAD result: size=" + std::to_string(info.content_length)
        + " ranges=" + (info.accept_ranges ? "yes" : "no")
        + " type=" + info.content_type
        + " final_url=" + info.final_url);

    file_size_ = info.content_length;
    accept_ranges_ = info.accept_ranges;
    etag_ = info.etag;
    last_modified_ = info.last_modified;

    // Update final URL if redirected
    if (!info.final_url.empty()) {
        url_ = info.final_url;
    }

    // Try to get filename from Content-Disposition header first
    if (!info.content_disposition.empty()) {
        std::string cd_name = parseContentDisposition(info.content_disposition);
        if (!cd_name.empty()) {
            file_name_ = cd_name;
            Logger::instance().info("Task " + std::to_string(task_id_)
                + " filename from Content-Disposition: " + file_name_);
        }
    }

    // If still the URL-extracted name, try the final URL too
    if (file_name_ == extractFileName(url_) && !info.final_url.empty()) {
        std::string final_name = extractFileName(info.final_url);
        if (final_name != "download" && !final_name.empty()) {
            file_name_ = final_name;
        }
    }

    // Resolve file name conflicts (add (1), (2), etc.)
    file_name_ = resolveConflict(save_dir_, file_name_);
    file_path_ = (fs::path(save_dir_) / file_name_).string();
    meta_path_ = buildMetaPath();

    // If file_size is unknown, use single block mode
    if (file_size_ <= 0) {
        accept_ranges_ = false;
        file_size_ = 0;  // will grow as we download
    }

    // Pre-allocate file on disk
    if (file_size_ > 0) {
        allocateFile();
    }

    // Create progress monitor
    progress_ = std::make_unique<ProgressMonitor>(file_size_);

    // Create and submit blocks
    createBlocks();
    saveMeta();
    submitBlocks();
}

// ── allocateFile ───────────────────────────────────────────────

void Task::allocateFile()
{
    // Ensure the directory exists
    fs::path dir = fs::path(file_path_).parent_path();
    if (!dir.empty() && !fs::exists(dir)) {
        fs::create_directories(dir);
    }

#ifdef _WIN32
    HANDLE hFile = ::CreateFileA(
        file_path_.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (hFile == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Task: failed to create file for pre-allocation: " + file_path_);
    }

    // Move file pointer to the desired size
    LARGE_INTEGER li;
    li.QuadPart = file_size_;
    if (!::SetFilePointerEx(hFile, li, nullptr, FILE_BEGIN)) {
        ::CloseHandle(hFile);
        throw std::runtime_error("Task: SetFilePointerEx failed for: " + file_path_);
    }

    // Set the end of file at the current pointer position
    if (!::SetEndOfFile(hFile)) {
        ::CloseHandle(hFile);
        throw std::runtime_error("Task: SetEndOfFile failed for: " + file_path_);
    }

    ::CloseHandle(hFile);
#else
    // Non-Windows fallback: create file and truncate to size
    std::ofstream ofs(file_path_, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        throw std::runtime_error("Task: failed to create file: " + file_path_);
    }
    if (file_size_ > 0) {
        ofs.seekp(file_size_ - 1);
        ofs.put('\0');
    }
    ofs.close();
#endif
}

// ── createBlocks ───────────────────────────────────────────────

void Task::createBlocks()
{
    std::lock_guard<std::mutex> lock(mutex_);

    blocks_.clear();
    engines_.clear();
    completed_blocks_.store(0);

    std::vector<BlockInfo> block_infos;

    if (file_size_ > 0) {
        block_infos = splitBlocks(file_size_, max_blocks_, accept_ranges_);
    } else {
        // Unknown file size: single block, full download
        BlockInfo bi;
        bi.block_id = 0;
        bi.range_start = -1;
        bi.range_end = -1;
        bi.downloaded = 0;
        bi.completed = false;
        block_infos.push_back(bi);
    }

    for (auto& bi : block_infos) {
        auto engine = std::make_unique<HttpEngine>();
        auto block = std::make_unique<Block>(
            bi,
            file_path_,
            url_,
            engine.get(),
            limiter_,
            [this](int block_id, int64_t bytes_delta) {
                onBlockProgress(block_id, bytes_delta);
            });

        engines_.push_back(std::move(engine));
        blocks_.push_back(std::move(block));
    }
}

// ── submitBlocks ───────────────────────────────────────────────

void Task::submitBlocks()
{
    HttpConfig config;
    config.referer = referer_;
    config.cookie = cookie_;

    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < blocks_.size(); ++i) {
        Block* block_ptr = blocks_[i].get();
        pool_->submit([block_ptr, config]() {
            try {
                block_ptr->execute(config);
            } catch (const std::exception&) {
                // Error handling: block failed, Task::checkCompletion will detect
            }
        });
    }
}

// ── pause ──────────────────────────────────────────────────────

void Task::pause()
{
    TaskState expected = TaskState::Downloading;
    if (!state_.compare_exchange_strong(expected, TaskState::Paused)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& block : blocks_) {
            block->pause();
        }
    }

    saveMeta();
    setState(TaskState::Paused);
}

// ── resume ─────────────────────────────────────────────────────

void Task::resume()
{
    TaskState expected = TaskState::Paused;
    if (!state_.compare_exchange_strong(expected, TaskState::Downloading)) {
        // Also allow resuming from Failed state
        expected = TaskState::Failed;
        if (!state_.compare_exchange_strong(expected, TaskState::Downloading)) {
            return;
        }
    }
    setState(TaskState::Downloading);

    pool_->submit([this]() {
        try {
            // Check if server file has changed via ETag/Last-Modified
            HttpEngine head_engine;
            HttpConfig config;
            config.referer = referer_;
            config.cookie = cookie_;
            FileInfo info = head_engine.fetchFileInfo(url_, config);

            bool server_changed = false;
            if (!etag_.empty() && !info.etag.empty() && etag_ != info.etag) {
                server_changed = true;
            }
            if (!last_modified_.empty() && !info.last_modified.empty()
                && last_modified_ != info.last_modified) {
                server_changed = true;
            }

            if (server_changed) {
                // Server file changed: discard progress and restart
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    blocks_.clear();
                    engines_.clear();
                }

                file_size_ = info.content_length;
                accept_ranges_ = info.accept_ranges;
                etag_ = info.etag;
                last_modified_ = info.last_modified;

                if (file_size_ > 0) {
                    allocateFile();
                }

                progress_ = std::make_unique<ProgressMonitor>(file_size_);
                createBlocks();
                saveMeta();
                submitBlocks();
                return;
            }

            // Server file unchanged: restore blocks from MetaFile
            auto meta_opt = MetaFile::load(meta_path_);
            if (!meta_opt) {
                // No meta file: restart from scratch
                fetchFileInfoAndStart();
                return;
            }

            const TaskMeta& meta = *meta_opt;

            // Recreate only incomplete blocks
            {
                std::lock_guard<std::mutex> lock(mutex_);
                blocks_.clear();
                engines_.clear();
                completed_blocks_.store(0);

                int64_t already_downloaded = 0;
                for (const auto& bi : meta.blocks) {
                    if (bi.completed) {
                        completed_blocks_.fetch_add(1);
                        already_downloaded += bi.downloaded;
                        continue;
                    }

                    already_downloaded += bi.downloaded;

                    auto engine = std::make_unique<HttpEngine>();
                    auto block = std::make_unique<Block>(
                        bi,
                        file_path_,
                        url_,
                        engine.get(),
                        limiter_,
                        [this](int block_id, int64_t bytes_delta) {
                            onBlockProgress(block_id, bytes_delta);
                        });

                    engines_.push_back(std::move(engine));
                    blocks_.push_back(std::move(block));
                }

                // Reset progress monitor with already-downloaded bytes
                progress_ = std::make_unique<ProgressMonitor>(file_size_);
                if (already_downloaded > 0) {
                    progress_->addBytes(already_downloaded);
                }
            }

            submitBlocks();

        } catch (const HttpError& e) {
            error_message_ = std::string(e.what())
                + " (HTTP " + std::to_string(e.httpStatus()) + ")";
            Logger::instance().error("Task " + std::to_string(task_id_)
                + " resume failed: " + e.what()
                + " (curl=" + std::to_string(e.curlCode())
                + " http=" + std::to_string(e.httpStatus()) + ")");
            setState(TaskState::Failed);
        } catch (const std::exception& e) {
            error_message_ = e.what();
            setState(TaskState::Failed);
        }
    });
}

// ── cancel ─────────────────────────────────────────────────────

void Task::cancel()
{
    state_.store(TaskState::Cancelled);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& block : blocks_) {
            block->pause();
        }
        // Do NOT clear blocks_ or engines_ here!
        // Thread pool workers may still hold raw pointers to Block objects.
        // They will be cleaned up when the Task is destroyed.
    }

    // Clean up temp files
    try {
        if (fs::exists(file_path_)) {
            fs::remove(file_path_);
        }
    } catch (...) {}

    MetaFile::remove(meta_path_);

    setState(TaskState::Cancelled);
}

// ── onBlockProgress ────────────────────────────────────────────

void Task::onBlockProgress(int /*block_id*/, int64_t bytes_delta)
{
    // If cancelled, don't touch any state — the Task may be getting destroyed
    if (state_.load() == TaskState::Cancelled) return;

    if (progress_) {
        progress_->addBytes(bytes_delta);
    }

    // Check if all blocks are done
    bool all_done = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& block : blocks_) {
            if (!block->getInfo().completed) {
                all_done = false;
                break;
            }
        }
    }

    if (all_done && state_.load() == TaskState::Downloading) {
        checkCompletion();
    }
}

// ── checkCompletion ────────────────────────────────────────────

void Task::checkCompletion()
{
    // Verify file size matches expected
    if (file_size_ > 0) {
        try {
            auto actual_size = static_cast<int64_t>(fs::file_size(file_path_));
            if (actual_size != file_size_) {
                setState(TaskState::Failed);
                return;
            }
        } catch (...) {
            setState(TaskState::Failed);
            return;
        }
    }

    setState(TaskState::Completed);

    // Classify the file into the appropriate category directory
    try {
        if (classifier_) {
            std::string category = classifier_->classify(file_name_);
            auto dest_dir = fs::path(save_dir_) / category;
            auto dest = dest_dir / fs::path(file_path_).filename();
            if (classifier_->moveToCategory(file_path_, save_dir_)) {
                file_path_ = dest.string();
            }
        }
    } catch (...) {
        // Classification failure is non-fatal
    }

    // Clean up meta file on successful completion
    MetaFile::remove(meta_path_);
}

// ── saveMeta ───────────────────────────────────────────────────

void Task::saveMeta()
{
    TaskMeta meta;
    meta.url = url_;
    meta.file_path = file_path_;
    meta.file_name = file_name_;
    meta.file_size = file_size_;
    meta.etag = etag_;
    meta.last_modified = last_modified_;
    meta.max_blocks = max_blocks_;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& block : blocks_) {
            meta.blocks.push_back(block->getInfo());
        }
    }

    MetaFile::save(meta_path_, meta);
}

// ── getInfo ────────────────────────────────────────────────────

TaskInfo Task::getInfo() const
{
    TaskInfo info;
    info.task_id = task_id_;
    info.url = url_;
    info.file_path = file_path_;
    info.file_name = file_name_;
    info.file_size = file_size_;
    info.state = state_.load();
    info.error_message = error_message_;

    if (progress_) {
        info.progress = progress_->snapshot();
    }

    return info;
}

int Task::getId() const
{
    return task_id_;
}

// ── setState ───────────────────────────────────────────────────

void Task::setState(TaskState new_state)
{
    TaskState old_state = state_.load();
    state_.store(new_state);
    // Only invoke callback if state actually changed
    if (on_state_change_ && old_state != new_state) {
        on_state_change_(task_id_, new_state);
    }
}

// ── extractFileName ────────────────────────────────────────────

std::string Task::extractFileName(const std::string& url)
{
    // Find the last '/' before any '?' query string
    std::string path = url;
    auto query_pos = path.find('?');
    if (query_pos != std::string::npos) {
        path = path.substr(0, query_pos);
    }

    auto slash_pos = path.rfind('/');
    if (slash_pos != std::string::npos && slash_pos + 1 < path.size()) {
        return urlDecode(path.substr(slash_pos + 1));
    }

    // Fallback: use "download" as default name
    return "download";
}

std::string Task::urlDecode(const std::string& encoded)
{
    std::string result;
    result.reserve(encoded.size());
    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            char hi = encoded[i + 1], lo = encoded[i + 2];
            auto hexVal = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int h = hexVal(hi), l = hexVal(lo);
            if (h >= 0 && l >= 0) {
                result += static_cast<char>((h << 4) | l);
                i += 2;
                continue;
            }
        } else if (encoded[i] == '+') {
            result += ' ';
            continue;
        }
        result += encoded[i];
    }
    return result;
}

std::string Task::parseContentDisposition(const std::string& header)
{
    // Look for filename*=UTF-8''... (RFC 5987) first, then filename="..."
    // Example: attachment; filename*=UTF-8''%E6%96%87%E4%BB%B6.zip
    // Example: attachment; filename="file.zip"

    // Try filename* first (RFC 5987 encoded)
    auto star_pos = header.find("filename*=");
    if (star_pos != std::string::npos) {
        std::string rest = header.substr(star_pos + 10);
        // Skip charset and language: UTF-8''
        auto quote_pos = rest.find("''");
        if (quote_pos != std::string::npos) {
            std::string encoded = rest.substr(quote_pos + 2);
            // Trim at ; or end
            auto semi = encoded.find(';');
            if (semi != std::string::npos) encoded = encoded.substr(0, semi);
            // Trim whitespace
            while (!encoded.empty() && (encoded.back() == ' ' || encoded.back() == '\t'
                   || encoded.back() == '"'))
                encoded.pop_back();
            std::string decoded = urlDecode(encoded);
            if (!decoded.empty()) return decoded;
        }
    }

    // Try filename="..." or filename=...
    auto fn_pos = header.find("filename=");
    if (fn_pos != std::string::npos) {
        std::string rest = header.substr(fn_pos + 9);
        std::string name;
        if (!rest.empty() && rest[0] == '"') {
            // Quoted
            auto end_quote = rest.find('"', 1);
            if (end_quote != std::string::npos) {
                name = rest.substr(1, end_quote - 1);
            }
        } else {
            // Unquoted — until ; or end
            auto semi = rest.find(';');
            name = (semi != std::string::npos) ? rest.substr(0, semi) : rest;
            // Trim whitespace
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
                name.pop_back();
        }
        if (!name.empty()) return name;
    }

    return {};
}

std::string Task::resolveConflict(const std::string& dir, const std::string& name)
{
    fs::path full = fs::path(dir) / name;
    if (!fs::exists(full)) return name;

    // Split name into stem and extension
    fs::path p(name);
    std::string stem = p.stem().string();
    std::string ext = p.extension().string();

    for (int i = 1; i < 1000; ++i) {
        std::string candidate = stem + " (" + std::to_string(i) + ")" + ext;
        if (!fs::exists(fs::path(dir) / candidate)) {
            return candidate;
        }
    }
    return name; // give up after 999
}

// ── buildMetaPath ──────────────────────────────────────────────

std::string Task::buildMetaPath() const
{
    return file_path_ + ".meta";
}
