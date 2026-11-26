#pragma once

// Entity-vs-block collision, following the structure of vanilla's
// Entity.collide: the move is clipped per axis in the order Y, then the larger
// horizontal component first, and a grounded entity that hit a wall retries
// with its step height.
//
// Blocks are full unit cubes here (SolidFn). Partial shapes (slabs, stairs,
// fences) need per-block box lists and are a later step.

#include "aabb.hpp"

#include <cmath>
#include <functional>
#include <vector>

namespace minecpp {

using SolidFn = std::function<bool(int x, int y, int z)>;

struct MoveResult {
    Vec3d moved;            // the movement actually allowed
    bool collided_x = false;
    bool collided_y = false;
    bool collided_z = false;
    bool on_ground = false;  // landed this move (moving down and blocked)
};

// Boxes of every solid block that could touch `region`.
inline void collect_block_boxes(const SolidFn& solid, const Aabb& region, std::vector<Aabb>& out) {
    const int x0 = static_cast<int>(std::floor(region.min_x - 1.0E-7)) - 1;
    const int y0 = static_cast<int>(std::floor(region.min_y - 1.0E-7)) - 1;
    const int z0 = static_cast<int>(std::floor(region.min_z - 1.0E-7)) - 1;
    const int x1 = static_cast<int>(std::floor(region.max_x + 1.0E-7)) + 1;
    const int y1 = static_cast<int>(std::floor(region.max_y + 1.0E-7)) + 1;
    const int z1 = static_cast<int>(std::floor(region.max_z + 1.0E-7)) + 1;
    for (int y = y0; y <= y1; ++y) {
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                if (solid(x, y, z)) {
                    out.push_back(Aabb::block(x, y, z));
                }
            }
        }
    }
}

// Clips `m` against `boxes`: Y first, then X and Z with the larger first.
inline Vec3d collide_with_boxes(Vec3d m, const Aabb& box, const std::vector<Aabb>& boxes) {
    Aabb b = box;
    if (m.y != 0.0) {
        for (const Aabb& o : boxes) {
            m.y = o.offset_y(b, m.y);
        }
        b = b.moved(0.0, m.y, 0.0);
    }
    const bool x_first = std::fabs(m.x) >= std::fabs(m.z);
    if (x_first && m.x != 0.0) {
        for (const Aabb& o : boxes) {
            m.x = o.offset_x(b, m.x);
        }
        b = b.moved(m.x, 0.0, 0.0);
    }
    if (m.z != 0.0) {
        for (const Aabb& o : boxes) {
            m.z = o.offset_z(b, m.z);
        }
        b = b.moved(0.0, 0.0, m.z);
    }
    if (!x_first && m.x != 0.0) {
        for (const Aabb& o : boxes) {
            m.x = o.offset_x(b, m.x);
        }
    }
    return m;
}

inline Vec3d collide_bounding(const SolidFn& solid, Vec3d m, const Aabb& box) {
    std::vector<Aabb> boxes;
    collect_block_boxes(solid, box.expanded_towards(m.x, m.y, m.z), boxes);
    return collide_with_boxes(m, box, boxes);
}

// The movement an entity with `box` can make when asked for `movement`.
// `max_up_step` is the step height (0.6 for most mobs, 0 to disable).
inline MoveResult move_with_collision(const SolidFn& solid, const Aabb& box, Vec3d movement,
                                      bool on_ground, double max_up_step) {
    Vec3d result = movement.length_sqr() == 0.0 ? movement : collide_bounding(solid, movement, box);

    const bool x_hit = movement.x != result.x;
    const bool y_hit = movement.y != result.y;
    const bool z_hit = movement.z != result.z;
    const bool landed = y_hit && movement.y < 0.0;

    if (max_up_step > 0.0 && (on_ground || landed) && (x_hit || z_hit)) {
        Vec3d step_move = collide_bounding(solid, {movement.x, max_up_step, movement.z}, box);
        const Vec3d up_only =
            collide_bounding(solid, {0.0, max_up_step, 0.0}, box.expanded_towards(movement.x, 0.0, movement.z));
        if (up_only.y < max_up_step) {
            const Vec3d over = collide_bounding(solid, {movement.x, 0.0, movement.z}, box.moved(up_only)) + up_only;
            if (over.horizontal_length_sqr() > step_move.horizontal_length_sqr()) {
                step_move = over;
            }
        }
        if (step_move.horizontal_length_sqr() > result.horizontal_length_sqr()) {
            const Vec3d down =
                collide_bounding(solid, {0.0, -step_move.y + movement.y, 0.0}, box.moved(step_move));
            result = step_move + down;
        }
    }

    MoveResult out;
    out.moved = result;
    out.collided_x = movement.x != result.x;
    out.collided_y = movement.y != result.y;
    out.collided_z = movement.z != result.z;
    out.on_ground = out.collided_y && movement.y < 0.0;
    return out;
}

}  // namespace minecpp
