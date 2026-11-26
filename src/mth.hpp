#pragma once

// Vanilla's trig helpers. The game does not call std::sin: Mth.sin reads a
// 65536-entry table indexed by (int)(angle * 10430.378f), so results differ
// from libm in the low bits. Anything that feeds movement or world generation
// must use these to stay in step with Java.

#include <array>
#include <cmath>
#include <cstdint>

namespace minecpp {

namespace detail {

inline const std::array<float, 65536>& sin_table() {
    static const std::array<float, 65536> table = [] {
        std::array<float, 65536> t{};
        constexpr double kPi = 3.14159265358979323846;
        for (std::size_t i = 0; i < t.size(); ++i) {
            t[i] = static_cast<float>(std::sin(static_cast<double>(i) * kPi * 2.0 / 65536.0));
        }
        return t;
    }();
    return table;
}

}  // namespace detail

// 65536 / (2 * pi), as the float constant vanilla uses.
constexpr float kSinScale = 10430.378F;
constexpr float kDegToRad = 0.017453292F;

inline float mth_sin(float radians) {
    const auto index = static_cast<std::int32_t>(radians * kSinScale);
    return detail::sin_table()[static_cast<std::size_t>(index & 0xFFFF)];
}

inline float mth_cos(float radians) {
    const auto index = static_cast<std::int32_t>(radians * kSinScale + 16384.0F);
    return detail::sin_table()[static_cast<std::size_t>(index & 0xFFFF)];
}

}  // namespace minecpp
