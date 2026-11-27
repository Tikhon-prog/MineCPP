#include "jar_locator.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace minecpp;
namespace fs = std::filesystem;

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Test failed: " #expression); } while (false)

static void touch(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << "x";
}

int main() {
    // Natural version ordering.
    CHECK(version_less("1.9", "1.10"));
    CHECK(!version_less("1.10", "1.9"));
    CHECK(version_less("1.20", "1.20.4"));
    CHECK(!version_less("1.20.4", "1.20"));
    CHECK(!version_less("1.20.4", "1.20.4"));
    CHECK(version_less("1.21.9", "1.21.10"));
    CHECK(version_less("1.21.10", "26.3"));
    CHECK(version_less("24w10a", "24w10b"));
    CHECK(version_less("1.8", "1.8.9"));

    // Release detection.
    CHECK(is_release_id("1.21.4"));
    CHECK(is_release_id("26.3"));
    CHECK(!is_release_id("24w10a"));
    CHECK(!is_release_id("1.21-pre1"));
    CHECK(!is_release_id("fabric-loader-0.15.0-1.21"));
    CHECK(!is_release_id(""));

    // Texture folder inside an import.
    CHECK(block_texture_dir("data", "1.21") ==
          fs::path("data") / "versions" / "1.21" / "client" / "assets" / "minecraft" / "textures" / "block");

    // Searching a Minecraft folder.
    const fs::path root = fs::temp_directory_path() /
        ("minecpp_jar_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path versions = root / "versions";
    touch(versions / "1.20.4" / "1.20.4.jar");
    touch(versions / "1.9" / "1.9.jar");
    touch(versions / "1.10" / "1.10.jar");
    touch(versions / "26.3" / "26.3.jar");
    touch(versions / "24w10a" / "24w10a.jar");
    touch(versions / "fabric-loader-0.15-1.20" / "fabric-loader-0.15-1.20.json");  // no jar
    touch(versions / "1.7" / "other.jar");                                         // jar not named after its folder
    fs::create_directories(versions / "empty");
    touch(versions / "stray-file.jar");                                            // a file, not a version folder

    {
        const auto found = find_client_jars(root);
        CHECK(found.size() == 5);
        CHECK(found[0].version_id == "26.3");
        CHECK(found[1].version_id == "1.20.4");
        CHECK(found[2].version_id == "1.10");
        CHECK(found[3].version_id == "1.9");
        CHECK(found[4].version_id == "24w10a");  // snapshots after releases
        CHECK(found[0].jar == versions / "26.3" / "26.3.jar");
    }

    // Missing or empty folders.
    CHECK(find_client_jars(root / "nope").empty());
    CHECK(find_client_jars(versions / "empty").empty());

    // Must not throw whatever the environment holds.
    (void)default_minecraft_dir();

    std::error_code ignored;
    fs::remove_all(root, ignored);
    std::cout << "jar locator tests passed\n";
    return 0;
}
