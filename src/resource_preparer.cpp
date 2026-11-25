#include "resource_preparer.hpp"

#include "json.hpp"
#include "resource_index.hpp"
#include "sha1.hpp"
#include "version_metadata.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <tuple>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace minecpp::launcher {
namespace fs = std::filesystem;
namespace {

bool is_safe_regular_file(const fs::path& root, std::string_view relative) {
    if (!safe_relative_resource_path(relative)) return false;
    std::error_code error;
    const fs::file_status root_status = fs::symlink_status(root, error);
    if (error || fs::is_symlink(root_status) || !fs::is_directory(root_status)) return false;

    fs::path current = root;
    const fs::path relative_path = fs::u8path(relative.begin(), relative.end());
    for (auto component = relative_path.begin(); component != relative_path.end(); ++component) {
        current /= *component;
        const fs::file_status status = fs::symlink_status(current, error);
        if (error || fs::is_symlink(status)) return false;
        const bool last = std::next(component) == relative_path.end();
        if (last) return fs::is_regular_file(status);
        if (!fs::is_directory(status)) return false;
    }
    return false;
}

void ensure_directory(const fs::path& path) {
    std::error_code error;
    const fs::file_status before = fs::symlink_status(path, error);
    if (!error && fs::exists(before)) {
        if (fs::is_symlink(before) || !fs::is_directory(before))
            throw std::runtime_error("Refusing a non-directory or symbolic-link output path: " + path.string());
        return;
    }
    if (error && error != std::errc::no_such_file_or_directory)
        throw std::runtime_error("Cannot inspect output directory: " + error.message());
    fs::create_directories(path);
    const fs::file_status after = fs::symlink_status(path, error);
    if (error || fs::is_symlink(after) || !fs::is_directory(after))
        throw std::runtime_error("Output directory could not be created safely: " + path.string());
}

std::string read_limited_file(const fs::path& path, std::uint64_t limit) {
    const std::uint64_t size = fs::file_size(path);
    if (size == 0 || size > limit)
        throw std::runtime_error("Metadata file is empty or exceeds its safety limit: " + path.string());
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open metadata file: " + path.string());
    std::string contents(static_cast<std::size_t>(size), '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!input || input.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("Metadata file changed or could not be read completely: " + path.string());
    return contents;
}

void validate_file(
    const fs::path& root, const std::string& relative,
    std::uint64_t expected_size, const std::string& expected_sha1) {
    if (!is_safe_regular_file(root, relative))
        throw std::runtime_error("Missing or unsafe local resource: " + relative);
    const fs::path path = root / fs::u8path(relative.begin(), relative.end());
    if (fs::file_size(path) != expected_size)
        throw std::runtime_error("Resource size mismatch: " + relative);
    if (!expected_sha1.empty() && sha1_file(path) != expected_sha1)
        throw std::runtime_error("Resource SHA-1 mismatch: " + relative);
}

void copy_resource(const fs::path& source, const fs::path& destination) {
    fs::create_directories(destination.parent_path());
    fs::copy_file(source, destination, fs::copy_options::none);
}

void link_or_copy_resource(const fs::path& source, const fs::path& destination) {
    fs::create_directories(destination.parent_path());
    std::error_code link_error;
    fs::create_hard_link(source, destination, link_error);
    if (link_error) fs::copy_file(source, destination, fs::copy_options::none);
}

void write_manifest(
    const fs::path& path, const std::string& version_id, const fs::path& staged_root,
    const AssetIndex& assets, const std::vector<LibraryArtifact>& libraries,
    const LibraryArtifact* logging_config) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot create resource manifest: " + path.string());
    const fs::path metadata_path = staged_root / "metadata" / "version.json";
    const fs::path index_path = staged_root / "assets" / "indexes" / (assets.id + ".json");
    output << "MINECPP-RESOURCES-1\n";
    output << "VERSION\t" << version_id << '\n';
    output << "METADATA\t" << sha1_file(metadata_path) << '\t'
           << fs::file_size(metadata_path) << '\n';
    output << "INDEX\t" << assets.id << '\t' << sha1_file(index_path) << '\t'
           << fs::file_size(index_path) << '\t' << (assets.virtual_assets ? 1 : 0)
           << '\t' << (assets.map_to_resources ? 1 : 0) << '\n';
    for (const auto& library : libraries)
        output << "LIB\t" << library.sha1 << '\t' << library.size << '\t' << library.path << '\n';
    for (const auto& asset : assets.objects)
        output << "ASSET\t" << asset.sha1 << '\t' << asset.size << '\t'
               << asset.logical_name << '\n';
    if (logging_config)
        output << "LOG\t" << logging_config->sha1 << '\t' << logging_config->size
               << '\t' << logging_config->path << '\n';
    if (!output) throw std::runtime_error("Failed writing resource manifest");
}

} // namespace


ResourceVerificationResult verify_prepared_resources(
    const fs::path& data_directory, const std::string& version_id) {
    ResourceVerificationResult result;
    if (version_id.empty() || version_id.size() > 128 ||
        version_id.find('/') != std::string::npos ||
        !safe_relative_resource_path(version_id)) {
        result.errors.push_back("Invalid version ID");
        return result;
    }

    try {
        const fs::path prepared_root = data_directory / "prepared" / version_id;
        if (!is_safe_regular_file(prepared_root, "resources.mcpp")) {
            result.errors.push_back("Prepared resource manifest is missing or unsafe");
            return result;
        }
        const std::string manifest = read_limited_file(
            prepared_root / "resources.mcpp", 64U * 1024U * 1024U);

        std::vector<std::string_view> lines;
        std::size_t start = 0;
        while (start < manifest.size()) {
            const std::size_t end = manifest.find('\n', start);
            const std::size_t length = (end == std::string::npos ? manifest.size() : end) - start;
            if (length > 4096U)
                throw std::runtime_error("Resource manifest contains an overlong line");
            lines.emplace_back(manifest.data() + start, length);
            if (end == std::string::npos) break;
            start = end + 1U;
        }
        if (lines.empty() || lines.front() != "MINECPP-RESOURCES-1")
            throw std::runtime_error("Unsupported resource manifest format");

        auto fields = [](std::string_view line) {
            std::vector<std::string_view> result_fields;
            std::size_t offset = 0;
            while (true) {
                const std::size_t tab = line.find('\t', offset);
                result_fields.push_back(line.substr(
                    offset, tab == std::string_view::npos ? line.size() - offset : tab - offset));
                if (tab == std::string_view::npos) break;
                offset = tab + 1U;
            }
            return result_fields;
        };
        auto parse_size = [](std::string_view text) -> std::uint64_t {
            std::uint64_t value = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                throw std::runtime_error("Resource manifest has an invalid file size");
            return value;
        };
        auto parse_flag = [](std::string_view text) -> bool {
            if (text == "0") return false;
            if (text == "1") return true;
            throw std::runtime_error("Resource manifest has an invalid boolean flag");
        };
        auto validate_hash = [](std::string_view hash) {
            if (hash.size() != 40U) throw std::runtime_error("Invalid SHA-1 in resource manifest");
            for (const char c : hash)
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                    throw std::runtime_error("Invalid SHA-1 in resource manifest");
        };
        auto check_file = [&](const std::string& relative, std::uint64_t expected_size,
                              const std::string& expected_hash) {
            if (!safe_relative_resource_path(relative) ||
                !is_safe_regular_file(prepared_root, relative)) {
                result.errors.push_back("Missing or unsafe file: " + relative);
                return false;
            }
            const fs::path file = prepared_root / fs::u8path(relative.begin(), relative.end());
            if (fs::file_size(file) != expected_size) {
                result.errors.push_back("Size mismatch: " + relative);
                return false;
            }
            if (sha1_file(file) != expected_hash) {
                result.errors.push_back("SHA-1 mismatch: " + relative);
                return false;
            }
            ++result.checked_files;
            return true;
        };

        bool version_seen = false;
        bool metadata_seen = false;
        bool index_seen = false;
        std::string manifest_version;
        std::string index_id;
        bool virtual_assets = false;
        bool map_to_resources = false;
        std::vector<std::tuple<std::string, std::uint64_t, std::string>> assets;
        std::vector<std::tuple<std::string, std::uint64_t, std::string>> libraries;
        std::optional<std::tuple<std::string, std::uint64_t, std::string>> logging;

        for (std::size_t i = 1; i < lines.size(); ++i) {
            if (lines[i].empty()) continue;
            const auto parts = fields(lines[i]);
            if (parts[0] == "VERSION" && parts.size() == 2U && !version_seen) {
                manifest_version = std::string(parts[1]);
                version_seen = true;
            } else if (parts[0] == "METADATA" && parts.size() == 3U && !metadata_seen) {
                validate_hash(parts[1]);
                metadata_seen = true;
                (void)check_file("metadata/version.json", parse_size(parts[2]),
                                 std::string(parts[1]));
            } else if (parts[0] == "INDEX" && parts.size() == 6U && !index_seen) {
                index_id = std::string(parts[1]);
                if (!safe_relative_resource_path(index_id))
                    throw std::runtime_error("Invalid asset-index ID in resource manifest");
                const std::string hash(parts[2]);
                validate_hash(hash);
                const std::uint64_t size = parse_size(parts[3]);
                virtual_assets = parse_flag(parts[4]);
                map_to_resources = parse_flag(parts[5]);
                index_seen = true;
                (void)check_file("assets/indexes/" + index_id + ".json", size, hash);
            } else if (parts[0] == "LIB" && parts.size() == 4U) {
                validate_hash(parts[1]);
                libraries.emplace_back(std::string(parts[3]), parse_size(parts[2]),
                                       std::string(parts[1]));
            } else if (parts[0] == "ASSET" && parts.size() == 4U) {
                validate_hash(parts[1]);
                if (!safe_relative_resource_path(parts[3]))
                    throw std::runtime_error("Invalid asset path in resource manifest");
                assets.emplace_back(std::string(parts[3]), parse_size(parts[2]),
                                    std::string(parts[1]));
            } else if (parts[0] == "LOG" && parts.size() == 4U && !logging) {
                validate_hash(parts[1]);
                if (!safe_relative_resource_path(parts[3]))
                    throw std::runtime_error("Invalid logging path in resource manifest");
                logging.emplace(std::string(parts[3]), parse_size(parts[2]),
                                std::string(parts[1]));
            } else {
                throw std::runtime_error("Unknown, duplicate, or malformed resource manifest entry");
            }
        }

        if (!version_seen || manifest_version != version_id || !metadata_seen || !index_seen)
            throw std::runtime_error("Resource manifest is incomplete or belongs to another version");

        for (const auto& [relative, size, hash] : libraries)
            (void)check_file("libraries/" + relative, size, hash);

        for (const auto& [logical, size, hash] : assets) {
            const std::string object = "assets/objects/" + hash.substr(0, 2) + "/" + hash;
            (void)check_file(object, size, hash);
            if (virtual_assets)
                (void)check_file("assets/virtual/" + index_id + "/" + logical, size, hash);
            if (map_to_resources)
                (void)check_file("resources/" + logical, size, hash);
        }

        if (logging) {
            const auto& [relative, size, hash] = *logging;
            (void)check_file("assets/log_configs/" + relative, size, hash);
        }

        result.valid = result.errors.empty();
    } catch (const std::exception& error) {
        result.errors.push_back(error.what());
        result.valid = false;
    }
    return result;
}


PreparationResult prepare_offline_resources(
    const fs::path& game_directory, const fs::path& data_directory,
    const std::string& version_id, bool overwrite) {
    if (version_id.empty() || version_id.size() > 128 || version_id.find("/") != std::string::npos || !safe_relative_resource_path(version_id))
        throw std::runtime_error("Invalid version ID");

    PreparationResult result;
    const fs::path metadata_relative = fs::path("versions") / version_id / "metadata" / "version.json";
    const std::string metadata_name = metadata_relative.generic_string();
    if (!is_safe_regular_file(data_directory, metadata_name))
        throw std::runtime_error("Import the JAR with an adjacent version JSON before preparing resources");

    const std::string metadata_text = read_limited_file(data_directory / metadata_relative, 16U * 1024U * 1024U);
    const VersionMetadata metadata = parse_version_metadata(metadata_text, version_id);
    if (!metadata.inherits_from.empty())
        result.missing.push_back("inherited version metadata is not resolved: " + metadata.inherits_from);
    if (metadata.has_unresolved_library_requirements)
        result.missing.push_back("library OS rules, Maven-only entries, or native classifiers are not fully resolved");

    std::error_code error;
    const fs::file_status game_status = fs::symlink_status(game_directory, error);
    if (error || fs::is_symlink(game_status) || !fs::is_directory(game_status))
        throw std::runtime_error("Minecraft game directory must be a regular, non-symlink directory");

    const fs::path assets_root = game_directory / "assets";
    const fs::path libraries_root = game_directory / "libraries";
    const fs::path index_relative = fs::path("indexes") / (metadata.asset_index_id + ".json");
    std::string index_text;
    AssetIndex asset_index;
    if (!metadata.has_asset_index) {
        result.missing.push_back("version JSON has no assetIndex declaration");
    } else if (!safe_relative_resource_path(metadata.asset_index_id + ".json") ||
               !is_safe_regular_file(assets_root, index_relative.generic_string())) {
        result.missing.push_back("assets/" + index_relative.generic_string());
    } else {
        const fs::path index_path = assets_root / index_relative;
        const std::uint64_t index_size = fs::file_size(index_path);
        if (index_size == 0 || index_size > 16U * 1024U * 1024U)
            throw std::runtime_error("Asset index is empty or exceeds the 16 MiB safety limit");
        if (metadata.has_asset_index_size && index_size != metadata.asset_index_size)
            throw std::runtime_error("Asset index size mismatch: " + index_relative.generic_string());
        if (!metadata.asset_index_sha1.empty() &&
            sha1_file(index_path) != metadata.asset_index_sha1)
            throw std::runtime_error("Asset index SHA-1 mismatch: " + index_relative.generic_string());
        index_text = read_limited_file(index_path, 16U * 1024U * 1024U);
        asset_index = parse_asset_index(index_text, metadata.asset_index_id);
    }

    constexpr std::uint64_t preparation_limit = 16ULL * 1024ULL * 1024ULL * 1024ULL;
    std::uint64_t total_bytes = fs::file_size(data_directory / metadata_relative);
    if (metadata.has_asset_index && !asset_index.id.empty()) {
        const std::uint64_t index_size = fs::file_size(assets_root / index_relative);
        if (index_size > preparation_limit - total_bytes)
            throw std::runtime_error("Prepared resources exceed the 16 GiB safety limit");
        total_bytes += index_size;
    }

    std::vector<LibraryArtifact> resolved_libraries;
    resolved_libraries.reserve(metadata.artifacts.size());
    for (const auto& library : metadata.artifacts) {
        if (!is_safe_regular_file(libraries_root, library.path)) {
            result.missing.push_back("libraries/" + library.path);
            continue;
        }
        const fs::path source = libraries_root / fs::u8path(library.path.begin(), library.path.end());
        const std::uint64_t actual_size = fs::file_size(source);
        if (actual_size > 16ULL * 1024ULL * 1024ULL * 1024ULL)
            throw std::runtime_error("Library exceeds the 16 GiB preparation limit: " + library.path);
        if (library.has_size && actual_size != library.size)
            throw std::runtime_error("Library size mismatch: " + library.path);
        const std::string actual_sha1 = sha1_file(source);
        if (library.has_sha1 && actual_sha1 != library.sha1)
            throw std::runtime_error("Library SHA-1 mismatch: " + library.path);
        if (actual_size > preparation_limit || total_bytes > preparation_limit - actual_size)
            throw std::runtime_error("Prepared resources exceed the 16 GiB safety limit");
        total_bytes += actual_size;
        LibraryArtifact resolved = library;
        resolved.size = actual_size;
        resolved.has_size = true;
        resolved.sha1 = actual_sha1;
        resolved.has_sha1 = true;
        resolved_libraries.push_back(std::move(resolved));
    }

    LibraryArtifact resolved_logging;
    bool has_resolved_logging = false;
    if (metadata.has_logging_config) {
        const std::string logging_relative = "log_configs/" + metadata.logging_config.path;
        if (!is_safe_regular_file(assets_root, logging_relative)) {
            result.missing.push_back("assets/" + logging_relative);
        } else {
            const fs::path logging_path = assets_root / fs::u8path(logging_relative.begin(), logging_relative.end());
            const std::uint64_t actual_size = fs::file_size(logging_path);
            if (actual_size > 16ULL * 1024ULL * 1024ULL * 1024ULL)
                throw std::runtime_error("Logging configuration exceeds the preparation size limit");
            if (metadata.logging_config.has_size && actual_size != metadata.logging_config.size)
                throw std::runtime_error("Logging configuration size mismatch: " + metadata.logging_config.path);
            const std::string actual_sha1 = sha1_file(logging_path);
            if (metadata.logging_config.has_sha1 && actual_sha1 != metadata.logging_config.sha1)
                throw std::runtime_error("Logging configuration SHA-1 mismatch: " + metadata.logging_config.path);
            if (total_bytes > 16ULL * 1024ULL * 1024ULL * 1024ULL - actual_size)
                throw std::runtime_error("Prepared resources exceed the 16 GiB safety limit");
            total_bytes += actual_size;
            resolved_logging = metadata.logging_config;
            resolved_logging.size = actual_size;
            resolved_logging.has_size = true;
            resolved_logging.sha1 = actual_sha1;
            resolved_logging.has_sha1 = true;
            has_resolved_logging = true;
        }
    }

    if (metadata.has_asset_index && !asset_index.id.empty()) {

        for (const auto& asset : asset_index.objects) {
            const std::string relative = asset.sha1.substr(0, 2) + "/" + asset.sha1;
            if (!is_safe_regular_file(assets_root / "objects", relative)) {
                result.missing.push_back("assets/objects/" + relative);
                continue;
            }
            validate_file(assets_root / "objects", relative, asset.size, asset.sha1);
            const std::uint64_t limit = 16ULL * 1024ULL * 1024ULL * 1024ULL;
            const std::uint64_t mapped_copies =
                static_cast<std::uint64_t>(asset_index.virtual_assets) +
                static_cast<std::uint64_t>(asset_index.map_to_resources);
            const std::uint64_t multiplier = 1ULL + mapped_copies;
            if (asset.size > limit / multiplier || total_bytes > limit - asset.size * multiplier)
                throw std::runtime_error("Prepared resources exceed the 16 GiB safety limit");
            total_bytes += asset.size * multiplier;
        }
    }

    if (!result.missing.empty()) return result;

    const fs::path prepared_root = data_directory / "prepared";
    ensure_directory(data_directory);
    ensure_directory(prepared_root);
    const fs::path final_directory = prepared_root / version_id;
    const fs::path backup_directory = prepared_root / ("." + version_id + ".backup");
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path staging_directory =
        prepared_root / ("." + version_id + ".staging-" + std::to_string(timestamp));

    const fs::file_status final_status = fs::symlink_status(final_directory, error);
    const bool has_previous = !error && fs::exists(final_status);
    if (error && error != std::errc::no_such_file_or_directory)
        throw std::runtime_error("Cannot inspect existing prepared resources: " + error.message());
    if (has_previous && (fs::is_symlink(final_status) || !fs::is_directory(final_status)))
        throw std::runtime_error("Refusing to replace unsafe prepared-resource path");
    if (has_previous && !overwrite)
        throw std::runtime_error("Prepared resources already exist; pass --force to replace them");

    error.clear();
    const fs::file_status backup_status = fs::symlink_status(backup_directory, error);
    if ((!error && fs::exists(backup_status)) ||
        (error && error != std::errc::no_such_file_or_directory))
        throw std::runtime_error("An earlier resource-preparation backup exists; recover it before retrying");

    if (!fs::create_directory(staging_directory))
        throw std::runtime_error("Could not reserve a staging directory for resources");

    bool backed_up = false;
    try {
        copy_resource(data_directory / metadata_relative,
                      staging_directory / "metadata" / "version.json");
        copy_resource(assets_root / index_relative,
                      staging_directory / "assets" / index_relative);
        result.bytes_copied += fs::file_size(data_directory / metadata_relative);
        result.bytes_copied += fs::file_size(assets_root / index_relative);

        for (const auto& library : resolved_libraries) {
            const fs::path relative = fs::u8path(library.path.begin(), library.path.end());
            copy_resource(libraries_root / relative, staging_directory / "libraries" / relative);
            ++result.libraries_copied;
            result.bytes_copied += fs::file_size(libraries_root / relative);
        }

        if (has_resolved_logging) {
            const fs::path relative = fs::u8path(resolved_logging.path.begin(), resolved_logging.path.end());
            copy_resource(assets_root / "log_configs" / relative,
                          staging_directory / "assets" / "log_configs" / relative);
            result.bytes_copied += resolved_logging.size;
        }

        for (const auto& asset : asset_index.objects) {
            const fs::path relative = fs::path(asset.sha1.substr(0, 2)) / asset.sha1;
            const fs::path source = assets_root / "objects" / relative;
            const fs::path destination = staging_directory / "assets" / "objects" / relative;
            copy_resource(source, destination);
            ++result.assets_copied;
            result.bytes_copied += asset.size;
            const fs::path logical = fs::u8path(asset.logical_name.begin(), asset.logical_name.end());
            if (asset_index.virtual_assets) {
                link_or_copy_resource(destination, staging_directory / "assets" / "virtual" / asset_index.id / logical);
                result.bytes_copied += asset.size;
            }
            if (asset_index.map_to_resources) {
                link_or_copy_resource(destination, staging_directory / "resources" / logical);
                result.bytes_copied += asset.size;
            }
        }
        write_manifest(staging_directory / "resources.mcpp", version_id, staging_directory,
                       asset_index, resolved_libraries,
                       has_resolved_logging ? &resolved_logging : nullptr);

        if (has_previous) {
            fs::rename(final_directory, backup_directory);
            backed_up = true;
        }
        try {
            fs::rename(staging_directory, final_directory);
        } catch (...) {
            if (backed_up && !fs::exists(final_directory)) {
                std::error_code restore_error;
                fs::rename(backup_directory, final_directory, restore_error);
            }
            throw;
        }
        if (backed_up) {
            std::error_code cleanup_error;
            fs::remove_all(backup_directory, cleanup_error);
        }

        result.ready = true;
        result.output_directory = final_directory;
        return result;
    } catch (...) {
        std::error_code cleanup_error;
        fs::remove_all(staging_directory, cleanup_error);
        throw;
    }
}

} // namespace minecpp::launcher
