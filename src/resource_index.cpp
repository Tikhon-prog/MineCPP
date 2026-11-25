#include "resource_index.hpp"

#include "json.hpp"
#include "version_metadata.hpp"

#include <algorithm>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace minecpp::launcher {
namespace {

std::string optional_string(const json::Value& value, std::string_view key) {
    const json::Value* field = value.find(key);
    if (!field || field->is_null()) return {};
    if (!field->is_string())
        throw std::runtime_error("Asset index field must be a string: " + std::string(key));
    return field->as_string();
}

bool optional_bool(const json::Value& value, std::string_view key) {
    const json::Value* field = value.find(key);
    if (!field || field->is_null()) return false;
    if (!field->is_bool())
        throw std::runtime_error("Asset index field must be a boolean: " + std::string(key));
    return field->as_bool();
}

std::uint64_t required_size(const json::Value& value) {
    if (!value.is_number()) throw std::runtime_error("Asset object size must be numeric");
    const double number = value.as_number();
    if (number < 0.0 ||
        number >= static_cast<double>(std::numeric_limits<std::uint64_t>::max()) ||
        std::floor(number) != number)
        throw std::runtime_error("Asset object size must be a non-negative integer");
    return static_cast<std::uint64_t>(number);
}

std::string lowercase(std::string value) {
    for (char& c : value)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return value;
}

bool valid_sha1(std::string_view hash) {
    if (hash.size() != 40) return false;
    for (const char c : hash) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

} // namespace

AssetIndex parse_asset_index(std::string_view json_text, std::string_view expected_id) {
    if (json_text.empty() || json_text.size() > 16U * 1024U * 1024U)
        throw std::runtime_error("Asset index is empty or exceeds the 16 MiB safety limit");
    const json::Value root = json::parse(json_text);
    if (!root.is_object()) throw std::runtime_error("Asset index root must be an object");

    AssetIndex result;
    result.id = optional_string(root, "id");
    // Vanilla asset-index documents commonly omit their own ID.
    if (result.id.empty()) result.id = std::string(expected_id);
    if (result.id.empty() || result.id.size() > 128 ||
        !safe_relative_resource_path(result.id))
        throw std::runtime_error("Asset index contains an invalid ID");
    if (!expected_id.empty() && result.id != expected_id)
        throw std::runtime_error("Asset index ID does not match version metadata");
    result.virtual_assets = optional_bool(root, "virtual");
    result.map_to_resources = optional_bool(root, "map_to_resources");

    const json::Value* objects = root.find("objects");
    if (!objects || !objects->is_object())
        throw std::runtime_error("Asset index objects must be an object");
    const auto& entries = objects->as_object();
    if (entries.size() > 1000000U)
        throw std::runtime_error("Asset index contains too many objects");

    result.objects.reserve(entries.size());
    std::set<std::string> portable_names;
    for (const auto& [logical_name, descriptor] : entries) {
        if (!safe_relative_resource_path(logical_name))
            throw std::runtime_error("Unsafe logical asset path: " + logical_name);
        std::string collision_key = lowercase(logical_name);
        if (!portable_names.insert(collision_key).second)
            throw std::runtime_error("Duplicate/case-colliding asset path: " + logical_name);
        if (!descriptor.is_object())
            throw std::runtime_error("Asset descriptor must be an object: " + logical_name);

        AssetObject object;
        object.logical_name = logical_name;
        object.sha1 = optional_string(descriptor, "hash");
        if (!valid_sha1(object.sha1))
            throw std::runtime_error("Invalid SHA-1 in asset descriptor: " + logical_name);
        object.sha1 = lowercase(std::move(object.sha1));
        const json::Value* size = descriptor.find("size");
        if (!size) throw std::runtime_error("Asset descriptor is missing size: " + logical_name);
        object.size = required_size(*size);

        constexpr std::uint64_t total_limit = 16ULL * 1024ULL * 1024ULL * 1024ULL;
        constexpr std::uint64_t file_limit = 1024ULL * 1024ULL * 1024ULL;
        if (object.size > file_limit)
            throw std::runtime_error("Asset object exceeds the 1 GiB per-file limit: " + logical_name);
        if (object.size > total_limit || result.total_bytes > total_limit - object.size)
            throw std::runtime_error("Asset index exceeds the 16 GiB total resource limit");
        result.total_bytes += object.size;
        result.objects.push_back(std::move(object));
    }
    return result;
}

} // namespace minecpp::launcher
