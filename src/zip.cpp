#include "zip.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <zlib.h>

namespace minecpp {
namespace {
constexpr std::uint32_t kEocd = 0x06054B50U;
constexpr std::uint32_t kCentral = 0x02014B50U;
constexpr std::uint32_t kLocal = 0x04034B50U;
constexpr std::uint64_t kMaxArchiveSize = 2ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxEntrySize = 128ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxTotalUncompressed = 2ULL * 1024ULL * 1024ULL * 1024ULL;

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
std::string lower_ascii(std::string value) {
    for (char& ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte >= 'A' && byte <= 'Z') ch = static_cast<char>(byte - 'A' + 'a');
    }
    return value;
}

std::string safe_archive_name(std::string name, bool& is_directory) {
    if (name.empty() || name.find('\0') != std::string::npos)
        throw std::runtime_error("JAR contains an empty or invalid entry name");
    for (char& ch : name) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte < 0x20U || byte == 0x7fU)
            throw std::runtime_error("JAR contains a filename with a control character");
        if (ch == '\\') ch = '/';
    }

    is_directory = name.back() == '/';
    if (name.front() == '/' || name.rfind("//", 0) == 0)
        throw std::runtime_error("JAR contains an absolute path: " + name);
    if (is_directory) name.pop_back();
    if (name.empty()) throw std::runtime_error("JAR contains an invalid root directory entry");

    std::size_t begin = 0;
    while (begin <= name.size()) {
        const std::size_t end = name.find('/', begin);
        const std::string part = name.substr(begin,
            (end == std::string::npos ? name.size() : end) - begin);
        if (part.empty() || part == "." || part == ".." ||
            part.find(':') != std::string::npos) {
            throw std::runtime_error("JAR contains an unsafe path: " + name);
        }
        if (part.back() == '.' || part.back() == ' ' ||
            part.find_first_of("<>\"|?*") != std::string::npos) {
            throw std::runtime_error("JAR contains a filename that is not portable to Windows: " + name);
        }

        const std::string device = lower_ascii(part.substr(0, part.find('.')));
        const bool numbered_device = device.size() == 4U &&
            (device.rfind("com", 0) == 0 || device.rfind("lpt", 0) == 0) &&
            device[3] >= '1' && device[3] <= '9';
        if (device == "con" || device == "prn" || device == "aux" ||
            device == "nul" || device == "conin$" || device == "conout$" || numbered_device) {
            throw std::runtime_error("JAR contains a reserved Windows device filename: " + name);
        }

        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return name;
}

std::filesystem::path path_from_utf8(std::string_view value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char byte : value)
        encoded.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(encoded);
}
}

ZipArchive::ZipArchive(std::filesystem::path file) : file_(std::move(file)) {
    std::ifstream in(file_, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("Cannot open JAR: " + file_.string());
    const auto end = in.tellg();
    if (end < 22) throw std::runtime_error("File is too small to be a ZIP/JAR archive");
    const auto file_size = static_cast<std::uint64_t>(end);
    if (file_size > kMaxArchiveSize)
        throw std::runtime_error("JAR exceeds the 2 GiB archive safety limit");
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
    central_directory_offset_ = directory_offset;
    if (count == 0xFFFFU || directory_size == 0xFFFFFFFFU || directory_offset == 0xFFFFFFFFU)
        throw std::runtime_error("ZIP64 archives are not supported in this build");
    const std::uint64_t eocd_absolute = file_size - tail_size + eocd;
    const std::uint64_t central_end =
        static_cast<std::uint64_t>(directory_offset) + directory_size;
    if (central_end > eocd_absolute)
        throw std::runtime_error("Invalid ZIP central-directory bounds");

    in.clear();
    in.seekg(static_cast<std::streamoff>(directory_offset));
    entries_.reserve(count);
    std::set<std::string> names;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::array<unsigned char, 46> header{};
        in.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
        if (!in) throw std::runtime_error("Truncated ZIP central directory");
        std::vector<unsigned char> h(header.begin(), header.end());
        if (u32(h, 0) != kCentral) throw std::runtime_error("Invalid ZIP central-directory entry");
        Entry e;
        e.made_by = u16(h, 4);
        e.flags = u16(h, 8);
        e.method = u16(h, 10);
        e.crc = u32(h, 16);
        e.compressed_size = u32(h, 20);
        e.uncompressed_size = u32(h, 24);
        const auto name_len = u16(h, 28);
        const auto extra_len = u16(h, 30);
        const auto comment_len = u16(h, 32);
        e.external_attributes = u32(h, 38);
        e.local_header_offset = u32(h, 42);
        e.name = read_text(in, name_len);
        in.seekg(static_cast<std::streamoff>(extra_len + comment_len), std::ios::cur);
        if (!in || static_cast<std::uint64_t>(in.tellg()) > central_end)
            throw std::runtime_error("Invalid ZIP central-directory lengths");

        e.name = safe_archive_name(std::move(e.name), e.directory);
        if (!names.insert(lower_ascii(e.name)).second)
            throw std::runtime_error("JAR contains duplicate or case-colliding path: " + e.name);

        const unsigned int host_os = e.made_by >> 8U;
        const std::uint32_t unix_type = (e.external_attributes >> 16U) & 0170000U;
        if (host_os == 3U) {
            if (unix_type == 0120000U)
                throw std::runtime_error("JAR contains a symbolic link, which is not extracted");
            if (unix_type != 0U && unix_type != 0100000U && unix_type != 0040000U)
                throw std::runtime_error("JAR contains a special Unix file type, which is not extracted");
            if (unix_type != 0U && ((unix_type == 0040000U) != e.directory))
                throw std::runtime_error("JAR entry type does not match its path: " + e.name);
        }

        if ((e.flags & 0x0001U) != 0 || (e.flags & 0x0040U) != 0 || (e.flags & 0x2000U) != 0)
            throw std::runtime_error("Encrypted JAR entries are not supported");
        if (e.method != 0 && e.method != 8)
            throw std::runtime_error("Unsupported ZIP compression method " + std::to_string(e.method) + " in " + e.name);
        if (e.compressed_size > kMaxEntrySize || e.uncompressed_size > kMaxEntrySize)
            throw std::runtime_error("JAR entry exceeds the 128 MiB safety limit: " + e.name);
        entries_.push_back(std::move(e));
    }
    if (static_cast<std::uint64_t>(in.tellg()) != central_end)
        throw std::runtime_error("ZIP central-directory size does not match its entries");
}

std::size_t ZipArchive::count_files() const {
    return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(), [](const Entry& e) { return !e.directory; }));
}

std::size_t ZipArchive::extract_all(const std::filesystem::path& destination) const {
    namespace fs = std::filesystem;
    const fs::path destination_absolute = fs::absolute(destination);
    fs::create_directories(destination_absolute);
    const fs::path destination_root = fs::weakly_canonical(destination_absolute);

    const auto is_within_root = [](const fs::path& root, const fs::path& candidate) {
        auto root_it = root.begin();
        auto candidate_it = candidate.begin();
        for (; root_it != root.end() && candidate_it != candidate.end(); ++root_it, ++candidate_it) {
            if (*root_it != *candidate_it) return false;
        }
        return root_it == root.end();
    };

    const auto reject_symlink_components = [](const fs::path& root, const fs::path& relative) {
        fs::path current = root;
        for (const fs::path& component : relative) {
            current /= component;
            std::error_code error;
            const fs::file_status status = fs::symlink_status(current, error);
            if (!error && fs::is_symlink(status))
                throw std::runtime_error("Refusing to follow a pre-existing symlink during JAR extraction: " +
                                         current.string());
            if (error && error != std::errc::no_such_file_or_directory)
                throw std::runtime_error("Cannot inspect extraction path: " + error.message());
        }
    };

    std::ifstream in(file_, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot reopen JAR: " + file_.string());

    std::size_t extracted = 0;
    std::uint64_t total_uncompressed = 0;
    for (const Entry& entry : entries_) {
        const fs::path relative = path_from_utf8(entry.name);
        if (relative.empty() || relative.is_absolute())
            throw std::runtime_error("Invalid normalized path in JAR: " + entry.name);

        const fs::path output = destination_absolute / relative;
        reject_symlink_components(destination_absolute, relative);

        if (entry.directory) {
            const fs::path resolved_directory = fs::weakly_canonical(output);
            if (!is_within_root(destination_root, resolved_directory))
                throw std::runtime_error("JAR extraction path escapes destination: " + entry.name);
            fs::create_directories(output);
            const fs::path resolved_after_creation = fs::weakly_canonical(output);
            if (!is_within_root(destination_root, resolved_after_creation))
                throw std::runtime_error("JAR extraction path escaped destination: " + entry.name);
            continue;
        }

        if (entry.uncompressed_size > kMaxTotalUncompressed - total_uncompressed)
            throw std::runtime_error("JAR extraction exceeds the 2 GiB total safety limit");
        total_uncompressed += entry.uncompressed_size;

        if (static_cast<std::uint64_t>(entry.local_header_offset) >= central_directory_offset_)
            throw std::runtime_error("JAR local header points into the central directory: " + entry.name);

        std::array<unsigned char, 30> local{};
        in.clear();
        in.seekg(static_cast<std::streamoff>(entry.local_header_offset));
        in.read(reinterpret_cast<char*>(local.data()), static_cast<std::streamsize>(local.size()));
        if (!in || u32(std::vector<unsigned char>(local.begin(), local.end()), 0) != kLocal)
            throw std::runtime_error("Invalid local ZIP header for " + entry.name);
        if (u16(std::vector<unsigned char>(local.begin(), local.end()), 6) != entry.flags)
            throw std::runtime_error("ZIP general-purpose flags mismatch for " + entry.name);
        if (u16(std::vector<unsigned char>(local.begin(), local.end()), 8) != entry.method)
            throw std::runtime_error("ZIP compression method mismatch for " + entry.name);

        const std::uint16_t local_name_length =
            static_cast<std::uint16_t>(local[26]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(local[27]) << 8U);
        const std::uint16_t local_extra_length =
            static_cast<std::uint16_t>(local[28]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(local[29]) << 8U);
        const std::uint64_t data_offset = static_cast<std::uint64_t>(entry.local_header_offset) +
            local.size() + local_name_length + local_extra_length;
        if (data_offset > central_directory_offset_ ||
            static_cast<std::uint64_t>(entry.compressed_size) > central_directory_offset_ - data_offset) {
            throw std::runtime_error("JAR entry data overlaps the central directory: " + entry.name);
        }

        const std::string local_name = read_text(in, local_name_length);
        bool local_is_directory = false;
        const std::string normalized_local_name =
            safe_archive_name(local_name, local_is_directory);
        if (normalized_local_name != entry.name || local_is_directory != entry.directory)
            throw std::runtime_error("ZIP local and central filenames do not match: " + entry.name);

        in.seekg(static_cast<std::streamoff>(local_extra_length), std::ios::cur);
        if (!in || static_cast<std::uint64_t>(in.tellg()) != data_offset)
            throw std::runtime_error("Invalid local ZIP header lengths for " + entry.name);

        std::vector<unsigned char> compressed(entry.compressed_size);
        if (!compressed.empty()) {
            in.read(reinterpret_cast<char*>(compressed.data()),
                    static_cast<std::streamsize>(compressed.size()));
        }
        if (!in) throw std::runtime_error("Truncated compressed data for " + entry.name);

        std::vector<unsigned char> data;
        if (entry.method == 0) {
            if (entry.compressed_size != entry.uncompressed_size)
                throw std::runtime_error("Invalid stored ZIP entry size: " + entry.name);
            data = std::move(compressed);
        } else {
            data.resize(entry.uncompressed_size);
            z_stream stream{};
            unsigned char empty_output = 0;
            stream.next_in = compressed.empty() ? Z_NULL : compressed.data();
            stream.avail_in = static_cast<uInt>(compressed.size());
            stream.next_out = data.empty() ? &empty_output : data.data();
            stream.avail_out = data.empty() ? 1U : static_cast<uInt>(data.size());
            if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
                throw std::runtime_error("Cannot initialize ZIP decompressor");
            const int result = inflate(&stream, Z_FINISH);
            const auto total_out = stream.total_out;
            const auto total_in = stream.total_in;
            inflateEnd(&stream);
            if (result != Z_STREAM_END || total_out != entry.uncompressed_size ||
                total_in != entry.compressed_size) {
                throw std::runtime_error("Cannot decompress JAR entry: " + entry.name);
            }
        }

        const auto actual_crc = crc32(
            0L, data.empty() ? Z_NULL : data.data(), static_cast<uInt>(data.size()));
        if (actual_crc != entry.crc)
            throw std::runtime_error("CRC check failed for JAR entry: " + entry.name);

        const fs::path resolved_parent = fs::weakly_canonical(output.parent_path());
        if (!is_within_root(destination_root, resolved_parent))
            throw std::runtime_error("JAR extraction path escapes destination: " + entry.name);
        fs::create_directories(output.parent_path());
        reject_symlink_components(destination_absolute, relative);
        const fs::path resolved_parent_after_creation = fs::weakly_canonical(output.parent_path());
        if (!is_within_root(destination_root, resolved_parent_after_creation))
            throw std::runtime_error("JAR extraction path escaped destination: " + entry.name);

        std::error_code status_error;
        const fs::file_status output_status = fs::symlink_status(output, status_error);
        if (!status_error && fs::is_symlink(output_status))
            throw std::runtime_error("Refusing to overwrite a symlink during JAR extraction: " + output.string());
        if (!status_error && fs::exists(output_status) && !fs::is_regular_file(output_status))
            throw std::runtime_error("Refusing to overwrite a non-regular file during JAR extraction: " +
                                     output.string());
        if (status_error && status_error != std::errc::no_such_file_or_directory)
            throw std::runtime_error("Cannot inspect extraction target: " + status_error.message());

        std::ofstream out(output, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot create extracted file: " + output.string());
        if (!data.empty())
            out.write(reinterpret_cast<const char*>(data.data()),
                      static_cast<std::streamsize>(data.size()));
        if (!out) throw std::runtime_error("Failed writing extracted file: " + output.string());
        ++extracted;
    }
    return extracted;
}

} // namespace minecpp
