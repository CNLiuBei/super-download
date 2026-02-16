#pragma once

#include <vector>
#include <cstdint>
#include "meta_file.h"  // for BlockInfo

/// Split a file into download blocks.
///
/// @param file_size       Total file size in bytes (must be > 0).
/// @param num_blocks      Desired number of blocks (1–32).
/// @param supports_range  Whether the server supports HTTP Range requests.
/// @return A vector of BlockInfo with block_id, range_start, range_end set.
///         downloaded = 0 and completed = false for every block.
///
/// Behaviour:
///  - If !supports_range, returns a single block covering [0, file_size-1].
///  - If file_size < num_blocks, actual block count = file_size (1 byte each).
///  - Otherwise divides evenly; the last block absorbs the remainder.
///  - Blocks are contiguous: block[i].range_end + 1 == block[i+1].range_start.
std::vector<BlockInfo> splitBlocks(int64_t file_size,
                                   int num_blocks,
                                   bool supports_range);
