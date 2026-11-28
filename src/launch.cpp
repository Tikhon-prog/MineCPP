#include "launch.hpp"

#include "jar_locator.hpp"
#include "md5.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>

#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace minecpp::launcher {
namespace {

constexpr std::uintmax_t kMaxJsonBytes = 16U * 1024U * 1024U;
constexpr int kMaxInheritDepth = 8;
constexpr const char* kJarKey = "_minecpp_jar";

std::string read_text_file(const fs::path& path) {
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error) throw std::runtime_error("Cannot read " + path.string());
    if (size > kMaxJsonBytes) throw std::runtime_error(path.string() + " is larger than 16 MiB");
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open " + path.string());
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// Sets or appends an object member.
void set_member(Json& object, const std::string& key, Json value) {
    if (object.type != Json::Type::Object) {
        object = Json{};
        object.type = Json::Type::Object;
    }
    for (std::size_t i = 0; i < object.keys.size(); ++i) {
        if (object.keys[i] == key) {
            object.items[i] = std::move(value);
            return;
        }
    }
    object.keys.push_back(key);
    object.items.push_back(std::move(value));
}

Json make_string(const std::string& text) {
    Json j;
    j.type = Json::Type::String;
    j.string = text;
    return j;
}

Json concat_arrays(const Json& first, const Json& second) {
    Json out;
    out.type = Json::Type::Array;
    if (first.is_array()) out.items.insert(out.items.end(), first.items.begin(), first.items.end());
    if (second.is_array()) out.items.insert(out.items.end(), second.items.begin(), second.items.end());
    return out;
}

// child overrides parent; libraries: child first; arguments: parent then child.
Json merge_profiles(const Json& parent, const Json& child) {
    Json out = parent;
    for (std::size_t i = 0; i < child.keys.size(); ++i) {
        const std::string& key = child.keys[i];
        const Json& value = child.items[i];
        if (key == "libraries") {
            set_member(out, key, concat_arrays(value, parent["libraries"]));
        } else if (key == "arguments") {
            Json merged = parent["arguments"].is_object() ? parent["arguments"] : Json{};
            for (const char* section : {"game", "jvm"}) {
                if (value[section].is_array() || merged[section].is_array()) {
                    set_member(merged, section, concat_arrays(merged[section], value[section]));
                }
            }
            set_member(out, key, merged);
        } else {
            set_member(out, key, value);
        }
    }
    return out;
}

Json load_profile_recursive(const fs::path& minecraft_dir, const std::string& id, int depth,
                            std::set<std::string>& seen) {
    if (depth > kMaxInheritDepth) throw std::runtime_error("Version inheritance is too deep");
    if (!safe_version_id(id)) throw std::runtime_error("Unsafe version id: " + id);
    if (!seen.insert(id).second) throw std::runtime_error("Version " + id + " inherits from itself");

    const fs::path json_path = minecraft_dir / "versions" / id / (id + ".json");
    Json profile = parse_json(read_text_file(json_path));
    if (!profile.is_object()) throw std::runtime_error(json_path.string() + " is not a JSON object");

    const std::string parent_id = profile["inheritsFrom"].as_string();
    std::string jar_id = profile["jar"].as_string();
    if (parent_id.empty()) {
        if (jar_id.empty()) jar_id = id;
        set_member(profile, kJarKey, make_string(jar_id));
        return profile;
    }
    Json parent = load_profile_recursive(minecraft_dir, parent_id, depth + 1, seen);
    if (jar_id.empty()) jar_id = parent[kJarKey].as_string(parent_id);
    Json merged = merge_profiles(parent, profile);
    set_member(merged, kJarKey, make_string(jar_id));
    return merged;
}

struct Context {
    std::string os_name;
    bool is_64bit = true;
    bool has_custom_resolution = false;
};

bool rule_matches(const Json& rule, const Context& ctx) {
    const Json& os = rule["os"];
    if (os.is_object()) {
        const std::string name = os["name"].as_string();
        if (!name.empty() && name != ctx.os_name) return false;
        const std::string arch = os["arch"].as_string();
        if (arch == "x86" && ctx.is_64bit) return false;
        // os.version patterns are not evaluated; they only select old Windows/macOS builds.
    }
    const Json& features = rule["features"];
    if (features.is_object()) {
        for (std::size_t i = 0; i < features.keys.size(); ++i) {
            bool actual = false;
            if (features.keys[i] == "has_custom_resolution") actual = ctx.has_custom_resolution;
            if (features.items[i].as_bool(false) != actual) return false;
        }
    }
    return true;
}

bool allowed_by(const Json& rules, const Context& ctx) {
    if (!rules.is_array() || rules.size() == 0) return true;
    bool allowed = false;
    for (const Json& rule : rules.items) {
        if (rule_matches(rule, ctx)) allowed = rule["action"].as_string() == "allow";
    }
    return allowed;
}

std::string substitute(const std::string& text, const std::map<std::string, std::string>& values) {
    std::string out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t start = text.find("${", pos);
        if (start == std::string::npos) {
            out.append(text, pos, std::string::npos);
            break;
        }
        const std::size_t end = text.find('}', start + 2);
        if (end == std::string::npos) {
            out.append(text, pos, std::string::npos);
            break;
        }
        out.append(text, pos, start - pos);
        const auto found = values.find(text.substr(start + 2, end - start - 2));
        if (found != values.end()) out += found->second;
        pos = end + 1;
    }
    return out;
}

std::vector<std::string> collect_arguments(const Json& list, const Context& ctx,
                                           const std::map<std::string, std::string>& values) {
    std::vector<std::string> out;
    if (!list.is_array()) return out;
    for (const Json& entry : list.items) {
        if (entry.is_string()) {
            out.push_back(substitute(entry.string, values));
        } else if (entry.is_object()) {
            if (!allowed_by(entry["rules"], ctx)) continue;
            const Json& value = entry["value"];
            if (value.is_string()) {
                out.push_back(substitute(value.string, values));
            } else if (value.is_array()) {
                for (const Json& item : value.items) {
                    if (item.is_string()) out.push_back(substitute(item.string, values));
                }
            }
        }
    }
    return out;
}

std::vector<std::string> split_words(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream stream(text);
    std::string word;
    while (stream >> word) out.push_back(word);
    return out;
}

std::string library_key(const std::string& name) {
    // group:artifact[:classifier], so two versions of one library collapse to the first.
    std::vector<std::string> parts;
    std::size_t start = 0;
    const std::string base = name.substr(0, name.find('@'));
    for (;;) {
        const std::size_t colon = base.find(':', start);
        parts.push_back(base.substr(start, colon == std::string::npos ? std::string::npos : colon - start));
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    std::string key = parts.size() > 0 ? parts[0] : "";
    if (parts.size() > 1) key += ":" + parts[1];
    if (parts.size() > 3) key += ":" + parts[3];
    return key;
}

fs::path find_java(const LaunchOptions& options, const std::string& component, const std::string& os_name) {
    if (!options.java.empty()) return options.java;
    const bool windows = os_name == "windows";
    const std::string exe = windows ? "java.exe" : "java";
    if (!component.empty()) {
        const std::string dir = windows ? "windows-x64" : (os_name == "osx" ? "mac-os" : "linux");
        const fs::path bundled = options.minecraft_dir / "runtime" / component / dir / component / "bin" / exe;
        std::error_code error;
        if (fs::is_regular_file(bundled, error)) return bundled;
    }
    if (const char* home = std::getenv("JAVA_HOME")) {
        const fs::path candidate = fs::path(home) / "bin" / exe;
        std::error_code error;
        if (fs::is_regular_file(candidate, error)) return candidate;
    }
    return fs::path(exe);  // resolved through PATH at launch time
}

std::string quote_for_display(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\"'$&;()<>|*?") == std::string::npos) return arg;
    std::string out = "'";
    for (const char c : arg) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

}  // namespace

std::string host_os_name() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "osx";
#else
    return "linux";
#endif
}

bool valid_player_name(const std::string& name) {
    if (name.empty() || name.size() > 16) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    });
}

bool safe_version_id(const std::string& id) {
    if (id.empty() || id.size() > 128 || id == "." || id == "..") return false;
    return id.find_first_of("/\\:") == std::string::npos && id.find("..") == std::string::npos;
}

std::string library_relative_path(const std::string& maven_name) {
    std::string base = maven_name;
    std::string extension = "jar";
    const std::size_t at = base.find('@');
    if (at != std::string::npos) {
        extension = base.substr(at + 1);
        base.resize(at);
    }
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t colon = base.find(':', start);
        parts.push_back(base.substr(start, colon == std::string::npos ? std::string::npos : colon - start));
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    if (parts.size() < 3) return {};
    std::string group = parts[0];
    std::replace(group.begin(), group.end(), '.', '/');
    std::string file = parts[1] + "-" + parts[2];
    if (parts.size() > 3) file += "-" + parts[3];
    return group + "/" + parts[1] + "/" + parts[2] + "/" + file + "." + extension;
}

bool rules_allow(const Json& rules, const std::string& os_name, bool is_64bit, bool has_custom_resolution) {
    Context ctx;
    ctx.os_name = os_name;
    ctx.is_64bit = is_64bit;
    ctx.has_custom_resolution = has_custom_resolution;
    return allowed_by(rules, ctx);
}

Json load_version_profile(const fs::path& minecraft_dir, const std::string& version_id) {
    std::set<std::string> seen;
    return load_profile_recursive(minecraft_dir, version_id, 0, seen);
}

LaunchPlan plan_launch(const Json& profile, const LaunchOptions& options) {
    if (!valid_player_name(options.player_name)) {
        throw std::runtime_error("Player name must be 1-16 letters, digits or underscores");
    }
    Context ctx;
    ctx.os_name = options.os_name.empty() ? host_os_name() : options.os_name;
    ctx.is_64bit = options.is_64bit;
    ctx.has_custom_resolution = options.width > 0 && options.height > 0;

    LaunchPlan plan;
    const std::string id = profile["id"].as_string(options.version_id);
    plan.main_class = profile["mainClass"].as_string();
    if (plan.main_class.empty()) throw std::runtime_error("The version file has no mainClass");
    plan.java_component = profile["javaVersion"]["component"].as_string();
    plan.java_major = profile["javaVersion"]["majorVersion"].as_int(0);

    const fs::path libraries_dir = options.minecraft_dir / "libraries";
    const std::string separator = ctx.os_name == "windows" ? ";" : ":";

    // Classpath: every allowed library (first definition of each wins), then the client jar.
    std::string classpath;
    std::set<std::string> seen_keys;
    for (const Json& lib : profile["libraries"].items) {
        if (!allowed_by(lib["rules"], ctx)) continue;
        const std::string name = lib["name"].as_string();
        if (name.empty()) continue;
        if (lib["natives"].is_object()) {
            plan.unsupported.push_back("Library " + name + " needs separately extracted natives (versions before 1.19)");
            continue;
        }
        if (!seen_keys.insert(library_key(name)).second) continue;
        std::string relative = lib["downloads"]["artifact"]["path"].as_string();
        if (relative.empty()) relative = library_relative_path(name);
        if (relative.empty()) continue;
        const fs::path file = libraries_dir / fs::path(relative);
        std::error_code error;
        if (!fs::is_regular_file(file, error)) plan.missing_files.push_back(file);
        if (!classpath.empty()) classpath += separator;
        classpath += file.string();
    }
    const std::string jar_id = profile[kJarKey].as_string(id);
    const fs::path client_jar = options.minecraft_dir / "versions" / jar_id / (jar_id + ".jar");
    {
        std::error_code error;
        if (!fs::is_regular_file(client_jar, error)) plan.missing_files.push_back(client_jar);
        if (!classpath.empty()) classpath += separator;
        classpath += client_jar.string();
    }

    const fs::path game_dir = options.game_dir.empty() ? options.minecraft_dir : options.game_dir;
    const fs::path natives_root = options.natives_root.empty() ? options.minecraft_dir / "minecpp-natives" : options.natives_root;
    plan.natives_dir = natives_root / id;
    plan.working_dir = game_dir;

    const std::string asset_index = profile["assetIndex"]["id"].as_string(profile["assets"].as_string("legacy"));
    std::map<std::string, std::string> values{
        {"auth_player_name", options.player_name},
        {"version_name", id},
        {"game_directory", game_dir.string()},
        {"assets_root", (options.minecraft_dir / "assets").string()},
        {"game_assets", (options.minecraft_dir / "assets").string()},
        {"assets_index_name", asset_index},
        {"auth_uuid", offline_player_uuid(options.player_name)},
        {"auth_access_token", "0"},
        {"auth_session", "0"},
        {"clientid", ""},
        {"auth_xuid", "0"},
        {"user_type", "legacy"},
        {"user_properties", "{}"},
        {"version_type", profile["type"].as_string("release")},
        {"natives_directory", plan.natives_dir.string()},
        {"launcher_name", "MineCPP"},
        {"launcher_version", "0.4.0"},
        {"classpath", classpath},
        {"library_directory", libraries_dir.string()},
        {"classpath_separator", separator},
        {"resolution_width", std::to_string(options.width)},
        {"resolution_height", std::to_string(options.height)},
    };

    std::vector<std::string> jvm;
    std::vector<std::string> game;
    const Json& arguments = profile["arguments"];
    if (arguments.is_object()) {
        jvm = collect_arguments(arguments["jvm"], ctx, values);
        game = collect_arguments(arguments["game"], ctx, values);
    } else {
        // Pre-1.13 profiles: one argument string, JVM flags implied.
        game = collect_arguments(
            [&] {
                Json list;
                list.type = Json::Type::Array;
                for (const std::string& word : split_words(profile["minecraftArguments"].as_string())) {
                    list.items.push_back(make_string(word));
                }
                return list;
            }(),
            ctx, values);
        jvm = {"-Djava.library.path=" + plan.natives_dir.string()};
        if (ctx.has_custom_resolution) {
            game.push_back("--width");
            game.push_back(std::to_string(options.width));
            game.push_back("--height");
            game.push_back(std::to_string(options.height));
        }
    }
    if (std::find(jvm.begin(), jvm.end(), classpath) == jvm.end()) {
        jvm.push_back("-cp");
        jvm.push_back(classpath);
    }

    if (options.memory_mb > 0) plan.args.push_back("-Xmx" + std::to_string(options.memory_mb) + "M");
    plan.args.insert(plan.args.end(), options.extra_jvm_args.begin(), options.extra_jvm_args.end());
    plan.args.insert(plan.args.end(), jvm.begin(), jvm.end());
    plan.args.push_back(plan.main_class);
    plan.args.insert(plan.args.end(), game.begin(), game.end());
    plan.args.insert(plan.args.end(), options.extra_game_args.begin(), options.extra_game_args.end());

    plan.java = find_java(options, plan.java_component, ctx.os_name);
    return plan;
}

std::vector<std::string> installed_versions(const fs::path& minecraft_dir) {
    std::vector<std::string> ids;
    std::error_code error;
    const fs::path versions = minecraft_dir / "versions";
    if (!fs::is_directory(versions, error)) return ids;
    for (const auto& entry : fs::directory_iterator(versions, error)) {
        if (!entry.is_directory(error)) continue;
        const std::string id = entry.path().filename().string();
        if (fs::is_regular_file(entry.path() / (id + ".json"), error)) ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end(), [](const std::string& a, const std::string& b) {
        const bool ra = is_release_id(a);
        const bool rb = is_release_id(b);
        if (ra != rb) return ra;
        return version_less(b, a);
    });
    return ids;
}

std::string describe_plan(const LaunchPlan& plan) {
    std::string out = "cd " + quote_for_display(plan.working_dir.string()) + " && " + quote_for_display(plan.java.string());
    for (const std::string& arg : plan.args) out += " " + quote_for_display(arg);
    return out;
}

int run_plan(const LaunchPlan& plan) {
    std::error_code error;
    fs::create_directories(plan.natives_dir, error);
    fs::create_directories(plan.working_dir, error);

    std::vector<std::string> argv_storage;
    argv_storage.push_back(plan.java.string());
    argv_storage.insert(argv_storage.end(), plan.args.begin(), plan.args.end());

#if defined(_WIN32)
    std::vector<std::string> quoted;
    for (const std::string& arg : argv_storage) {
        if (arg.find_first_of(" \t\"") == std::string::npos && !arg.empty()) {
            quoted.push_back(arg);
        } else {
            std::string q = "\"";
            for (const char c : arg) {
                if (c == '"') q += "\\\"";
                else q.push_back(c);
            }
            q += "\"";
            quoted.push_back(q);
        }
    }
    std::vector<const char*> argv;
    for (const std::string& arg : quoted) argv.push_back(arg.c_str());
    argv.push_back(nullptr);
    if (_chdir(plan.working_dir.string().c_str()) != 0) return -1;
    const intptr_t result = _spawnvp(_P_WAIT, argv_storage[0].c_str(), argv.data());
    return static_cast<int>(result);
#else
    std::vector<char*> argv;
    for (std::string& arg : argv_storage) argv.push_back(arg.data());
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (chdir(plan.working_dir.c_str()) != 0) _exit(126);
        execvp(argv[0], argv.data());
        _exit(127);  // java not found
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
#endif
}

}  // namespace minecpp::launcher
