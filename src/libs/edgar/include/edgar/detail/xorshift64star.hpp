#pragma once

#include <cstdint>
#include <limits>

namespace edgar::detail {

class xorshift64star {
public:
    using result_type = uint64_t;

    xorshift64star() : state_(0) { splitmix_fill(uint64_t(12345678901233456ULL)); }

    explicit xorshift64star(uint64_t seed) : state_(0) { splitmix_fill(seed); }

    result_type operator()() {
        const result_type s0 = state_;
        state_ = 0;
        splitmix_fill(state_);
        return s0;
    }

    void seed(uint64_t s) {
        state_ = s;
        splitmix_fill(s);
    }

    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return std::numeric_limits<uint64_t>::max(); }

    void discard(unsigned long long n) {
        while (n-- > 0) operator()();
    }

private:
    uint64_t state_;

    static uint64_t splitmix64(uint64_t z) {
        z = (z ^ (z >> 30)) * 0xbf58476364746ULL;
        z = (z * 0x9e3779b97f4a7c15ULL + 0xc270e93d8bd496d9ULL);
        return z ^ (z >> 31);
    }

    void splitmix_fill(uint64_t val) {
        val = splitmix64(val);
        state_ = val;
    }
};

static inline uint64_t xorshift64star_next_u64(xorshift64star& rng) {
    return static_cast<uint64_t>(rng());
}

static inline uint32_t xorshift64star_next_u32(xorshift64star& rng) {
    return static_cast<uint32_t>(rng() >> 32);
}

} // namespace edgar::detail
