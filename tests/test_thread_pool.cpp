// test_thread_pool.cpp
#include <gtest/gtest.h>
#include "thread_pool.h"

#include <atomic>
#include <chrono>
#include <numeric>
#include <vector>

// ── Basic construction / size ──────────────────────────────────

TEST(ThreadPoolTest, SizeMatchesRequestedThreadCount) {
    ThreadPool pool(4);
    EXPECT_EQ(pool.size(), 4u);
}

TEST(ThreadPoolTest, SingleThread) {
    ThreadPool pool(1);
    EXPECT_EQ(pool.size(), 1u);

    auto f = pool.submit([] { return 42; });
    EXPECT_EQ(f.get(), 42);
}

// ── Submit and get results ─────────────────────────────────────

TEST(ThreadPoolTest, SubmitReturnsCorrectResult) {
    ThreadPool pool(2);
    auto f = pool.submit([] { return 7 + 3; });
    EXPECT_EQ(f.get(), 10);
}

TEST(ThreadPoolTest, SubmitVoidTask) {
    ThreadPool pool(2);
    std::atomic<bool> executed{false};
    auto f = pool.submit([&] { executed.store(true); });
    f.get();
    EXPECT_TRUE(executed.load());
}

TEST(ThreadPoolTest, MultipleTasks) {
    ThreadPool pool(4);
    constexpr int N = 100;
    std::vector<std::future<int>> futures;
    futures.reserve(N);

    for (int i = 0; i < N; ++i) {
        futures.push_back(pool.submit([i] { return i * i; }));
    }

    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(futures[i].get(), i * i);
    }
}

// ── Concurrency ────────────────────────────────────────────────

TEST(ThreadPoolTest, TasksRunConcurrently) {
    constexpr size_t NUM_THREADS = 4;
    ThreadPool pool(NUM_THREADS);

    std::atomic<int> concurrent_count{0};
    std::atomic<int> max_concurrent{0};

    std::vector<std::future<void>> futures;
    for (size_t i = 0; i < NUM_THREADS; ++i) {
        futures.push_back(pool.submit([&] {
            int cur = ++concurrent_count;
            // Update max observed concurrency
            int prev_max = max_concurrent.load();
            while (cur > prev_max &&
                   !max_concurrent.compare_exchange_weak(prev_max, cur)) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            --concurrent_count;
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    EXPECT_GT(max_concurrent.load(), 1);
}

// ── Exception propagation ──────────────────────────────────────

TEST(ThreadPoolTest, ExceptionPropagatedThroughFuture) {
    ThreadPool pool(2);
    auto f = pool.submit([]() -> int {
        throw std::runtime_error("task error");
    });
    EXPECT_THROW(f.get(), std::runtime_error);
}

// ── Destructor drains remaining tasks ──────────────────────────

TEST(ThreadPoolTest, DestructorCompletesQueuedTasks) {
    std::atomic<int> counter{0};
    {
        ThreadPool pool(1);
        // Submit a blocking task so subsequent tasks queue up
        pool.submit([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            ++counter;
        });
        for (int i = 0; i < 5; ++i) {
            pool.submit([&] { ++counter; });
        }
    } // destructor joins here
    EXPECT_EQ(counter.load(), 6);
}
