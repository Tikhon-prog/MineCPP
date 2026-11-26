#pragma once

// Double-precision axis-aligned boxes and vectors for entity physics.
// Minecraft computes entity movement in doubles, so this does too; the float
// Vec3 in math.hpp stays for rendering.

#include <cmath>

namespace minecpp {

struct Vec3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    Vec3d operator+(const Vec3d& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3d operator-(const Vec3d& o) const { return {x - o.x, y - o.y, z - o.z}; }
    double length_sqr() const { return x * x + y * y + z * z; }
    double horizontal_length_sqr() const { return x * x + z * z; }
};

struct Aabb {
    // Boxes closer than this along an axis are treated as touching, not
    // overlapping (vanilla uses 1.0E-7).
    static constexpr double kEpsilon = 1.0E-7;

    double min_x = 0.0, min_y = 0.0, min_z = 0.0;
    double max_x = 0.0, max_y = 0.0, max_z = 0.0;

    // Box of an entity: centred on (x, z), feet at y.
    static Aabb from_feet(double x, double y, double z, double width, double height) {
        const double half = width / 2.0;
        return {x - half, y, z - half, x + half, y + height, z + half};
    }

    // The unit cube of block (x, y, z).
    static Aabb block(int x, int y, int z) {
        return {static_cast<double>(x), static_cast<double>(y), static_cast<double>(z),
                static_cast<double>(x + 1), static_cast<double>(y + 1), static_cast<double>(z + 1)};
    }

    Aabb moved(double dx, double dy, double dz) const {
        return {min_x + dx, min_y + dy, min_z + dz, max_x + dx, max_y + dy, max_z + dz};
    }
    Aabb moved(const Vec3d& d) const { return moved(d.x, d.y, d.z); }

    // Grows the box toward the direction of travel only.
    Aabb expanded_towards(double dx, double dy, double dz) const {
        Aabb r = *this;
        if (dx < 0.0) r.min_x += dx; else r.max_x += dx;
        if (dy < 0.0) r.min_y += dy; else r.max_y += dy;
        if (dz < 0.0) r.min_z += dz; else r.max_z += dz;
        return r;
    }

    Aabb inflated(double amount) const {
        return {min_x - amount, min_y - amount, min_z - amount, max_x + amount, max_y + amount, max_z + amount};
    }

    bool intersects(const Aabb& o) const {
        return min_x < o.max_x && max_x > o.min_x && min_y < o.max_y && max_y > o.min_y &&
               min_z < o.max_z && max_z > o.min_z;
    }

    // `this` is the obstacle. How far `m` may move along X (by `d`) before
    // touching it; returns `d` unchanged when it never would.
    double offset_x(const Aabb& m, double d) const {
        if (m.max_y > min_y + kEpsilon && m.min_y < max_y - kEpsilon &&
            m.max_z > min_z + kEpsilon && m.min_z < max_z - kEpsilon) {
            if (d > 0.0 && m.max_x <= min_x + kEpsilon) {
                const double gap = min_x - m.max_x;
                if (gap < d) d = gap;
            } else if (d < 0.0 && m.min_x >= max_x - kEpsilon) {
                const double gap = max_x - m.min_x;
                if (gap > d) d = gap;
            }
        }
        return d;
    }

    double offset_y(const Aabb& m, double d) const {
        if (m.max_x > min_x + kEpsilon && m.min_x < max_x - kEpsilon &&
            m.max_z > min_z + kEpsilon && m.min_z < max_z - kEpsilon) {
            if (d > 0.0 && m.max_y <= min_y + kEpsilon) {
                const double gap = min_y - m.max_y;
                if (gap < d) d = gap;
            } else if (d < 0.0 && m.min_y >= max_y - kEpsilon) {
                const double gap = max_y - m.min_y;
                if (gap > d) d = gap;
            }
        }
        return d;
    }

    double offset_z(const Aabb& m, double d) const {
        if (m.max_x > min_x + kEpsilon && m.min_x < max_x - kEpsilon &&
            m.max_y > min_y + kEpsilon && m.min_y < max_y - kEpsilon) {
            if (d > 0.0 && m.max_z <= min_z + kEpsilon) {
                const double gap = min_z - m.max_z;
                if (gap < d) d = gap;
            } else if (d < 0.0 && m.min_z >= max_z - kEpsilon) {
                const double gap = max_z - m.min_z;
                if (gap > d) d = gap;
            }
        }
        return d;
    }
};

}  // namespace minecpp
