#include "random.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>

using namespace minecpp;

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Test failed: " #expression); } while (false)

template <typename Fn>
static bool throws(Fn&& fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

int main() {
    // Reference values from java.util.Random.
    {
        JavaRandom r42(42);
        CHECK(r42.next_int() == -1170105035);
        CHECK(r42.next_int() == 234785527);

        JavaRandom r0(0);
        CHECK(r0.next_int() == -1155484576);
        CHECK(r0.next_int() == -723955400);

        JavaRandom d(42);
        CHECK(d.next_double() == 0.7275636800328681);

        JavaRandom d0(0);
        CHECK(d0.next_double() == 0.730967787376657);

        // Gaussian may differ from StrictMath by an ulp, so compare loosely.
        JavaRandom g(0);
        CHECK(std::fabs(g.next_gaussian() - 0.8025330637390305) < 1e-12);
    }

    // Same seed, same stream; set_seed restarts it.
    {
        JavaRandom a(123456789L);
        JavaRandom b(123456789L);
        for (int i = 0; i < 100; ++i) {
            CHECK(a.next_long() == b.next_long());
        }
        const std::int32_t first = JavaRandom(7).next_int();
        JavaRandom c(7);
        (void)c.next_int();
        c.set_seed(7);
        CHECK(c.next_int() == first);
    }

    // Bounded ints stay in range, power-of-two and other bounds both.
    {
        JavaRandom r(99);
        for (int i = 0; i < 2000; ++i) {
            const std::int32_t a = r.next_int(10);
            CHECK(a >= 0 && a < 10);
            const std::int32_t b = r.next_int(16);
            CHECK(b >= 0 && b < 16);
            const std::int32_t c = r.next_int(1);
            CHECK(c == 0);
        }
        CHECK(throws([] { JavaRandom(1).next_int(0); }));
        CHECK(throws([] { JavaRandom(1).next_int(-5); }));

        // Power-of-two bound takes the high bits: next_int(2^k) == next(31) >> (31 - k).
        JavaRandom p(5);
        JavaRandom q(5);
        for (int i = 0; i < 50; ++i) {
            CHECK(p.next_int(8) == (q.next(31) >> 28));
        }
    }

    // Ranges of the other generators.
    {
        JavaRandom r(2024);
        for (int i = 0; i < 1000; ++i) {
            const double d = r.next_double();
            CHECK(d >= 0.0 && d < 1.0);
            const float f = r.next_float();
            CHECK(f >= 0.0F && f < 1.0F);
        }
        // next(bits) fits in `bits` bits for bits < 32.
        for (int i = 0; i < 1000; ++i) {
            const std::int32_t v = r.next(5);
            CHECK(v >= 0 && v < 32);
        }
        // Gaussians come in pairs and are finite.
        for (int i = 0; i < 200; ++i) {
            CHECK(std::isfinite(r.next_gaussian()));
        }
    }

    // splitmix64: the first output for seed 0 is the mixer applied to the golden ratio.
    {
        CHECK(mix_stafford13(0) == 0ULL);
        CHECK(mix_stafford13(kGoldenRatio64) == 0xE220A8397B1DCDAFULL);
    }

    // xoroshiro128++: deterministic, never stuck, ranges respected.
    {
        Xoroshiro128PlusPlus a(12345ULL);
        Xoroshiro128PlusPlus b(12345ULL);
        for (int i = 0; i < 100; ++i) {
            CHECK(a.next_long() == b.next_long());
        }
        Xoroshiro128PlusPlus c(1ULL);
        Xoroshiro128PlusPlus d(2ULL);
        CHECK(c.next_long() != d.next_long());

        // A zero state is replaced rather than producing zeros forever.
        Xoroshiro128PlusPlus zero(Seed128{0, 0});
        CHECK(zero.next_long() != 0 || zero.next_long() != 0);

        Xoroshiro128PlusPlus r(777ULL);
        std::int32_t seen[8] = {0};
        for (int i = 0; i < 4000; ++i) {
            const std::int32_t v = r.next_int(8);
            CHECK(v >= 0 && v < 8);
            ++seen[v];
            const double x = r.next_double();
            CHECK(x >= 0.0 && x < 1.0);
        }
        for (const std::int32_t count : seen) {
            CHECK(count > 300);  // roughly uniform: expected 500 each
        }
        CHECK(throws([] { Xoroshiro128PlusPlus(1ULL).next_int(0); }));
    }

    std::cout << "random tests passed\n";
    return 0;
}
