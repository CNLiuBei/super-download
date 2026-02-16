#include "task_queue.h"

#include <algorithm>
#include <stdexcept>

// ── Constructor ────────────────────────────────────────────────

TaskQueue::TaskQueue(int max_concurrent)
    : max_concurrent_(std::clamp(max_concurrent, 1, 10))
{
}

// ── addTask ────────────────────────────────────────────────────

void TaskQueue::addTask(std::shared_ptr<Task> task)
{
    if (!task) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.push_back(std::move(task));
    tryStartNext();
}

// ── removeTask ─────────────────────────────────────────────────

bool TaskQueue::removeTask(int task_id)
{
    std::shared_ptr<Task> task;
    bool was_active = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = std::find_if(tasks_.begin(), tasks_.end(),
            [task_id](const std::shared_ptr<Task>& t) {
                return t->getId() == task_id;
            });

        if (it == tasks_.end()) {
            return false;
        }

        task = *it;
        TaskState state = task->getInfo().state;
        was_active = (state == TaskState::Downloading);

        if (was_active) {
            --active_count_;
        }

        tasks_.erase(it);

        // Start next queued task if a slot opened up
        tryStartNext();
    }

    // Cancel OUTSIDE the lock to avoid deadlock with onTaskFinished callback
    if (task) {
        task->cancel();
    }

    return true;
}

// ── moveUp ─────────────────────────────────────────────────────

bool TaskQueue::moveUp(int task_id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find_if(tasks_.begin(), tasks_.end(),
        [task_id](const std::shared_ptr<Task>& t) {
            return t->getId() == task_id;
        });

    if (it == tasks_.end() || it == tasks_.begin()) {
        return false;
    }

    std::iter_swap(it, it - 1);
    return true;
}

// ── moveDown ───────────────────────────────────────────────────

bool TaskQueue::moveDown(int task_id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find_if(tasks_.begin(), tasks_.end(),
        [task_id](const std::shared_ptr<Task>& t) {
            return t->getId() == task_id;
        });

    if (it == tasks_.end() || it + 1 == tasks_.end()) {
        return false;
    }

    std::iter_swap(it, it + 1);
    return true;
}

// ── onTaskFinished ─────────────────────────────────────────────

void TaskQueue::onTaskFinished(int task_id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Only decrement if the task is still in our queue.
    // If removeTask already erased it, we must not double-decrement.
    auto it = std::find_if(tasks_.begin(), tasks_.end(),
        [task_id](const std::shared_ptr<Task>& t) {
            return t->getId() == task_id;
        });

    if (it == tasks_.end()) {
        return;  // already removed by removeTask
    }

    if (active_count_ > 0) {
        --active_count_;
    }

    tryStartNext();
}

// ── getAllTaskInfo ──────────────────────────────────────────────

std::vector<TaskInfo> TaskQueue::getAllTaskInfo() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<TaskInfo> infos;
    infos.reserve(tasks_.size());
    for (const auto& task : tasks_) {
        infos.push_back(task->getInfo());
    }
    return infos;
}

// ── setMaxConcurrent ───────────────────────────────────────────

void TaskQueue::setMaxConcurrent(int max)
{
    std::lock_guard<std::mutex> lock(mutex_);
    max_concurrent_ = std::clamp(max, 1, 10);
    tryStartNext();
}

// ── getMaxConcurrent ───────────────────────────────────────────

int TaskQueue::getMaxConcurrent() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return max_concurrent_;
}

// ── size ───────────────────────────────────────────────────────

size_t TaskQueue::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

// ── setAutoStart ───────────────────────────────────────────────

void TaskQueue::setAutoStart(bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto_start_ = enabled;
}

// ── tryStartNext (private, must be called with mutex held) ─────

void TaskQueue::tryStartNext()
{
    if (!auto_start_) return;

    for (auto& task : tasks_) {
        if (active_count_ >= max_concurrent_) {
            break;
        }

        TaskInfo info = task->getInfo();
        if (info.state == TaskState::Queued) {
            task->start();
            ++active_count_;
        }
    }
}
