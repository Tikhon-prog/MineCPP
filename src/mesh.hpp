#pragma once

#include "chunk.hpp"
#include "tiles.hpp"

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

// Selects the atlas UVs used by build_chunk_mesh. Call before any chunk is
// meshed (worker threads read it without locking).
void set_mesh_uv_table(const TileUvTable& table);

[[nodiscard]] std::vector<MeshVertex> build_chunk_mesh(const ChunkSnapshot& snapshot);

} // namespace minecpp
