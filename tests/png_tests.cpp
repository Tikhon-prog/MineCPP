#include "png.hpp"
#include "png_test_util.hpp"

#include <iostream>
#include <stdexcept>

using namespace minecpp;
using pngtest::Bytes;
using pngtest::Spec;

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Test failed: " #expression); } while (false)

template <typename Fn>
static bool throws(Fn&& fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

static bool pixel_is(const Image& image, int x, int y, int r, int g, int b, int a) {
    const std::uint8_t* p = image.pixel(x, y);
    return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
}

int main() {
    // RGBA 2x2, no filter.
    {
        const Bytes rgba = {255, 0, 0, 255, 0, 255, 0, 128,
                            0, 0, 255, 0, 10, 20, 30, 40};
        const Image image = decode_png(pngtest::make_rgba_png(2, 2, rgba));
        CHECK(image.width == 2 && image.height == 2);
        CHECK(image.rgba == rgba);
        CHECK(pixel_is(image, 1, 0, 0, 255, 0, 128));
        CHECK(pixel_is(image, 1, 1, 10, 20, 30, 40));
    }

    // Every filter type, on an image whose rows differ.
    {
        constexpr int w = 7;
        constexpr int h = 10;
        Bytes rgba;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                rgba.push_back(static_cast<std::uint8_t>(x * 37 + y * 11));
                rgba.push_back(static_cast<std::uint8_t>(x * 5 + y * 90));
                rgba.push_back(static_cast<std::uint8_t>((x ^ y) * 23));
                rgba.push_back(static_cast<std::uint8_t>(255 - x * y));
            }
        }
        Spec spec;
        spec.width = w;
        spec.height = h;
        const Bytes scan = pngtest::filter_rows(rgba, w * 4, h, 4, [](std::size_t y) { return static_cast<int>(y % 5); });
        const Image image = decode_png(pngtest::make_png(spec, scan));
        CHECK(image.rgba == rgba);
    }

    // RGB (no alpha) fills alpha with 255; tRNS colour key makes one colour clear.
    {
        Spec spec;
        spec.width = 2;
        spec.height = 1;
        spec.color_type = 2;
        const Bytes rows = {1, 2, 3, 9, 8, 7};
        spec.transparency = {0, 9, 0, 8, 0, 7};
        const Image image = decode_png(pngtest::make_png(spec, pngtest::filter_rows(rows, 6, 1, 3, [](std::size_t) { return 0; })));
        CHECK(pixel_is(image, 0, 0, 1, 2, 3, 255));
        CHECK(pixel_is(image, 1, 0, 9, 8, 7, 0));
    }

    // Grey, 8-bit and 1-bit.
    {
        Spec spec;
        spec.width = 3;
        spec.height = 1;
        spec.color_type = 0;
        const Bytes rows = {0, 128, 255};
        const Image image = decode_png(pngtest::make_png(spec, pngtest::filter_rows(rows, 3, 1, 1, [](std::size_t) { return 0; })));
        CHECK(pixel_is(image, 1, 0, 128, 128, 128, 255));

        Spec one;
        one.width = 8;
        one.height = 1;
        one.depth = 1;
        one.color_type = 0;
        const Bytes bits = {0b10100101};
        const Image bw = decode_png(pngtest::make_png(one, pngtest::filter_rows(bits, 1, 1, 1, [](std::size_t) { return 0; })));
        CHECK(pixel_is(bw, 0, 0, 255, 255, 255, 255));
        CHECK(pixel_is(bw, 1, 0, 0, 0, 0, 255));
        CHECK(pixel_is(bw, 2, 0, 255, 255, 255, 255));
        CHECK(pixel_is(bw, 7, 0, 255, 255, 255, 255));
    }

    // Grey + alpha.
    {
        Spec spec;
        spec.width = 1;
        spec.height = 1;
        spec.color_type = 4;
        const Bytes rows = {200, 100};
        const Image image = decode_png(pngtest::make_png(spec, pngtest::filter_rows(rows, 2, 1, 2, [](std::size_t) { return 0; })));
        CHECK(pixel_is(image, 0, 0, 200, 200, 200, 100));
    }

    // 4-bit palette with tRNS alpha for the first entries only.
    {
        Spec spec;
        spec.width = 3;
        spec.height = 1;
        spec.depth = 4;
        spec.color_type = 3;
        spec.palette = {10, 20, 30, 40, 50, 60, 70, 80, 90};
        spec.transparency = {0, 128};
        const Bytes rows = {0x01, 0x20};  // indices 0, 1, 2 (+ padding nibble)
        const Image image = decode_png(pngtest::make_png(spec, pngtest::filter_rows(rows, 2, 1, 1, [](std::size_t) { return 0; })));
        CHECK(pixel_is(image, 0, 0, 10, 20, 30, 0));
        CHECK(pixel_is(image, 1, 0, 40, 50, 60, 128));
        CHECK(pixel_is(image, 2, 0, 70, 80, 90, 255));
    }

    // 16-bit RGB keeps the high byte.
    {
        Spec spec;
        spec.width = 1;
        spec.height = 1;
        spec.depth = 16;
        spec.color_type = 2;
        const Bytes rows = {0xAB, 0xCD, 0x12, 0x34, 0xFF, 0xFF};
        const Image image = decode_png(pngtest::make_png(spec, pngtest::filter_rows(rows, 6, 1, 6, [](std::size_t) { return 0; })));
        CHECK(pixel_is(image, 0, 0, 0xAB, 0x12, 0xFF, 255));
    }

    // Bad input is rejected, never read out of bounds.
    {
        const Bytes good = pngtest::make_rgba_png(1, 1, {1, 2, 3, 4});
        CHECK(!throws([&] { (void)decode_png(good); }));

        CHECK(throws([] { (void)decode_png({}); }));
        CHECK(throws([] { (void)decode_png({1, 2, 3, 4, 5, 6, 7, 8, 9}); }));

        Bytes bad_crc = good;
        bad_crc[20] ^= 0x01;  // inside IHDR
        CHECK(throws([&] { (void)decode_png(bad_crc); }));

        Bytes truncated(good.begin(), good.begin() + 30);
        CHECK(throws([&] { (void)decode_png(truncated); }));

        Spec interlaced;
        interlaced.interlace = 1;
        CHECK(throws([&] { (void)decode_png(pngtest::make_png(interlaced, {0, 1, 2, 3, 4})); }));

        Spec no_data;
        no_data.include_idat = false;
        CHECK(throws([&] { (void)decode_png(pngtest::make_png(no_data, {})); }));

        Spec no_palette;
        no_palette.color_type = 3;
        CHECK(throws([&] { (void)decode_png(pngtest::make_png(no_palette, {0, 0})); }));

        Spec wrong_size;  // data shorter than 1 row
        CHECK(throws([&] { (void)decode_png(pngtest::make_png(wrong_size, {0, 1})); }));

        Spec huge;
        huge.width = 100000;
        CHECK(throws([&] { (void)decode_png(pngtest::make_png(huge, {0, 0, 0, 0, 0})); }));

        CHECK(throws([] { (void)load_png_file("/nonexistent/definitely-missing.png"); }));
    }

    std::cout << "png tests passed\n";
    return 0;
}
