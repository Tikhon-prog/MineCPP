#pragma once

// Tiny PNG encoder for tests: builds files with chosen colour type, bit depth
// and per-row filters so the decoder can be checked against known pixels.

#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <vector>

namespace pngtest {

using Bytes = std::vector<std::uint8_t>;

inline void put32(Bytes& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24U));
    out.push_back(static_cast<std::uint8_t>(value >> 16U));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}

inline void add_chunk(Bytes& out, const char* type, const Bytes& body) {
    put32(out, static_cast<std::uint32_t>(body.size()));
    Bytes typed(type, type + 4);
    typed.insert(typed.end(), body.begin(), body.end());
    out.insert(out.end(), typed.begin(), typed.end());
    put32(out, static_cast<std::uint32_t>(crc32(0L, typed.data(), static_cast<uInt>(typed.size()))));
}

inline int paeth_predict(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

// `rows` holds `height` unfiltered scanlines of `stride` bytes each. Returns
// scanlines with a filter byte in front; row y uses filter_for_row(y) (0..4).
inline Bytes filter_rows(const Bytes& rows, std::size_t stride, std::size_t height, std::size_t bytes_per_pixel,
                         const std::function<int(std::size_t)>& filter_for_row) {
    Bytes out;
    for (std::size_t y = 0; y < height; ++y) {
        const int filter = filter_for_row(y);
        out.push_back(static_cast<std::uint8_t>(filter));
        for (std::size_t i = 0; i < stride; ++i) {
            const int cur = rows[y * stride + i];
            const int left = i >= bytes_per_pixel ? rows[y * stride + i - bytes_per_pixel] : 0;
            const int up = y > 0 ? rows[(y - 1) * stride + i] : 0;
            const int up_left = (y > 0 && i >= bytes_per_pixel) ? rows[(y - 1) * stride + i - bytes_per_pixel] : 0;
            int predicted = 0;
            switch (filter) {
                case 1: predicted = left; break;
                case 2: predicted = up; break;
                case 3: predicted = (left + up) / 2; break;
                case 4: predicted = paeth_predict(left, up, up_left); break;
                default: break;
            }
            out.push_back(static_cast<std::uint8_t>((cur - predicted) & 0xFF));
        }
    }
    return out;
}

inline Bytes deflate(const Bytes& input) {
    uLongf size = compressBound(static_cast<uLong>(input.size()));
    Bytes out(size);
    compress(out.data(), &size, input.data(), static_cast<uLong>(input.size()));
    out.resize(size);
    return out;
}

struct Spec {
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::uint8_t depth = 8;
    std::uint8_t color_type = 6;
    std::uint8_t interlace = 0;
    Bytes palette;        // PLTE body, empty = none
    Bytes transparency;   // tRNS body, empty = none
    bool include_idat = true;
};

// `scanlines` already carries filter bytes.
inline Bytes make_png(const Spec& spec, const Bytes& scanlines) {
    Bytes out{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    Bytes header;
    put32(header, spec.width);
    put32(header, spec.height);
    header.push_back(spec.depth);
    header.push_back(spec.color_type);
    header.push_back(0);
    header.push_back(0);
    header.push_back(spec.interlace);
    add_chunk(out, "IHDR", header);
    if (!spec.palette.empty()) add_chunk(out, "PLTE", spec.palette);
    if (!spec.transparency.empty()) add_chunk(out, "tRNS", spec.transparency);
    if (spec.include_idat) add_chunk(out, "IDAT", deflate(scanlines));
    add_chunk(out, "IEND", {});
    return out;
}

// Convenience: an RGBA image with filter 0 on every row.
inline Bytes make_rgba_png(int width, int height, const Bytes& rgba) {
    Spec spec;
    spec.width = static_cast<std::uint32_t>(width);
    spec.height = static_cast<std::uint32_t>(height);
    const std::size_t stride = static_cast<std::size_t>(width) * 4U;
    return make_png(spec, filter_rows(rgba, stride, static_cast<std::size_t>(height), 4, [](std::size_t) { return 0; }));
}

}  // namespace pngtest
