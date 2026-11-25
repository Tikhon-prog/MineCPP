#include "json.hpp"
#include "version_metadata.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace minecpp::launcher;

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Test failed: " #expression); } while (false)

template<typename Function>
void expect_throw(Function&& function) {
    bool threw = false;
    try { function(); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

int main() {
    const auto parsed = minecpp::json::parse(
        R"({"text":"hello \uD83D\uDE80","count":3,"ok":true,"values":[null,false,1.5]})");
    CHECK(parsed.find("text")->as_string().size() > 6U);
    CHECK(parsed.find("count")->as_number() == 3.0);
    CHECK(parsed.find("ok")->as_bool());
    CHECK(parsed.find("values")->as_array().size() == 3U);

    expect_throw([] { (void)minecpp::json::parse(R"({"a":1,"a":2})"); });
    expect_throw([] { (void)minecpp::json::parse(R"({"n":01})"); });
    expect_throw([] { (void)minecpp::json::parse(R"({"x":"\uD800"})"); });
    expect_throw([] { (void)minecpp::json::parse(R"({"a":[1,]})"); });
    expect_throw([] { (void)minecpp::json::parse(R"({"a":1} trailing)"); });
    expect_throw([] { (void)minecpp::json::parse(std::string("{\"x\":\"") + static_cast<char>(0xc0) + "\"}"); });

    const std::string version_json = R"({
      "id":"1.20.4",
      "mainClass":"net.minecraft.client.main.Main",
      "assets":"7",
      "assetIndex":{"id":"7","url":"https://example.invalid/index.json",
                    "sha1":"0123456789012345678901234567890123456789","size":20},
      "javaVersion":{"majorVersion":17},
      "logging":{"client":{"file":{"id":"client-log.xml","sha1":"0123456789012345678901234567890123456789","size":512,"url":"https://example.invalid/client-log.xml"}}},
      "libraries":[
        {"name":"com.example:library:1.0","downloads":{"artifact":{
          "path":"com/example/library/1.0/library-1.0.jar",
          "url":"https://example.invalid/library.jar",
          "sha1":"abcdef0123456789abcdef0123456789abcdef01","size":1234}}},
        {"name":"com.example:platform:1.0","downloads":{}}
      ]
    })";
    const auto metadata = parse_version_metadata(version_json, "1.20.4");
    CHECK(metadata.id == "1.20.4");
    CHECK(metadata.main_class == "net.minecraft.client.main.Main");
    CHECK(metadata.assets == "7");
    CHECK(metadata.java_major_version == 17);
    CHECK(metadata.has_logging_config);
    CHECK(metadata.logging_config.path == "client-log.xml");
    CHECK(metadata.logging_config.has_sha1);
    CHECK(metadata.logging_config.size == 512U);
    CHECK(metadata.inherits_from.empty());
    CHECK(metadata.has_asset_index);
    CHECK(metadata.asset_index_id == "7");
    CHECK(metadata.declared_library_count == 2U);
    CHECK(metadata.has_unresolved_library_requirements);
    CHECK(metadata.artifacts.size() == 1U);
    CHECK(metadata.artifacts.front().has_sha1);
    CHECK(metadata.artifacts.front().size == 1234U);

    CHECK(safe_relative_resource_path("com/example/library.jar"));
    CHECK(!safe_relative_resource_path("CON.txt"));
    CHECK(!safe_relative_resource_path("NUL"));
    CHECK(!safe_relative_resource_path("folder/trailing."));
    CHECK(!safe_relative_resource_path("../outside.jar"));
    CHECK(!safe_relative_resource_path("/absolute.jar"));
    CHECK(!safe_relative_resource_path("C:/absolute.jar"));
    CHECK(!safe_relative_resource_path("com\\example\\library.jar"));
    CHECK(!safe_relative_resource_path("com//library.jar"));

    expect_throw([&] { (void)parse_version_metadata(version_json, "1.20.5"); });
    expect_throw([] { (void)parse_version_metadata(R"({"id":"1.20.4","mainClass":"Main","assetIndex":{"id":"../outside"}})"); });
    expect_throw([] { (void)parse_version_metadata(R"({"id":"1.20.4","mainClass":"Main","logging":{"client":{"file":{"id":"../outside.xml"}}}})"); });
    expect_throw([] {
        (void)parse_version_metadata(
            R"({"id":"1.20.4","mainClass":"Main","libraries":[{"downloads":{"artifact":{"path":"../../escape.jar"}}}]})");
    });
    expect_throw([] {
        (void)parse_version_metadata(
            R"({"id":"1.20.4","mainClass":"Main","assetIndex":{"id":"x","sha1":"not-a-hash"}})");
    });
    expect_throw([] {
        (void)parse_version_metadata(R"({"id":"1.20.4"})");
    });

    std::cout << "MineCPP metadata tests passed\n";
    return 0;
}
