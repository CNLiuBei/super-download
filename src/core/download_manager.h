#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <cstdint>

#include "task.h"
#include "task_queue.h"
#include "thread_pool.h"
#include "token_bucket.h"
#include "file_classifier.h"

struct ManagerConfig {
    std::string default_save_dir;
    int max_blocks_per_task = 8;
    int max_concurrent_tasks = 3;
    int thread_pool_size = 16;
    int64_t speed_limit = 0;       // 0 = no limit
    // File classification rules: category_name -> [extensions]
    std::map<std::string, std::vector<std::string>> classification_rules;
};

class DownloadManager {
public:
    explicit DownloadManager(const ManagerConfig& config);
    ~DownloadManager();

    // Non-copyable
    DownloadManager(const DownloadManager&) = delete;
    DownloadManager& operator=(const DownloadManager&) = delete;

    /// Add a new download. Returns the assigned task_id.
    int addDownload(const std::string& url, const std::string& save_dir = "",
                    const std::string& referer = "", const std::string& cookie = "");

    /// Pause a downloading task.
    void pauseTask(int task_id);

    /// Resume a paused task.
    void resumeTask(int task_id);

    /// Cancel a task (stops download, cleans up files).
    void cancelTask(int task_id);

    /// Remove a task from the queue entirely.
    void removeTask(int task_id);

    /// Move task one position up in the queue.
    void moveTaskUp(int task_id);

    /// Move task one position down in the queue.
    void moveTaskDown(int task_id);

    /// Set global speed limit (bytes/sec). 0 = unlimited.
    void setSpeedLimit(int64_t bytes_per_sec);

    /// Get info snapshots for all tasks.
    std::vector<TaskInfo> getAllTasks() const;

    /// Scan default_save_dir for .meta files and recover unfinished tasks.
    void recoverTasks();

    /// Update configuration (save dir, concurrency, blocks, speed limit, rules).
    void updateConfig(const ManagerConfig& config);

private:
    /// Callback invoked when a task changes state.
    void onTaskStateChange(int task_id, TaskState state);

    /// Find a task by ID across the queue. Returns nullptr if not found.
    std::shared_ptr<Task> findTask(int task_id) const;

    ManagerConfig config_;
    std::unique_ptr<ThreadPool> thread_pool_;
    std::unique_ptr<TokenBucket> token_bucket_;
    std::unique_ptr<TaskQueue> task_queue_;
    std::unique_ptr<FileClassifier> file_classifier_;

    mutable std::mutex mutex_;
    // Map task_id -> shared_ptr<Task> for quick lookup
    std::map<int, std::shared_ptr<Task>> tasks_by_id_;
    int next_task_id_ = 1;
};
