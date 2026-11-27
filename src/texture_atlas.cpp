#include "texture_atlas.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>

namespace minecpp {
namespace {

constexpr std::uint8_t kGrassTint[3] = {145, 189, 89};   // plains grass  #91BD59
constexpr std::uint8_t kLeafTint[3] = {119, 171, 47};    // plains foliage #77AB2F

std::optional<Image> try_load(const std::filesystem::path& dir, const std::string& name) {
    try {
        Image image = load_png_file(dir / (name + ".png"));
        if (image.width <= 0 || image.height <= 0) return std::nullopt;
        return image;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

Image blank(int size) {
    Image image;
    image.width = size;
    image.height = size;
    image.rgba.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4U, 0);
    return image;
}

Block block_for_flat_colour(Tile tile) {
    switch (tile) {
        case Tile::GrassTop:
        case Tile::GrassSide: return Block::Grass;
        case Tile::Dirt: return Block::Dirt;
        case Tile::Stone: return Block::Stone;
        case Tile::Sand: return Block::Sand;
        case Tile::LogSide:
        case Tile::LogTop: return Block::Wood;
        case Tile::Leaves: return Block::Leaves;
        case Tile::Bedrock: return Block::Bedrock;
        case Tile::Count: break;
    }
    return Block::Stone;
}

Image flat_tile(Tile tile, int size) {
    Image image = blank(size);
    const Color3 c = block_color(block_for_flat_colour(tile));
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        image.rgba[i] = static_cast<std::uint8_t>(std::clamp(c.r, 0.0F, 1.0F) * 255.0F);
        image.rgba[i + 1] = static_cast<std::uint8_t>(std::clamp(c.g, 0.0F, 1.0F) * 255.0F);
        image.rgba[i + 2] = static_cast<std::uint8_t>(std::clamp(c.b, 0.0F, 1.0F) * 255.0F);
        image.rgba[i + 3] = 255;
    }
    return image;
}

void place(Atlas& atlas, Tile tile, const Image& image) {
    const int index = static_cast<int>(tile);
    const int col = index % Atlas::kColumns;
    const int row = index / Atlas::kColumns;
    const int size = atlas.tile_size;
    for (int y = 0; y < size; ++y) {
        const std::size_t dst = (static_cast<std::size_t>(row * size + y) * static_cast<std::size_t>(atlas.width()) +
                                 static_cast<std::size_t>(col * size)) * 4U;
        const std::size_t src = static_cast<std::size_t>(y) * static_cast<std::size_t>(size) * 4U;
        std::copy_n(image.rgba.begin() + static_cast<std::ptrdiff_t>(src), static_cast<std::size_t>(size) * 4U,
                    atlas.rgba.begin() + static_cast<std::ptrdiff_t>(dst));
    }
}

}  // namespace

int Atlas::loaded_count() const {
    return static_cast<int>(std::count(from_file.begin(), from_file.end(), true));
}

UvRect Atlas::uv(Tile tile) const {
    const int index = static_cast<int>(tile);
    const int col = index % kColumns;
    const int row = index / kColumns;
    const float w = static_cast<float>(width());
    const float h = static_cast<float>(height());
    const float inset_u = 0.25F / w;
    const float inset_v = 0.25F / h;
    UvRect r;
    r.u0 = static_cast<float>(col * tile_size) / w + inset_u;
    r.v0 = static_cast<float>(row * tile_size) / h + inset_v;
    r.u1 = static_cast<float>((col + 1) * tile_size) / w - inset_u;
    r.v1 = static_cast<float>((row + 1) * tile_size) / h - inset_v;
    return r;
}

TileUvTable Atlas::uv_table() const {
    TileUvTable table;
    table.enabled = loaded_count() > 0;
    for (std::size_t i = 0; i < kTileCount; ++i) {
        table.uv[i] = uv(static_cast<Tile>(i));
    }
    return table;
}

Image fit_to_size(const Image& source, int size) {
    Image out = blank(size);
    if (source.width <= 0 || source.height <= 0 || size <= 0) return out;
    // Animated textures are a vertical strip of square frames: keep the first.
    const int frame_height = source.height >= source.width ? source.width : source.height;
    for (int y = 0; y < size; ++y) {
        const int sy = std::min(frame_height - 1, y * frame_height / size);
        for (int x = 0; x < size; ++x) {
            const int sx = std::min(source.width - 1, x * source.width / size);
            const std::uint8_t* s = source.pixel(sx, sy);
            std::uint8_t* d = out.pixel(x, y);
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = s[3];
        }
    }
    return out;
}

void multiply_tint(Image& image, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        image.rgba[i] = static_cast<std::uint8_t>(image.rgba[i] * r / 255);
        image.rgba[i + 1] = static_cast<std::uint8_t>(image.rgba[i + 1] * g / 255);
        image.rgba[i + 2] = static_cast<std::uint8_t>(image.rgba[i + 2] * b / 255);
    }
}

void alpha_over(Image& base, const Image& overlay) {
    if (base.width != overlay.width || base.height != overlay.height) return;
    for (std::size_t i = 0; i + 3 < base.rgba.size(); i += 4) {
        const int a = overlay.rgba[i + 3];
        for (std::size_t c = 0; c < 3; ++c) {
            base.rgba[i + c] = static_cast<std::uint8_t>((overlay.rgba[i + c] * a + base.rgba[i + c] * (255 - a)) / 255);
        }
        base.rgba[i + 3] = static_cast<std::uint8_t>(std::max<int>(base.rgba[i + 3], a));
    }
}

Atlas build_flat_atlas(int tile_size) {
    Atlas atlas;
    atlas.tile_size = std::clamp(tile_size, 1, 256);
    atlas.rgba.assign(static_cast<std::size_t>(atlas.width()) * static_cast<std::size_t>(atlas.height()) * 4U, 0);
    for (std::size_t i = 0; i < kTileCount; ++i) {
        const Tile tile = static_cast<Tile>(i);
        place(atlas, tile, flat_tile(tile, atlas.tile_size));
    }
    return atlas;
}

Atlas build_atlas(const std::filesystem::path& dir) {
    // Tile size follows the first texture that loads (16 for vanilla, more for
    // resource packs); everything else is resampled to it.
    int size = 0;
    for (const char* probe : {"stone", "dirt", "grass_block_top", "sand", "bedrock"}) {
        if (const auto image = try_load(dir, probe)) {
            size = std::clamp(image->width, 1, 256);
            break;
        }
    }
    if (size == 0) {
        return build_flat_atlas();
    }

    Atlas atlas = build_flat_atlas(size);
    auto load = [&](const std::string& name) -> std::optional<Image> {
        if (auto image = try_load(dir, name)) return fit_to_size(*image, size);
        return std::nullopt;
    };
    auto put = [&](Tile tile, const Image& image) {
        place(atlas, tile, image);
        atlas.from_file[static_cast<std::size_t>(tile)] = true;
    };

    if (auto image = load("grass_block_top")) {
        multiply_tint(*image, kGrassTint[0], kGrassTint[1], kGrassTint[2]);
        put(Tile::GrassTop, *image);
    }
    if (auto side = load("grass_block_side")) {
        if (auto overlay = load("grass_block_side_overlay")) {
            multiply_tint(*overlay, kGrassTint[0], kGrassTint[1], kGrassTint[2]);
            alpha_over(*side, *overlay);
        }
        put(Tile::GrassSide, *side);
    }
    if (auto image = load("dirt")) put(Tile::Dirt, *image);
    if (auto image = load("stone")) put(Tile::Stone, *image);
    if (auto image = load("sand")) put(Tile::Sand, *image);
    if (auto image = load("oak_log")) put(Tile::LogSide, *image);
    if (auto image = load("oak_log_top")) put(Tile::LogTop, *image);
    if (auto image = load("oak_leaves")) {
        multiply_tint(*image, kLeafTint[0], kLeafTint[1], kLeafTint[2]);
        put(Tile::Leaves, *image);
    }
    if (auto image = load("bedrock")) put(Tile::Bedrock, *image);
    return atlas;
}

}  // namespace minecpp
