#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>

#include "block.h"
#include "http_engine.h"
#include "progress_monitor.h"
#include "meta_file.h"

enum class TaskState {
    Queued,       // 等待中
    Downloading,  // 下载中
    Paused,       // 已暂停
    Completed,    // 已完成
    Failed,       // 失败
    Cancelled     // 已取消
};

struct TaskInfo {
    int task_id = 0;
    std::string url;
    std::string file_path;
    std::string file_name;
    int64_t file_size = 0;
    TaskState state = TaskState::Queued;
    ProgressInfo progress;
    std::string error_message;  // populated when state == Failed
};

using TaskStateCallback = std::function<void(int task_id, TaskState state)>;

class ThreadPool;
class TokenBucket;
class FileClassifier;

class Task {
public:
    Task(int task_id,
         const std::string& url,
         const std::string& save_dir,
         int max_blocks,
         ThreadPool* pool,
         TokenBucket* limiter,
         FileClassifier* classifier,
         TaskStateCallback on_state_change,
         const std::string& referer = "",
         const std::string& cookie = "");

    /// Restore a Task from a MetaFile (created in Paused state, ready to resume).
    static std::unique_ptr<Task> fromMeta(
         const std::string& meta_path,
         ThreadPool* pool,
         TokenBucket* limiter,
         FileClassifier* classifier,
         TaskStateCallback on_state_change);

    /// Start downloading (sends HEAD, allocates file, splits blocks, submits).
    void start();

    /// Pause all blocks and save MetaFile.
    void pause();

    /// Resume from MetaFile, checking server file changes via ETag/Last-Modified.
    void resume();

    /// Cancel all blocks, clean up temp files and MetaFile.
    void cancel();

    /// Return a snapshot of the current task info.
    TaskInfo getInfo() const;

    /// Return the task ID.
    int getId() const;

private:
    /// Send HEAD request, get file info, allocate, split, submit.
    void fetchFileInfoAndStart();

    /// Pre-allocate file space on disk (Windows: SetFilePointerEx + SetEndOfFile).
    void allocateFile();

    /// Create Block objects from the split result.
    void createBlocks();

    /// Submit all blocks to the thread pool for execution.
    void submitBlocks();

    /// Called by each Block to report incremental progress.
    void onBlockProgress(int block_id, int64_t bytes_delta);

    /// Check if all blocks are done; verify file size and classify.
    void checkCompletion();

    /// Persist current state to MetaFile.
    void saveMeta();

    /// Extract file name from URL (last path segment).
    static std::string extractFileName(const std::string& url);
    static std::string parseContentDisposition(const std::string& header);
    static std::string urlDecode(const std::string& encoded);
    static std::string resolveConflict(const std::string& dir, const std::string& name);

    /// Build the meta file path from the download file path.
    std::string buildMetaPath() const;

    /// Set state and invoke callback.
    void setState(TaskState new_state);

    int task_id_;
    std::string url_;
    std::string save_dir_;
    std::string file_path_;
    std::string file_name_;
    std::string meta_path_;
    int64_t file_size_ = 0;
    int max_blocks_;
    std::string etag_;
    std::string last_modified_;
    bool accept_ranges_ = false;

    std::atomic<TaskState> state_{TaskState::Queued};
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<Block>> blocks_;
    std::vector<std::unique_ptr<HttpEngine>> engines_;  // one HttpEngine per Block
    std::unique_ptr<ProgressMonitor> progress_;
    std::atomic<int> completed_blocks_{0};

    ThreadPool* pool_;           // non-owning
    TokenBucket* limiter_;       // non-owning
    FileClassifier* classifier_; // non-owning
    TaskStateCallback on_state_change_;
    std::string error_message_;  // last error description
    std::string referer_;        // Referer header from browser
    std::string cookie_;         // Cookie header from browser
    int auto_retry_count_ = 0;
    static constexpr int kMaxAutoRetries = 3;
};
