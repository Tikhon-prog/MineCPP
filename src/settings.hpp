#pragma once

// User settings stored in a plain `key = value` file (minecpp-settings.cfg).
// Lines starting with '#' are comments. Unknown keys and bad values are
// reported as warnings and skipped; out-of-range numbers are clamped.

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace minecpp {

struct Settings {
    int render_distance = 4;          // chunks, 2..8
    float fov = 70.0F;                // degrees, 30..110
    float mouse_sensitivity = 0.11F;  // degrees per pixel, 0.01..1.0
    bool vsync = true;
    bool fullscreen = false;
    int window_width = 1280;
    int window_height = 720;
    bool textures = true;             // use textures from the Minecraft client jar
    std::string minecraft_dir;        // empty = default install location
    std::string jar;                  // empty = newest jar found in minecraft_dir
    std::string data_dir = "minecpp-data";

    void clamp() {
        render_distance = std::clamp(render_distance, 2, 8);
        fov = std::clamp(fov, 30.0F, 110.0F);
        mouse_sensitivity = std::clamp(mouse_sensitivity, 0.01F, 1.0F);
        window_width = std::clamp(window_width, 320, 7680);
        window_height = std::clamp(window_height, 240, 4320);
    }
};

struct SettingsLoad {
    Settings settings;
    std::vector<std::string> warnings;
};

namespace detail {

inline std::string trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
    return std::string(text.substr(begin, end - begin));
}

inline std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

inline bool parse_bool(const std::string& value, bool& out) {
    const std::string v = lower(value);
    if (v == "true" || v == "1" || v == "yes" || v == "on") { out = true; return true; }
    if (v == "false" || v == "0" || v == "no" || v == "off") { out = false; return true; }
    return false;
}

inline bool parse_int(const std::string& value, int& out) {
    try {
        std::size_t used = 0;
        const int parsed = std::stoi(value, &used);
        if (used != value.size()) return false;
        out = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

inline bool parse_float(const std::string& value, float& out) {
    try {
        std::size_t used = 0;
        const float parsed = std::stof(value, &used);
        if (used != value.size()) return false;
        out = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace detail

inline SettingsLoad parse_settings(std::string_view text) {
    SettingsLoad result;
    std::istringstream stream{std::string(text)};
    std::string line;
    int line_number = 0;
    while (std::getline(stream, line)) {
        ++line_number;
        const std::string trimmed = detail::trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        const std::size_t equals = trimmed.find('=');
        auto warn = [&](const std::string& message) {
            result.warnings.push_back("line " + std::to_string(line_number) + ": " + message);
        };
        if (equals == std::string::npos) {
            warn("expected key = value");
            continue;
        }
        const std::string key = detail::lower(detail::trim(std::string_view(trimmed).substr(0, equals)));
        std::string value = detail::trim(std::string_view(trimmed).substr(equals + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }

        Settings& s = result.settings;
        bool ok = true;
        if (key == "render_distance") ok = detail::parse_int(value, s.render_distance);
        else if (key == "fov") ok = detail::parse_float(value, s.fov);
        else if (key == "mouse_sensitivity") ok = detail::parse_float(value, s.mouse_sensitivity);
        else if (key == "vsync") ok = detail::parse_bool(value, s.vsync);
        else if (key == "fullscreen") ok = detail::parse_bool(value, s.fullscreen);
        else if (key == "window_width") ok = detail::parse_int(value, s.window_width);
        else if (key == "window_height") ok = detail::parse_int(value, s.window_height);
        else if (key == "textures") ok = detail::parse_bool(value, s.textures);
        else if (key == "minecraft_dir") s.minecraft_dir = value;
        else if (key == "jar") s.jar = value;
        else if (key == "data_dir") s.data_dir = value;
        else {
            warn("unknown setting '" + key + "'");
            continue;
        }
        if (!ok) warn("bad value '" + value + "' for " + key);
    }
    result.settings.clamp();
    return result;
}

inline std::string format_settings(const Settings& s) {
    std::ostringstream out;
    out << "# MineCPP settings. Delete a line to restore its default.\n"
        << "render_distance = " << s.render_distance << "\n"
        << "fov = " << s.fov << "\n"
        << "mouse_sensitivity = " << s.mouse_sensitivity << "\n"
        << "vsync = " << (s.vsync ? "true" : "false") << "\n"
        << "fullscreen = " << (s.fullscreen ? "true" : "false") << "\n"
        << "window_width = " << s.window_width << "\n"
        << "window_height = " << s.window_height << "\n"
        << "# Use textures from your own Minecraft client jar.\n"
        << "textures = " << (s.textures ? "true" : "false") << "\n"
        << "# Empty = auto-detect the default Minecraft folder and its newest version.\n"
        << "minecraft_dir = " << s.minecraft_dir << "\n"
        << "jar = " << s.jar << "\n"
        << "data_dir = " << s.data_dir << "\n";
    return out.str();
}

// A missing file is not an error: defaults are returned.
inline SettingsLoad load_settings(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) return {};
    std::ostringstream text;
    text << file.rdbuf();
    return parse_settings(text.str());
}

inline bool save_settings(const std::filesystem::path& path, const Settings& settings) {
    std::ofstream file(path, std::ios::trunc);
    if (!file) return false;
    file << format_settings(settings);
    return static_cast<bool>(file);
}

}  // namespace minecpp
