#include "anvil_chunk.hpp"

#include "math.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <string_view>
#include <stdexcept>
#include <string>
#include <utility>

namespace minecpp::nbt {
namespace {

const Tag& required(const Tag& tag, std::string_view name) {
    return tag.at(name);
}

int read_integer(const Tag& tag, std::string_view label) {
    if (tag.type == Type::Byte) return tag.as_byte();
    if (tag.type == Type::Short) return tag.as_short();
    if (tag.type == Type::Int) return tag.as_int();
    throw std::runtime_error("Chunk field is not an integer: " + std::string(label));
}

const Tag& find_sections_list(const Tag& data) {
    if (const Tag* modern = data.find("sections")) {
        if (modern->type != Type::List) throw std::runtime_error("Chunk sections must be a list");
        return *modern;
    }
    const Tag& legacy = required(data, "Sections");
    if (legacy.type != Type::List) throw std::runtime_error("Chunk Sections must be a list");
    return legacy;
}

const Tag& state_palette(const Tag& section, const Tag*& packed_data) {
    if (const Tag* block_states = section.find("block_states")) {
        if (block_states->type != Type::Compound)
            throw std::runtime_error("section block_states must be a compound");
        const Tag& palette = required(*block_states, "palette");
        packed_data = block_states->find("data");
        return palette;
    }
    const Tag& palette = required(section, "Palette");
    packed_data = section.find("BlockStates");
    return palette;
}

BlockState parse_state(const Tag& compound) {
    if (compound.type != Type::Compound)
        throw std::runtime_error("Block-state palette entry must be a compound");
    BlockState state;
    state.name = required(compound, "Name").as_string();
    if (state.name.empty() || state.name.size() > 256 ||
        state.name.find(':') == std::string::npos)
        throw std::runtime_error("Block state has an invalid namespaced identifier");

    if (const Tag* properties = compound.find("Properties")) {
        if (properties->type != Type::Compound)
            throw std::runtime_error("Block-state Properties must be a compound");
        for (const Tag& property : properties->children) {
            if (property.type != Type::String)
                throw std::runtime_error("Block-state property values must be strings");
            if (!state.properties.emplace(property.name, property.as_string()).second)
                throw std::runtime_error("Duplicate block-state property");
        }
    }
    return state;
}

void decode_palette_indices(
    ChunkSection& section, const Tag* packed_data, int data_version) {
    if (section.palette.empty() || section.palette.size() > 4096U)
        throw std::runtime_error("Chunk block palette has an invalid size");

    if (section.palette.size() == 1U) {
        section.palette_indices.fill(0);
        if (packed_data && packed_data->as_long_array().size() != 0U) {
            for (const std::int64_t value : packed_data->as_long_array())
                if (value != 0) throw std::runtime_error("Single-state palette has non-zero packed data");
        }
        return;
    }

    // DataVersion 2529 (1.16) introduced the padded-per-long representation.
    // Older continuous bit streams are deliberately not interpreted as padded.
    if (data_version < 2529)
        throw std::runtime_error("Pre-1.16 continuous block-state packing is not supported");

    if (!packed_data || packed_data->type != Type::LongArray)
        throw std::runtime_error("Multi-state block palette is missing its packed long array");

    const unsigned int bits = std::max(
        4U, static_cast<unsigned int>(std::bit_width(section.palette.size() - 1U)));
    const std::size_t per_long = 64U / bits;
    const std::size_t expected_longs = (4096U + per_long - 1U) / per_long;
    const auto& packed = packed_data->as_long_array();
    if (packed.size() != expected_longs)
        throw std::runtime_error("Block-state packed array has an invalid length");

    const std::uint64_t mask = (std::uint64_t{1} << bits) - 1U;
    for (std::size_t index = 0; index < 4096U; ++index) {
        const std::size_t long_index = index / per_long;
        const unsigned int bit_offset =
            static_cast<unsigned int>((index % per_long) * bits);
        const std::uint64_t word = static_cast<std::uint64_t>(packed[long_index]);
        const auto palette_index = static_cast<std::uint16_t>((word >> bit_offset) & mask);
        if (palette_index >= section.palette.size())
            throw std::runtime_error("Block-state packed array references outside its palette");
        section.palette_indices[index] = palette_index;
    }
}

} // namespace

const BlockState& ChunkSection::at(int x, int y, int z) const {
    if (x < 0 || x >= 16 || y < 0 || y >= 16 || z < 0 || z >= 16)
        throw std::out_of_range("Chunk section coordinates must be between 0 and 15");
    const std::size_t index =
        static_cast<std::size_t>(y * 256 + z * 16 + x);
    const std::uint16_t palette_index = palette_indices[index];
    if (palette_index >= palette.size())
        throw std::runtime_error("Chunk section contains an invalid palette index");
    return palette[palette_index];
}

const ChunkSection* AnvilChunk::find_section(int section_y) const noexcept {
    const auto found = std::find_if(sections.begin(), sections.end(),
        [section_y](const ChunkSection& section) { return section.section_y == section_y; });
    return found == sections.end() ? nullptr : &*found;
}

const BlockState* AnvilChunk::block_at(
    int world_x, int world_y, int world_z) const noexcept {
    if (floor_div(world_x, 16) != chunk_x || floor_div(world_z, 16) != chunk_z)
        return nullptr;
    const ChunkSection* section = find_section(floor_div(world_y, 16));
    if (!section) return nullptr;
    const int local_x = positive_mod(world_x, 16);
    const int local_y = positive_mod(world_y, 16);
    const int local_z = positive_mod(world_z, 16);
    const std::size_t index =
        static_cast<std::size_t>(local_y * 256 + local_z * 16 + local_x);
    const std::uint16_t palette_index = section->palette_indices[index];
    if (palette_index >= section->palette.size()) return nullptr;
    return &section->palette[palette_index];
}

std::optional<Block> to_builtin_block(const BlockState& state) noexcept {
    const std::string_view name = state.name;
    if (name == "minecraft:air" || name == "minecraft:cave_air" ||
        name == "minecraft:void_air") return Block::Air;
    if (name == "minecraft:grass_block") return Block::Grass;
    if (name == "minecraft:dirt") return Block::Dirt;
    if (name == "minecraft:stone") return Block::Stone;
    if (name == "minecraft:sand" || name == "minecraft:red_sand") return Block::Sand;
    if (name == "minecraft:bedrock") return Block::Bedrock;
    if (name == "minecraft:oak_log" || name == "minecraft:spruce_log" ||
        name == "minecraft:birch_log" || name == "minecraft:jungle_log" ||
        name == "minecraft:acacia_log" || name == "minecraft:dark_oak_log" ||
        name == "minecraft:mangrove_log" || name == "minecraft:cherry_log" ||
        name == "minecraft:oak_wood" || name == "minecraft:spruce_wood" ||
        name == "minecraft:birch_wood" || name == "minecraft:jungle_wood" ||
        name == "minecraft:acacia_wood" || name == "minecraft:dark_oak_wood" ||
        name == "minecraft:mangrove_wood" || name == "minecraft:cherry_wood")
        return Block::Wood;
    if (name == "minecraft:oak_leaves" || name == "minecraft:spruce_leaves" ||
        name == "minecraft:birch_leaves" || name == "minecraft:jungle_leaves" ||
        name == "minecraft:acacia_leaves" || name == "minecraft:dark_oak_leaves" ||
        name == "minecraft:mangrove_leaves" || name == "minecraft:cherry_leaves" ||
        name == "minecraft:azalea_leaves" ||
        name == "minecraft:flowering_azalea_leaves")
        return Block::Leaves;
    return std::nullopt;
}

AnvilChunk decode_anvil_chunk(
    const Document& document,
    std::optional<int> expected_chunk_x,
    std::optional<int> expected_chunk_z) {
    const Tag& root = document.root;
    if (root.type != Type::Compound)
        throw std::runtime_error("Anvil chunk root must be a compound");

    const Tag* level = root.find("Level");
    const Tag& data = level ? *level : root;
    AnvilChunk result;
    result.data_version = root.find("DataVersion")
        ? root.at("DataVersion").as_int() : 0;
    if (result.data_version == 0)
        throw std::runtime_error("Chunk is missing DataVersion");
    result.chunk_x = read_integer(required(data, "xPos"), "xPos");
    result.chunk_z = read_integer(required(data, "zPos"), "zPos");
    if (expected_chunk_x && result.chunk_x != *expected_chunk_x)
        throw std::runtime_error("Chunk xPos does not match the requested region coordinate");
    if (expected_chunk_z && result.chunk_z != *expected_chunk_z)
        throw std::runtime_error("Chunk zPos does not match the requested region coordinate");

    const Tag& sections = find_sections_list(data);
    result.sections.reserve(sections.children.size());
    for (const Tag& section_tag : sections.children) {
        if (section_tag.type != Type::Compound)
            throw std::runtime_error("Chunk section list contains a non-compound entry");

        ChunkSection section;
        section.section_y = read_integer(required(section_tag, "Y"), "section Y");
        if (section.section_y < -64 || section.section_y > 319)
            throw std::runtime_error("Chunk section Y is outside supported world limits");

        const Tag* packed_data = nullptr;
        const Tag& palette = state_palette(section_tag, packed_data);
        if (palette.type != Type::List)
            throw std::runtime_error("Block-state palette must be a list");
        section.palette.reserve(palette.children.size());
        for (const Tag& entry : palette.children)
            section.palette.push_back(parse_state(entry));
        decode_palette_indices(section, packed_data, result.data_version);

        if (result.find_section(section.section_y))
            throw std::runtime_error("Chunk contains duplicate section Y coordinates");
        result.sections.push_back(std::move(section));
    }

    std::sort(result.sections.begin(), result.sections.end(),
        [](const ChunkSection& a, const ChunkSection& b) {
            return a.section_y < b.section_y;
        });
    return result;
}

} // namespace minecpp::nbt
