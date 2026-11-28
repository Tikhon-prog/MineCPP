#pragma once

// Launcher settings, stored as `key = value` in minecpp-launcher.cfg (same
// format and warnings as the game's settings file).

#include "settings.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace minecpp {

struct LauncherSettings {
    std::string minecraft_dir;   // empty = default Minecraft folder of this OS
    std::string version;         // empty = newest installed release
    std::string player_name = "Player";
    std::string java;            // empty = bundled runtime, JAVA_HOME, then PATH
    int memory_mb = 0;           // 0 = Java default; otherwise 512..65536
    std::string game_dir;        // empty = Minecraft folder
    int width = 0;               // 0 = game default
    int height = 0;
    std::string jvm_args;        // extra JVM arguments, space separated
    std::string game_args;       // extra game arguments, space separated

    void clamp() {
        if (memory_mb != 0) memory_mb = std::clamp(memory_mb, 512, 65536);
        width = std::clamp(width, 0, 7680);
        height = std::clamp(height, 0, 4320);
        if (width == 0 || height == 0) width = height = 0;  // both or neither
    }
};

struct LauncherSettingsLoad {
    LauncherSettings settings;
    std::vector<std::string> warnings;
};

inline std::vector<std::string> split_arguments(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream stream(text);
    std::string word;
    while (stream >> word) out.push_back(word);
    return out;
}

inline LauncherSettingsLoad parse_launcher_settings(std::string_view text) {
    LauncherSettingsLoad result;
    std::istringstream stream{std::string(text)};
    std::string line;
    int line_number = 0;
    while (std::getline(stream, line)) {
        ++line_number;
        const std::string trimmed = detail::trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        auto warn = [&](const std::string& message) {
            result.warnings.push_back("line " + std::to_string(line_number) + ": " + message);
        };
        const std::size_t equals = trimmed.find('=');
        if (equals == std::string::npos) {
            warn("expected key = value");
            continue;
        }
        const std::string key = detail::lower(detail::trim(std::string_view(trimmed).substr(0, equals)));
        std::string value = detail::trim(std::string_view(trimmed).substr(equals + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        LauncherSettings& s = result.settings;
        bool ok = true;
        if (key == "minecraft_dir") s.minecraft_dir = value;
        else if (key == "version") s.version = value;
        else if (key == "player_name") s.player_name = value;
        else if (key == "java") s.java = value;
        else if (key == "memory_mb") ok = detail::parse_int(value, s.memory_mb);
        else if (key == "game_dir") s.game_dir = value;
        else if (key == "width") ok = detail::parse_int(value, s.width);
        else if (key == "height") ok = detail::parse_int(value, s.height);
        else if (key == "jvm_args") s.jvm_args = value;
        else if (key == "game_args") s.game_args = value;
        else {
            warn("unknown setting '" + key + "'");
            continue;
        }
        if (!ok) warn("bad value '" + value + "' for " + key);
    }
    result.settings.clamp();
    return result;
}

inline std::string format_launcher_settings(const LauncherSettings& s) {
    std::ostringstream out;
    out << "# MineCPP launcher settings. Empty value = automatic.\n"
        << "minecraft_dir = " << s.minecraft_dir << "\n"
        << "version = " << s.version << "\n"
        << "player_name = " << s.player_name << "\n"
        << "java = " << s.java << "\n"
        << "memory_mb = " << s.memory_mb << "\n"
        << "game_dir = " << s.game_dir << "\n"
        << "width = " << s.width << "\n"
        << "height = " << s.height << "\n"
        << "jvm_args = " << s.jvm_args << "\n"
        << "game_args = " << s.game_args << "\n";
    return out.str();
}

inline LauncherSettingsLoad load_launcher_settings(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) return {};
    std::ostringstream text;
    text << file.rdbuf();
    return parse_launcher_settings(text.str());
}

inline bool save_launcher_settings(const std::filesystem::path& path, const LauncherSettings& settings) {
    std::ofstream file(path, std::ios::trunc);
    if (!file) return false;
    file << format_launcher_settings(settings);
    return static_cast<bool>(file);
}

}  // namespace minecpp
