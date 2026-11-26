#include "aabb.hpp"
#include "collision.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace minecpp;

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Test failed: " #expression); } while (false)

static bool approx(double a, double b) { return std::fabs(a - b) < 1.0E-9; }

int main() {
    // Box helpers.
    {
        const Aabb player = Aabb::from_feet(0.5, 1.0, 0.5, 0.6, 1.8);
        CHECK(approx(player.min_x, 0.2) && approx(player.max_x, 0.8));
        CHECK(approx(player.min_y, 1.0) && approx(player.max_y, 2.8));
        CHECK(Aabb::block(0, 0, 0).intersects(Aabb::block(0, 0, 0)));
        CHECK(!Aabb::block(0, 0, 0).intersects(Aabb::block(1, 0, 0)));  // touching only
        const Aabb grown = Aabb::block(0, 0, 0).expanded_towards(-2.0, 3.0, 0.0);
        CHECK(approx(grown.min_x, -2.0) && approx(grown.max_x, 1.0));
        CHECK(approx(grown.max_y, 4.0) && approx(grown.min_y, 0.0));
    }

    // Floor: every block with y == 0 is solid, so its top is at y = 1.
    const SolidFn floor = [](int, int y, int) { return y == 0; };
    const Aabb standing = Aabb::from_feet(0.5, 1.0, 0.5, 0.6, 1.8);

    // Standing on the floor, falling does not move and reports landing.
    {
        const MoveResult r = move_with_collision(floor, standing, {0.0, -1.0, 0.0}, true, 0.6);
        CHECK(approx(r.moved.y, 0.0));
        CHECK(r.collided_y && r.on_ground);
    }

    // Falling from above stops exactly on the floor.
    {
        const Aabb high = Aabb::from_feet(0.5, 4.0, 0.5, 0.6, 1.8);
        const MoveResult r = move_with_collision(floor, high, {0.0, -10.0, 0.0}, false, 0.6);
        CHECK(approx(r.moved.y, -3.0));
        CHECK(r.on_ground);
    }

    // Moving up into open air is free.
    {
        const MoveResult r = move_with_collision(floor, standing, {0.0, 2.0, 0.0}, true, 0.6);
        CHECK(approx(r.moved.y, 2.0));
        CHECK(!r.collided_y);
    }

    // Free horizontal move on the floor.
    {
        const MoveResult r = move_with_collision(floor, standing, {1.5, 0.0, -0.5}, true, 0.6);
        CHECK(approx(r.moved.x, 1.5) && approx(r.moved.z, -0.5));
        CHECK(!r.collided_x && !r.collided_z);
    }

    // A two-block-high wall at x = 3 stops the box at its face.
    const SolidFn wall = [](int x, int y, int) { return y == 0 || (x == 3 && (y == 1 || y == 2)); };
    {
        const MoveResult r = move_with_collision(wall, standing, {5.0, 0.0, 0.0}, true, 0.6);
        CHECK(approx(r.moved.x, 2.2));  // box max_x 0.8 -> 3.0
        CHECK(r.collided_x && !r.collided_z);
        CHECK(approx(r.moved.y, 0.0));
    }

    // Wall on the negative side stops at its far face.
    const SolidFn back_wall = [](int x, int y, int) { return y == 0 || (x == -2 && y >= 1 && y <= 2); };
    {
        const MoveResult r = move_with_collision(back_wall, standing, {-5.0, 0.0, 0.0}, true, 0.6);
        CHECK(approx(r.moved.x, -1.2));  // box min_x 0.2 -> -1.0
        CHECK(r.collided_x);
    }

    // Z collision, wall at z = 2.
    const SolidFn z_wall = [](int, int y, int z) { return y == 0 || (z == 2 && (y == 1 || y == 2)); };
    {
        const MoveResult r = move_with_collision(z_wall, standing, {0.0, 0.0, 4.0}, true, 0.6);
        CHECK(approx(r.moved.z, 1.2));  // box max_z 0.8 -> 2.0
        CHECK(r.collided_z && !r.collided_x);
    }

    // A one-block step: blocked at step height 0.6, climbs with step height 1.
    const SolidFn step = [](int x, int y, int z) { return y == 0 || (x == 2 && y == 1 && z == 0); };
    {
        const MoveResult blocked = move_with_collision(step, standing, {2.0, 0.0, 0.0}, true, 0.6);
        CHECK(approx(blocked.moved.x, 1.2));
        CHECK(approx(blocked.moved.y, 0.0));

        const MoveResult climbed = move_with_collision(step, standing, {2.0, 0.0, 0.0}, true, 1.0);
        CHECK(approx(climbed.moved.x, 2.0));
        CHECK(approx(climbed.moved.y, 1.0));

        // Not grounded: no step, even with the height.
        const MoveResult airborne = move_with_collision(step, standing.moved(0.0, 3.0, 0.0), {2.0, 0.0, 0.0}, false, 1.0);
        CHECK(approx(airborne.moved.x, 2.0));  // free in the air above the step
        CHECK(approx(airborne.moved.y, 0.0));
    }

    // Diagonal into a corner slides along the open wall: the axis order only
    // affects which axis is clipped first.
    {
        const MoveResult r = move_with_collision(wall, standing, {4.0, 0.0, 1.0}, true, 0.6);
        CHECK(approx(r.moved.x, 2.2));
        CHECK(approx(r.moved.z, 1.0));
    }

    // Collecting block boxes only returns solids near the region.
    {
        std::vector<Aabb> out;
        collect_block_boxes(floor, Aabb::from_feet(0.5, 1.0, 0.5, 0.6, 1.8), out);
        CHECK(!out.empty());
        for (const Aabb& b : out) {
            CHECK(approx(b.min_y, 0.0) && approx(b.max_y, 1.0));
        }
    }

    std::cout << "collision tests passed\n";
    return 0;
}
