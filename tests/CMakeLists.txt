# tests/CMakeLists.txt
# Test executable: download_tests

add_executable(download_tests
    placeholder_test.cpp
    test_http_retry.cpp
    test_token_bucket.cpp
    test_thread_pool.cpp
    test_progress_monitor.cpp
    test_meta_file.cpp
    test_file_classifier.cpp
    test_block_splitter.cpp
    test_task_queue.cpp
    test_logger.cpp
)

target_link_libraries(download_tests PRIVATE
    download_core
    GTest::gtest_main
    rapidcheck
)

include(GoogleTest)
gtest_discover_tests(download_tests)
