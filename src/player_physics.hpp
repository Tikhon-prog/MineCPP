#pragma once

// One tick of player movement on foot, following vanilla's LivingEntity
// aiStep/travel for the plain case: gravity, ground and air friction, jumping
// and sprint-jumping, collision with full-cube blocks.
//
// Not modelled yet: water and lava, ladders, flying, sneaking, slime/ice
// friction per block (pass block_friction yourself), effects such as speed or
// jump boost, and the game's 0.003 sneak-edge rules.
//
// Order per tick: tiny velocities snap to zero, jump, accelerate by input,
// move with collision, then apply gravity and friction to the velocity for the
// NEXT tick. Positions therefore use last tick's velocity, as in the game.
//
// Reference numbers this reproduces (checked in the tests): a jump rises
// 1.2522 blocks, walking covers 0.2159 blocks per tick (4.317 m/s), sprinting
// 0.2806 (5.612 m/s), and a falling body gains -0.0784 then -0.155232 per tick.

#include "aabb.hpp"
#include "collision.hpp"
#include "mth.hpp"

#include <cmath>

namespace minecpp {

struct PlayerInput {
    double forward = 0.0;  // -1..1, positive = forward
    double strafe = 0.0;   // -1..1, positive = left
    bool jump = false;
    bool sprint = false;
};

struct PlayerState {
    Vec3d position;  // feet
    Vec3d velocity;  // blocks per tick, carried into the next tick
    float yaw = 0.0F;  // degrees; 0 faces +Z
    bool on_ground = false;
};

struct PlayerConstants {
    static constexpr double kWidth = 0.6;
    static constexpr double kHeight = 1.8;
    static constexpr double kGravity = 0.08;
    static constexpr double kAirDrag = 0.98;        // vertical drag
    static constexpr double kAirFriction = 0.91;    // horizontal drag in the air
    static constexpr double kWalkSpeed = 0.1;       // movement-speed attribute
    static constexpr double kSprintMultiplier = 1.3;
    static constexpr double kAirAcceleration = 0.02;
    static constexpr double kSprintAirAcceleration = 0.026;
    static constexpr double kInputScale = 0.98;     // vanilla scales input by 0.98
    static constexpr double kJumpVelocity = 0.42;
    static constexpr double kSprintJumpBoost = 0.2;
    static constexpr double kSnapZero = 0.003;
    static constexpr double kStepHeight = 0.6;
    static constexpr double kDefaultBlockFriction = 0.6;
};

inline Aabb player_box(const Vec3d& feet) {
    return Aabb::from_feet(feet.x, feet.y, feet.z, PlayerConstants::kWidth, PlayerConstants::kHeight);
}

// Advances `state` by one tick. `block_friction` is the friction of the block
// under the feet (0.6 for most blocks).
inline void tick_player(PlayerState& state, const PlayerInput& input, const SolidFn& solid,
                        double block_friction = PlayerConstants::kDefaultBlockFriction) {
    using C = PlayerConstants;
    Vec3d& v = state.velocity;

    if (std::fabs(v.x) < C::kSnapZero) v.x = 0.0;
    if (std::fabs(v.y) < C::kSnapZero) v.y = 0.0;
    if (std::fabs(v.z) < C::kSnapZero) v.z = 0.0;

    const float yaw_rad = state.yaw * kDegToRad;
    const double sin_yaw = static_cast<double>(mth_sin(yaw_rad));
    const double cos_yaw = static_cast<double>(mth_cos(yaw_rad));

    // Jump: set (not add to) the upward velocity, and push forward when sprinting.
    if (input.jump && state.on_ground) {
        if (v.y < C::kJumpVelocity) {
            v.y = C::kJumpVelocity;
        }
        if (input.sprint) {
            v.x += -sin_yaw * C::kSprintJumpBoost;
            v.z += cos_yaw * C::kSprintJumpBoost;
        }
    }

    // Accelerate along the facing direction. Diagonal input is normalised.
    double strafe = input.strafe * C::kInputScale;
    double forward = input.forward * C::kInputScale;
    const double length_sqr = strafe * strafe + forward * forward;
    if (length_sqr >= 1.0E-7) {
        double speed;
        if (state.on_ground) {
            const double base = C::kWalkSpeed * (input.sprint ? C::kSprintMultiplier : 1.0);
            // 0.21600002 (a float in vanilla) ~ 0.6^3: speed is `base` on a
            // 0.6-friction block.
            speed = base * (0.21600002 / (block_friction * block_friction * block_friction));
        } else {
            speed = input.sprint ? C::kSprintAirAcceleration : C::kAirAcceleration;
        }
        if (length_sqr > 1.0) {
            const double inv = 1.0 / std::sqrt(length_sqr);
            strafe *= inv;
            forward *= inv;
        }
        strafe *= speed;
        forward *= speed;
        v.x += strafe * cos_yaw - forward * sin_yaw;
        v.z += forward * cos_yaw + strafe * sin_yaw;
    }

    // Vanilla picks the friction from the ground state BEFORE the move.
    const bool was_on_ground = state.on_ground;

    // Move, then let the collision result cancel velocity on blocked axes.
    const MoveResult result =
        move_with_collision(solid, player_box(state.position), v, state.on_ground, C::kStepHeight);
    state.position = state.position + result.moved;
    state.on_ground = result.on_ground;
    if (result.collided_x) v.x = 0.0;
    if (result.collided_z) v.z = 0.0;
    if (result.collided_y) v.y = 0.0;

    // Gravity and friction for the next tick.
    const double friction = was_on_ground ? block_friction * C::kAirFriction : C::kAirFriction;
    v.y = (v.y - C::kGravity) * C::kAirDrag;
    v.x *= friction;
    v.z *= friction;
}

}  // namespace minecpp
