#include "region.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace minecpp {

namespace {

int window_bits(ZFormat format) {
    return format == ZFormat::GZip ? 31 : 15;
}

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

void put_be32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t v) {
    out[at] = static_cast<std::uint8_t>(v >> 24);
    out[at + 1] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    out[at + 2] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    out[at + 3] = static_cast<std::uint8_t>(v & 0xFF);
}

struct Location {
    std::size_t offset_sectors = 0;
    std::size_t count_sectors = 0;
    bool empty() const { return offset_sectors == 0 && count_sectors == 0; }
};

Location location_at(const std::vector<std::uint8_t>& data, std::size_t index) {
    const std::uint8_t* p = data.data() + index * 4;
    Location loc;
    loc.offset_sectors = (static_cast<std::size_t>(p[0]) << 16) | (static_cast<std::size_t>(p[1]) << 8) | p[2];
    loc.count_sectors = p[3];
    return loc;
}

}  // namespace

std::vector<std::uint8_t> zlib_compress(const std::uint8_t* data, std::size_t size, ZFormat format) {
    if (size > UINT_MAX) {
        throw std::invalid_argument("zlib_compress: input too large");
    }
    z_stream s{};
    if (deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, window_bits(format), 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        throw std::runtime_error("zlib_compress: deflateInit2 failed");
    }
    std::vector<std::uint8_t> out(deflateBound(&s, static_cast<uLong>(size)) + 64);
    s.next_in = const_cast<Bytef*>(data);
    s.avail_in = static_cast<uInt>(size);
    s.next_out = out.data();
    s.avail_out = static_cast<uInt>(out.size());
    const int rc = deflate(&s, Z_FINISH);
    const auto produced = static_cast<std::size_t>(s.total_out);
    deflateEnd(&s);
    if (rc != Z_STREAM_END) {
        throw std::runtime_error("zlib_compress: deflate failed");
    }
    out.resize(produced);
    return out;
}

std::vector<std::uint8_t> zlib_decompress(const std::uint8_t* data, std::size_t size, ZFormat format,
                                          std::size_t max_output) {
    if (size > UINT_MAX) {
        throw std::runtime_error("zlib_decompress: input too large");
    }
    z_stream s{};
    if (inflateInit2(&s, window_bits(format)) != Z_OK) {
        throw std::runtime_error("zlib_decompress: inflateInit2 failed");
    }
    s.next_in = const_cast<Bytef*>(data);
    s.avail_in = static_cast<uInt>(size);

    std::vector<std::uint8_t> out;
    std::array<std::uint8_t, 16384> buffer;
    int rc = Z_OK;
    do {
        s.next_out = buffer.data();
        s.avail_out = static_cast<uInt>(buffer.size());
        rc = inflate(&s, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) {
            inflateEnd(&s);
            throw std::runtime_error("zlib_decompress: corrupt or truncated data");
        }
        const std::size_t produced = buffer.size() - s.avail_out;
        if (out.size() + produced > max_output) {
            inflateEnd(&s);
            throw std::runtime_error("zlib_decompress: output exceeds limit");
        }
        out.insert(out.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(produced));
    } while (rc != Z_STREAM_END);
    inflateEnd(&s);
    return out;
}

std::size_t RegionFile::index_of(int x, int z) {
    return static_cast<std::size_t>(x & 31) + static_cast<std::size_t>(z & 31) * kChunksPerSide;
}

RegionFile::RegionFile(std::vector<std::uint8_t> data) : data_(std::move(data)) {
    const std::size_t header = kHeaderSectors * kSectorSize;
    if (data_.size() < header) {
        throw std::runtime_error("region: file shorter than its header");
    }
    if (data_.size() % kSectorSize != 0) {
        throw std::runtime_error("region: size is not a multiple of 4096");
    }
    const std::size_t total_sectors = data_.size() / kSectorSize;

    for (std::size_t i = 0; i < 1024; ++i) {
        const Location loc = location_at(data_, i);
        if (loc.empty()) {
            continue;
        }
        if (loc.offset_sectors < kHeaderSectors || loc.count_sectors == 0) {
            throw std::runtime_error("region: invalid chunk location");
        }
        if (loc.offset_sectors + loc.count_sectors > total_sectors) {
            throw std::runtime_error("region: chunk lies beyond the end of the file");
        }
        const std::size_t start = loc.offset_sectors * kSectorSize;
        const std::uint32_t length = be32(data_.data() + start);
        if (length == 0 || static_cast<std::size_t>(length) > loc.count_sectors * kSectorSize - 4) {
            throw std::runtime_error("region: chunk length does not fit its sectors");
        }
    }
}

RegionFile RegionFile::from_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("region: cannot open " + path);
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return RegionFile(std::move(bytes));
}

bool RegionFile::has_chunk(int x, int z) const {
    return !location_at(data_, index_of(x, z)).empty();
}

std::uint32_t RegionFile::timestamp(int x, int z) const {
    return be32(data_.data() + kSectorSize + index_of(x, z) * 4);
}

std::optional<std::vector<std::uint8_t>> RegionFile::read_chunk(int x, int z) const {
    const Location loc = location_at(data_, index_of(x, z));
    if (loc.empty()) {
        return std::nullopt;
    }
    const std::uint8_t* record = data_.data() + loc.offset_sectors * kSectorSize;
    const std::uint32_t length = be32(record);  // validated in the constructor
    const std::uint8_t type = record[4];
    const std::uint8_t* payload = record + 5;
    const std::size_t payload_size = static_cast<std::size_t>(length) - 1;

    if ((type & 0x80) != 0) {
        throw std::runtime_error("region: chunk stored in an external .mcc file (unsupported)");
    }
    switch (type) {
        case 1:
            return zlib_decompress(payload, payload_size, ZFormat::GZip, kMaxChunkBytes);
        case 2:
            return zlib_decompress(payload, payload_size, ZFormat::Zlib, kMaxChunkBytes);
        case 3:
            return std::vector<std::uint8_t>(payload, payload + payload_size);
        case 4:
            throw std::runtime_error("region: LZ4 compression (unsupported)");
        default:
            throw std::runtime_error("region: unknown compression type");
    }
}

std::vector<std::uint8_t> RegionFile::build(const std::map<std::pair<int, int>, ChunkData>& chunks) {
    std::vector<std::uint8_t> out(kHeaderSectors * kSectorSize, 0);
    std::size_t next_sector = kHeaderSectors;

    // std::map iterates in (x, z) order; sort by file index for a stable layout.
    std::vector<std::pair<std::size_t, const ChunkData*>> ordered;
    for (const auto& [key, chunk] : chunks) {
        if (key.first < 0 || key.first >= kChunksPerSide || key.second < 0 || key.second >= kChunksPerSide) {
            throw std::invalid_argument("region: chunk coordinates must be 0..31");
        }
        ordered.emplace_back(index_of(key.first, key.second), &chunk);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [index, chunk] : ordered) {
        const std::vector<std::uint8_t> packed = zlib_compress(chunk->nbt.data(), chunk->nbt.size(), ZFormat::Zlib);
        const std::size_t record_bytes = 4 + 1 + packed.size();
        const std::size_t sectors = (record_bytes + kSectorSize - 1) / kSectorSize;
        if (sectors > 255) {
            throw std::invalid_argument("region: chunk needs more than 255 sectors (.mcc not supported)");
        }

        const std::size_t start = next_sector * kSectorSize;
        out.resize(start + sectors * kSectorSize, 0);
        put_be32(out, start, static_cast<std::uint32_t>(packed.size() + 1));
        out[start + 4] = 2;
        std::memcpy(out.data() + start + 5, packed.data(), packed.size());

        const std::size_t loc = index * 4;
        out[loc] = static_cast<std::uint8_t>((next_sector >> 16) & 0xFF);
        out[loc + 1] = static_cast<std::uint8_t>((next_sector >> 8) & 0xFF);
        out[loc + 2] = static_cast<std::uint8_t>(next_sector & 0xFF);
        out[loc + 3] = static_cast<std::uint8_t>(sectors);
        put_be32(out, kSectorSize + index * 4, chunk->timestamp);

        next_sector += sectors;
    }
    return out;
}

}  // namespace minecpp
