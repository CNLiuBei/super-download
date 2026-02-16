# src/core/CMakeLists.txt
# Static library: download_core

add_library(download_core STATIC
    http_engine.cpp
    token_bucket.cpp
    thread_pool.cpp
    progress_monitor.cpp
    meta_file.cpp
    file_classifier.cpp
    block.cpp
    block_splitter.cpp
    task.cpp
    task_queue.cpp
    download_manager.cpp
    logger.cpp
)

target_include_directories(download_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(download_core PUBLIC
    CURL::libcurl
    nlohmann_json::nlohmann_json
)
