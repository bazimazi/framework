// base_simd.h: portable 4-lane SIMD through clang ext_vector_type (D-06). One code path for
// x64 SSE, wasm simd128 and NEON.
//
// Determinism (D-37 rules 7 and 9): every operation here gives the same bits as the scalar code
// written next to it, on every target. Therefore:
// - There is deliberately NO reciprocal or reciprocal-sqrt estimate: use `/` and bmf_f32x4_sqrt.
// - min/max are defined as compare+select (`a < b ? a : b`), not as the target's min/max
//   instruction: x64 minps and wasm f32x4.min disagree on NaN and on -0 vs +0.
// - Arithmetic uses the plain operators (+ - * /), which are IEEE-exact lanewise. With
//   -ffp-contract=off (D-09) clang never fuses them.
#ifndef BMF_BASE_SIMD_H
#define BMF_BASE_SIMD_H

#include "base_types.h"

typedef f32 bmf_f32x4 __attribute__((ext_vector_type(4)));
typedef i32 bmf_i32x4 __attribute__((ext_vector_type(4)));
typedef u32 bmf_u32x4 __attribute__((ext_vector_type(4)));

// Arithmetic: use the operators directly: a + b, a - b, a * b, a / b (also with a scalar,
// which is splatted), and & | ^ ~ << >> on the integer types. Comparisons (a < b, a == b, ...)
// return a bmf_i32x4 mask with -1 (all bits set) for true and 0 for false.

// ---------------------------------------------------------------- construction and memory
static inline bmf_f32x4 bmf_f32x4_splat(f32 v) { bmf_f32x4 r = { v, v, v, v }; return r; }
static inline bmf_f32x4 bmf_f32x4_set(f32 x, f32 y, f32 z, f32 w) { bmf_f32x4 r = { x, y, z, w }; return r; }
static inline bmf_i32x4 bmf_i32x4_splat(i32 v) { bmf_i32x4 r = { v, v, v, v }; return r; }
static inline bmf_u32x4 bmf_u32x4_splat(u32 v) { bmf_u32x4 r = { v, v, v, v }; return r; }

// Loads 4 floats from a 16-byte-aligned address (asserted in debug).
static inline bmf_f32x4 bmf_f32x4_load(const f32* p) {
    BMF_ASSERT(((u64)p & 15u) == 0);
    return *(const bmf_f32x4*)p;
}
// Loads 4 floats from any address.
static inline bmf_f32x4 bmf_f32x4_load_unaligned(const f32* p) {
    bmf_f32x4 r;
    __builtin_memcpy(&r, p, sizeof r);
    return r;
}
static inline void bmf_f32x4_store(f32* p, bmf_f32x4 v) {
    BMF_ASSERT(((u64)p & 15u) == 0);
    *(bmf_f32x4*)p = v;
}
static inline void bmf_f32x4_store_unaligned(f32* p, bmf_f32x4 v) { __builtin_memcpy(p, &v, sizeof v); }

static inline bmf_i32x4 bmf_i32x4_load(const i32* p) {
    BMF_ASSERT(((u64)p & 15u) == 0);
    return *(const bmf_i32x4*)p;
}
static inline bmf_i32x4 bmf_i32x4_load_unaligned(const i32* p) {
    bmf_i32x4 r;
    __builtin_memcpy(&r, p, sizeof r);
    return r;
}
static inline void bmf_i32x4_store(i32* p, bmf_i32x4 v) {
    BMF_ASSERT(((u64)p & 15u) == 0);
    *(bmf_i32x4*)p = v;
}
static inline void bmf_i32x4_store_unaligned(i32* p, bmf_i32x4 v) { __builtin_memcpy(p, &v, sizeof v); }

static inline bmf_u32x4 bmf_u32x4_load_unaligned(const u32* p) {
    bmf_u32x4 r;
    __builtin_memcpy(&r, p, sizeof r);
    return r;
}
static inline void bmf_u32x4_store_unaligned(u32* p, bmf_u32x4 v) { __builtin_memcpy(p, &v, sizeof v); }

// ---------------------------------------------------------------- bit casts (no conversion)
static inline bmf_i32x4 bmf_f32x4_as_i32x4(bmf_f32x4 v) { return (bmf_i32x4)v; }
static inline bmf_f32x4 bmf_i32x4_as_f32x4(bmf_i32x4 v) { return (bmf_f32x4)v; }
static inline bmf_u32x4 bmf_i32x4_as_u32x4(bmf_i32x4 v) { return (bmf_u32x4)v; }
static inline bmf_i32x4 bmf_u32x4_as_i32x4(bmf_u32x4 v) { return (bmf_i32x4)v; }

// ---------------------------------------------------------------- select, min, max, abs, sqrt
// Lanewise mask ? a : b. `mask` lanes must be 0 or -1 (as produced by comparisons).
static inline bmf_f32x4 bmf_f32x4_select(bmf_i32x4 mask, bmf_f32x4 a, bmf_f32x4 b) {
    return (bmf_f32x4)((mask & (bmf_i32x4)a) | (~mask & (bmf_i32x4)b));
}
static inline bmf_i32x4 bmf_i32x4_select(bmf_i32x4 mask, bmf_i32x4 a, bmf_i32x4 b) {
    return (mask & a) | (~mask & b);
}

// Lanewise a < b ? a : b. Equals the scalar expression bit for bit (b is returned when equal).
static inline bmf_f32x4 bmf_f32x4_min(bmf_f32x4 a, bmf_f32x4 b) { return bmf_f32x4_select(a < b, a, b); }
// Lanewise a > b ? a : b. Equals the scalar expression bit for bit (b is returned when equal).
static inline bmf_f32x4 bmf_f32x4_max(bmf_f32x4 a, bmf_f32x4 b) { return bmf_f32x4_select(a > b, a, b); }
static inline bmf_i32x4 bmf_i32x4_min(bmf_i32x4 a, bmf_i32x4 b) { return bmf_i32x4_select(a < b, a, b); }
static inline bmf_i32x4 bmf_i32x4_max(bmf_i32x4 a, bmf_i32x4 b) { return bmf_i32x4_select(a > b, a, b); }

// Clears the sign bit (exact).
static inline bmf_f32x4 bmf_f32x4_abs(bmf_f32x4 v) { return (bmf_f32x4)((bmf_i32x4)v & bmf_i32x4_splat(0x7FFFFFFF)); }

// Correctly rounded IEEE square root per lane (sqrtps / f32x4.sqrt).
static inline bmf_f32x4 bmf_f32x4_sqrt(bmf_f32x4 v) { return __builtin_elementwise_sqrt(v); }

// ---------------------------------------------------------------- reductions (fixed order, D-37 rule 13)
// ((x + y) + z) + w: the same order as the scalar loop over the 4 lanes.
static inline f32 bmf_f32x4_sum(bmf_f32x4 v) { return ((v.x + v.y) + v.z) + v.w; }
// True if any lane of a comparison mask is set.
static inline b32 bmf_i32x4_any(bmf_i32x4 mask) { return (mask.x | mask.y | mask.z | mask.w) != 0; }

#endif // BMF_BASE_SIMD_H
