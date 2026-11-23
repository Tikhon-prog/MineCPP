#include "jar_importer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

#include <zlib.h>

namespace minecpp::launcher {
namespace fs = std::filesystem;
namespace {

constexpr std::uint32_t kEocdSignature = 0x06054b50U;
constexpr std::uint32_t kCentralSignature = 0x02014b50U;
constexpr std::uint32_t kLocalSignature = 0x04034b50U;
constexpr std::uint64_t kMaxArchiveBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxEntryBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxExpandedBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kMaxEntries = 100000U;
constexpr std::string_view kManifestName = "manifest.mcpp";

struct FileRecord {
    std::string relative_path;
    std::uint64_t size = 0;
    std::uint32_t crc = 0;
};

struct Manifest {
    std::string version_id;
    std::string source_filename;
    std::size_t file_count = 0;
    std::uint64_t total_bytes = 0;
    std::vector<FileRecord> files;
};

std::uint16_t u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8U);
}

std::uint32_t u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8U) |
           (static_cast<std::uint32_t>(p[2]) << 16U) |
           (static_cast<std::uint32_t>(p[3]) << 24U);
}

void read_at(std::ifstream& input, std::uint64_t offset, char* destination, std::size_t count) {
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        count > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("JAR contains an offset that this platform cannot read");
    }
    input.clear();
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) throw std::runtime_error("Cannot seek inside JAR file");
    input.read(destination, static_cast<std::streamsize>(count));
    if (input.gcount() != static_cast<std::streamsize>(count))
        throw std::runtime_error("JAR is truncated or has an invalid ZIP offset");
}

std::string safe_archive_path(std::string name, bool& is_directory) {
    if (name.empty()) throw std::runtime_error("JAR contains an empty entry name");
    if (name.find('\0') != std::string::npos)
        throw std::runtime_error("JAR contains a filename with a NUL byte");

    for (char& ch : name) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte < 0x20U || byte == 0x7fU)
            throw std::runtime_error("JAR contains a filename with a control character");
        if (ch == '\\') ch = '/';
    }

    is_directory = !name.empty() && name.back() == '/';
    if (name.front() == '/' || name.rfind("//", 0) == 0)
        throw std::runtime_error("JAR contains an absolute path: " + name);
    if (is_directory) name.pop_back();
    if (name.empty()) throw std::runtime_error("JAR contains an invalid root directory entry");

    std::size_t begin = 0;
    while (begin <= name.size()) {
        const std::size_t end = name.find('/', begin);
        const std::string_view part(name.data() + begin,
            (end == std::string::npos ? name.size() : end) - begin);
        if (part.empty() || part == "." || part == ".." || part.find(':') != std::string_view::npos)
            throw std::runtime_error("JAR contains an unsafe path: " + name);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return name;
}

std::string lower_ascii(std::string value) {
    for (char& ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte >= 'A' && byte <= 'Z') ch = static_cast<char>(byte - 'A' + 'a');
    }
    return value;
}

std::string percent_encode(std::string_view value) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' ||
            ch == '/' ) {
            result.push_back(static_cast<char>(ch));
        } else {
            result.push_back('%');
            result.push_back(digits[ch >> 4U]);
            result.push_back(digits[ch & 0x0fU]);
        }
    }
    return result;
}

int hex_digit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

std::string percent_decode(std::string_view value) {
    std::string result;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '%') {
            result.push_back(value[i]);
            continue;
        }
        if (i + 2 >= value.size()) throw std::runtime_error("Malformed manifest escape");
        const int hi = hex_digit(value[i + 1]);
        const int lo = hex_digit(value[i + 2]);
        if (hi < 0 || lo < 0) throw std::runtime_error("Malformed manifest escape");
        result.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
    }
    return result;
}

std::uint32_t crc_of(const std::uint8_t* data, std::size_t size) {
    const uLong value = ::crc32(0L, data, static_cast<uInt>(size));
    return static_cast<std::uint32_t>(value);
}

std::uint32_t crc_file(const fs::path& path, std::uint64_t& size) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read imported file: " + path.string());

    uLong crc = ::crc32(0L, Z_NULL, 0);
    size = 0;
    std::array<unsigned char, 64U * 1024U> buffer{};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count <= 0) break;
        crc = ::crc32(crc, buffer.data(), static_cast<uInt>(count));
        size += static_cast<std::uint64_t>(count);
    }
    if (!input.eof()) throw std::runtime_error("Error while reading imported file: " + path.string());
    return static_cast<std::uint32_t>(crc);
}

void write_file(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot create extracted file: " + path.string());
    if (!bytes.empty())
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("Failed writing extracted file: " + path.string());
}

std::vector<std::uint8_t> inflate_raw(
    const std::vector<std::uint8_t>& compressed,
    std::uint32_t expected_size) {

    std::vector<std::uint8_t> output(std::max<std::size_t>(1U, expected_size));
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());

    const int init_result = inflateInit2(&stream, -MAX_WBITS);
    if (init_result != Z_OK) throw std::runtime_error("zlib could not initialize the ZIP decompressor");
    const int result = inflate(&stream, Z_FINISH);
    const bool valid = result == Z_STREAM_END &&
        stream.total_out == expected_size && stream.total_in == compressed.size();
    inflateEnd(&stream);

    if (!valid) throw std::runtime_error("JAR contains a corrupt or incomplete DEFLATE entry");
    output.resize(expected_size);
    return output;
}

std::uint64_t parse_u64(const std::string& value, const char* field) {
    std::size_t consumed = 0;
    unsigned long long parsed = 0;
    try { parsed = std::stoull(value, &consumed, 10); }
    catch (...) { throw std::runtime_error(std::string("Invalid manifest field: ") + field); }
    if (consumed != value.size()) throw std::runtime_error(std::string("Invalid manifest field: ") + field);
    return static_cast<std::uint64_t>(parsed);
}

std::uint32_t parse_hex32(const std::string& value) {
    std::size_t consumed = 0;
    unsigned long parsed = 0;
    try { parsed = std::stoul(value, &consumed, 16); }
    catch (...) { throw std::runtime_error("Invalid CRC in import manifest"); }
    if (consumed != value.size() || parsed > 0xffffffffUL)
        throw std::runtime_error("Invalid CRC in import manifest");
    return static_cast<std::uint32_t>(parsed);
}

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t begin = 0;
    for (;;) {
        const std::size_t end = line.find('\t', begin);
        fields.push_back(line.substr(begin, end == std::string::npos ? end : end - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return fields;
}

Manifest read_manifest(const fs::path& directory) {
    std::ifstream input(directory / std::string(kManifestName), std::ios::binary);
    if (!input) throw std::runtime_error("Import manifest is missing: " + directory.string());

    std::string line;
    if (!std::getline(input, line) || line != "MINECPP_IMPORT\t1")
        throw std::runtime_error("Unsupported or damaged MineCPP import manifest");

    Manifest manifest;
    bool got_version = false;
    bool got_source = false;
    bool got_count = false;
    bool got_bytes = false;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto fields = split_tabs(line);
        if (fields.empty()) continue;
        if (fields[0] == "VERSION" && fields.size() == 2) {
            manifest.version_id = percent_decode(fields[1]);
            got_version = true;
        } else if (fields[0] == "SOURCE" && fields.size() == 2) {
            manifest.source_filename = percent_decode(fields[1]);
            got_source = true;
        } else if (fields[0] == "FILES" && fields.size() == 2) {
            manifest.file_count = static_cast<std::size_t>(parse_u64(fields[1], "FILES"));
            got_count = true;
        } else if (fields[0] == "BYTES" && fields.size() == 2) {
            manifest.total_bytes = parse_u64(fields[1], "BYTES");
            got_bytes = true;
        } else if (fields[0] == "ENTRY" && fields.size() == 4) {
            FileRecord record;
            record.size = parse_u64(fields[1], "ENTRY size");
            record.crc = parse_hex32(fields[2]);
            record.relative_path = percent_decode(fields[3]);
            bool is_directory = false;
            if (safe_archive_path(record.relative_path, is_directory) != record.relative_path || is_directory)
                throw std::runtime_error("Unsafe path in import manifest");
            manifest.files.push_back(std::move(record));
        } else {
            throw std::runtime_error("Unknown or malformed line in import manifest");
        }
    }

    if (!got_version || !got_source || !got_count || !got_bytes ||
        manifest.file_count != manifest.files.size())
        throw std::runtime_error("Import manifest is incomplete");
    if (!valid_version_id(manifest.version_id))
        throw std::runtime_error("Import manifest contains an invalid version identifier");
    return manifest;
}

void write_manifest(const fs::path& path, const Manifest& manifest) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot create import manifest");
    output << "MINECPP_IMPORT\t1\n";
    output << "VERSION\t" << percent_encode(manifest.version_id) << '\n';
    output << "SOURCE\t" << percent_encode(manifest.source_filename) << '\n';
    output << "FILES\t" << manifest.files.size() << '\n';
    output << "BYTES\t" << manifest.total_bytes << '\n';
    for (const auto& file : manifest.files) {
        output << "ENTRY\t" << file.size << '\t'
               << std::hex << std::setw(8) << std::setfill('0') << file.crc
               << std::dec << std::setfill(' ') << '\t'
               << percent_encode(file.relative_path) << '\n';
    }
    if (!output) throw std::runtime_error("Failed writing import manifest");
}

std::vector<FileRecord> extract_archive(const fs::path& jar_path, const fs::path& output_directory,
                                         std::uint64_t& total_bytes) {
    const std::uint64_t archive_size = fs::file_size(jar_path);
    if (archive_size < 22U) throw std::runtime_error("Input file is too small to be a JAR/ZIP archive");
    if (archive_size > kMaxArchiveBytes)
        throw std::runtime_error("JAR exceeds the 2 GiB safety limit for this first release");

    std::ifstream input(jar_path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open JAR: " + jar_path.string());

    const std::size_t tail_size = static_cast<std::size_t>(
        std::min<std::uint64_t>(archive_size, 22U + 65535U));
    std::vector<std::uint8_t> tail(tail_size);
    read_at(input, archive_size - tail_size, reinterpret_cast<char*>(tail.data()), tail.size());

    std::size_t eocd_index = std::string::npos;
    for (std::size_t i = tail.size() - 22U;; --i) {
        if (u32(tail.data() + i) == kEocdSignature) {
            const std::uint16_t comment_size = u16(tail.data() + i + 20U);
            if (i + 22U + comment_size == tail.size()) {
                eocd_index = i;
                break;
            }
        }
        if (i == 0) break;
    }
    if (eocd_index == std::string::npos)
        throw std::runtime_error("JAR has no valid ZIP end-of-central-directory record");

    const std::uint8_t* eocd = tail.data() + eocd_index;
    const std::uint16_t disk_number = u16(eocd + 4U);
    const std::uint16_t central_disk = u16(eocd + 6U);
    const std::uint16_t disk_entries = u16(eocd + 8U);
    const std::uint16_t total_entries = u16(eocd + 10U);
    const std::uint32_t central_size = u32(eocd + 12U);
    const std::uint32_t central_offset = u32(eocd + 16U);
    const std::uint64_t eocd_absolute = archive_size - tail_size + eocd_index;

    if (disk_number != 0 || central_disk != 0 || disk_entries != total_entries)
        throw std::runtime_error("Multi-disk ZIP/JAR files are not supported");
    if (total_entries == 0xffffU || central_size == 0xffffffffU || central_offset == 0xffffffffU)
        throw std::runtime_error("ZIP64 JAR files are not supported yet");
    if (total_entries > kMaxEntries)
        throw std::runtime_error("JAR contains more than 100,000 entries");
    if (static_cast<std::uint64_t>(central_offset) + central_size > eocd_absolute)
        throw std::runtime_error("JAR central directory points outside the archive");

    std::uint64_t cursor = central_offset;
    const std::uint64_t central_end = static_cast<std::uint64_t>(central_offset) + central_size;
    std::vector<FileRecord> records;
    records.reserve(total_entries);
    std::set<std::string> names;
    std::uint64_t expanded_total = 0;
    bool found_class = false;

    for (std::uint32_t index = 0; index < total_entries; ++index) {
        std::array<std::uint8_t, 46> header{};
        if (cursor + header.size() > central_end) throw std::runtime_error("Truncated JAR central directory");
        read_at(input, cursor, reinterpret_cast<char*>(header.data()), header.size());
        if (u32(header.data()) != kCentralSignature)
            throw std::runtime_error("Invalid entry in JAR central directory");

        const std::uint16_t made_by = u16(header.data() + 4U);
        const std::uint16_t flags = u16(header.data() + 8U);
        const std::uint16_t method = u16(header.data() + 10U);
        const std::uint32_t expected_crc = u32(header.data() + 16U);
        const std::uint32_t compressed_size = u32(header.data() + 20U);
        const std::uint32_t uncompressed_size = u32(header.data() + 24U);
        const std::uint16_t name_length = u16(header.data() + 28U);
        const std::uint16_t extra_length = u16(header.data() + 30U);
        const std::uint16_t comment_length = u16(header.data() + 32U);
        const std::uint16_t start_disk = u16(header.data() + 34U);
        const std::uint32_t external_attributes = u32(header.data() + 38U);
        const std::uint32_t local_offset = u32(header.data() + 42U);

        if (start_disk != 0) throw std::runtime_error("Split JAR entries are not supported");
        if (compressed_size == 0xffffffffU || uncompressed_size == 0xffffffffU ||
            local_offset == 0xffffffffU)
            throw std::runtime_error("ZIP64 entries are not supported yet");
        if ((flags & 0x0001U) != 0 || (flags & 0x0040U) != 0 || (flags & 0x2000U) != 0)
            throw std::runtime_error("Encrypted JAR entries are not supported");
        if (method != 0 && method != 8)
            throw std::runtime_error("JAR uses an unsupported compression method");

        const std::uint64_t variable_size = static_cast<std::uint64_t>(name_length) +
            extra_length + comment_length;
        if (cursor + header.size() + variable_size > central_end)
            throw std::runtime_error("Truncated JAR entry metadata");

        std::vector<char> variable(static_cast<std::size_t>(variable_size));
        if (!variable.empty())
            read_at(input, cursor + header.size(), variable.data(), variable.size());
        std::string raw_name(variable.data(), name_length);
        bool is_directory = false;
        const std::string relative_name = safe_archive_path(std::move(raw_name), is_directory);
        const std::string collision_key = lower_ascii(relative_name);
        if (!names.insert(collision_key).second)
            throw std::runtime_error("JAR contains duplicate or case-colliding path: " + relative_name);

        const unsigned int host_os = made_by >> 8U;
        const std::uint32_t unix_mode = external_attributes >> 16U;
        if (host_os == 3U && (unix_mode & 0170000U) == 0120000U)
            throw std::runtime_error("JAR contains a symbolic link, which is not extracted");

        cursor += header.size() + variable_size;
        const fs::path destination = output_directory / fs::u8path(relative_name);
        if (is_directory) {
            fs::create_directories(destination);
            continue;
        }

        if (uncompressed_size > kMaxEntryBytes || compressed_size > kMaxEntryBytes)
            throw std::runtime_error("JAR entry exceeds the 512 MiB safety limit: " + relative_name);
        if (expanded_total > kMaxExpandedBytes - uncompressed_size)
            throw std::runtime_error("JAR expands beyond the 2 GiB safety limit");

        std::array<std::uint8_t, 30> local{};
        read_at(input, local_offset, reinterpret_cast<char*>(local.data()), local.size());
        if (u32(local.data()) != kLocalSignature)
            throw std::runtime_error("Invalid local ZIP header for: " + relative_name);
        if (u16(local.data() + 8U) != method)
            throw std::runtime_error("ZIP compression method mismatch for: " + relative_name);
        const std::uint16_t local_name_length = u16(local.data() + 26U);
        const std::uint16_t local_extra_length = u16(local.data() + 28U);
        std::vector<char> local_name(local_name_length);
        if (!local_name.empty())
            read_at(input, static_cast<std::uint64_t>(local_offset) + local.size(),
                    local_name.data(), local_name.size());
        if (std::string(local_name.begin(), local_name.end()) !=
            std::string(variable.data(), name_length))
            throw std::runtime_error("ZIP filename mismatch for: " + relative_name);

        const std::uint64_t data_offset = static_cast<std::uint64_t>(local_offset) +
            local.size() + local_name_length + local_extra_length;
        if (data_offset + compressed_size > central_offset)
            throw std::runtime_error("JAR entry data overlaps the central directory");
        std::vector<std::uint8_t> compressed(compressed_size);
        if (!compressed.empty())
            read_at(input, data_offset, reinterpret_cast<char*>(compressed.data()), compressed.size());

        std::vector<std::uint8_t> uncompressed;
        if (method == 0) {
            if (compressed_size != uncompressed_size)
                throw std::runtime_error("Stored JAR entry has inconsistent sizes: " + relative_name);
            uncompressed = std::move(compressed);
        } else {
            uncompressed = inflate_raw(compressed, uncompressed_size);
        }
        const std::uint32_t actual_crc = crc_of(uncompressed.data(), uncompressed.size());
        if (actual_crc != expected_crc)
            throw std::runtime_error("CRC check failed for JAR entry: " + relative_name);

        write_file(destination, uncompressed);
        expanded_total += uncompressed_size;
        records.push_back(FileRecord{
            "client/" + relative_name,
            static_cast<std::uint64_t>(uncompressed_size),
            actual_crc
        });
        const std::string lower_name = lower_ascii(relative_name);
        if (lower_name.size() >= 6U && lower_name.substr(lower_name.size() - 6U) == ".class")
            found_class = true;
    }

    if (cursor != central_end) throw std::runtime_error("JAR central directory size does not match its entries");
    if (records.empty() || !found_class)
        throw std::runtime_error("The selected JAR does not look like a Minecraft client JAR (no .class files)");

    total_bytes = expanded_total;
    return records;
}

std::string source_filename_utf8(const fs::path& path) {
    const auto value = path.filename().u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

} // namespace

bool valid_version_id(const std::string& value) {
    if (value.empty() || value.size() > 64U) return false;
    const auto first = static_cast<unsigned char>(value.front());
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') ||
          (first >= '0' && first <= '9'))) return false;
    for (const unsigned char ch : value) {
        const bool valid = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '+' || ch == '-';
        if (!valid) return false;
    }
    return value != "." && value != "..";
}

ImportSummary import_jar(const fs::path& jar_path, const fs::path& data_directory,
                          std::string version_id, bool overwrite) {
    std::error_code error;
    const fs::path absolute_jar = fs::absolute(jar_path, error);
    if (error) throw std::runtime_error("Cannot resolve JAR path: " + error.message());
    if (!fs::is_regular_file(absolute_jar))
        throw std::runtime_error("Input JAR does not exist or is not a regular file: " + absolute_jar.string());

    if (version_id.empty()) version_id = source_filename_utf8(absolute_jar);
    if (version_id.size() >= 4U && lower_ascii(version_id.substr(version_id.size() - 4U)) == ".jar")
        version_id.resize(version_id.size() - 4U);
    if (!valid_version_id(version_id))
        throw std::runtime_error("Invalid version ID. Use 1.20.4, 26.3, or another simple identifier.");

    const fs::path versions_directory = data_directory / "versions";
    fs::create_directories(versions_directory);
    const fs::path final_directory = versions_directory / version_id;
    if (fs::exists(final_directory) && !overwrite)
        throw std::runtime_error("Version " + version_id + " is already imported; use --force to replace it.");

    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path staging_directory = final_directory;
    staging_directory += ".staging-" + std::to_string(tick);
    fs::create_directories(staging_directory);

    try {
        std::uint64_t jar_bytes = 0;
        std::vector<FileRecord> records = extract_archive(
            absolute_jar, staging_directory / "client", jar_bytes);

        bool copied_metadata = false;
        const fs::path metadata_source = absolute_jar.parent_path() / fs::u8path(version_id + ".json");
        if (fs::is_regular_file(metadata_source)) {
            const std::uint64_t metadata_size = fs::file_size(metadata_source);
            if (metadata_size > 16U * 1024U * 1024U)
                throw std::runtime_error("Version JSON metadata exceeds the 16 MiB safety limit");
            const fs::path metadata_target = staging_directory / "metadata" / "version.json";
            fs::create_directories(metadata_target.parent_path());
            fs::copy_file(metadata_source, metadata_target, fs::copy_options::none);
            std::uint64_t measured_size = 0;
            const std::uint32_t metadata_crc = crc_file(metadata_target, measured_size);
            records.push_back(FileRecord{"metadata/version.json", measured_size, metadata_crc});
            jar_bytes += measured_size;
            copied_metadata = true;
        }

        Manifest manifest;
        manifest.version_id = version_id;
        manifest.source_filename = source_filename_utf8(absolute_jar);
        manifest.files = std::move(records);
        manifest.file_count = manifest.files.size();
        manifest.total_bytes = jar_bytes;
        write_manifest(staging_directory / std::string(kManifestName), manifest);

        fs::path backup_directory = final_directory;
        backup_directory += ".backup";
        bool backed_up = false;
        if (fs::exists(final_directory)) {
            if (fs::exists(backup_directory))
                throw std::runtime_error("Backup path already exists; remove it before using --force: " +
                                         backup_directory.string());
            fs::rename(final_directory, backup_directory);
            backed_up = true;
        }

        try {
            fs::rename(staging_directory, final_directory);
        } catch (...) {
            if (backed_up) {
                std::error_code restore_error;
                fs::rename(backup_directory, final_directory, restore_error);
            }
            throw;
        }
        if (backed_up) fs::remove_all(backup_directory);

        return ImportSummary{
            version_id, source_filename_utf8(absolute_jar), final_directory,
            manifest.file_count, manifest.total_bytes, copied_metadata
        };
    } catch (...) {
        std::error_code cleanup_error;
        fs::remove_all(staging_directory, cleanup_error);
        throw;
    }
}

std::vector<ImportedVersion> list_imports(const fs::path& data_directory) {
    std::vector<ImportedVersion> versions;
    const fs::path root = data_directory / "versions";
    if (!fs::exists(root)) return versions;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (!entry.is_directory()) continue;
        try {
            const Manifest manifest = read_manifest(entry.path());
            if (entry.path().filename().string() != manifest.version_id) continue;
            versions.push_back(ImportedVersion{
                manifest.version_id, manifest.source_filename, entry.path(),
                manifest.file_count, manifest.total_bytes
            });
        } catch (const std::exception&) {
            // Ignore unrelated folders and incomplete staging data.
        }
    }
    std::sort(versions.begin(), versions.end(),
        [](const ImportedVersion& a, const ImportedVersion& b) {
            return a.version_id < b.version_id;
        });
    return versions;
}

VerificationResult verify_import(const std::string& version_id, const fs::path& data_directory) {
    VerificationResult result;
    if (!valid_version_id(version_id)) {
        result.errors.push_back("Invalid version ID");
        return result;
    }

    const fs::path directory = data_directory / "versions" / version_id;
    try {
        const Manifest manifest = read_manifest(directory);
        if (manifest.version_id != version_id)
            throw std::runtime_error("Manifest version ID does not match its directory");

        std::uint64_t summed_bytes = 0;
        for (const auto& record : manifest.files) {
            const fs::path file = directory / fs::u8path(record.relative_path);
            if (!fs::is_regular_file(file)) {
                result.errors.push_back("Missing file: " + record.relative_path);
                continue;
            }
            try {
                std::uint64_t actual_size = 0;
                const std::uint32_t actual_crc = crc_file(file, actual_size);
                if (actual_size != record.size) {
                    result.errors.push_back("Size mismatch: " + record.relative_path);
                } else if (actual_crc != record.crc) {
                    result.errors.push_back("CRC mismatch: " + record.relative_path);
                } else {
                    ++result.checked_files;
                }
            } catch (const std::exception& error) {
                result.errors.push_back(error.what());
            }
            summed_bytes += record.size;
        }

        if (summed_bytes != manifest.total_bytes)
            result.errors.push_back("Manifest total byte count is inconsistent");
        if (manifest.files.size() != manifest.file_count)
            result.errors.push_back("Manifest file count is inconsistent");
    } catch (const std::exception& error) {
        result.errors.push_back(error.what());
    }

    result.ok = result.errors.empty() && result.checked_files > 0;
    return result;
}

} // namespace minecpp::launcher
