#pragma once

// Starts the real Minecraft: Java Edition from a version the official launcher
// has already installed (versions/<id>/<id>.json + .jar, libraries/, assets/).
//
// MineCPP does not download anything and does not sign in. It builds the same
// java command line the official launcher would, with an offline-mode player,
// which is enough for singleplayer and LAN/offline servers. Playing on online
// servers needs a Microsoft sign-in, which is not implemented.
//
// Versions that ship native libraries as separate extracted archives (before
// 1.19) are reported as unsupported; newer ones carry them on the classpath.

#include "json.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace minecpp::launcher {

struct LaunchOptions {
    std::filesystem::path minecraft_dir;
    std::string version_id;
    std::string player_name = "Player";
    std::filesystem::path game_dir;      // empty = minecraft_dir
    std::filesystem::path natives_root;  // empty = <minecraft_dir>/minecpp-natives
    std::filesystem::path java;          // empty = auto-detect
    int memory_mb = 0;                   // 0 = JVM default
    int width = 0;                       // 0 = game default
    int height = 0;
    std::vector<std::string> extra_jvm_args;
    std::vector<std::string> extra_game_args;
    std::string os_name;                 // "linux", "windows", "osx"; empty = this machine
    bool is_64bit = true;
};

struct LaunchPlan {
    std::filesystem::path java;
    std::vector<std::string> args;       // everything after the java executable
    std::filesystem::path working_dir;
    std::filesystem::path natives_dir;
    std::string main_class;
    std::string java_component;          // e.g. "java-runtime-delta", may be empty
    int java_major = 0;                  // required Java version, 0 = unknown
    std::vector<std::filesystem::path> missing_files;  // libraries or jar not on disk
    std::vector<std::string> unsupported;               // reasons this cannot be launched
};

// "linux", "windows" or "osx".
std::string host_os_name();

// 1..16 characters of letters, digits and underscore.
bool valid_player_name(const std::string& name);

// A folder name under versions/ with no path separators or "..".
bool safe_version_id(const std::string& id);

// "org.lwjgl:lwjgl:3.3.3:natives-linux" -> "org/lwjgl/lwjgl/3.3.3/lwjgl-3.3.3-natives-linux.jar".
std::string library_relative_path(const std::string& maven_name);

// Evaluates a version-file "rules" array (allow/disallow with os and features).
// An absent or empty array allows.
bool rules_allow(const Json& rules, const std::string& os_name, bool is_64bit, bool has_custom_resolution);

// Reads versions/<id>/<id>.json and merges the "inheritsFrom" chain (Fabric,
// Forge and similar profiles). Throws std::runtime_error if a file is missing,
// too large or malformed.
Json load_version_profile(const std::filesystem::path& minecraft_dir, const std::string& version_id);

// Pure: builds the command from a merged profile; touches the disk only to
// check that files exist.
LaunchPlan plan_launch(const Json& profile, const LaunchOptions& options);

// Version ids that have a JSON file, releases first and newest first.
std::vector<std::string> installed_versions(const std::filesystem::path& minecraft_dir);

// Creates the natives folder, runs java in the working directory and waits.
// Returns the game's exit code, or -1 if it could not be started.
int run_plan(const LaunchPlan& plan);

// The command as one shell-style line (for --dry-run and logs).
std::string describe_plan(const LaunchPlan& plan);

}  // namespace minecpp::launcher
