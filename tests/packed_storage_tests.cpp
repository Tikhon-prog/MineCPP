#include "packed_storage.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace minecpp;

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

int main() {
    // Word counts for a 16x16x16 section (4096 entries). An entry never spans
    // two words, so widths that do not divide 64 waste bits at the top.
    CHECK(PackedStorage(4, 4096).words().size() == 256U);
    CHECK(PackedStorage(5, 4096).words().size() == 342U);
    CHECK(PackedStorage(9, 4096).words().size() == 586U);
    CHECK(PackedStorage(1, 64).words().size() == 1U);
    CHECK(PackedStorage(1, 65).words().size() == 2U);
    CHECK(PackedStorage(8, 0).words().empty());

    // Bit layout: entry i of a word sits at bit i * bits, low bits first.
    {
        PackedStorage s(4, 16);
        s.set(0, 0xA);
        s.set(1, 0x5);
        s.set(15, 0xF);
        CHECK(s.words().size() == 1U);
        CHECK(s.words()[0] == (0xFULL << 60 | 0x5ULL << 4 | 0xAULL));
    }

    // 5-bit entries: 12 per word, the top 4 bits of each word stay padding.
    {
        PackedStorage s(5, 24);
        for (std::size_t i = 0; i < 24; ++i) {
            s.set(i, 31U);
        }
        CHECK(s.words().size() == 2U);
        CHECK(s.words()[0] == 0x0FFFFFFFFFFFFFFFULL);
        CHECK(s.words()[1] == 0x0FFFFFFFFFFFFFFFULL);
    }

    // Round trip across widths, neighbours must not be disturbed.
    for (unsigned bits = 1; bits <= 32; ++bits) {
        const std::size_t length = 200;
        const std::uint64_t max = (std::uint64_t{1} << bits) - 1U;
        PackedStorage s(bits, length);
        for (std::size_t i = 0; i < length; ++i) {
            const auto value = static_cast<std::uint32_t>((i * 2654435761ULL + 12345ULL) & max);
            CHECK(s.set(i, value) == 0U);
        }
        for (std::size_t i = 0; i < length; ++i) {
            const auto value = static_cast<std::uint32_t>((i * 2654435761ULL + 12345ULL) & max);
            CHECK(s.get(i) == value);
        }
    }

    // set returns the previous value and overwrites cleanly.
    {
        PackedStorage s(7, 50);
        s.set(10, 100);
        CHECK(s.set(10, 3) == 100U);
        CHECK(s.get(10) == 3U);
        CHECK(s.get(9) == 0U);
        CHECK(s.get(11) == 0U);
    }

    // Adopted words keep padding bits untouched.
    {
        std::vector<std::uint64_t> raw{0xF000000000000001ULL};
        PackedStorage s(5, 3, raw);
        CHECK(s.get(0) == 1U);
        s.set(1, 2U);
        CHECK((s.words()[0] >> 60) == 0xFU);
    }

    // Repacking to a wider palette keeps every value.
    {
        PackedStorage narrow(4, 4096);
        for (std::size_t i = 0; i < 4096; ++i) {
            narrow.set(i, static_cast<std::uint32_t>(i % 16));
        }
        const PackedStorage wide = narrow.repacked(5);
        CHECK(wide.bits() == 5U);
        CHECK(wide.size() == 4096U);
        for (std::size_t i = 0; i < 4096; ++i) {
            CHECK(wide.get(i) == i % 16);
        }
    }

    // Errors.
    CHECK(throws([] { PackedStorage(0, 10); }));
    CHECK(throws([] { PackedStorage(33, 10); }));
    CHECK(throws([] { PackedStorage(4, 100, std::vector<std::uint64_t>(3)); }));
    CHECK(throws([] { PackedStorage s(4, 10); (void)s.get(10); }));
    CHECK(throws([] { PackedStorage s(4, 10); s.set(0, 16U); }));
    CHECK(throws([] { PackedStorage s(4, 16); (void)s.repacked(2).get(0); s.set(0, 15U); (void)s.repacked(2); }));

    std::cout << "packed storage tests passed\n";
    return 0;
}
