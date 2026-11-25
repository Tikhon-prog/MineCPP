#include "resource_index.hpp"
#include "resource_preparer.hpp"
#include "sha1.hpp"
#include "version_metadata.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace minecpp::launcher;
namespace fs = std::filesystem;

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Test failed: " #expression); } while (false)

template<typename Function>
void expect_throw(Function&& function) {
    bool threw = false;
    try { function(); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

int main() {
    const fs::path sample = fs::temp_directory_path() / "minecpp-sha1-abc.txt";
    {
        std::ofstream output(sample, std::ios::binary | std::ios::trunc);
        output << "abc";
    }
    CHECK(minecpp::sha1_file(sample) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    fs::remove(sample);

    const std::string index = R"({
      "objects": {
        "minecraft/lang/en_us.json": {
          "hash":"a9993e364706816aba3e25717850c26c9cd0d89d",
          "size":3
        },
        "minecraft/sounds.json": {
          "hash":"0123456789012345678901234567890123456789",
          "size":20
        }
      },
      "virtual":false,
      "map_to_resources":false
    })";
    const AssetIndex assets = parse_asset_index(index, "example-index");
    CHECK(assets.objects.size() == 2U);
    CHECK(assets.total_bytes == 23U);
    CHECK(!assets.virtual_assets);

    expect_throw([] {
        (void)parse_asset_index(
            R"({"objects":{"bad/../path":{"hash":"a9993e364706816aba3e25717850c26c9cd0d89d","size":3}}})");
    });
    expect_throw([] {
        (void)parse_asset_index(
            R"({"objects":{"x":{"hash":"not-a-sha1","size":3}}})");
    });
    expect_throw([] {
        (void)parse_asset_index(
            R"({"objects":{"x":{"hash":"a9993e364706816aba3e25717850c26c9cd0d89d","size":-1}}})");
    });

    // End-to-end offline preparation, including legacy asset mappings and log config.
    const fs::path root = fs::temp_directory_path() / "minecpp-resource-prep-tests";
    fs::remove_all(root);
    const fs::path data = root / "minecpp-data";
    const fs::path game = root / "game";
    const fs::path metadata_path = data / "versions" / "1.20.4" / "metadata" / "version.json";
    const fs::path index_path = game / "assets" / "indexes" / "test-index.json";
    const fs::path object_path = game / "assets" / "objects" / "a9" / "a9993e364706816aba3e25717850c26c9cd0d89d";
    const fs::path library_path = game / "libraries" / "com" / "example" / "lib" / "1.0" / "lib-1.0.jar";
    const fs::path log_path = game / "assets" / "log_configs" / "client-log.xml";
    fs::create_directories(metadata_path.parent_path());
    fs::create_directories(index_path.parent_path());
    fs::create_directories(object_path.parent_path());
    fs::create_directories(library_path.parent_path());
    fs::create_directories(log_path.parent_path());
    const std::string index_json = R"({"objects":{"example/data.txt":{
      "hash":"a9993e364706816aba3e25717850c26c9cd0d89d","size":3
    }},"virtual":true,"map_to_resources":true})";
    { std::ofstream output(index_path); output << index_json; }
    { std::ofstream output(object_path, std::ios::binary); output << "abc"; }
    { std::ofstream output(library_path, std::ios::binary); output << "abc"; }
    { std::ofstream output(log_path, std::ios::binary); output << "abc"; }
    const std::string index_hash = minecpp::sha1_file(index_path);
    const auto index_size = fs::file_size(index_path);
    const std::string metadata_json =
        "{\"id\":\"1.20.4\",\"mainClass\":\"example.Main\","
        "\"assetIndex\":{\"id\":\"test-index\",\"sha1\":\"" + index_hash +
        "\",\"size\":" + std::to_string(index_size) + "},"
        "\"logging\":{\"client\":{\"file\":{\"id\":\"client-log.xml\","
        "\"sha1\":\"a9993e364706816aba3e25717850c26c9cd0d89d\",\"size\":3}}},"
        "\"libraries\":[{\"name\":\"example:lib:1.0\",\"downloads\":{\"artifact\":{"
        "\"path\":\"com/example/lib/1.0/lib-1.0.jar\","
        "\"sha1\":\"a9993e364706816aba3e25717850c26c9cd0d89d\",\"size\":3}}}]}";
    { std::ofstream output(metadata_path); output << metadata_json; }
    const auto prepared = prepare_offline_resources(game, data, "1.20.4");
    CHECK(prepared.ready);
    CHECK(prepared.libraries_copied == 1U);
    CHECK(prepared.assets_copied == 1U);
    CHECK(fs::is_regular_file(prepared.output_directory / "assets" / "indexes" / "test-index.json"));
    CHECK(fs::is_regular_file(prepared.output_directory / "assets" / "objects" / "a9" / object_path.filename()));
    CHECK(fs::is_regular_file(prepared.output_directory / "assets" / "virtual" / "test-index" / "example" / "data.txt"));
    CHECK(fs::is_regular_file(prepared.output_directory / "resources" / "example" / "data.txt"));
    CHECK(fs::is_regular_file(prepared.output_directory / "assets" / "log_configs" / "client-log.xml"));
    const auto verified = verify_prepared_resources(data, "1.20.4");
    CHECK(verified.valid);
    CHECK(verified.checked_files >= 5U);
    expect_throw([&] { (void)prepare_offline_resources(game, data, "1.20.4"); });
    const auto replaced = prepare_offline_resources(game, data, "1.20.4", true);
    CHECK(replaced.ready);
    CHECK(verify_prepared_resources(data, "1.20.4").valid);
    fs::remove(library_path);
    const auto incomplete = prepare_offline_resources(game, data, "1.20.4", true);
    CHECK(!incomplete.ready);
    CHECK(!incomplete.missing.empty());
    CHECK(fs::is_regular_file(replaced.output_directory / "resources.mcpp"));
    const auto still_verified = verify_prepared_resources(data, "1.20.4");
    CHECK(still_verified.valid);
    CHECK(!verify_prepared_resources(data, "../escape").valid);
    const fs::path prepared_object = still_verified.valid
        ? data / "prepared" / "1.20.4" / "assets" / "objects" / "a9" /
          "a9993e364706816aba3e25717850c26c9cd0d89d"
        : fs::path{};
    {
        std::ofstream tampered(prepared_object, std::ios::binary | std::ios::trunc);
        tampered << "bad";
    }
    CHECK(!verify_prepared_resources(data, "1.20.4").valid);
    fs::remove_all(root);

    std::cout << "MineCPP resource tests passed\n";
    return 0;
}
