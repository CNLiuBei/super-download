#include "block_splitter.h"

#include <algorithm>
#include <stdexcept>

std::vector<BlockInfo> splitBlocks(int64_t file_size,
                                   int num_blocks,
                                   bool supports_range) {
    if (file_size <= 0) {
        throw std::invalid_argument("file_size must be > 0");
    }
    if (num_blocks < 1 || num_blocks > 32) {
        throw std::invalid_argument("num_blocks must be in [1, 32]");
    }

    std::vector<BlockInfo> blocks;

    // Server doesn't support Range → single block for the whole file.
    // Also use single block for small files (< 2 MB) — overhead not worth it.
    if (!supports_range || file_size < 2 * 1024 * 1024) {
        BlockInfo b;
        b.block_id    = 0;
        b.range_start = 0;
        b.range_end   = file_size - 1;
        b.downloaded  = 0;
        b.completed   = false;
        blocks.push_back(b);
        return blocks;
    }

    // Actual block count: cannot exceed file_size (each block >= 1 byte).
    int actual_blocks = static_cast<int>(
        std::min(static_cast<int64_t>(num_blocks), file_size));

    int64_t block_size = file_size / actual_blocks;
    int64_t remainder  = file_size % actual_blocks;

    blocks.reserve(static_cast<size_t>(actual_blocks));

    int64_t offset = 0;
    for (int i = 0; i < actual_blocks; ++i) {
        BlockInfo b;
        b.block_id    = i;
        b.range_start = offset;

        // Last block absorbs the remainder.
        int64_t this_size = block_size;
        if (i == actual_blocks - 1) {
            this_size = file_size - offset;
        }

        b.range_end  = offset + this_size - 1;
        b.downloaded = 0;
        b.completed  = false;

        blocks.push_back(b);
        offset += this_size;
    }

    return blocks;
}
