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

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace minecpp {
namespace {
constexpr float kPlayerHeight = 1.8F;
constexpr float kPlayerRadius = 0.30F;
constexpr float kMouseSensitivity = 0.11F;
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

Game::Game(std::filesystem::path world_path, std::uint32_t seed,
           bool seed_explicit, int render_distance)
    : world_path_(std::move(world_path)), world_(seed, render_distance) {
    world_.load(world_path_, seed_explicit);
}

Game::~Game() {
    if (window_) {
        glfwDestroyWindow(window_);
        glfwTerminate();
    }
}

int Game::run() {
    if (!glfwInit()) throw std::runtime_error("GLFW could not initialise. Check that a display session is available.");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    window_ = glfwCreateWindow(1280, 720, "MineCPP — preparing voxel world", nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("Could not create an OpenGL window. Update your graphics driver.");
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);
    glfwSetWindowUserPointer(window_, this);
    glfwSetCursorPosCallback(window_, &Game::mouse_callback);
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwGetCursorPos(window_, &last_mouse_x_, &last_mouse_y_);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    glClearColor(0.53F, 0.76F, 0.94F, 1.0F);

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
    player_.yaw += static_cast<float>(dx) * kMouseSensitivity;
    player_.pitch = std::clamp(player_.pitch + static_cast<float>(dy) * kMouseSensitivity, -89.0F, 89.0F);
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
    const double top = near_plane * std::tan(static_cast<double>(radians(70.0F)) * 0.5);
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
    for (const auto& [position, chunk] : world_.chunks()) {
        (void)position;
        if (!chunk.mesh_ready || chunk.mesh.empty()) continue;
        const auto stride = static_cast<GLsizei>(sizeof(MeshVertex));
        glVertexPointer(3, GL_FLOAT, stride, static_cast<const void*>(&chunk.mesh.front().x));
        glColorPointer(3, GL_FLOAT, stride, static_cast<const void*>(&chunk.mesh.front().r));
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(chunk.mesh.size()));
    }
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

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
    title << "MineCPP 0.1.0 | XYZ " << std::fixed << std::setprecision(1)
          << player_.position.x << " " << player_.position.y << " " << player_.position.z
          << " | " << block_name(kHotbar[static_cast<std::size_t>(selected_block_)])
          << " | Chunks " << world_.loaded_chunk_count()
          << " | Jobs " << world_.pending_job_count()
          << " | " << (mouse_captured_ ? "Tab: unlock mouse" : "Tab: capture mouse");
    glfwSetWindowTitle(window_, title.str().c_str());
}

} // namespace minecpp
