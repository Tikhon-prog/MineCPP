#pragma once

// Chunk NBT <-> ChunkSection, for the section layout introduced in 1.18:
//
//   xPos, zPos, yPos (lowest section Y), DataVersion, Status,
//   sections: [ { Y: byte,
//                 block_states: { palette: [ {Name, Properties?} ], data?: long[] },
//                 biomes:       { palette: [ "minecraft:plains" ... ], data?: long[] },
//                 BlockLight?, SkyLight? } ]
//
// Only the 1.18-1.21 layout is implemented and it has not yet been checked
// against real worlds of the newest game versions. Check DataVersion before
// trusting a world from a release that changed the format.
//
// Nothing is lost on a round trip: top-level tags other than the ones above
// (block_entities, Heightmaps, structures, ...) are kept in `rest`, and
// per-section tags other than Y/block_states/biomes (light arrays) in `extra`.
// Light is not interpreted here.
//
// Blocks count as air only when their id is 0 (minecraft:air). cave_air and
// void_air are separate states and count as non-air for ChunkSection.

#include "chunk_section.hpp"
#include "nbt.hpp"
#include "registries.hpp"

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace minecpp {

struct LoadedSection {
    ChunkSection section;
    nbt::Compound extra;
};

struct LoadedChunk {
    int x = 0;
    int z = 0;
    int min_section_y = -4;
    int data_version = 0;
    std::string status;
    std::map<int, LoadedSection> sections;  // keyed by section Y
    nbt::Compound rest;
};

namespace detail {

[[noreturn]] inline void chunk_error(const std::string& what) {
    throw std::runtime_error("chunk: " + what);
}

inline const nbt::Compound& compound_of(const nbt::Tag& tag, const char* what) {
    const nbt::Compound* c = tag.get_if<nbt::Compound>();
    if (c == nullptr) {
        chunk_error(std::string(what) + " is not a compound");
    }
    return *c;
}

inline std::int64_t integer_of(const nbt::Tag& tag, const char* what) {
    if (const auto* v = tag.get_if<std::int8_t>()) return *v;
    if (const auto* v = tag.get_if<std::int16_t>()) return *v;
    if (const auto* v = tag.get_if<std::int32_t>()) return *v;
    if (const auto* v = tag.get_if<std::int64_t>()) return *v;
    chunk_error(std::string(what) + " is not an integer");
}

inline const std::string& string_of(const nbt::Tag& tag, const char* what) {
    const std::string* s = tag.get_if<std::string>();
    if (s == nullptr) {
        chunk_error(std::string(what) + " is not a string");
    }
    return *s;
}

inline const nbt::List& list_of(const nbt::Tag& tag, const char* what) {
    const nbt::List* l = tag.get_if<nbt::List>();
    if (l == nullptr) {
        chunk_error(std::string(what) + " is not a list");
    }
    return *l;
}

inline std::vector<std::uint64_t> words_of(const nbt::Compound& container) {
    std::vector<std::uint64_t> words;
    if (const nbt::Tag* data = nbt::find(container, "data")) {
        const auto* longs = data->get_if<std::vector<std::int64_t>>();
        if (longs == nullptr) {
            chunk_error("data is not a long array");
        }
        words.reserve(longs->size());
        for (const std::int64_t v : *longs) {
            words.push_back(static_cast<std::uint64_t>(v));
        }
    }
    return words;
}

inline PalettedContainer parse_block_states(const nbt::Compound& bs, BlockStateRegistry& registry) {
    const nbt::Tag* palette_tag = nbt::find(bs, "palette");
    if (palette_tag == nullptr) {
        chunk_error("block_states has no palette");
    }
    const nbt::List& list = list_of(*palette_tag, "block_states palette");
    if (list.items.empty()) {
        chunk_error("block_states palette is empty");
    }
    std::vector<PalettedContainer::Value> palette;
    palette.reserve(list.items.size());
    for (const nbt::Tag& entry : list.items) {
        const nbt::Compound& c = compound_of(entry, "palette entry");
        const nbt::Tag* name = nbt::find(c, "Name");
        if (name == nullptr) {
            chunk_error("palette entry has no Name");
        }
        BlockState state;
        state.name = string_of(*name, "palette entry Name");
        if (const nbt::Tag* props = nbt::find(c, "Properties")) {
            for (const auto& kv : compound_of(*props, "Properties").entries) {
                state.properties.emplace_back(kv.first, string_of(kv.second, "property value"));
            }
        }
        palette.push_back(registry.intern(std::move(state)));
    }
    std::vector<std::uint64_t> words = words_of(bs);
    if (palette.size() == 1) {
        words.clear();  // a single-entry palette needs no data
    }
    return PalettedContainer::from_parts(std::move(palette), std::move(words), ChunkSection::kBlockCount,
                                         ChunkSection::kBlockMinBits);
}

inline PalettedContainer parse_biomes(const nbt::Compound& biomes, NameRegistry& names) {
    const nbt::Tag* palette_tag = nbt::find(biomes, "palette");
    if (palette_tag == nullptr) {
        chunk_error("biomes has no palette");
    }
    const nbt::List& list = list_of(*palette_tag, "biomes palette");
    if (list.items.empty()) {
        chunk_error("biomes palette is empty");
    }
    std::vector<PalettedContainer::Value> palette;
    palette.reserve(list.items.size());
    for (const nbt::Tag& entry : list.items) {
        palette.push_back(names.intern(string_of(entry, "biome name")));
    }
    std::vector<std::uint64_t> words = words_of(biomes);
    if (palette.size() == 1) {
        words.clear();
    }
    return PalettedContainer::from_parts(std::move(palette), std::move(words), ChunkSection::kBiomeCount,
                                         ChunkSection::kBiomeMinBits);
}

inline nbt::Tag long_array_of(const std::vector<std::uint64_t>& words) {
    std::vector<std::int64_t> longs;
    longs.reserve(words.size());
    for (const std::uint64_t w : words) {
        longs.push_back(static_cast<std::int64_t>(w));
    }
    return nbt::Tag::make<std::vector<std::int64_t>>(std::move(longs));
}

}  // namespace detail

// `root` is the chunk's root compound (as returned by nbt::read_root).
inline LoadedChunk chunk_from_nbt(const nbt::Tag& root, BlockStateRegistry& blocks, NameRegistry& biomes) {
    const nbt::Compound& top = detail::compound_of(root, "root");
    LoadedChunk chunk;

    const nbt::Tag* x = nbt::find(top, "xPos");
    const nbt::Tag* z = nbt::find(top, "zPos");
    if (x == nullptr || z == nullptr) {
        detail::chunk_error("missing xPos or zPos");
    }
    chunk.x = static_cast<int>(detail::integer_of(*x, "xPos"));
    chunk.z = static_cast<int>(detail::integer_of(*z, "zPos"));
    if (const nbt::Tag* y = nbt::find(top, "yPos")) {
        chunk.min_section_y = static_cast<int>(detail::integer_of(*y, "yPos"));
    }
    if (const nbt::Tag* v = nbt::find(top, "DataVersion")) {
        chunk.data_version = static_cast<int>(detail::integer_of(*v, "DataVersion"));
    }
    if (const nbt::Tag* s = nbt::find(top, "Status")) {
        chunk.status = detail::string_of(*s, "Status");
    }

    for (const auto& entry : top.entries) {
        const std::string& key = entry.first;
        if (key == "xPos" || key == "zPos" || key == "yPos" || key == "DataVersion" || key == "Status" ||
            key == "sections") {
            continue;
        }
        nbt::set(chunk.rest, key, entry.second);
    }

    const nbt::Tag* sections = nbt::find(top, "sections");
    if (sections == nullptr) {
        return chunk;
    }
    for (const nbt::Tag& tag : detail::list_of(*sections, "sections").items) {
        const nbt::Compound& sc = detail::compound_of(tag, "section");
        const nbt::Tag* y_tag = nbt::find(sc, "Y");
        if (y_tag == nullptr) {
            detail::chunk_error("section has no Y");
        }
        const int y = static_cast<int>(detail::integer_of(*y_tag, "section Y"));
        if (chunk.sections.count(y) != 0) {
            detail::chunk_error("duplicate section Y " + std::to_string(y));
        }

        PalettedContainer block_states = PalettedContainer::uniform(0, ChunkSection::kBlockCount, ChunkSection::kBlockMinBits);
        if (const nbt::Tag* bs = nbt::find(sc, "block_states")) {
            block_states = detail::parse_block_states(detail::compound_of(*bs, "block_states"), blocks);
        }
        // Missing biomes default to plains; only intern it when it is needed.
        const nbt::Tag* bt = nbt::find(sc, "biomes");
        PalettedContainer biome_states =
            bt != nullptr ? detail::parse_biomes(detail::compound_of(*bt, "biomes"), biomes)
                          : PalettedContainer::uniform(biomes.intern("minecraft:plains"), ChunkSection::kBiomeCount,
                                                       ChunkSection::kBiomeMinBits);

        LoadedSection loaded{ChunkSection::from_containers(std::move(block_states), std::move(biome_states)), {}};
        for (const auto& kv : sc.entries) {
            if (kv.first != "Y" && kv.first != "block_states" && kv.first != "biomes") {
                nbt::set(loaded.extra, kv.first, kv.second);
            }
        }
        chunk.sections.emplace(y, std::move(loaded));
    }
    return chunk;
}

inline nbt::Tag chunk_to_nbt(const LoadedChunk& chunk, const BlockStateRegistry& blocks, const NameRegistry& biomes) {
    nbt::Compound top;
    nbt::set(top, "DataVersion", nbt::Tag::make<std::int32_t>(chunk.data_version));
    nbt::set(top, "xPos", nbt::Tag::make<std::int32_t>(chunk.x));
    nbt::set(top, "yPos", nbt::Tag::make<std::int32_t>(chunk.min_section_y));
    nbt::set(top, "zPos", nbt::Tag::make<std::int32_t>(chunk.z));
    nbt::set(top, "Status", nbt::Tag::make<std::string>(chunk.status));

    nbt::List sections;
    sections.element = nbt::TagType::Compound;
    for (const auto& [y, loaded] : chunk.sections) {
        nbt::Compound sc;
        nbt::set(sc, "Y", nbt::Tag::make<std::int8_t>(static_cast<std::int8_t>(y)));

        const PalettedContainer& bc = loaded.section.blocks();
        nbt::List block_palette;
        block_palette.element = nbt::TagType::Compound;
        for (const PalettedContainer::Value id : bc.palette()) {
            const BlockState& state = blocks.get(id);
            nbt::Compound entry;
            nbt::set(entry, "Name", nbt::Tag::make<std::string>(state.name));
            if (!state.properties.empty()) {
                nbt::Compound props;
                for (const auto& kv : state.properties) {
                    nbt::set(props, kv.first, nbt::Tag::make<std::string>(kv.second));
                }
                nbt::set(entry, "Properties", nbt::Tag::make<nbt::Compound>(std::move(props)));
            }
            block_palette.items.push_back(nbt::Tag::make<nbt::Compound>(std::move(entry)));
        }
        nbt::Compound block_states;
        nbt::set(block_states, "palette", nbt::Tag::make<nbt::List>(std::move(block_palette)));
        if (!bc.words().empty()) {
            nbt::set(block_states, "data", detail::long_array_of(bc.words()));
        }
        nbt::set(sc, "block_states", nbt::Tag::make<nbt::Compound>(std::move(block_states)));

        const PalettedContainer& bio = loaded.section.biomes();
        nbt::List biome_palette;
        biome_palette.element = nbt::TagType::String;
        for (const PalettedContainer::Value id : bio.palette()) {
            biome_palette.items.push_back(nbt::Tag::make<std::string>(biomes.get(id)));
        }
        nbt::Compound biome_compound;
        nbt::set(biome_compound, "palette", nbt::Tag::make<nbt::List>(std::move(biome_palette)));
        if (!bio.words().empty()) {
            nbt::set(biome_compound, "data", detail::long_array_of(bio.words()));
        }
        nbt::set(sc, "biomes", nbt::Tag::make<nbt::Compound>(std::move(biome_compound)));

        for (const auto& kv : loaded.extra.entries) {
            nbt::set(sc, kv.first, kv.second);
        }
        sections.items.push_back(nbt::Tag::make<nbt::Compound>(std::move(sc)));
    }
    nbt::set(top, "sections", nbt::Tag::make<nbt::List>(std::move(sections)));

    for (const auto& kv : chunk.rest.entries) {
        nbt::set(top, kv.first, kv.second);
    }
    return nbt::Tag::make<nbt::Compound>(std::move(top));
}

}  // namespace minecpp
