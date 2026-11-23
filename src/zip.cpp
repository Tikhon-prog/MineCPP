#include "zip.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <zlib.h>

namespace minecpp {
namespace {
constexpr std::uint32_t kEocd = 0x06054B50U;
constexpr std::uint32_t kCentral = 0x02014B50U;
constexpr std::uint32_t kLocal = 0x04034B50U;
constexpr std::uint64_t kMaxEntrySize = 1024ULL * 1024ULL * 1024ULL;

std::uint16_t u16(const std::vector<unsigned char>& b, std::size_t p) {
    if (p + 2 > b.size()) throw std::runtime_error("Unexpected end of ZIP file");
    return static_cast<std::uint16_t>(b[p] | (static_cast<unsigned int>(b[p + 1]) << 8U));
}
std::uint32_t u32(const std::vector<unsigned char>& b, std::size_t p) {
    if (p + 4 > b.size()) throw std::runtime_error("Unexpected end of ZIP file");
    return static_cast<std::uint32_t>(b[p]) | (static_cast<std::uint32_t>(b[p + 1]) << 8U) |
           (static_cast<std::uint32_t>(b[p + 2]) << 16U) | (static_cast<std::uint32_t>(b[p + 3]) << 24U);
}
std::string read_text(std::ifstream& in, std::size_t n) {
    std::string value(n, '\0');
    in.read(value.data(), static_cast<std::streamsize>(n));
    if (in.gcount() != static_cast<std::streamsize>(n)) throw std::runtime_error("Truncated ZIP entry name");
    return value;
}
std::filesystem::path safe_relative_path(std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    if (name.empty() || name.find('\0') != std::string::npos || name.front() == '/' || (name.size() > 1 && name[1] == ':'))
        throw std::runtime_error("Unsafe absolute or invalid path in JAR: " + name);
    std::filesystem::path relative;
    std::size_t start = 0;
    while (start <= name.size()) {
        const auto end = name.find('/', start);
        const std::string part = name.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part == "..") throw std::runtime_error("Unsafe parent path in JAR: " + name);
        if (!part.empty() && part != ".") relative /= std::filesystem::path(part);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return relative;
}
}

ZipArchive::ZipArchive(std::filesystem::path file) : file_(std::move(file)) {
    std::ifstream in(file_, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("Cannot open JAR: " + file_.string());
    const auto end = in.tellg();
    if (end < 22) throw std::runtime_error("File is too small to be a ZIP/JAR archive");
    const auto file_size = static_cast<std::uint64_t>(end);
    const std::size_t tail_size = static_cast<std::size_t>(std::min<std::uint64_t>(file_size, 22ULL + 65535ULL));
    std::vector<unsigned char> tail(tail_size);
    in.seekg(static_cast<std::streamoff>(file_size - tail_size));
    in.read(reinterpret_cast<char*>(tail.data()), static_cast<std::streamsize>(tail.size()));
    if (!in) throw std::runtime_error("Cannot read ZIP directory");
    std::size_t eocd = std::string::npos;
    for (std::size_t p = tail.size() - 22 + 1; p-- > 0;) {
        if (u32(tail, p) == kEocd && p + 22 <= tail.size() &&
            p + 22 + u16(tail, p + 20) == tail.size()) { eocd = p; break; }
    }
    if (eocd == std::string::npos) throw std::runtime_error("ZIP end-of-central-directory record not found");
    if (u16(tail, eocd + 4) != 0 || u16(tail, eocd + 6) != 0 || u16(tail, eocd + 8) != u16(tail, eocd + 10))
        throw std::runtime_error("Multi-disk ZIP/JAR files are not supported");
    const auto count = u16(tail, eocd + 10);
    const auto directory_size = u32(tail, eocd + 12);
    const auto directory_offset = u32(tail, eocd + 16);
    if (count == 0xFFFFU || directory_size == 0xFFFFFFFFU || directory_offset == 0xFFFFFFFFU)
        throw std::runtime_error("ZIP64 archives are not supported in this build");
    if (static_cast<std::uint64_t>(directory_offset) + directory_size > file_size)
        throw std::runtime_error("Invalid ZIP central-directory bounds");

    in.clear();
    in.seekg(static_cast<std::streamoff>(directory_offset));
    entries_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::array<unsigned char, 46> header{};
        in.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
        if (!in) throw std::runtime_error("Truncated ZIP central directory");
        std::vector<unsigned char> h(header.begin(), header.end());
        if (u32(h, 0) != kCentral) throw std::runtime_error("Invalid ZIP central-directory entry");
        Entry e;
        e.flags = u16(h, 8);
        e.method = u16(h, 10);
        e.crc = u32(h, 16);
        e.compressed_size = u32(h, 20);
        e.uncompressed_size = u32(h, 24);
        const auto name_len = u16(h, 28);
        const auto extra_len = u16(h, 30);
        const auto comment_len = u16(h, 32);
        e.local_header_offset = u32(h, 42);
        e.name = read_text(in, name_len);
        in.seekg(static_cast<std::streamoff>(extra_len + comment_len), std::ios::cur);
        if (!in) throw std::runtime_error("Invalid ZIP central-directory lengths");
        e.directory = !e.name.empty() && (e.name.back() == '/' || e.name.back() == '\\');
        if ((e.flags & 0x0001U) != 0) throw std::runtime_error("Encrypted JAR entries are not supported");
        if (e.method != 0 && e.method != 8) throw std::runtime_error("Unsupported ZIP compression method " + std::to_string(e.method) + " in " + e.name);
        if (e.uncompressed_size > kMaxEntrySize) throw std::runtime_error("JAR entry exceeds 1 GiB safety limit: " + e.name);
        entries_.push_back(std::move(e));
    }
}

std::size_t ZipArchive::count_files() const {
    return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(), [](const Entry& e) { return !e.directory; }));
}

std::size_t ZipArchive::extract_all(const std::filesystem::path& destination) const {
    std::filesystem::create_directories(destination);
    std::ifstream in(file_, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot reopen JAR: " + file_.string());
    std::size_t extracted = 0;
    std::uint64_t total_uncompressed = 0;
    for (const Entry& entry : entries_) {
        const auto relative = safe_relative_path(entry.name);
        if (relative.empty()) continue;
        const auto output = destination / relative;
        if (entry.directory) { std::filesystem::create_directories(output); continue; }
        total_uncompressed += entry.uncompressed_size;
        if (total_uncompressed > 4ULL * 1024ULL * 1024ULL * 1024ULL)
            throw std::runtime_error("JAR extraction exceeds 4 GiB safety limit");

        std::vector<unsigned char> local(30);
        in.seekg(static_cast<std::streamoff>(entry.local_header_offset));
        in.read(reinterpret_cast<char*>(local.data()), static_cast<std::streamsize>(local.size()));
        if (!in || u32(local, 0) != kLocal) throw std::runtime_error("Invalid local ZIP header for " + entry.name);
        if (u16(local, 8) != entry.method) throw std::runtime_error("ZIP compression method mismatch for " + entry.name);
        const auto name_len = u16(local, 26);
        const auto extra_len = u16(local, 28);
        const auto data_offset = static_cast<std::uint64_t>(entry.local_header_offset) + 30ULL + name_len + extra_len;
        in.seekg(static_cast<std::streamoff>(data_offset));
        std::vector<unsigned char> compressed(entry.compressed_size);
        if (!compressed.empty()) in.read(reinterpret_cast<char*>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
        if (!in) throw std::runtime_error("Truncated compressed data for " + entry.name);

        std::vector<unsigned char> data(entry.uncompressed_size);
        if (entry.method == 0) {
            if (entry.compressed_size != entry.uncompressed_size) throw std::runtime_error("Invalid stored ZIP entry size: " + entry.name);
            data = std::move(compressed);
        } else {
            z_stream stream{};
            stream.next_in = compressed.data();
            stream.avail_in = static_cast<uInt>(compressed.size());
            stream.next_out = data.data();
            stream.avail_out = static_cast<uInt>(data.size());
            if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) throw std::runtime_error("Cannot initialize ZIP decompressor");
            const int result = inflate(&stream, Z_FINISH);
            inflateEnd(&stream);
            if (result != Z_STREAM_END || stream.total_out != entry.uncompressed_size)
                throw std::runtime_error("Cannot decompress JAR entry: " + entry.name);
        }
        const auto actual_crc = crc32(0L, data.empty() ? Z_NULL : data.data(), static_cast<uInt>(data.size()));
        if (actual_crc != entry.crc) throw std::runtime_error("CRC check failed for JAR entry: " + entry.name);
        std::filesystem::create_directories(output.parent_path());
        std::ofstream out(output, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot create extracted file: " + output.string());
        if (!data.empty()) out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) throw std::runtime_error("Failed writing extracted file: " + output.string());
        ++extracted;
    }
    return extracted;
}

} // namespace minecpp
