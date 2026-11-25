#include "region_file.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace minecpp::nbt {
namespace {

std::uint32_t read_be_u32(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) |
           static_cast<std::uint32_t>(bytes[3]);
}

struct SectorRange { std::uint32_t begin; std::uint32_t end; };

} // namespace

std::optional<Document> read_region_chunk(
    const std::filesystem::path& path, int local_x, int local_z,
    const RegionLimits& limits) {
    if (local_x < 0 || local_x >= 32 || local_z < 0 || local_z >= 32)
        throw std::runtime_error("Region chunk coordinates must be between 0 and 31");
    if (limits.maximum_region_bytes < 8192U ||
        limits.maximum_compressed_chunk_bytes == 0 ||
        limits.maximum_decompressed_chunk_bytes == 0)
        throw std::runtime_error("Invalid region reader limits");

    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status))
        throw std::runtime_error("Region file must be a regular, non-symlink file");
    const std::uintmax_t file_size = std::filesystem::file_size(path);
    if (file_size == 0) return std::nullopt;
    if (file_size < 8192U || file_size > limits.maximum_region_bytes ||
        file_size % 4096U != 0)
        throw std::runtime_error("Region file has an invalid or excessive size");

    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open region file");
    std::array<std::uint8_t, 8192> header{};
    input.read(reinterpret_cast<char*>(header.data()),
               static_cast<std::streamsize>(header.size()));
    if (!input) throw std::runtime_error("Region file header is truncated");

    std::vector<SectorRange> allocations;
    allocations.reserve(1024);
    for (std::size_t index = 0; index < 1024; ++index) {
        const std::uint8_t* entry = header.data() + index * 4U;
        const std::uint32_t offset =
            (static_cast<std::uint32_t>(entry[0]) << 16U) |
            (static_cast<std::uint32_t>(entry[1]) << 8U) |
            static_cast<std::uint32_t>(entry[2]);
        const std::uint32_t count = entry[3];
        if (offset == 0 && count == 0) continue;
        if (offset < 2 || count == 0 ||
            (static_cast<std::uint64_t>(offset) + count) * 4096ULL > file_size)
            throw std::runtime_error("Region file contains an invalid sector allocation");
        allocations.push_back({offset, offset + count});
    }
    std::sort(allocations.begin(), allocations.end(),
              [](const SectorRange& a, const SectorRange& b) { return a.begin < b.begin; });
    for (std::size_t i = 1; i < allocations.size(); ++i)
        if (allocations[i].begin < allocations[i - 1].end)
            throw std::runtime_error("Region file contains overlapping chunk sectors");

    const std::size_t index = static_cast<std::size_t>(local_x + local_z * 32);
    const std::uint8_t* location = header.data() + index * 4U;
    const std::uint32_t sector_offset =
        (static_cast<std::uint32_t>(location[0]) << 16U) |
        (static_cast<std::uint32_t>(location[1]) << 8U) |
        static_cast<std::uint32_t>(location[2]);
    const std::uint32_t sector_count = location[3];
    if (sector_offset == 0 && sector_count == 0) return std::nullopt;

    const std::uint64_t byte_offset = static_cast<std::uint64_t>(sector_offset) * 4096ULL;
    input.seekg(static_cast<std::streamoff>(byte_offset), std::ios::beg);
    std::array<std::uint8_t, 4> length_bytes{};
    input.read(reinterpret_cast<char*>(length_bytes.data()), 4);
    if (!input) throw std::runtime_error("Region chunk length is truncated");
    const std::uint32_t length = read_be_u32(length_bytes.data());
    const std::uint64_t capacity = static_cast<std::uint64_t>(sector_count) * 4096ULL;
    if (length < 1 || length > capacity - 4ULL ||
        length - 1ULL > limits.maximum_compressed_chunk_bytes)
        throw std::runtime_error("Region chunk payload length is invalid or excessive");

    std::vector<std::uint8_t> payload(length);
    input.read(reinterpret_cast<char*>(payload.data()),
               static_cast<std::streamsize>(payload.size()));
    if (!input) throw std::runtime_error("Region chunk payload is truncated");

    const std::uint8_t compression = payload.front();
    if ((compression & 0x80U) != 0)
        throw std::runtime_error("External .mcc chunk payloads are not supported yet");
    const std::span<const std::uint8_t> compressed(payload.data() + 1, payload.size() - 1U);

    std::vector<std::uint8_t> raw;
    if (compression == 1 || compression == 2) {
        raw = decompress(compressed, limits.maximum_decompressed_chunk_bytes);
    } else if (compression == 3) {
        if (compressed.size() > limits.maximum_decompressed_chunk_bytes)
            throw std::runtime_error("Uncompressed region chunk exceeds the output limit");
        raw.assign(compressed.begin(), compressed.end());
    } else {
        throw std::runtime_error("Unsupported region compression type: " +
                                 std::to_string(compression));
    }
    return parse(raw, limits.nbt);
}

} // namespace minecpp::nbt
