#include "nbt.hpp"
#include "region_file.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>
#include <zlib.h>

using namespace minecpp::nbt;
#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Test failed: " #expression); } while (false)

namespace {
void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}
void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24U));
    out.push_back(static_cast<std::uint8_t>(value >> 16U));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}
void append_string_tag(std::vector<std::uint8_t>& out, std::string_view name, std::string_view value) {
    out.push_back(static_cast<std::uint8_t>(Type::String));
    append_u16(out, static_cast<std::uint16_t>(name.size()));
    out.insert(out.end(), name.begin(), name.end());
    append_u16(out, static_cast<std::uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}
std::vector<std::uint8_t> sample_nbt() {
    std::vector<std::uint8_t> out;
    out.push_back(static_cast<std::uint8_t>(Type::Compound));
    append_u16(out, 4);
    out.insert(out.end(), {'r','o','o','t'});
    out.push_back(static_cast<std::uint8_t>(Type::Byte));
    append_u16(out, 1); out.push_back('x'); out.push_back(42);
    append_string_tag(out, "name", "cat");
    out.push_back(static_cast<std::uint8_t>(Type::List));
    append_u16(out, 7); out.insert(out.end(), {'n','u','m','b','e','r','s'});
    out.push_back(static_cast<std::uint8_t>(Type::Int));
    append_u32(out, 2); append_u32(out, 5); append_u32(out, 9);
    out.push_back(static_cast<std::uint8_t>(Type::Compound));
    append_u16(out, 4); out.insert(out.end(), {'n','e','s','t'});
    out.push_back(static_cast<std::uint8_t>(Type::Long));
    append_u16(out, 1); out.push_back('v'); append_u32(out, 0); append_u32(out, 1234);
    out.push_back(static_cast<std::uint8_t>(Type::End));
    out.push_back(static_cast<std::uint8_t>(Type::End));
    return out;
}
std::vector<std::uint8_t> zlib_compress(const std::vector<std::uint8_t>& input) {
    uLongf length = compressBound(static_cast<uLong>(input.size()));
    std::vector<std::uint8_t> output(static_cast<std::size_t>(length));
    const int status = compress2(output.data(), &length, input.data(),
                                 static_cast<uLong>(input.size()), Z_BEST_SPEED);
    if (status != Z_OK) throw std::runtime_error("Could not construct test zlib stream");
    output.resize(static_cast<std::size_t>(length));
    return output;
}
}

int main() {
    const auto raw = sample_nbt();
    const Document document = parse(raw);
    CHECK(document.root.name == "root");
    CHECK(document.root.at("x").as_byte() == 42);
    CHECK(document.root.at("name").as_string() == "cat");
    CHECK(document.root.at("numbers").children.size() == 2U);
    CHECK(document.root.at("numbers").children[0].as_int() == 5);
    CHECK(document.root.at("numbers").children[1].as_int() == 9);
    CHECK(document.root.at("nest").at("v").as_long() == 1234);

    const std::vector<std::uint8_t> duplicate_names{
        static_cast<std::uint8_t>(Type::Compound), 0, 0,
        static_cast<std::uint8_t>(Type::Byte), 0, 1, 'x', 1,
        static_cast<std::uint8_t>(Type::Byte), 0, 1, 'x', 2,
        static_cast<std::uint8_t>(Type::End)
    };
    bool duplicate_tag_rejected = false;
    try { (void)parse(duplicate_names); }
    catch (const std::exception&) { duplicate_tag_rejected = true; }
    CHECK(duplicate_tag_rejected);

    Limits shallow;
    shallow.maximum_depth = 1;
    bool depth_rejected = false;
    try { (void)parse(raw, shallow); }
    catch (const std::exception&) { depth_rejected = true; }
    CHECK(depth_rejected);

    const auto compressed = zlib_compress(raw);
    CHECK(decompress(compressed) == raw);
    bool decompression_limit_rejected = false;
    try { (void)decompress(compressed, 2U); }
    catch (const std::exception&) { decompression_limit_rejected = true; }
    CHECK(decompression_limit_rejected);

    // Minimal Anvil region: chunk at (0,0), offset 2, one sector, zlib payload.
    std::vector<std::uint8_t> region(3U * 4096U, 0);
    region[2] = 2;
    region[3] = 1;
    const std::size_t payload_length = compressed.size() + 1U;
    const std::size_t chunk_start = 8192U;
    region[chunk_start] = static_cast<std::uint8_t>(payload_length >> 24U);
    region[chunk_start + 1] = static_cast<std::uint8_t>(payload_length >> 16U);
    region[chunk_start + 2] = static_cast<std::uint8_t>(payload_length >> 8U);
    region[chunk_start + 3] = static_cast<std::uint8_t>(payload_length);
    region[chunk_start + 4] = 2;
    std::copy(compressed.begin(), compressed.end(),
              region.begin() + static_cast<std::ptrdiff_t>(chunk_start + 5U));

    const auto path = std::filesystem::temp_directory_path() / "minecpp-region-tests.mca";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(region.data()),
                     static_cast<std::streamsize>(region.size()));
    }
    const auto chunk = read_region_chunk(path, 0, 0);
    CHECK(chunk.has_value());
    CHECK(chunk->root.at("name").as_string() == "cat");
    CHECK(!read_region_chunk(path, 1, 0).has_value());
    std::filesystem::remove(path);

    std::cout << "MineCPP NBT and region tests passed\n";
    return 0;
}
