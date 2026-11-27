#include "settings.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace minecpp;
namespace fs = std::filesystem;

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Test failed: " #expression); } while (false)

int main() {
    // Defaults.
    {
        const Settings s;
        CHECK(s.render_distance == 4 && s.fov == 70.0F && s.vsync && !s.fullscreen);
        CHECK(s.textures && s.jar.empty() && s.minecraft_dir.empty() && s.data_dir == "minecpp-data");
        CHECK(parse_settings("").warnings.empty());
        CHECK(parse_settings("").settings.render_distance == 4);
    }

    // Parsing: comments, spacing, case, quotes, bool spellings.
    {
        const SettingsLoad r = parse_settings(
            "# comment\n"
            "\n"
            "render_distance = 6\n"
            "  FOV=90.5  \n"
            "mouse_sensitivity = 0.2\n"
            "vsync = off\n"
            "fullscreen = YES\n"
            "window_width = 1920\n"
            "window_height = 1080\n"
            "textures = 0\n"
            "minecraft_dir = \"/home/me/.minecraft\"\n"
            "jar = /tmp/client.jar\n"
            "data_dir = game-data\n");
        CHECK(r.warnings.empty());
        const Settings& s = r.settings;
        CHECK(s.render_distance == 6);
        CHECK(s.fov == 90.5F);
        CHECK(s.mouse_sensitivity == 0.2F);
        CHECK(!s.vsync && s.fullscreen && !s.textures);
        CHECK(s.window_width == 1920 && s.window_height == 1080);
        CHECK(s.minecraft_dir == "/home/me/.minecraft");  // quotes stripped
        CHECK(s.jar == "/tmp/client.jar");
        CHECK(s.data_dir == "game-data");
    }

    // Out-of-range numbers are clamped.
    {
        const Settings s = parse_settings("render_distance = 99\nfov = 5\nmouse_sensitivity = 50\nwindow_width = 10\nwindow_height = 99999\n").settings;
        CHECK(s.render_distance == 8 && s.fov == 30.0F && s.mouse_sensitivity == 1.0F);
        CHECK(s.window_width == 320 && s.window_height == 4320);
        Settings low;
        low.render_distance = -3;
        low.clamp();
        CHECK(low.render_distance == 2);
    }

    // Bad lines warn and are skipped; good lines still apply.
    {
        const SettingsLoad r = parse_settings(
            "render_distance = lots\n"
            "no equals sign\n"
            "colour = blue\n"
            "fov = 85\n"
            "vsync = maybe\n"
            "window_width = 12px\n");
        CHECK(r.warnings.size() == 5);
        CHECK(r.settings.fov == 85.0F);
        CHECK(r.settings.render_distance == 4);  // unchanged by the bad value
        CHECK(r.settings.vsync);
        CHECK(r.warnings[0].find("line 1") == 0);
    }

    // format -> parse is lossless.
    {
        Settings s;
        s.render_distance = 7;
        s.fov = 80.0F;
        s.mouse_sensitivity = 0.25F;
        s.vsync = false;
        s.fullscreen = true;
        s.window_width = 1600;
        s.window_height = 900;
        s.textures = false;
        s.minecraft_dir = "/games/mc";
        s.jar = "/games/mc/versions/1.21/1.21.jar";
        s.data_dir = "elsewhere";
        const SettingsLoad back = parse_settings(format_settings(s));
        CHECK(back.warnings.empty());
        CHECK(back.settings.render_distance == 7 && back.settings.fov == 80.0F);
        CHECK(back.settings.mouse_sensitivity == 0.25F && !back.settings.vsync && back.settings.fullscreen);
        CHECK(back.settings.window_width == 1600 && back.settings.window_height == 900);
        CHECK(!back.settings.textures);
        CHECK(back.settings.minecraft_dir == "/games/mc" && back.settings.jar == s.jar);
        CHECK(back.settings.data_dir == "elsewhere");

        // Empty values (the defaults written for minecraft_dir/jar) survive too.
        const Settings defaults = parse_settings(format_settings(Settings{})).settings;
        CHECK(defaults.minecraft_dir.empty() && defaults.jar.empty());
    }

    // Files: a missing one is just the defaults, a saved one loads back.
    {
        const fs::path path = fs::temp_directory_path() /
            ("minecpp_settings_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".cfg");
        const SettingsLoad missing = load_settings(path);
        CHECK(missing.warnings.empty() && missing.settings.render_distance == 4);

        Settings s;
        s.render_distance = 3;
        s.fov = 100.0F;
        CHECK(save_settings(path, s));
        const SettingsLoad again = load_settings(path);
        CHECK(again.warnings.empty());
        CHECK(again.settings.render_distance == 3 && again.settings.fov == 100.0F);

        std::error_code ignored;
        fs::remove(path, ignored);
        CHECK(!save_settings("/nonexistent-dir/minecpp.cfg", s));
    }

    std::cout << "settings tests passed\n";
    return 0;
}
