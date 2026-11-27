#include "png.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace minecpp {
namespace {

constexpr int kMaxDimension = 8192;
constexpr std::size_t kMaxRawBytes = 256U * 1024U * 1024U;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error("PNG: " + message);
}

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24U) | (static_cast<std::uint32_t>(p[1]) << 16U) |
           (static_cast<std::uint32_t>(p[2]) << 8U) | static_cast<std::uint32_t>(p[3]);
}

int paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

// Reads sample `index` of a row at `depth` bits. 16-bit samples return all 16 bits.
unsigned read_sample(const std::uint8_t* row, std::size_t index, unsigned depth) {
    if (depth == 8) return row[index];
    if (depth == 16) return (static_cast<unsigned>(row[index * 2]) << 8U) | row[index * 2 + 1];
    const std::size_t bit = index * depth;
    const unsigned byte = row[bit / 8U];
    const unsigned shift = 8U - depth - static_cast<unsigned>(bit % 8U);
    return (byte >> shift) & ((1U << depth) - 1U);
}

// Scales a sample of any depth to 0..255.
std::uint8_t to_byte(unsigned sample, unsigned depth) {
    if (depth == 8) return static_cast<std::uint8_t>(sample);
    if (depth == 16) return static_cast<std::uint8_t>(sample >> 8U);
    const unsigned max = (1U << depth) - 1U;
    return static_cast<std::uint8_t>(sample * 255U / max);
}

}  // namespace

Image decode_png(const std::vector<std::uint8_t>& data) {
    static constexpr std::array<std::uint8_t, 8> kSignature{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (data.size() < 8 || !std::equal(kSignature.begin(), kSignature.end(), data.begin())) {
        fail("bad signature");
    }

    std::uint32_t width = 0, height = 0;
    unsigned depth = 0, color_type = 0;
    bool have_header = false;
    std::vector<std::uint8_t> palette;       // RGB triples
    std::vector<std::uint8_t> transparency;  // tRNS payload
    std::vector<std::uint8_t> idat;
    bool seen_end = false;

    std::size_t pos = 8;
    while (pos + 12 <= data.size() && !seen_end) {
        const std::uint32_t length = be32(&data[pos]);
        if (length > data.size() || pos + 12U + length > data.size()) fail("truncated chunk");
        const char* type = reinterpret_cast<const char*>(&data[pos + 4]);
        const std::uint8_t* body = &data[pos + 8];
        const std::uint32_t expected_crc = be32(body + length);
        uLong crc = crc32(0L, reinterpret_cast<const Bytef*>(type), 4U);
        crc = crc32(crc, body, length);
        if (static_cast<std::uint32_t>(crc) != expected_crc) fail("chunk CRC mismatch");

        const std::string name(type, 4);
        if (name == "IHDR") {
            if (length != 13) fail("bad IHDR");
            width = be32(body);
            height = be32(body + 4);
            depth = body[8];
            color_type = body[9];
            if (body[10] != 0 || body[11] != 0) fail("unsupported compression or filter method");
            if (body[12] != 0) fail("interlaced PNG is not supported");
            have_header = true;
        } else if (name == "PLTE") {
            if (length % 3U != 0 || length == 0 || length > 768) fail("bad PLTE");
            palette.assign(body, body + length);
        } else if (name == "tRNS") {
            transparency.assign(body, body + length);
        } else if (name == "IDAT") {
            idat.insert(idat.end(), body, body + length);
        } else if (name == "IEND") {
            seen_end = true;
        }
        pos += 12U + length;
    }
    if (!have_header) fail("missing IHDR");
    if (idat.empty()) fail("missing image data");
    if (width == 0 || height == 0 || width > static_cast<std::uint32_t>(kMaxDimension) ||
        height > static_cast<std::uint32_t>(kMaxDimension)) {
        fail("image dimensions out of range");
    }

    unsigned channels = 0;
    switch (color_type) {
        case 0: channels = 1; if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) fail("bad bit depth"); break;
        case 2: channels = 3; if (depth != 8 && depth != 16) fail("bad bit depth"); break;
        case 3: channels = 1; if (depth != 1 && depth != 2 && depth != 4 && depth != 8) fail("bad bit depth"); break;
        case 4: channels = 2; if (depth != 8 && depth != 16) fail("bad bit depth"); break;
        case 6: channels = 4; if (depth != 8 && depth != 16) fail("bad bit depth"); break;
        default: fail("bad color type");
    }
    if (color_type == 3 && palette.empty()) fail("palette image without PLTE");

    const std::size_t bits_per_pixel = static_cast<std::size_t>(channels) * depth;
    const std::size_t stride = (static_cast<std::size_t>(width) * bits_per_pixel + 7U) / 8U;
    const std::size_t raw_size = (stride + 1U) * height;
    if (raw_size > kMaxRawBytes) fail("image too large");

    std::vector<std::uint8_t> raw(raw_size);
    uLongf raw_len = static_cast<uLongf>(raw_size);
    const int status = uncompress(raw.data(), &raw_len, idat.data(), static_cast<uLong>(idat.size()));
    if (status != Z_OK || raw_len != raw_size) fail("image data is corrupt");

    // Undo the per-row filters in place.
    const std::size_t step = std::max<std::size_t>(1U, bits_per_pixel / 8U);
    std::vector<std::uint8_t> zero_row(stride, 0);
    for (std::uint32_t y = 0; y < height; ++y) {
        std::uint8_t* row = &raw[y * (stride + 1U)];
        const std::uint8_t filter = row[0];
        std::uint8_t* cur = row + 1;
        const std::uint8_t* prev = y == 0 ? zero_row.data() : &raw[(y - 1U) * (stride + 1U) + 1U];
        switch (filter) {
            case 0: break;
            case 1:
                for (std::size_t i = step; i < stride; ++i) cur[i] = static_cast<std::uint8_t>(cur[i] + cur[i - step]);
                break;
            case 2:
                for (std::size_t i = 0; i < stride; ++i) cur[i] = static_cast<std::uint8_t>(cur[i] + prev[i]);
                break;
            case 3:
                for (std::size_t i = 0; i < stride; ++i) {
                    const int left = i >= step ? cur[i - step] : 0;
                    cur[i] = static_cast<std::uint8_t>(cur[i] + ((left + prev[i]) / 2));
                }
                break;
            case 4:
                for (std::size_t i = 0; i < stride; ++i) {
                    const int left = i >= step ? cur[i - step] : 0;
                    const int up_left = i >= step ? prev[i - step] : 0;
                    cur[i] = static_cast<std::uint8_t>(cur[i] + paeth(left, prev[i], up_left));
                }
                break;
            default: fail("bad row filter");
        }
    }

    Image image;
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.rgba.assign(static_cast<std::size_t>(width) * height * 4U, 255);

    // tRNS colour keys compare at the file's own sample depth.
    auto key_at = [&](std::size_t k) -> unsigned {
        return k * 2U + 1U < transparency.size()
                   ? (static_cast<unsigned>(transparency[k * 2U]) << 8U) | transparency[k * 2U + 1U]
                   : 0xFFFFFFFFU;
    };

    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* row = &raw[y * (stride + 1U) + 1U];
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint8_t* out = image.pixel(static_cast<int>(x), static_cast<int>(y));
            switch (color_type) {
                case 0: {
                    const unsigned g = read_sample(row, x, depth);
                    out[0] = out[1] = out[2] = to_byte(g, depth);
                    if (transparency.size() >= 2 && g == key_at(0)) out[3] = 0;
                    break;
                }
                case 2: {
                    const unsigned r = read_sample(row, x * 3U, depth);
                    const unsigned g = read_sample(row, x * 3U + 1U, depth);
                    const unsigned b = read_sample(row, x * 3U + 2U, depth);
                    out[0] = to_byte(r, depth);
                    out[1] = to_byte(g, depth);
                    out[2] = to_byte(b, depth);
                    if (transparency.size() >= 6 && r == key_at(0) && g == key_at(1) && b == key_at(2)) out[3] = 0;
                    break;
                }
                case 3: {
                    const unsigned index = read_sample(row, x, depth);
                    if (index * 3U + 2U >= palette.size()) fail("palette index out of range");
                    out[0] = palette[index * 3U];
                    out[1] = palette[index * 3U + 1U];
                    out[2] = palette[index * 3U + 2U];
                    out[3] = index < transparency.size() ? transparency[index] : 255;
                    break;
                }
                case 4: {
                    const unsigned g = read_sample(row, x * 2U, depth);
                    out[0] = out[1] = out[2] = to_byte(g, depth);
                    out[3] = to_byte(read_sample(row, x * 2U + 1U, depth), depth);
                    break;
                }
                default: {  // 6
                    out[0] = to_byte(read_sample(row, x * 4U, depth), depth);
                    out[1] = to_byte(read_sample(row, x * 4U + 1U, depth), depth);
                    out[2] = to_byte(read_sample(row, x * 4U + 2U, depth), depth);
                    out[3] = to_byte(read_sample(row, x * 4U + 3U, depth), depth);
                    break;
                }
            }
        }
    }
    return image;
}

Image load_png_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("PNG: cannot open " + path.string());
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return decode_png(bytes);
}

}  // namespace minecpp
