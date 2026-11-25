#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace minecpp::launcher {

struct ResourceVerificationResult {
    bool valid = false;
    std::size_t checked_files = 0;
    std::vector<std::string> errors;
};

struct PreparationResult {
    bool ready = false;
    std::filesystem::path output_directory;
    std::size_t libraries_copied = 0;
    std::size_t assets_copied = 0;
    std::uint64_t bytes_copied = 0;
    std::vector<std::string> missing;
};

[[nodiscard]] ResourceVerificationResult verify_prepared_resources(
    const std::filesystem::path& data_directory,
    const std::string& version_id);

[[nodiscard]] PreparationResult prepare_offline_resources(
    const std::filesystem::path& game_directory,
    const std::filesystem::path& data_directory,
    const std::string& version_id,
    bool overwrite = false);

} // namespace minecpp::launcher
