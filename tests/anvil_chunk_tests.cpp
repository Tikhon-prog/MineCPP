#include "anvil_chunk.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace minecpp::nbt;
#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Test failed: " #expression); } while (false)

namespace {
void u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}
void u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24U));
    out.push_back(static_cast<std::uint8_t>(value >> 16U));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}
void u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    u32(out, static_cast<std::uint32_t>(value >> 32U));
    u32(out, static_cast<std::uint32_t>(value));
}
void named(std::vector<std::uint8_t>& out, Type type, const char* name) {
    out.push_back(static_cast<std::uint8_t>(type));
    const std::string value(name);
    u16(out, static_cast<std::uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}
void string_payload(std::vector<std::uint8_t>& out, const char* value) {
    const std::string text(value);
    u16(out, static_cast<std::uint16_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
}
std::vector<std::uint8_t> make_chunk(bool two_states) {
    std::vector<std::uint8_t> out;
    out.push_back(static_cast<std::uint8_t>(Type::Compound));
    u16(out, 0); // root name
    named(out, Type::Int, "DataVersion"); u32(out, 3465);
    named(out, Type::Int, "xPos"); u32(out, 3);
    named(out, Type::Int, "zPos"); u32(out, -2);
    named(out, Type::List, "sections");
    out.push_back(static_cast<std::uint8_t>(Type::Compound)); u32(out, 1);
    named(out, Type::Byte, "Y"); out.push_back(0);
    named(out, Type::Compound, "block_states");
    named(out, Type::List, "palette");
    out.push_back(static_cast<std::uint8_t>(Type::Compound)); u32(out, two_states ? 2 : 1);
    named(out, Type::String, "Name"); string_payload(out, "minecraft:air");
    out.push_back(static_cast<std::uint8_t>(Type::End));
    if (two_states) {
        named(out, Type::String, "Name"); string_payload(out, "minecraft:stone");
        out.push_back(static_cast<std::uint8_t>(Type::End));
    }
    if (two_states) {
        named(out, Type::LongArray, "data");
        u32(out, 256);
        for (std::size_t i = 0; i < 256; ++i) u64(out, i == 0 ? (1ULL << 4U) : 0ULL);
    }
    out.push_back(static_cast<std::uint8_t>(Type::End)); // block_states
    out.push_back(static_cast<std::uint8_t>(Type::End)); // section
    out.push_back(static_cast<std::uint8_t>(Type::End)); // root
    return out;
}
}

int main() {
    const auto single_doc = parse(make_chunk(false));
    const auto single = decode_anvil_chunk(single_doc, 3, -2);
    CHECK(single.data_version == 3465);
    CHECK(single.chunk_x == 3 && single.chunk_z == -2);
    CHECK(single.sections.size() == 1U);
    CHECK(single.sections[0].at(15, 15, 15).name == "minecraft:air");

    const auto multi_doc = parse(make_chunk(true));
    const auto multi = decode_anvil_chunk(multi_doc);
    CHECK(multi.sections[0].at(0, 0, 0).name == "minecraft:air");
    CHECK(multi.sections[0].at(1, 0, 0).name == "minecraft:stone");
    CHECK(to_builtin_block(multi.sections[0].at(1, 0, 0)) == minecpp::Block::Stone);
    CHECK(to_builtin_block(BlockState{"minecraft:oak_leaves", {}}) == minecpp::Block::Leaves);
    CHECK(!to_builtin_block(BlockState{"minecraft:diamond_ore", {}}).has_value());
    CHECK(multi.block_at(49, 0, -32)->name == "minecraft:stone");
    CHECK(multi.block_at(49, 0, -31)->name == "minecraft:air");
    CHECK(multi.block_at(65, 0, -32) == nullptr);

    bool mismatch_rejected = false;
    try { (void)decode_anvil_chunk(single_doc, 2, -2); }
    catch (const std::exception&) { mismatch_rejected = true; }
    CHECK(mismatch_rejected);

    std::cout << "MineCPP Anvil chunk tests passed\n";
    return 0;
}
