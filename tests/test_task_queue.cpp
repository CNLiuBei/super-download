#include <gtest/gtest.h>
#include "task_queue.h"
#include "thread_pool.h"
#include "token_bucket.h"

#include <memory>
#include <vector>
#include <atomic>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

class TaskQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = fs::temp_directory_path() / "task_queue_test";
        fs::create_directories(test_dir_);
        pool_ = std::make_unique<ThreadPool>(2);
        limiter_ = std::make_unique<TokenBucket>(0);
    }

    void TearDown() override {
        if (limiter_) limiter_->cancel();
        pool_.reset();
        limiter_.reset();
        try { fs::remove_all(test_dir_); } catch (...) {}
    }

    std::shared_ptr<Task> makeTask(int id) {
        return std::make_shared<Task>(
            id, "http://0.0.0.0:1/file" + std::to_string(id) + ".bin",
            test_dir_.string(), 1, pool_.get(), limiter_.get(),
            nullptr, [](int, TaskState) {});
    }

    std::unique_ptr<TaskQueue> makeQueue(int max_concurrent) {
        auto q = std::make_unique<TaskQueue>(max_concurrent);
        q->setAutoStart(false);
        return q;
    }

    fs::path test_dir_;
    std::unique_ptr<ThreadPool> pool_;
    std::unique_ptr<TokenBucket> limiter_;
};

TEST_F(TaskQueueTest, ConstructorClampsMaxConcurrent) {
    TaskQueue q1(0);
    EXPECT_EQ(q1.getMaxConcurrent(), 1);
    TaskQueue q2(5);
    EXPECT_EQ(q2.getMaxConcurrent(), 5);
    TaskQueue q3(100);
    EXPECT_EQ(q3.getMaxConcurrent(), 10);
}

TEST_F(TaskQueueTest, AddTaskAppendsToEnd) {
    auto q = makeQueue(10);
    q->addTask(makeTask(1));
    EXPECT_EQ(q->size(), 1u);
    q->addTask(makeTask(2));
    EXPECT_EQ(q->size(), 2u);
    q->addTask(makeTask(3));
    EXPECT_EQ(q->size(), 3u);
    auto infos = q->getAllTaskInfo();
    ASSERT_EQ(infos.size(), 3u);
    EXPECT_EQ(infos[0].task_id, 1);
    EXPECT_EQ(infos[1].task_id, 2);
    EXPECT_EQ(infos[2].task_id, 3);
}

TEST_F(TaskQueueTest, AddNullTaskIsIgnored) {
    auto q = makeQueue(3);
    q->addTask(nullptr);
    EXPECT_EQ(q->size(), 0u);
}

TEST_F(TaskQueueTest, RemoveExistingTask) {
    auto q = makeQueue(10);
    q->addTask(makeTask(1));
    q->addTask(makeTask(2));
    q->addTask(makeTask(3));
    EXPECT_TRUE(q->removeTask(2));
    EXPECT_EQ(q->size(), 2u);
    auto infos = q->getAllTaskInfo();
    ASSERT_EQ(infos.size(), 2u);
    EXPECT_EQ(infos[0].task_id, 1);
    EXPECT_EQ(infos[1].task_id, 3);
}

TEST_F(TaskQueueTest, RemoveNonExistentTaskReturnsFalse) {
    auto q = makeQueue(10);
    q->addTask(makeTask(1));
    EXPECT_FALSE(q->removeTask(999));
    EXPECT_EQ(q->size(), 1u);
}

TEST_F(TaskQueueTest, RemoveFromEmptyQueueReturnsFalse) {
    auto q = makeQueue(3);
    EXPECT_FALSE(q->removeTask(1));
}

TEST_F(TaskQueueTest, MoveUpSwapsWithPrevious) {
    auto q = makeQueue(10);
    q->addTask(makeTask(1));
    q->addTask(makeTask(2));
    q->addTask(makeTask(3));
    EXPECT_TRUE(q->moveUp(2));
    auto infos = q->getAllTaskInfo();
    ASSERT_EQ(infos.size(), 3u);
    EXPECT_EQ(infos[0].task_id, 2);
    EXPECT_EQ(infos[1].task_id, 1);
    EXPECT_EQ(infos[2].task_id, 3);
}

TEST_F(TaskQueueTest, MoveUpFirstElementReturnsFalse) {
    auto q = makeQueue(10);
    q->addTask(makeTask(1));
    q->addTask(makeTask(2));
    EXPECT_FALSE(q->moveUp(1));
}

TEST_F(TaskQueueTest, MoveUpNonExistentReturnsFalse) {
    auto q = makeQueue(10);
    q->addTask(makeTask(1));
    EXPECT_FALSE(q->moveUp(999));
}

TEST_F(TaskQueueTest, MoveDownSwapsWithNext) {
    auto q = makeQueue(10);
    q->addTask(makeTask(1));
    q->addTask(makeTask(2));
    q->addTask(makeTask(3));
    EXPECT_TRUE(q->moveDown(2));
    auto infos = q->getAllTaskInfo();
    ASSERT_EQ(infos.size(), 3u);
    EXPECT_EQ(infos[0].task_id, 1);
    EXPECT_EQ(infos[1].task_id, 3);
    EXPECT_EQ(infos[2].task_id, 2);
}

TEST_F(TaskQueueTest, MoveDownLastElementReturnsFalse) {
    auto q = makeQueue(10);
    q->addTask(makeTask(1));
    q->addTask(makeTask(2));
    EXPECT_FALSE(q->moveDown(2));
}

TEST_F(TaskQueueTest, MoveDownNonExistentReturnsFalse) {
    auto q = makeQueue(10);
    q->addTask(makeTask(1));
    EXPECT_FALSE(q->moveDown(999));
}

TEST_F(TaskQueueTest, MoveUpThenDownRestoresOrder) {
    auto q = makeQueue(10);
    q->addTask(makeTask(1));
    q->addTask(makeTask(2));
    q->addTask(makeTask(3));
    EXPECT_TRUE(q->moveUp(3));
    EXPECT_TRUE(q->moveDown(3));
    auto infos = q->getAllTaskInfo();
    EXPECT_EQ(infos[0].task_id, 1);
    EXPECT_EQ(infos[1].task_id, 2);
    EXPECT_EQ(infos[2].task_id, 3);
}

TEST_F(TaskQueueTest, SetMaxConcurrentClampsRange) {
    auto q = makeQueue(3);
    q->setMaxConcurrent(0);
    EXPECT_EQ(q->getMaxConcurrent(), 1);
    q->setMaxConcurrent(5);
    EXPECT_EQ(q->getMaxConcurrent(), 5);
    q->setMaxConcurrent(100);
    EXPECT_EQ(q->getMaxConcurrent(), 10);
    q->setMaxConcurrent(-5);
    EXPECT_EQ(q->getMaxConcurrent(), 1);
}

TEST_F(TaskQueueTest, GetAllTaskInfoReturnsEmpty) {
    auto q = makeQueue(3);
    EXPECT_TRUE(q->getAllTaskInfo().empty());
}

TEST_F(TaskQueueTest, GetAllTaskInfoReturnsCorrectInfo) {
    auto q = makeQueue(10);
    q->addTask(makeTask(42));
    auto infos = q->getAllTaskInfo();
    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos[0].task_id, 42);
}

TEST_F(TaskQueueTest, OnTaskFinishedDecrementsActiveCount) {
    auto q = makeQueue(3);
    q->onTaskFinished(1);
    EXPECT_EQ(q->size(), 0u);
}

TEST_F(TaskQueueTest, SingleTaskMoveUpReturnsFalse) {
    auto q = makeQueue(10);
    q->addTask(makeTask(1));
    EXPECT_FALSE(q->moveUp(1));
}

TEST_F(TaskQueueTest, SingleTaskMoveDownReturnsFalse) {
    auto q = makeQueue(10);
    q->addTask(makeTask(1));
    EXPECT_FALSE(q->moveDown(1));
}

TEST_F(TaskQueueTest, RemoveAllTasks) {
    auto q = makeQueue(10);
    q->addTask(makeTask(1));
    q->addTask(makeTask(2));
    q->addTask(makeTask(3));
    EXPECT_TRUE(q->removeTask(1));
    EXPECT_TRUE(q->removeTask(2));
    EXPECT_TRUE(q->removeTask(3));
    EXPECT_EQ(q->size(), 0u);
}

TEST_F(TaskQueueTest, MoveLastToFirst) {
    auto q = makeQueue(10);
    q->addTask(makeTask(1));
    q->addTask(makeTask(2));
    q->addTask(makeTask(3));
    EXPECT_TRUE(q->moveUp(3));
    EXPECT_TRUE(q->moveUp(3));
    auto infos = q->getAllTaskInfo();
    EXPECT_EQ(infos[0].task_id, 3);
    EXPECT_EQ(infos[1].task_id, 1);
    EXPECT_EQ(infos[2].task_id, 2);
}

} // namespace
