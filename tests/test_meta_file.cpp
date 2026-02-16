#include <gtest/gtest.h>
#include "meta_file.h"
#include <cstdio>
#include <fstream>
#include <string>

namespace {

// Helper: build a sample TaskMeta
TaskMeta makeSampleMeta() {
    TaskMeta meta;
    meta.url           = "https://example.com/file.zip";
    meta.file_path     = "D:/Downloads/file.zip";
    meta.file_name     = "file.zip";
    meta.file_size     = 104857600;
    meta.etag          = "\"abc123\"";
    meta.last_modified = "Wed, 01 Jan 2025 00:00:00 GMT";
    meta.max_blocks    = 8;

    BlockInfo b0;
    b0.block_id    = 0;
    b0.range_start = 0;
    b0.range_end   = 13107199;
    b0.downloaded  = 13107200;
    b0.completed   = true;

    BlockInfo b1;
    b1.block_id    = 1;
    b1.range_start = 13107200;
    b1.range_end   = 26214399;
    b1.downloaded  = 5242880;
    b1.completed   = false;

    meta.blocks = {b0, b1};
    return meta;
}

const char* kTestMetaPath = "test_meta_temp.json";

class MetaFileTest : public ::testing::Test {
protected:
    void TearDown() override {
        std::remove(kTestMetaPath);
    }
};

// ── save / load round-trip ─────────────────────────────────────

TEST_F(MetaFileTest, SaveAndLoadRoundTrip) {
    TaskMeta original = makeSampleMeta();
    ASSERT_TRUE(MetaFile::save(kTestMetaPath, original));

    auto loaded = MetaFile::load(kTestMetaPath);
    ASSERT_TRUE(loaded.has_value());

    const TaskMeta& m = loaded.value();
    EXPECT_EQ(m.url,           original.url);
    EXPECT_EQ(m.file_path,     original.file_path);
    EXPECT_EQ(m.file_name,     original.file_name);
    EXPECT_EQ(m.file_size,     original.file_size);
    EXPECT_EQ(m.etag,          original.etag);
    EXPECT_EQ(m.last_modified, original.last_modified);
    EXPECT_EQ(m.max_blocks,    original.max_blocks);

    ASSERT_EQ(m.blocks.size(), original.blocks.size());
    for (size_t i = 0; i < m.blocks.size(); ++i) {
        EXPECT_EQ(m.blocks[i].block_id,    original.blocks[i].block_id);
        EXPECT_EQ(m.blocks[i].range_start, original.blocks[i].range_start);
        EXPECT_EQ(m.blocks[i].range_end,   original.blocks[i].range_end);
        EXPECT_EQ(m.blocks[i].downloaded,  original.blocks[i].downloaded);
        EXPECT_EQ(m.blocks[i].completed,   original.blocks[i].completed);
    }
}

// ── empty blocks list ──────────────────────────────────────────

TEST_F(MetaFileTest, EmptyBlocksList) {
    TaskMeta meta;
    meta.url       = "https://example.com/empty.txt";
    meta.file_path = "/tmp/empty.txt";
    meta.file_name = "empty.txt";
    meta.file_size = 0;
    // blocks left empty

    ASSERT_TRUE(MetaFile::save(kTestMetaPath, meta));
    auto loaded = MetaFile::load(kTestMetaPath);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->blocks.empty());
}

// ── special characters in URL and etag ─────────────────────────

TEST_F(MetaFileTest, SpecialCharacters) {
    TaskMeta meta;
    meta.url       = "https://example.com/path?q=hello&lang=中文";
    meta.file_path = "C:\\Users\\测试\\file (1).zip";
    meta.file_name = "file (1).zip";
    meta.file_size = 1024;
    meta.etag      = "\"W/abc-123\"";
    meta.last_modified = "";

    ASSERT_TRUE(MetaFile::save(kTestMetaPath, meta));
    auto loaded = MetaFile::load(kTestMetaPath);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->url,       meta.url);
    EXPECT_EQ(loaded->file_path, meta.file_path);
    EXPECT_EQ(loaded->etag,      meta.etag);
}

// ── load from non-existent file ────────────────────────────────

TEST_F(MetaFileTest, LoadNonExistentFile) {
    auto result = MetaFile::load("does_not_exist_12345.json");
    EXPECT_FALSE(result.has_value());
}

// ── load from corrupted / invalid JSON ─────────────────────────

TEST_F(MetaFileTest, LoadCorruptedFile) {
    {
        std::ofstream ofs(kTestMetaPath);
        ofs << "this is not valid json {{{";
    }
    auto result = MetaFile::load(kTestMetaPath);
    EXPECT_FALSE(result.has_value());
}

// ── remove existing file ───────────────────────────────────────

TEST_F(MetaFileTest, RemoveExistingFile) {
    TaskMeta meta = makeSampleMeta();
    ASSERT_TRUE(MetaFile::save(kTestMetaPath, meta));

    EXPECT_TRUE(MetaFile::remove(kTestMetaPath));
    // File should be gone — load should fail
    EXPECT_FALSE(MetaFile::load(kTestMetaPath).has_value());
}

// ── remove non-existent file ───────────────────────────────────

TEST_F(MetaFileTest, RemoveNonExistentFile) {
    EXPECT_FALSE(MetaFile::remove("does_not_exist_12345.json"));
}

} // namespace
