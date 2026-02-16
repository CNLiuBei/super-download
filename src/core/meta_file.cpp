#include "meta_file.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>

using json = nlohmann::json;

// ── JSON serialization helpers ─────────────────────────────────

static json blockInfoToJson(const BlockInfo& b) {
    return json{
        {"block_id",    b.block_id},
        {"range_start", b.range_start},
        {"range_end",   b.range_end},
        {"downloaded",  b.downloaded},
        {"completed",   b.completed}
    };
}

static BlockInfo blockInfoFromJson(const json& j) {
    BlockInfo b;
    b.block_id    = j.at("block_id").get<int>();
    b.range_start = j.at("range_start").get<int64_t>();
    b.range_end   = j.at("range_end").get<int64_t>();
    b.downloaded  = j.at("downloaded").get<int64_t>();
    b.completed   = j.at("completed").get<bool>();
    return b;
}

static json taskMetaToJson(const TaskMeta& meta) {
    json blocks_arr = json::array();
    for (const auto& b : meta.blocks) {
        blocks_arr.push_back(blockInfoToJson(b));
    }
    return json{
        {"url",           meta.url},
        {"file_path",     meta.file_path},
        {"file_name",     meta.file_name},
        {"file_size",     meta.file_size},
        {"etag",          meta.etag},
        {"last_modified", meta.last_modified},
        {"max_blocks",    meta.max_blocks},
        {"blocks",        blocks_arr}
    };
}

static TaskMeta taskMetaFromJson(const json& j) {
    TaskMeta meta;
    meta.url           = j.at("url").get<std::string>();
    meta.file_path     = j.at("file_path").get<std::string>();
    meta.file_name     = j.at("file_name").get<std::string>();
    meta.file_size     = j.at("file_size").get<int64_t>();
    meta.etag          = j.at("etag").get<std::string>();
    meta.last_modified = j.at("last_modified").get<std::string>();
    meta.max_blocks    = j.at("max_blocks").get<int>();
    for (const auto& bj : j.at("blocks")) {
        meta.blocks.push_back(blockInfoFromJson(bj));
    }
    return meta;
}

// ── MetaFile implementation ────────────────────────────────────

bool MetaFile::save(const std::string& meta_path, const TaskMeta& meta) {
    try {
        std::ofstream ofs(meta_path);
        if (!ofs.is_open()) {
            return false;
        }
        ofs << taskMetaToJson(meta).dump(4);
        return ofs.good();
    } catch (...) {
        return false;
    }
}

std::optional<TaskMeta> MetaFile::load(const std::string& meta_path) {
    try {
        std::ifstream ifs(meta_path);
        if (!ifs.is_open()) {
            return std::nullopt;
        }
        json j = json::parse(ifs);
        return taskMetaFromJson(j);
    } catch (...) {
        return std::nullopt;
    }
}

bool MetaFile::remove(const std::string& meta_path) {
    return std::remove(meta_path.c_str()) == 0;
}
