#include "jar_importer.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using minecpp::launcher::ImportSummary;
using minecpp::launcher::ImportedVersion;
using minecpp::launcher::VerificationResult;

namespace {

void print_help() {
    std::cout
        << "MineCPP Launcher 0.1.0 — offline Minecraft JAR importer\n\n"
        << "Usage:\n"
        << "  minecpp-launcher import <client.jar> [options]\n"
        << "  minecpp-launcher list [options]\n"
        << "  minecpp-launcher verify <version-id> [options]\n\n"
        << "Options:\n"
        << "  --id ID          Version identifier (defaults to the JAR filename)\n"
        << "  --data-dir PATH  Import directory (default: minecpp-data)\n"
        << "  --force          Replace an already imported version safely\n"
        << "  -h, --help       Show this help\n\n"
        << "Examples:\n"
        << "  minecpp-launcher import ~/.minecraft/versions/1.20.4/1.20.4.jar\n"
        << "  minecpp-launcher list\n"
        << "  minecpp-launcher verify 1.20.4\n\n"
        << "This first release works offline. It extracts a supplied JAR and records\n"
        << "a local integrity manifest; it does not download, authenticate, or run\n"
        << "Minecraft yet. Keep your original Minecraft installation and JAR intact.\n";
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

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
            print_help();
            return 0;
        }

        const std::string command = argv[1];
        if (command == "import") return command_import(argc, argv);
        if (command == "list") return command_list(argc, argv);
        if (command == "verify") return command_verify(argc, argv);
        throw std::runtime_error("Unknown command: " + command + " (use --help)");
    } catch (const std::exception& error) {
        std::cerr << "MineCPP Launcher: " << error.what() << '\n';
        return 1;
    }
}
