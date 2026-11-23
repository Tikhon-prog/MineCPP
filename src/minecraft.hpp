#pragma once

#include <filesystem>
#include <string>

namespace minecpp {
std::filesystem::path default_minecraft_dir();
std::filesystem::path default_data_dir();
void list_versions(const std::filesystem::path& minecraft_dir);
void list_profiles(const std::filesystem::path& data_dir);
void inspect_jar(const std::filesystem::path& jar);
void import_version(const std::string& id, const std::filesystem::path& minecraft_dir,
                    const std::filesystem::path& data_dir,
                    const std::filesystem::path& jar_override = {},
                    const std::filesystem::path& metadata_override = {});
int launch_version(const std::string& id, const std::string& username,
                   const std::filesystem::path& minecraft_dir,
                   const std::filesystem::path& data_dir, bool dry_run);
}
