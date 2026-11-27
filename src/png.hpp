#pragma once

// Small PNG decoder for Minecraft textures: 8-bit (and 16/1/2/4-bit) grey,
// grey+alpha, RGB, RGBA and palette images, with tRNS transparency. Output is
// always 8-bit RGBA. Interlaced (Adam7) files are rejected. Chunk CRCs are
// verified and dimensions are limited, since the input comes from disk.

#include <cstdint>
#include <filesystem>
#include <vector>

namespace minecpp {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;  // width * height * 4

    std::uint8_t* pixel(int x, int y) {
        return &rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U];
    }
    const std::uint8_t* pixel(int x, int y) const {
        return &rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U];
    }
};

// Throws std::runtime_error on malformed or unsupported input.
Image decode_png(const std::vector<std::uint8_t>& data);
Image load_png_file(const std::filesystem::path& path);

}  // namespace minecpp
