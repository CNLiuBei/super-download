#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include <algorithm>
#include "task.h"

class TaskQueue {
public:
    explicit TaskQueue(int max_concurrent);

    /// Append task to end of queue; start immediately if slots available.
    void addTask(std::shared_ptr<Task> task);

    /// Remove task by id, cancel it, return true if found.
    bool removeTask(int task_id);

    /// Move task one position up (toward front). Returns false if not found or already first.
    bool moveUp(int task_id);

    /// Move task one position down (toward back). Returns false if not found or already last.
    bool moveDown(int task_id);

    /// Called when a task finishes (Completed/Cancelled/Failed). Decrements active count and starts next.
    void onTaskFinished(int task_id);

    /// Collect TaskInfo from all tasks.
    std::vector<TaskInfo> getAllTaskInfo() const;

    /// Update max concurrent downloads (clamped to 1-10), may start waiting tasks.
    void setMaxConcurrent(int max);

    /// Get current max concurrent value.
    int getMaxConcurrent() const;

    /// Get current number of tasks in the queue.
    size_t size() const;

    /// Disable auto-start of queued tasks (useful for testing).
    void setAutoStart(bool enabled);

private:
    /// Start next queued task(s) if active_count_ < max_concurrent_.
    void tryStartNext();

    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<Task>> tasks_;
    int max_concurrent_;
    int active_count_ = 0;
    bool auto_start_ = true;  // set to false in tests to prevent network calls
};
