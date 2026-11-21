// base_math.h: deterministic scalar math (task 1A-04a, D-17, D-37).
//
// Every function here returns bit-identical results on every target (x64 SSE, wasm32, arm64)
// and at every optimization level. They use only IEEE basic operations (+ - * / sqrt), the
// exact rounding instructions (floor/ceil/trunc), compares and integer arithmetic. Transcendental
// functions compute in f64 and round to f32 once at the end. All are allowed in `tick`.
//
// Accuracy (measured by tests/test_math.c against a double-double reference, 100 000 samples per
// function): every transcendental function is faithfully rounded, max error < 1 ULP; in practice
// the result is the correctly rounded one except in rare near-tie cases. sqrt, floor, ceil,
// round, trunc, fabs, fmin, fmax and fmod are exact.
//
// Domain rules: NaN and Inf MUST NOT reach the state block (D-37 rule 11). The functions follow
// C99 Annex F for special inputs (NaN in gives NaN out, sin(Inf) is NaN, log(0) is -Inf, and so
// on), so callers must keep inputs inside the documented domain.
#ifndef BMF_BASE_MATH_H
#define BMF_BASE_MATH_H

#include "base_types.h"

#define BMF_PI       3.14159265358979323846f
#define BMF_TAU      6.28318530717958647692f
#define BMF_HALF_PI  1.57079632679489661923f
#define BMF_F32_MAX  3.40282346638528859812e+38f
#define BMF_F32_EPSILON 1.1920928955078125e-07f   // 2^-23: the gap between 1 and the next f32

// ---------------------------------------------------------------- bit access
// Reinterprets the bits of a float as an unsigned integer and back. O(1). Allowed in tick.
static inline u32 bmf_f32_bits(f32 x) { u32 u; __builtin_memcpy(&u, &x, 4); return u; }
static inline f32 bmf_f32_from_bits(u32 u) { f32 x; __builtin_memcpy(&x, &u, 4); return x; }
static inline u64 bmf_f64_bits(f64 x) { u64 u; __builtin_memcpy(&u, &x, 8); return u; }
static inline f64 bmf_f64_from_bits(u64 u) { f64 x; __builtin_memcpy(&x, &u, 8); return x; }

// ---------------------------------------------------------------- exact operations (inline)

// Square root, the IEEE instruction (sqrtss / f32.sqrt): correctly rounded. Domain x >= 0
// (sqrt(-0) is -0; negative x gives NaN). O(1). Allowed in tick.
static inline f32 bmf_sqrtf(f32 x) { return __builtin_sqrtf(x); }

// |x|, by clearing the sign bit. Exact. O(1). Allowed in tick.
static inline f32 bmf_fabsf(f32 x) { return bmf_f32_from_bits(bmf_f32_bits(x) & 0x7FFFFFFFu); }

// Rounding to an integral value, exact. floor/ceil/trunc compile to one instruction (roundss with
// SSE4.1, part of x86-64-v2; f32.floor/ceil/trunc on wasm). Signed zeros and large values pass
// through unchanged. O(1). Allowed in tick.
static inline f32 bmf_floorf(f32 x) { return __builtin_floorf(x); }
static inline f32 bmf_ceilf(f32 x)  { return __builtin_ceilf(x); }
static inline f32 bmf_truncf(f32 x) { return __builtin_truncf(x); }

// Rounds half away from zero (C roundf): 0.5 -> 1, -0.5 -> -1, 0.49999997 -> 0. Exact. O(1).
// Allowed in tick.
static inline f32 bmf_roundf(f32 x) {
    if (!(bmf_fabsf(x) < 8388608.0f)) return x;   // |x| >= 2^23 is already integral; also Inf, NaN
    f32 t = __builtin_truncf(x);
    f32 d = x - t;                                  // the fractional part, exact
    f32 adjust = (d >= 0.5f ? 1.0f : 0.0f) - (d <= -0.5f ? 1.0f : 0.0f);   // selects, no branches
    f32 r = t + adjust;
    return bmf_f32_from_bits(bmf_f32_bits(r) | (bmf_f32_bits(x) & 0x80000000u));   // sign of x (round(-0.3) = -0)
}

// The smaller / larger of a and b with defined edge cases: -0 is smaller than +0, and if one
// operand is NaN the other is returned. Unlike minss/f32.min these agree on every target. O(1).
// Allowed in tick.
static inline f32 bmf_fminf(f32 a, f32 b) {
    if (a < b) return a;
    if (b < a) return b;
    if (a == b) return bmf_f32_from_bits(bmf_f32_bits(a) | bmf_f32_bits(b));   // min(-0, +0) = -0
    return a != a ? b : a;
}
static inline f32 bmf_fmaxf(f32 a, f32 b) {
    if (a > b) return a;
    if (b > a) return b;
    if (a == b) return bmf_f32_from_bits(bmf_f32_bits(a) & bmf_f32_bits(b));   // max(-0, +0) = +0
    return a != a ? b : a;
}

// Float to int, truncating toward zero and saturating: values >= 2^31 give INT32_MAX, values
// <= -2^31 give INT32_MIN, NaN gives 0. The ONLY allowed float-to-int conversion in simulation
// code (D-37 rule 12): a raw cast of an out-of-range value differs between x64 and wasm. O(1).
// Allowed in tick.
static inline i32 bmf_f32_to_i32(f32 x) {
    if (x != x) return 0;
    if (x >= 2147483648.0f) return 2147483647;
    if (x <= -2147483648.0f) return -2147483647 - 1;
    return (i32)x;   // in range: identical on every target
}

// ---------------------------------------------------------------- transcendental functions
// All: faithfully rounded (max error < 1 ULP), O(1), allowed in tick. Cost is a few tens of
// f64 operations; numbers are in src/base/README.md.

// sin, cos, tan of x radians, any finite x (exact range reduction, also for |x| up to 3.4e38).
// Inf or NaN gives NaN. bmf_sincosf returns exactly bmf_sinf(x) and bmf_cosf(x) at lower cost.
f32 bmf_sinf(f32 x);
f32 bmf_cosf(f32 x);
void bmf_sincosf(f32 x, f32* sin_out, f32* cos_out);
f32 bmf_tanf(f32 x);

// Inverse trigonometric functions, in radians. asin: [-pi/2, pi/2], acos: [0, pi], domain
// [-1, 1] (outside gives NaN). atan: (-pi/2, pi/2), any x.
f32 bmf_asinf(f32 x);
f32 bmf_acosf(f32 x);
f32 bmf_atanf(f32 x);

// Angle of the vector (x, y) in [-pi, pi]. atan2(0, 0) is 0, atan2(+-0, -0) is +-pi (C99).
f32 bmf_atan2f(f32 y, f32 x);

// e^x and 2^x. Overflow (x > 88.72 for exp, x >= 128 for exp2) gives +Inf, large negative x
// gives 0, results in the denormal range are rounded correctly. exp2 of an integer is exact.
f32 bmf_expf(f32 x);
f32 bmf_exp2f(f32 x);

// Natural and base-2 logarithm. Domain x > 0 (denormals included). log(0) is -Inf, negative x
// gives NaN. log2 of a power of two is exact.
f32 bmf_logf(f32 x);
f32 bmf_log2f(f32 x);

// x^y with C99 special cases (pow(x, 0) = 1, pow(1, y) = 1, negative x needs an integer y or
// gives NaN, pow(0, negative) = +Inf). Exact for exact powers such as pow(2, 10) or pow(4, 0.5).
f32 bmf_powf(f32 x, f32 y);

// Remainder of x / y with the sign of x (C fmodf), computed exactly. y == 0, x = Inf or NaN
// gives NaN.
f32 bmf_fmodf(f32 x, f32 y);

// sqrt(x^2 + y^2) without intermediate overflow or underflow. Inf if either is Inf.
f32 bmf_hypotf(f32 x, f32 y);

// ================================================================ scalar helpers (1A-04b)
// All: O(1), exact operation order as written (deterministic), allowed in tick.

// a + (b - a) * t. Exact at t = 0; at t = 1 it can differ from b by rounding.
static inline f32 bmf_lerpf(f32 a, f32 b, f32 t) { return a + (b - a) * t; }

// x limited to [lo, hi] (lo <= hi). NaN gives lo.
static inline f32 bmf_clampf(f32 x, f32 lo, f32 hi) { return bmf_fminf(bmf_fmaxf(x, lo), hi); }
static inline f32 bmf_saturatef(f32 x) { return bmf_clampf(x, 0.0f, 1.0f); }
static inline i32 bmf_clamp_i32(i32 x, i32 lo, i32 hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline i32 bmf_min_i32(i32 a, i32 b) { return a < b ? a : b; }
static inline i32 bmf_max_i32(i32 a, i32 b) { return a > b ? a : b; }

// Hermite step: 0 for x <= edge0, 1 for x >= edge1, t*t*(3 - 2t) between. edge0 != edge1.
static inline f32 bmf_smoothstepf(f32 edge0, f32 edge1, f32 x) {
    f32 t = bmf_saturatef((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

// ================================================================ vectors (1A-04b)
// Plain structs passed by value; every operation is a static inline function with a fixed
// operation order. All: O(1), allowed in tick. Lengths use the sqrt instruction, never rsqrt.

typedef struct bmf_v2 { f32 x, y; } bmf_v2;
typedef struct bmf_v3 { f32 x, y, z; } bmf_v3;
typedef struct bmf_v4 { f32 x, y, z, w; } bmf_v4;

static inline bmf_v2 bmf_v2_make(f32 x, f32 y) { bmf_v2 r = { x, y }; return r; }
static inline bmf_v2 bmf_v2_add(bmf_v2 a, bmf_v2 b) { bmf_v2 r = { a.x + b.x, a.y + b.y }; return r; }
static inline bmf_v2 bmf_v2_sub(bmf_v2 a, bmf_v2 b) { bmf_v2 r = { a.x - b.x, a.y - b.y }; return r; }
static inline bmf_v2 bmf_v2_mul(bmf_v2 a, bmf_v2 b) { bmf_v2 r = { a.x * b.x, a.y * b.y }; return r; }   // per component
static inline bmf_v2 bmf_v2_scale(bmf_v2 a, f32 s) { bmf_v2 r = { a.x * s, a.y * s }; return r; }
static inline bmf_v2 bmf_v2_div(bmf_v2 a, f32 s) { bmf_v2 r = { a.x / s, a.y / s }; return r; }       // real division, s != 0
static inline bmf_v2 bmf_v2_neg(bmf_v2 a) { bmf_v2 r = { -a.x, -a.y }; return r; }
// a + b * s (two roundings: no FMA).
static inline bmf_v2 bmf_v2_add_scaled(bmf_v2 a, bmf_v2 b, f32 s) { bmf_v2 r = { a.x + b.x * s, a.y + b.y * s }; return r; }
static inline f32 bmf_v2_dot(bmf_v2 a, bmf_v2 b) { return a.x * b.x + a.y * b.y; }
// z of the 3D cross product: positive when b is counter-clockwise from a.
static inline f32 bmf_v2_cross(bmf_v2 a, bmf_v2 b) { return a.x * b.y - a.y * b.x; }
// Rotated 90 degrees counter-clockwise: (-y, x).
static inline bmf_v2 bmf_v2_perp(bmf_v2 a) { bmf_v2 r = { -a.y, a.x }; return r; }
static inline f32 bmf_v2_length_sq(bmf_v2 a) { return a.x * a.x + a.y * a.y; }
static inline f32 bmf_v2_length(bmf_v2 a) { return bmf_sqrtf(a.x * a.x + a.y * a.y); }
static inline f32 bmf_v2_distance_sq(bmf_v2 a, bmf_v2 b) { return bmf_v2_length_sq(bmf_v2_sub(a, b)); }
static inline f32 bmf_v2_distance(bmf_v2 a, bmf_v2 b) { return bmf_v2_length(bmf_v2_sub(a, b)); }
// a / |a|, or (0, 0) when |a| is 0 (no NaN). Overflows to Inf/NaN for |a| > 1.8e19 (length_sq overflow).
static inline bmf_v2 bmf_v2_normalize_or_zero(bmf_v2 a) {
    f32 len = bmf_v2_length(a);
    if (len == 0.0f) return bmf_v2_make(0.0f, 0.0f);
    return bmf_v2_div(a, len);
}
static inline bmf_v2 bmf_v2_lerp(bmf_v2 a, bmf_v2 b, f32 t) { return bmf_v2_make(bmf_lerpf(a.x, b.x, t), bmf_lerpf(a.y, b.y, t)); }
static inline bmf_v2 bmf_v2_min(bmf_v2 a, bmf_v2 b) { return bmf_v2_make(bmf_fminf(a.x, b.x), bmf_fminf(a.y, b.y)); }
static inline bmf_v2 bmf_v2_max(bmf_v2 a, bmf_v2 b) { return bmf_v2_make(bmf_fmaxf(a.x, b.x), bmf_fmaxf(a.y, b.y)); }
static inline bmf_v2 bmf_v2_abs(bmf_v2 a) { return bmf_v2_make(bmf_fabsf(a.x), bmf_fabsf(a.y)); }
static inline bmf_v2 bmf_v2_clamp(bmf_v2 a, bmf_v2 lo, bmf_v2 hi) { return bmf_v2_make(bmf_clampf(a.x, lo.x, hi.x), bmf_clampf(a.y, lo.y, hi.y)); }
// Exact component equality (+0 == -0).
static inline b32 bmf_v2_equal(bmf_v2 a, bmf_v2 b) { return a.x == b.x && a.y == b.y; }

static inline bmf_v3 bmf_v3_make(f32 x, f32 y, f32 z) { bmf_v3 r = { x, y, z }; return r; }
static inline bmf_v3 bmf_v3_add(bmf_v3 a, bmf_v3 b) { bmf_v3 r = { a.x + b.x, a.y + b.y, a.z + b.z }; return r; }
static inline bmf_v3 bmf_v3_sub(bmf_v3 a, bmf_v3 b) { bmf_v3 r = { a.x - b.x, a.y - b.y, a.z - b.z }; return r; }
static inline bmf_v3 bmf_v3_mul(bmf_v3 a, bmf_v3 b) { bmf_v3 r = { a.x * b.x, a.y * b.y, a.z * b.z }; return r; }
static inline bmf_v3 bmf_v3_scale(bmf_v3 a, f32 s) { bmf_v3 r = { a.x * s, a.y * s, a.z * s }; return r; }
static inline bmf_v3 bmf_v3_div(bmf_v3 a, f32 s) { bmf_v3 r = { a.x / s, a.y / s, a.z / s }; return r; }
static inline bmf_v3 bmf_v3_neg(bmf_v3 a) { bmf_v3 r = { -a.x, -a.y, -a.z }; return r; }
static inline f32 bmf_v3_dot(bmf_v3 a, bmf_v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline bmf_v3 bmf_v3_cross(bmf_v3 a, bmf_v3 b) {
    bmf_v3 r = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    return r;
}
static inline f32 bmf_v3_length_sq(bmf_v3 a) { return bmf_v3_dot(a, a); }
static inline f32 bmf_v3_length(bmf_v3 a) { return bmf_sqrtf(bmf_v3_dot(a, a)); }
static inline bmf_v3 bmf_v3_normalize_or_zero(bmf_v3 a) {
    f32 len = bmf_v3_length(a);
    if (len == 0.0f) return bmf_v3_make(0.0f, 0.0f, 0.0f);
    return bmf_v3_div(a, len);
}
static inline bmf_v3 bmf_v3_lerp(bmf_v3 a, bmf_v3 b, f32 t) {
    return bmf_v3_make(bmf_lerpf(a.x, b.x, t), bmf_lerpf(a.y, b.y, t), bmf_lerpf(a.z, b.z, t));
}
static inline bmf_v3 bmf_v3_min(bmf_v3 a, bmf_v3 b) { return bmf_v3_make(bmf_fminf(a.x, b.x), bmf_fminf(a.y, b.y), bmf_fminf(a.z, b.z)); }
static inline bmf_v3 bmf_v3_max(bmf_v3 a, bmf_v3 b) { return bmf_v3_make(bmf_fmaxf(a.x, b.x), bmf_fmaxf(a.y, b.y), bmf_fmaxf(a.z, b.z)); }

static inline bmf_v4 bmf_v4_make(f32 x, f32 y, f32 z, f32 w) { bmf_v4 r = { x, y, z, w }; return r; }
static inline bmf_v4 bmf_v4_add(bmf_v4 a, bmf_v4 b) { bmf_v4 r = { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w }; return r; }
static inline bmf_v4 bmf_v4_sub(bmf_v4 a, bmf_v4 b) { bmf_v4 r = { a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w }; return r; }
static inline bmf_v4 bmf_v4_mul(bmf_v4 a, bmf_v4 b) { bmf_v4 r = { a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w }; return r; }
static inline bmf_v4 bmf_v4_scale(bmf_v4 a, f32 s) { bmf_v4 r = { a.x * s, a.y * s, a.z * s, a.w * s }; return r; }
static inline f32 bmf_v4_dot(bmf_v4 a, bmf_v4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
static inline bmf_v4 bmf_v4_lerp(bmf_v4 a, bmf_v4 b, f32 t) {
    return bmf_v4_make(bmf_lerpf(a.x, b.x, t), bmf_lerpf(a.y, b.y, t), bmf_lerpf(a.z, b.z, t), bmf_lerpf(a.w, b.w, t));
}

// ================================================================ rotation (1A-04b)
// A 2D rotation stored as its cosine and sine, so applying it needs no trig. Composing many
// rotations drifts from unit length slowly; re-create from an angle or call bmf_rot_normalize.
typedef struct bmf_rot { f32 c, s; } bmf_rot;

static inline bmf_rot bmf_rot_identity(void) { bmf_rot r = { 1.0f, 0.0f }; return r; }
// From an angle in radians (counter-clockwise), via bmf_sincosf.
static inline bmf_rot bmf_rot_from_angle(f32 radians) { bmf_rot r; bmf_sincosf(radians, &r.s, &r.c); return r; }
// The angle in (-pi, pi], via bmf_atan2f.
static inline f32 bmf_rot_angle(bmf_rot r) { return bmf_atan2f(r.s, r.c); }
// Rotation by a then b (angles add).
static inline bmf_rot bmf_rot_mul(bmf_rot a, bmf_rot b) {
    bmf_rot r = { a.c * b.c - a.s * b.s, a.s * b.c + a.c * b.s };
    return r;
}
static inline bmf_rot bmf_rot_inverse(bmf_rot r) { bmf_rot i = { r.c, -r.s }; return i; }
// Back to unit length; the identity if both parts are 0.
static inline bmf_rot bmf_rot_normalize(bmf_rot r) {
    f32 len = bmf_sqrtf(r.c * r.c + r.s * r.s);
    if (len == 0.0f) return bmf_rot_identity();
    bmf_rot n = { r.c / len, r.s / len };
    return n;
}
static inline bmf_v2 bmf_rot_apply(bmf_rot r, bmf_v2 v) {
    bmf_v2 o = { r.c * v.x - r.s * v.y, r.s * v.x + r.c * v.y };
    return o;
}
static inline bmf_v2 bmf_rot_apply_inverse(bmf_rot r, bmf_v2 v) {
    bmf_v2 o = { r.c * v.x + r.s * v.y, r.c * v.y - r.s * v.x };
    return o;
}

// ================================================================ 2D affine transform (1A-04b)
// p' = x_axis * p.x + y_axis * p.y + origin (column vectors: the images of (1,0), (0,1) and the
// origin). bmf_m3x2_mul(a, b) applies b first, then a.
typedef struct bmf_m3x2 { bmf_v2 x_axis, y_axis, origin; } bmf_m3x2;

static inline bmf_m3x2 bmf_m3x2_identity(void) { bmf_m3x2 m = { { 1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f } }; return m; }
static inline bmf_m3x2 bmf_m3x2_translation(bmf_v2 t) { bmf_m3x2 m = { { 1.0f, 0.0f }, { 0.0f, 1.0f }, t }; return m; }
// Translate * rotate * scale: scales first, then rotates, then moves to `position`.
static inline bmf_m3x2 bmf_m3x2_trs(bmf_v2 position, bmf_rot rotation, bmf_v2 scale) {
    bmf_m3x2 m;
    m.x_axis = bmf_v2_make(rotation.c * scale.x, rotation.s * scale.x);
    m.y_axis = bmf_v2_make(-rotation.s * scale.y, rotation.c * scale.y);
    m.origin = position;
    return m;
}
static inline bmf_v2 bmf_m3x2_apply_point(bmf_m3x2 m, bmf_v2 p) {
    bmf_v2 r = { m.x_axis.x * p.x + m.y_axis.x * p.y + m.origin.x, m.x_axis.y * p.x + m.y_axis.y * p.y + m.origin.y };
    return r;
}
// Direction: no translation.
static inline bmf_v2 bmf_m3x2_apply_vector(bmf_m3x2 m, bmf_v2 v) {
    bmf_v2 r = { m.x_axis.x * v.x + m.y_axis.x * v.y, m.x_axis.y * v.x + m.y_axis.y * v.y };
    return r;
}
static inline bmf_m3x2 bmf_m3x2_mul(bmf_m3x2 a, bmf_m3x2 b) {
    bmf_m3x2 m;
    m.x_axis = bmf_m3x2_apply_vector(a, b.x_axis);
    m.y_axis = bmf_m3x2_apply_vector(a, b.y_axis);
    m.origin = bmf_m3x2_apply_point(a, b.origin);
    return m;
}
static inline f32 bmf_m3x2_determinant(bmf_m3x2 m) { return m.x_axis.x * m.y_axis.y - m.y_axis.x * m.x_axis.y; }
// Inverse transform. Precondition: determinant != 0 (asserted in debug).
static inline bmf_m3x2 bmf_m3x2_inverse(bmf_m3x2 m) {
    f32 det = bmf_m3x2_determinant(m);
    BMF_ASSERT(det != 0.0f);
    f32 inv = 1.0f / det;
    bmf_m3x2 r;
    r.x_axis = bmf_v2_make(m.y_axis.y * inv, -m.x_axis.y * inv);
    r.y_axis = bmf_v2_make(-m.y_axis.x * inv, m.x_axis.x * inv);
    r.origin = bmf_v2_neg(bmf_m3x2_apply_vector(r, m.origin));
    return r;
}

// ================================================================ rectangles (1A-04b)
// bmf_rect: position and size, half-open [x, x + w) x [y, y + h) (pixels, UV rects, UI).
// bmf_aabb: closed box [min, max] (geometry, physics). Touching boxes overlap.
typedef struct bmf_rect { f32 x, y, w, h; } bmf_rect;
typedef struct bmf_aabb { bmf_v2 min, max; } bmf_aabb;

static inline bmf_rect bmf_rect_make(f32 x, f32 y, f32 w, f32 h) { bmf_rect r = { x, y, w, h }; return r; }
static inline b32 bmf_rect_contains(bmf_rect r, bmf_v2 p) { return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h; }
static inline bmf_v2 bmf_rect_center(bmf_rect r) { return bmf_v2_make(r.x + r.w * 0.5f, r.y + r.h * 0.5f); }
static inline bmf_aabb bmf_rect_to_aabb(bmf_rect r) { bmf_aabb b = { { r.x, r.y }, { r.x + r.w, r.y + r.h } }; return b; }

static inline bmf_aabb bmf_aabb_make(bmf_v2 min, bmf_v2 max) { bmf_aabb b = { min, max }; return b; }
// The box of a circle or of a center with half extents.
static inline bmf_aabb bmf_aabb_from_center(bmf_v2 center, bmf_v2 half_extents) {
    bmf_aabb b = { bmf_v2_sub(center, half_extents), bmf_v2_add(center, half_extents) };
    return b;
}
static inline b32 bmf_aabb_overlaps(bmf_aabb a, bmf_aabb b) {
    return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y;
}
static inline b32 bmf_aabb_contains_point(bmf_aabb a, bmf_v2 p) { return p.x >= a.min.x && p.x <= a.max.x && p.y >= a.min.y && p.y <= a.max.y; }
// True if `inner` lies entirely inside `outer` (used by broadphase fat boxes).
static inline b32 bmf_aabb_contains(bmf_aabb outer, bmf_aabb inner) {
    return inner.min.x >= outer.min.x && inner.min.y >= outer.min.y && inner.max.x <= outer.max.x && inner.max.y <= outer.max.y;
}
static inline bmf_aabb bmf_aabb_union(bmf_aabb a, bmf_aabb b) { return bmf_aabb_make(bmf_v2_min(a.min, b.min), bmf_v2_max(a.max, b.max)); }
static inline bmf_aabb bmf_aabb_expand(bmf_aabb a, f32 margin) {
    return bmf_aabb_make(bmf_v2_make(a.min.x - margin, a.min.y - margin), bmf_v2_make(a.max.x + margin, a.max.y + margin));
}
static inline bmf_v2 bmf_aabb_center(bmf_aabb a) { return bmf_v2_make((a.min.x + a.max.x) * 0.5f, (a.min.y + a.max.y) * 0.5f); }
static inline bmf_v2 bmf_aabb_half_extents(bmf_aabb a) { return bmf_v2_make((a.max.x - a.min.x) * 0.5f, (a.max.y - a.min.y) * 0.5f); }
// 2 (w + h): the surface-area heuristic cost in 2D.
static inline f32 bmf_aabb_perimeter(bmf_aabb a) { return 2.0f * ((a.max.x - a.min.x) + (a.max.y - a.min.y)); }

// ================================================================ colors (1A-04b)
// 0xRRGGBBAA in sRGB (SPEC §7); blending is premultiplied alpha in sRGB space.
typedef u32 bmf_color;

static inline bmf_color bmf_color_rgba(u8 r, u8 g, u8 b, u8 a) { return ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | (u32)a; }
static inline u8 bmf_color_r(bmf_color c) { return (u8)(c >> 24); }
static inline u8 bmf_color_g(bmf_color c) { return (u8)(c >> 16); }
static inline u8 bmf_color_b(bmf_color c) { return (u8)(c >> 8); }
static inline u8 bmf_color_a(bmf_color c) { return (u8)c; }
// Channels as floats in [0, 1] (channel / 255).
static inline bmf_v4 bmf_color_to_v4(bmf_color c) {
    return bmf_v4_make((f32)bmf_color_r(c) / 255.0f, (f32)bmf_color_g(c) / 255.0f, (f32)bmf_color_b(c) / 255.0f, (f32)bmf_color_a(c) / 255.0f);
}
// Floats clamped to [0, 1], scaled to 0..255 and rounded to nearest (NaN gives 0).
// bmf_color_from_v4(bmf_color_to_v4(c)) == c for every c.
static inline bmf_color bmf_color_from_v4(bmf_v4 v) {
    u32 r = (u32)bmf_f32_to_i32(bmf_saturatef(v.x) * 255.0f + 0.5f);
    u32 g = (u32)bmf_f32_to_i32(bmf_saturatef(v.y) * 255.0f + 0.5f);
    u32 b = (u32)bmf_f32_to_i32(bmf_saturatef(v.z) * 255.0f + 0.5f);
    u32 a = (u32)bmf_f32_to_i32(bmf_saturatef(v.w) * 255.0f + 0.5f);
    return (r << 24) | (g << 16) | (b << 8) | a;
}
// rgb * a / 255, rounded to nearest (integer arithmetic, exact).
static inline bmf_color bmf_color_premultiply(bmf_color c) {
    u32 a = bmf_color_a(c);
    u32 r = ((u32)bmf_color_r(c) * a + 127u) / 255u;
    u32 g = ((u32)bmf_color_g(c) * a + 127u) / 255u;
    u32 b = ((u32)bmf_color_b(c) * a + 127u) / 255u;
    return (r << 24) | (g << 16) | (b << 8) | a;
}

// ================================================================ fast tier (1A-04c, D-17)
// Lower-precision f32 versions for render and audio code. NEVER use them in simulation code
// (anything reachable from tick, D-37 rule 4): their precision may change in any release, which
// would change game state. They are plain f32 polynomials (no rsqrt/rcp instructions), so their
// results happen to match across targets, but nothing promises that.
// All: O(1), branch-light, inline (vectorizable in audio loops). NOT allowed in tick.
// Measured max errors against the deterministic tier are in tests/test_math.c (fast_tier).

// Names that simulation code must never use (D-37 rules 4, 9, 10). The determinism lint (1A-41)
// searches the simulation modules for every entry. 0-terminated.
static const char* const BMF_SIMULATION_FORBIDDEN[] = {
    "bmf_fast_", "_mm_rsqrt", "_mm_rcp", "_mm_setcsr", "__builtin_ia32_rsqrt", "__builtin_ia32_rcp",
    "__builtin_fma", "fmaf(", "relaxed_", "__int128", "long double", 0,
};

// sin and cos of x. Domain |x| <= 8192 (3-part f32 reduction); max abs error 2e-7 there.
static inline void bmf_fast_sincosf(f32 x, f32* sin_out, f32* cos_out) {
    f32 k = bmf_floorf(x * 0x1.45f306p-1f + 0.5f);                  // nearest multiple of pi/2
    f32 r = ((x - k * 0x1.92p+0f) - k * 0x1.fb4p-12f) - k * 0x1.4442d2p-24f;   // 11 + 11 + 24 bits of pi/2
    i32 q = bmf_f32_to_i32(k) & 3;
    f32 r2 = r * r;
    f32 s = r + r * r2 * (-1.0f / 6.0f + r2 * (1.0f / 120.0f + r2 * (-1.0f / 5040.0f + r2 * (1.0f / 362880.0f))));
    f32 c = 1.0f + r2 * (-0.5f + r2 * (1.0f / 24.0f + r2 * (-1.0f / 720.0f + r2 * (1.0f / 40320.0f))));
    u32 swap = 0u - (u32)(q & 1);                                    // all ones in odd quadrants
    u32 s_bits = bmf_f32_bits(s), c_bits = bmf_f32_bits(c);
    u32 sin_bits = (s_bits & ~swap) | (c_bits & swap);               // bit selects: no branches
    u32 cos_bits = (c_bits & ~swap) | (s_bits & swap);
    *sin_out = bmf_f32_from_bits(sin_bits ^ ((u32)(q & 2) << 30));   // negate in quadrants 2, 3
    *cos_out = bmf_f32_from_bits(cos_bits ^ ((u32)((q + 1) & 2) << 30));   // and in 1, 2
}
static inline f32 bmf_fast_sinf(f32 x) { f32 s, c; bmf_fast_sincosf(x, &s, &c); return s; }
static inline f32 bmf_fast_cosf(f32 x) { f32 s, c; bmf_fast_sincosf(x, &s, &c); return c; }

// Angle of (x, y) in [-pi, pi]; finite inputs. Max abs error 3e-7 rad.
static inline f32 bmf_fast_atan2f(f32 y, f32 x) {
    f32 ax = bmf_fabsf(x), ay = bmf_fabsf(y);
    f32 hi = ax > ay ? ax : ay, lo = ax > ay ? ay : ax;
    f32 t = hi > 0.0f ? lo / hi : 0.0f;                              // [0, 1]
    b32 shift = t > 0x1.126146p-2f;                                  // tan(pi/12)
    f32 u = shift ? (t * 0x1.bb67aep+0f - 1.0f) / (t + 0x1.bb67aep+0f) : t;
    f32 u2 = u * u;
    f32 a = u + u * u2 * (-1.0f / 3.0f + u2 * (1.0f / 5.0f + u2 * (-1.0f / 7.0f + u2 * (1.0f / 9.0f))));
    if (shift) a += 0x1.0c1524p-1f;                                  // pi/6
    if (ay > ax) a = 0x1.921fb6p+0f - a;                             // pi/2
    if (bmf_f32_bits(x) >> 31) a = 0x1.921fb6p+1f - a;               // pi
    return (bmf_f32_bits(y) >> 31) ? -a : a;
}

// 2^x. Max relative error 3e-7. x >= 127.5 gives +Inf, x < -126 gives 0 (no denormals).
static inline f32 bmf_fast_exp2f(f32 x) {
    if (x >= 127.5f) return bmf_f32_from_bits(0x7F800000u);
    if (x < -126.0f) return 0.0f;
    f32 k = bmf_floorf(x + 0.5f);
    f32 u = (x - k) * 0x1.62e430p-1f;                                // (x - k) ln 2, |u| <= 0.35
    f32 p = 1.0f + u * (1.0f + u * (0.5f + u * (1.0f / 6.0f + u * (1.0f / 24.0f + u * (1.0f / 120.0f + u * (1.0f / 720.0f))))));
    return p * bmf_f32_from_bits((u32)(bmf_f32_to_i32(k) + 127) << 23);
}
// e^x = 2^(x log2 e). Max relative error 1e-6 for |x| <= 10, 6e-6 near the range ends.
static inline f32 bmf_fast_expf(f32 x) { return bmf_fast_exp2f(x * 0x1.715476p+0f); }

// log2(x) for positive normal x (denormals, 0, negatives and NaN give garbage, no crash).
// Max error 2e-7 * (1 + |log2 x|) (the f32 result itself cannot be closer for large results).
static inline f32 bmf_fast_log2f(f32 x) {
    u32 bits = bmf_f32_bits(x);
    i32 e = (i32)(bits >> 23) - 127;
    u32 m_bits = (bits & 0x007FFFFFu) | 0x3F800000u;
    if (m_bits > 0x3FB504F3u) { m_bits -= 0x00800000u; e += 1; }      // m in [sqrt(1/2), sqrt(2)]
    f32 m = bmf_f32_from_bits(m_bits);
    f32 s = (m - 1.0f) / (m + 1.0f);
    f32 s2 = s * s;
    f32 log_m = 2.0f * s * (1.0f + s2 * (1.0f / 3.0f + s2 * (1.0f / 5.0f + s2 * (1.0f / 7.0f))));
    return (f32)e + log_m * 0x1.715476p+0f;
}
static inline f32 bmf_fast_logf(f32 x) { return bmf_fast_log2f(x) * 0x1.62e430p-1f; }

// x^y for x > 0 as 2^(y log2 x). Relative error about 1e-6 * (1 + |y log2 x|).
static inline f32 bmf_fast_powf(f32 x, f32 y) { return bmf_fast_exp2f(y * bmf_fast_log2f(x)); }

#endif // BMF_BASE_MATH_H
