#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace minecpp {

struct ZipEntryInfo {
    std::string name;
    std::uint32_t compressed_size = 0;
    std::uint32_t uncompressed_size = 0;
    std::uint16_t method = 0;
    bool directory = false;
};

class ZipArchive {
public:
    explicit ZipArchive(std::filesystem::path file);
    [[nodiscard]] const auto& entries() const { return entries_; }
    [[nodiscard]] std::size_t extract_all(const std::filesystem::path& destination) const;
    [[nodiscard]] std::size_t count_files() const;

private:
    struct Entry : ZipEntryInfo {
        std::uint16_t made_by = 0;
        std::uint16_t flags = 0;
        std::uint32_t crc = 0;
        std::uint32_t external_attributes = 0;
        std::uint32_t local_header_offset = 0;
    };
    std::filesystem::path file_;
    std::vector<Entry> entries_;
};

} // namespace minecpp
