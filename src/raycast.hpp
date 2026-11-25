#pragma once

#include "block.hpp"
#include "chunk.hpp"
#include "math.hpp"

#include <cmath>
#include <functional>
#include <limits>

namespace minecpp {

struct RaycastResult {
    BlockPos hit{};
    BlockPos previous{};
    float distance = 0.0F;
};

using BlockQuery = std::function<Block(int, int, int)>;

// Amanatides-Woo voxel traversal. The previous cell is suitable for placement.
[[nodiscard]] inline bool voxel_raycast(
    Vec3 origin, Vec3 direction, float max_distance,
    const BlockQuery& get_block, RaycastResult& result) {
    constexpr float coordinate_limit = 100000000.0F;
    if (!get_block || !std::isfinite(max_distance) || max_distance < 0.0F ||
        max_distance > coordinate_limit ||
        !std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
        std::abs(origin.x) > coordinate_limit || std::abs(origin.y) > coordinate_limit ||
        std::abs(origin.z) > coordinate_limit ||
        !std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z))
        return false;

    direction = normalized(direction);
    if (!std::isfinite(length(direction)) || length(direction) <= 0.000001F)
        return false;

    int x = floor_to_int(origin.x);
    int y = floor_to_int(origin.y);
    int z = floor_to_int(origin.z);
    BlockPos previous{x, y, z};
    if (get_block(x, y, z) != Block::Air) {
        result = {{x, y, z}, previous, 0.0F};
        return true;
    }

    const int step_x = direction.x > 0.0F ? 1 : (direction.x < 0.0F ? -1 : 0);
    const int step_y = direction.y > 0.0F ? 1 : (direction.y < 0.0F ? -1 : 0);
    const int step_z = direction.z > 0.0F ? 1 : (direction.z < 0.0F ? -1 : 0);
    const float infinity = std::numeric_limits<float>::infinity();

    const float delta_x = step_x == 0 ? infinity : std::abs(1.0F / direction.x);
    const float delta_y = step_y == 0 ? infinity : std::abs(1.0F / direction.y);
    const float delta_z = step_z == 0 ? infinity : std::abs(1.0F / direction.z);

    float max_x = step_x > 0
        ? (static_cast<float>(x + 1) - origin.x) / direction.x
        : (step_x < 0 ? (origin.x - static_cast<float>(x)) / -direction.x : infinity);
    float max_y = step_y > 0
        ? (static_cast<float>(y + 1) - origin.y) / direction.y
        : (step_y < 0 ? (origin.y - static_cast<float>(y)) / -direction.y : infinity);
    float max_z = step_z > 0
        ? (static_cast<float>(z + 1) - origin.z) / direction.z
        : (step_z < 0 ? (origin.z - static_cast<float>(z)) / -direction.z : infinity);

    while (true) {
        float travelled = 0.0F;
        if (max_x <= max_y && max_x <= max_z) {
            travelled = max_x;
            if (travelled > max_distance) return false;
            if ((step_x > 0 && x == std::numeric_limits<int>::max()) ||
                (step_x < 0 && x == std::numeric_limits<int>::min())) return false;
            x += step_x;
            max_x += delta_x;
        } else if (max_y <= max_z) {
            travelled = max_y;
            if (travelled > max_distance) return false;
            if ((step_y > 0 && y == std::numeric_limits<int>::max()) ||
                (step_y < 0 && y == std::numeric_limits<int>::min())) return false;
            y += step_y;
            max_y += delta_y;
        } else {
            travelled = max_z;
            if (travelled > max_distance) return false;
            if ((step_z > 0 && z == std::numeric_limits<int>::max()) ||
                (step_z < 0 && z == std::numeric_limits<int>::min())) return false;
            z += step_z;
            max_z += delta_z;
        }

        if (get_block(x, y, z) != Block::Air) {
            result = {{x, y, z}, previous, travelled};
            return true;
        }
        previous = {x, y, z};
    }
}

} // namespace minecpp
