// thread_pool.h
#pragma once
#include <functional>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>
#include <type_traits>

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    // Non-copyable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Submit a callable and return a future for its result.
    template<typename F>
    auto submit(F&& func) -> std::future<decltype(func())>;

    // Number of worker threads.
    size_t size() const;

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stopped_{false};
};

// ── Template implementation (must live in the header) ──────────

template<typename F>
auto ThreadPool::submit(F&& func) -> std::future<decltype(func())> {
    using ReturnType = decltype(func());

    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::forward<F>(func));

    std::future<ReturnType> future = task->get_future();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_.load(std::memory_order_relaxed)) {
            throw std::runtime_error("submit() called on a stopped ThreadPool");
        }
        tasks_.emplace([task]() { (*task)(); });
    }

    cv_.notify_one();
    return future;
}
