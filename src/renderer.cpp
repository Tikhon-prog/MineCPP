#define GLFW_INCLUDE_NONE
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <GLFW/glfw3.h>

#include "renderer.hpp"

#include "math.hpp"
#include "texture_atlas.hpp"
#include "tiles.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace minecpp {
namespace {
constexpr float kPlayerHeight = 1.8F;
constexpr float kPlayerRadius = 0.30F;
constexpr float kPi = 3.14159265358979323846F;

constexpr std::array<Block, 6> kHotbar{
    Block::Grass, Block::Dirt, Block::Stone, Block::Sand, Block::Wood, Block::Leaves
};

float radians(float degrees) { return degrees * (kPi / 180.0F); }

bool inside_player_aabb(const Vec3& feet, BlockPos block) {
    const float min_x = feet.x - kPlayerRadius;
    const float max_x = feet.x + kPlayerRadius;
    const float min_y = feet.y;
    const float max_y = feet.y + kPlayerHeight;
    const float min_z = feet.z - kPlayerRadius;
    const float max_z = feet.z + kPlayerRadius;
    return static_cast<float>(block.x + 1) > min_x && static_cast<float>(block.x) < max_x &&
           static_cast<float>(block.y + 1) > min_y && static_cast<float>(block.y) < max_y &&
           static_cast<float>(block.z + 1) > min_z && static_cast<float>(block.z) < max_z;
}
}

Game::Game(std::filesystem::path world_path, std::uint32_t seed, bool seed_explicit,
           Settings settings, std::filesystem::path settings_path, std::filesystem::path texture_dir)
    : world_path_(std::move(world_path)),
      settings_(std::move(settings)),
      settings_path_(std::move(settings_path)),
      texture_dir_(std::move(texture_dir)),
      world_(seed, settings_.render_distance) {
    settings_.clamp();
    windowed_width_ = settings_.window_width;
    windowed_height_ = settings_.window_height;
    world_.load(world_path_, seed_explicit);
}

Game::~Game() {
    if (atlas_texture_ != 0) {
        glDeleteTextures(1, &atlas_texture_);
    }
    if (window_) {
        glfwDestroyWindow(window_);
        glfwTerminate();
    }
}

void Game::show_notice(const std::string& text) {
    notice_ = text;
    notice_until_ = glfwGetTime() + 2.5;
    last_title_update_ = 0.0;  // refresh the title now
}

bool Game::key_pressed_once(int key) {
    const bool down = glfwGetKey(window_, key) == GLFW_PRESS;
    bool& previous = hotkey_state_[key];
    const bool fired = down && !previous;
    previous = down;
    return fired;
}

void Game::toggle_fullscreen() {
    if (glfwGetWindowMonitor(window_) == nullptr) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
        if (mode == nullptr) return;
        glfwGetWindowPos(window_, &windowed_x_, &windowed_y_);
        glfwGetWindowSize(window_, &windowed_width_, &windowed_height_);
        glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        settings_.fullscreen = true;
    } else {
        glfwSetWindowMonitor(window_, nullptr, windowed_x_, windowed_y_, windowed_width_, windowed_height_, 0);
        settings_.fullscreen = false;
    }
    glfwSwapInterval(settings_.vsync ? 1 : 0);
    first_mouse_ = true;
    settings_dirty_ = true;
}

void Game::handle_hotkeys() {
    if (key_pressed_once(GLFW_KEY_F11)) {
        toggle_fullscreen();
        show_notice(settings_.fullscreen ? "Fullscreen" : "Windowed");
    }
    if (key_pressed_once(GLFW_KEY_V)) {
        settings_.vsync = !settings_.vsync;
        glfwSwapInterval(settings_.vsync ? 1 : 0);
        settings_dirty_ = true;
        show_notice(settings_.vsync ? "VSync on" : "VSync off");
    }
    if (key_pressed_once(GLFW_KEY_LEFT_BRACKET)) {
        settings_.fov -= 5.0F;
        settings_.clamp();
        settings_dirty_ = true;
        show_notice("FOV " + std::to_string(static_cast<int>(settings_.fov)));
    }
    if (key_pressed_once(GLFW_KEY_RIGHT_BRACKET)) {
        settings_.fov += 5.0F;
        settings_.clamp();
        settings_dirty_ = true;
        show_notice("FOV " + std::to_string(static_cast<int>(settings_.fov)));
    }
    if (key_pressed_once(GLFW_KEY_MINUS)) {
        settings_.mouse_sensitivity -= 0.01F;
        settings_.clamp();
        settings_dirty_ = true;
        show_notice("Mouse sensitivity " + std::to_string(static_cast<int>(settings_.mouse_sensitivity * 100.0F + 0.5F)));
    }
    if (key_pressed_once(GLFW_KEY_EQUAL)) {
        settings_.mouse_sensitivity += 0.01F;
        settings_.clamp();
        settings_dirty_ = true;
        show_notice("Mouse sensitivity " + std::to_string(static_cast<int>(settings_.mouse_sensitivity * 100.0F + 0.5F)));
    }
}

void Game::setup_textures() {
    textured_ = false;
    uv_table_ = TileUvTable{};
    if (settings_.textures && !texture_dir_.empty()) {
        const Atlas atlas = build_atlas(texture_dir_);
        if (atlas.loaded_count() > 0) {
            glGenTextures(1, &atlas_texture_);
            glBindTexture(GL_TEXTURE_2D, atlas_texture_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas.width(), atlas.height(), 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, atlas.rgba.data());
            uv_table_ = atlas.uv_table();
            textured_ = true;
            std::cout << "Loaded " << atlas.loaded_count() << "/" << kTileCount
                      << " block textures from " << texture_dir_.string() << '\n';
        } else {
            std::cout << "No block textures found in " << texture_dir_.string() << "; using flat colours.\n";
        }
    }
    // Must be set before the first chunk is meshed.
    set_mesh_uv_table(uv_table_);
}

int Game::run() {
    if (!glfwInit()) throw std::runtime_error("GLFW could not initialise. Check that a display session is available.");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    GLFWmonitor* start_monitor = nullptr;
    int start_width = settings_.window_width;
    int start_height = settings_.window_height;
    if (settings_.fullscreen) {
        start_monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = start_monitor != nullptr ? glfwGetVideoMode(start_monitor) : nullptr;
        if (mode != nullptr) {
            start_width = mode->width;
            start_height = mode->height;
        } else {
            start_monitor = nullptr;
        }
    }
    window_ = glfwCreateWindow(start_width, start_height, "MineCPP — preparing voxel world", start_monitor, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("Could not create an OpenGL window. Update your graphics driver.");
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(settings_.vsync ? 1 : 0);
    glfwSetWindowUserPointer(window_, this);
    glfwSetCursorPosCallback(window_, &Game::mouse_callback);
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwGetCursorPos(window_, &last_mouse_x_, &last_mouse_y_);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    glClearColor(0.53F, 0.76F, 0.94F, 1.0F);
    setup_textures();

    std::cout << "Generating the spawn area using background worker threads...\n";
    while (!glfwWindowShouldClose(window_) && !world_.has_spawn_chunk()) {
        world_.update_streaming(8, 8);
        world_.pump_jobs();
        glfwPollEvents();
        glfwSetWindowTitle(window_, ("MineCPP — generating chunks (" +
            std::to_string(world_.loaded_chunk_count()) + " ready)").c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (glfwWindowShouldClose(window_)) return 0;

    int spawn_x = 8;
    int spawn_z = 8;
    for (int z = 6; z <= 10; ++z) {
        for (int x = 6; x <= 10; ++x) {
            const int height = World::terrain_height(world_.seed(), x, z);
            if (world_.get_block(x, height + 1, z) == Block::Air &&
                world_.get_block(x, height + 2, z) == Block::Air) {
                spawn_x = x;
                spawn_z = z;
            }
        }
    }
    player_.position = {
        static_cast<float>(spawn_x) + 0.5F,
        static_cast<float>(World::terrain_height(world_.seed(), spawn_x, spawn_z)) + 1.02F,
        static_cast<float>(spawn_z) + 0.5F
    };

    double previous_time = glfwGetTime();
    last_save_ = previous_time;
    while (!glfwWindowShouldClose(window_)) {
        const double now = glfwGetTime();
        const float delta_time = static_cast<float>(std::clamp(now - previous_time, 0.0, 0.05));
        previous_time = now;

        glfwPollEvents();
        update(delta_time);
        render();
        update_window_title(now);
        glfwSwapBuffers(window_);

        if (now - last_save_ >= 20.0) {
            world_.save(world_path_);
            last_save_ = now;
        }
    }

    world_.save(world_path_);
    std::cout << "Saved world to " << world_path_.string() << '\n';
    if (settings_dirty_ && !settings_path_.empty()) {
        if (save_settings(settings_path_, settings_)) {
            std::cout << "Saved settings to " << settings_path_.string() << '\n';
        } else {
            std::cerr << "Could not write " << settings_path_.string() << '\n';
        }
    }
    return 0;
}

void Game::mouse_callback(GLFWwindow* window, double x, double y) {
    auto* game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (game) game->on_mouse_move(x, y);
}

void Game::on_mouse_move(double x, double y) {
    if (!mouse_captured_) return;
    if (first_mouse_) {
        last_mouse_x_ = x;
        last_mouse_y_ = y;
        first_mouse_ = false;
        return;
    }
    const double dx = x - last_mouse_x_;
    const double dy = last_mouse_y_ - y;
    last_mouse_x_ = x;
    last_mouse_y_ = y;
    player_.yaw += static_cast<float>(dx) * settings_.mouse_sensitivity;
    player_.pitch = std::clamp(player_.pitch + static_cast<float>(dy) * settings_.mouse_sensitivity, -89.0F, 89.0F);
}

Vec3 Game::look_direction() const {
    const float yaw = radians(player_.yaw);
    const float pitch = radians(player_.pitch);
    const float horizontal = std::cos(pitch);
    return normalized({horizontal * std::cos(yaw), std::sin(pitch), horizontal * std::sin(yaw)});
}

bool Game::collides(const Vec3& feet) const {
    const int min_x = floor_to_int(feet.x - kPlayerRadius);
    const int max_x = floor_to_int(feet.x + kPlayerRadius);
    const int min_y = floor_to_int(feet.y + 0.001F);
    const int max_y = floor_to_int(feet.y + kPlayerHeight - 0.001F);
    const int min_z = floor_to_int(feet.z - kPlayerRadius);
    const int max_z = floor_to_int(feet.z + kPlayerRadius);

    for (int y = min_y; y <= max_y; ++y) {
        for (int z = min_z; z <= max_z; ++z) {
            for (int x = min_x; x <= max_x; ++x) {
                if (!world_.is_chunk_loaded(x, z)) return true;
                if (is_solid(world_.get_block(x, y, z))) return true;
            }
        }
    }
    return false;
}

void Game::update_player(float delta_time) {
    const float yaw = radians(player_.yaw);
    const Vec3 forward{std::cos(yaw), 0.0F, std::sin(yaw)};
    const Vec3 right{-std::sin(yaw), 0.0F, std::cos(yaw)};
    Vec3 movement{};
    if (glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS) movement += forward;
    if (glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS) movement += forward * -1.0F;
    if (glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS) movement += right;
    if (glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS) movement += right * -1.0F;
    movement = normalized(movement);
    const float speed = glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? 8.5F : 5.4F;

    Vec3 candidate = player_.position;
    candidate.x += movement.x * speed * delta_time;
    if (!collides(candidate)) player_.position.x = candidate.x;
    candidate = player_.position;
    candidate.z += movement.z * speed * delta_time;
    if (!collides(candidate)) player_.position.z = candidate.z;

    const bool wants_jump = glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS;
    if (wants_jump && player_.grounded) {
        player_.vertical_velocity = 8.0F;
        player_.grounded = false;
    }
    player_.vertical_velocity -= 22.0F * delta_time;
    candidate = player_.position;
    candidate.y += player_.vertical_velocity * delta_time;
    if (!collides(candidate)) {
        player_.position.y = candidate.y;
        player_.grounded = false;
    } else {
        if (player_.vertical_velocity < 0.0F)
            player_.position.y = std::floor(candidate.y) + 1.0F;
        else
            player_.position.y = std::floor(candidate.y + kPlayerHeight) - kPlayerHeight;
        player_.vertical_velocity = 0.0F;
        player_.grounded = true;
    }
    if (player_.position.y < -8.0F) {
        const int x = floor_to_int(player_.position.x);
        const int z = floor_to_int(player_.position.z);
        player_.position.y = static_cast<float>(World::terrain_height(world_.seed(), x, z)) + 2.0F;
        player_.vertical_velocity = 0.0F;
    }
}

bool Game::raycast(BlockPos& hit, BlockPos& previous) const {
    const Vec3 origin = player_.position + Vec3{0.0F, 1.62F, 0.0F};
    const Vec3 direction = look_direction();
    BlockPos last{floor_to_int(origin.x), floor_to_int(origin.y), floor_to_int(origin.z)};
    for (float distance = 0.05F; distance <= 6.0F; distance += 0.05F) {
        const Vec3 point = origin + direction * distance;
        const BlockPos cell{floor_to_int(point.x), floor_to_int(point.y), floor_to_int(point.z)};
        if (cell == last) continue;
        const Block block = world_.get_block(cell.x, cell.y, cell.z);
        if (block != Block::Air) {
            hit = cell;
            previous = last;
            return true;
        }
        last = cell;
    }
    return false;
}

bool Game::block_overlaps_player(BlockPos position) const {
    return inside_player_aabb(player_.position, position);
}

void Game::handle_block_clicks() {
    const bool left = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool right = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    BlockPos hit{}, previous{};
    if (left && !previous_left_ && raycast(hit, previous) && hit.y > 0)
        (void)world_.set_block(hit.x, hit.y, hit.z, Block::Air);
    if (right && !previous_right_ && raycast(hit, previous) &&
        world_.get_block(previous.x, previous.y, previous.z) == Block::Air &&
        !block_overlaps_player(previous))
        (void)world_.set_block(previous.x, previous.y, previous.z, kHotbar[static_cast<std::size_t>(selected_block_)]);
    previous_left_ = left;
    previous_right_ = right;
}

void Game::update(float delta_time) {
    if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window_, GLFW_TRUE);

    const bool tab = glfwGetKey(window_, GLFW_KEY_TAB) == GLFW_PRESS;
    if (tab && !previous_tab_) {
        mouse_captured_ = !mouse_captured_;
        glfwSetInputMode(window_, GLFW_CURSOR, mouse_captured_ ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        first_mouse_ = true;
    }
    previous_tab_ = tab;
    handle_hotkeys();

    for (int i = 0; i < 6; ++i) {
        const int key = GLFW_KEY_1 + i;
        const bool down = glfwGetKey(window_, key) == GLFW_PRESS;
        if (down && !previous_keys_[i]) selected_block_ = i;
        previous_keys_[i] = down;
    }

    update_player(delta_time);
    world_.update_streaming(floor_to_int(player_.position.x), floor_to_int(player_.position.z));
    world_.pump_jobs();
    if (mouse_captured_) handle_block_clicks();
}

void Game::render() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    if (height <= 0 || width <= 0) return;
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const double aspect = static_cast<double>(width) / static_cast<double>(height);
    const double near_plane = 0.08;
    const double far_plane = 180.0;
    const double top = near_plane * std::tan(static_cast<double>(radians(settings_.fov)) * 0.5);
    const double right = top * aspect;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-right, right, -top, top, near_plane, far_plane);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(-player_.pitch, 1.0F, 0.0F, 0.0F);
    glRotatef(90.0F + player_.yaw, 0.0F, 1.0F, 0.0F);
    const Vec3 eye = player_.position + Vec3{0.0F, 1.62F, 0.0F};
    glTranslatef(-eye.x, -eye.y, -eye.z);

    // OpenGL 1.1 client arrays avoid a glColor/glVertex call for every vertex.
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    if (textured_) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, atlas_texture_);
        glEnable(GL_ALPHA_TEST);  // leaves have transparent gaps
        glAlphaFunc(GL_GREATER, 0.5F);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    for (const auto& [position, chunk] : world_.chunks()) {
        (void)position;
        if (!chunk.mesh_ready || chunk.mesh.empty()) continue;
        const auto stride = static_cast<GLsizei>(sizeof(MeshVertex));
        glVertexPointer(3, GL_FLOAT, stride, static_cast<const void*>(&chunk.mesh.front().x));
        glColorPointer(3, GL_FLOAT, stride, static_cast<const void*>(&chunk.mesh.front().r));
        if (textured_) glTexCoordPointer(2, GL_FLOAT, stride, static_cast<const void*>(&chunk.mesh.front().u));
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(chunk.mesh.size()));
    }
    if (textured_) {
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_TEXTURE_2D);
    }
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

    BlockPos targeted_block{}, adjacent_air{};
    if (raycast(targeted_block, adjacent_air)) {
        const float x0 = static_cast<float>(targeted_block.x) - 0.002F;
        const float y0 = static_cast<float>(targeted_block.y) - 0.002F;
        const float z0 = static_cast<float>(targeted_block.z) - 0.002F;
        const float x1 = x0 + 1.004F;
        const float y1 = y0 + 1.004F;
        const float z1 = z0 + 1.004F;
        const std::array<Vec3, 8> corners{{
            {x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1}, {x0,y0,z1},
            {x0,y1,z0}, {x1,y1,z0}, {x1,y1,z1}, {x0,y1,z1}
        }};
        constexpr std::array<int, 24> edges{
            0,1, 1,2, 2,3, 3,0,
            4,5, 5,6, 6,7, 7,4,
            0,4, 1,5, 2,6, 3,7
        };
        glColor3f(0.06F, 0.06F, 0.06F);
        glLineWidth(2.0F);
        glBegin(GL_LINES);
        for (int index : edges) {
            const Vec3& vertex = corners[static_cast<std::size_t>(index)];
            glVertex3f(vertex.x, vertex.y, vertex.z);
        }
        glEnd();
        glLineWidth(1.0F);
    }

    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(-aspect, aspect, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glColor3f(0.08F, 0.08F, 0.08F);
    glLineWidth(3.0F);
    glBegin(GL_LINES);
    glVertex2f(-0.018F, 0.0F); glVertex2f(0.018F, 0.0F);
    glVertex2f(0.0F, -0.032F); glVertex2f(0.0F, 0.032F);
    glEnd();
    glColor3f(1.0F, 1.0F, 1.0F);
    glLineWidth(1.0F);
    glBegin(GL_LINES);
    glVertex2f(-0.018F, 0.0F); glVertex2f(0.018F, 0.0F);
    glVertex2f(0.0F, -0.032F); glVertex2f(0.0F, 0.032F);
    glEnd();

    // Six-slot visual hotbar; the selected item is outlined in gold.
    constexpr float slot_width = 0.09F;
    constexpr float slot_gap = 0.018F;
    constexpr float slot_bottom = -0.91F;
    constexpr float slot_top = -0.77F;
    constexpr float total_width = slot_width * 6.0F + slot_gap * 5.0F;
    const float hotbar_left = -total_width * 0.5F;
    glColor3f(0.08F, 0.08F, 0.08F);
    glBegin(GL_QUADS);
    glVertex2f(hotbar_left - 0.025F, slot_bottom - 0.025F);
    glVertex2f(hotbar_left + total_width + 0.025F, slot_bottom - 0.025F);
    glVertex2f(hotbar_left + total_width + 0.025F, slot_top + 0.025F);
    glVertex2f(hotbar_left - 0.025F, slot_top + 0.025F);
    glEnd();
    for (std::size_t i = 0; i < kHotbar.size(); ++i) {
        const float x0 = hotbar_left + static_cast<float>(i) * (slot_width + slot_gap);
        const float x1 = x0 + slot_width;
        glColor3f(static_cast<int>(i) == selected_block_ ? 1.0F : 0.42F,
                  static_cast<int>(i) == selected_block_ ? 0.78F : 0.42F,
                  static_cast<int>(i) == selected_block_ ? 0.16F : 0.42F);
        glBegin(GL_QUADS);
        glVertex2f(x0, slot_bottom); glVertex2f(x1, slot_bottom);
        glVertex2f(x1, slot_top); glVertex2f(x0, slot_top);
        glEnd();
        if (textured_) {
            // Grass shows its top, every other block its side texture.
            const FaceKind kind = kHotbar[i] == Block::Grass ? FaceKind::Top : FaceKind::Side;
            const UvRect uv = uv_table_.uv[static_cast<std::size_t>(tile_for(kHotbar[i], kind))];
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, atlas_texture_);
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, 0.5F);
            glColor3f(1.0F, 1.0F, 1.0F);
            glBegin(GL_QUADS);
            glTexCoord2f(uv.u0, uv.v1); glVertex2f(x0 + 0.012F, slot_bottom + 0.018F);
            glTexCoord2f(uv.u1, uv.v1); glVertex2f(x1 - 0.012F, slot_bottom + 0.018F);
            glTexCoord2f(uv.u1, uv.v0); glVertex2f(x1 - 0.012F, slot_top - 0.018F);
            glTexCoord2f(uv.u0, uv.v0); glVertex2f(x0 + 0.012F, slot_top - 0.018F);
            glEnd();
            glDisable(GL_ALPHA_TEST);
            glDisable(GL_TEXTURE_2D);
        } else {
            const Color3 color = block_color(kHotbar[i]);
            glColor3f(color.r, color.g, color.b);
            glBegin(GL_QUADS);
            glVertex2f(x0 + 0.012F, slot_bottom + 0.018F);
            glVertex2f(x1 - 0.012F, slot_bottom + 0.018F);
            glVertex2f(x1 - 0.012F, slot_top - 0.018F);
            glVertex2f(x0 + 0.012F, slot_top - 0.018F);
            glEnd();
        }
    }

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
}

void Game::update_window_title(double now) {
    if (now - last_title_update_ < 0.2) return;
    last_title_update_ = now;
    std::ostringstream title;
    title << "MineCPP 0.3.0 | XYZ " << std::fixed << std::setprecision(1)
          << player_.position.x << " " << player_.position.y << " " << player_.position.z
          << " | " << block_name(kHotbar[static_cast<std::size_t>(selected_block_)])
          << " | Chunks " << world_.loaded_chunk_count()
          << " | Jobs " << world_.pending_job_count()
          << " | FOV " << static_cast<int>(settings_.fov)
          << " | " << (textured_ ? "textures" : "flat colours")
          << " | " << (mouse_captured_ ? "Tab: unlock mouse" : "Tab: capture mouse");
    if (now < notice_until_) title << " | " << notice_;
    glfwSetWindowTitle(window_, title.str().c_str());
}

} // namespace minecpp
