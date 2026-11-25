#include "physics.hpp"
#include "raycast.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_set>

using namespace minecpp;

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Test failed: " #expression); } while (false)

namespace {
struct Cell {
    int x, y, z;
    bool operator==(const Cell&) const = default;
};
struct CellHash {
    std::size_t operator()(const Cell& cell) const noexcept {
        std::size_t hash = static_cast<std::size_t>(static_cast<unsigned int>(cell.x));
        hash = hash * 1315423911U + static_cast<unsigned int>(cell.y);
        hash = hash * 2654435761U + static_cast<unsigned int>(cell.z);
        return hash;
    }
};
}

int main() {
    std::unordered_set<Cell, CellHash> solid;
    for (int z = -8; z <= 8; ++z)
        for (int x = -8; x <= 8; ++x)
            solid.insert({x, 0, z});
    for (int y = 1; y <= 4; ++y)
        for (int z = -4; z <= 4; ++z)
            solid.insert({2, y, z});

    const SolidBlockQuery collision = [&solid](int x, int y, int z) {
        return solid.contains({x, y, z});
    };

    CHECK(Physics::intersects({0.5F, 0.5F, 0.5F}, collision));
    CHECK(!Physics::intersects({0.5F, 1.0F, 0.5F}, collision));
    CHECK(Physics::overlaps_block({0.5F, 1.0F, 0.5F}, 0, 1, 0));
    CHECK(!Physics::overlaps_block({0.5F, 1.0F, 0.5F}, 4, 1, 0));

    PhysicsBody player{{0.5F, 1.0F, 0.5F}, {}, false};
    for (int frame = 0; frame < 120; ++frame)
        (void)Physics::step(player, {}, 1.0F / 60.0F, collision);
    CHECK(player.grounded);
    CHECK(std::abs(player.position.y - 1.0F) < 0.002F);
    CHECK(std::abs(player.velocity.y) < 0.001F);

    PhysicsInput forward;
    forward.movement = {1.0F, 0.0F, 0.0F};
    for (int frame = 0; frame < 120; ++frame)
        (void)Physics::step(player, forward, 1.0F / 60.0F, collision);
    CHECK(player.position.x < 1.701F);
    CHECK(player.velocity.x == 0.0F);

    PhysicsInput jump;
    jump.jump_pressed = true;
    (void)Physics::step(player, jump, 1.0F / 60.0F, collision);
    CHECK(!player.grounded);
    CHECK(player.velocity.y > 0.0F);
    for (int frame = 0; frame < 90; ++frame)
        (void)Physics::step(player, {}, 1.0F / 60.0F, collision);
    CHECK(player.grounded);
    CHECK(std::abs(player.position.y - 1.0F) < 0.003F);

    PhysicsBody fast{{0.5F, 1.0F, 0.5F}, {40.0F, 0.0F, 0.0F}, true};
    const auto fast_result = Physics::step(fast, {}, 0.10F, collision);
    CHECK(fast_result.hit_x);
    CHECK(fast.position.x < 1.701F);

    const auto invalid_step = Physics::step(
        fast, {}, std::numeric_limits<float>::quiet_NaN(), collision);
    CHECK(invalid_step.hit_x == false);
    std::unordered_set<Cell, CellHash> targets{Cell{3, 1, 0}, Cell{-3, 1, 0}};
    const BlockQuery get_block = [&targets](int x, int y, int z) {
        return targets.contains({x, y, z}) ? Block::Stone : Block::Air;
    };
    RaycastResult hit;
    CHECK(voxel_raycast({0.5F, 1.5F, 0.5F}, {1.0F, 0.0F, 0.0F}, 6.0F, get_block, hit));
    CHECK(hit.hit.x == 3 && hit.hit.y == 1 && hit.hit.z == 0);
    CHECK(hit.previous.x == 2 && hit.previous.y == 1 && hit.previous.z == 0);
    CHECK(std::abs(hit.distance - 2.5F) < 0.001F);

    CHECK(voxel_raycast({0.5F, 1.5F, 0.5F}, {-1.0F, 0.0F, 0.0F}, 6.0F, get_block, hit));
    CHECK(hit.hit.x == -3 && hit.hit.y == 1 && hit.hit.z == 0);
    CHECK(hit.previous.x == -2 && hit.previous.y == 1 && hit.previous.z == 0);
    CHECK(!voxel_raycast({0.5F, 1.5F, 0.5F}, {1.0F, 0.0F, 0.0F}, 1.0F, get_block, hit));
    CHECK(!voxel_raycast(
        {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F}, 6.0F, get_block, hit));

    std::cout << "MineCPP engine tests defined\n";
    return 0;
}
