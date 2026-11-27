#include "png_test_util.hpp"
#include "texture_atlas.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace minecpp;
namespace fs = std::filesystem;
using pngtest::Bytes;

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Test failed: " #expression); } while (false)

static Bytes solid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
    Bytes rgba;
    for (int i = 0; i < w * h; ++i) {
        rgba.push_back(r);
        rgba.push_back(g);
        rgba.push_back(b);
        rgba.push_back(a);
    }
    return rgba;
}

static void write_png(const fs::path& dir, const std::string& name, int w, int h, const Bytes& rgba) {
    const Bytes png = pngtest::make_rgba_png(w, h, rgba);
    std::ofstream file(dir / (name + ".png"), std::ios::binary);
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
}

// Pixel (x, y) inside the tile of `tile`.
static const std::uint8_t* tile_pixel(const Atlas& atlas, Tile tile, int x, int y) {
    const int index = static_cast<int>(tile);
    const int col = index % Atlas::kColumns;
    const int row = index / Atlas::kColumns;
    const std::size_t at = (static_cast<std::size_t>(row * atlas.tile_size + y) * static_cast<std::size_t>(atlas.width()) +
                            static_cast<std::size_t>(col * atlas.tile_size + x)) * 4U;
    return &atlas.rgba[at];
}

int main() {
    // Tile table: grass uses three tiles, logs two, everything else one.
    CHECK(tile_for(Block::Grass, FaceKind::Top) == Tile::GrassTop);
    CHECK(tile_for(Block::Grass, FaceKind::Side) == Tile::GrassSide);
    CHECK(tile_for(Block::Grass, FaceKind::Bottom) == Tile::Dirt);
    CHECK(tile_for(Block::Wood, FaceKind::Side) == Tile::LogSide);
    CHECK(tile_for(Block::Wood, FaceKind::Top) == Tile::LogTop);
    CHECK(tile_for(Block::Stone, FaceKind::Bottom) == Tile::Stone);
    CHECK(Atlas::kRows == 3);

    // Image helpers.
    {
        Image src;
        src.width = 4;
        src.height = 4;
        src.rgba = solid(4, 4, 200, 100, 50);
        multiply_tint(src, 255, 128, 0);
        CHECK(src.rgba[0] == 200 && src.rgba[1] == 50 && src.rgba[2] == 0 && src.rgba[3] == 255);

        const Image small = fit_to_size(src, 2);
        CHECK(small.width == 2 && small.height == 2 && small.rgba.size() == 16U);
        CHECK(small.rgba[0] == 200);

        Image base;
        base.width = 1;
        base.height = 1;
        base.rgba = {0, 0, 100, 255};
        Image over;
        over.width = 1;
        over.height = 1;
        over.rgba = {200, 0, 0, 255};
        alpha_over(base, over);
        CHECK(base.rgba[0] == 200 && base.rgba[2] == 0);  // fully opaque overlay wins
        over.rgba = {200, 0, 0, 0};
        base.rgba = {0, 0, 100, 255};
        alpha_over(base, over);
        CHECK(base.rgba[0] == 0 && base.rgba[2] == 100);  // transparent overlay changes nothing
    }

    // Animated strip: only the first square frame is used.
    {
        Image strip;
        strip.width = 2;
        strip.height = 6;  // three frames
        strip.rgba.assign(2 * 6 * 4, 0);
        for (int y = 0; y < 6; ++y) {
            for (int x = 0; x < 2; ++x) {
                std::uint8_t* p = strip.pixel(x, y);
                p[0] = static_cast<std::uint8_t>(y < 2 ? 255 : 0);  // frame 0 red, others black
                p[3] = 255;
            }
        }
        const Image fitted = fit_to_size(strip, 2);
        CHECK(fitted.pixel(0, 1)[0] == 255);
    }

    // No directory: a flat-colour atlas with nothing loaded.
    {
        const Atlas flat = build_atlas("/nonexistent/minecpp-test-textures");
        CHECK(flat.loaded_count() == 0);
        CHECK(flat.tile_size == 16);
        CHECK(flat.rgba.size() == static_cast<std::size_t>(flat.width() * flat.height() * 4));
        CHECK(!flat.uv_table().enabled);
        CHECK(tile_pixel(flat, Tile::Stone, 0, 0)[3] == 255);
    }

    // A real directory with some textures.
    const fs::path dir = fs::temp_directory_path() /
        ("minecpp_atlas_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    write_png(dir, "stone", 4, 4, solid(4, 4, 100, 110, 120));
    write_png(dir, "dirt", 4, 4, solid(4, 4, 130, 80, 40));
    write_png(dir, "grass_block_top", 4, 4, solid(4, 4, 200, 200, 200));
    write_png(dir, "grass_block_side", 4, 4, solid(4, 4, 130, 80, 40));
    write_png(dir, "grass_block_side_overlay", 4, 4, solid(4, 4, 100, 100, 100, 0));  // fully transparent
    write_png(dir, "oak_leaves", 8, 8, solid(8, 8, 100, 100, 100));                    // bigger than the tile size
    {
        const Atlas atlas = build_atlas(dir);
        CHECK(atlas.tile_size == 4);  // follows the first loaded texture
        CHECK(atlas.loaded_count() == 5);
        CHECK(atlas.from_file[static_cast<std::size_t>(Tile::Stone)]);
        CHECK(!atlas.from_file[static_cast<std::size_t>(Tile::Bedrock)]);  // no file: flat fallback

        const std::uint8_t* stone = tile_pixel(atlas, Tile::Stone, 3, 3);
        CHECK(stone[0] == 100 && stone[1] == 110 && stone[2] == 120 && stone[3] == 255);

        // Grass top is tinted by the plains colour (145, 189, 89).
        const std::uint8_t* top = tile_pixel(atlas, Tile::GrassTop, 0, 0);
        CHECK(top[0] == 200 * 145 / 255 && top[1] == 200 * 189 / 255 && top[2] == 200 * 89 / 255);

        // Transparent overlay leaves the side texture as it was.
        const std::uint8_t* side = tile_pixel(atlas, Tile::GrassSide, 1, 1);
        CHECK(side[0] == 130 && side[1] == 80 && side[2] == 40);

        // Leaves were resampled from 8x8 down to the 4x4 tile and tinted.
        const std::uint8_t* leaves = tile_pixel(atlas, Tile::Leaves, 2, 2);
        CHECK(leaves[0] == 100 * 119 / 255 && leaves[1] == 100 * 171 / 255);

        // UVs: inside 0..1, inside the right cell, distinct per tile, usable.
        const TileUvTable table = atlas.uv_table();
        CHECK(table.enabled);
        for (std::size_t i = 0; i < kTileCount; ++i) {
            const UvRect r = table.uv[i];
            CHECK(r.u0 >= 0.0F && r.v0 >= 0.0F && r.u1 <= 1.0F && r.v1 <= 1.0F);
            CHECK(r.u0 < r.u1 && r.v0 < r.v1);
            for (std::size_t j = i + 1; j < kTileCount; ++j) {
                CHECK(r.u0 != table.uv[j].u0 || r.v0 != table.uv[j].v0);
            }
        }
        const UvRect stone_uv = atlas.uv(Tile::Stone);  // index 3 -> column 3, row 0
        CHECK(stone_uv.u0 > 0.75F && stone_uv.u1 <= 1.0F && stone_uv.v1 < 0.34F);
    }

    // A texture directory that has no PNG at all is the flat atlas as well.
    {
        const fs::path empty = dir / "empty";
        fs::create_directories(empty);
        CHECK(build_atlas(empty).loaded_count() == 0);
    }

    // A corrupt PNG is skipped, not fatal.
    {
        const fs::path broken = dir / "broken";
        fs::create_directories(broken);
        {
            std::ofstream file(broken / "stone.png", std::ios::binary);
            file << "this is not a png";
        }
        write_png(broken, "dirt", 4, 4, solid(4, 4, 1, 2, 3));
        const Atlas atlas = build_atlas(broken);
        CHECK(atlas.loaded_count() == 1);
        CHECK(!atlas.from_file[static_cast<std::size_t>(Tile::Stone)]);
        CHECK(atlas.from_file[static_cast<std::size_t>(Tile::Dirt)]);
    }

    std::error_code ignored;
    fs::remove_all(dir, ignored);
    std::cout << "texture atlas tests passed\n";
    return 0;
}
