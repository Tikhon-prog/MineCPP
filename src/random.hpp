#pragma once

// Random number generators that must match Java bit for bit, because world
// generation and many game rules are functions of them.
//
// JavaRandom        java.util.Random: 48-bit linear congruential generator.
// mix_stafford13    Stafford's "variant 13" 64-bit mixer (the splitmix64 finalizer).
// Xoroshiro128PlusPlus  the generator newer world generation uses, with the
//                   seed expansion from a 64-bit seed.
//
// Verification status: JavaRandom and mix_stafford13 are checked in the tests
// against published reference values. The xoroshiro step follows the published
// algorithm, but its seeding path has no vanilla-derived test vector yet, so
// treat it as unverified until it is diffed against a real game.
//
// All arithmetic is done on unsigned types so overflow wraps as Java's does;
// signed overflow would be undefined behaviour in C++.
//
// nextGaussian uses std::log and std::sqrt. Java uses StrictMath (fdlibm) and
// libm may differ in the last bit, so a gaussian can differ from Java by one
// ulp on some platforms. Do not use it where exact parity matters.

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace minecpp {

class JavaRandom {
public:
    explicit JavaRandom(std::int64_t seed) { set_seed(seed); }

    void set_seed(std::int64_t seed) {
        seed_ = (static_cast<std::uint64_t>(seed) ^ kMultiplier) & kMask;
        have_gaussian_ = false;
    }

    // The `bits` high-order bits of the next state (1..32), as a signed int.
    std::int32_t next(int bits) {
        seed_ = (seed_ * kMultiplier + kAddend) & kMask;
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(seed_ >> (48 - bits)));
    }

    std::int32_t next_int() { return next(32); }

    // Uniform in [0, bound). Same rejection loop as Java, so the same seed
    // consumes the same number of states.
    std::int32_t next_int(std::int32_t bound) {
        if (bound <= 0) {
            throw std::invalid_argument("JavaRandom: bound must be positive");
        }
        if ((bound & -bound) == bound) {  // power of two
            return static_cast<std::int32_t>((static_cast<std::int64_t>(bound) * next(31)) >> 31);
        }
        std::int32_t bits;
        std::int32_t value;
        do {
            bits = next(31);
            value = bits % bound;
            // Java's `bits - value + (bound - 1) < 0` is an int overflow test.
        } while (static_cast<std::int64_t>(bits) - value + (bound - 1) > INT32_MAX);
        return value;
    }

    std::int64_t next_long() {
        const std::uint64_t hi = static_cast<std::uint64_t>(static_cast<std::int64_t>(next(32)));
        const std::uint64_t lo = static_cast<std::uint64_t>(static_cast<std::int64_t>(next(32)));
        return static_cast<std::int64_t>((hi << 32) + lo);
    }

    bool next_boolean() { return next(1) != 0; }

    float next_float() { return static_cast<float>(next(24)) / static_cast<float>(1 << 24); }

    double next_double() {
        const std::int64_t hi = static_cast<std::int64_t>(next(26));
        const std::int64_t lo = static_cast<std::int64_t>(next(27));
        return static_cast<double>((hi << 27) + lo) * (1.0 / 9007199254740992.0);  // 2^-53
    }

    double next_gaussian() {
        if (have_gaussian_) {
            have_gaussian_ = false;
            return next_gaussian_;
        }
        double v1;
        double v2;
        double s;
        do {
            v1 = 2.0 * next_double() - 1.0;
            v2 = 2.0 * next_double() - 1.0;
            s = v1 * v1 + v2 * v2;
        } while (s >= 1.0 || s == 0.0);
        const double multiplier = std::sqrt(-2.0 * std::log(s) / s);
        next_gaussian_ = v2 * multiplier;
        have_gaussian_ = true;
        return v1 * multiplier;
    }

private:
    static constexpr std::uint64_t kMultiplier = 0x5DEECE66DULL;
    static constexpr std::uint64_t kAddend = 0xBULL;
    static constexpr std::uint64_t kMask = (1ULL << 48) - 1;

    std::uint64_t seed_ = 0;
    double next_gaussian_ = 0.0;
    bool have_gaussian_ = false;
};

constexpr std::uint64_t kGoldenRatio64 = 0x9E3779B97F4A7C15ULL;

constexpr std::uint64_t mix_stafford13(std::uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

constexpr std::uint64_t rotl64(std::uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

struct Seed128 {
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
};

// Expands a 64-bit seed to the 128-bit state, as the game does for xoroshiro.
constexpr Seed128 upgrade_seed_to_128(std::uint64_t seed) {
    const std::uint64_t l = seed ^ 0x6A09E667F3BCC909ULL;
    const std::uint64_t m = l + kGoldenRatio64;
    return {mix_stafford13(l), mix_stafford13(m)};
}

class Xoroshiro128PlusPlus {
public:
    explicit Xoroshiro128PlusPlus(Seed128 seed) : lo_(seed.lo), hi_(seed.hi) {
        // An all-zero state would stay zero forever.
        if (lo_ == 0 && hi_ == 0) {
            lo_ = kGoldenRatio64;
            hi_ = 0x6A09E667F3BCC909ULL;
        }
    }
    explicit Xoroshiro128PlusPlus(std::uint64_t seed) : Xoroshiro128PlusPlus(upgrade_seed_to_128(seed)) {}

    std::int64_t next_long() {
        const std::uint64_t l = lo_;
        std::uint64_t m = hi_;
        const std::uint64_t result = rotl64(l + m, 17) + l;
        m ^= l;
        lo_ = rotl64(l, 49) ^ m ^ (m << 21);
        hi_ = rotl64(m, 28);
        return static_cast<std::int64_t>(result);
    }

    std::int32_t next_int() { return static_cast<std::int32_t>(static_cast<std::uint64_t>(next_long()) & 0xFFFFFFFFULL); }

    // Uniform in [0, bound) using the unbiased multiply-and-reject method.
    std::int32_t next_int(std::int32_t bound) {
        if (bound <= 0) {
            throw std::invalid_argument("Xoroshiro128PlusPlus: bound must be positive");
        }
        std::uint64_t l = static_cast<std::uint32_t>(next_int());
        std::uint64_t m = l * static_cast<std::uint64_t>(bound);
        std::uint64_t n = m & 0xFFFFFFFFULL;
        if (n < static_cast<std::uint64_t>(bound)) {
            const std::uint32_t threshold = (~static_cast<std::uint32_t>(bound) + 1U) % static_cast<std::uint32_t>(bound);
            while (n < threshold) {
                l = static_cast<std::uint32_t>(next_int());
                m = l * static_cast<std::uint64_t>(bound);
                n = m & 0xFFFFFFFFULL;
            }
        }
        return static_cast<std::int32_t>(m >> 32);
    }

    double next_double() {
        return static_cast<double>(static_cast<std::uint64_t>(next_long()) >> 11) * (1.0 / 9007199254740992.0);
    }

    bool next_boolean() { return (static_cast<std::uint64_t>(next_long()) & 1ULL) != 0; }

private:
    std::uint64_t lo_;
    std::uint64_t hi_;
};

}  // namespace minecpp
