// base_math.c: see base_math.h.
//
// Structure: every transcendental function converts its f32 input to f64, reduces the argument
// exactly (or with error far below 2^-50 relative), evaluates a polynomial kernel in f64 and
// rounds to f32 once. f64 + - * / and sqrt are correctly rounded IEEE operations on every target,
// and -ffp-contract=off forbids FMA contraction (D-09), so the f64 bits are identical everywhere.
// The kernels are truncated Taylor series whose first omitted term is below 2^-52 relative on the
// kernel interval; the f32 result therefore has a total error of 0.5 ULP + about 2^-25 ULP.
//
// Constants: hex float literals are the exact f64 values. They were derived from pi and ln 2
// computed with big integers (Machin's formula, atanh series); tests/test_math.c recomputes them.
#include "base_math.h"

static const f64 MATH_PI       = 0x1.921fb54442d18p+1;    // pi rounded to f64
static const f64 MATH_PIO2     = 0x1.921fb54442d18p+0;    // pi/2 rounded
static const f64 MATH_PIO4     = 0x1.921fb54442d18p-1;    // pi/4 rounded
static const f64 MATH_INV_PIO2 = 0x1.45f306dc9c883p-1;    // 2/pi rounded
static const f64 MATH_PIO2_1   = 0x1.921fb544p+0;         // first 33 bits of pi/2
static const f64 MATH_PIO2_2   = 0x1.0b4611a6p-34;        // next 33 bits
static const f64 MATH_PIO2_2T  = 0x1.3198a2e037073p-69;   // pi/2 - PIO2_1 - PIO2_2, rounded
static const f64 MATH_LN2      = 0x1.62e42fefa39efp-1;    // ln 2 rounded
static const f64 MATH_LN2_HI   = 0x1.62e42feep-1;         // first 32 bits of ln 2
static const f64 MATH_LN2_LO   = 0x1.a39ef35793c76p-33;   // ln 2 - LN2_HI, rounded
static const f64 MATH_INV_LN2  = 0x1.71547652b82fep+0;    // 1/ln 2 rounded

// Bits of 2/pi: 2/pi = sum over j of MATH_TWO_OVER_PI_BITS[j] * 2^(-32 (j+1)). Words 0..6 are
// needed for f32 inputs up to 2^128; the rest are guard words.
static const u32 MATH_TWO_OVER_PI_BITS[10] = {
    0xA2F9836Eu, 0x4E441529u, 0xFC2757D1u, 0xF534DDC0u, 0xDB629599u,
    0x3C439041u, 0xFE5163ABu, 0xDEBBC561u, 0xB7246E3Au, 0x424DD2E0u,
};

static inline f32 math_nan32(void) { return bmf_f32_from_bits(0x7FC00000u); }
static inline f32 math_inf32(void) { return bmf_f32_from_bits(0x7F800000u); }

// 2^k as an exact f64, k in [-1022, 1023].
static inline f64 math_pow2(i32 k) { return bmf_f64_from_bits((u64)(i64)(k + 1023) << 52); }

// ---------------------------------------------------------------- trigonometric range reduction

// Bits [pos, pos + 32) of the 160-bit little-endian number p[0..4] (p[0] least significant).
// Bits outside the number read as zero; pos may be negative.
static u32 math_window32(const u32* p, i32 pos) {
    i32 word = pos >= 0 ? pos / 32 : -((31 - pos) / 32);   // floor(pos / 32)
    i32 shift = pos - word * 32;
    u32 lo = (word >= 0 && word < 5) ? p[word] : 0u;
    u32 hi = (word + 1 >= 0 && word + 1 < 5) ? p[word + 1] : 0u;
    return shift == 0 ? lo : (lo >> shift) | (hi << (32 - shift));
}

// Payne-Hanek reduction for 2^17 <= ax < 2^128 (finite): ax = m * 2^e with a 24-bit integer m,
// and ax * 2/pi = m * 2^e * (bits of 2/pi). Words of 2/pi whose product with m * 2^e is a
// multiple of 4 cannot change the quadrant or the fraction and are skipped; the next four words
// give a 152-bit product P with ax * 2/pi = P * 2^-s (mod 4) up to an error below 2^-71.
static f64 math_reduce_large(f32 ax, i32* quadrant) {
    u32 bits = bmf_f32_bits(ax);
    i32 e = (i32)(bits >> 23) - 150;                    // -6 .. 104 for this path
    u64 m = (u64)((bits & 0x007FFFFFu) | 0x00800000u);
    i32 j0 = e >= 2 ? (e - 2) / 32 : 0;
    u32 p[5];
    u64 carry = 0;
    for (i32 i = 0; i < 4; i++) {                        // m * (128-bit window), least significant word first
        u64 v = m * (u64)MATH_TWO_OVER_PI_BITS[j0 + 3 - i] + carry;
        p[i] = (u32)v;
        carry = v >> 32;
    }
    p[4] = (u32)carry;
    i32 s = 32 * (j0 + 4) - e;                           // 95 .. 134: fraction bits below s
    u32 q = math_window32(p, s) & 3u;
    u64 f_hi = ((u64)math_window32(p, s - 32) << 32) | (u64)math_window32(p, s - 64);
    u32 f_lo = math_window32(p, s - 96);
    f64 sign = 1.0;
    if (f_hi >> 63) {                                    // fraction >= 1/2: next quadrant, fraction - 1
        q = (q + 1u) & 3u;
        b32 low_zero = f_lo == 0;
        f_lo = 0u - f_lo;                                // 96-bit two's complement negation
        f_hi = ~f_hi + (low_zero ? 1u : 0u);
        sign = -1.0;
    }
    // The 96-bit fraction as f64: three exact u32 conversions, two roundings.
    f64 turns = (f64)(u32)(f_hi >> 32) * 0x1p-32 + (f64)(u32)f_hi * 0x1p-64 + (f64)f_lo * 0x1p-96;
    *quadrant = (i32)q;
    return sign * turns * MATH_PIO2;
}

// Reduces ax = |x| (finite) to r in about [-pi/4, pi/4] with ax = r + quadrant * pi/2 (mod 2 pi).
// For ax < 2^17: Cody-Waite with a three-part pi/2. k < 2^17 and the first two parts have 33
// bits, so k * PIO2_1 and k * PIO2_2 are exact and ax - k * PIO2_1 is exact (both are multiples
// of 2^-32 and the difference is below 1).
static f64 math_reduce_pio2(f32 ax, i32* quadrant) {
    if (ax < 131072.0f) {
        f64 x = (f64)ax;
        f64 k = __builtin_floor(x * MATH_INV_PIO2 + 0.5);
        f64 r = x - k * MATH_PIO2_1;
        r = r - k * MATH_PIO2_2;
        r = r - k * MATH_PIO2_2T;
        *quadrant = (i32)k & 3;                          // 0 <= k < 2^17: the cast is in range
        return r;
    }
    return math_reduce_large(ax, quadrant);
}

// sin(r) and cos(r) for |r| <= pi/4 (plus a little): Taylor series to r^17 and r^16. The first
// omitted terms are below 2^-55 and 2^-58 there.
static f64 math_sin_kernel(f64 r) {
    f64 r2 = r * r;
    f64 p = 1.0 / 355687428096000.0;                     // 1/17!
    p = p * -r2 + 1.0 / 1307674368000.0;                 // 1/15!
    p = p * -r2 + 1.0 / 6227020800.0;                    // 1/13!
    p = p * -r2 + 1.0 / 39916800.0;                      // 1/11!
    p = p * -r2 + 1.0 / 362880.0;                        // 1/9!
    p = p * -r2 + 1.0 / 5040.0;                          // 1/7!
    p = p * -r2 + 1.0 / 120.0;                           // 1/5!
    p = p * -r2 + 1.0 / 6.0;                             // 1/3!
    return r - r * r2 * p;
}
static f64 math_cos_kernel(f64 r) {
    f64 r2 = r * r;
    f64 p = 1.0 / 20922789888000.0;                      // 1/16!
    p = p * -r2 + 1.0 / 87178291200.0;                   // 1/14!
    p = p * -r2 + 1.0 / 479001600.0;                     // 1/12!
    p = p * -r2 + 1.0 / 3628800.0;                       // 1/10!
    p = p * -r2 + 1.0 / 40320.0;                         // 1/8!
    p = p * -r2 + 1.0 / 720.0;                           // 1/6!
    p = p * -r2 + 1.0 / 24.0;                            // 1/4!
    p = p * -r2 + 1.0 / 2.0;                             // 1/2!
    return 1.0 - r2 * p;
}

// sin and cos of ax = |x| (finite) in f64. Both kernels are always evaluated and the quadrant
// is applied with bit selects: random angles would mispredict a branch half of the time.
static inline void math_sincos64(f32 ax, f64* sin_out, f64* cos_out) {
    i32 q;
    f64 r = math_reduce_pio2(ax, &q);
    u64 s_bits = bmf_f64_bits(math_sin_kernel(r)), c_bits = bmf_f64_bits(math_cos_kernel(r));
    u64 swap = 0ull - (u64)(q & 1);                       // all ones in odd quadrants
    u64 sin_bits = (s_bits & ~swap) | (c_bits & swap);
    u64 cos_bits = (c_bits & ~swap) | (s_bits & swap);
    *sin_out = bmf_f64_from_bits(sin_bits ^ ((u64)(q & 2) << 62));          // negative in quadrants 2, 3
    *cos_out = bmf_f64_from_bits(cos_bits ^ ((u64)((q + 1) & 2) << 62));    // negative in quadrants 1, 2
}

f32 bmf_sinf(f32 x) {
    f32 ax = bmf_fabsf(x);
    if (!(ax <= BMF_F32_MAX)) return math_nan32();      // Inf or NaN
    f64 s, c;
    math_sincos64(ax, &s, &c);
    return bmf_f32_from_bits(bmf_f32_bits((f32)s) ^ (bmf_f32_bits(x) & 0x80000000u));   // sin(-x) = -sin(x)
}

f32 bmf_cosf(f32 x) {
    f32 ax = bmf_fabsf(x);
    if (!(ax <= BMF_F32_MAX)) return math_nan32();
    f64 s, c;
    math_sincos64(ax, &s, &c);
    return (f32)c;
}

void bmf_sincosf(f32 x, f32* sin_out, f32* cos_out) {
    f32 ax = bmf_fabsf(x);
    if (!(ax <= BMF_F32_MAX)) { *sin_out = math_nan32(); *cos_out = math_nan32(); return; }
    f64 s, c;
    math_sincos64(ax, &s, &c);
    *sin_out = bmf_f32_from_bits(bmf_f32_bits((f32)s) ^ (bmf_f32_bits(x) & 0x80000000u));
    *cos_out = (f32)c;
}

f32 bmf_tanf(f32 x) {
    f32 ax = bmf_fabsf(x);
    if (!(ax <= BMF_F32_MAX)) return math_nan32();
    i32 q;
    f64 r = math_reduce_pio2(ax, &q);
    u64 s_bits = bmf_f64_bits(math_sin_kernel(r)), c_bits = bmf_f64_bits(math_cos_kernel(r));
    u64 odd = 0ull - (u64)(q & 1);                        // tan(r + pi/2) = -cos(r) / sin(r)
    f64 numerator = bmf_f64_from_bits((s_bits & ~odd) | (c_bits & odd));
    f64 denominator = bmf_f64_from_bits((c_bits & ~odd) | (s_bits & odd));
    f32 out = (f32)(numerator / denominator);
    u32 negate = ((u32)q << 31) ^ (bmf_f32_bits(x) & 0x80000000u);   // odd quadrant xor negative x
    return bmf_f32_from_bits(bmf_f32_bits(out) ^ negate);
}

// ---------------------------------------------------------------- inverse trigonometric

// atan(t) for |t| <= tan(pi/12) = 0.268: Taylor series to t^29 (first omitted term < 2^-60).
static f64 math_atan_kernel(f64 t) {
    f64 t2 = t * t;
    f64 p = 1.0 / 29.0;
    p = 1.0 / 27.0 - t2 * p;
    p = 1.0 / 25.0 - t2 * p;
    p = 1.0 / 23.0 - t2 * p;
    p = 1.0 / 21.0 - t2 * p;
    p = 1.0 / 19.0 - t2 * p;
    p = 1.0 / 17.0 - t2 * p;
    p = 1.0 / 15.0 - t2 * p;
    p = 1.0 / 13.0 - t2 * p;
    p = 1.0 / 11.0 - t2 * p;
    p = 1.0 / 9.0 - t2 * p;
    p = 1.0 / 7.0 - t2 * p;
    p = 1.0 / 5.0 - t2 * p;
    p = 1.0 / 3.0 - t2 * p;
    return t - t * t2 * p;
}

// atan(t) for t >= 0 (t may be +Inf). Reduction: t > 1 uses pi/2 - atan(1/t); t > tan(pi/12)
// uses pi/6 + atan((t sqrt3 - 1) / (t + sqrt3)).
static f64 math_atan_pos(f64 t) {
    b32 invert = 0;
    if (t > 1.0) { t = 1.0 / t; invert = 1; }
    f64 base = 0.0;
    if (t > 0x1.126145e9ecd56p-2) {                      // tan(pi/12) = 2 - sqrt(3)
        const f64 sqrt3 = 0x1.bb67ae8584caap+0;
        t = (t * sqrt3 - 1.0) / (t + sqrt3);
        base = 0x1.0c152382d7366p-1;                     // pi/6
    }
    f64 a = base + math_atan_kernel(t);
    return invert ? MATH_PIO2 - a : a;
}

f32 bmf_atanf(f32 x) {
    if (x != x) return x;
    f32 out = (f32)math_atan_pos((f64)bmf_fabsf(x));
    return (bmf_f32_bits(x) >> 31) ? -out : out;
}

f32 bmf_atan2f(f32 y, f32 x) {
    if (x != x || y != y) return math_nan32();
    f64 ax = (f64)bmf_fabsf(x), ay = (f64)bmf_fabsf(y);
    f64 a;
    if (ay == 0.0) a = 0.0;
    else if (ax == 0.0) a = MATH_PIO2;
    else if (ax == ay) a = MATH_PIO4;                    // also both infinite
    else if (ax > ay) a = math_atan_pos(ay / ax);
    else a = MATH_PIO2 - math_atan_pos(ax / ay);
    if (bmf_f32_bits(x) >> 31) a = MATH_PI - a;          // x < 0 or x = -0
    f32 out = (f32)a;
    return (bmf_f32_bits(y) >> 31) ? -out : out;
}

// asin(x) = atan2(x, sqrt(1 - x^2)) and acos(x) = atan2(sqrt(1 - x^2), x). For an f32 x,
// x*x is exact in f64 and 1 - x*x is exact for |x| >= 1/2, so there is no cancellation near 1.
f32 bmf_asinf(f32 x) {
    f64 ax = (f64)bmf_fabsf(x);
    if (!(ax <= 1.0)) return math_nan32();
    f64 d = __builtin_sqrt(1.0 - ax * ax);
    f64 a = ax > d ? MATH_PIO2 - math_atan_pos(d / ax) : math_atan_pos(ax / d);
    f32 out = (f32)a;
    return (bmf_f32_bits(x) >> 31) ? -out : out;
}

f32 bmf_acosf(f32 x) {
    f64 ax = (f64)bmf_fabsf(x);
    if (!(ax <= 1.0)) return math_nan32();
    f64 d = __builtin_sqrt(1.0 - ax * ax);
    f64 a = ax > d ? math_atan_pos(d / ax) : MATH_PIO2 - math_atan_pos(ax / d);
    if (bmf_f32_bits(x) >> 31) a = MATH_PI - a;
    return (f32)a;
}

// ---------------------------------------------------------------- exponential and logarithm

// e^r for |r| <= ln2/2 + a little: Taylor series to r^13 (first omitted term < 2^-57).
static f64 math_exp_kernel(f64 r) {
    f64 p = 1.0 / 6227020800.0;                          // 1/13!
    p = p * r + 1.0 / 479001600.0;                       // 1/12!
    p = p * r + 1.0 / 39916800.0;
    p = p * r + 1.0 / 3628800.0;
    p = p * r + 1.0 / 362880.0;
    p = p * r + 1.0 / 40320.0;
    p = p * r + 1.0 / 5040.0;
    p = p * r + 1.0 / 720.0;
    p = p * r + 1.0 / 120.0;
    p = p * r + 1.0 / 24.0;
    p = p * r + 1.0 / 6.0;
    p = p * r + 0.5;
    p = p * r + 1.0;
    return p * r + 1.0;
}

// 2^w in f64 for w in [-160, 130]: k = nearest integer, w - k is exact, 2^(w-k) = e^((w-k) ln 2).
static f64 math_exp2_f64(f64 w) {
    f64 k = __builtin_floor(w + 0.5);
    f64 r = (w - k) * MATH_LN2;
    return math_exp_kernel(r) * math_pow2((i32)k);       // |k| <= 160: in range, 2^k is normal
}

f32 bmf_expf(f32 x) {
    if (x != x) return x;
    f64 v = (f64)x;
    if (v > 100.0) v = 100.0;                            // e^100 rounds to +Inf in f32
    if (v < -110.0) return 0.0f;                         // e^-110 is below half the smallest denormal
    f64 k = __builtin_floor(v * MATH_INV_LN2 + 0.5);
    f64 r = (v - k * MATH_LN2_HI) - k * MATH_LN2_LO;     // k * LN2_HI and the subtraction are exact
    return (f32)(math_exp_kernel(r) * math_pow2((i32)k));
}

f32 bmf_exp2f(f32 x) {
    if (x != x) return x;
    f64 v = (f64)x;
    if (v > 130.0) v = 130.0;
    if (v < -160.0) return 0.0f;
    return (f32)math_exp2_f64(v);
}

// For finite x > 0: writes e and returns log(m) with x = m * 2^e and m in [sqrt(1/2), sqrt(2)].
// log(m) = 2 atanh(s) with s = (m - 1)/(m + 1), |s| <= 0.1716: series to s^21 (omitted < 2^-56).
// m - 1 is exact, so the result has small relative error even next to x = 1.
static f64 math_log_reduced(f32 x, i32* exponent) {
    u32 bits = bmf_f32_bits(x);
    i32 e = 0;
    if (bits < 0x00800000u) {                            // denormal: scale by 2^23 (exact)
        bits = bmf_f32_bits(x * 8388608.0f);
        e = -23;
    }
    e += (i32)(bits >> 23) - 127;
    u32 m_bits = (bits & 0x007FFFFFu) | 0x3F800000u;     // m in [1, 2)
    if (m_bits > 0x3FB504F3u) { m_bits -= 0x00800000u; e += 1; }   // m > sqrt(2): use m/2
    f64 m = (f64)bmf_f32_from_bits(m_bits);
    f64 s = (m - 1.0) / (m + 1.0);
    f64 s2 = s * s;
    f64 p = 1.0 / 21.0;
    p = p * s2 + 1.0 / 19.0;
    p = p * s2 + 1.0 / 17.0;
    p = p * s2 + 1.0 / 15.0;
    p = p * s2 + 1.0 / 13.0;
    p = p * s2 + 1.0 / 11.0;
    p = p * s2 + 1.0 / 9.0;
    p = p * s2 + 1.0 / 7.0;
    p = p * s2 + 1.0 / 5.0;
    p = p * s2 + 1.0 / 3.0;
    *exponent = e;
    return 2.0 * s + 2.0 * s * s2 * p;
}

f32 bmf_logf(f32 x) {
    if (x != x) return x;
    if (x < 0.0f) return math_nan32();
    if (x == 0.0f) return -math_inf32();
    if (x == math_inf32()) return x;
    i32 e;
    f64 log_m = math_log_reduced(x, &e);
    return (f32)((f64)e * MATH_LN2 + log_m);             // |log_m| <= ln2/2: no cancellation
}

f32 bmf_log2f(f32 x) {
    if (x != x) return x;
    if (x < 0.0f) return math_nan32();
    if (x == 0.0f) return -math_inf32();
    if (x == math_inf32()) return x;
    i32 e;
    f64 log_m = math_log_reduced(x, &e);
    return (f32)((f64)e + log_m * MATH_INV_LN2);
}

f32 bmf_powf(f32 x, f32 y) {
    if (y == 0.0f || x == 1.0f) return 1.0f;
    if (x != x || y != y) return math_nan32();
    f32 ax = bmf_fabsf(x), ay = bmf_fabsf(y);
    f32 inf = math_inf32();
    if (ay == inf) {                                     // y = +-Inf
        if (ax == 1.0f) return 1.0f;
        return ((ax < 1.0f) == (y < 0.0f)) ? inf : 0.0f;
    }
    b32 y_integer = ay >= 16777216.0f || bmf_truncf(y) == y;
    b32 y_odd = ay < 16777216.0f && y_integer && (bmf_f32_to_i32(ay) & 1);
    b32 x_negative = bmf_f32_bits(x) >> 31;
    if (ax == 0.0f) { f32 r = y < 0.0f ? inf : 0.0f; return (x_negative && y_odd) ? -r : r; }
    if (ax == inf)  { f32 r = y < 0.0f ? 0.0f : inf; return (x_negative && y_odd) ? -r : r; }
    if (x_negative && !y_integer) return math_nan32();
    b32 negate = x_negative && y_odd;
    i32 e;
    f64 log_m = math_log_reduced(ax, &e);
    f64 w = (f64)y * ((f64)e + log_m * MATH_INV_LN2);   // y * log2|x|, absolute error < 2^-44
    f32 out;
    if (w > 130.0) out = inf;
    else if (w < -160.0) out = 0.0f;
    else out = (f32)math_exp2_f64(w);
    return negate ? -out : out;
}

// ---------------------------------------------------------------- fmod, hypot

// Exact: with x = mx * 2^ex and y = my * 2^ey (integer mantissas), the remainder is
// (mx * 2^(ex-ey) mod my) * 2^ey, computed with integer arithmetic 32 doublings at a time.
f32 bmf_fmodf(f32 x, f32 y) {
    u32 ux = bmf_f32_bits(x), uy = bmf_f32_bits(y);
    u32 sign = ux & 0x80000000u;
    ux &= 0x7FFFFFFFu;
    uy &= 0x7FFFFFFFu;
    if (uy == 0 || ux >= 0x7F800000u || uy > 0x7F800000u) return math_nan32();
    if (ux < uy) return x;                               // |x| < |y| (includes y = Inf)
    if (ux == uy) return bmf_f32_from_bits(sign);
    i32 ex = (i32)(ux >> 23), ey = (i32)(uy >> 23);
    u64 mx = ux & 0x007FFFFFu, my = uy & 0x007FFFFFu;
    if (ex) mx |= 0x00800000u; else ex = 1;              // value = mantissa * 2^(exponent - 150)
    if (ey) my |= 0x00800000u; else ey = 1;
    u64 r = mx % my;
    for (i32 d = ex - ey; d > 0;) {
        i32 step = d < 32 ? d : 32;                      // r < 2^24, so r << 32 fits in u64
        r = (r << step) % my;
        d -= step;
    }
    if (r == 0) return bmf_f32_from_bits(sign);
    while (r < 0x00800000u && ey > 1) { r <<= 1; ey--; }
    u32 out = r >= 0x00800000u ? ((u32)ey << 23) | ((u32)r & 0x007FFFFFu) : (u32)r;
    return bmf_f32_from_bits(out | sign);
}

// In f64 the squares of any f32 neither overflow nor underflow, and x*x is exact.
f32 bmf_hypotf(f32 x, f32 y) {
    f32 ax = bmf_fabsf(x), ay = bmf_fabsf(y);
    if (ax == math_inf32() || ay == math_inf32()) return math_inf32();
    if (ax != ax || ay != ay) return math_nan32();
    f64 a = (f64)ax, b = (f64)ay;
    return (f32)__builtin_sqrt(a * a + b * b);
}
