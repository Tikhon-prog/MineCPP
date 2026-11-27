#include "jar_importer.hpp"
#include "jar_locator.hpp"
#include "renderer.hpp"
#include "settings.hpp"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;

struct Options {
    std::uint32_t seed = 1337U;
    bool seed_explicit = false;
    std::string world_path = "worlds/Default.mcw";
    std::string settings_path = "minecpp-settings.cfg";
    std::optional<int> render_distance;
    std::optional<std::string> jar;
    std::optional<std::string> minecraft_dir;
    std::optional<std::string> data_dir;
    bool no_textures = false;
};

void print_help() {
    std::cout <<
        "MineCPP 0.3.0 — native C++ voxel sandbox\n\n"
        "Usage: minecpp [options]\n\n"
        "Textures come from your own Minecraft client jar. By default the newest\n"
        "version in your Minecraft folder (~/.minecraft/versions) is imported once\n"
        "into minecpp-data/ and its block textures are used. Nothing is downloaded.\n\n"
        "Options:\n"
        "  --world PATH             World save file (default: worlds/Default.mcw)\n"
        "  --seed NUMBER            World seed for a new world\n"
        "  --render-distance N      Chunk radius, 2..8 (default: 4)\n"
        "  --jar PATH               Use this client jar for textures\n"
        "  --minecraft-dir PATH     Minecraft folder to search for a client jar\n"
        "  --data-dir PATH          Where imported versions are kept (default: minecpp-data)\n"
        "  --no-textures            Flat colours, do not read any jar\n"
        "  --settings PATH          Settings file (default: minecpp-settings.cfg)\n"
        "  -h, --help               Show this help\n\n"
        "Settings file (key = value): render_distance, fov, mouse_sensitivity, vsync,\n"
        "fullscreen, window_width, window_height, textures, minecraft_dir, jar, data_dir.\n"
        "Command-line options override the file.\n\n"
        "Controls:\n"
        "  W A S D                  Move\n"
        "  Mouse                    Look around\n"
        "  Space                    Jump\n"
        "  Shift                    Sprint\n"
        "  Left mouse               Break block\n"
        "  Right mouse              Place block\n"
        "  1..6                     Select block\n"
        "  F11                      Fullscreen on/off\n"
        "  V                        VSync on/off\n"
        "  [ and ]                  Field of view -/+\n"
        "  - and =                  Mouse sensitivity -/+\n"
        "  Tab                      Release / capture mouse\n"
        "  Escape                   Quit and save\n"
        "Changes made with F11, V, [ ] and - = are saved to the settings file on exit.\n";
}

Options parse_options(int argc, char** argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("Missing value after " + arg);
            return argv[++i];
        };
        if (arg == "--world") result.world_path = value();
        else if (arg == "--seed") {
            const std::string text = value();
            const auto parsed = std::stoull(text);
            if (parsed > 0xffffffffULL) throw std::runtime_error("Seed must fit in an unsigned 32-bit integer");
            result.seed = static_cast<std::uint32_t>(parsed);
            result.seed_explicit = true;
        } else if (arg == "--render-distance") {
            const int distance = std::stoi(value());
            if (distance < 2 || distance > 8)
                throw std::runtime_error("Render distance must be between 2 and 8 chunks");
            result.render_distance = distance;
        } else if (arg == "--jar") result.jar = value();
        else if (arg == "--minecraft-dir") result.minecraft_dir = value();
        else if (arg == "--data-dir") result.data_dir = value();
        else if (arg == "--settings") result.settings_path = value();
        else if (arg == "--no-textures") result.no_textures = true;
        else if (arg == "-h" || arg == "--help") {
            print_help();
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown option: " + arg + " (use --help)");
        }
    }
    return result;
}

// Finds the player's client jar, imports it on first use and returns the folder
// with the block textures. Returns an empty path (flat colours) on any problem.
fs::path prepare_textures(const minecpp::Settings& settings) {
    if (!settings.textures) return {};

    fs::path jar;
    std::string id;
    if (!settings.jar.empty()) {
        jar = settings.jar;
        id = jar.stem().string();
        if (!fs::is_regular_file(jar)) {
            std::cerr << "Textures: jar not found: " << jar.string() << " — using flat colours.\n";
            return {};
        }
    } else {
        const fs::path dir = settings.minecraft_dir.empty() ? minecpp::default_minecraft_dir()
                                                            : fs::path(settings.minecraft_dir);
        const auto jars = minecpp::find_client_jars(dir);
        if (jars.empty()) {
            std::cout << "Textures: no Minecraft client jar found in '" << dir.string()
                      << "'. Install a version with the official launcher, or pass --jar PATH or\n"
                         "--minecraft-dir PATH. Using flat colours.\n";
            return {};
        }
        jar = jars.front().jar;
        id = jars.front().version_id;
    }

    if (!minecpp::launcher::valid_version_id(id)) {
        std::cerr << "Textures: '" << id << "' is not a usable version id — using flat colours.\n";
        return {};
    }

    const fs::path data(settings.data_dir);
    const fs::path textures = minecpp::block_texture_dir(data, id);
    std::error_code error;
    if (fs::is_directory(textures, error)) {
        std::cout << "Textures: using imported version " << id << ".\n";
        return textures;
    }
    if (fs::exists(data / "versions" / id, error)) {
        std::cerr << "Textures: " << (data / "versions" / id).string()
                  << " exists but has no block textures. Delete that folder to import again. Using flat colours.\n";
        return {};
    }

    std::cout << "Textures: importing " << jar.string() << " as version " << id
              << " (one time, can take a few seconds)...\n";
    try {
        const auto summary = minecpp::launcher::import_jar(jar, data, id, false);
        std::cout << "Textures: extracted " << summary.file_count << " files.\n";
    } catch (const std::exception& problem) {
        std::cerr << "Textures: import failed: " << problem.what() << " — using flat colours.\n";
        return {};
    }
    if (!fs::is_directory(textures, error)) {
        std::cerr << "Textures: the jar has no assets/minecraft/textures/block folder — using flat colours.\n";
        return {};
    }
    return textures;
}
}

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);

        minecpp::SettingsLoad loaded = minecpp::load_settings(options.settings_path);
        for (const std::string& warning : loaded.warnings) {
            std::cerr << options.settings_path << ": " << warning << '\n';
        }
        minecpp::Settings settings = loaded.settings;
        if (options.render_distance) settings.render_distance = *options.render_distance;
        if (options.jar) settings.jar = *options.jar;
        if (options.minecraft_dir) settings.minecraft_dir = *options.minecraft_dir;
        if (options.data_dir) settings.data_dir = *options.data_dir;
        if (options.no_textures) settings.textures = false;
        settings.clamp();

        const fs::path texture_dir = prepare_textures(settings);
        minecpp::Game game(options.world_path, options.seed, options.seed_explicit, settings,
                           options.settings_path, texture_dir);
        return game.run();
    } catch (const std::exception& error) {
        std::cerr << "MineCPP: " << error.what() << '\n';
        return 1;
    }
}
