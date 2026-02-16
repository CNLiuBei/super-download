#include <gtest/gtest.h>
#include "logger.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <regex>

namespace fs = std::filesystem;

// Helper: read entire file into a string.
static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_path_ = (fs::temp_directory_path() / "test_logger.log").string();
        // Remove leftover file from previous run.
        fs::remove(log_path_);
    }

    void TearDown() override {
        // Reset the log file so the singleton doesn't hold the handle.
        Logger::instance().setLogFile("");
        fs::remove(log_path_);
    }

    std::string log_path_;
};

TEST_F(LoggerTest, SingletonReturnsSameInstance) {
    Logger& a = Logger::instance();
    Logger& b = Logger::instance();
    EXPECT_EQ(&a, &b);
}

TEST_F(LoggerTest, LogWritesToFileWithTimestampAndLevel) {
    Logger::instance().setLogFile(log_path_);
    Logger::instance().info("hello world");

    std::string content = readFile(log_path_);
    // Expect format: [YYYY-MM-DD HH:MM:SS] [INFO] hello world
    std::regex pattern(R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\] \[INFO\] hello world)");
    EXPECT_TRUE(std::regex_search(content, pattern));
}

TEST_F(LoggerTest, AllLogLevelsWriteCorrectTag) {
    Logger::instance().setLogFile(log_path_);
    Logger::instance().info("i");
    Logger::instance().warn("w");
    Logger::instance().error("e");

    std::string content = readFile(log_path_);
    EXPECT_NE(content.find("[INFO] i"), std::string::npos);
    EXPECT_NE(content.find("[WARN] w"), std::string::npos);
    EXPECT_NE(content.find("[ERROR] e"), std::string::npos);
}

TEST_F(LoggerTest, GetRecentLogsReturnsLatestEntries) {
    Logger::instance().setLogFile(log_path_);
    for (int i = 0; i < 5; ++i) {
        Logger::instance().info("msg" + std::to_string(i));
    }

    auto logs = Logger::instance().getRecentLogs(3);
    ASSERT_EQ(logs.size(), 3u);
    // Should be the last 3 messages.
    EXPECT_NE(logs[0].find("msg2"), std::string::npos);
    EXPECT_NE(logs[1].find("msg3"), std::string::npos);
    EXPECT_NE(logs[2].find("msg4"), std::string::npos);
}

TEST_F(LoggerTest, GetRecentLogsCountExceedsAvailable) {
    Logger::instance().setLogFile(log_path_);
    Logger::instance().info("only one");

    auto logs = Logger::instance().getRecentLogs(100);
    // The singleton may have logs from previous tests in the same process,
    // but at minimum the one we just added should be present.
    EXPECT_GE(logs.size(), 1u);
    EXPECT_NE(logs.back().find("only one"), std::string::npos);
}

TEST_F(LoggerTest, ThreadSafety) {
    Logger::instance().setLogFile(log_path_);

    constexpr int kThreads = 8;
    constexpr int kMessagesPerThread = 50;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t]() {
            for (int i = 0; i < kMessagesPerThread; ++i) {
                Logger::instance().info("t" + std::to_string(t) + "_m" + std::to_string(i));
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    // Verify the file has the expected number of lines.
    std::string content = readFile(log_path_);
    int line_count = 0;
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty()) ++line_count;
    }
    EXPECT_EQ(line_count, kThreads * kMessagesPerThread);
}
