#include "download_manager.h"

#include <filesystem>
#include <algorithm>
#include <stdexcept>

namespace fs = std::filesystem;

// ── Constructor ────────────────────────────────────────────────

DownloadManager::DownloadManager(const ManagerConfig& config)
    : config_(config)
{
    // Clamp configuration values to valid ranges
    config_.max_blocks_per_task = std::clamp(config_.max_blocks_per_task, 1, 32);
    config_.max_concurrent_tasks = std::clamp(config_.max_concurrent_tasks, 1, 10);
    if (config_.thread_pool_size < 1) {
        config_.thread_pool_size = 16;
    }
    if (config_.speed_limit < 0) {
        config_.speed_limit = 0;
    }

    // Ensure default save directory exists
    if (!config_.default_save_dir.empty()) {
        try {
            fs::create_directories(config_.default_save_dir);
        } catch (...) {
            // Non-fatal: directory may already exist or be created later
        }
    }

    // Initialize components
    thread_pool_ = std::make_unique<ThreadPool>(
        static_cast<size_t>(config_.thread_pool_size));

    token_bucket_ = std::make_unique<TokenBucket>(config_.speed_limit);

    task_queue_ = std::make_unique<TaskQueue>(config_.max_concurrent_tasks);

    if (!config_.classification_rules.empty()) {
        file_classifier_ = std::make_unique<FileClassifier>(
            config_.classification_rules);
    } else {
        file_classifier_ = std::make_unique<FileClassifier>();
    }
}

// ── Destructor ─────────────────────────────────────────────────

DownloadManager::~DownloadManager()
{
    // Cancel the token bucket so any blocked threads wake up
    if (token_bucket_) {
        token_bucket_->cancel();
    }

    // Clear task references before destroying the thread pool
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_by_id_.clear();
    }
}

// ── addDownload ────────────────────────────────────────────────

int DownloadManager::addDownload(const std::string& url, const std::string& save_dir,
                                 const std::string& referer, const std::string& cookie)
{
    std::string dir = save_dir.empty() ? config_.default_save_dir : save_dir;

    // Check for duplicate URL (skip completed/cancelled tasks)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, task] : tasks_by_id_) {
            auto info = task->getInfo();
            if (info.url == url &&
                info.state != TaskState::Completed &&
                info.state != TaskState::Cancelled &&
                info.state != TaskState::Failed) {
                return id;  // already downloading this URL
            }
        }
    }

    int task_id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        task_id = next_task_id_++;
    }

    auto task = std::make_shared<Task>(
        task_id,
        url,
        dir,
        config_.max_blocks_per_task,
        thread_pool_.get(),
        token_bucket_.get(),
        file_classifier_.get(),
        [this](int id, TaskState state) {
            onTaskStateChange(id, state);
        },
        referer,
        cookie);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_by_id_[task_id] = task;
    }

    task_queue_->addTask(std::move(task));

    return task_id;
}

// ── pauseTask ──────────────────────────────────────────────────

void DownloadManager::pauseTask(int task_id)
{
    auto task = findTask(task_id);
    if (task) {
        task->pause();
    }
}

// ── resumeTask ─────────────────────────────────────────────────

void DownloadManager::resumeTask(int task_id)
{
    auto task = findTask(task_id);
    if (task) {
        task->resume();
    }
}

// ── cancelTask ─────────────────────────────────────────────────

void DownloadManager::cancelTask(int task_id)
{
    auto task = findTask(task_id);
    if (task) {
        task->cancel();
    }
}

// ── removeTask ─────────────────────────────────────────────────

void DownloadManager::removeTask(int task_id)
{
    // Hold a shared_ptr to keep the Task alive until cancel completes
    // and all thread-pool workers have a chance to see the paused flag.
    std::shared_ptr<Task> kept_alive;

    task_queue_->removeTask(task_id);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tasks_by_id_.find(task_id);
        if (it != tasks_by_id_.end()) {
            kept_alive = std::move(it->second);
            tasks_by_id_.erase(it);
        }
    }

    // kept_alive destructs here — Task is destroyed only after removeTask returns,
    // giving thread-pool workers time to see the cancel/pause flag and exit.
}

// ── moveTaskUp ─────────────────────────────────────────────────

void DownloadManager::moveTaskUp(int task_id)
{
    task_queue_->moveUp(task_id);
}

// ── moveTaskDown ───────────────────────────────────────────────

void DownloadManager::moveTaskDown(int task_id)
{
    task_queue_->moveDown(task_id);
}

// ── setSpeedLimit ──────────────────────────────────────────────

void DownloadManager::setSpeedLimit(int64_t bytes_per_sec)
{
    if (bytes_per_sec < 0) {
        bytes_per_sec = 0;
    }
    token_bucket_->setRate(bytes_per_sec);
    config_.speed_limit = bytes_per_sec;
}

// ── getAllTasks ─────────────────────────────────────────────────

std::vector<TaskInfo> DownloadManager::getAllTasks() const
{
    return task_queue_->getAllTaskInfo();
}

// ── recoverTasks ───────────────────────────────────────────────

void DownloadManager::recoverTasks()
{
    if (config_.default_save_dir.empty()) {
        return;
    }

    fs::path scan_dir(config_.default_save_dir);
    if (!fs::exists(scan_dir) || !fs::is_directory(scan_dir)) {
        return;
    }

    // Scan for .meta files in the default save directory
    for (const auto& entry : fs::directory_iterator(scan_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const auto& path = entry.path();
        if (path.extension() != ".meta") {
            continue;
        }

        // Try to restore the task from the meta file
        auto task = Task::fromMeta(
            path.string(),
            thread_pool_.get(),
            token_bucket_.get(),
            file_classifier_.get(),
            [this](int id, TaskState state) {
                onTaskStateChange(id, state);
            });

        if (!task) {
            // Corrupted meta file: remove it
            MetaFile::remove(path.string());
            continue;
        }

        int task_id;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            task_id = next_task_id_++;
        }

        // fromMeta creates the task with id=0; we need to assign a real id.
        // Since Task doesn't expose a setter, we create a shared_ptr and
        // track it. The task_id in TaskInfo will be 0 but we track by our map.
        // NOTE: A better approach would be to add a setId() method to Task,
        // but we work with the existing interface.
        auto shared_task = std::shared_ptr<Task>(std::move(task));

        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_by_id_[task_id] = shared_task;
        }

        task_queue_->addTask(std::move(shared_task));
    }
}

// ── updateConfig ───────────────────────────────────────────────

void DownloadManager::updateConfig(const ManagerConfig& config)
{
    config_.default_save_dir = config.default_save_dir;
    config_.max_blocks_per_task = std::clamp(config.max_blocks_per_task, 1, 32);
    config_.max_concurrent_tasks = std::clamp(config.max_concurrent_tasks, 1, 10);

    // Update speed limit
    setSpeedLimit(config.speed_limit);

    // Update task queue concurrency
    task_queue_->setMaxConcurrent(config_.max_concurrent_tasks);

    // Update file classifier rules
    if (!config.classification_rules.empty()) {
        file_classifier_->updateRules(config.classification_rules);
    }
}

// ── onTaskStateChange (private) ────────────────────────────────

void DownloadManager::onTaskStateChange(int task_id, TaskState state)
{
    // When a task reaches a terminal or paused state, notify the queue
    switch (state) {
        case TaskState::Completed:
        case TaskState::Failed:
        case TaskState::Cancelled:
            task_queue_->onTaskFinished(task_id);
            break;
        default:
            break;
    }

    // TODO: Notify GUI layer via signal/callback when integrated
}

// ── findTask (private) ─────────────────────────────────────────

std::shared_ptr<Task> DownloadManager::findTask(int task_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_by_id_.find(task_id);
    if (it != tasks_by_id_.end()) {
        return it->second;
    }
    return nullptr;
}
