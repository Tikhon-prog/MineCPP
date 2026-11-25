#pragma once

#include "nbt.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>

namespace minecpp::nbt {

struct RegionLimits {
    std::size_t maximum_region_bytes = 64U * 1024U * 1024U;
    std::size_t maximum_compressed_chunk_bytes = 16U * 1024U * 1024U;
    std::size_t maximum_decompressed_chunk_bytes = 64U * 1024U * 1024U;
    Limits nbt{};
};

// Reads a chunk at local region coordinates 0..31. Empty location entries return nullopt.
// LZ4 and external .mcc chunk payloads are reported as unsupported, never misread.
[[nodiscard]] std::optional<Document> read_region_chunk(
    const std::filesystem::path& path, int local_x, int local_z,
    const RegionLimits& limits = {});

} // namespace minecpp::nbt
