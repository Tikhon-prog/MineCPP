#include "jar_importer.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <string>
#include <vector>

#include <zlib.h>

using namespace minecpp::launcher;
namespace fs = std::filesystem;

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Test failed: " #expression); } while (false)

namespace {

struct ZipEntry {
    std::string name;
    std::string contents;
    bool deflated = true;
    bool corrupt_crc = false;
    bool symlink = false;
};

struct CentralEntry {
    std::string name;
    std::vector<unsigned char> compressed;
    std::uint32_t crc = 0;
    std::uint32_t uncompressed_size = 0;
    std::uint16_t method = 0;
    std::uint32_t local_offset = 0;
    std::uint32_t external_attributes = 0;
};

void append_u16(std::string& output, std::uint16_t value) {
    output.push_back(static_cast<char>(value & 0xffU));
    output.push_back(static_cast<char>((value >> 8U) & 0xffU));
}

void append_u32(std::string& output, std::uint32_t value) {
    append_u16(output, static_cast<std::uint16_t>(value & 0xffffU));
    append_u16(output, static_cast<std::uint16_t>(value >> 16U));
}

std::vector<unsigned char> deflate_raw(const std::string& contents) {
    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        throw std::runtime_error("Test could not initialize zlib");
    }

    std::vector<unsigned char> result(static_cast<std::size_t>(deflateBound(
        &stream, static_cast<uLong>(contents.size()))));
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(contents.data()));
    stream.avail_in = static_cast<uInt>(contents.size());
    stream.next_out = result.data();
    stream.avail_out = static_cast<uInt>(result.size());
    const int status = deflate(&stream, Z_FINISH);
    const std::size_t written = static_cast<std::size_t>(stream.total_out);
    deflateEnd(&stream);
    if (status != Z_STREAM_END) throw std::runtime_error("Test could not compress fixture data");
    result.resize(written);
    return result;
}

void write_zip(const fs::path& path, const std::vector<ZipEntry>& entries) {
    std::string bytes;
    std::vector<CentralEntry> central;
    for (const ZipEntry& entry : entries) {
        CentralEntry record;
        record.name = entry.name;
        record.method = entry.deflated ? 8U : 0U;
        record.uncompressed_size = static_cast<std::uint32_t>(entry.contents.size());
        record.external_attributes = entry.symlink ? 0xA1FF0000U : 0x81A40000U;
        record.crc = static_cast<std::uint32_t>(::crc32(
            0L, reinterpret_cast<const Bytef*>(entry.contents.data()),
            static_cast<uInt>(entry.contents.size())));
        if (entry.corrupt_crc) record.crc ^= 0x01020304U;
        record.compressed = entry.deflated
            ? deflate_raw(entry.contents)
            : std::vector<unsigned char>(entry.contents.begin(), entry.contents.end());
        record.local_offset = static_cast<std::uint32_t>(bytes.size());

        append_u32(bytes, 0x04034b50U);
        append_u16(bytes, 20U);
        append_u16(bytes, 0U);
        append_u16(bytes, record.method);
        append_u16(bytes, 0U);
        append_u16(bytes, 0U);
        append_u32(bytes, record.crc);
        append_u32(bytes, static_cast<std::uint32_t>(record.compressed.size()));
        append_u32(bytes, record.uncompressed_size);
        append_u16(bytes, static_cast<std::uint16_t>(record.name.size()));
        append_u16(bytes, 0U);
        bytes += record.name;
        bytes.append(reinterpret_cast<const char*>(record.compressed.data()), record.compressed.size());
        central.push_back(std::move(record));
    }

    const std::uint32_t central_offset = static_cast<std::uint32_t>(bytes.size());
    for (const CentralEntry& entry : central) {
        append_u32(bytes, 0x02014b50U);
        append_u16(bytes, 0x0314U);
        append_u16(bytes, 20U);
        append_u16(bytes, 0U);
        append_u16(bytes, entry.method);
        append_u16(bytes, 0U);
        append_u16(bytes, 0U);
        append_u32(bytes, entry.crc);
        append_u32(bytes, static_cast<std::uint32_t>(entry.compressed.size()));
        append_u32(bytes, entry.uncompressed_size);
        append_u16(bytes, static_cast<std::uint16_t>(entry.name.size()));
        append_u16(bytes, 0U);
        append_u16(bytes, 0U);
        append_u16(bytes, 0U);
        append_u16(bytes, 0U);
        append_u32(bytes, entry.external_attributes);
        append_u32(bytes, entry.local_offset);
        bytes += entry.name;
    }

    const std::uint32_t central_size =
        static_cast<std::uint32_t>(bytes.size()) - central_offset;
    append_u32(bytes, 0x06054b50U);
    append_u16(bytes, 0U);
    append_u16(bytes, 0U);
    append_u16(bytes, static_cast<std::uint16_t>(central.size()));
    append_u16(bytes, static_cast<std::uint16_t>(central.size()));
    append_u32(bytes, central_size);
    append_u32(bytes, central_offset);
    append_u16(bytes, 0U);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("Could not write test JAR");
}

template<typename Function>
void expect_throw(Function&& function) {
    bool threw = false;
    try { function(); }
    catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

} // namespace

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path temporary = fs::temp_directory_path() /
        ("minecpp-import-tests-" + std::to_string(stamp));
    fs::create_directories(temporary);

    try {
        CHECK(valid_version_id("1.20.4"));
        CHECK(valid_version_id("26.3"));
        CHECK(!valid_version_id("CON"));
        CHECK(!valid_version_id("com1"));
        CHECK(!valid_version_id("lpt9"));
        CHECK(!valid_version_id("../escape"));
        CHECK(!valid_version_id("C:\\bad"));
        CHECK(!valid_version_id(""));
        CHECK(!valid_version_id("CON"));
        CHECK(!valid_version_id("com1"));
        CHECK(!valid_version_id("LPT9.log"));

        const fs::path source_directory = temporary / "minecraft" / "versions" / "1.20.4";
        fs::create_directories(source_directory);
        const fs::path jar = source_directory / "1.20.4.jar";
        write_zip(jar, {
            {"META-INF/MANIFEST.MF", "Manifest-Version: 1.0\n", false, false},
            {"net/minecraft/client/main/Main.class", "compiled-class-placeholder", true, false},
            {"assets/minecraft/lang/en_us.json", "{\"menu.singleplayer\":\"Singleplayer\"}", true, false}
        });
        {
            std::ofstream json(source_directory / "1.20.4.json");
            json << "{\"id\":\"1.20.4\",\"mainClass\":\"net.minecraft.client.main.Main\"}\n";
        }

        const fs::path data_directory = temporary / "minecpp-data";
        const ImportSummary imported = import_jar(jar, data_directory);
        CHECK(imported.version_id == "1.20.4");
        CHECK(imported.file_count == 4U);
        CHECK(imported.copied_version_metadata);
        CHECK(fs::is_regular_file(imported.version_directory / "client" /
                                  "net/minecraft/client/main/Main.class"));
        CHECK(fs::is_regular_file(imported.version_directory / "metadata" / "version.json"));

        const VerificationResult good = verify_import("1.20.4", data_directory);
        CHECK(good.ok);
        CHECK(good.checked_files == 4U);

        // Importing into the source .minecraft directory must never replace the original JAR.
        expect_throw([&] {
            (void)import_jar(jar, temporary / "minecraft", "1.20.4", true);
        });
        CHECK(fs::is_regular_file(jar));

        // A generic client.jar should use its Minecraft versions directory as the version ID.
        const fs::path generic_jar = source_directory / "client.jar";
        fs::copy_file(jar, generic_jar);
        const ImportSummary auto_detected =
            import_jar(generic_jar, temporary / "auto-detect-data");
        CHECK(auto_detected.version_id == "1.20.4");
        CHECK(auto_detected.copied_version_metadata);

        // A hand-edited manifest must not be able to introduce traversal paths or signed sizes.
        const auto expect_bad_manifest = [&](const std::string& suffix, const std::string& line) {
            const fs::path bad_data = temporary / ("bad-manifest-" + suffix);
            const fs::path bad_version = bad_data / "versions" / "1.20.4";
            fs::create_directories(bad_version.parent_path());
            fs::copy(imported.version_directory, bad_version, fs::copy_options::recursive);
            std::ofstream manifest(bad_version / "manifest.mcpp", std::ios::binary | std::ios::app);
            manifest << line << '\n';
            manifest.close();
            CHECK(!verify_import("1.20.4", bad_data).ok);
        };
        expect_bad_manifest("traversal", "ENTRY\t0\t00000000\tclient/%2E%2E/escaped.txt");
        expect_bad_manifest("negative-size", "ENTRY\t-1\t00000000\tclient/extra.bin");

        const auto versions = list_imports(data_directory);
        CHECK(versions.size() == 1U);
        CHECK(versions.front().version_id == "1.20.4");

        expect_throw([&] { (void)import_jar(jar, data_directory); });

        const fs::path edited_file = imported.version_directory / "client" /
            "assets/minecraft/lang/en_us.json";
        {
            std::ofstream edited(edited_file, std::ios::binary | std::ios::trunc);
            edited << "tampered";
        }
        const VerificationResult damaged = verify_import("1.20.4", data_directory);
        CHECK(!damaged.ok);
        CHECK(!damaged.errors.empty());

        const ImportSummary replaced = import_jar(jar, data_directory, "1.20.4", true);
        CHECK(replaced.version_id == "1.20.4");
        CHECK(verify_import("1.20.4", data_directory).ok);

        const fs::path external_file = temporary / "external-copy.bin";
        {
            std::ofstream external(external_file, std::ios::binary);
            external << "{\"menu.singleplayer\":\"Singleplayer\"}";
        }
        const fs::path imported_file = replaced.version_directory / "client" /
            "assets/minecraft/lang/en_us.json";
        fs::remove(imported_file);
        std::error_code symlink_error;
        fs::create_symlink(external_file, imported_file, symlink_error);
        if (!symlink_error) {
            CHECK(!verify_import("1.20.4", data_directory).ok);
            fs::remove(imported_file);
            const ImportSummary restored = import_jar(jar, data_directory, "1.20.4", true);
            CHECK(verify_import("1.20.4", data_directory).ok);
            CHECK(restored.version_id == "1.20.4");
        } else {
            const ImportSummary restored = import_jar(jar, data_directory, "1.20.4", true);
            CHECK(verify_import("1.20.4", data_directory).ok);
            CHECK(restored.version_id == "1.20.4");
        }

        const fs::path manifest_path = replaced.version_directory / "manifest.mcpp";
        const fs::path manifest_copy = temporary / "manifest-copy.mcpp";
        fs::copy_file(manifest_path, manifest_copy);
        fs::remove(manifest_path);
        std::error_code manifest_link_error;
        fs::create_symlink(manifest_copy, manifest_path, manifest_link_error);
        if (!manifest_link_error) {
            CHECK(!verify_import("1.20.4", data_directory).ok);
            fs::remove(manifest_path);
        }
        fs::copy_file(manifest_copy, manifest_path);
        CHECK(verify_import("1.20.4", data_directory).ok);

        const fs::path malicious_jar = temporary / "malicious.jar";
        write_zip(malicious_jar, {
            {"../../escaped.txt", "must never be extracted", false, false},
            {"net/minecraft/client/main/Main.class", "class", false, false}
        });
        expect_throw([&] {
            (void)import_jar(malicious_jar, data_directory, "1.20.5");
        });
        CHECK(!fs::exists(data_directory / "versions" / "escaped.txt"));

        const fs::path symlink_jar = temporary / "symlink.jar";
        write_zip(symlink_jar, {
            {"linked.class", "target", false, false, true},
            {"net/minecraft/client/main/Main.class", "class", false, false}
        });
        expect_throw([&] {
            (void)import_jar(symlink_jar, data_directory, "1.20.8");
        });

        const fs::path duplicate_jar = temporary / "duplicate-paths.jar";
        write_zip(duplicate_jar, {
            {"net/minecraft/client/main/Main.class", "class", false, false},
            {"assets/test.txt", "first", false, false},
            {"ASSETS/TEST.TXT", "second", false, false}
        });
        expect_throw([&] {
            (void)import_jar(duplicate_jar, data_directory, "1.20.9");
        });

        const fs::path device_jar = temporary / "windows-device.jar";
        write_zip(device_jar, {
            {"CON.txt", "reserved device name", false, false},
            {"net/minecraft/client/main/Main.class", "class", false, false}
        });
        expect_throw([&] {
            (void)import_jar(device_jar, data_directory, "1.20.7");
        });

        const fs::path corrupt_jar = temporary / "corrupt.jar";
        write_zip(corrupt_jar, {
            {"net/minecraft/client/main/Main.class", "class bytes", true, true}
        });
        expect_throw([&] {
            (void)import_jar(corrupt_jar, data_directory, "1.20.6");
        });

        const fs::path duplicate_jar = temporary / "case-collision.jar";
        write_zip(duplicate_jar, {
            {"net/minecraft/client/main/Main.class", "class", false, false},
            {"Assets/thing.bin", "first", false, false},
            {"assets/THING.bin", "second", false, false}
        });
        expect_throw([&] {
            (void)import_jar(duplicate_jar, data_directory, "1.20.8");
        });
        CHECK(!fs::exists(data_directory / "versions" / "1.20.8"));

        fs::remove_all(temporary);
        std::cout << "MineCPP importer tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code cleanup_error;
        fs::remove_all(temporary, cleanup_error);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
