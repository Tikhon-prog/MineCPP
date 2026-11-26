#pragma once

// One 16x16x16 chunk section: block-state ids plus 4x4x4 biome cells, each a
// PalettedContainer, as in the Anvil chunk format (1.18+).
//
// Index order matches vanilla: blocks are YZX ((y << 8) | (z << 4) | x), biome
// cells are YZX in a 4x4x4 grid ((y << 4) | (z << 2) | x). Block containers use
// at least 4 bits per index, biome containers at least 1.

#include "paletted_container.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace minecpp {

class ChunkSection {
public:
    using BlockId = PalettedContainer::Value;
    using BiomeId = PalettedContainer::Value;

    static constexpr int kSize = 16;
    static constexpr int kBlockCount = kSize * kSize * kSize;
    static constexpr int kBiomeSize = 4;
    static constexpr int kBiomeCount = kBiomeSize * kBiomeSize * kBiomeSize;
    static constexpr unsigned kBlockMinBits = 4;
    static constexpr unsigned kBiomeMinBits = 1;

    // A section filled with one block id and one biome id. `air` names the id
    // that counts as empty for non_air_count() and is_empty().
    explicit ChunkSection(BlockId fill_block = 0, BiomeId fill_biome = 0, BlockId air = 0)
        : blocks_(PalettedContainer::uniform(fill_block, kBlockCount, kBlockMinBits)),
          biomes_(PalettedContainer::uniform(fill_biome, kBiomeCount, kBiomeMinBits)),
          air_(air),
          non_air_(fill_block == air ? 0 : kBlockCount) {}

    // Adopts loaded containers. Sizes must match a section.
    static ChunkSection from_containers(PalettedContainer blocks, PalettedContainer biomes, BlockId air = 0) {
        if (blocks.size() != static_cast<std::size_t>(kBlockCount) ||
            biomes.size() != static_cast<std::size_t>(kBiomeCount)) {
            throw std::invalid_argument("ChunkSection: wrong container size");
        }
        int non_air = 0;
        for (int i = 0; i < kBlockCount; ++i) {
            if (blocks.get(static_cast<std::size_t>(i)) != air) {
                ++non_air;
            }
        }
        return ChunkSection(std::move(blocks), std::move(biomes), air, non_air);
    }

    static constexpr int block_index(int x, int y, int z) {
        return (y << 8) | (z << 4) | x;
    }
    static constexpr int biome_index(int bx, int by, int bz) {
        return (by << 4) | (bz << 2) | bx;
    }

    BlockId get_block(int x, int y, int z) const {
        check_block(x, y, z);
        return blocks_.get(static_cast<std::size_t>(block_index(x, y, z)));
    }

    // Returns the replaced id.
    BlockId set_block(int x, int y, int z, BlockId id) {
        check_block(x, y, z);
        const BlockId old = blocks_.set(static_cast<std::size_t>(block_index(x, y, z)), id);
        if (old == air_ && id != air_) {
            ++non_air_;
        } else if (old != air_ && id == air_) {
            --non_air_;
        }
        return old;
    }

    // Biome of the 4x4x4 cell that contains block (x, y, z), in 0..15.
    BiomeId get_biome_at_block(int x, int y, int z) const {
        check_block(x, y, z);
        return biomes_.get(static_cast<std::size_t>(biome_index(x >> 2, y >> 2, z >> 2)));
    }

    BiomeId get_biome_cell(int bx, int by, int bz) const {
        check_biome(bx, by, bz);
        return biomes_.get(static_cast<std::size_t>(biome_index(bx, by, bz)));
    }

    BiomeId set_biome_cell(int bx, int by, int bz, BiomeId id) {
        check_biome(bx, by, bz);
        return biomes_.set(static_cast<std::size_t>(biome_index(bx, by, bz)), id);
    }

    int non_air_count() const noexcept { return non_air_; }
    bool is_empty() const noexcept { return non_air_ == 0; }

    const PalettedContainer& blocks() const noexcept { return blocks_; }
    const PalettedContainer& biomes() const noexcept { return biomes_; }

private:
    ChunkSection(PalettedContainer blocks, PalettedContainer biomes, BlockId air, int non_air)
        : blocks_(std::move(blocks)), biomes_(std::move(biomes)), air_(air), non_air_(non_air) {}

    static void check_block(int x, int y, int z) {
        if (x < 0 || x >= kSize || y < 0 || y >= kSize || z < 0 || z >= kSize) {
            throw std::out_of_range("ChunkSection: block coordinate out of range");
        }
    }
    static void check_biome(int x, int y, int z) {
        if (x < 0 || x >= kBiomeSize || y < 0 || y >= kBiomeSize || z < 0 || z >= kBiomeSize) {
            throw std::out_of_range("ChunkSection: biome coordinate out of range");
        }
    }

    PalettedContainer blocks_;
    PalettedContainer biomes_;
    BlockId air_;
    int non_air_;
};

}  // namespace minecpp
