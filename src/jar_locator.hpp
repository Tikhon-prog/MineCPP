#pragma once

// Finds the player's own Minecraft client jars: <minecraft dir>/versions/<id>/<id>.jar.
// Nothing is downloaded; this only looks at what the official launcher already
// installed.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <system_error>
#include <filesystem>
#include <string>
#include <vector>

namespace minecpp {

struct JarCandidate {
    std::string version_id;
    std::filesystem::path jar;
};

// The default Minecraft folder of the current OS, or an empty path when the
// home directory cannot be determined.
inline std::filesystem::path default_minecraft_dir() {
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA")) return std::filesystem::path(appdata) / ".minecraft";
    return {};
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / "Library" / "Application Support" / "minecraft";
    return {};
#else
    if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / ".minecraft";
    return {};
#endif
}

// "1.21.10" -> true, "24w10a" -> false, "fabric-loader-0.15" -> false.
inline bool is_release_id(const std::string& id) {
    if (id.empty() || std::isdigit(static_cast<unsigned char>(id.front())) == 0) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '.';
    });
}

// Natural ordering: digit runs compare as numbers ("1.9" < "1.10"), the rest as text.
inline bool version_less(const std::string& a, const std::string& b) {
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
        const bool da = std::isdigit(static_cast<unsigned char>(a[i])) != 0;
        const bool db = std::isdigit(static_cast<unsigned char>(b[j])) != 0;
        if (da && db) {
            std::size_t ie = i;
            std::size_t je = j;
            while (ie < a.size() && std::isdigit(static_cast<unsigned char>(a[ie])) != 0) ++ie;
            while (je < b.size() && std::isdigit(static_cast<unsigned char>(b[je])) != 0) ++je;
            // Strip leading zeros, then shorter = smaller, else compare digits.
            std::size_t is = i;
            std::size_t js = j;
            while (is + 1 < ie && a[is] == '0') ++is;
            while (js + 1 < je && b[js] == '0') ++js;
            const std::size_t la = ie - is;
            const std::size_t lb = je - js;
            if (la != lb) return la < lb;
            const int cmp = a.compare(is, la, b, js, lb);
            if (cmp != 0) return cmp < 0;
            i = ie;
            j = je;
        } else {
            if (a[i] != b[j]) return a[i] < b[j];
            ++i;
            ++j;
        }
    }
    return (a.size() - i) < (b.size() - j);
}

// Every version folder that holds a jar named after it, releases first and
// newest first within each group. Missing or unreadable folders give an empty list.
inline std::vector<JarCandidate> find_client_jars(const std::filesystem::path& minecraft_dir) {
    std::vector<JarCandidate> found;
    std::error_code error;
    const std::filesystem::path versions = minecraft_dir / "versions";
    if (!std::filesystem::is_directory(versions, error)) return found;
    for (const auto& entry : std::filesystem::directory_iterator(versions, error)) {
        if (!entry.is_directory(error)) continue;
        const std::string id = entry.path().filename().string();
        const std::filesystem::path jar = entry.path() / (id + ".jar");
        if (std::filesystem::is_regular_file(jar, error)) {
            found.push_back({id, jar});
        }
    }
    std::sort(found.begin(), found.end(), [](const JarCandidate& a, const JarCandidate& b) {
        const bool ra = is_release_id(a.version_id);
        const bool rb = is_release_id(b.version_id);
        if (ra != rb) return ra;
        return version_less(b.version_id, a.version_id);  // newest first
    });
    return found;
}

// Where an imported version keeps its block textures.
inline std::filesystem::path block_texture_dir(const std::filesystem::path& data_dir, const std::string& version_id) {
    return data_dir / "versions" / version_id / "client" / "assets" / "minecraft" / "textures" / "block";
}

}  // namespace minecpp
