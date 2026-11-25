#include "renderer.hpp"

#ifndef MINECPP_VERSION
#define MINECPP_VERSION "1.0.0"
#endif

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
struct Options {
    std::uint32_t seed = 1337U;
    bool seed_explicit = false;
    int render_distance = 4;
    std::string world_path = "worlds/Default.mcw";
};

void print_help() {
    std::cout <<
        "MineCPP " MINECPP_VERSION " — native C++ voxel client\n\n"
        "Usage: minecpp [options]\n\n"
        "Options:\n"
        "  --world PATH             World save file (default: worlds/Default.mcw)\n"
        "  --seed NUMBER            World seed for a new world\n"
        "  --render-distance N      Chunk radius, 2..8 (default: 4)\n"
        "  --version                Show the MineCPP version\n"
        "  -h, --help               Show this help\n\n"
        "Controls:\n"
        "  W A S D                  Move\n"
        "  Mouse                    Look around\n"
        "  Space                    Jump\n"
        "  Shift                    Sprint\n"
        "  Left mouse               Break block\n"
        "  Right mouse              Place block\n"
        "  1..6                     Select block\n"
        "  Tab                      Release / capture mouse\n"
        "  Escape                   Quit and save\n";
}

Options parse_options(int argc, char** argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("Missing value after " + arg);
            return argv[++i];
        };
        if (arg == "--version") {
            std::cout << "MineCPP " MINECPP_VERSION << '\n';
            std::exit(0);
        } else if (arg == "--world") result.world_path = value();
        else if (arg == "--seed") {
            const std::string text = value();
            std::uint64_t parsed = 0;
            const auto conversion = std::from_chars(
                text.data(), text.data() + text.size(), parsed);
            if (conversion.ec != std::errc{} || conversion.ptr != text.data() + text.size() ||
                parsed > 0xffffffffULL)
                throw std::runtime_error("Seed must be an unsigned 32-bit integer");
            result.seed = static_cast<std::uint32_t>(parsed);
            result.seed_explicit = true;
        } else if (arg == "--render-distance") {
            const std::string text = value();
            int parsed = 0;
            const auto conversion = std::from_chars(
                text.data(), text.data() + text.size(), parsed);
            if (conversion.ec != std::errc{} || conversion.ptr != text.data() + text.size() ||
                parsed < 2 || parsed > 8)
                throw std::runtime_error("Render distance must be an integer between 2 and 8 chunks");
            result.render_distance = parsed;
        } else if (arg == "-h" || arg == "--help") {
            print_help();
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown option: " + arg + " (use --help)");
        }
    }
    return result;
}
}

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        minecpp::Game game(options.world_path, options.seed, options.seed_explicit, options.render_distance);
        return game.run();
    } catch (const std::exception& error) {
        std::cerr << "MineCPP: " << error.what() << '\n';
        return 1;
    }
}
