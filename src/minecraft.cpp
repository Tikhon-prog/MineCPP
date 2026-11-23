#include "minecraft.hpp"

#include "json.hpp"
#include "process.hpp"
#include "zip.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
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
#include <string_view>
#include <system_error>
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

#ifdef _WIN32
fs::path windows_environment_path(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required <= 1U) return {};
    std::wstring value(static_cast<std::size_t>(required), L'\\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
    if (written == 0U || written >= required) return {};
    value.resize(static_cast<std::size_t>(written));
    return fs::path(value);
}
#endif

std::string path_to_utf8(const fs::path& path) {
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

fs::path path_from_utf8(const std::string& value) {
    return fs::u8path(value);
}

std::string read_file(const fs::path& path) {
    std::error_code size_error;
    const std::uint64_t size = fs::file_size(path, size_error);
    if (size_error) throw std::runtime_error("Cannot inspect file: " + path_to_utf8(path));
    if (size > 32ULL * 1024ULL * 1024ULL)
        throw std::runtime_error("Refusing to read a file larger than 32 MiB: " + path_to_utf8(path));

    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open file: " + path_to_utf8(path));
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!in.good() && !in.eof()) throw std::runtime_error("Cannot read file: " + path_to_utf8(path));
    const std::string content = buffer.str();
    if (content.size() > 32ULL * 1024ULL * 1024ULL)
        throw std::runtime_error("File grew beyond the 32 MiB read limit: " + path_to_utf8(path));
    return content;
}

void write_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot write file: " + path_to_utf8(path));
    out << content;
    if (!out) throw std::runtime_error("Failed writing file: " + path_to_utf8(path));
}

bool valid_java_class_name(const std::string& name) {
    if (name.empty()) return false;
    bool segment_start = true;
    for (const unsigned char c : name) {
        if (c == '.') {
            if (segment_start) return false;
            segment_start = true;
            continue;
        }
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            c == '_' || c == '$';
        const bool digit = c >= '0' && c <= '9';
        if (segment_start ? !letter : !(letter || digit)) return false;
        segment_start = false;
    }
    return !segment_start;
}

fs::path safe_relative_path(std::string value, const char* description) {
    if (value.empty() || value.find('\0') != std::string::npos)
        throw std::runtime_error(std::string("Invalid empty or NUL-containing ") + description + " path");
    for (char& ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte < 0x20U || byte == 0x7fU)
            throw std::runtime_error(std::string("Control character in ") + description + " path");
        if (ch == '\\') ch = '/';
    }
    if (value.front() == '/' || value.rfind("//", 0) == 0)
        throw std::runtime_error(std::string("Absolute ") + description + " path is not allowed");

    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t end = value.find('/', begin);
        const std::string part = value.substr(begin,
            (end == std::string::npos ? value.size() : end) - begin);
        if (part.empty() || part == "." || part == ".." ||
            part.find(':') != std::string::npos || part.back() == '.' || part.back() == ' ' ||
            part.find_first_of("<>\"|?*") != std::string::npos) {
            throw std::runtime_error(std::string("Unsafe ") + description + " path: " + value);
        }

        std::string device;
        for (const unsigned char c : part.substr(0, part.find('.')))
            device.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c));
        const bool numbered_device = device.size() == 4U &&
            (device.rfind("com", 0) == 0 || device.rfind("lpt", 0) == 0) &&
            device[3] >= '1' && device[3] <= '9';
        if (device == "con" || device == "prn" || device == "aux" || device == "nul" ||
            device == "conin$" || device == "conout$" || numbered_device) {
            throw std::runtime_error(std::string("Reserved Windows name in ") + description + " path");
        }

        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return path_from_utf8(value);
}

bool path_is_within(const fs::path& root, const fs::path& candidate) {
    auto root_it = root.begin();
    auto candidate_it = candidate.begin();
    for (; root_it != root.end() && candidate_it != candidate.end(); ++root_it, ++candidate_it) {
        if (*root_it != *candidate_it) return false;
    }
    return root_it == root.end();
}

fs::path safe_local_child(const fs::path& root, const std::string& relative,
                          const char* description) {
    const fs::path child = root / safe_relative_path(relative, description);
    std::error_code error;
    const fs::path canonical_root = fs::weakly_canonical(root, error);
    if (error) throw std::runtime_error("Cannot resolve " + std::string(description) + " root: " + error.message());
    const fs::path canonical_child = fs::weakly_canonical(child, error);
    if (error) throw std::runtime_error("Cannot resolve " + std::string(description) + " path: " + error.message());
    if (!path_is_within(canonical_root, canonical_child))
        throw std::runtime_error(std::string(description) + " path escapes its Minecraft directory: " + relative);
    return child;
}

bool valid_id(const std::string& id) {
    if (id.empty() || id.size() > 64U) return false;
    const auto first = static_cast<unsigned char>(id.front());
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') ||
          (first >= '0' && first <= '9'))) return false;
    std::string device;
    for (const unsigned char c : id) {
        const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '+' || c == '-';
        if (!valid) return false;
    }
    const std::size_t dot = id.find('.');
    device.reserve(dot == std::string::npos ? id.size() : dot);
    for (const unsigned char c : id.substr(0, dot))
        device.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c));
    const bool numbered_device = device.size() == 4U &&
        (device.rfind("com", 0) == 0 || device.rfind("lpt", 0) == 0) &&
        device[3] >= '1' && device[3] <= '9';
    return device != "con" && device != "prn" && device != "aux" &&
           device != "nul" && !numbered_device;
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

    const auto safe_token = [](const std::string& token, bool allow_dots) {
        if (token.empty() || token == "." || token == ".." ||
            token.front() == '.' || token.back() == '.') return false;
        for (const unsigned char c : token) {
            const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '+' ||
                (allow_dots && c == '.');
            if (!valid) return false;
        }
        return true;
    };

    if (!safe_token(extension, false)) return {};
    std::vector<std::string> parts;
    std::stringstream input(value);
    std::string part;
    while (std::getline(input, part, ':')) parts.push_back(part);
    if (parts.size() < 3U || parts.size() > 4U ||
        !safe_token(parts[1], true) || !safe_token(parts[2], true) ||
        (parts.size() == 4U && !safe_token(parts[3], true))) return {};

    if (parts[0].empty() || parts[0].front() == '.' || parts[0].back() == '.' ||
        parts[0].find("..") != std::string::npos) return {};
    std::string path;
    std::stringstream group(parts[0]);
    while (std::getline(group, part, '.')) {
        if (!safe_token(part, false)) return {};
        path += part + "/";
    }
    if (path.empty()) return {};
    path += parts[1] + "/" + parts[2] + "/" + parts[1] + "-" + parts[2];
    if (parts.size() == 4U) path += "-" + parts[3];
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
        result += path_to_utf8(path);
    }
    return result;
}

std::string quote_display(const std::string& text) {
    if (!text.empty() && text.find_first_of(" \t\n\"") == std::string::npos) return text;
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
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
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
    const fs::path appdata = windows_environment_path(L"APPDATA");
    if (!appdata.empty()) return appdata / L".minecraft";
    const fs::path profile = windows_environment_path(L"USERPROFILE");
    if (!profile.empty()) return profile / L"AppData" / L"Roaming" / L".minecraft";
#else
    if (const char* home = std::getenv("HOME"))
        return fs::path(home) / ".minecraft";
#endif
    return fs::current_path() / ".minecraft";
}

fs::path default_data_dir() {
#ifdef _WIN32
    fs::path base = windows_environment_path(L"LOCALAPPDATA");
    if (base.empty()) base = windows_environment_path(L"APPDATA");
    if (!base.empty()) return base / L"MineCPP";
    const fs::path profile = windows_environment_path(L"USERPROFILE");
    if (!profile.empty()) return profile / L"AppData" / L"Local" / L"MineCPP";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"))
        return fs::path(xdg) / "minecpp";
    if (const char* home = std::getenv("HOME"))
        return fs::path(home) / ".local" / "share" / "minecpp";
#endif
    return fs::current_path() / "minecpp-data";
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
    if (!fs::exists(json_path)) throw std::runtime_error("Version metadata not found: " + json_path_to_utf8(path));
    if (!fs::exists(jar_path)) throw std::runtime_error("Version JAR not found: " + path_to_utf8(jar_path));

    const Json metadata = Json::parse(read_file(json_path));
    if (!metadata.get_string("inheritsFrom").empty())
        throw std::runtime_error("Inherited Minecraft versions are not supported in this first release");
    if (!metadata.is_object()) throw std::runtime_error("Version metadata must be a JSON object");
    const Json* metadata_id_value = metadata.get("id");
    if (!metadata_id_value || !metadata_id_value->is_string() || metadata_id_value->string() != id)
        throw std::runtime_error("Version JSON must contain a matching string id");
    const Json* main_class_value = metadata.get("mainClass");
    if (!main_class_value || !main_class_value->is_string() ||
        !valid_java_class_name(main_class_value->string())) {
        throw std::runtime_error("Version metadata does not contain a valid mainClass");
    }
    const std::string main_class = main_class_value->string();
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
    std::error_code status_error;
    const fs::file_status profiles_status = fs::symlink_status(profiles, status_error);
    if (status_error || fs::is_symlink(profiles_status) || !fs::is_directory(profiles_status))
        throw std::runtime_error("MineCPP profiles path must be a regular directory");

    const fs::path transactions = profiles / ".minecpp-transactions";
    fs::create_directories(transactions);
    const fs::file_status transactions_status = fs::symlink_status(transactions, status_error);
    if (status_error || fs::is_symlink(transactions_status) || !fs::is_directory(transactions_status))
        throw std::runtime_error("MineCPP transaction path must be a regular directory");

    const fs::path staging = transactions /
        (id + ".staging-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path target = profiles / id;
    const fs::path backup = transactions / (id + ".previous");

    const fs::file_status backup_status = fs::symlink_status(backup, status_error);
    if (!status_error && backup_status.type() != fs::file_type::not_found)
        throw std::runtime_error("An interrupted-import backup already exists: " + path_to_utf8(backup) +
                                 ". Recover or rename it before importing this version again.");
    if (status_error && status_error != std::errc::no_such_file_or_directory)
        throw std::runtime_error("Cannot inspect import backup path: " + status_error.message());

    if (!fs::create_directory(staging))
        throw std::runtime_error("Cannot create a unique import staging directory: " + path_to_utf8(staging));

    try {
        fs::copy_file(jar_path, staging / "client.jar", fs::copy_options::none);
        fs::copy_file(json_path, staging / "version.json", fs::copy_options::none);
        write_file(staging / "source-game-dir.txt", path_to_utf8(fs::absolute(minecraft_dir)));
        std::cout << "Extracting " << archive.count_files() << " JAR entries...\n";
        const std::size_t extracted = archive.extract_all(staging / "extracted");

        status_error.clear();
        const fs::file_status target_status = fs::symlink_status(target, status_error);
        const bool had_previous = !status_error && target_status.type() != fs::file_type::not_found;
        if (status_error && status_error != std::errc::no_such_file_or_directory)
            throw std::runtime_error("Cannot inspect existing profile: " + status_error.message());
        if (had_previous && (fs::is_symlink(target_status) || !fs::is_directory(target_status)))
            throw std::runtime_error("Refusing to replace a profile path that is not a regular directory");

        if (had_previous) fs::rename(target, backup);
        try {
            if (!had_previous && fs::exists(target))
                throw std::runtime_error("Profile appeared while import was running; refusing to overwrite it");
            fs::rename(staging, target);
        } catch (...) {
            if (had_previous && !fs::exists(target) && fs::exists(backup)) {
                std::error_code restore_error;
                fs::rename(backup, target, restore_error);
                if (restore_error)
                    throw std::runtime_error("New profile could not be published and the previous profile "
                                             "could not be restored. Backup remains at " + path_to_utf8(backup) +
                                             ". Restore error: " + restore_error.message());
            }
            throw;
        }

        if (had_previous) {
            std::error_code cleanup_error;
            fs::remove_all(backup, cleanup_error);
            if (cleanup_error)
                std::cerr << "Warning: imported successfully, but the previous profile backup remains at "
                          << path_to_utf8(backup) << ": " << cleanup_error.message() << '\n';
        }

        std::cout << "Imported Minecraft " << id << " successfully.\n"
                  << "  Profile: " << path_to_utf8(target) << "\n"
                  << "  Extracted files: " << extracted << "\n"
                  << "  Original installation was not modified.\n";
    } catch (...) {
        std::error_code cleanup_error;
        fs::remove_all(staging, cleanup_error);
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
    if (!metadata.get_string("inheritsFrom").empty())
        throw std::runtime_error("Inherited Minecraft versions are not supported in this first release");
    const fs::path minecraft_dir = minecraft_dir_arg.empty()
        ? path_from_utf8(read_file(profile / "source-game-dir.txt")) : minecraft_dir_arg;
    const fs::path libraries_dir = minecraft_dir / "libraries";
    const fs::path assets_dir = minecraft_dir / "assets";
    if (!fs::exists(libraries_dir)) throw std::runtime_error("Minecraft libraries folder not found: " + path_to_utf8(libraries_dir));
    if (!fs::exists(assets_dir)) throw std::runtime_error("Minecraft assets folder not found: " + path_to_utf8(assets_dir));
    const Json* metadata_id_value = metadata.get("id");
    if (!metadata_id_value || !metadata_id_value->is_string() || metadata_id_value->string() != id)
        throw std::runtime_error("Imported profile metadata id does not match its profile");
    const Json* main_class_value = metadata.get("mainClass");
    if (!main_class_value || !main_class_value->is_string() ||
        !valid_java_class_name(main_class_value->string())) {
        throw std::runtime_error("Imported profile does not contain a valid mainClass");
    }
    const std::string main_class = main_class_value->string();
    if (metadata.contains("inheritsFrom"))
        throw std::runtime_error("Inherited or modded version metadata is not supported in MineCPP 0.1.0");

    const fs::path extracted_dir = profile / "extracted";
    if (!fs::exists(extracted_dir)) throw std::runtime_error("Extracted profile data is missing; import the version again.");
    // Put the extracted client tree first so Java loads the imported files, while
    // retaining the original JAR in the classpath as a fallback for JAR-specific resources.
    std::vector<fs::path> classpath_entries{fs::absolute(extracted_dir), fs::absolute(jar_path)};
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
                const fs::path path = safe_local_child(libraries_dir, relative, "library");
                if (fs::is_regular_file(path)) classpath_entries.push_back(fs::absolute(path));
                else missing.push_back(path_to_utf8(path));
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
                            const fs::path native_jar =
                                safe_local_child(libraries_dir, native_path, "native library");
                            if (!fs::is_regular_file(native_jar)) missing.push_back(path_to_utf8(native_jar));
                            else (void)ZipArchive(native_jar).extract_all(natives_dir);
                        }
                    }
                }
            }
        }
    }
    const std::string assets_id = metadata.get("assetIndex")
        ? metadata.get("assetIndex")->get_string("id") : metadata.get_string("assets", id);
    fs::path asset_index;
    if (!assets_id.empty()) {
        asset_index = safe_local_child(assets_dir, "indexes/" + assets_id + ".json", "asset index");
        if (!fs::is_regular_file(asset_index))
            throw std::runtime_error("Local asset index not found: " + path_to_utf8(asset_index));
    }

    const std::string classpath_value = join_classpath(classpath_entries);
    const std::string natives_path = path_to_utf8(fs::absolute(natives_dir));
    std::map<std::string, std::string, std::less<>> vars{
        {"auth_player_name", username}, {"version_name", id},
        {"game_directory", path_to_utf8(fs::absolute(minecraft_dir))},
        {"assets_root", path_to_utf8(fs::absolute(assets_dir))}, {"assets_index_name", assets_id},
        {"auth_uuid", offline_uuid(username)}, {"auth_access_token", "0"},
        {"auth_session", "token:0:" + offline_uuid(username)}, {"game_assets", path_to_utf8(assets_dir / "virtual" / "legacy")},
        {"user_type", "legacy"}, {"version_type", metadata.get_string("type", "release")},
        {"natives_directory", natives_path}, {"launcher_name", "MineCPP"}, {"launcher_version", "0.1.0"},
        {"library_directory", path_to_utf8(fs::absolute(libraries_dir))}, {"classpath", classpath_value},
        {"classpath_separator", classpath_separator()}, {"user_properties", "{}"},
        {"resolution_width", "854"}, {"resolution_height", "480"}, {"clientid", ""}, {"auth_xuid", ""}
    };

    if (!asset_index.empty()) {
        const Json asset_index_json = Json::parse(read_file(asset_index));
        const Json* objects = asset_index_json.get("objects");
        if (!objects || !objects->is_object())
            throw std::runtime_error("Local asset index does not contain an objects map");

        std::size_t missing_assets = 0;
        for (const auto& [logical_name, object] : objects->object()) {
            (void)logical_name;
            if (!object.is_object())
                throw std::runtime_error("Asset index contains a malformed object record");
            const Json* hash_value = object.get("hash");
            const Json* size_value = object.get("size");
            if (!hash_value || !hash_value->is_string() ||
                !size_value || !size_value->is_number()) {
                throw std::runtime_error("Asset index contains an object without a hash or size");
            }

            std::string hash = hash_value->string();
            if (hash.size() != 40U ||
                !std::all_of(hash.begin(), hash.end(), [](unsigned char ch) {
                    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
                           (ch >= 'A' && ch <= 'F');
                })) {
                throw std::runtime_error("Asset index contains an invalid SHA-1 hash");
            }
            std::transform(hash.begin(), hash.end(), hash.begin(), [](unsigned char ch) {
                return ch >= 'A' && ch <= 'F' ? static_cast<char>(ch - 'A' + 'a') :
                                                static_cast<char>(ch);
            });

            const double raw_size = size_value->number(-1.0);
            if (raw_size < 0.0 || raw_size > 2147483648.0 ||
                static_cast<double>(static_cast<std::uint64_t>(raw_size)) != raw_size) {
                throw std::runtime_error("Asset index contains an invalid object size");
            }
            const std::uint64_t expected_size = static_cast<std::uint64_t>(raw_size);
            const fs::path object_path =
                safe_local_child(assets_dir, "objects/" + hash.substr(0, 2) + "/" + hash,
                                 "asset object");
            std::error_code object_error;
            const std::uint64_t actual_size = fs::file_size(object_path, object_error);
            if (object_error || !fs::is_regular_file(object_path) || actual_size != expected_size) {
                ++missing_assets;
                if (missing.size() < 50U)
                    missing.push_back(path_to_utf8(object_path) + " (missing or wrong size)");
            }
        }
        if (missing_assets != 0U)
            std::cerr << "Missing or damaged asset objects: " << missing_assets << '\n';
    }

    if (!missing.empty()) {
        std::ostringstream message;
        message << missing.size() << " required local files are missing:";
        for (const auto& item : missing) message << "\n  - " << item;
        throw std::runtime_error(message.str());
    }

    std::vector<std::string> arguments;
    const Json* sections = metadata.get("arguments");
    if (sections && sections->is_object()) append_arguments(arguments, sections->get("jvm"));
    else {
        arguments.emplace_back("-Djava.library.path=" + natives_path);
        arguments.emplace_back("-cp");
        arguments.push_back(classpath_value);
    }
    for (std::string& argument : arguments) argument = substitute(std::move(argument), vars);
    bool has_classpath = false;
    for (const auto& argument : arguments) {
        if (argument == "-cp" || argument == "-classpath" || argument == "--class-path" ||
            argument.rfind("-cp=", 0) == 0 || argument.rfind("--class-path=", 0) == 0)
            has_classpath = true;
    }
    if (!has_classpath) { arguments.emplace_back("-cp"); arguments.push_back(classpath_value); }

    if (const Json* logging = metadata.get("logging")) {
        if (const Json* client = logging->get("client")) {
            const std::string flag = client->get_string("argument");
            const Json* file = client->get("file");
            if (!flag.empty() && file) {
                auto log_vars = vars;
                log_vars["path"] = path_to_utf8(safe_local_child(assets_dir, "log_configs/" + file->get_string("id"), "logging configuration"));
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
    const int result = run_process(find_java(), arguments, path_to_utf8(fs::absolute(minecraft_dir)));
    if (result != 0) std::cerr << "Minecraft exited with code " << result << '\n';
    return result;
}

} // namespace minecpp
