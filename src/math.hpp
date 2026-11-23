#pragma once

#include <cmath>

namespace minecpp {

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    Vec3 operator+(const Vec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
    Vec3 operator-(const Vec3& other) const { return {x - other.x, y - other.y, z - other.z}; }
    Vec3 operator*(float scale) const { return {x * scale, y * scale, z * scale}; }
    Vec3& operator+=(const Vec3& other) { x += other.x; y += other.y; z += other.z; return *this; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline float length(const Vec3& value) { return std::sqrt(dot(value, value)); }
inline Vec3 normalized(const Vec3& value) {
    const float magnitude = length(value);
    return magnitude > 0.00001F ? value * (1.0F / magnitude) : Vec3{};
}
inline int floor_to_int(float value) { return static_cast<int>(std::floor(value)); }
inline int floor_div(int value, int divisor) {
    const int quotient = value / divisor;
    const int remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}
inline int positive_mod(int value, int divisor) {
    const int remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

} // namespace minecpp
