#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace minecpp::launcher {

struct LibraryArtifact {
    std::string path;
    std::string url;
    std::string sha1;
    std::uint64_t size = 0;
    bool has_size = false;
    bool has_sha1 = false;
};

struct VersionMetadata {
    std::string id;
    std::string main_class;
    std::string inherits_from;
    std::string assets;
    std::string asset_index_id;
    std::string asset_index_url;
    std::string asset_index_sha1;
    std::uint64_t asset_index_size = 0;
    bool has_asset_index = false;
    bool has_asset_index_size = false;
    int java_major_version = 0;
    std::size_t declared_library_count = 0;
    bool has_unresolved_library_requirements = false;
    LibraryArtifact logging_config;
    bool has_logging_config = false;
    std::vector<LibraryArtifact> artifacts;
};

[[nodiscard]] VersionMetadata parse_version_metadata(
    std::string_view json_text, std::string_view expected_version_id = {});
[[nodiscard]] bool safe_relative_resource_path(std::string_view value) noexcept;

} // namespace minecpp::launcher
