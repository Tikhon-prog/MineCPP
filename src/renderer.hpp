#pragma once

#include "math.hpp"
#include "settings.hpp"
#include "tiles.hpp"
#include "world.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

struct GLFWwindow;

namespace minecpp {

class Game {
public:
    // `texture_dir` is the imported jar's textures/block folder; empty = flat colours.
    Game(std::filesystem::path world_path, std::uint32_t seed, bool seed_explicit,
         Settings settings, std::filesystem::path settings_path, std::filesystem::path texture_dir);
    ~Game();

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    int run();

private:
    struct Player {
        Vec3 position;
        float yaw = -90.0F;
        float pitch = 0.0F;
        float vertical_velocity = 0.0F;
        bool grounded = false;
    };

    static void mouse_callback(GLFWwindow* window, double x, double y);
    void on_mouse_move(double x, double y);
    void update(float delta_time);
    void update_player(float delta_time);
    void handle_block_clicks();
    void render();
    void update_window_title(double now);
    void setup_textures();
    void handle_hotkeys();
    void toggle_fullscreen();
    void show_notice(const std::string& text);
    [[nodiscard]] bool key_pressed_once(int key);
    [[nodiscard]] bool collides(const Vec3& feet_position) const;
    [[nodiscard]] Vec3 look_direction() const;
    [[nodiscard]] bool raycast(BlockPos& hit, BlockPos& previous) const;
    [[nodiscard]] bool block_overlaps_player(BlockPos position) const;

    std::filesystem::path world_path_;
    Settings settings_;
    std::filesystem::path settings_path_;
    std::filesystem::path texture_dir_;
    World world_;
    GLFWwindow* window_ = nullptr;
    Player player_;
    int selected_block_ = 0;
    bool first_mouse_ = true;
    bool mouse_captured_ = true;
    double last_mouse_x_ = 0.0;
    double last_mouse_y_ = 0.0;
    double last_title_update_ = 0.0;
    double last_save_ = 0.0;
    bool previous_left_ = false;
    bool previous_right_ = false;
    bool previous_tab_ = false;
    bool previous_keys_[6]{};

    unsigned int atlas_texture_ = 0;
    bool textured_ = false;
    TileUvTable uv_table_;
    bool settings_dirty_ = false;
    int windowed_x_ = 100;
    int windowed_y_ = 100;
    int windowed_width_ = 1280;
    int windowed_height_ = 720;
    std::map<int, bool> hotkey_state_;
    std::string notice_;
    double notice_until_ = 0.0;
};

} // namespace minecpp
