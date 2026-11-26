#pragma once

// Anvil region files (.mca): up to 32x32 chunks in one file.
//
// Layout:
//   bytes 0..4095     location table, 1024 entries of 4 bytes:
//                     3-byte big-endian sector offset, 1-byte sector count
//   bytes 4096..8191  timestamp table, 1024 big-endian u32 (seconds)
//   sector 2 onward   chunk records, each starting on a 4096-byte boundary:
//                     u32 length (counts the compression byte and the data),
//                     u8 compression type, then length - 1 bytes of data
// Entry index is (x & 31) + (z & 31) * 32. An entry of 0 means "no chunk".
//
// Compression types: 1 gzip, 2 zlib, 3 none. 4 (LZ4) and custom types are
// reported as unsupported. If bit 0x80 is set the data lives in a separate
// c.<x>.<z>.mcc file (chunks over 1 MiB); that is not supported either.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace minecpp {

enum class ZFormat { Zlib, GZip };

std::vector<std::uint8_t> zlib_compress(const std::uint8_t* data, std::size_t size, ZFormat format);

// Throws std::runtime_error on corrupt or truncated input, and when the output
// would exceed `max_output` bytes (protection against decompression bombs).
std::vector<std::uint8_t> zlib_decompress(const std::uint8_t* data, std::size_t size, ZFormat format,
                                          std::size_t max_output);

class RegionFile {
public:
    static constexpr int kChunksPerSide = 32;
    static constexpr std::size_t kSectorSize = 4096;
    static constexpr std::size_t kHeaderSectors = 2;
    static constexpr std::size_t kMaxChunkBytes = 64U * 1024U * 1024U;

    // Parses and validates the header of an in-memory region file. Throws
    // std::runtime_error when the header or any chunk record is inconsistent.
    explicit RegionFile(std::vector<std::uint8_t> data);

    static RegionFile from_file(const std::string& path);

    // Local chunk coordinates are taken modulo 32, so world chunk coordinates
    // can be passed directly.
    bool has_chunk(int x, int z) const;
    std::uint32_t timestamp(int x, int z) const;

    // The chunk's NBT bytes, decompressed; nullopt when the chunk is absent.
    std::optional<std::vector<std::uint8_t>> read_chunk(int x, int z) const;

    // Serialises chunks into a region file, compressing each with zlib. Keys
    // are (x, z) local coordinates, values are (NBT bytes, timestamp).
    struct ChunkData {
        std::vector<std::uint8_t> nbt;
        std::uint32_t timestamp = 0;
    };
    static std::vector<std::uint8_t> build(const std::map<std::pair<int, int>, ChunkData>& chunks);

    const std::vector<std::uint8_t>& bytes() const noexcept { return data_; }

private:
    static std::size_t index_of(int x, int z);

    std::vector<std::uint8_t> data_;
};

}  // namespace minecpp
