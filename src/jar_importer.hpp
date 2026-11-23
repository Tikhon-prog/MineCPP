#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace minecpp::launcher {

struct ImportSummary {
    std::string version_id;
    std::string source_filename;
    std::filesystem::path version_directory;
    std::size_t file_count = 0;
    std::uint64_t uncompressed_bytes = 0;
    bool copied_version_metadata = false;
};

struct VerificationResult {
    bool ok = false;
    std::size_t checked_files = 0;
    std::vector<std::string> errors;
};

struct ImportedVersion {
    std::string version_id;
    std::string source_filename;
    std::filesystem::path directory;
    std::size_t file_count = 0;
    std::uint64_t uncompressed_bytes = 0;
};

[[nodiscard]] bool valid_version_id(const std::string& value);

[[nodiscard]] ImportSummary import_jar(
    const std::filesystem::path& jar_path,
    const std::filesystem::path& data_directory,
    std::string version_id = {},
    bool overwrite = false);

[[nodiscard]] std::vector<ImportedVersion> list_imports(
    const std::filesystem::path& data_directory);

[[nodiscard]] VerificationResult verify_import(
    const std::string& version_id,
    const std::filesystem::path& data_directory);

} // namespace minecpp::launcher
