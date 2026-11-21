// base_rng.h: PCG32 random number generator (O'Neill, "PCG: A Family of Simple Fast
// Space-Efficient Statistically Good Algorithms for Random Number Generation", 2014;
// variant XSH-RR 64/32). The state is 16 bytes, pointer-free, and lives in the game state
// block (SPEC §5.4, D-37 rule 3). Every function is deterministic on every target.
//
// Draw at most ONE random number per full expression. C leaves the evaluation order of function
// arguments and of operands unspecified, and clang evaluates them in different orders on win64
// and wasm32: `bmf_v2_make(bmf_rng_f32(r), bmf_rng_f32(r))` gives swapped x/y across targets.
// Use one statement per draw.
#ifndef BMF_BASE_RNG_H
#define BMF_BASE_RNG_H

#include "base_types.h"
#include "base_math.h"

typedef struct bmf_rng {
    u64 state;
    u64 inc;      // stream selector, always odd
} bmf_rng;

// Seeds the generator. `stream` selects one of 2^63 independent sequences. O(1). Allowed in tick.
static inline u32 bmf_rng_next_u32(bmf_rng* r);
static inline void bmf_rng_seed(bmf_rng* r, u64 seed, u64 stream) {
    r->state = 0;
    r->inc = (stream << 1) | 1u;
    bmf_rng_next_u32(r);
    r->state += seed;
    bmf_rng_next_u32(r);
}

// Next uniformly distributed u32. O(1). Allowed in tick.
static inline u32 bmf_rng_next_u32(bmf_rng* r) {
    u64 old = r->state;
    r->state = old * 6364136223846793005ull + r->inc;
    u32 xorshifted = (u32)(((old >> 18) ^ old) >> 27);
    u32 rot = (u32)(old >> 59);
    return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
}

// Uniform f32 in [0, 1) with 24 random bits (every value is exactly representable). O(1).
static inline f32 bmf_rng_f32(bmf_rng* r) {
    return (f32)(bmf_rng_next_u32(r) >> 8) * (1.0f / 16777216.0f);
}

// Uniform integer in [lo, hi] (inclusive; lo <= hi), unbiased (rejection). Expected O(1).
static inline i32 bmf_rng_range_i32(bmf_rng* r, i32 lo, i32 hi) {
    BMF_ASSERT(lo <= hi);
    u32 bound = (u32)((i64)hi - (i64)lo) + 1u;   // 0 means the full 2^32 range
    if (bound == 0) return (i32)bmf_rng_next_u32(r);
    u32 threshold = (0u - bound) % bound;        // values below this would bias the modulo
    for (;;) {
        u32 x = bmf_rng_next_u32(r);
        if (x >= threshold) return (i32)((i64)lo + (i64)(x % bound));
    }
}

// Uniform f32 in [lo, hi). O(1).
static inline f32 bmf_rng_range_f32(bmf_rng* r, f32 lo, f32 hi) {
    return lo + (hi - lo) * bmf_rng_f32(r);
}

// Standard normal sample (mean 0, variance 1) by the Box-Muller transform, using the
// deterministic bmf_logf/bmf_cosf/bmf_sqrtf. Consumes two u32. u1 is in (0, 1], so the result is
// finite and |result| <= sqrt(-2 ln 2^-24) = 5.77. O(1). Allowed in tick.
static inline f32 bmf_rng_normal(bmf_rng* r) {
    f32 u1 = (f32)((bmf_rng_next_u32(r) >> 8) + 1u) * (1.0f / 16777216.0f);
    f32 u2 = bmf_rng_f32(r);
    return bmf_sqrtf(-2.0f * bmf_logf(u1)) * bmf_cosf(BMF_TAU * u2);
}

#endif // BMF_BASE_RNG_H
