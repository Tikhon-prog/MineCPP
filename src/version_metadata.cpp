#include "version_metadata.hpp"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace minecpp::launcher {
namespace {

const json::Value& required(const json::Value& object, std::string_view key) {
    const json::Value* value = object.find(key);
    if (!value) throw std::runtime_error("Version metadata is missing required field: " + std::string(key));
    return *value;
}

std::string optional_string(const json::Value& object, std::string_view key) {
    const json::Value* value = object.find(key);
    if (!value || value->is_null()) return {};
    if (!value->is_string())
        throw std::runtime_error("Version metadata field must be a string: " + std::string(key));
    return value->as_string();
}

std::uint64_t optional_size(const json::Value& object, std::string_view key, bool& present) {
    const json::Value* value = object.find(key);
    if (!value || value->is_null()) {
        present = false;
        return 0;
    }
    if (!value->is_number()) throw std::runtime_error("Expected a numeric size in metadata: " + std::string(key));
    const double number = value->as_number();
    if (number < 0.0 || number >= static_cast<double>(std::numeric_limits<std::uint64_t>::max()) ||
        std::floor(number) != number)
        throw std::runtime_error("Invalid non-negative integer size in metadata: " + std::string(key));
    present = true;
    return static_cast<std::uint64_t>(number);
}

void validate_sha1(std::string_view hash, std::string_view field) {
    if (hash.empty()) return;
    if (hash.size() != 40)
        throw std::runtime_error("Invalid SHA-1 length in metadata field " + std::string(field));
    for (const char c : hash) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                         (c >= 'A' && c <= 'F');
        if (!hex) throw std::runtime_error("Invalid SHA-1 value in metadata field " + std::string(field));
    }
}

std::string lowercase_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

} // namespace

bool safe_relative_resource_path(std::string_view value) noexcept {
    if (value.empty() || value.size() > 1024 || value.front() == '/' ||
        value.front() == '\\' || value.find('\\') != std::string_view::npos ||
        value.find(':') != std::string_view::npos)
        return false;

    std::size_t component_start = 0;
    while (component_start <= value.size()) {
        const std::size_t separator = value.find('/', component_start);
        const std::size_t component_end =
            separator == std::string_view::npos ? value.size() : separator;
        const std::string_view component = value.substr(
            component_start, component_end - component_start);
        if (component.empty() || component == "." || component == ".." ||
            component.back() == '.' || component.back() == ' ')
            return false;
        for (const unsigned char c : component) {
            if (c < 0x20U || c == '<' || c == '>' || c == '"' ||
                c == '|' || c == '?' || c == '*')
                return false;
        }
        const std::size_t extension = component.find('.');
        const std::string_view device = component.substr(0, extension);
        const auto equals_ascii = [](std::string_view value, std::string_view expected) {
            if (value.size() != expected.size()) return false;
            for (std::size_t i = 0; i < value.size(); ++i) {
                char character = value[i];
                if (character >= 'A' && character <= 'Z')
                    character = static_cast<char>(character - 'A' + 'a');
                if (character != expected[i]) return false;
            }
            return true;
        };
        if (equals_ascii(device, "con") || equals_ascii(device, "prn") ||
            equals_ascii(device, "aux") || equals_ascii(device, "nul"))
            return false;
        if (device.size() == 4 &&
            ((equals_ascii(device.substr(0, 3), "com") ||
              equals_ascii(device.substr(0, 3), "lpt")) &&
             device[3] >= '1' && device[3] <= '9'))
            return false;
        if (separator == std::string_view::npos) break;
        component_start = separator + 1;
    }
    return true;
}

VersionMetadata parse_version_metadata(
    std::string_view json_text, std::string_view expected_version_id) {
    if (json_text.empty() || json_text.size() > 16U * 1024U * 1024U)
        throw std::runtime_error("Version JSON is empty or exceeds the 16 MiB safety limit");

    const json::Value root = json::parse(json_text);
    if (!root.is_object()) throw std::runtime_error("Version JSON root must be an object");

    VersionMetadata result;
    result.id = required(root, "id").as_string();
    result.main_class = required(root, "mainClass").as_string();
    result.inherits_from = optional_string(root, "inheritsFrom");
    if (result.id.empty() || result.id.size() > 128)
        throw std::runtime_error("Version metadata contains an invalid version ID");
    if (!expected_version_id.empty() && result.id != expected_version_id)
        throw std::runtime_error("Version JSON ID does not match the imported version directory");
    if (result.main_class.empty() || result.main_class.size() > 512)
        throw std::runtime_error("Version metadata contains an invalid mainClass");

    result.assets = optional_string(root, "assets");
    if (!result.assets.empty() &&
        (result.assets.size() > 128 || !safe_relative_resource_path(result.assets)))
        throw std::runtime_error("Version metadata contains an unsafe assets ID");

    if (const json::Value* logging = root.find("logging")) {
        if (!logging->is_object()) throw std::runtime_error("logging must be an object");
        if (const json::Value* client = logging->find("client")) {
            if (!client->is_object()) throw std::runtime_error("logging.client must be an object");
            if (const json::Value* file = client->find("file")) {
                if (!file->is_object()) throw std::runtime_error("logging.client.file must be an object");
                result.logging_config.path = required(*file, "id").as_string();
                if (!safe_relative_resource_path(result.logging_config.path))
                    throw std::runtime_error("Unsafe logging configuration path");
                result.logging_config.sha1 = lowercase_ascii(optional_string(*file, "sha1"));
                validate_sha1(result.logging_config.sha1, "logging.client.file.sha1");
                result.logging_config.size = optional_size(*file, "size", result.logging_config.has_size);
                result.logging_config.has_sha1 = !result.logging_config.sha1.empty();
                result.has_logging_config = true;
            }
        }
    }

    if (const json::Value* java = root.find("javaVersion")) {
        if (!java->is_object()) throw std::runtime_error("javaVersion must be an object");
        const json::Value& major = required(*java, "majorVersion");
        if (!major.is_number() || major.as_number() < 1.0 ||
            major.as_number() > 1000.0 || std::floor(major.as_number()) != major.as_number())
            throw std::runtime_error("javaVersion.majorVersion must be a positive integer");
        result.java_major_version = static_cast<int>(major.as_number());
    }

    if (const json::Value* index = root.find("assetIndex")) {
        if (!index->is_null()) {
            if (!index->is_object()) throw std::runtime_error("assetIndex must be an object");
            result.asset_index_id = required(*index, "id").as_string();
            result.asset_index_url = optional_string(*index, "url");
            result.asset_index_sha1 = lowercase_ascii(optional_string(*index, "sha1"));
            validate_sha1(result.asset_index_sha1, "assetIndex.sha1");
            result.asset_index_size = optional_size(*index, "size", result.has_asset_index_size);
            if (result.asset_index_id.empty() || result.asset_index_id.size() > 128 ||
                !safe_relative_resource_path(result.asset_index_id))
                throw std::runtime_error("assetIndex.id is empty, too long, or unsafe");
            result.has_asset_index = true;
        }
    }

    if (const json::Value* libraries = root.find("libraries")) {
        if (!libraries->is_array()) throw std::runtime_error("libraries must be an array");
        const auto& entries = libraries->as_array();
        if (entries.size() > 10000U) throw std::runtime_error("Version metadata contains too many libraries");
        result.declared_library_count = entries.size();

        std::set<std::string> artifact_paths;
        for (const json::Value& library : entries) {
            if (!library.is_object()) throw std::runtime_error("Each library entry must be an object");
            if (const json::Value* rules = library.find("rules")) {
                if (!rules->is_array()) throw std::runtime_error("Library rules must be an array");
                result.has_unresolved_library_requirements = true;
            }
            if (library.find("natives"))
                result.has_unresolved_library_requirements = true;
            const json::Value* downloads = library.find("downloads");
            if (!downloads || downloads->is_null()) {
                result.has_unresolved_library_requirements = true;
                continue;
            }
            if (!downloads->is_object()) throw std::runtime_error("Library downloads must be an object");
            if (downloads->find("classifiers"))
                result.has_unresolved_library_requirements = true;
            const json::Value* artifact = downloads->find("artifact");
            if (!artifact || artifact->is_null()) {
                result.has_unresolved_library_requirements = true;
                continue;
            }
            if (!artifact->is_object()) throw std::runtime_error("Library artifact must be an object");

            LibraryArtifact parsed;
            parsed.path = required(*artifact, "path").as_string();
            if (!safe_relative_resource_path(parsed.path))
                throw std::runtime_error("Unsafe library artifact path: " + parsed.path);
            parsed.url = optional_string(*artifact, "url");
            parsed.sha1 = lowercase_ascii(optional_string(*artifact, "sha1"));
            validate_sha1(parsed.sha1, "libraries.downloads.artifact.sha1");
            parsed.size = optional_size(*artifact, "size", parsed.has_size);
            parsed.has_sha1 = !parsed.sha1.empty();
            const std::string collision_key = lowercase_ascii(parsed.path);
            if (!artifact_paths.insert(collision_key).second)
                throw std::runtime_error("Duplicate or case-colliding library artifact path: " + parsed.path);
            result.artifacts.push_back(std::move(parsed));
        }
    }

    return result;
}

} // namespace minecpp::launcher
