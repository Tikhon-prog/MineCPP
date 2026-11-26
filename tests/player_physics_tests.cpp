#include "mth.hpp"
#include "player_physics.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace minecpp;

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Test failed: " #expression); } while (false)

static bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

int main() {
    // Mth.sin / Mth.cos table lookups.
    {
        CHECK(mth_sin(0.0F) == 0.0F);
        CHECK(mth_cos(0.0F) == 1.0F);
        CHECK(std::fabs(mth_sin(1.5707964F) - 1.0F) < 1.0E-6F);
        CHECK(std::fabs(mth_sin(0.5F) - static_cast<float>(std::sin(0.5))) < 2.0E-4F);
        CHECK(std::fabs(mth_cos(0.5F) - static_cast<float>(std::cos(0.5))) < 2.0E-4F);
        CHECK(std::fabs(mth_sin(-0.5F) + mth_sin(0.5F)) < 2.0E-4F);  // negative angles wrap correctly
    }

    // Floor with its top at y = 1.
    const SolidFn floor = [](int, int y, int) { return y == 0; };
    auto standing = [] {
        PlayerState s;
        s.position = {0.5, 1.0, 0.5};
        s.on_ground = true;
        return s;
    };

    // Free fall in the air: velocity -0.0784, then -0.155232 per tick.
    {
        PlayerState s;
        s.position = {0.5, 100.0, 0.5};
        tick_player(s, {}, floor);
        CHECK(near(s.position.y, 100.0, 1.0E-12));  // moved with last tick's (zero) velocity
        CHECK(near(s.velocity.y, -0.0784, 1.0E-9));
        tick_player(s, {}, floor);
        CHECK(near(s.position.y, 100.0 - 0.0784, 1.0E-9));
        CHECK(near(s.velocity.y, -0.155232, 1.0E-9));
        CHECK(!s.on_ground);
    }

    // Falling from 4 blocks up lands on the floor within a few ticks.
    {
        PlayerState s;
        s.position = {0.5, 5.0, 0.5};
        int landed_after = -1;
        for (int tick = 1; tick <= 30; ++tick) {
            tick_player(s, {}, floor);
            if (s.on_ground) {
                landed_after = tick;
                break;
            }
        }
        CHECK(landed_after >= 10 && landed_after <= 12);
        CHECK(near(s.position.y, 1.0, 1.0E-9));
    }

    // Standing still stays put.
    {
        PlayerState s = standing();
        for (int i = 0; i < 20; ++i) {
            tick_player(s, {}, floor);
        }
        CHECK(near(s.position.y, 1.0, 1.0E-9));
        CHECK(near(s.position.x, 0.5, 1.0E-12) && near(s.position.z, 0.5, 1.0E-12));
        CHECK(s.on_ground);
    }

    // A standing jump rises about 1.2522 blocks.
    {
        PlayerState s = standing();
        PlayerInput jump;
        jump.jump = true;
        double apex = 1.0;
        for (int tick = 0; tick < 30; ++tick) {
            tick_player(s, tick == 0 ? jump : PlayerInput{}, floor);
            apex = std::max(apex, s.position.y);
        }
        CHECK(near(apex - 1.0, 1.2522, 1.0E-4));
        CHECK(s.on_ground);  // back on the floor
    }

    // Walking speed settles at ~0.2159 blocks per tick (4.317 m/s).
    {
        PlayerState s = standing();
        PlayerInput walk;
        walk.forward = 1.0;
        double before = 0.0;
        for (int tick = 0; tick < 80; ++tick) {
            before = s.position.z;
            tick_player(s, walk, floor);
        }
        CHECK(near(s.position.z - before, 0.2159, 2.0E-4));
        CHECK(near(s.position.x, 0.5, 1.0E-9));
    }

    // Sprinting settles at ~0.2806 blocks per tick (5.612 m/s).
    {
        PlayerState s = standing();
        PlayerInput run;
        run.forward = 1.0;
        run.sprint = true;
        double before = 0.0;
        for (int tick = 0; tick < 80; ++tick) {
            before = s.position.z;
            tick_player(s, run, floor);
        }
        CHECK(near(s.position.z - before, 0.2806, 2.0E-4));
    }

    // Input is scaled by 0.98, then normalised only when longer than 1. Straight
    // ahead therefore runs at 0.98 of full strength while a diagonal (forward +
    // strafe) is normalised to 1.0, so it covers 1/0.98 times the distance. This
    // is vanilla's known diagonal quirk.
    {
        PlayerState straight = standing();
        PlayerState diagonal = standing();
        PlayerInput f;
        f.forward = 1.0;
        PlayerInput d;
        d.forward = 1.0;
        d.strafe = 1.0;
        for (int tick = 0; tick < 60; ++tick) {
            tick_player(straight, f, floor);
            tick_player(diagonal, d, floor);
        }
        const double straight_dist = std::hypot(straight.position.x - 0.5, straight.position.z - 0.5);
        const double diagonal_dist = std::hypot(diagonal.position.x - 0.5, diagonal.position.z - 0.5);
        CHECK(near(diagonal_dist / straight_dist, 1.0 / 0.98, 2.0E-3));
        CHECK(diagonal.position.x > 0.5);  // strafe-left (positive) moves toward +X at yaw 0
    }

    // A sprint jump kicks the player forward on the first tick.
    {
        PlayerState s = standing();
        PlayerInput sj;
        sj.forward = 1.0;
        sj.sprint = true;
        sj.jump = true;
        tick_player(s, sj, floor);
        CHECK(s.position.z - 0.5 > 0.32 && s.position.z - 0.5 < 0.34);
    }

    // Yaw 90 turns forward into -X; yaw -90 into +X.
    {
        PlayerInput walk;
        walk.forward = 1.0;
        PlayerState left = standing();
        left.yaw = 90.0F;
        tick_player(left, walk, floor);
        CHECK(left.position.x < 0.5 - 0.09 && near(left.position.z, 0.5, 1.0E-3));

        PlayerState right = standing();
        right.yaw = -90.0F;
        tick_player(right, walk, floor);
        CHECK(right.position.x > 0.5 + 0.09 && near(right.position.z, 0.5, 1.0E-3));
    }

    // Walking into a two-block-high wall stops flush against it.
    {
        const SolidFn wall = [](int x, int y, int) { return y == 0 || (x == 3 && (y == 1 || y == 2)); };
        PlayerState s = standing();
        s.yaw = -90.0F;  // toward +X
        PlayerInput walk;
        walk.forward = 1.0;
        for (int tick = 0; tick < 40; ++tick) {
            tick_player(s, walk, wall);
        }
        CHECK(near(s.position.x, 2.7, 1.0E-6));  // wall face 3.0 minus half the 0.6 width
        CHECK(near(s.position.y, 1.0, 1.0E-9));
        CHECK(s.on_ground);
    }

    // Lower block friction (ice) makes the player slide further after input stops.
    {
        PlayerState normal = standing();
        PlayerState ice = standing();
        PlayerInput walk;
        walk.forward = 1.0;
        for (int tick = 0; tick < 40; ++tick) {
            tick_player(normal, walk, floor, 0.6);
            tick_player(ice, walk, floor, 0.98);
        }
        const double normal_start = normal.position.z;
        const double ice_start = ice.position.z;
        for (int tick = 0; tick < 20; ++tick) {
            tick_player(normal, {}, floor, 0.6);
            tick_player(ice, {}, floor, 0.98);
        }
        CHECK(ice.position.z - ice_start > normal.position.z - normal_start);
    }

    std::cout << "player physics tests passed\n";
    return 0;
}
