#include "chunk_section.hpp"

#include <iostream>
#include <stdexcept>

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
    // Vanilla YZX index order.
    CHECK(ChunkSection::block_index(0, 0, 0) == 0);
    CHECK(ChunkSection::block_index(1, 0, 0) == 1);
    CHECK(ChunkSection::block_index(0, 0, 1) == 16);
    CHECK(ChunkSection::block_index(0, 1, 0) == 256);
    CHECK(ChunkSection::block_index(15, 15, 15) == 4095);
    CHECK(ChunkSection::biome_index(3, 3, 3) == 63);
    CHECK(ChunkSection::biome_index(0, 1, 0) == 16);

    // Empty section.
    {
        ChunkSection s;
        CHECK(s.is_empty());
        CHECK(s.non_air_count() == 0);
        CHECK(s.get_block(5, 5, 5) == 0U);
        CHECK(s.blocks().storage_bits() == 0U);
    }

    // Counting non-air blocks across set/replace/clear.
    {
        ChunkSection s;
        CHECK(s.set_block(1, 2, 3, 5U) == 0U);
        CHECK(s.non_air_count() == 1);
        CHECK(s.set_block(1, 2, 3, 6U) == 5U);  // replace, still one
        CHECK(s.non_air_count() == 1);
        CHECK(s.get_block(1, 2, 3) == 6U);
        CHECK(s.get_block(1, 2, 2) == 0U);
        CHECK(s.set_block(1, 2, 3, 0U) == 6U);
        CHECK(s.non_air_count() == 0);
        CHECK(s.is_empty());
        CHECK(s.blocks().storage_bits() == 4U);  // storage stays once created
    }

    // Filled with stone.
    {
        ChunkSection s(1U);
        CHECK(s.non_air_count() == 4096);
        s.set_block(0, 0, 0, 0U);
        CHECK(s.non_air_count() == 4095);
    }

    // Biome cells cover 4x4x4 blocks.
    {
        ChunkSection s;
        s.set_biome_cell(1, 2, 3, 7U);
        CHECK(s.get_biome_cell(1, 2, 3) == 7U);
        CHECK(s.get_biome_at_block(4, 8, 12) == 7U);
        CHECK(s.get_biome_at_block(7, 11, 15) == 7U);
        CHECK(s.get_biome_at_block(8, 8, 12) == 0U);
        CHECK(s.biomes().storage_bits() == 1U);
    }

    // Loading from containers recounts non-air blocks.
    {
        auto blocks = PalettedContainer::uniform(0U, 4096, 4);
        blocks.set(0, 3U);
        blocks.set(100, 3U);
        auto biomes = PalettedContainer::uniform(0U, 64, 1);
        const ChunkSection s = ChunkSection::from_containers(blocks, biomes);
        CHECK(s.non_air_count() == 2);
        CHECK(s.get_block(4, 0, 6) == 3U);  // index 100 = x 4, y 0, z 6
    }

    // Errors.
    CHECK(throws([] { ChunkSection s; (void)s.get_block(16, 0, 0); }));
    CHECK(throws([] { ChunkSection s; (void)s.get_block(0, -1, 0); }));
    CHECK(throws([] { ChunkSection s; s.set_block(0, 0, 16, 1U); }));
    CHECK(throws([] { ChunkSection s; (void)s.get_biome_cell(4, 0, 0); }));
    CHECK(throws([] {
        ChunkSection::from_containers(PalettedContainer::uniform(0U, 10, 4), PalettedContainer::uniform(0U, 64, 1));
    }));

    std::cout << "chunk section tests passed\n";
    return 0;
}
