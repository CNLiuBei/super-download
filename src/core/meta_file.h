#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

// BlockInfo and TaskMeta are defined here temporarily.
// When block.h is created (Task 9), BlockInfo will move there.

struct BlockInfo {
    int block_id = 0;
    int64_t range_start = 0;
    int64_t range_end = 0;
    int64_t downloaded = 0;
    bool completed = false;
};

struct TaskMeta {
    std::string url;
    std::string file_path;
    std::string file_name;
    int64_t file_size = 0;
    std::string etag;
    std::string last_modified;
    int max_blocks = 8;
    std::vector<BlockInfo> blocks;
};

class MetaFile {
public:
    /// Serialize TaskMeta to JSON and write to file.
    static bool save(const std::string& meta_path, const TaskMeta& meta);

    /// Deserialize TaskMeta from a JSON file.
    static std::optional<TaskMeta> load(const std::string& meta_path);

    /// Delete the meta file from disk.
    static bool remove(const std::string& meta_path);
};
