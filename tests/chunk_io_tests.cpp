#include "chunk_io.hpp"
#include "nbt.hpp"
#include "packed_storage.hpp"
#include "region.hpp"

#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
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

using Props = std::vector<std::pair<std::string, std::string>>;

static nbt::Tag str(const std::string& s) { return nbt::Tag::make<std::string>(s); }

static nbt::Tag palette_entry(const std::string& name, const Props& props = {}) {
    nbt::Compound c;
    nbt::set(c, "Name", str(name));
    if (!props.empty()) {
        nbt::Compound p;
        for (const auto& kv : props) {
            nbt::set(p, kv.first, str(kv.second));
        }
        nbt::set(c, "Properties", nbt::Tag::make<nbt::Compound>(p));
    }
    return nbt::Tag::make<nbt::Compound>(c);
}

static nbt::Tag list_of(nbt::TagType element, std::vector<nbt::Tag> items) {
    nbt::List l;
    l.element = element;
    l.items = std::move(items);
    return nbt::Tag::make<nbt::List>(std::move(l));
}

static nbt::Tag longs(const PackedStorage& s) {
    std::vector<std::int64_t> v;
    for (const std::uint64_t w : s.words()) {
        v.push_back(static_cast<std::int64_t>(w));
    }
    return nbt::Tag::make<std::vector<std::int64_t>>(v);
}

static nbt::Tag block_states(std::vector<nbt::Tag> palette, const PackedStorage* data) {
    nbt::Compound c;
    nbt::set(c, "palette", list_of(nbt::TagType::Compound, std::move(palette)));
    if (data != nullptr) {
        nbt::set(c, "data", longs(*data));
    }
    return nbt::Tag::make<nbt::Compound>(c);
}

static nbt::Tag biomes_tag(std::vector<std::string> names, const PackedStorage* data) {
    std::vector<nbt::Tag> items;
    for (const auto& n : names) {
        items.push_back(str(n));
    }
    nbt::Compound c;
    nbt::set(c, "palette", list_of(nbt::TagType::String, std::move(items)));
    if (data != nullptr) {
        nbt::set(c, "data", longs(*data));
    }
    return nbt::Tag::make<nbt::Compound>(c);
}

static nbt::Tag section(int y, nbt::Tag bs, nbt::Tag bio) {
    nbt::Compound c;
    nbt::set(c, "Y", nbt::Tag::make<std::int8_t>(static_cast<std::int8_t>(y)));
    nbt::set(c, "block_states", std::move(bs));
    nbt::set(c, "biomes", std::move(bio));
    return nbt::Tag::make<nbt::Compound>(c);
}

static nbt::Compound base_chunk() {
    nbt::Compound top;
    nbt::set(top, "DataVersion", nbt::Tag::make<std::int32_t>(3700));
    nbt::set(top, "xPos", nbt::Tag::make<std::int32_t>(3));
    nbt::set(top, "yPos", nbt::Tag::make<std::int32_t>(-4));
    nbt::set(top, "zPos", nbt::Tag::make<std::int32_t>(-5));
    nbt::set(top, "Status", str("minecraft:full"));
    return top;
}

static std::string state_key(const BlockStateRegistry& reg, ChunkSection::BlockId id) {
    return BlockStateRegistry::key_of(reg.get(id));
}

int main() {
    // Registries.
    {
        BlockStateRegistry reg;
        CHECK(reg.get(0).name == "minecraft:air");
        const auto a = reg.intern({"minecraft:oak_log", {{"axis", "y"}}});
        const auto b = reg.intern({"minecraft:oak_log", {{"axis", "x"}}});
        CHECK(a != b);
        // Property order does not matter.
        const auto c = reg.intern({"minecraft:lever", {{"powered", "true"}, {"face", "wall"}}});
        const auto d = reg.intern({"minecraft:lever", {{"face", "wall"}, {"powered", "true"}}});
        CHECK(c == d);
        CHECK(BlockStateRegistry::key_of(reg.get(c)) == "minecraft:lever[face=wall,powered=true]");
        CHECK(reg.intern({"minecraft:air", {}}) == 0U);
        CHECK(throws([&] { (void)reg.get(999); }));

        NameRegistry names;
        CHECK(names.intern("minecraft:plains") == 0U);
        CHECK(names.intern("minecraft:desert") == 1U);
        CHECK(names.intern("minecraft:plains") == 0U);
        CHECK(names.get(1) == "minecraft:desert");
    }

    // A hand-built 1.18-style chunk.
    nbt::Compound top = base_chunk();
    {
        PackedStorage idx(4, 4096);
        idx.set(static_cast<std::size_t>(ChunkSection::block_index(0, 0, 0)), 1);
        idx.set(static_cast<std::size_t>(ChunkSection::block_index(1, 0, 0)), 2);
        idx.set(static_cast<std::size_t>(ChunkSection::block_index(0, 5, 0)), 2);
        idx.set(static_cast<std::size_t>(ChunkSection::block_index(15, 15, 15)), 1);

        PackedStorage bio(1, 64);
        bio.set(static_cast<std::size_t>(ChunkSection::biome_index(1, 2, 3)), 1);

        nbt::Tag low = section(
            -4,
            block_states({palette_entry("minecraft:air"), palette_entry("minecraft:stone"),
                          palette_entry("minecraft:oak_log", {{"axis", "y"}})},
                         &idx),
            biomes_tag({"minecraft:plains", "minecraft:desert"}, &bio));
        nbt::set(low.as<nbt::Compound>(), "BlockLight",
                 nbt::Tag::make<std::vector<std::int8_t>>(std::vector<std::int8_t>{1, 2, 3}));

        nbt::Tag solid = section(0, block_states({palette_entry("minecraft:stone")}, nullptr),
                                 biomes_tag({"minecraft:plains"}, nullptr));

        nbt::Compound no_blocks;  // a section with neither block_states nor biomes
        nbt::set(no_blocks, "Y", nbt::Tag::make<std::int8_t>(1));

        nbt::set(top, "sections", list_of(nbt::TagType::Compound,
                                          {low, solid, nbt::Tag::make<nbt::Compound>(no_blocks)}));

        nbt::Compound heightmaps;
        nbt::set(heightmaps, "MOTION_BLOCKING", nbt::Tag::make<std::vector<std::int64_t>>(std::vector<std::int64_t>{7, 8}));
        nbt::set(top, "Heightmaps", nbt::Tag::make<nbt::Compound>(heightmaps));
        nbt::set(top, "block_entities", list_of(nbt::TagType::End, {}));
    }
    const nbt::Tag root = nbt::Tag::make<nbt::Compound>(top);

    BlockStateRegistry blocks;
    NameRegistry biomes;
    const LoadedChunk chunk = chunk_from_nbt(root, blocks, biomes);
    {
        CHECK(chunk.x == 3 && chunk.z == -5);
        CHECK(chunk.min_section_y == -4 && chunk.data_version == 3700);
        CHECK(chunk.status == "minecraft:full");
        CHECK(chunk.sections.size() == 3U);

        const ChunkSection& low = chunk.sections.at(-4).section;
        CHECK(state_key(blocks, low.get_block(0, 0, 0)) == "minecraft:stone");
        CHECK(state_key(blocks, low.get_block(1, 0, 0)) == "minecraft:oak_log[axis=y]");
        CHECK(state_key(blocks, low.get_block(0, 5, 0)) == "minecraft:oak_log[axis=y]");
        CHECK(state_key(blocks, low.get_block(15, 15, 15)) == "minecraft:stone");
        CHECK(state_key(blocks, low.get_block(2, 0, 0)) == "minecraft:air");
        CHECK(low.non_air_count() == 4);
        CHECK(biomes.get(low.get_biome_cell(1, 2, 3)) == "minecraft:desert");
        CHECK(biomes.get(low.get_biome_cell(0, 0, 0)) == "minecraft:plains");

        const ChunkSection& solid = chunk.sections.at(0).section;
        CHECK(solid.non_air_count() == 4096);
        CHECK(solid.blocks().storage_bits() == 0U);

        const ChunkSection& bare = chunk.sections.at(1).section;
        CHECK(bare.is_empty());
        CHECK(biomes.get(bare.get_biome_cell(0, 0, 0)) == "minecraft:plains");

        // Light and other unknown tags are kept, not interpreted.
        CHECK(nbt::find(chunk.sections.at(-4).extra, "BlockLight") != nullptr);
        CHECK(nbt::find(chunk.sections.at(-4).extra, "Y") == nullptr);
        CHECK(nbt::find(chunk.rest, "Heightmaps") != nullptr);
        CHECK(nbt::find(chunk.rest, "block_entities") != nullptr);
        CHECK(nbt::find(chunk.rest, "xPos") == nullptr);
    }

    // Write it back, re-read through bytes, and compare everything.
    {
        const nbt::Tag out = chunk_to_nbt(chunk, blocks, biomes);
        const std::vector<std::uint8_t> bytes = nbt::write_root("", out);
        const nbt::Root reread = nbt::read_root(bytes);

        BlockStateRegistry blocks2;
        NameRegistry biomes2;
        const LoadedChunk again = chunk_from_nbt(reread.tag, blocks2, biomes2);
        CHECK(again.x == chunk.x && again.z == chunk.z && again.data_version == chunk.data_version);
        CHECK(again.status == chunk.status && again.min_section_y == chunk.min_section_y);
        CHECK(again.sections.size() == chunk.sections.size());
        for (const auto& [y, a] : chunk.sections) {
            const ChunkSection& sa = a.section;
            const ChunkSection& sb = again.sections.at(y).section;
            CHECK(sa.non_air_count() == sb.non_air_count());
            for (int by = 0; by < 16; ++by) {
                for (int bz = 0; bz < 16; ++bz) {
                    for (int bx = 0; bx < 16; ++bx) {
                        CHECK(state_key(blocks, sa.get_block(bx, by, bz)) == state_key(blocks2, sb.get_block(bx, by, bz)));
                    }
                }
            }
            for (int cy = 0; cy < 4; ++cy) {
                for (int cz = 0; cz < 4; ++cz) {
                    for (int cx = 0; cx < 4; ++cx) {
                        CHECK(biomes.get(sa.get_biome_cell(cx, cy, cz)) == biomes2.get(sb.get_biome_cell(cx, cy, cz)));
                    }
                }
            }
            CHECK(a.extra.entries.size() == again.sections.at(y).extra.entries.size());
        }
        CHECK(nbt::find(again.rest, "Heightmaps") != nullptr);
    }

    // Full pipeline: chunk -> NBT -> region file -> NBT -> chunk.
    {
        const std::vector<std::uint8_t> bytes = nbt::write_root("", chunk_to_nbt(chunk, blocks, biomes));
        std::map<std::pair<int, int>, RegionFile::ChunkData> chunks;
        chunks[{chunk.x & 31, chunk.z & 31}] = {bytes, 42U};
        const RegionFile region{RegionFile::build(chunks)};
        const auto stored = region.read_chunk(chunk.x, chunk.z);
        CHECK(stored.has_value());
        BlockStateRegistry blocks3;
        NameRegistry biomes3;
        const LoadedChunk loaded = chunk_from_nbt(nbt::read_root(*stored).tag, blocks3, biomes3);
        CHECK(loaded.x == 3 && loaded.z == -5);
        CHECK(state_key(blocks3, loaded.sections.at(-4).section.get_block(1, 0, 0)) == "minecraft:oak_log[axis=y]");
        CHECK(region.timestamp(chunk.x, chunk.z) == 42U);
    }

    // Edited sections are written back with the right palette and data.
    {
        LoadedChunk edited = chunk;
        BlockStateRegistry b = blocks;
        NameRegistry n = biomes;
        const auto dirt = b.intern({"minecraft:dirt", {}});
        edited.sections.at(1).section.set_block(4, 4, 4, dirt);
        const nbt::Tag out = chunk_to_nbt(edited, b, n);
        BlockStateRegistry b2;
        NameRegistry n2;
        const LoadedChunk back = chunk_from_nbt(out, b2, n2);
        const ChunkSection& s = back.sections.at(1).section;
        CHECK(s.non_air_count() == 1);
        CHECK(state_key(b2, s.get_block(4, 4, 4)) == "minecraft:dirt");
        CHECK(state_key(b2, s.get_block(4, 4, 5)) == "minecraft:air");
    }

    // Malformed chunks are rejected with an error, never misread.
    {
        // Missing position.
        nbt::Compound no_pos = base_chunk();
        no_pos.entries.erase(no_pos.entries.begin() + 1);  // xPos
        CHECK(throws([&] {
            BlockStateRegistry r;
            NameRegistry nm;
            chunk_from_nbt(nbt::Tag::make<nbt::Compound>(no_pos), r, nm);
        }));

        const auto load_with_section = [&](nbt::Tag sec) {
            nbt::Compound t = base_chunk();
            nbt::set(t, "sections", list_of(nbt::TagType::Compound, {std::move(sec)}));
            BlockStateRegistry r;
            NameRegistry nm;
            return chunk_from_nbt(nbt::Tag::make<nbt::Compound>(t), r, nm);
        };
        const nbt::Tag plains = biomes_tag({"minecraft:plains"}, nullptr);

        // Palette index outside the palette: palette has 2 entries, data says 5.
        PackedStorage bad_index(4, 4096);
        bad_index.set(10, 5);
        CHECK(throws([&] {
            load_with_section(section(0, block_states({palette_entry("minecraft:air"), palette_entry("minecraft:stone")}, &bad_index), plains));
        }));

        // Wrong number of data words for the palette size.
        PackedStorage short_data(4, 64);
        CHECK(throws([&] {
            load_with_section(section(0, block_states({palette_entry("minecraft:air"), palette_entry("minecraft:stone")}, &short_data), plains));
        }));

        // Empty palette, no palette, non-string property.
        CHECK(throws([&] { load_with_section(section(0, block_states({}, nullptr), plains)); }));
        CHECK(throws([&] {
            nbt::Compound empty;
            load_with_section(section(0, nbt::Tag::make<nbt::Compound>(empty), plains));
        }));
        CHECK(throws([&] {
            nbt::Compound entry;
            nbt::set(entry, "Name", str("minecraft:lever"));
            nbt::Compound props;
            nbt::set(props, "powered", nbt::Tag::make<std::int8_t>(1));
            nbt::set(entry, "Properties", nbt::Tag::make<nbt::Compound>(props));
            load_with_section(section(0, block_states({nbt::Tag::make<nbt::Compound>(entry)}, nullptr), plains));
        }));

        // Duplicate section Y.
        CHECK(throws([&] {
            nbt::Compound t = base_chunk();
            const nbt::Tag s = section(0, block_states({palette_entry("minecraft:air")}, nullptr), plains);
            nbt::set(t, "sections", list_of(nbt::TagType::Compound, {s, s}));
            BlockStateRegistry r;
            NameRegistry nm;
            chunk_from_nbt(nbt::Tag::make<nbt::Compound>(t), r, nm);
        }));

        // Root that is not a compound.
        CHECK(throws([&] {
            BlockStateRegistry r;
            NameRegistry nm;
            chunk_from_nbt(nbt::Tag::make<std::int32_t>(1), r, nm);
        }));
    }

    std::cout << "chunk io tests passed\n";
    return 0;
}
