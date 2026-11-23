#pragma once

#include "block.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace minecpp {

inline constexpr int kChunkSize = 16;
inline constexpr int kWorldHeight = 96;
inline constexpr int kSeaLevel = 28;

struct ChunkPos {
    int x = 0;
    int z = 0;
    bool operator==(const ChunkPos&) const = default;
};

struct ChunkPosHash {
    std::size_t operator()(const ChunkPos& pos) const noexcept {
        const auto x = static_cast<std::uint32_t>(pos.x);
        const auto z = static_cast<std::uint32_t>(pos.z);
        std::uint64_t value = (static_cast<std::uint64_t>(x) << 32U) | z;
        value ^= value >> 30U;
        value *= 0xbf58476d1ce4e5b9ULL;
        value ^= value >> 27U;
        value *= 0x94d049bb133111ebULL;
        value ^= value >> 31U;
        return static_cast<std::size_t>(value);
    }
};

struct BlockPos {
    int x = 0;
    int y = 0;
    int z = 0;
    bool operator==(const BlockPos&) const = default;
};

struct BlockPosHash {
    std::size_t operator()(const BlockPos& pos) const noexcept {
        std::uint64_t h = static_cast<std::uint32_t>(pos.x);
        h = (h * 0x9e3779b185ebca87ULL) ^ static_cast<std::uint32_t>(pos.y);
        h = (h * 0xc2b2ae3d27d4eb4fULL) ^ static_cast<std::uint32_t>(pos.z);
        h ^= h >> 29U;
        return static_cast<std::size_t>(h);
    }
};

inline std::size_t block_index(int x, int y, int z) {
    return (static_cast<std::size_t>(y) * kChunkSize + static_cast<std::size_t>(z)) *
           kChunkSize + static_cast<std::size_t>(x);
}

struct MeshVertex { float x, y, z, r, g, b; };

struct Chunk {
    ChunkPos position;
    std::vector<Block> blocks;
    std::vector<MeshVertex> mesh;
    std::uint64_t mesh_revision = 0;
    bool mesh_ready = false;
};

struct BlockEdit {
    BlockPos position;
    Block block = Block::Air;
};

} // namespace minecpp
