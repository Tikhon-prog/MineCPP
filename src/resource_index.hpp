#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace minecpp::launcher {

struct AssetObject {
    std::string logical_name;
    std::string sha1;
    std::uint64_t size = 0;
};

struct AssetIndex {
    std::string id;
    bool virtual_assets = false;
    bool map_to_resources = false;
    std::vector<AssetObject> objects;
    std::uint64_t total_bytes = 0;
};

[[nodiscard]] AssetIndex parse_asset_index(
    std::string_view json_text, std::string_view expected_id = {});

} // namespace minecpp::launcher
