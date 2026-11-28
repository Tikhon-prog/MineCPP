#include "jar_importer.hpp"
#include "jar_locator.hpp"
#include "launch.hpp"
#include "launcher_settings.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using minecpp::launcher::ImportSummary;
using minecpp::launcher::ImportedVersion;
using minecpp::launcher::VerificationResult;

namespace {

void print_help() {
    std::cout
        << "MineCPP Launcher 0.4.0 — starts Minecraft: Java Edition installed by the official launcher\n\n"
        << "Usage:\n"
        << "  minecpp-launcher                       Interactive menu (play, settings)\n"
        << "  minecpp-launcher versions [options]    List installed Minecraft versions\n"
        << "  minecpp-launcher launch [VERSION|latest] [options]\n"
        << "  minecpp-launcher import <client.jar> [options]\n"
        << "  minecpp-launcher list [options]        List imported versions\n"
        << "  minecpp-launcher verify <version-id> [options]\n\n"
        << "launch options:\n"
        << "  --minecraft-dir PATH   Minecraft folder (default: the official launcher's folder)\n"
        << "  --player NAME          Offline player name (default: Player)\n"
        << "  --java PATH            Java executable (default: bundled runtime, JAVA_HOME, PATH)\n"
        << "  --memory MB            Maximum Java heap\n"
        << "  --game-dir PATH        Game/save folder (default: the Minecraft folder)\n"
        << "  --width W --height H   Window size\n"
        << "  --jvm-arg ARG          Extra JVM argument (repeatable)\n"
        << "  --game-arg ARG         Extra game argument (repeatable)\n"
        << "  --dry-run              Print the command and do not start the game\n"
        << "  --config PATH          Launcher settings file (default: minecpp-launcher.cfg)\n\n"
        << "import options:\n"
        << "  --id ID          Version identifier (defaults to the JAR filename)\n"
        << "  --data-dir PATH  Import directory (default: minecpp-data)\n"
        << "  --force          Replace an already imported version safely\n"
        << "  -h, --help       Show this help\n\n"
        << "Examples:\n"
        << "  minecpp-launcher launch latest --player Steve\n"
        << "  minecpp-launcher launch 1.21.4 --memory 4096 --dry-run\n"
        << "  minecpp-launcher import ~/.minecraft/versions/1.20.4/1.20.4.jar\n\n"
        << "The launcher downloads nothing and does not sign in. Install and start a version once\n"
        << "with the official Minecraft launcher so its libraries and assets exist. Games start in\n"
        << "offline mode: singleplayer and LAN work; online servers need the official launcher.\n";
}

struct CommonOptions {
    fs::path data_directory = "minecpp-data";
};

CommonOptions parse_common_options(int argc, char** argv, int begin) {
    CommonOptions options;
    for (int i = begin; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--data-dir") {
            if (++i >= argc) throw std::runtime_error("Missing path after --data-dir");
            options.data_directory = argv[i];
        } else if (argument == "-h" || argument == "--help") {
            print_help();
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown option: " + argument);
        }
    }
    return options;
}

void print_import_summary(const ImportSummary& summary) {
    std::cout << "Imported Minecraft version " << summary.version_id << "\n"
              << "Source JAR: " << summary.source_filename << "\n"
              << "Extracted files: " << summary.file_count << "\n"
              << "Uncompressed data: " << summary.uncompressed_bytes << " bytes\n"
              << "Version JSON copied: " << (summary.copied_version_metadata ? "yes" : "no") << "\n"
              << "Stored at: " << summary.version_directory.string() << "\n"
              << "Integrity manifest: " << (summary.version_directory / "manifest.mcpp").string() << "\n";
}

int command_import(int argc, char** argv) {
    if (argc < 3) throw std::runtime_error("import requires a path to an existing .jar file");
    const fs::path jar_path = argv[2];
    std::string version_id;
    bool force = false;
    CommonOptions common;

    for (int i = 3; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--id") {
            if (++i >= argc) throw std::runtime_error("Missing version ID after --id");
            version_id = argv[i];
        } else if (argument == "--data-dir") {
            if (++i >= argc) throw std::runtime_error("Missing path after --data-dir");
            common.data_directory = argv[i];
        } else if (argument == "--force") {
            force = true;
        } else if (argument == "-h" || argument == "--help") {
            print_help();
            return 0;
        } else {
            throw std::runtime_error("Unknown option: " + argument);
        }
    }

    const ImportSummary summary = minecpp::launcher::import_jar(
        jar_path, common.data_directory, version_id, force);
    print_import_summary(summary);
    return 0;
}

int command_list(int argc, char** argv) {
    const CommonOptions common = parse_common_options(argc, argv, 2);
    const std::vector<ImportedVersion> versions =
        minecpp::launcher::list_imports(common.data_directory);
    if (versions.empty()) {
        std::cout << "No imported versions found in "
                  << (common.data_directory / "versions").string() << "\n";
        return 0;
    }

    std::cout << "Imported versions:\n";
    for (const auto& version : versions) {
        std::cout << "  " << version.version_id
                  << " | " << version.file_count << " files"
                  << " | " << version.uncompressed_bytes << " bytes"
                  << " | source: " << version.source_filename << "\n";
    }
    return 0;
}

int command_verify(int argc, char** argv) {
    if (argc < 3) throw std::runtime_error("verify requires a version ID");
    const std::string version_id = argv[2];
    const CommonOptions common = parse_common_options(argc, argv, 3);
    const VerificationResult result =
        minecpp::launcher::verify_import(version_id, common.data_directory);

    if (result.ok) {
        std::cout << "OK: " << version_id << " — checked "
                  << result.checked_files << " files; all sizes and CRC-32 checksums match.\n";
        return 0;
    }

    std::cerr << "Verification failed for " << version_id << ":\n";
    for (const auto& error : result.errors) std::cerr << "  - " << error << '\n';
    return 2;
}


// ---- Launching the real game ---------------------------------------------------------------

bool stdin_is_terminal() {
#if defined(_WIN32)
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

fs::path resolve_minecraft_dir(const minecpp::LauncherSettings& settings) {
    if (!settings.minecraft_dir.empty()) return settings.minecraft_dir;
    return minecpp::default_minecraft_dir();
}

std::string or_auto(const std::string& value) {
    return value.empty() ? std::string("(auto)") : value;
}

int launch_version(const minecpp::LauncherSettings& settings, std::string version, bool dry_run,
                   const std::vector<std::string>& extra_jvm, const std::vector<std::string>& extra_game) {
    namespace launcher = minecpp::launcher;
    const fs::path minecraft_dir = resolve_minecraft_dir(settings);
    std::error_code error;
    if (minecraft_dir.empty() || !fs::is_directory(minecraft_dir, error)) {
        throw std::runtime_error("Minecraft folder not found: '" + minecraft_dir.string() +
                                 "'. Install Minecraft with the official launcher, or set --minecraft-dir.");
    }
    if (version.empty() || version == "latest") {
        const std::vector<std::string> versions = launcher::installed_versions(minecraft_dir);
        if (versions.empty()) {
            throw std::runtime_error("No installed versions in " + (minecraft_dir / "versions").string() +
                                     ". Start a version once in the official launcher first.");
        }
        version = versions.front();
    }

    const minecpp::Json profile = launcher::load_version_profile(minecraft_dir, version);
    launcher::LaunchOptions options;
    options.minecraft_dir = minecraft_dir;
    options.version_id = version;
    options.player_name = settings.player_name;
    options.game_dir = settings.game_dir;
    options.java = settings.java;
    options.memory_mb = settings.memory_mb;
    options.width = settings.width;
    options.height = settings.height;
    options.extra_jvm_args = minecpp::split_arguments(settings.jvm_args);
    options.extra_jvm_args.insert(options.extra_jvm_args.end(), extra_jvm.begin(), extra_jvm.end());
    options.extra_game_args = minecpp::split_arguments(settings.game_args);
    options.extra_game_args.insert(options.extra_game_args.end(), extra_game.begin(), extra_game.end());
    const launcher::LaunchPlan plan = launcher::plan_launch(profile, options);

    std::cout << "Version:  " << version << "\n"
              << "Player:   " << settings.player_name << " (offline)\n"
              << "Java:     " << plan.java.string();
    if (plan.java_major > 0) std::cout << "  (this version wants Java " << plan.java_major << ")";
    std::cout << "\n";

    if (!plan.unsupported.empty()) {
        std::cerr << "This version cannot be started by MineCPP yet:\n";
        for (const std::string& reason : plan.unsupported) std::cerr << "  - " << reason << '\n';
        std::cerr << "Pick a newer version, or start it from the official launcher.\n";
        return 3;
    }
    if (!plan.missing_files.empty()) {
        std::cerr << plan.missing_files.size() << " required file(s) are missing, for example:\n";
        for (std::size_t i = 0; i < plan.missing_files.size() && i < 8; ++i) {
            std::cerr << "  - " << plan.missing_files[i].string() << '\n';
        }
        std::cerr << "MineCPP does not download game files. Start this version once in the official\n"
                     "Minecraft launcher so it installs them, then try again.\n";
        if (!dry_run) return 4;
    }
    if (dry_run) {
        std::cout << minecpp::launcher::describe_plan(plan) << "\n";
        return 0;
    }

    std::cout << "Starting Minecraft in offline mode (singleplayer and LAN). Online servers need the\n"
                 "official launcher. Close the game window to return here.\n";
    const int code = minecpp::launcher::run_plan(plan);
    if (code == 127 || code == -1) {
        std::cerr << "Could not start Java ('" << plan.java.string() << "'). Install Java"
                  << (plan.java_major > 0 ? " " + std::to_string(plan.java_major) : std::string())
                  << " or pass --java PATH.\n";
    } else if (code != 0) {
        std::cerr << "Minecraft exited with code " << code << ".\n";
    }
    return code;
}

int command_versions(int argc, char** argv) {
    minecpp::LauncherSettings settings;
    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--minecraft-dir" && i + 1 < argc) settings.minecraft_dir = argv[++i];
        else if (argument == "-h" || argument == "--help") { print_help(); return 0; }
        else throw std::runtime_error("Unknown option: " + argument);
    }
    const fs::path dir = resolve_minecraft_dir(settings);
    const std::vector<std::string> versions = minecpp::launcher::installed_versions(dir);
    if (versions.empty()) {
        std::cout << "No installed versions found in " << (dir / "versions").string() << "\n";
        return 0;
    }
    std::cout << "Installed versions in " << dir.string() << ":\n";
    for (const std::string& id : versions) std::cout << "  " << id << "\n";
    return 0;
}

int command_launch(int argc, char** argv) {
    fs::path config = "minecpp-launcher.cfg";
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--config" && i + 1 < argc) config = argv[i + 1];
    }
    minecpp::LauncherSettingsLoad loaded = minecpp::load_launcher_settings(config);
    for (const std::string& warning : loaded.warnings) std::cerr << config.string() << ": " << warning << '\n';
    minecpp::LauncherSettings settings = loaded.settings;

    std::string version = settings.version;
    bool dry_run = false;
    std::vector<std::string> extra_jvm;
    std::vector<std::string> extra_game;
    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("Missing value after " + argument);
            return argv[++i];
        };
        if (argument == "--minecraft-dir") settings.minecraft_dir = value();
        else if (argument == "--player") settings.player_name = value();
        else if (argument == "--java") settings.java = value();
        else if (argument == "--memory") settings.memory_mb = std::stoi(value());
        else if (argument == "--game-dir") settings.game_dir = value();
        else if (argument == "--width") settings.width = std::stoi(value());
        else if (argument == "--height") settings.height = std::stoi(value());
        else if (argument == "--jvm-arg") extra_jvm.push_back(value());
        else if (argument == "--game-arg") extra_game.push_back(value());
        else if (argument == "--dry-run") dry_run = true;
        else if (argument == "--config") (void)value();
        else if (argument == "-h" || argument == "--help") { print_help(); return 0; }
        else if (!argument.empty() && argument[0] != '-') version = argument;
        else throw std::runtime_error("Unknown option: " + argument);
    }
    settings.clamp();
    return launch_version(settings, version, dry_run, extra_jvm, extra_game);
}

// ---- Interactive menu ----------------------------------------------------------------------

std::string read_line(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) throw std::runtime_error("input closed");
    return minecpp::detail::trim(line);
}

struct SettingItem {
    std::string label;
    std::function<std::string()> get;
    std::function<bool(const std::string&)> set;  // false = rejected
    std::string hint;
};

std::vector<SettingItem> settings_items(minecpp::LauncherSettings& s) {
    auto text_item = [](const char* label, std::string& field, const char* hint) {
        return SettingItem{label, [&field] { return field; },
                           [&field](const std::string& v) { field = v == "-" ? std::string() : v; return true; }, hint};
    };
    auto int_item = [](const char* label, int& field, const char* hint) {
        return SettingItem{label, [&field] { return std::to_string(field); },
                           [&field](const std::string& v) {
                               int parsed = 0;
                               if (!minecpp::detail::parse_int(v == "-" ? "0" : v, parsed) || parsed < 0) return false;
                               field = parsed;
                               return true;
                           }, hint};
    };
    std::vector<SettingItem> items;
    items.push_back(text_item("Minecraft folder", s.minecraft_dir, "folder with versions/ and libraries/; '-' = default"));
    items.push_back(SettingItem{"Player name", [&s] { return s.player_name; },
                                [&s](const std::string& v) {
                                    if (!minecpp::launcher::valid_player_name(v)) return false;
                                    s.player_name = v;
                                    return true;
                                }, "1-16 letters, digits or _ (offline mode)"});
    items.push_back(text_item("Default version", s.version, "e.g. 1.21.4; '-' = newest installed"));
    items.push_back(text_item("Java executable", s.java, "path to java; '-' = automatic"));
    items.push_back(int_item("Memory (MB)", s.memory_mb, "512-65536, 0 = Java default"));
    items.push_back(text_item("Game folder", s.game_dir, "saves/options folder; '-' = Minecraft folder"));
    items.push_back(int_item("Window width", s.width, "0 = game default (set height too)"));
    items.push_back(int_item("Window height", s.height, "0 = game default"));
    items.push_back(text_item("Extra JVM arguments", s.jvm_args, "space separated, e.g. -XX:+UseG1GC"));
    items.push_back(text_item("Extra game arguments", s.game_args, "space separated"));
    return items;
}

void settings_menu(minecpp::LauncherSettings& settings, const fs::path& config) {
    for (;;) {
        std::vector<SettingItem> items = settings_items(settings);
        std::cout << "\n== Settings ==\n";
        for (std::size_t i = 0; i < items.size(); ++i) {
            std::cout << "  " << (i + 1) << ") " << items[i].label << ": " << or_auto(items[i].get())
                      << "   [" << items[i].hint << "]\n";
        }
        const std::string choice = read_line("Number to change, S = save and go back, 0 = go back without saving: ");
        if (choice == "0") return;
        if (choice == "s" || choice == "S") {
            settings.clamp();
            if (minecpp::save_launcher_settings(config, settings)) {
                std::cout << "Saved to " << config.string() << "\n";
            } else {
                std::cerr << "Could not write " << config.string() << "\n";
            }
            return;
        }
        int number = 0;
        if (!minecpp::detail::parse_int(choice, number) || number < 1 || number > static_cast<int>(items.size())) {
            std::cout << "Not a valid choice.\n";
            continue;
        }
        SettingItem& item = items[static_cast<std::size_t>(number - 1)];
        const std::string value = read_line(item.label + " (" + item.hint + "): ");
        if (value.empty()) continue;
        if (!item.set(value)) std::cout << "That value is not accepted.\n";
    }
}

int interactive_menu(const fs::path& config) {
    minecpp::LauncherSettingsLoad loaded = minecpp::load_launcher_settings(config);
    for (const std::string& warning : loaded.warnings) std::cerr << config.string() << ": " << warning << '\n';
    minecpp::LauncherSettings settings = loaded.settings;

    for (;;) {
        const fs::path dir = resolve_minecraft_dir(settings);
        const std::vector<std::string> versions = minecpp::launcher::installed_versions(dir);
        std::cout << "\n== MineCPP Launcher 0.4.0 ==\n"
                  << "Minecraft folder: " << dir.string() << "\n"
                  << "Installed versions: " << versions.size()
                  << (versions.empty() ? "  (install one with the official launcher first)" : "") << "\n"
                  << "Player: " << settings.player_name << "\n\n"
                  << "  1) Play\n"
                  << "  2) Choose version\n"
                  << "  3) Settings\n"
                  << "  4) Show launch command (dry run)\n"
                  << "  0) Quit\n";
        const std::string choice = read_line("> ");
        try {
            if (choice == "0" || choice == "q" || choice == "Q") return 0;
            if (choice == "1" || choice == "4") {
                launch_version(settings, settings.version, choice == "4", {}, {});
            } else if (choice == "2") {
                if (versions.empty()) {
                    std::cout << "No versions found.\n";
                    continue;
                }
                for (std::size_t i = 0; i < versions.size(); ++i) {
                    std::cout << "  " << (i + 1) << ") " << versions[i] << "\n";
                }
                const std::string pick = read_line("Number (Enter = keep '" + or_auto(settings.version) + "'): ");
                int number = 0;
                if (!pick.empty() && minecpp::detail::parse_int(pick, number) && number >= 1 &&
                    number <= static_cast<int>(versions.size())) {
                    settings.version = versions[static_cast<std::size_t>(number - 1)];
                    minecpp::save_launcher_settings(config, settings);
                    std::cout << "Default version: " << settings.version << "\n";
                }
            } else if (choice == "3") {
                settings_menu(settings, config);
            } else {
                std::cout << "Not a valid choice.\n";
            }
        } catch (const std::exception& error) {
            if (std::string(error.what()) == "input closed") return 0;
            std::cerr << "Error: " << error.what() << "\n";
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            if (stdin_is_terminal()) return interactive_menu("minecpp-launcher.cfg");
            print_help();
            return 0;
        }
        if (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
            print_help();
            return 0;
        }

        const std::string command = argv[1];
        if (command == "launch") return command_launch(argc, argv);
        if (command == "versions") return command_versions(argc, argv);
        if (command == "menu") return interactive_menu("minecpp-launcher.cfg");
        if (command == "import") return command_import(argc, argv);
        if (command == "list") return command_list(argc, argv);
        if (command == "verify") return command_verify(argc, argv);
        throw std::runtime_error("Unknown command: " + command + " (use --help)");
    } catch (const std::exception& error) {
        std::cerr << "MineCPP Launcher: " << error.what() << '\n';
        return 1;
    }
}
