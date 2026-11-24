#include "paletted_container.hpp"

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
    constexpr std::size_t kSection = 4096;

    // A uniform container has no index storage.
    {
        auto c = PalettedContainer::uniform(7U, kSection, 4);
        CHECK(c.size() == kSection);
        CHECK(c.storage_bits() == 0U);
        CHECK(c.words().empty());
        CHECK(c.palette().size() == 1U);
        CHECK(c.get(0) == 7U);
        CHECK(c.get(kSection - 1) == 7U);
        // Writing the same value keeps it uniform.
        CHECK(c.set(5, 7U) == 7U);
        CHECK(c.storage_bits() == 0U);
    }

    // First different value creates storage at min_bits.
    {
        auto c = PalettedContainer::uniform(0U, kSection, 4);
        CHECK(c.set(10, 9U) == 0U);
        CHECK(c.storage_bits() == 4U);
        CHECK(c.words().size() == 256U);
        CHECK(c.get(10) == 9U);
        CHECK(c.get(9) == 0U);
        CHECK(c.get(11) == 0U);
        CHECK(c.palette().size() == 2U);
        CHECK(c.set(10, 0U) == 9U);
        CHECK(c.get(10) == 0U);
        // Palette does not shrink.
        CHECK(c.palette().size() == 2U);
    }

    // Palette growth widens the indices; no value is lost.
    {
        auto c = PalettedContainer::uniform(0U, kSection, 4);
        for (std::size_t i = 0; i < 16; ++i) {
            c.set(i, static_cast<PalettedContainer::Value>(i + 100));
        }
        CHECK(c.palette().size() == 17U);
        CHECK(c.storage_bits() == 5U);
        for (std::size_t i = 0; i < 16; ++i) {
            CHECK(c.get(i) == i + 100);
        }
        CHECK(c.get(16) == 0U);
        CHECK(c.get(kSection - 1) == 0U);

        for (std::size_t i = 16; i < 40; ++i) {
            c.set(i, static_cast<PalettedContainer::Value>(i + 100));
        }
        CHECK(c.palette().size() == 41U);
        CHECK(c.storage_bits() == 6U);
        for (std::size_t i = 0; i < 40; ++i) {
            CHECK(c.get(i) == i + 100);
        }
    }

    // A larger min_bits is respected from the start.
    {
        auto c = PalettedContainer::uniform(1U, 64, 8);
        c.set(0, 2U);
        CHECK(c.storage_bits() == 8U);
    }

    // Save and load round trip.
    {
        auto a = PalettedContainer::uniform(0U, kSection, 4);
        for (std::size_t i = 0; i < kSection; i += 3) {
            a.set(i, static_cast<PalettedContainer::Value>(i % 11));
        }
        auto b = PalettedContainer::from_parts(a.palette(), a.words(), a.size(), 4);
        CHECK(b.storage_bits() == a.storage_bits());
        for (std::size_t i = 0; i < kSection; ++i) {
            CHECK(a.get(i) == b.get(i));
        }

        auto u = PalettedContainer::from_parts({3U}, {}, 64, 4);
        CHECK(u.get(63) == 3U);
        CHECK(u.storage_bits() == 0U);
    }

    // Errors.
    CHECK(throws([] { PalettedContainer::uniform(0U, 0, 4); }));
    CHECK(throws([] { PalettedContainer::uniform(0U, 16, 0); }));
    CHECK(throws([] { PalettedContainer::uniform(0U, 16, 33); }));
    CHECK(throws([] { PalettedContainer::from_parts({}, {}, 16, 4); }));
    CHECK(throws([] { PalettedContainer::from_parts({1U}, std::vector<std::uint64_t>(1), 16, 4); }));
    CHECK(throws([] { PalettedContainer::from_parts({1U, 2U}, std::vector<std::uint64_t>(5), 16, 4); }));
    // Index 15 names a palette entry that does not exist.
    CHECK(throws([] { PalettedContainer::from_parts({1U, 2U}, std::vector<std::uint64_t>{0xFULL}, 16, 4); }));
    CHECK(throws([] { auto c = PalettedContainer::uniform(0U, 16, 4); (void)c.get(16); }));
    CHECK(throws([] { auto c = PalettedContainer::uniform(0U, 16, 4); c.set(16, 1U); }));

    std::cout << "paletted container tests passed\n";
    return 0;
}
