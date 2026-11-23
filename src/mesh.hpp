#pragma once

#include "chunk.hpp"

#include <vector>

namespace minecpp {

struct ChunkSnapshot {
    ChunkPos position;
    // One-cell border around the 16x16 chunk allows meshing against neighbours.
    std::vector<Block> blocks;

    [[nodiscard]] Block at(int x, int y, int z) const {
        if (y < 0 || y >= kWorldHeight || x < -1 || x > kChunkSize || z < -1 || z > kChunkSize)
            return Block::Air;
        const auto index = (static_cast<std::size_t>(y) * (kChunkSize + 2) +
                            static_cast<std::size_t>(z + 1)) * (kChunkSize + 2) +
                           static_cast<std::size_t>(x + 1);
        return blocks[index];
    }
};

[[nodiscard]] std::vector<MeshVertex> build_chunk_mesh(const ChunkSnapshot& snapshot);

} // namespace minecpp
