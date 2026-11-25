#pragma once

#include "math.hpp"

#include <functional>

namespace minecpp {

struct PhysicsBody {
    Vec3 position{}; // Feet position, not eye position.
    Vec3 velocity{};
    bool grounded = false;
};

struct PhysicsInput {
    Vec3 movement{}; // Horizontal world-space input.
    bool jump_pressed = false; // One-frame edge, not a held key.
    bool sprint = false;
};

struct PhysicsConfig {
    float player_radius = 0.30F;
    float player_height = 1.80F;
    float walk_speed = 5.4F;
    float sprint_speed = 8.5F;
    float ground_acceleration = 38.0F;
    float air_acceleration = 12.0F;
    float ground_friction = 18.0F;
    float gravity = 22.0F;
    float jump_velocity = 8.0F;
    float terminal_velocity = 55.0F;
    float collision_substep = 0.20F;
};

struct PhysicsStepResult {
    bool hit_x = false;
    bool hit_y = false;
    bool hit_z = false;
    bool grounded = false;
};

using SolidBlockQuery = std::function<bool(int, int, int)>;

class Physics {
public:
    [[nodiscard]] static bool intersects(
        const Vec3& feet_position,
        const SolidBlockQuery& is_solid,
        const PhysicsConfig& config = {});

    [[nodiscard]] static bool overlaps_block(
        const Vec3& feet_position, int block_x, int block_y, int block_z,
        const PhysicsConfig& config = {});

    [[nodiscard]] static PhysicsStepResult step(
        PhysicsBody& body, const PhysicsInput& input, float delta_time,
        const SolidBlockQuery& is_solid, const PhysicsConfig& config = {});
};

} // namespace minecpp
