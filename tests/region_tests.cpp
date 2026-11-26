#include "nbt.hpp"
#include "region.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
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

using Bytes = std::vector<std::uint8_t>;

static Bytes small_chunk(std::int32_t marker) {
    nbt::Compound c;
    nbt::set(c, "xPos", nbt::Tag::make<std::int32_t>(marker));
    nbt::set(c, "Status", nbt::Tag::make<std::string>("minecraft:full"));
    return nbt::write_root("", nbt::Tag::make<nbt::Compound>(c));
}

// Incompressible bytes, so the chunk needs several sectors.
static Bytes noisy_chunk(std::size_t n) {
    std::vector<std::int8_t> noise(n);
    std::uint32_t state = 12345U;
    for (auto& b : noise) {
        state = state * 1664525U + 1013904223U;
        b = static_cast<std::int8_t>(state >> 24);
    }
    nbt::Compound c;
    nbt::set(c, "noise", nbt::Tag::make<std::vector<std::int8_t>>(noise));
    return nbt::write_root("", nbt::Tag::make<nbt::Compound>(c));
}

static void put32(Bytes& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 24);
    b[at + 1] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    b[at + 2] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    b[at + 3] = static_cast<std::uint8_t>(v & 0xFF);
}

int main() {
    // zlib / gzip round trips and limits.
    {
        const Bytes data = small_chunk(1);
        for (const ZFormat f : {ZFormat::Zlib, ZFormat::GZip}) {
            const Bytes packed = zlib_compress(data.data(), data.size(), f);
            CHECK(zlib_decompress(packed.data(), packed.size(), f, 1U << 20) == data);
        }
        // gzip starts with 1F 8B, zlib with 78.
        const Bytes gz = zlib_compress(data.data(), data.size(), ZFormat::GZip);
        CHECK(gz[0] == 0x1F && gz[1] == 0x8B);
        const Bytes zl = zlib_compress(data.data(), data.size(), ZFormat::Zlib);
        CHECK(zl[0] == 0x78);
        // The wrong format is rejected.
        CHECK(throws([&] { zlib_decompress(gz.data(), gz.size(), ZFormat::Zlib, 1U << 20); }));
        // Truncated and garbage input.
        CHECK(throws([&] { zlib_decompress(zl.data(), zl.size() - 3, ZFormat::Zlib, 1U << 20); }));
        const Bytes junk = {1, 2, 3, 4, 5, 6, 7, 8};
        CHECK(throws([&] { zlib_decompress(junk.data(), junk.size(), ZFormat::Zlib, 1U << 20); }));
        // Decompression bomb: 8 MiB of zeros packs tiny and is refused at 1 MiB.
        const Bytes zeros(8U << 20, 0);
        const Bytes bomb = zlib_compress(zeros.data(), zeros.size(), ZFormat::Zlib);
        CHECK(bomb.size() < 20000);
        CHECK(throws([&] { zlib_decompress(bomb.data(), bomb.size(), ZFormat::Zlib, 1U << 20); }));
        CHECK(zlib_decompress(bomb.data(), bomb.size(), ZFormat::Zlib, 16U << 20).size() == zeros.size());
    }

    // Build and parse a region.
    std::map<std::pair<int, int>, RegionFile::ChunkData> chunks;
    chunks[{0, 0}] = {small_chunk(100), 1111U};
    chunks[{5, 7}] = {small_chunk(200), 2222U};
    chunks[{31, 31}] = {small_chunk(300), 3333U};
    const Bytes file = RegionFile::build(chunks);
    {
        CHECK(file.size() % RegionFile::kSectorSize == 0);
        CHECK(file.size() == 5 * RegionFile::kSectorSize);  // header (2) + three single-sector chunks

        // Location of chunk (0,0): sector 2, one sector. (5,7) is index 229, (31,31) index 1023.
        CHECK(file[0] == 0 && file[1] == 0 && file[2] == 2 && file[3] == 1);
        CHECK(file[229 * 4 + 2] == 3 && file[229 * 4 + 3] == 1);
        CHECK(file[1023 * 4 + 2] == 4 && file[1023 * 4 + 3] == 1);

        const RegionFile region{Bytes(file)};
        CHECK(region.has_chunk(0, 0) && region.has_chunk(5, 7) && region.has_chunk(31, 31));
        CHECK(!region.has_chunk(1, 0));
        CHECK(!region.read_chunk(1, 0).has_value());
        CHECK(region.timestamp(5, 7) == 2222U);
        CHECK(region.timestamp(1, 0) == 0U);

        CHECK(*region.read_chunk(0, 0) == small_chunk(100));
        CHECK(*region.read_chunk(5, 7) == small_chunk(200));
        CHECK(*region.read_chunk(31, 31) == small_chunk(300));

        // World chunk coordinates wrap into the region, negatives included.
        CHECK(region.has_chunk(32, 32));
        CHECK(region.has_chunk(-1, -1));
        CHECK(*region.read_chunk(-1, -1) == small_chunk(300));
        CHECK(*region.read_chunk(37, -25) == small_chunk(200));  // 37 & 31 = 5, -25 & 31 = 7

        // The decoded bytes parse as NBT.
        const nbt::Root root = nbt::read_root(*region.read_chunk(5, 7));
        CHECK(nbt::find(root.tag.as<nbt::Compound>(), "xPos")->as<std::int32_t>() == 200);
    }

    // A chunk bigger than one sector spans several, and the next chunk follows it.
    {
        std::map<std::pair<int, int>, RegionFile::ChunkData> big;
        const Bytes noisy = noisy_chunk(20000);
        big[{0, 0}] = {noisy, 1U};
        big[{1, 0}] = {small_chunk(7), 2U};
        const Bytes f = RegionFile::build(big);
        CHECK(f[3] >= 2);  // 20000 bytes of noise do not fit one sector
        const std::size_t first_sectors = f[3];
        CHECK(f[4 + 2] == 2 + first_sectors);  // chunk (1,0) starts right after
        const RegionFile region{Bytes(f)};
        CHECK(*region.read_chunk(0, 0) == noisy);
        CHECK(*region.read_chunk(1, 0) == small_chunk(7));
    }

    // Empty region and build errors.
    {
        const Bytes empty = RegionFile::build({});
        CHECK(empty.size() == 2 * RegionFile::kSectorSize);
        const RegionFile region{Bytes(empty)};
        CHECK(!region.has_chunk(0, 0));

        std::map<std::pair<int, int>, RegionFile::ChunkData> bad;
        bad[{32, 0}] = {small_chunk(1), 0U};
        CHECK(throws([&] { RegionFile::build(bad); }));
        std::map<std::pair<int, int>, RegionFile::ChunkData> too_big;
        too_big[{0, 0}] = {noisy_chunk(1200000), 0U};  // over 255 sectors even compressed
        CHECK(throws([&] { RegionFile::build(too_big); }));
    }

    // Chunk (0,0) of `file` starts at byte 8192: length, then the compression byte.
    const std::size_t record = 2 * RegionFile::kSectorSize;

    // Compression types.
    {
        // Type 3 (none), crafted by hand.
        Bytes raw = file;
        const Bytes nbt_bytes = small_chunk(55);
        put32(raw, record, static_cast<std::uint32_t>(nbt_bytes.size() + 1));
        raw[record + 4] = 3;
        std::copy(nbt_bytes.begin(), nbt_bytes.end(), raw.begin() + static_cast<std::ptrdiff_t>(record + 5));
        const RegionFile none{Bytes(raw)};
        CHECK(*none.read_chunk(0, 0) == nbt_bytes);

        // Type 1 (gzip), crafted by hand.
        Bytes gz = file;
        const Bytes packed = zlib_compress(nbt_bytes.data(), nbt_bytes.size(), ZFormat::GZip);
        put32(gz, record, static_cast<std::uint32_t>(packed.size() + 1));
        gz[record + 4] = 1;
        std::copy(packed.begin(), packed.end(), gz.begin() + static_cast<std::ptrdiff_t>(record + 5));
        const RegionFile gzipped{Bytes(gz)};
        CHECK(*gzipped.read_chunk(0, 0) == nbt_bytes);

        // LZ4, external (.mcc) and unknown types are reported, not misread.
        for (const std::uint8_t type : {std::uint8_t{4}, std::uint8_t{0x82}, std::uint8_t{9}}) {
            Bytes other = file;
            other[record + 4] = type;
            const RegionFile region{Bytes(other)};  // header is fine
            CHECK(throws([&] { region.read_chunk(0, 0); }));
        }
    }

    // Corrupt headers are rejected up front.
    {
        CHECK(throws([] { RegionFile{Bytes{}}; }));
        CHECK(throws([] { RegionFile{Bytes(4096, 0)}; }));
        CHECK(throws([&] { Bytes b = file; b.push_back(0); RegionFile{std::move(b)}; }));  // not sector-aligned

        // Sector offset below the header.
        Bytes low = file;
        low[2] = 1;
        CHECK(throws([&] { RegionFile{Bytes(low)}; }));

        // Sector offset past the end of the file.
        Bytes far = file;
        far[1] = 1;  // offset 0x000102
        CHECK(throws([&] { RegionFile{Bytes(far)}; }));

        // Offset set but zero sector count.
        Bytes zero_count = file;
        zero_count[3] = 0;
        CHECK(throws([&] { RegionFile{Bytes(zero_count)}; }));

        // Record length larger than its sectors, and zero length.
        Bytes long_len = file;
        put32(long_len, record, 5000);
        CHECK(throws([&] { RegionFile{Bytes(long_len)}; }));
        Bytes zero_len = file;
        put32(zero_len, record, 0);
        CHECK(throws([&] { RegionFile{Bytes(zero_len)}; }));

        // Corrupted compressed data fails on read, not on open.
        Bytes damaged = file;
        for (std::size_t i = record + 7; i < record + 12; ++i) {
            damaged[i] ^= 0xFF;
        }
        const RegionFile region{Bytes(damaged)};
        CHECK(throws([&] { region.read_chunk(0, 0); }));
    }

    // Reading from disk.
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / "minecpp_region_test.mca";
        {
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
        }
        const RegionFile region = RegionFile::from_file(path.string());
        CHECK(*region.read_chunk(31, 31) == small_chunk(300));
        std::filesystem::remove(path);
        CHECK(throws([&] { RegionFile::from_file(path.string()); }));
    }

    std::cout << "region tests passed\n";
    return 0;
}
