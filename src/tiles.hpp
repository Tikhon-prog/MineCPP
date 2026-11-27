#pragma once

// Which atlas tile each block face uses. Shared by the mesher (which writes
// UVs) and the atlas builder (which packs the textures).

#include "block.hpp"

#include <array>
#include <cstddef>

namespace minecpp {

enum class Tile : int {
    GrassTop = 0,
    GrassSide,
    Dirt,
    Stone,
    Sand,
    LogSide,
    LogTop,
    Leaves,
    Bedrock,
    Count
};

constexpr std::size_t kTileCount = static_cast<std::size_t>(Tile::Count);

enum class FaceKind { Top, Bottom, Side };

struct UvRect {
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 1.0F;
    float v1 = 1.0F;
};

// UV rectangle of every tile; `enabled` is false when no atlas is loaded and
// the mesher should fall back to flat colours.
struct TileUvTable {
    bool enabled = false;
    std::array<UvRect, kTileCount> uv{};
};

constexpr Tile tile_for(Block block, FaceKind face) {
    switch (block) {
        case Block::Grass:
            return face == FaceKind::Top ? Tile::GrassTop : (face == FaceKind::Bottom ? Tile::Dirt : Tile::GrassSide);
        case Block::Dirt: return Tile::Dirt;
        case Block::Stone: return Tile::Stone;
        case Block::Sand: return Tile::Sand;
        case Block::Wood: return face == FaceKind::Side ? Tile::LogSide : Tile::LogTop;
        case Block::Leaves: return Tile::Leaves;
        case Block::Bedrock: return Tile::Bedrock;
        case Block::Air: break;
    }
    return Tile::Dirt;
}

}  // namespace minecpp
