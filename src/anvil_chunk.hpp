#pragma once

#include "nbt.hpp"
#include "block.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <optional>
#include <string>
#include <vector>

namespace minecpp::nbt {

struct BlockState {
    std::string name;
    std::map<std::string, std::string, std::less<>> properties;
};

struct ChunkSection {
    int section_y = 0;
    std::vector<BlockState> palette;
    std::array<std::uint16_t, 4096> palette_indices{};

    [[nodiscard]] const BlockState& at(int x, int y, int z) const;
};

struct AnvilChunk {
    int data_version = 0;
    int chunk_x = 0;
    int chunk_z = 0;
    std::vector<ChunkSection> sections;

    [[nodiscard]] const ChunkSection* find_section(int section_y) const noexcept;
    [[nodiscard]] const BlockState* block_at(int world_x, int world_y, int world_z) const noexcept;
};

// Supports the modern 1.18+ root/sections/block_states layout and the
// 1.16-1.17 Level/Sections/Palette/BlockStates layout. Unknown namespaced
// states and their properties are preserved verbatim.
[[nodiscard]] std::optional<Block> to_builtin_block(const BlockState& state) noexcept;

[[nodiscard]] AnvilChunk decode_anvil_chunk(
    const Document& document,
    std::optional<int> expected_chunk_x = {},
    std::optional<int> expected_chunk_z = {});

} // namespace minecpp::nbt
