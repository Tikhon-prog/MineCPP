#include "minecraft.hpp"

#include "json.hpp"
#include "process.hpp"
#include "zip.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>
#else
#include <sys/utsname.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#endif

namespace minecpp {
namespace fs = std::filesystem;
namespace {

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open file: " + path.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!in.good() && !in.eof()) throw std::runtime_error("Cannot read file: " + path.string());
    return buffer.str();
}

void write_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot write file: " + path.string());
    out << content;
    if (!out) throw std::runtime_error("Failed writing file: " + path.string());
}

bool valid_id(const std::string& id) {
    return !id.empty() && id != "." && id != ".." &&
        std::all_of(id.begin(), id.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '.' || c == '_' || c == '-';
        });
}

std::string current_os() {
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "osx";
#else
    return "linux";
#endif
}

std::string current_arch() {
#if defined(_M_ARM64) || defined(__aarch64__)
    return "aarch64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return "x86_64";
#endif
}

std::string os_version() {
#ifdef _WIN32
    using Fn = LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
    const HMODULE module = GetModuleHandleW(L"ntdll.dll");
    const auto fn = module ? reinterpret_cast<Fn>(GetProcAddress(module, "RtlGetVersion")) : nullptr;
    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (fn && fn(&info) == 0)
        return std::to_string(info.dwMajorVersion) + "." + std::to_string(info.dwMinorVersion) + "." + std::to_string(info.dwBuildNumber);
    return {};
#elif defined(__APPLE__)
    char value[128]{};
    std::size_t size = sizeof(value);
    return sysctlbyname("kern.osproductversion", value, &size, nullptr, 0) == 0 ? value : "";
#else
    struct utsname info{};
    return uname(&info) == 0 ? info.release : "";
#endif
}

bool rule_matches(const Json& rule) {
    const Json* os = rule.get("os");
    if (os && os->is_object()) {
        const std::string name = os->get_string("name");
        if (!name.empty() && name != current_os()) return false;
        const std::string arch = os->get_string("arch");
        if (!arch.empty() && arch != current_arch() && !(arch == "x86" && current_arch() == "x86_64")) return false;
        const std::string pattern = os->get_string("version");
        if (!pattern.empty()) {
            try {
                const std::string actual = os_version();
                if (actual.empty() || !std::regex_search(actual, std::regex(pattern))) return false;
            } catch (const std::regex_error&) { return false; }
        }
    }
    // MineCPP does not expose launcher UI feature flags; those features are false.
    const Json* features = rule.get("features");
    if (features && features->is_object()) {
        for (const auto& [key, value] : features->object()) {
            (void)key;
            if (value.boolean(false)) return false;
        }
    }
    return true;
}

bool allowed_by_rules(const Json& item) {
    const Json* rules = item.get("rules");
    if (!rules || !rules->is_array()) return true;
    bool allowed = false;
    for (const Json& rule : rules->array()) {
        if (rule_matches(rule)) allowed = rule.get_string("action") == "allow";
    }
    return allowed;
}

std::string maven_path(const std::string& coordinate) {
    std::string value = coordinate;
    std::string extension = "jar";
    const auto at = value.find('@');
    if (at != std::string::npos) {
        extension = value.substr(at + 1);
        value.erase(at);
    }
    std::vector<std::string> parts;
    std::stringstream input(value);
    std::string part;
    while (std::getline(input, part, ':')) parts.push_back(part);
    if (parts.size() < 3) return {};
    std::string path;
    std::stringstream group(parts[0]);
    while (std::getline(group, part, '.')) path += part + "/";
    path += parts[1] + "/" + parts[2] + "/" + parts[1] + "-" + parts[2];
    if (parts.size() > 3) path += "-" + parts[3];
    return path + "." + extension;
}

std::string expand_arch(std::string value) {
    const auto position = value.find("${arch}");
    if (position != std::string::npos)
        value.replace(position, 7, current_arch() == "x86" ? "32" : "64");
    return value;
}

std::string classpath_separator() {
#ifdef _WIN32
    return ";";
#else
    return ":";
#endif
}

std::string join_classpath(const std::vector<fs::path>& paths) {
    std::string result;
    for (const auto& path : paths) {
        if (!result.empty()) result += classpath_separator();
        result += path.string();
    }
    return result;
}

std::string quote_display(const std::string& text) {
    if (text.find_first_of(" \t\n\"") == std::string::npos) return text;
    std::string result = "\"";
    for (char c : text) {
        if (c == '\"') result += "\\\"";
        else result += c;
    }
    return result + "\"";
}

std::string offline_uuid(const std::string& username) {
    // RFC 1321 MD5; Java's offline UUID is UUIDv3 over "OfflinePlayer:<name>".
    static constexpr std::array<std::uint32_t, 64> shifts{
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
    };
    static constexpr std::array<std::uint32_t, 64> constants{
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe9b6c7e6,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
    };
    std::string data = "OfflinePlayer:" + username;
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8ULL;
    data.push_back(static_cast<char>(0x80));
    while (data.size() % 64 != 56) data.push_back('\0');
    for (int i = 0; i < 8; ++i) data.push_back(static_cast<char>((bits >> (8 * i)) & 0xffU));

    std::uint32_t a0 = 0x67452301U, b0 = 0xefcdab89U, c0 = 0x98badcfeU, d0 = 0x10325476U;
    for (std::size_t offset = 0; offset < data.size(); offset += 64) {
        std::array<std::uint32_t, 16> words{};
        for (std::size_t i = 0; i < 16; ++i) {
            const std::size_t p = offset + i * 4;
            words[i] = static_cast<std::uint32_t>(static_cast<unsigned char>(data[p])) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(data[p + 1])) << 8U) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(data[p + 2])) << 16U) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(data[p + 3])) << 24U);
        }
        std::uint32_t a = a0, b = b0, c = c0, d = d0;
        for (std::uint32_t i = 0; i < 64; ++i) {
            std::uint32_t f = 0, g = 0;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7 * i) % 16; }
            const std::uint32_t sum = a + f + constants[i] + words[g];
            const std::uint32_t rotated = (sum << shifts[i]) | (sum >> (32U - shifts[i]));
            a = d; d = c; c = b; b += rotated;
        }
        a0 += a; b0 += b; c0 += c; d0 += d;
    }

    std::array<unsigned char, 16> digest{};
    const std::array<std::uint32_t, 4> state{a0, b0, c0, d0};
    for (std::size_t i = 0; i < 4; ++i)
        for (std::size_t j = 0; j < 4; ++j)
            digest[i * 4 + j] = static_cast<unsigned char>((state[i] >> (j * 8)) & 0xffU);
    digest[6] = static_cast<unsigned char>((digest[6] & 0x0fU) | 0x30U);
    digest[8] = static_cast<unsigned char>((digest[8] & 0x3fU) | 0x80U);
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < digest.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) output << '-';
        output << std::setw(2) << static_cast<unsigned int>(digest[i]);
    }
    return output.str();
}

std::vector<std::string> split_legacy_args(const std::string& text) {
    std::vector<std::string> result;
    std::string current;
    bool quoted = false, escaped = false;
    for (char c : text) {
        if (escaped) { current.push_back(c); escaped = false; continue; }
        if (c == '\\' && quoted) { escaped = true; continue; }
        if (c == '\"') { quoted = !quoted; continue; }
        if (std::isspace(static_cast<unsigned char>(c)) && !quoted) {
            if (!current.empty()) { result.push_back(std::move(current)); current.clear(); }
        } else current.push_back(c);
    }
    if (escaped) current.push_back('\\');
    if (!current.empty()) result.push_back(std::move(current));
    return result;
}

void append_values(std::vector<std::string>& result, const Json& value) {
    if (value.is_string()) result.push_back(value.string());
    else if (value.is_array())
        for (const Json& item : value.array())
            if (item.is_string()) result.push_back(item.string());
}

void append_arguments(std::vector<std::string>& result, const Json* values) {
    if (!values || !values->is_array()) return;
    for (const Json& item : values->array()) {
        if (item.is_string()) { result.push_back(item.string()); continue; }
        if (!item.is_object()) continue;
        if (allowed_by_rules(item)) {
            if (const Json* value = item.get("value")) append_values(result, *value);
        }
    }
}

std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    if (from.empty()) return text;
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::string substitute(std::string text, const std::map<std::string, std::string, std::less<>>& variables) {
    for (const auto& [key, value] : variables)
        text = replace_all(std::move(text), "${" + key + "}", value);
    return text;
}

fs::path version_source_folder(const fs::path& minecraft_dir, const std::string& id) {
    return minecraft_dir / "versions" / id;
}

} // namespace

fs::path default_minecraft_dir() {
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA")) return fs::path(appdata) / ".minecraft";
    if (const char* home = std::getenv("USERPROFILE")) return fs::path(home) / "AppData" / "Roaming" / ".minecraft";
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) return fs::path(home) / "Library" / "Application Support" / "minecraft";
#else
    if (const char* home = std::getenv("HOME")) return fs::path(home) / ".minecraft";
#endif
    return fs::current_path() / ".minecraft";
}

fs::path default_data_dir() {
#ifdef _WIN32
    if (const char* local = std::getenv("LOCALAPPDATA")) return fs::path(local) / "MineCPP";
    if (const char* home = std::getenv("USERPROFILE")) return fs::path(home) / "AppData" / "Local" / "MineCPP";
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) return fs::path(home) / "Library" / "Application Support" / "MineCPP";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) return fs::path(xdg) / "MineCPP";
    if (const char* home = std::getenv("HOME")) return fs::path(home) / ".local" / "share" / "MineCPP";
#endif
    return fs::current_path() / ".minecpp";
}

void list_versions(const fs::path& minecraft_dir) {
    const fs::path root = minecraft_dir / "versions";
    if (!fs::exists(root)) throw std::runtime_error("Minecraft versions folder not found: " + root.string());
    std::cout << "Minecraft directory: " << minecraft_dir << "\n\n";
    std::size_t count = 0;
    for (const auto& item : fs::directory_iterator(root)) {
        if (!item.is_directory()) continue;
        const std::string id = item.path().filename().string();
        const fs::path json = item.path() / (id + ".json");
        const fs::path jar = item.path() / (id + ".jar");
        if (!fs::exists(json) || !fs::exists(jar)) continue;
        std::string type = "unknown";
        try { type = Json::parse(read_file(json)).get_string("type", "unknown"); } catch (...) {}
        std::cout << "  " << id << " [" << type << "]  " << fs::file_size(jar) / (1024 * 1024) << " MiB\n";
        ++count;
    }
    if (count == 0) std::cout << "No complete version folders found.\n";
}

void list_profiles(const fs::path& data_dir) {
    const fs::path root = data_dir / "profiles";
    std::cout << "MineCPP data directory: " << data_dir << "\n\n";
    if (!fs::exists(root)) { std::cout << "No imported profiles yet.\n"; return; }
    std::size_t count = 0;
    for (const auto& item : fs::directory_iterator(root)) {
        if (item.is_directory() && fs::exists(item.path() / "version.json") && fs::exists(item.path() / "client.jar")) {
            std::cout << "  " << item.path().filename().string() << '\n';
            ++count;
        }
    }
    if (count == 0) std::cout << "No imported profiles yet.\n";
}

void inspect_jar(const fs::path& jar) {
    ZipArchive archive(jar);
    const auto classes = std::count_if(archive.entries().begin(), archive.entries().end(), [](const ZipEntryInfo& entry) {
        return !entry.directory && entry.name.ends_with(".class");
    });
    const auto manifest = std::find_if(archive.entries().begin(), archive.entries().end(), [](const ZipEntryInfo& entry) {
        return entry.name == "META-INF/MANIFEST.MF";
    });
    std::cout << "JAR: " << jar << "\nFiles: " << archive.count_files()
              << "\nJava classes: " << classes
              << "\nManifest: " << (manifest != archive.entries().end() ? "present" : "missing") << '\n';
}

void import_version(const std::string& id, const fs::path& minecraft_dir, const fs::path& data_dir,
                    const fs::path& jar_override, const fs::path& metadata_override) {
    if (!valid_id(id)) throw std::runtime_error("Invalid version id: " + id);
    const fs::path source = version_source_folder(minecraft_dir, id);
    const fs::path json_path = metadata_override.empty() ? source / (id + ".json") : metadata_override;
    const fs::path jar_path = jar_override.empty() ? source / (id + ".jar") : jar_override;
    if (!fs::exists(json_path)) throw std::runtime_error("Version metadata not found: " + json_path.string());
    if (!fs::exists(jar_path)) throw std::runtime_error("Version JAR not found: " + jar_path.string());

    const Json metadata = Json::parse(read_file(json_path));
    if (!metadata.is_object()) throw std::runtime_error("Version metadata must be a JSON object");
    const std::string metadata_id = metadata.get_string("id");
    if (!metadata_id.empty() && metadata_id != id) throw std::runtime_error("Version JSON id does not match selected id");
    const std::string main_class = metadata.get_string("mainClass");
    if (main_class.empty()) throw std::runtime_error("Version metadata does not contain mainClass");
    ZipArchive archive(jar_path);
    if (archive.count_files() == 0) throw std::runtime_error("Selected JAR is empty");
    std::string main_entry = main_class;
    std::replace(main_entry.begin(), main_entry.end(), '.', '/');
    main_entry += ".class";
    const bool has_main = std::any_of(archive.entries().begin(), archive.entries().end(), [&](const ZipEntryInfo& entry) {
        return !entry.directory && entry.name == main_entry;
    });
    if (!has_main) throw std::runtime_error("JAR does not contain declared main class: " + main_entry);

    const fs::path profiles = data_dir / "profiles";
    fs::create_directories(profiles);
    const fs::path staging = profiles / (id + ".importing");
    const fs::path target = profiles / id;
    const fs::path backup = profiles / (id + ".previous");
    std::error_code ec;
    fs::remove_all(staging, ec);
    fs::create_directories(staging);
    try {
        fs::copy_file(jar_path, staging / "client.jar", fs::copy_options::overwrite_existing);
        fs::copy_file(json_path, staging / "version.json", fs::copy_options::overwrite_existing);
        write_file(staging / "source-game-dir.txt", fs::absolute(minecraft_dir).string());
        std::cout << "Extracting " << archive.count_files() << " JAR entries...\n";
        const std::size_t extracted = archive.extract_all(staging / "extracted");
        fs::remove_all(backup, ec);
        const bool had_previous = fs::exists(target);
        if (had_previous) fs::rename(target, backup);
        try {
            fs::rename(staging, target);
            fs::remove_all(backup, ec);
        } catch (...) {
            if (had_previous && !fs::exists(target) && fs::exists(backup)) fs::rename(backup, target);
            throw;
        }
        std::cout << "Imported Minecraft " << id << " successfully.\n"
                  << "  Profile: " << target << "\n"
                  << "  Extracted files: " << extracted << "\n"
                  << "  Original installation was not modified.\n";
    } catch (...) {
        fs::remove_all(staging, ec);
        throw;
    }
}

int launch_version(const std::string& id, const std::string& username, const fs::path& minecraft_dir_arg,
                   const fs::path& data_dir, bool dry_run) {
    if (!valid_id(id)) throw std::runtime_error("Invalid version id: " + id);
    if (username.empty() || username.size() > 16 || !std::isalpha(static_cast<unsigned char>(username[0])))
        throw std::runtime_error("Offline username must start with a letter and contain 1-16 characters");
    for (unsigned char c : username)
        if (!std::isalnum(c) && c != '_')
            throw std::runtime_error("Offline username may contain only letters, digits and underscore");

    const fs::path profile = data_dir / "profiles" / id;
    const fs::path json_path = profile / "version.json";
    const fs::path jar_path = profile / "client.jar";
    if (!fs::exists(json_path) || !fs::exists(jar_path))
        throw std::runtime_error("Profile not imported. Run: minecpp import " + id);
    const Json metadata = Json::parse(read_file(json_path));
    const fs::path minecraft_dir = minecraft_dir_arg.empty()
        ? fs::path(read_file(profile / "source-game-dir.txt")) : minecraft_dir_arg;
    const fs::path libraries_dir = minecraft_dir / "libraries";
    const fs::path assets_dir = minecraft_dir / "assets";
    if (!fs::exists(libraries_dir)) throw std::runtime_error("Minecraft libraries folder not found: " + libraries_dir.string());
    if (!fs::exists(assets_dir)) throw std::runtime_error("Minecraft assets folder not found: " + assets_dir.string());
    const std::string main_class = metadata.get_string("mainClass");
    if (main_class.empty()) throw std::runtime_error("Version metadata does not contain mainClass");
    if (metadata.contains("inheritsFrom"))
        throw std::runtime_error("Inherited or modded version metadata is not supported in MineCPP 0.1.0");

    std::vector<fs::path> classpath{fs::absolute(jar_path)};
    std::vector<std::string> missing;
    const fs::path natives_dir = profile / "natives";
    std::error_code ec;
    fs::remove_all(natives_dir, ec);
    fs::create_directories(natives_dir);

    const Json* libraries = metadata.get("libraries");
    if (libraries && libraries->is_array()) {
        for (const Json& library : libraries->array()) {
            if (!allowed_by_rules(library)) continue;
            const Json* downloads = library.get("downloads");
            const Json* artifact = downloads ? downloads->get("artifact") : nullptr;
            std::string relative = artifact ? artifact->get_string("path") : "";
            const bool classifiers_only = downloads && downloads->contains("classifiers") && !artifact;
            if (relative.empty() && !classifiers_only) relative = maven_path(library.get_string("name"));
            if (!relative.empty()) {
                const fs::path path = libraries_dir / fs::path(relative);
                if (fs::exists(path)) classpath.push_back(fs::absolute(path));
                else missing.push_back(path.string());
            }
            const Json* natives = library.get("natives");
            const Json* classifiers = downloads ? downloads->get("classifiers") : nullptr;
            if (natives && classifiers && natives->is_object() && classifiers->is_object()) {
                const std::string key = expand_arch(natives->get_string(current_os()));
                if (!key.empty()) {
                    const Json* native_info = classifiers->get(key);
                    if (!native_info) {
                        missing.push_back("missing native classifier '" + key + "' for " + library.get_string("name"));
                    } else {
                        const std::string native_path = native_info->get_string("path");
                        if (native_path.empty()) missing.push_back("native classifier has no path: " + key);
                        else {
                            const fs::path native_jar = libraries_dir / fs::path(native_path);
                            if (!fs::exists(native_jar)) missing.push_back(native_jar.string());
                            else (void)ZipArchive(native_jar).extract_all(natives_dir);
                        }
                    }
                }
            }
        }
    }
    if (!missing.empty()) {
        std::ostringstream message;
        message << "Required local libraries are missing (MineCPP does not download files):";
        for (const auto& item : missing) message << "\n  " << item;
        throw std::runtime_error(message.str());
    }

    const std::string assets_id = metadata.get("assetIndex")
        ? metadata.get("assetIndex")->get_string("id") : metadata.get_string("assets", id);
    const fs::path asset_index = assets_dir / "indexes" / (assets_id + ".json");
    if (!assets_id.empty() && !fs::exists(asset_index))
        throw std::runtime_error("Local asset index not found: " + asset_index.string());

    const std::string classpath = join_classpath(classpath);
    const std::string natives_path = fs::absolute(natives_dir).string();
    std::map<std::string, std::string, std::less<>> vars{
        {"auth_player_name", username}, {"version_name", id},
        {"game_directory", fs::absolute(minecraft_dir).string()},
        {"assets_root", fs::absolute(assets_dir).string()}, {"assets_index_name", assets_id},
        {"auth_uuid", offline_uuid(username)}, {"auth_access_token", "0"},
        {"user_type", "legacy"}, {"version_type", metadata.get_string("type", "release")},
        {"natives_directory", natives_path}, {"launcher_name", "MineCPP"}, {"launcher_version", "0.1.0"},
        {"library_directory", fs::absolute(libraries_dir).string()}, {"classpath", classpath},
        {"classpath_separator", classpath_separator()}, {"user_properties", "{}"},
        {"resolution_width", "854"}, {"resolution_height", "480"}, {"clientid", ""}, {"auth_xuid", ""}
    };

    std::vector<std::string> arguments;
    const Json* sections = metadata.get("arguments");
    if (sections && sections->is_object()) append_arguments(arguments, sections->get("jvm"));
    else {
        arguments.emplace_back("-Djava.library.path=" + natives_path);
        arguments.emplace_back("-cp");
        arguments.push_back(classpath);
    }
    for (std::string& argument : arguments) argument = substitute(std::move(argument), vars);
    bool has_classpath = false;
    for (const auto& argument : arguments) {
        if (argument == "-cp" || argument == "-classpath" || argument == "--class-path" ||
            argument.rfind("-cp=", 0) == 0 || argument.rfind("--class-path=", 0) == 0)
            has_classpath = true;
    }
    if (!has_classpath) { arguments.emplace_back("-cp"); arguments.push_back(classpath); }

    if (const Json* logging = metadata.get("logging")) {
        if (const Json* client = logging->get("client")) {
            const std::string flag = client->get_string("argument");
            const Json* file = client->get("file");
            if (!flag.empty() && file) {
                auto log_vars = vars;
                log_vars["path"] = (assets_dir / "log_configs" / file->get_string("id")).string();
                arguments.push_back(substitute(flag, log_vars));
            }
        }
    }

    arguments.push_back(main_class);
    if (sections && sections->is_object()) append_arguments(arguments, sections->get("game"));
    else {
        const std::string legacy = metadata.get_string("minecraftArguments");
        if (!legacy.empty()) {
            auto old_args = split_legacy_args(legacy);
            arguments.insert(arguments.end(), old_args.begin(), old_args.end());
        }
    }
    for (std::string& argument : arguments) argument = substitute(std::move(argument), vars);

    std::cout << "Launching Minecraft " << id << " offline as " << username << "\n";
    std::cout << "Java: " << find_java() << "\n";
    if (dry_run) {
        std::cout << "Command (dry run): " << quote_display(find_java());
        for (const auto& argument : arguments) std::cout << ' ' << quote_display(argument);
        std::cout << '\n';
        return 0;
    }
    const int result = run_process(find_java(), arguments, fs::absolute(minecraft_dir).string());
    if (result != 0) std::cerr << "Minecraft exited with code " << result << '\n';
    return result;
}

} // namespace minecpp
