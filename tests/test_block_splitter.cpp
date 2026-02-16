#include <gtest/gtest.h>
#include "block_splitter.h"

#include <numeric>
#include <set>

namespace {

// ── Helper: verify contiguity, no gaps, no overlaps ────────────

void verifyContiguous(const std::vector<BlockInfo>& blocks, int64_t file_size) {
    ASSERT_FALSE(blocks.empty());
    EXPECT_EQ(blocks.front().range_start, 0);
    EXPECT_EQ(blocks.back().range_end, file_size - 1);

    for (size_t i = 1; i < blocks.size(); ++i) {
        EXPECT_EQ(blocks[i].range_start, blocks[i - 1].range_end + 1)
            << "Gap or overlap between block " << (i - 1) << " and " << i;
    }
}

// ── Basic even split ───────────────────────────────────────────

TEST(BlockSplitterTest, EvenSplit) {
    auto blocks = splitBlocks(100, 4, true);
    ASSERT_EQ(blocks.size(), 4u);
    verifyContiguous(blocks, 100);

    // Each block should be 25 bytes
    for (const auto& b : blocks) {
        EXPECT_EQ(b.range_end - b.range_start + 1, 25);
        EXPECT_EQ(b.downloaded, 0);
        EXPECT_FALSE(b.completed);
    }
}

// ── Remainder goes to last block ───────────────────────────────

TEST(BlockSplitterTest, RemainderGoesToLastBlock) {
    auto blocks = splitBlocks(103, 4, true);
    ASSERT_EQ(blocks.size(), 4u);
    verifyContiguous(blocks, 103);

    // First 3 blocks: 25 bytes each, last block: 28 bytes
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(blocks[i].range_end - blocks[i].range_start + 1, 25);
    }
    EXPECT_EQ(blocks[3].range_end - blocks[3].range_start + 1, 28);
}

// ── Single block when Range not supported ──────────────────────

TEST(BlockSplitterTest, NoRangeSupportSingleBlock) {
    auto blocks = splitBlocks(1000, 8, false);
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].block_id, 0);
    EXPECT_EQ(blocks[0].range_start, 0);
    EXPECT_EQ(blocks[0].range_end, 999);
    EXPECT_EQ(blocks[0].downloaded, 0);
    EXPECT_FALSE(blocks[0].completed);
}

// ── file_size < num_blocks → one byte per block ────────────────

TEST(BlockSplitterTest, FileSmallerThanBlockCount) {
    auto blocks = splitBlocks(3, 32, true);
    ASSERT_EQ(blocks.size(), 3u);
    verifyContiguous(blocks, 3);

    for (const auto& b : blocks) {
        EXPECT_EQ(b.range_end - b.range_start + 1, 1);
    }
}

// ── Single byte file ───────────────────────────────────────────

TEST(BlockSplitterTest, SingleByteFile) {
    auto blocks = splitBlocks(1, 8, true);
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].range_start, 0);
    EXPECT_EQ(blocks[0].range_end, 0);
}

// ── num_blocks = 1 ─────────────────────────────────────────────

TEST(BlockSplitterTest, SingleBlockRequested) {
    auto blocks = splitBlocks(500, 1, true);
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].range_start, 0);
    EXPECT_EQ(blocks[0].range_end, 499);
}

// ── block_id is sequential ─────────────────────────────────────

TEST(BlockSplitterTest, BlockIdsAreSequential) {
    auto blocks = splitBlocks(1000, 8, true);
    for (size_t i = 0; i < blocks.size(); ++i) {
        EXPECT_EQ(blocks[i].block_id, static_cast<int>(i));
    }
}

// ── Total coverage equals file size ────────────────────────────

TEST(BlockSplitterTest, TotalCoverageEqualsFileSize) {
    auto blocks = splitBlocks(999, 7, true);
    int64_t total = 0;
    for (const auto& b : blocks) {
        total += (b.range_end - b.range_start + 1);
    }
    EXPECT_EQ(total, 999);
}

// ── Max blocks (32) ────────────────────────────────────────────

TEST(BlockSplitterTest, MaxBlocks) {
    auto blocks = splitBlocks(10000, 32, true);
    ASSERT_EQ(blocks.size(), 32u);
    verifyContiguous(blocks, 10000);
}

// ── Invalid arguments ──────────────────────────────────────────

TEST(BlockSplitterTest, ThrowsOnZeroFileSize) {
    EXPECT_THROW(splitBlocks(0, 4, true), std::invalid_argument);
}

TEST(BlockSplitterTest, ThrowsOnNegativeFileSize) {
    EXPECT_THROW(splitBlocks(-1, 4, true), std::invalid_argument);
}

TEST(BlockSplitterTest, ThrowsOnZeroBlocks) {
    EXPECT_THROW(splitBlocks(100, 0, true), std::invalid_argument);
}

TEST(BlockSplitterTest, ThrowsOnTooManyBlocks) {
    EXPECT_THROW(splitBlocks(100, 33, true), std::invalid_argument);
}

// ── No Range + various num_blocks still yields single block ────

TEST(BlockSplitterTest, NoRangeIgnoresBlockCount) {
    for (int n : {1, 8, 32}) {
        auto blocks = splitBlocks(500, n, false);
        ASSERT_EQ(blocks.size(), 1u) << "num_blocks=" << n;
        EXPECT_EQ(blocks[0].range_start, 0);
        EXPECT_EQ(blocks[0].range_end, 499);
    }
}

// ── All blocks initialized correctly ───────────────────────────

TEST(BlockSplitterTest, AllBlocksInitializedCorrectly) {
    auto blocks = splitBlocks(200, 5, true);
    for (const auto& b : blocks) {
        EXPECT_EQ(b.downloaded, 0);
        EXPECT_FALSE(b.completed);
        EXPECT_GE(b.range_start, 0);
        EXPECT_LE(b.range_end, 199);
        EXPECT_LE(b.range_start, b.range_end);
    }
}

} // namespace
