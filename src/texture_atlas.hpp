#pragma once

// Packs the block textures from an imported client jar into one RGBA atlas.
//
// Reads <textures/block>/*.png (stone, dirt, grass_block_top, ...) that
// minecpp-launcher extracted from the player's own client.jar. No texture is
// bundled with MineCPP. A texture that is missing falls back to a flat colour
// tile, so the game still runs with a partial or empty directory.
//
// Vanilla draws grass and leaves in grey and tints them per biome. Until biomes
// exist here, the plains colours are baked into the tiles.

#include "png.hpp"
#include "tiles.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace minecpp {

struct Atlas {
    static constexpr int kColumns = 4;
    static constexpr int kRows = (static_cast<int>(kTileCount) + kColumns - 1) / kColumns;

    int tile_size = 16;
    std::vector<std::uint8_t> rgba;           // width() * height() * 4
    std::array<bool, kTileCount> from_file{}; // true when the tile came from a PNG

    int width() const { return tile_size * kColumns; }
    int height() const { return tile_size * kRows; }
    int loaded_count() const;

    // UV rectangle of a tile, inset by a quarter texel against bleeding.
    UvRect uv(Tile tile) const;
    TileUvTable uv_table() const;
};

// `block_texture_dir` is .../assets/minecraft/textures/block.
Atlas build_atlas(const std::filesystem::path& block_texture_dir);

// An atlas of flat colours only (no files read).
Atlas build_flat_atlas(int tile_size = 16);

// Image helpers, public for tests.
Image fit_to_size(const Image& source, int size);  // nearest-neighbour, first frame only
void multiply_tint(Image& image, std::uint8_t r, std::uint8_t g, std::uint8_t b);
void alpha_over(Image& base, const Image& overlay);  // same size

}  // namespace minecpp
