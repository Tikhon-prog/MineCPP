#include "physics.hpp"

#include "block.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace minecpp {
namespace {

constexpr float kEpsilon = 0.0001F;
constexpr float kGroundProbe = 0.025F;
constexpr int kBinaryIterations = 18;
constexpr int kMaximumSubsteps = 4096;

void approach_horizontal_velocity(
    PhysicsBody& body, const PhysicsInput& input, float dt,
    const PhysicsConfig& config) {
    Vec3 direction{input.movement.x, 0.0F, input.movement.z};
    const float direction_length = length(direction);
    if (direction_length > 1.0F) direction = direction * (1.0F / direction_length);

    const float target_speed = input.sprint ? config.sprint_speed : config.walk_speed;
    const Vec3 target = direction * target_speed;
    const float acceleration = body.grounded
        ? config.ground_acceleration : config.air_acceleration;

    if (direction_length > kEpsilon) {
        const Vec3 difference{
            target.x - body.velocity.x, 0.0F, target.z - body.velocity.z
        };
        const float difference_length = length(difference);
        const float maximum_change = acceleration * dt;
        if (difference_length <= maximum_change) {
            body.velocity.x = target.x;
            body.velocity.z = target.z;
        } else if (difference_length > kEpsilon) {
            const Vec3 change = difference * (maximum_change / difference_length);
            body.velocity.x += change.x;
            body.velocity.z += change.z;
        }
    } else if (body.grounded) {
        const float speed = std::sqrt(
            body.velocity.x * body.velocity.x + body.velocity.z * body.velocity.z);
        const float reduction = config.ground_friction * dt;
        if (speed <= reduction) {
            body.velocity.x = 0.0F;
            body.velocity.z = 0.0F;
        } else if (speed > kEpsilon) {
            const float factor = (speed - reduction) / speed;
            body.velocity.x *= factor;
            body.velocity.z *= factor;
        }
    }
}

bool move_axis(
    PhysicsBody& body, int axis, float displacement,
    const SolidBlockQuery& is_solid, const PhysicsConfig& config) {
    if (std::abs(displacement) <= kEpsilon) return false;

    const float substep = std::max(config.collision_substep, 0.025F);
    const float requested = std::ceil(std::abs(displacement) / substep);
    // Refuse a pathological teleport-sized physics step instead of silently
    // clamping the substep count and allowing the player to tunnel through walls.
    if (!std::isfinite(requested) || requested > static_cast<float>(kMaximumSubsteps))
        return true;

    const int steps = std::max(1, static_cast<int>(requested));
    const float amount = displacement / static_cast<float>(steps);
    for (int index = 0; index < steps; ++index) {
        Vec3 candidate = body.position;
        if (axis == 0) candidate.x += amount;
        else if (axis == 1) candidate.y += amount;
        else candidate.z += amount;

        if (!Physics::intersects(candidate, is_solid, config)) {
            body.position = candidate;
            continue;
        }

        float low = 0.0F;
        float high = 1.0F;
        for (int iteration = 0; iteration < kBinaryIterations; ++iteration) {
            const float middle = (low + high) * 0.5F;
            candidate = body.position;
            const float partial = amount * middle;
            if (axis == 0) candidate.x += partial;
            else if (axis == 1) candidate.y += partial;
            else candidate.z += partial;

            if (Physics::intersects(candidate, is_solid, config)) high = middle;
            else low = middle;
        }

        const float safe_amount = amount * low;
        if (axis == 0) body.position.x += safe_amount;
        else if (axis == 1) body.position.y += safe_amount;
        else body.position.z += safe_amount;
        return true;
    }
    return false;
}

bool finite_config(const PhysicsConfig& config) {
    return std::isfinite(config.player_radius) && std::isfinite(config.player_height) &&
           std::isfinite(config.walk_speed) && std::isfinite(config.sprint_speed) &&
           std::isfinite(config.ground_acceleration) && std::isfinite(config.air_acceleration) &&
           std::isfinite(config.ground_friction) && std::isfinite(config.gravity) &&
           std::isfinite(config.jump_velocity) && std::isfinite(config.terminal_velocity) &&
           std::isfinite(config.collision_substep);
}

} // namespace

bool Physics::intersects(
    const Vec3& feet, const SolidBlockQuery& is_solid, const PhysicsConfig& config) {
    constexpr float coordinate_limit = 100000000.0F;
    if (!is_solid) return false;
    if (!std::isfinite(feet.x) || !std::isfinite(feet.y) || !std::isfinite(feet.z) ||
        std::abs(feet.x) > coordinate_limit || std::abs(feet.y) > coordinate_limit ||
        std::abs(feet.z) > coordinate_limit || !finite_config(config) ||
        config.player_radius <= 0.0F || config.player_radius > 8.0F ||
        config.player_height <= 0.0F || config.player_height > 16.0F)
        return true;

    const int min_x = static_cast<int>(std::floor(feet.x - config.player_radius + kEpsilon));
    const int max_x = static_cast<int>(std::floor(feet.x + config.player_radius - kEpsilon));
    const int min_y = static_cast<int>(std::floor(feet.y + kEpsilon));
    const int max_y = static_cast<int>(std::floor(feet.y + config.player_height - kEpsilon));
    const int min_z = static_cast<int>(std::floor(feet.z - config.player_radius + kEpsilon));
    const int max_z = static_cast<int>(std::floor(feet.z + config.player_radius - kEpsilon));

    for (int y = min_y; y <= max_y; ++y)
        for (int z = min_z; z <= max_z; ++z)
            for (int x = min_x; x <= max_x; ++x)
                if (is_solid(x, y, z)) return true;
    return false;
}

bool Physics::overlaps_block(
    const Vec3& feet, int block_x, int block_y, int block_z,
    const PhysicsConfig& config) {
    if (!std::isfinite(feet.x) || !std::isfinite(feet.y) || !std::isfinite(feet.z) ||
        !finite_config(config) || config.player_radius <= 0.0F ||
        config.player_height <= 0.0F)
        return true;

    const float min_x = feet.x - config.player_radius;
    const float max_x = feet.x + config.player_radius;
    const float min_y = feet.y;
    const float max_y = feet.y + config.player_height;
    const float min_z = feet.z - config.player_radius;
    const float max_z = feet.z + config.player_radius;

    return static_cast<float>(block_x) + 1.0F > min_x + kEpsilon &&
           static_cast<float>(block_x) < max_x - kEpsilon &&
           static_cast<float>(block_y) + 1.0F > min_y + kEpsilon &&
           static_cast<float>(block_y) < max_y - kEpsilon &&
           static_cast<float>(block_z) + 1.0F > min_z + kEpsilon &&
           static_cast<float>(block_z) < max_z - kEpsilon;
}

PhysicsStepResult Physics::step(
    PhysicsBody& body, const PhysicsInput& input, float delta_time,
    const SolidBlockQuery& is_solid, const PhysicsConfig& config) {
    PhysicsStepResult result;
    if (!is_solid || !std::isfinite(delta_time) || delta_time <= 0.0F ||
        !std::isfinite(body.position.x) || !std::isfinite(body.position.y) ||
        !std::isfinite(body.position.z) || !std::isfinite(body.velocity.x) ||
        !std::isfinite(body.velocity.y) || !std::isfinite(body.velocity.z) ||
        !std::isfinite(input.movement.x) || !std::isfinite(input.movement.z) ||
        !finite_config(config) || config.terminal_velocity <= 0.0F ||
        config.gravity < 0.0F || config.collision_substep <= 0.0F ||
        config.collision_substep > 4.0F || config.walk_speed < 0.0F ||
        config.sprint_speed < 0.0F || config.ground_acceleration < 0.0F ||
        config.air_acceleration < 0.0F || config.ground_friction < 0.0F ||
        config.player_radius <= 0.0F || config.player_height <= 0.0F) {
        result.grounded = body.grounded;
        return result;
    }

    const float dt = std::clamp(delta_time, 0.0F, 0.10F);
    if (body.velocity.y <= 0.0F) {
        Vec3 probe = body.position;
        probe.y -= kGroundProbe;
        body.grounded = intersects(probe, is_solid, config);
    }
    if (input.jump_pressed && body.grounded) {
        body.velocity.y = config.jump_velocity;
        body.grounded = false;
    }

    approach_horizontal_velocity(body, input, dt, config);
    result.hit_x = move_axis(body, 0, body.velocity.x * dt, is_solid, config);
    if (result.hit_x) body.velocity.x = 0.0F;
    result.hit_z = move_axis(body, 2, body.velocity.z * dt, is_solid, config);
    if (result.hit_z) body.velocity.z = 0.0F;

    body.velocity.y = std::max(
        -config.terminal_velocity, body.velocity.y - config.gravity * dt);
    result.hit_y = move_axis(body, 1, body.velocity.y * dt, is_solid, config);
    if (result.hit_y) {
        if (body.velocity.y < 0.0F) body.grounded = true;
        body.velocity.y = 0.0F;
    } else {
        Vec3 probe = body.position;
        probe.y -= kGroundProbe;
        body.grounded = intersects(probe, is_solid, config);
        if (body.grounded && body.velocity.y < 0.0F) body.velocity.y = 0.0F;
    }

    result.grounded = body.grounded;
    return result;
}

} // namespace minecpp
