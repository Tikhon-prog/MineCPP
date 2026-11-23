#include "renderer.hpp"

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
        "MineCPP 0.1.0 — native C++ voxel sandbox\n\n"
        "Usage: minecpp [options]\n\n"
        "Options:\n"
        "  --world PATH             World save file (default: worlds/Default.mcw)\n"
        "  --seed NUMBER            World seed for a new world\n"
        "  --render-distance N      Chunk radius, 2..8 (default: 4)\n"
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
        if (arg == "--world") result.world_path = value();
        else if (arg == "--seed") {
            const std::string text = value();
            const auto parsed = std::stoull(text);
            if (parsed > 0xffffffffULL) throw std::runtime_error("Seed must fit in an unsigned 32-bit integer");
            result.seed = static_cast<std::uint32_t>(parsed);
            result.seed_explicit = true;
        } else if (arg == "--render-distance") {
            result.render_distance = std::stoi(value());
            if (result.render_distance < 2 || result.render_distance > 8)
                throw std::runtime_error("Render distance must be between 2 and 8 chunks");
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
