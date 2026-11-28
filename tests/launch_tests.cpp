#include "launch.hpp"
#include "launcher_settings.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace minecpp;
using namespace minecpp::launcher;
namespace fs = std::filesystem;

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Test failed: " #expression); } while (false)

template <typename Fn>
static bool throws(Fn&& fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

static void write_file(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
}

static bool contains(const std::vector<std::string>& args, const std::string& value) {
    return std::find(args.begin(), args.end(), value) != args.end();
}

// Value right after `flag`, or "" when absent.
static std::string after(const std::vector<std::string>& args, const std::string& flag) {
    const auto it = std::find(args.begin(), args.end(), flag);
    return (it == args.end() || it + 1 == args.end()) ? std::string() : *(it + 1);
}

static bool any_contains(const std::vector<std::string>& args, const std::string& part) {
    return std::any_of(args.begin(), args.end(), [&](const std::string& a) { return a.find(part) != std::string::npos; });
}

static const char* kModern = R"({
  "id": "1.21.4", "type": "release", "mainClass": "net.minecraft.client.main.Main",
  "assetIndex": {"id": "19"},
  "javaVersion": {"component": "java-runtime-delta", "majorVersion": 21},
  "libraries": [
    {"name": "com.example:lib:1.0", "downloads": {"artifact": {"path": "com/example/lib/1.0/lib-1.0.jar"}}},
    {"name": "org.lwjgl:lwjgl:3.3.3"},
    {"name": "org.lwjgl:lwjgl:3.3.3:natives-linux", "rules": [{"action": "allow", "os": {"name": "linux"}}]},
    {"name": "org.lwjgl:lwjgl:3.3.3:natives-windows", "rules": [{"action": "allow", "os": {"name": "windows"}}]}
  ],
  "arguments": {
    "jvm": ["-Djava.library.path=${natives_directory}",
            {"rules": [{"action": "allow", "os": {"name": "osx"}}], "value": ["-XstartOnFirstThread"]},
            "-Dminecraft.launcher.brand=${launcher_name}", "-cp", "${classpath}"],
    "game": ["--username", "${auth_player_name}", "--version", "${version_name}", "--gameDir", "${game_directory}",
             "--assetsDir", "${assets_root}", "--assetIndex", "${assets_index_name}", "--uuid", "${auth_uuid}",
             "--accessToken", "${auth_access_token}", "--userType", "${user_type}",
             {"rules": [{"action": "allow", "features": {"has_custom_resolution": true}}],
              "value": ["--width", "${resolution_width}", "--height", "${resolution_height}"]},
             {"rules": [{"action": "allow", "features": {"is_demo_user": true}}], "value": ["--demo"]}]
  }
})";

static const char* kFabric = R"({
  "id": "fabric-loader-0.16-1.21.4", "inheritsFrom": "1.21.4", "type": "release",
  "mainClass": "net.fabricmc.loader.impl.launch.knot.KnotClient",
  "libraries": [{"name": "net.fabricmc:fabric-loader:0.16"}],
  "arguments": {"jvm": ["-DFabricMcEmu=net.minecraft.client.main.Main"], "game": []}
})";

int main() {
    // Pure helpers.
    {
        CHECK(valid_player_name("Steve") && valid_player_name("a_b_9") && valid_player_name("1234567890123456"));
        CHECK(!valid_player_name("") && !valid_player_name("12345678901234567") && !valid_player_name("bad name") &&
              !valid_player_name("na/me") && !valid_player_name("Ünï"));
        CHECK(safe_version_id("1.21.4") && safe_version_id("fabric-loader-0.16-1.21.4"));
        CHECK(!safe_version_id("") && !safe_version_id("..") && !safe_version_id("a/b") && !safe_version_id("a\\b") &&
              !safe_version_id("a..b") && !safe_version_id("c:x"));
        CHECK(library_relative_path("org.lwjgl:lwjgl:3.3.3:natives-linux") ==
              "org/lwjgl/lwjgl/3.3.3/lwjgl-3.3.3-natives-linux.jar");
        CHECK(library_relative_path("com.example:lib:1.0") == "com/example/lib/1.0/lib-1.0.jar");
        CHECK(library_relative_path("a.b:c:1@zip") == "a/b/c/1/c-1.zip");
        CHECK(library_relative_path("broken").empty());
    }

    // Rules: os, arch, features, last matching rule wins.
    {
        const Json none = parse_json("[]");
        CHECK(rules_allow(none, "linux", true, false));
        CHECK(rules_allow(Json{}, "linux", true, false));  // absent -> allowed

        const Json only_osx = parse_json(R"([{"action":"allow","os":{"name":"osx"}}])");
        CHECK(rules_allow(only_osx, "osx", true, false));
        CHECK(!rules_allow(only_osx, "linux", true, false));

        const Json not_osx = parse_json(R"([{"action":"allow"},{"action":"disallow","os":{"name":"osx"}}])");
        CHECK(rules_allow(not_osx, "linux", true, false));
        CHECK(!rules_allow(not_osx, "osx", true, false));

        const Json x86 = parse_json(R"([{"action":"allow","os":{"name":"windows","arch":"x86"}}])");
        CHECK(rules_allow(x86, "windows", false, false));
        CHECK(!rules_allow(x86, "windows", true, false));

        const Json res = parse_json(R"([{"action":"allow","features":{"has_custom_resolution":true}}])");
        CHECK(rules_allow(res, "linux", true, true));
        CHECK(!rules_allow(res, "linux", true, false));

        const Json demo = parse_json(R"([{"action":"allow","features":{"is_demo_user":true}}])");
        CHECK(!rules_allow(demo, "linux", true, true));
    }

    // A fake Minecraft folder.
    const fs::path root = fs::temp_directory_path() /
        ("minecpp_launch_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    write_file(root / "versions/1.21.4/1.21.4.json", kModern);
    write_file(root / "versions/1.21.4/1.21.4.jar", "jar");
    write_file(root / "versions/fabric-loader-0.16-1.21.4/fabric-loader-0.16-1.21.4.json", kFabric);
    write_file(root / "libraries/com/example/lib/1.0/lib-1.0.jar", "x");
    write_file(root / "libraries/org/lwjgl/lwjgl/3.3.3/lwjgl-3.3.3.jar", "x");
    write_file(root / "libraries/org/lwjgl/lwjgl/3.3.3/lwjgl-3.3.3-natives-linux.jar", "x");

    LaunchOptions options;
    options.minecraft_dir = root;
    options.version_id = "1.21.4";
    options.os_name = "linux";

    // Plain launch.
    {
        options.player_name = "Steve_1";
        const Json profile = load_version_profile(root, "1.21.4");
        const LaunchPlan plan = plan_launch(profile, options);
        CHECK(plan.unsupported.empty());
        CHECK(plan.missing_files.empty());
        CHECK(plan.main_class == "net.minecraft.client.main.Main");
        CHECK(plan.java_component == "java-runtime-delta" && plan.java_major == 21);

        // JVM flags come before the main class, game flags after it.
        const auto main_at = std::find(plan.args.begin(), plan.args.end(), plan.main_class);
        CHECK(main_at != plan.args.end());
        const std::vector<std::string> jvm(plan.args.begin(), main_at);
        const std::vector<std::string> game(main_at + 1, plan.args.end());
        CHECK(contains(jvm, "-cp"));
        CHECK(contains(jvm, "-Dminecraft.launcher.brand=MineCPP"));
        CHECK(any_contains(jvm, "-Djava.library.path=" + (root / "minecpp-natives" / "1.21.4").string()));
        CHECK(!contains(jvm, "-XstartOnFirstThread"));       // macOS only

        const std::string classpath = after(jvm, "-cp");
        CHECK(classpath.find("lib-1.0.jar") != std::string::npos);
        CHECK(classpath.find("lwjgl-3.3.3.jar") != std::string::npos);
        CHECK(classpath.find("natives-linux") != std::string::npos);
        CHECK(classpath.find("natives-windows") == std::string::npos);
        const std::string client_jar = (root / "versions" / "1.21.4" / "1.21.4.jar").string();
        CHECK(classpath.size() > client_jar.size() &&
              classpath.compare(classpath.size() - client_jar.size(), client_jar.size(), client_jar) == 0);  // jar last
        CHECK(classpath.find(';') == std::string::npos);

        CHECK(after(game, "--username") == "Steve_1");
        CHECK(after(game, "--version") == "1.21.4");
        CHECK(after(game, "--gameDir") == root.string());
        CHECK(after(game, "--assetsDir") == (root / "assets").string());
        CHECK(after(game, "--assetIndex") == "19");
        CHECK(after(game, "--uuid") == "ed378328805a3b9a940a29bd82fbcf20");
        CHECK(after(game, "--accessToken") == "0");
        CHECK(!contains(game, "--width"));
        CHECK(!contains(game, "--demo"));
        CHECK(!any_contains(plan.args, "${"));                 // every placeholder resolved
        CHECK(plan.working_dir == root);
    }

    // Options: memory, window size, game folder, extras.
    {
        LaunchOptions o = options;
        o.memory_mb = 2048;
        o.width = 1280;
        o.height = 720;
        o.game_dir = root / "instance";
        o.extra_jvm_args = {"-XX:+UseG1GC"};
        o.extra_game_args = {"--quickPlaySingleplayer", "World"};
        const LaunchPlan plan = plan_launch(load_version_profile(root, "1.21.4"), o);
        CHECK(plan.args[0] == "-Xmx2048M");
        CHECK(plan.args[1] == "-XX:+UseG1GC");
        CHECK(after(plan.args, "--width") == "1280" && after(plan.args, "--height") == "720");
        CHECK(after(plan.args, "--gameDir") == (root / "instance").string());
        CHECK(plan.args[plan.args.size() - 2] == "--quickPlaySingleplayer" && plan.args.back() == "World");
        CHECK(plan.working_dir == root / "instance");
    }

    // Other operating systems.
    {
        LaunchOptions mac = options;
        mac.os_name = "osx";
        const LaunchPlan m = plan_launch(load_version_profile(root, "1.21.4"), mac);
        CHECK(contains(m.args, "-XstartOnFirstThread"));
        CHECK(after(m.args, "-cp").find("natives-linux") == std::string::npos);

        LaunchOptions win = options;
        win.os_name = "windows";
        const LaunchPlan w = plan_launch(load_version_profile(root, "1.21.4"), win);
        CHECK(after(w.args, "-cp").find(';') != std::string::npos);
        CHECK(after(w.args, "-cp").find("natives-windows") != std::string::npos);
        CHECK(after(w.args, "-cp").find("natives-linux") == std::string::npos);
    }

    // A missing library is reported, not silently skipped.
    {
        fs::remove(root / "libraries/com/example/lib/1.0/lib-1.0.jar");
        const LaunchPlan plan = plan_launch(load_version_profile(root, "1.21.4"), options);
        CHECK(plan.missing_files.size() == 1);
        CHECK(plan.missing_files[0].filename() == "lib-1.0.jar");
        write_file(root / "libraries/com/example/lib/1.0/lib-1.0.jar", "x");
    }

    // inheritsFrom: Fabric-style profile on top of vanilla.
    {
        const Json profile = load_version_profile(root, "fabric-loader-0.16-1.21.4");
        CHECK(profile["mainClass"].as_string() == "net.fabricmc.loader.impl.launch.knot.KnotClient");
        CHECK(profile["assetIndex"]["id"].as_string() == "19");           // inherited
        CHECK(profile["libraries"][0]["name"].as_string() == "net.fabricmc:fabric-loader:0.16");  // child first
        CHECK(profile["libraries"].size() == 5);

        LaunchOptions o = options;
        o.version_id = "fabric-loader-0.16-1.21.4";
        const LaunchPlan plan = plan_launch(profile, o);
        CHECK(contains(plan.args, "net.fabricmc.loader.impl.launch.knot.KnotClient"));
        CHECK(contains(plan.args, "-DFabricMcEmu=net.minecraft.client.main.Main"));
        CHECK(after(plan.args, "--version") == "fabric-loader-0.16-1.21.4");
        // The game jar is the parent's; the fabric library file does not exist here.
        CHECK(after(plan.args, "-cp").find((root / "versions" / "1.21.4" / "1.21.4.jar").string()) != std::string::npos);
        CHECK(!plan.missing_files.empty());
        CHECK(plan.missing_files[0].filename() == "fabric-loader-0.16.jar");
    }

    // Old-style profiles: one argument string, implied JVM flags.
    {
        const Json legacy = parse_json(R"({"id":"1.12.2","type":"release","mainClass":"net.minecraft.client.main.Main",
            "minecraftArguments":"--username ${auth_player_name} --version ${version_name} --assetIndex ${assets_index_name}",
            "assets":"1.12","libraries":[]})");
        LaunchOptions o = options;
        o.version_id = "1.12.2";
        o.width = 800;
        o.height = 600;
        const LaunchPlan plan = plan_launch(legacy, o);
        CHECK(any_contains(plan.args, "-Djava.library.path="));
        CHECK(contains(plan.args, "-cp"));
        CHECK(after(plan.args, "--username") == "Steve_1");
        CHECK(after(plan.args, "--assetIndex") == "1.12");
        CHECK(after(plan.args, "--width") == "800");
    }

    // Libraries that need extracted natives are refused up front.
    {
        const Json old_natives = parse_json(R"({"id":"1.16.5","mainClass":"x","libraries":[
            {"name":"org.lwjgl:lwjgl:3.2.2","natives":{"linux":"natives-linux"}}],"arguments":{"jvm":[],"game":[]}})");
        LaunchOptions o = options;
        o.version_id = "1.16.5";
        const LaunchPlan plan = plan_launch(old_natives, o);
        CHECK(!plan.unsupported.empty());
    }

    // Duplicate libraries (same group:artifact) keep the first definition.
    {
        const Json dup = parse_json(R"({"id":"d","mainClass":"x","libraries":[
            {"name":"g:a:2.0"},{"name":"g:a:1.0"},{"name":"g:a:1.0:extra"}],"arguments":{"jvm":["-cp","${classpath}"],"game":[]}})");
        LaunchOptions o = options;
        o.version_id = "d";
        const std::string cp = after(plan_launch(dup, o).args, "-cp");
        CHECK(cp.find("a-2.0.jar") != std::string::npos);
        CHECK(cp.find("a-1.0.jar") == std::string::npos);
        CHECK(cp.find("a-1.0-extra.jar") != std::string::npos);   // a different classifier is a different library
    }

    // Bad input.
    {
        CHECK(throws([&] { (void)load_version_profile(root, "does-not-exist"); }));
        CHECK(throws([&] { (void)load_version_profile(root, "../escape"); }));
        write_file(root / "versions/broken/broken.json", "{not json");
        CHECK(throws([&] { (void)load_version_profile(root, "broken"); }));
        write_file(root / "versions/loop/loop.json", R"({"id":"loop","inheritsFrom":"loop","mainClass":"x"})");
        CHECK(throws([&] { (void)load_version_profile(root, "loop"); }));
        write_file(root / "versions/array/array.json", "[1,2]");
        CHECK(throws([&] { (void)load_version_profile(root, "array"); }));

        LaunchOptions bad = options;
        bad.player_name = "no spaces allowed";
        CHECK(throws([&] { (void)plan_launch(load_version_profile(root, "1.21.4"), bad); }));
        CHECK(throws([&] { (void)plan_launch(parse_json(R"({"id":"x"})"), options); }));  // no mainClass
    }

    // Installed versions: only folders with a JSON, releases first, newest first.
    {
        write_file(root / "versions/1.9/1.9.json", "{}");
        write_file(root / "versions/1.10/1.10.json", "{}");
        write_file(root / "versions/24w10a/24w10a.json", "{}");
        fs::create_directories(root / "versions/no-json");
        const std::vector<std::string> versions = installed_versions(root);
        CHECK(versions.size() == 8);  // 1.21.4, 1.10, 1.9, fabric, 24w10a, broken, loop, array
        CHECK(versions[0] == "1.21.4" && versions[1] == "1.10" && versions[2] == "1.9");
        CHECK(std::find(versions.begin(), versions.end(), "no-json") == versions.end());
        CHECK(installed_versions(root / "nope").empty());
    }

    // The printed command quotes what needs quoting.
    {
        LaunchPlan plan;
        plan.java = "/opt/java 21/bin/java";
        plan.working_dir = "/home/me/.minecraft";
        plan.args = {"-Dx=1", "name with space", "it's"};
        const std::string text = describe_plan(plan);
        CHECK(text.find("cd /home/me/.minecraft && '/opt/java 21/bin/java' -Dx=1 'name with space' 'it'\\''s'") == 0);
    }

    // Launcher settings file.
    {
        const LauncherSettingsLoad blank = parse_launcher_settings("");
        CHECK(blank.warnings.empty() && blank.settings.player_name == "Player" && blank.settings.memory_mb == 0);

        const LauncherSettingsLoad r = parse_launcher_settings(
            "# c\nminecraft_dir = /mc\nversion = 1.21.4\nplayer_name = Alex\njava = /usr/bin/java\n"
            "memory_mb = 100\nwidth = 1280\nheight = 0\njvm_args = -XX:+UseG1GC -Xss1M\ngame_args = --fullscreen\nbogus = 1\n");
        CHECK(r.warnings.size() == 1);
        CHECK(r.settings.minecraft_dir == "/mc" && r.settings.version == "1.21.4" && r.settings.player_name == "Alex");
        CHECK(r.settings.memory_mb == 512);                         // clamped up
        CHECK(r.settings.width == 0 && r.settings.height == 0);     // size needs both
        CHECK(split_arguments(r.settings.jvm_args).size() == 2 && split_arguments(r.settings.jvm_args)[1] == "-Xss1M");

        LauncherSettings s;
        s.minecraft_dir = "/games/mc";
        s.player_name = "Zed";
        s.memory_mb = 4096;
        s.width = 1920;
        s.height = 1080;
        s.game_args = "--demo";
        const LauncherSettingsLoad back = parse_launcher_settings(format_launcher_settings(s));
        CHECK(back.warnings.empty());
        CHECK(back.settings.minecraft_dir == "/games/mc" && back.settings.player_name == "Zed");
        CHECK(back.settings.memory_mb == 4096 && back.settings.width == 1920 && back.settings.height == 1080);
        CHECK(back.settings.game_args == "--demo");

        const fs::path cfg = root / "launcher.cfg";
        CHECK(save_launcher_settings(cfg, s));
        CHECK(load_launcher_settings(cfg).settings.player_name == "Zed");
        CHECK(load_launcher_settings(root / "absent.cfg").warnings.empty());
    }

    std::error_code ignored;
    fs::remove_all(root, ignored);
    std::cout << "launch tests passed\n";
    return 0;
}
