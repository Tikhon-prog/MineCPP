#include "minecraft.hpp"
#include "zip.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
void print_help() {
    std::cout << R"(MineCPP 0.1.0 — offline Minecraft Java Edition launcher

Usage:
  minecpp versions [--minecraft-dir DIR]
  minecpp profiles [--data-dir DIR]
  minecpp inspect <client.jar>
  minecpp extract <archive.jar> --output DIR
  minecpp import <version> [--minecraft-dir DIR] [--data-dir DIR]
                        [--jar PATH] [--metadata PATH]
  minecpp launch <version> --username NAME [--minecraft-dir DIR] [--data-dir DIR] [--dry-run]
  minecpp doctor [--minecraft-dir DIR] [--data-dir DIR]

Options:
  --minecraft-dir DIR   Existing Minecraft game directory (default: platform default)
  --data-dir DIR        MineCPP data directory (default: platform default)
  --username NAME       Offline player name (1–16 letters, digits or underscores)
  --output DIR          Destination folder for 'extract'
  --jar PATH            Use this existing client JAR when importing
  --metadata PATH       Use this version JSON when importing
  --dry-run             Print the Java command without starting the game
  -h, --help            Show this help
  --version             Print MineCPP version

MineCPP never downloads game files or contacts an authentication server.
A local Minecraft version, its libraries/assets, and a Java runtime must already
be available. The launched game process retains its normal networking capability;
online-mode servers and Microsoft authentication are not supported.
)";
}

struct Options {
    fs::path minecraft_dir;
    fs::path data_dir;
    fs::path output;
    fs::path jar;
    fs::path metadata;
    std::string username;
    bool dry_run = false;
};

Options parse_options(int argc, char** argv, int start) {
    Options options;
    for (int i = start; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("Missing value after " + arg);
            return argv[++i];
        };
        if (arg == "--minecraft-dir") options.minecraft_dir = value();
        else if (arg == "--data-dir") options.data_dir = value();
        else if (arg == "--output") options.output = value();
        else if (arg == "--jar") options.jar = value();
        else if (arg == "--metadata") options.metadata = value();
        else if (arg == "--username") options.username = value();
        else if (arg == "--dry-run") options.dry_run = true;
        else if (arg == "--help" || arg == "-h") { print_help(); std::exit(0); }
        else throw std::runtime_error("Unknown option: " + arg);
    }
    return options;
}

void doctor(const Options& options) {
    const fs::path game = options.minecraft_dir.empty() ? minecpp::default_minecraft_dir() : options.minecraft_dir;
    const fs::path data = options.data_dir.empty() ? minecpp::default_data_dir() : options.data_dir;
    std::cout << "MineCPP environment check\n\n";
    std::cout << (fs::exists(game / "versions") ? "[OK] " : "[!!] ") << "Minecraft versions: " << game / "versions" << '\n';
    std::cout << (fs::exists(game / "libraries") ? "[OK] " : "[!!] ") << "Libraries: " << game / "libraries" << '\n';
    std::cout << (fs::exists(game / "assets") ? "[OK] " : "[!!] ") << "Assets: " << game / "assets" << '\n';
    std::cout << "[INFO] MineCPP data: " << data << '\n';
    const char* java_home = std::getenv("JAVA_HOME");
    std::cout << "[INFO] JAVA_HOME: " << (java_home ? java_home : "not set (java will be searched in PATH)") << '\n';
#ifndef _WIN32
    const int result = std::system("java -version >/dev/null 2>&1");
#else
    const int result = std::system("java -version >NUL 2>&1");
#endif
    std::cout << (result == 0 ? "[OK] " : "[!!] ") << "Java runtime on PATH\n";
    if (!fs::exists(game / "versions") || !fs::exists(game / "libraries") || !fs::exists(game / "assets") || result != 0)
        std::cout << "\nSome requirements are missing. MineCPP works offline and will not download them.\n";
    else std::cout << "\nThe basic local installation layout is present.\n";
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 1) { print_help(); return 0; }
        const std::string command = argv[1];
        if (command == "--help" || command == "-h" || command == "help") { print_help(); return 0; }
        if (command == "--version" || command == "version") { std::cout << "MineCPP 0.1.0\n"; return 0; }

        if (command == "inspect") {
            if (argc < 3) throw std::runtime_error("Usage: minecpp inspect <client.jar>");
            minecpp::inspect_jar(argv[2]); return 0;
        }
        if (command == "extract") {
            if (argc < 3) throw std::runtime_error("Usage: minecpp extract <archive.jar> --output DIR");
            const Options opts = parse_options(argc, argv, 3);
            if (opts.output.empty()) throw std::runtime_error("extract requires --output DIR");
            minecpp::ZipArchive archive(argv[2]);
            const auto count = archive.extract_all(opts.output);
            std::cout << "Extracted " << count << " files to " << opts.output << '\n';
            return 0;
        }

        const int option_start = (command == "import" || command == "launch") ? 3 : 2;
        const Options opts = parse_options(argc, argv, option_start);
        const fs::path game = opts.minecraft_dir.empty() ? minecpp::default_minecraft_dir() : opts.minecraft_dir;
        const fs::path data = opts.data_dir.empty() ? minecpp::default_data_dir() : opts.data_dir;
        if (command == "versions") { minecpp::list_versions(game); return 0; }
        if (command == "profiles") { minecpp::list_profiles(data); return 0; }
        if (command == "doctor") { doctor(opts); return 0; }
        if (command == "import") {
            if (argc < 3) throw std::runtime_error("Usage: minecpp import <version> [options]");
            minecpp::import_version(argv[2], game, data, opts.jar, opts.metadata); return 0;
        }
        if (command == "launch") {
            if (argc < 3) throw std::runtime_error("Usage: minecpp launch <version> --username NAME [options]");
            if (opts.username.empty()) throw std::runtime_error("launch requires --username NAME");
            return minecpp::launch_version(argv[2], opts.username, opts.minecraft_dir, data, opts.dry_run);
        }
        throw std::runtime_error("Unknown command: " + command + " (use --help)");
    } catch (const std::exception& error) {
        std::cerr << "MineCPP error: " << error.what() << '\n';
        return 1;
    }
}
