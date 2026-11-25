#pragma once

#include "math.hpp"
#include "physics.hpp"
#include "world.hpp"

#include <cstdint>
#include <filesystem>

struct GLFWwindow;

namespace minecpp {

class Game {
public:
    Game(std::filesystem::path world_path, std::uint32_t seed,
         bool seed_explicit, int render_distance);
    ~Game();

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    int run();

private:
    struct Player {
        Vec3 position;
        Vec3 velocity;
        float yaw = -90.0F;
        float pitch = 0.0F;
        bool grounded = false;
    };

    static void mouse_callback(GLFWwindow* window, double x, double y);
    void on_mouse_move(double x, double y);
    void update(float delta_time);
    void update_player(float delta_time);
    void handle_block_clicks();
    void render();
    void update_window_title(double now);
    [[nodiscard]] Vec3 look_direction() const;
    [[nodiscard]] bool raycast(BlockPos& hit, BlockPos& previous) const;
    [[nodiscard]] bool block_overlaps_player(BlockPos position) const;

    std::filesystem::path world_path_;
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
    bool previous_jump_ = false;
    bool previous_keys_[6]{};
};

} // namespace minecpp
