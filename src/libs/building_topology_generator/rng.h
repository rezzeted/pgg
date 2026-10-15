#pragma once

// Deterministic PRNG for the building topology search (requirements §5.8,
// N1: own PRNG, fixed traversal order). xoshiro256** (Blackman & Vigna):
// 64-bit integer math only, so streams are bit-identical across platforms.
// Each anneal start gets its own split stream (algorithms §11).

#include <cstdint>
#include <cstddef>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace building_topology_generator {

struct Rng {
    uint64_t s[4] = {0, 0, 0, 0};

    static uint64_t splitmix64(uint64_t& x) {
        uint64_t z = (x += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }

    static Rng from_seed(uint64_t seed) {
        Rng r;
        uint64_t x = seed;
        for (int i = 0; i < 4; ++i) r.s[i] = splitmix64(x);
        return r;
    }

    // A child stream; the parent is unchanged (algorithms §11: "отщеплённый
    // поток PRNG" per start).
    Rng split(uint64_t key) const {
        Rng r;
        for (int i = 0; i < 4; ++i) r.s[i] = s[i] ^ splitmix64(key);
        // Zero state is absorbing in xoshiro; xor-fold with the key again.
        if (r.s[0] == 0 && r.s[1] == 0 && r.s[2] == 0 && r.s[3] == 0)
            r.s[0] = splitmix64(key) | 1;
        return r;
    }

    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

    uint64_t next() {
        const uint64_t result = rotl(s[1] * 5, 7) * 9;
        const uint64_t t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return result;
    }

    // Uniform double in [0, 1) (top 53 bits).
    double f64() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }

    // Uniform index in [0, n) via the multiply-high trick (unbiased).
    static uint64_t mulhi(uint64_t a, uint64_t b) {
#if defined(_MSC_VER)
        return __umulh(a, b);
#else
        return static_cast<uint64_t>((static_cast<__uint128_t>(a) * b) >> 64);
#endif
    }
    int below(int n) { return static_cast<int>(mulhi(next(), static_cast<uint64_t>(n))); }

    // Weighted pick: weights[i] >= 0, not all zero.
    int pick_weighted(const double* weights, int n) {
        double total = 0.0;
        for (int i = 0; i < n; ++i) total += weights[i];
        double u = f64() * total;
        for (int i = 0; i < n; ++i) {
            u -= weights[i];
            if (u <= 0.0) return i;
        }
        return n - 1;
    }

    // Fisher-Yates with this stream.
    template <typename T>
    void shuffle(T* data, size_t n) {
        for (size_t i = n; i-- > 1;) {
            const size_t j = static_cast<size_t>(below(static_cast<int>(i + 1)));
            const T tmp = data[i];
            data[i] = data[j];
            data[j] = tmp;
        }
    }
};

}  // namespace building_topology_generator
