// Tests for src/base/base_math.* (task 1A-04a) and bmf_rng_normal.
//
// ULP tests: for every transcendental function, 100 000 deterministic samples (edge cases, then
// a mix of typical ranges, random bit patterns over the whole domain and hard cases) are compared
// against the double-double reference in tests/gen/math_ref.c. The bound is max error < 1 ULP;
// the note shows the measured max (in millionths of a ULP), how many results were not the
// correctly rounded one, and the output hash.
//
// Sampling draws at most one random number per full expression: C leaves the evaluation order
// of function arguments and operands unspecified, and clang really does evaluate them in
// different orders on win64 and wasm32.
//
// Output hashes: a hash of the output bits of every sample, compared against the constant below.
// The same samples must give the same hash on every target (win64 now, wasm once M9 runs the
// tests in the browser). A change to base_math.c that changes any output bit is a determinism
// event: it changes every golden replay that uses the function. Update a constant only on purpose,
// and state it in the task summary (R5).

#include "gen/math_ref.c"

enum {
    MF_SIN, MF_COS, MF_TAN, MF_ASIN, MF_ACOS, MF_ATAN, MF_ATAN2,
    MF_EXP, MF_EXP2, MF_LOG, MF_LOG2, MF_POW, MF_HYPOT, MF_COUNT
};
static const char* MATH_NAMES[MF_COUNT] = {
    "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "exp", "exp2", "log", "log2", "pow", "hypot"
};
// Recorded 2026-11-19 (1A-04a); identical at -O0 + UBSan, -O2 + LTO and -march=x86-64-v3.
static const u64 MATH_OUTPUT_HASH[MF_COUNT] = {
    0x32256037f78e9dfbull, 0x61745942a2a80641ull, 0xdebc498d97cde0f0ull,   // sin, cos, tan
    0x612036137aa8114aull, 0x8a0abfcc2e361924ull, 0xbcfcbbdde38b6181ull,   // asin, acos, atan
    0xc06d931bfb0c52eaull, 0xd32255304301ce7full, 0x9d0920ce098acc4aull,   // atan2, exp, exp2
    0x0332c8cdab272435ull, 0x91f1b7d061616d1bull, 0x654e3cbcd3b10537ull,   // log, log2, pow
    0xd8478e879d3e56f5ull,                                                  // hypot
};
enum { MATH_SAMPLES = 100000 };

// ---------------------------------------------------------------- sampling
static f32 math_uniform(bmf_rng* r, f32 lo, f32 hi) { return lo + (hi - lo) * bmf_rng_f32(r); }

// A random finite f32 with |x| < 2^(max_exponent - 127 + 1) (exponent field <= max_exponent),
// random sign; the magnitude is log-uniform and includes denormals.
static f32 math_random_bits(bmf_rng* r, u32 max_exponent) {
    u32 u = bmf_rng_next_u32(r);
    u32 exponent = ((u >> 23) & 0xFFu) % (max_exponent + 1);
    return bmf_f32_from_bits((u & 0x807FFFFFu) | (exponent << 23));
}

// x moved by `steps` ULPs (signed) through the bit pattern.
static f32 math_nudge(f32 x, i32 steps) { return bmf_f32_from_bits((u32)((i32)bmf_f32_bits(x) + steps)); }

static void math_sample(i32 fn, bmf_rng* r, i32 i, f32* x, f32* y) {
    static const f32 trig_edges[] = { 0.0f, -0.0f, 1.4e-45f, 1e-30f, 0.785398185f, -0.785398185f, 1.57079637f, 3.14159274f,
                                      6.28318548f, 0.5f, 1.0f, 2.0f, 100.0f, 1000.0f, 131071.99f, 131072.0f, 131072.02f,
                                      -131072.02f, 1e10f, 3.40282347e38f, -3.40282347e38f, 1e30f };
    static const f32 asin_edges[] = { 0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 1.4e-45f, 0.99999994f, -0.99999994f, 0.70710677f, 1e-20f };
    static const f32 atan_edges[] = { 0.0f, -0.0f, 1.0f, -1.0f, 1.4e-45f, 3.40282347e38f, -3.40282347e38f, 0.267949194f, 1e-20f, 1e20f };
    static const f32 exp_edges[]  = { 0.0f, -0.0f, 1.0f, -1.0f, 88.7228317f, -87.3365479f, -103.0f, 1.4e-45f, 1e-10f, 0.346573591f, -103.97f };
    static const f32 exp2_edges[] = { 0.0f, 1.0f, -1.0f, 10.0f, -149.0f, 127.0f, 127.99999f, -126.5f, 0.5f, -149.5f, -148.9f };
    static const f32 log_edges[]  = { 1.0f, 2.0f, 0.5f, 1.4e-45f, 3.40282347e38f, 0.99999994f, 1.00000012f, 1.41421354f, 0.70710677f,
                                      2.71828175f, 1.17549435e-38f, 8.0f };
    static const f32 pow_edges[][2] = { { 2.0f, 10.0f }, { 4.0f, 0.5f }, { 9.0f, 0.5f }, { 2.0f, -149.0f }, { 2.0f, 127.0f },
                                        { 10.0f, 38.0f }, { -2.0f, 3.0f }, { -2.0f, -3.0f }, { -8.0f, 2.0f }, { 0.5f, 3.0f },
                                        { 1.00000012f, 1e7f }, { 3.40282347e38f, 1.0f }, { 1.4e-45f, 1.0f }, { 0.1f, 2.0f } };
    static const f32 two_edges[][2] = { { 0.0f, 0.0f }, { 0.0f, -0.0f }, { -0.0f, -0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f },
                                        { 0.0f, -1.0f }, { 1.0f, -1.0f }, { -1.0f, -1.0f }, { 1e-30f, -1.0f }, { 1.0f, 1e-30f },
                                        { 3.0f, 4.0f }, { 1.4e-45f, 1.4e-45f }, { 3.40282347e38f, 1.0f }, { -3.0f, -4.0f } };
    *y = 0.0f;
    i32 n_edges = 0;
    const f32* edges = 0;
    switch (fn) {
    case MF_SIN: case MF_COS: case MF_TAN: edges = trig_edges; n_edges = (i32)(sizeof trig_edges / 4); break;
    case MF_ASIN: case MF_ACOS: edges = asin_edges; n_edges = (i32)(sizeof asin_edges / 4); break;
    case MF_ATAN: edges = atan_edges; n_edges = (i32)(sizeof atan_edges / 4); break;
    case MF_EXP: edges = exp_edges; n_edges = (i32)(sizeof exp_edges / 4); break;
    case MF_EXP2: edges = exp2_edges; n_edges = (i32)(sizeof exp2_edges / 4); break;
    case MF_LOG: case MF_LOG2: edges = log_edges; n_edges = (i32)(sizeof log_edges / 4); break;
    case MF_POW: if (i < (i32)(sizeof pow_edges / 8)) { *x = pow_edges[i][0]; *y = pow_edges[i][1]; return; } break;
    case MF_ATAN2: case MF_HYPOT: if (i < (i32)(sizeof two_edges / 8)) { *x = two_edges[i][0]; *y = two_edges[i][1]; return; } break;
    }
    if (i < n_edges) { *x = edges[i]; return; }
    i32 kind = i & 3;
    switch (fn) {
    case MF_SIN: case MF_COS: case MF_TAN:
        if (kind == 0) *x = math_uniform(r, -10.0f, 10.0f);
        else if (kind == 1) *x = math_random_bits(r, 254);
        else if (kind == 2) {   // next to a multiple of pi/2: the hardest reductions
            i32 k = bmf_rng_range_i32(r, 1, 1 << 22);
            *x = math_nudge((f32)((f64)k * 1.5707963267948966), bmf_rng_range_i32(r, -3, 3));
        } else *x = math_uniform(r, -100000.0f, 100000.0f);
        break;
    case MF_ASIN: case MF_ACOS:
        if (kind <= 1) *x = math_uniform(r, -1.0f, 1.0f);
        else if (kind == 2) *x = math_random_bits(r, 126);
        else { *x = math_nudge(1.0f, -bmf_rng_range_i32(r, 1, 1000)); if (bmf_rng_next_u32(r) & 1) *x = -*x; }
        break;
    case MF_ATAN:
        *x = kind <= 1 ? math_uniform(r, -10.0f, 10.0f) : math_random_bits(r, 254);
        break;
    case MF_ATAN2:
        if (kind <= 1) { *y = math_uniform(r, -10.0f, 10.0f); *x = math_uniform(r, -10.0f, 10.0f); }
        else if (kind == 2) { *y = math_random_bits(r, 254); *x = math_random_bits(r, 254); }
        else { *y = math_uniform(r, -1.0f, 1.0f); *x = math_random_bits(r, 140); }
        break;
    case MF_EXP:
        if (kind <= 1) *x = math_uniform(r, -103.9f, 88.72f);
        else if (kind == 2) { do *x = math_random_bits(r, 133); while (bmf_fabsf(*x) > 88.72f); }
        else *x = math_uniform(r, -1.0f, 1.0f);
        break;
    case MF_EXP2:
        if (kind <= 1) *x = math_uniform(r, -149.9f, 127.99f);
        else if (kind == 2) { do *x = math_random_bits(r, 133); while (*x > 127.99f || *x < -149.9f); }
        else { f32 whole = (f32)bmf_rng_range_i32(r, -149, 127); *x = whole + (f32)bmf_rng_range_i32(r, -1, 1) * 0.5f; }
        break;
    case MF_LOG: case MF_LOG2:
        if (kind <= 1) *x = bmf_fabsf(math_random_bits(r, 254));
        else if (kind == 2) *x = math_uniform(r, 0.5f, 2.0f);
        else *x = math_nudge(1.0f, bmf_rng_range_i32(r, -2000, 2000));
        if (*x == 0.0f) *x = 1.0f;
        break;
    case MF_POW:
        if (kind == 0) {   // random x, y chosen so that y * log2(x) covers the whole f32 range
            do *x = bmf_fabsf(math_random_bits(r, 254)); while (*x == 0.0f || *x == 1.0f);
            f32 w = math_uniform(r, -149.0f, 127.9f);
            *y = (f32)((f64)w / (f64)bmf_log2f(*x));
        } else if (kind == 1) { *x = math_uniform(r, 0.0f, 4.0f); *y = math_uniform(r, -30.0f, 30.0f); if (*x == 0.0f) *x = 1.0f; }
        else if (kind == 2) { *x = math_uniform(r, -4.0f, -0.1f); *y = (f32)bmf_rng_range_i32(r, -20, 20); }
        else { *x = math_nudge(1.0f, bmf_rng_range_i32(r, -1000, 1000)); *y = math_uniform(r, -1e6f, 1e6f); }
        if (fn == MF_POW && *x != 0.0f) {   // keep |result| inside the f32 range (overflow is a special-value test)
            f64 w = (f64)*y * (f64)bmf_log2f(bmf_fabsf(*x));
            if (w > 127.9 || w < -149.0) *y = 1.0f;
        }
        break;
    case MF_HYPOT:
        if (kind <= 1) { *x = math_uniform(r, -100.0f, 100.0f); *y = math_uniform(r, -100.0f, 100.0f); }
        else if (kind == 2) { *x = math_random_bits(r, 253); *y = math_random_bits(r, 253); }
        else { *x = math_random_bits(r, 254); *y = math_uniform(r, -1.0f, 1.0f); }
        break;
    }
}

static f32 math_eval(i32 fn, f32 x, f32 y) {
    switch (fn) {
    case MF_SIN: return bmf_sinf(x);
    case MF_COS: return bmf_cosf(x);
    case MF_TAN: return bmf_tanf(x);
    case MF_ASIN: return bmf_asinf(x);
    case MF_ACOS: return bmf_acosf(x);
    case MF_ATAN: return bmf_atanf(x);
    case MF_ATAN2: return bmf_atan2f(x, y);
    case MF_EXP: return bmf_expf(x);
    case MF_EXP2: return bmf_exp2f(x);
    case MF_LOG: return bmf_logf(x);
    case MF_LOG2: return bmf_log2f(x);
    case MF_POW: return bmf_powf(x, y);
    case MF_HYPOT: return bmf_hypotf(x, y);
    }
    return 0.0f;
}

static ref_dd math_reference(i32 fn, f32 x, f32 y) {
    switch (fn) {
    case MF_SIN: return ref_sin(x);
    case MF_COS: return ref_cos(x);
    case MF_TAN: return ref_tan(x);
    case MF_ASIN: return ref_asin(x);
    case MF_ACOS: return ref_acos(x);
    case MF_ATAN: return ref_atan(x);
    case MF_ATAN2: return ref_atan2(x, y);
    case MF_EXP: return ref_expf(x);
    case MF_EXP2: return ref_exp2f(x);
    case MF_LOG: return ref_logf(x);
    case MF_LOG2: return ref_log2f(x);
    case MF_POW: return ref_powf(x, y);
    case MF_HYPOT: return ref_hypot(x, y);
    }
    return ref_dd_make(0.0);
}

static void math_ulp_run(bmf_test* t, i32 fn) {
    ref_init();
    bmf_rng r;
    bmf_rng_seed(&r, 0x6D617468u + (u64)fn, (u64)fn);
    bmf_hasher h;
    bmf_hasher_init(&h, 0);
    f64 max_error = 0.0;
    f32 worst_x = 0.0f, worst_y = 0.0f;
    i32 misrounded = 0;
    for (i32 i = 0; i < MATH_SAMPLES; i++) {
        f32 x, y;
        math_sample(fn, &r, i, &x, &y);
        f32 got = math_eval(fn, x, y);
        u32 bits = bmf_f32_bits(got);
        bmf_hasher_update(&h, &bits, 4);
        f64 err = ref_ulp_error(got, math_reference(fn, x, y));
        if (err > 0.5) misrounded++;
        if (err > max_error) { max_error = err; worst_x = x; worst_y = y; }
    }
    u64 hash = bmf_hasher_final(&h);
    char note[512];
    bmf_fmt_buf(note, (i32)sizeof note, "%s max_ulp=%.6f misrounded=%i/%i worst_x=%f worst_y=%f hash=0x%016lx",
                MATH_NAMES[fn], max_error < 1e9 ? max_error : 1e9, misrounded, (i32)MATH_SAMPLES, worst_x, worst_y, hash);
    bmf_test_note(t, note);
    bmf_check(t, max_error < 1.0, __FILE__, __LINE__, "max error must be below 1 ULP (see note)");
    bmf_check_eq_u64(t, hash, MATH_OUTPUT_HASH[fn], __FILE__, __LINE__, "output hash (determinism golden)");
}

static void test_math_ulp_sin(bmf_test* t)   { math_ulp_run(t, MF_SIN); }
static void test_math_ulp_cos(bmf_test* t)   { math_ulp_run(t, MF_COS); }
static void test_math_ulp_tan(bmf_test* t)   { math_ulp_run(t, MF_TAN); }
static void test_math_ulp_asin(bmf_test* t)  { math_ulp_run(t, MF_ASIN); }
static void test_math_ulp_acos(bmf_test* t)  { math_ulp_run(t, MF_ACOS); }
static void test_math_ulp_atan(bmf_test* t)  { math_ulp_run(t, MF_ATAN); }
static void test_math_ulp_atan2(bmf_test* t) { math_ulp_run(t, MF_ATAN2); }
static void test_math_ulp_exp(bmf_test* t)   { math_ulp_run(t, MF_EXP); }
static void test_math_ulp_exp2(bmf_test* t)  { math_ulp_run(t, MF_EXP2); }
static void test_math_ulp_log(bmf_test* t)   { math_ulp_run(t, MF_LOG); }
static void test_math_ulp_log2(bmf_test* t)  { math_ulp_run(t, MF_LOG2); }
static void test_math_ulp_pow(bmf_test* t)   { math_ulp_run(t, MF_POW); }
static void test_math_ulp_hypot(bmf_test* t) { math_ulp_run(t, MF_HYPOT); }

// ---------------------------------------------------------------- constants
// The 2/pi table and the f64 constants of base_math.c, recomputed from big-integer pi and ln 2.
static void test_math_constants(bmf_test* t) {
    ref_init();
    i32 bad_words = 0;
    for (i32 j = 0; j < 10; j++) if (REF_BIG_TWO_OVER_PI.limb[1 + j] != MATH_TWO_OVER_PI_BITS[j]) bad_words++;
    bmf_check_eq_i64(t, bad_words, 0, __FILE__, __LINE__, "2/pi table words differ from the big-integer 2/pi");
    bmf_check_eq_u64(t, REF_BIG_PI.limb[0], 3, __FILE__, __LINE__, "integer part of pi");
    bmf_check_eq_u64(t, REF_BIG_PI.limb[1], 0x243F6A88u, __FILE__, __LINE__, "first fraction word of pi");
    bmf_check_eq_u64(t, REF_BIG_LN2.limb[1], 0xB17217F7u, __FILE__, __LINE__, "first fraction word of ln 2");
    bmf_check_eq_u64(t, bmf_f64_bits(MATH_PI), bmf_f64_bits(REF_PI.hi), __FILE__, __LINE__, "MATH_PI");
    bmf_check_eq_u64(t, bmf_f64_bits(MATH_PIO2), bmf_f64_bits(REF_PIO2.hi), __FILE__, __LINE__, "MATH_PIO2");
    bmf_check_eq_u64(t, bmf_f64_bits(MATH_PIO4), bmf_f64_bits(ref_scale(REF_PI, 0.25).hi), __FILE__, __LINE__, "MATH_PIO4");
    bmf_check_eq_u64(t, bmf_f64_bits(MATH_LN2), bmf_f64_bits(REF_LN2.hi), __FILE__, __LINE__, "MATH_LN2");
    bmf_check_eq_u64(t, bmf_f64_bits(MATH_INV_LN2), bmf_f64_bits(ref_div(ref_dd_make(1.0), REF_LN2).hi), __FILE__, __LINE__, "MATH_INV_LN2");
    bmf_check_eq_u64(t, bmf_f64_bits(MATH_INV_PIO2), bmf_f64_bits(ref_big_to_dd(&REF_BIG_TWO_OVER_PI).hi), __FILE__, __LINE__, "MATH_INV_PIO2");
    // Split constants: the parts must add up to the true value with the stated precision.
    // Computed in big integers: double-double has too few bits for the 2^-122 tail.
    ref_big pio2_rest = REF_BIG_PI, part;
    ref_big_div_small(&pio2_rest, 2);
    part = ref_big_from_f64(MATH_PIO2_1);
    ref_big_sub(&pio2_rest, &part);
    part = ref_big_from_f64(MATH_PIO2_2);
    ref_big_sub(&pio2_rest, &part);
    bmf_check_eq_u64(t, bmf_f64_bits(ref_big_small_to_f64(&pio2_rest)), bmf_f64_bits(MATH_PIO2_2T), __FILE__, __LINE__, "pi/2 - PIO2_1 - PIO2_2 = PIO2_2T");
    bmf_check(t, (bmf_f64_bits(MATH_PIO2_1) & 0xFFFFFull) == 0 && (bmf_f64_bits(MATH_PIO2_2) & 0xFFFFFull) == 0, __FILE__, __LINE__, "PIO2_1 and PIO2_2 have at most 33 bits");
    bmf_check(t, ref_abs64(MATH_PIO2_2) < 0x1p-33, __FILE__, __LINE__, "PIO2_1 is pi/2 truncated to 33 bits");
    ref_big ln2_rest = REF_BIG_LN2;
    part = ref_big_from_f64(MATH_LN2_HI);
    ref_big_sub(&ln2_rest, &part);
    bmf_check_eq_u64(t, bmf_f64_bits(ref_big_small_to_f64(&ln2_rest)), bmf_f64_bits(MATH_LN2_LO), __FILE__, __LINE__, "ln2 - LN2_HI = LN2_LO");
    bmf_check(t, (bmf_f64_bits(MATH_LN2_HI) & 0x1FFFFFull) == 0, __FILE__, __LINE__, "LN2_HI has at most 32 bits");
}

// The Cody-Waite and Payne-Hanek reductions agree wherever both apply (x in [0.8, 2^17)).
static void test_math_reduction_paths(bmf_test* t) {
    bmf_rng r;
    bmf_rng_seed(&r, 99, 1);
    i32 bad = 0;
    for (i32 i = 0; i < 100000; i++) {
        f32 x;
        if (i & 1) x = math_uniform(&r, 0.8f, 131071.0f);
        else { f64 k = (f64)bmf_rng_range_i32(&r, 1, 83000); x = math_nudge((f32)(k * 1.5707963267948966), bmf_rng_range_i32(&r, -2, 2)); }
        if (!(x >= 0.8f && x < 131072.0f)) continue;
        i32 qa, qb;
        f64 ra = math_reduce_pio2(x, &qa);
        f64 rb = math_reduce_large(x, &qb);
        f64 sa = (qa & 1) ? math_cos_kernel(ra) : math_sin_kernel(ra);
        f64 sb = (qb & 1) ? math_cos_kernel(rb) : math_sin_kernel(rb);
        if (qa & 2) sa = -sa;
        if (qb & 2) sb = -sb;
        if (ref_abs64(sa - sb) > 0x1p-48 * (ref_abs64(sa) + 0x1p-30)) bad++;
    }
    bmf_check_eq_i64(t, bad, 0, __FILE__, __LINE__, "medium and large reductions disagree");
}

// sincos returns exactly sin and cos.
static void test_math_sincos_matches(bmf_test* t) {
    bmf_rng r;
    bmf_rng_seed(&r, 5, 5);
    i32 bad = 0;
    for (i32 i = 0; i < 100000; i++) {
        f32 x = (i & 1) ? math_uniform(&r, -20.0f, 20.0f) : math_random_bits(&r, 254);
        f32 s, c;
        bmf_sincosf(x, &s, &c);
        if (bmf_f32_bits(s) != bmf_f32_bits(bmf_sinf(x)) || bmf_f32_bits(c) != bmf_f32_bits(bmf_cosf(x))) bad++;
    }
    bmf_check_eq_i64(t, bad, 0, __FILE__, __LINE__, "sincos differs from sin/cos");
}

// ---------------------------------------------------------------- exact operations
static void test_math_exact_ops(bmf_test* t) {
    bmf_rng r;
    bmf_rng_seed(&r, 17, 3);
    i32 bad[8] = { 0 };
    for (i32 i = 0; i < 100000; i++) {
        f32 x;
        i32 kind = i & 3;
        if (kind == 0) x = math_random_bits(&r, 254);
        else if (kind == 1) x = math_uniform(&r, -10.0f, 10.0f);
        else if (kind == 2) x = (f32)bmf_rng_range_i32(&r, -100000, 100000) * 0.5f;     // exact halves
        else { f32 half = (f32)bmf_rng_range_i32(&r, -1000, 1000) + 0.5f; x = math_nudge(half, bmf_rng_range_i32(&r, -2, 2)); }
        if (bmf_f32_bits(bmf_floorf(x)) != bmf_f32_bits(ref_integral(x, 0))) bad[0]++;
        if (bmf_f32_bits(bmf_ceilf(x)) != bmf_f32_bits(ref_integral(x, 1))) bad[1]++;
        if (bmf_f32_bits(bmf_truncf(x)) != bmf_f32_bits(ref_integral(x, 2))) bad[2]++;
        if (bmf_f32_bits(bmf_roundf(x)) != bmf_f32_bits(ref_integral(x, 3))) bad[3]++;
        f32 ax = bmf_fabsf(x);
        if (bmf_f32_bits(ax) != (bmf_f32_bits(x) & 0x7FFFFFFFu)) bad[4]++;
        if (bmf_f32_bits(bmf_sqrtf(ax)) != bmf_f32_bits((f32)__builtin_sqrt((f64)ax))) bad[5]++;
        f32 y = (i & 4) ? math_random_bits(&r, 254) : math_uniform(&r, -3.0f, 3.0f);
        if (y != 0.0f && bmf_f32_bits(bmf_fmodf(x, y)) != bmf_f32_bits(ref_fmod(x, y))) bad[6]++;
        i32 to_int = bmf_f32_to_i32(x);
        i32 want = x >= 2147483648.0f ? 2147483647 : x <= -2147483648.0f ? -2147483647 - 1 : (i32)(i64)x;
        if (to_int != want) bad[7]++;
    }
    const char* names[8] = { "floor", "ceil", "trunc", "round", "fabs", "sqrt", "fmod", "f32_to_i32" };
    for (i32 k = 0; k < 8; k++) bmf_check_eq_i64(t, bad[k], 0, __FILE__, __LINE__, names[k]);
}

// ---------------------------------------------------------------- special values
static void math_expect(bmf_test* t, f32 got, f32 want, i32 line, const char* what) {
    b32 ok = (want != want) ? (got != got) : bmf_f32_bits(got) == bmf_f32_bits(want);
    if (ok) { bmf_check(t, 1, __FILE__, line, what); return; }
    char buf[256];
    bmf_fmt_buf(buf, (i32)sizeof buf, "%s: expected %f (bits %08x), got %f (bits %08x)", what, want, bmf_f32_bits(want), got, bmf_f32_bits(got));
    bmf_check(t, 0, __FILE__, line, buf);
}

static void test_math_special_values(bmf_test* t) {
    f32 inf = bmf_f32_from_bits(0x7F800000u), nan = bmf_f32_from_bits(0x7FC00000u);
    f32 pi = 3.14159274f, half_pi = 1.57079637f, quarter_pi = 0.785398185f;
    f32 max = BMF_F32_MAX, tiny = bmf_f32_from_bits(1);
    math_expect(t, bmf_sinf(-0.0f), -0.0f, __LINE__, "sin(-0)");
    math_expect(t, bmf_sinf(0.0f), 0.0f, __LINE__, "sin(0)");
    math_expect(t, bmf_cosf(-0.0f), 1.0f, __LINE__, "cos(-0)");
    math_expect(t, bmf_tanf(-0.0f), -0.0f, __LINE__, "tan(-0)");
    math_expect(t, bmf_sinf(tiny), tiny, __LINE__, "sin(denormal)");
    math_expect(t, bmf_sinf(inf), nan, __LINE__, "sin(Inf)");
    math_expect(t, bmf_cosf(-inf), nan, __LINE__, "cos(-Inf)");
    math_expect(t, bmf_tanf(nan), nan, __LINE__, "tan(NaN)");
    math_expect(t, bmf_asinf(1.0f), half_pi, __LINE__, "asin(1)");
    math_expect(t, bmf_asinf(-0.0f), -0.0f, __LINE__, "asin(-0)");
    math_expect(t, bmf_asinf(1.00000012f), nan, __LINE__, "asin(>1)");
    math_expect(t, bmf_acosf(1.0f), 0.0f, __LINE__, "acos(1)");
    math_expect(t, bmf_acosf(-1.0f), pi, __LINE__, "acos(-1)");
    math_expect(t, bmf_acosf(-0.0f), half_pi, __LINE__, "acos(-0)");
    math_expect(t, bmf_acosf(-2.0f), nan, __LINE__, "acos(-2)");
    math_expect(t, bmf_atanf(-0.0f), -0.0f, __LINE__, "atan(-0)");
    math_expect(t, bmf_atanf(inf), half_pi, __LINE__, "atan(Inf)");
    math_expect(t, bmf_atanf(-inf), -half_pi, __LINE__, "atan(-Inf)");
    math_expect(t, bmf_atan2f(0.0f, 0.0f), 0.0f, __LINE__, "atan2(0, 0)");
    math_expect(t, bmf_atan2f(-0.0f, 0.0f), -0.0f, __LINE__, "atan2(-0, 0)");
    math_expect(t, bmf_atan2f(0.0f, -0.0f), pi, __LINE__, "atan2(0, -0)");
    math_expect(t, bmf_atan2f(-0.0f, -0.0f), -pi, __LINE__, "atan2(-0, -0)");
    math_expect(t, bmf_atan2f(0.0f, -1.0f), pi, __LINE__, "atan2(0, -1)");
    math_expect(t, bmf_atan2f(1.0f, 0.0f), half_pi, __LINE__, "atan2(1, 0)");
    math_expect(t, bmf_atan2f(-1.0f, -0.0f), -half_pi, __LINE__, "atan2(-1, -0)");
    math_expect(t, bmf_atan2f(1.0f, 1.0f), quarter_pi, __LINE__, "atan2(1, 1)");
    math_expect(t, bmf_atan2f(inf, inf), quarter_pi, __LINE__, "atan2(Inf, Inf)");
    math_expect(t, bmf_atan2f(inf, -inf), 2.3561945f, __LINE__, "atan2(Inf, -Inf)");
    math_expect(t, bmf_atan2f(1.0f, inf), 0.0f, __LINE__, "atan2(1, Inf)");
    math_expect(t, bmf_atan2f(nan, 1.0f), nan, __LINE__, "atan2(NaN, 1)");
    math_expect(t, bmf_expf(0.0f), 1.0f, __LINE__, "exp(0)");
    math_expect(t, bmf_expf(-inf), 0.0f, __LINE__, "exp(-Inf)");
    math_expect(t, bmf_expf(inf), inf, __LINE__, "exp(Inf)");
    math_expect(t, bmf_expf(89.0f), inf, __LINE__, "exp(89)");
    math_expect(t, bmf_expf(-200.0f), 0.0f, __LINE__, "exp(-200)");
    math_expect(t, bmf_exp2f(10.0f), 1024.0f, __LINE__, "exp2(10)");
    math_expect(t, bmf_exp2f(-149.0f), tiny, __LINE__, "exp2(-149)");
    math_expect(t, bmf_exp2f(-150.0f), 0.0f, __LINE__, "exp2(-150) (tie rounds to even 0)");
    math_expect(t, bmf_exp2f(128.0f), inf, __LINE__, "exp2(128)");
    math_expect(t, bmf_exp2f(-126.0f), 1.17549435e-38f, __LINE__, "exp2(-126)");
    math_expect(t, bmf_logf(1.0f), 0.0f, __LINE__, "log(1)");
    math_expect(t, bmf_logf(0.0f), -inf, __LINE__, "log(0)");
    math_expect(t, bmf_logf(-0.0f), -inf, __LINE__, "log(-0)");
    math_expect(t, bmf_logf(-1.0f), nan, __LINE__, "log(-1)");
    math_expect(t, bmf_logf(inf), inf, __LINE__, "log(Inf)");
    math_expect(t, bmf_log2f(8.0f), 3.0f, __LINE__, "log2(8)");
    math_expect(t, bmf_log2f(0.5f), -1.0f, __LINE__, "log2(0.5)");
    math_expect(t, bmf_log2f(tiny), -149.0f, __LINE__, "log2(2^-149)");
    math_expect(t, bmf_log2f(max), 128.0f, __LINE__, "log2(FLT_MAX) rounds to 128");
    math_expect(t, bmf_powf(nan, 0.0f), 1.0f, __LINE__, "pow(NaN, 0)");
    math_expect(t, bmf_powf(1.0f, nan), 1.0f, __LINE__, "pow(1, NaN)");
    math_expect(t, bmf_powf(2.0f, nan), nan, __LINE__, "pow(2, NaN)");
    math_expect(t, bmf_powf(2.0f, 10.0f), 1024.0f, __LINE__, "pow(2, 10)");
    math_expect(t, bmf_powf(4.0f, 0.5f), 2.0f, __LINE__, "pow(4, 0.5)");
    math_expect(t, bmf_powf(9.0f, 0.5f), 3.0f, __LINE__, "pow(9, 0.5)");
    math_expect(t, bmf_powf(-2.0f, 3.0f), -8.0f, __LINE__, "pow(-2, 3)");
    math_expect(t, bmf_powf(-2.0f, 2.0f), 4.0f, __LINE__, "pow(-2, 2)");
    math_expect(t, bmf_powf(-2.0f, 0.5f), nan, __LINE__, "pow(-2, 0.5)");
    math_expect(t, bmf_powf(0.0f, -1.0f), inf, __LINE__, "pow(0, -1)");
    math_expect(t, bmf_powf(-0.0f, -1.0f), -inf, __LINE__, "pow(-0, -1)");
    math_expect(t, bmf_powf(-0.0f, -2.0f), inf, __LINE__, "pow(-0, -2)");
    math_expect(t, bmf_powf(-0.0f, 3.0f), -0.0f, __LINE__, "pow(-0, 3)");
    math_expect(t, bmf_powf(-0.0f, 0.5f), 0.0f, __LINE__, "pow(-0, 0.5)");
    math_expect(t, bmf_powf(-1.0f, inf), 1.0f, __LINE__, "pow(-1, Inf)");
    math_expect(t, bmf_powf(0.5f, inf), 0.0f, __LINE__, "pow(0.5, Inf)");
    math_expect(t, bmf_powf(0.5f, -inf), inf, __LINE__, "pow(0.5, -Inf)");
    math_expect(t, bmf_powf(2.0f, -inf), 0.0f, __LINE__, "pow(2, -Inf)");
    math_expect(t, bmf_powf(-inf, 3.0f), -inf, __LINE__, "pow(-Inf, 3)");
    math_expect(t, bmf_powf(-inf, -3.0f), -0.0f, __LINE__, "pow(-Inf, -3)");
    math_expect(t, bmf_powf(inf, -0.5f), 0.0f, __LINE__, "pow(Inf, -0.5)");
    math_expect(t, bmf_powf(10.0f, 39.0f), inf, __LINE__, "pow(10, 39) overflows");
    math_expect(t, bmf_powf(-10.0f, 39.0f), -inf, __LINE__, "pow(-10, 39) overflows");
    math_expect(t, bmf_powf(10.0f, -50.0f), 0.0f, __LINE__, "pow(10, -50) underflows");
    math_expect(t, bmf_powf(-2.0f, 16777217.0f), inf, __LINE__, "pow(-2, 2^24) (even)");
    math_expect(t, bmf_fmodf(5.5f, 2.0f), 1.5f, __LINE__, "fmod(5.5, 2)");
    math_expect(t, bmf_fmodf(-5.5f, 2.0f), -1.5f, __LINE__, "fmod(-5.5, 2)");
    math_expect(t, bmf_fmodf(5.5f, -2.0f), 1.5f, __LINE__, "fmod(5.5, -2)");
    math_expect(t, bmf_fmodf(1.0f, inf), 1.0f, __LINE__, "fmod(1, Inf)");
    math_expect(t, bmf_fmodf(inf, 1.0f), nan, __LINE__, "fmod(Inf, 1)");
    math_expect(t, bmf_fmodf(1.0f, 0.0f), nan, __LINE__, "fmod(1, 0)");
    math_expect(t, bmf_fmodf(-0.0f, 1.0f), -0.0f, __LINE__, "fmod(-0, 1)");
    math_expect(t, bmf_fmodf(-6.0f, 3.0f), -0.0f, __LINE__, "fmod(-6, 3)");
    math_expect(t, bmf_fmodf(max, tiny), 0.0f, __LINE__, "fmod(FLT_MAX, 2^-149)");
    math_expect(t, bmf_fmodf(max, 3.0f), 0.0f, __LINE__, "fmod(FLT_MAX, 3) = 0 since (2^24-1) 2^104 is a multiple of 3");
    math_expect(t, bmf_fmodf(max, 11.0f), 9.0f, __LINE__, "fmod(FLT_MAX, 11)");
    math_expect(t, bmf_hypotf(3.0f, 4.0f), 5.0f, __LINE__, "hypot(3, 4)");
    math_expect(t, bmf_hypotf(inf, nan), inf, __LINE__, "hypot(Inf, NaN)");
    math_expect(t, bmf_hypotf(max, max), inf, __LINE__, "hypot(FLT_MAX, FLT_MAX)");
    math_expect(t, bmf_hypotf(tiny, tiny), tiny, __LINE__, "hypot(2^-149, 2^-149)");
    math_expect(t, bmf_sqrtf(-0.0f), -0.0f, __LINE__, "sqrt(-0)");
    math_expect(t, bmf_roundf(0.49999997f), 0.0f, __LINE__, "round(0.49999997)");
    math_expect(t, bmf_roundf(-0.5f), -1.0f, __LINE__, "round(-0.5)");
    math_expect(t, bmf_roundf(-0.3f), -0.0f, __LINE__, "round(-0.3)");
    math_expect(t, bmf_roundf(8388609.0f), 8388609.0f, __LINE__, "round(2^23 + 1)");
    math_expect(t, bmf_ceilf(-0.5f), -0.0f, __LINE__, "ceil(-0.5)");
    math_expect(t, bmf_floorf(-0.0f), -0.0f, __LINE__, "floor(-0)");
    math_expect(t, bmf_fminf(-0.0f, 0.0f), -0.0f, __LINE__, "fmin(-0, 0)");
    math_expect(t, bmf_fminf(0.0f, -0.0f), -0.0f, __LINE__, "fmin(0, -0)");
    math_expect(t, bmf_fmaxf(-0.0f, 0.0f), 0.0f, __LINE__, "fmax(-0, 0)");
    math_expect(t, bmf_fmaxf(0.0f, -0.0f), 0.0f, __LINE__, "fmax(0, -0)");
    math_expect(t, bmf_fminf(nan, 1.0f), 1.0f, __LINE__, "fmin(NaN, 1)");
    math_expect(t, bmf_fminf(1.0f, nan), 1.0f, __LINE__, "fmin(1, NaN)");
    math_expect(t, bmf_fmaxf(nan, -1.0f), -1.0f, __LINE__, "fmax(NaN, -1)");
    math_expect(t, bmf_fmaxf(-1.0f, nan), -1.0f, __LINE__, "fmax(-1, NaN)");
    math_expect(t, bmf_fminf(1.0f, 2.0f), 1.0f, __LINE__, "fmin(1, 2)");
    math_expect(t, bmf_fmaxf(1.0f, 2.0f), 2.0f, __LINE__, "fmax(1, 2)");
    bmf_check_eq_i64(t, bmf_f32_to_i32(nan), 0, __FILE__, __LINE__, "f32_to_i32(NaN)");
    bmf_check_eq_i64(t, bmf_f32_to_i32(1e10f), 2147483647, __FILE__, __LINE__, "f32_to_i32(1e10)");
    bmf_check_eq_i64(t, bmf_f32_to_i32(-1e10f), -2147483647 - 1, __FILE__, __LINE__, "f32_to_i32(-1e10)");
    bmf_check_eq_i64(t, bmf_f32_to_i32(inf), 2147483647, __FILE__, __LINE__, "f32_to_i32(Inf)");
    bmf_check_eq_i64(t, bmf_f32_to_i32(2147483520.0f), 2147483520, __FILE__, __LINE__, "f32_to_i32(largest below 2^31)");
    bmf_check_eq_i64(t, bmf_f32_to_i32(-2147483648.0f), -2147483647 - 1, __FILE__, __LINE__, "f32_to_i32(-2^31)");
    bmf_check_eq_i64(t, bmf_f32_to_i32(-1.9f), -1, __FILE__, __LINE__, "f32_to_i32(-1.9)");
    bmf_check_eq_i64(t, bmf_f32_to_i32(1.9f), 1, __FILE__, __LINE__, "f32_to_i32(1.9)");
}

// ---------------------------------------------------------------- rng normal
static void test_rng_normal(bmf_test* t) {
    bmf_rng r, r2;
    bmf_rng_seed(&r, 2024, 7);
    bmf_rng_seed(&r2, 2024, 7);
    f64 sum = 0.0, sum2 = 0.0, max_abs = 0.0;
    i32 differ = 0, non_finite = 0;
    enum { N = 100000 };
    for (i32 i = 0; i < N; i++) {
        f32 z = bmf_rng_normal(&r);
        if (bmf_f32_bits(z) != bmf_f32_bits(bmf_rng_normal(&r2))) differ++;
        if (!(bmf_fabsf(z) <= BMF_F32_MAX)) non_finite++;
        sum += z;
        sum2 += (f64)z * z;
        if (ref_abs64(z) > max_abs) max_abs = ref_abs64(z);
    }
    f64 mean = sum / (f64)N, variance = sum2 / (f64)N - mean * mean;
    bmf_check_eq_i64(t, differ, 0, __FILE__, __LINE__, "same seed, same sequence");
    bmf_check_eq_i64(t, non_finite, 0, __FILE__, __LINE__, "non-finite sample");
    bmf_check(t, ref_abs64(mean) < 0.02, __FILE__, __LINE__, "mean within 6 sigma of 0");
    bmf_check(t, variance > 0.973 && variance < 1.027, __FILE__, __LINE__, "variance within 6 sigma of 1");
    bmf_check(t, max_abs <= 5.78, __FILE__, __LINE__, "|z| <= sqrt(-2 ln 2^-24)");
}

// ---------------------------------------------------------------- vectors, transforms, colors (1A-04b)
static b32 math_near(f32 a, f32 b, f32 tolerance) { return bmf_fabsf(a - b) <= tolerance; }

static void test_math_scalar_helpers(bmf_test* t) {
    f32 nan = bmf_f32_from_bits(0x7FC00000u);
    math_expect(t, bmf_lerpf(2.0f, 6.0f, 0.0f), 2.0f, __LINE__, "lerp t=0");
    math_expect(t, bmf_lerpf(2.0f, 6.0f, 0.25f), 3.0f, __LINE__, "lerp t=0.25");
    math_expect(t, bmf_lerpf(2.0f, 6.0f, 1.0f), 6.0f, __LINE__, "lerp t=1");
    math_expect(t, bmf_clampf(5.0f, 0.0f, 1.0f), 1.0f, __LINE__, "clamp high");
    math_expect(t, bmf_clampf(-5.0f, 0.0f, 1.0f), 0.0f, __LINE__, "clamp low");
    math_expect(t, bmf_clampf(0.5f, 0.0f, 1.0f), 0.5f, __LINE__, "clamp inside");
    math_expect(t, bmf_clampf(nan, -1.0f, 1.0f), -1.0f, __LINE__, "clamp NaN gives lo");
    math_expect(t, bmf_saturatef(2.0f), 1.0f, __LINE__, "saturate");
    math_expect(t, bmf_smoothstepf(0.0f, 2.0f, -1.0f), 0.0f, __LINE__, "smoothstep below");
    math_expect(t, bmf_smoothstepf(0.0f, 2.0f, 1.0f), 0.5f, __LINE__, "smoothstep middle");
    math_expect(t, bmf_smoothstepf(0.0f, 2.0f, 3.0f), 1.0f, __LINE__, "smoothstep above");
    math_expect(t, bmf_smoothstepf(0.0f, 4.0f, 1.0f), 0.15625f, __LINE__, "smoothstep quarter");
    bmf_check_eq_i64(t, bmf_clamp_i32(7, 0, 5), 5, __FILE__, __LINE__, "clamp_i32");
    bmf_check_eq_i64(t, bmf_min_i32(-3, 2) + bmf_max_i32(-3, 2), -1, __FILE__, __LINE__, "min/max_i32");
}

static void test_math_vectors(bmf_test* t) {
    bmf_v2 a = bmf_v2_make(3.0f, 4.0f), b = bmf_v2_make(-1.0f, 2.0f);
    bmf_check(t, bmf_v2_equal(bmf_v2_add(a, b), bmf_v2_make(2.0f, 6.0f)), __FILE__, __LINE__, "v2 add");
    bmf_check(t, bmf_v2_equal(bmf_v2_sub(a, b), bmf_v2_make(4.0f, 2.0f)), __FILE__, __LINE__, "v2 sub");
    bmf_check(t, bmf_v2_equal(bmf_v2_mul(a, b), bmf_v2_make(-3.0f, 8.0f)), __FILE__, __LINE__, "v2 mul");
    bmf_check(t, bmf_v2_equal(bmf_v2_add_scaled(a, b, 2.0f), bmf_v2_make(1.0f, 8.0f)), __FILE__, __LINE__, "v2 add_scaled");
    bmf_check(t, bmf_v2_equal(bmf_v2_div(a, 2.0f), bmf_v2_make(1.5f, 2.0f)), __FILE__, __LINE__, "v2 div");
    math_expect(t, bmf_v2_dot(a, b), 5.0f, __LINE__, "v2 dot");
    math_expect(t, bmf_v2_cross(a, b), 10.0f, __LINE__, "v2 cross");
    math_expect(t, bmf_v2_cross(bmf_v2_make(1, 0), bmf_v2_perp(bmf_v2_make(1, 0))), 1.0f, __LINE__, "perp is counter-clockwise");
    math_expect(t, bmf_v2_length(a), 5.0f, __LINE__, "v2 length");
    math_expect(t, bmf_v2_distance(a, b), bmf_sqrtf(20.0f), __LINE__, "v2 distance");
    bmf_check(t, bmf_v2_equal(bmf_v2_normalize_or_zero(a), bmf_v2_make(0.6f, 0.8f)), __FILE__, __LINE__, "v2 normalize");
    bmf_v2 z = bmf_v2_normalize_or_zero(bmf_v2_make(0.0f, -0.0f));
    bmf_check(t, bmf_f32_bits(z.x) == 0 && bmf_f32_bits(z.y) == 0, __FILE__, __LINE__, "normalize of zero is +0, +0");
    bmf_check(t, bmf_v2_equal(bmf_v2_min(a, b), bmf_v2_make(-1.0f, 2.0f)) && bmf_v2_equal(bmf_v2_max(a, b), a), __FILE__, __LINE__, "v2 min/max");
    bmf_check(t, bmf_v2_equal(bmf_v2_clamp(bmf_v2_make(9, -9), bmf_v2_make(0, 0), bmf_v2_make(1, 1)), bmf_v2_make(1, 0)), __FILE__, __LINE__, "v2 clamp");
    bmf_check(t, bmf_v2_equal(bmf_v2_lerp(a, b, 0.5f), bmf_v2_make(1.0f, 3.0f)), __FILE__, __LINE__, "v2 lerp");
    bmf_v3 x = bmf_v3_make(1, 0, 0), y = bmf_v3_make(0, 1, 0);
    bmf_v3 c = bmf_v3_cross(x, y);
    bmf_check(t, c.x == 0.0f && c.y == 0.0f && c.z == 1.0f, __FILE__, __LINE__, "v3 cross x * y = z");
    math_expect(t, bmf_v3_length(bmf_v3_make(2, 3, 6)), 7.0f, __LINE__, "v3 length");
    bmf_v3 n = bmf_v3_normalize_or_zero(bmf_v3_make(0, 0, -5));
    bmf_check(t, n.x == 0.0f && n.y == 0.0f && n.z == -1.0f, __FILE__, __LINE__, "v3 normalize");
    math_expect(t, bmf_v4_dot(bmf_v4_make(1, 2, 3, 4), bmf_v4_make(5, 6, 7, 8)), 70.0f, __LINE__, "v4 dot");
    // Random normalize: unit length within 2 ULP of 1, no NaN.
    bmf_rng r;
    bmf_rng_seed(&r, 3, 3);
    i32 bad = 0;
    for (i32 i = 0; i < 10000; i++) {
        f32 vx = math_uniform(&r, -1e6f, 1e6f);
        bmf_v2 v = bmf_v2_make(vx, math_uniform(&r, -1e-3f, 1e-3f));
        if (!math_near(bmf_v2_length(bmf_v2_normalize_or_zero(v)), 1.0f, 2.4e-7f)) bad++;
    }
    bmf_check_eq_i64(t, bad, 0, __FILE__, __LINE__, "normalized length not within 2 ULP of 1");
}

static void test_math_rot_and_transform(bmf_test* t) {
    bmf_rot q = bmf_rot_from_angle(BMF_HALF_PI);
    bmf_v2 v = bmf_rot_apply(q, bmf_v2_make(1.0f, 0.0f));
    bmf_check(t, math_near(v.x, 0.0f, 1e-7f) && v.y == 1.0f, __FILE__, __LINE__, "rot 90 degrees maps x to y");
    bmf_v2 back = bmf_rot_apply_inverse(q, v);
    bmf_check(t, math_near(back.x, 1.0f, 1e-7f) && math_near(back.y, 0.0f, 1e-7f), __FILE__, __LINE__, "apply_inverse undoes apply");
    bmf_rot a = bmf_rot_from_angle(0.3f), b = bmf_rot_from_angle(0.5f), ab = bmf_rot_mul(a, b);
    bmf_check(t, math_near(bmf_rot_angle(ab), 0.8f, 2e-7f), __FILE__, __LINE__, "rot_mul adds angles");
    bmf_rot id = bmf_rot_mul(a, bmf_rot_inverse(a));
    bmf_check(t, math_near(id.c, 1.0f, 2.4e-7f) && math_near(id.s, 0.0f, 1e-7f), __FILE__, __LINE__, "rot inverse (c within 2 ULP of 1)");
    math_expect(t, bmf_rot_angle(bmf_rot_identity()), 0.0f, __LINE__, "identity angle");
    bmf_rot zero = { 0.0f, 0.0f }, scaled = { 3.0f, 4.0f };
    bmf_check(t, bmf_rot_normalize(zero).c == 1.0f && bmf_rot_normalize(zero).s == 0.0f, __FILE__, __LINE__, "normalize of zero is identity");
    bmf_check(t, bmf_rot_normalize(scaled).c == 0.6f && bmf_rot_normalize(scaled).s == 0.8f, __FILE__, __LINE__, "rot normalize");
    // TRS: scale (2, 3), rotate 90 degrees, move to (10, 20): (1, 1) -> rotate (2, 3) = (-3, 2) -> (7, 22).
    bmf_m3x2 m = bmf_m3x2_trs(bmf_v2_make(10.0f, 20.0f), q, bmf_v2_make(2.0f, 3.0f));
    bmf_v2 p = bmf_m3x2_apply_point(m, bmf_v2_make(1.0f, 1.0f));
    bmf_check(t, math_near(p.x, 7.0f, 1e-5f) && math_near(p.y, 22.0f, 1e-5f), __FILE__, __LINE__, "trs apply_point");
    bmf_v2 d = bmf_m3x2_apply_vector(m, bmf_v2_make(1.0f, 1.0f));
    bmf_check(t, math_near(d.x, -3.0f, 1e-5f) && math_near(d.y, 2.0f, 1e-5f), __FILE__, __LINE__, "apply_vector ignores translation");
    bmf_m3x2 mi = bmf_m3x2_mul(bmf_m3x2_inverse(m), m);
    b32 is_identity = math_near(mi.x_axis.x, 1.0f, 1e-6f) && math_near(mi.x_axis.y, 0.0f, 1e-6f) &&
                      math_near(mi.y_axis.x, 0.0f, 1e-6f) && math_near(mi.y_axis.y, 1.0f, 1e-6f) &&
                      math_near(mi.origin.x, 0.0f, 1e-5f) && math_near(mi.origin.y, 0.0f, 1e-5f);
    bmf_check(t, is_identity, __FILE__, __LINE__, "inverse(m) * m = identity");
    // mul order: mul(translation, scale) scales first.
    bmf_m3x2 ts = bmf_m3x2_mul(bmf_m3x2_translation(bmf_v2_make(5, 0)), bmf_m3x2_trs(bmf_v2_make(0, 0), bmf_rot_identity(), bmf_v2_make(2, 2)));
    bmf_check(t, bmf_v2_equal(bmf_m3x2_apply_point(ts, bmf_v2_make(1, 1)), bmf_v2_make(7, 2)), __FILE__, __LINE__, "mul(a, b) applies b first");
    bmf_check(t, math_near(bmf_m3x2_determinant(m), 6.0f, 1e-6f), __FILE__, __LINE__, "determinant = scale.x * scale.y");
}

static void test_math_rect_aabb(bmf_test* t) {
    bmf_rect r = bmf_rect_make(10, 20, 30, 40);
    bmf_check(t, bmf_rect_contains(r, bmf_v2_make(10, 20)) && !bmf_rect_contains(r, bmf_v2_make(40, 20)) && !bmf_rect_contains(r, bmf_v2_make(10, 60)),
              __FILE__, __LINE__, "rect is half-open");
    bmf_check(t, bmf_v2_equal(bmf_rect_center(r), bmf_v2_make(25, 40)), __FILE__, __LINE__, "rect center");
    bmf_aabb a = bmf_rect_to_aabb(r), b = bmf_aabb_make(bmf_v2_make(40, 60), bmf_v2_make(50, 70));
    bmf_check(t, bmf_aabb_overlaps(a, b) && bmf_aabb_overlaps(b, a), __FILE__, __LINE__, "touching boxes overlap");
    bmf_aabb c = bmf_aabb_make(bmf_v2_make(40.001f, 0), bmf_v2_make(50, 10));
    bmf_check(t, !bmf_aabb_overlaps(a, c), __FILE__, __LINE__, "separated boxes do not overlap");
    bmf_check(t, bmf_aabb_contains_point(a, bmf_v2_make(40, 60)), __FILE__, __LINE__, "aabb is closed");
    bmf_aabb u = bmf_aabb_union(a, b);
    bmf_check(t, bmf_v2_equal(u.min, bmf_v2_make(10, 20)) && bmf_v2_equal(u.max, bmf_v2_make(50, 70)), __FILE__, __LINE__, "union");
    bmf_check(t, bmf_aabb_contains(u, a) && bmf_aabb_contains(u, b) && !bmf_aabb_contains(a, u), __FILE__, __LINE__, "contains");
    bmf_aabb e = bmf_aabb_expand(a, 1.0f);
    bmf_check(t, bmf_v2_equal(e.min, bmf_v2_make(9, 19)) && bmf_v2_equal(e.max, bmf_v2_make(41, 61)), __FILE__, __LINE__, "expand");
    bmf_aabb f = bmf_aabb_from_center(bmf_v2_make(1, 2), bmf_v2_make(3, 4));
    bmf_check(t, bmf_v2_equal(bmf_aabb_center(f), bmf_v2_make(1, 2)) && bmf_v2_equal(bmf_aabb_half_extents(f), bmf_v2_make(3, 4)), __FILE__, __LINE__, "center/half extents");
    math_expect(t, bmf_aabb_perimeter(f), 28.0f, __LINE__, "perimeter");
}

static void test_math_colors(bmf_test* t) {
    bmf_color c = bmf_color_rgba(0x12, 0x34, 0x56, 0x78);
    bmf_check_eq_u64(t, c, 0x12345678u, __FILE__, __LINE__, "rgba pack");
    bmf_check(t, bmf_color_r(c) == 0x12 && bmf_color_g(c) == 0x34 && bmf_color_b(c) == 0x56 && bmf_color_a(c) == 0x78, __FILE__, __LINE__, "unpack");
    i32 bad = 0;
    for (u32 v = 0; v < 256; v++) {
        bmf_color k = bmf_color_rgba((u8)v, (u8)(255 - v), (u8)(v * 7), (u8)(v ^ 0x5A));
        if (bmf_color_from_v4(bmf_color_to_v4(k)) != k) bad++;
    }
    bmf_check_eq_i64(t, bad, 0, __FILE__, __LINE__, "from_v4(to_v4(c)) == c for every channel value");
    bmf_check_eq_u64(t, bmf_color_from_v4(bmf_v4_make(2.0f, -1.0f, 0.5f, bmf_f32_from_bits(0x7FC00000u))), 0xFF008000u, __FILE__, __LINE__, "from_v4 clamps, rounds, NaN -> 0");
    bmf_check_eq_u64(t, bmf_color_premultiply(0xFFFFFF80u), 0x80808080u, __FILE__, __LINE__, "premultiply half alpha");
    bmf_check_eq_u64(t, bmf_color_premultiply(0x40C0FFFFu), 0x40C0FFFFu, __FILE__, __LINE__, "premultiply opaque is identity");
    bmf_check_eq_u64(t, bmf_color_premultiply(0xFFFFFF00u), 0x00000000u, __FILE__, __LINE__, "premultiply transparent is zero");
}

// Determinism golden for the inline vector code: hash of the output bits over random inputs.
static void test_math_vector_output_hash(bmf_test* t) {
    bmf_rng r;
    bmf_rng_seed(&r, 4, 4);
    bmf_hasher h;
    bmf_hasher_init(&h, 0);
    for (i32 i = 0; i < 20000; i++) {
        bmf_v2 a, b;   // one random draw per statement: argument evaluation order is unspecified in C
        a.x = math_uniform(&r, -100, 100);
        a.y = math_uniform(&r, -100, 100);
        b.x = math_uniform(&r, -100, 100);
        b.y = math_uniform(&r, -100, 100);
        f32 s = math_uniform(&r, -2, 2);
        bmf_color random_color = bmf_rng_next_u32(&r);
        bmf_rot q = bmf_rot_from_angle(s);
        bmf_m3x2 m = bmf_m3x2_trs(a, q, bmf_v2_make(1.5f, 0.5f));
        bmf_m3x2 mi = bmf_m3x2_inverse(m);
        bmf_v2 an = bmf_v2_normalize_or_zero(a), lerped = bmf_v2_lerp(a, b, s);
        bmf_v2 qb = bmf_rot_apply(q, b), qib = bmf_rot_apply_inverse(q, b);
        bmf_v2 mb = bmf_m3x2_apply_point(m, b), mib = bmf_m3x2_apply_point(mi, b);
        f32 out[24] = {
            bmf_v2_dot(a, b), bmf_v2_cross(a, b), bmf_v2_length(a), bmf_v2_distance(a, b),
            an.x, an.y, lerped.x, lerped.y, qb.x, qb.y, qib.x, qib.y, mb.x, mb.y, mib.x, mib.y,
            bmf_rot_angle(bmf_rot_mul(q, q)), bmf_smoothstepf(-1, 1, s),
            bmf_v3_length(bmf_v3_cross(bmf_v3_make(a.x, a.y, s), bmf_v3_make(b.x, s, b.y))),
            bmf_aabb_perimeter(bmf_aabb_union(bmf_aabb_from_center(a, bmf_v2_abs(b)), bmf_aabb_from_center(b, bmf_v2_abs(a)))),
            bmf_v4_dot(bmf_v4_make(a.x, a.y, b.x, b.y), bmf_v4_make(s, s, a.x, b.y)), bmf_rot_normalize(bmf_rot_mul(q, q)).c,
            bmf_color_to_v4(random_color).z, (f32)bmf_color_from_v4(bmf_v4_make(s, -s, s * 0.5f, 1))
        };
        bmf_hasher_update(&h, out, sizeof out);
    }
    u64 hash = bmf_hasher_final(&h);
    char note[64];
    bmf_fmt_buf(note, (i32)sizeof note, "hash=0x%016lx", hash);
    bmf_test_note(t, note);
    // Recorded 2026-11-19 (1A-04b); identical on win64 and wasm32 (Node/V8) at -O0 and -O2 + LTO.
    bmf_check_eq_u64(t, hash, 0xbf5f8646f1e92864ull, __FILE__, __LINE__, "vector output hash (determinism golden)");
}

// ---------------------------------------------------------------- fast tier (1A-04c)
// Errors of bmf_fast_* against the deterministic tier (which is correctly rounded here) over
// 200 000 samples each; the bounds are the ones documented in base_math.h.
static void test_math_fast_tier(bmf_test* t) {
    bmf_rng r;
    bmf_rng_seed(&r, 11, 11);
    f64 sincos_abs = 0, atan2_abs = 0, exp2_rel = 0, exp_rel_small = 0, exp_rel = 0, log2_abs = 0, pow_rel = 0;
    for (i32 i = 0; i < 200000; i++) {
        f32 x = (i & 1) ? math_uniform(&r, -8192.0f, 8192.0f) : math_uniform(&r, -10.0f, 10.0f);
        f32 fs, fc;
        bmf_fast_sincosf(x, &fs, &fc);
        f64 e = ref_abs64((f64)fs - (f64)bmf_sinf(x));
        if (e > sincos_abs) sincos_abs = e;
        e = ref_abs64((f64)fc - (f64)bmf_cosf(x));
        if (e > sincos_abs) sincos_abs = e;

        f32 ay = (i & 2) ? math_uniform(&r, -100.0f, 100.0f) : math_random_bits(&r, 180);
        f32 ax = (i & 4) ? math_uniform(&r, -100.0f, 100.0f) : math_random_bits(&r, 180);
        e = ref_abs64((f64)bmf_fast_atan2f(ay, ax) - (f64)bmf_atan2f(ay, ax));
        if (e > atan2_abs) atan2_abs = e;

        f32 w = math_uniform(&r, -126.0f, 127.4f);
        e = ref_abs64((f64)bmf_fast_exp2f(w) / (f64)bmf_exp2f(w) - 1.0);
        if (e > exp2_rel) exp2_rel = e;
        f32 v = math_uniform(&r, -10.0f, 10.0f);
        e = ref_abs64((f64)bmf_fast_expf(v) / (f64)bmf_expf(v) - 1.0);
        if (e > exp_rel_small) exp_rel_small = e;
        v = math_uniform(&r, -87.0f, 88.0f);
        e = ref_abs64((f64)bmf_fast_expf(v) / (f64)bmf_expf(v) - 1.0);
        if (e > exp_rel) exp_rel = e;

        f32 p = bmf_fabsf(math_random_bits(&r, 254));
        if (p < 1.17549435e-38f) p = 1.0f;                          // normal inputs only
        e = ref_abs64((f64)bmf_fast_log2f(p) - (f64)bmf_log2f(p)) / (1.0 + ref_abs64((f64)bmf_log2f(p)));
        if (e > log2_abs) log2_abs = e;

        f32 base = math_uniform(&r, 0.01f, 100.0f);
        f32 power = math_uniform(&r, -10.0f, 10.0f);
        f64 scale = 1.0 + ref_abs64((f64)power * (f64)bmf_log2f(base));
        e = ref_abs64((f64)bmf_fast_powf(base, power) / (f64)bmf_powf(base, power) - 1.0) / scale;
        if (e > pow_rel) pow_rel = e;
    }
    char note[512];
    bmf_fmt_buf(note, (i32)sizeof note, "max errors: sincos_abs=%f atan2_abs=%f exp2_rel=%f exp_rel_small=%f exp_rel=%f log2_per_unit=%f pow_rel_per_unit=%f",
                sincos_abs, atan2_abs, exp2_rel, exp_rel_small, exp_rel, log2_abs, pow_rel);
    bmf_test_note(t, note);
    bmf_check(t, sincos_abs <= 2e-7, __FILE__, __LINE__, "fast sincos abs error > 2e-7");
    bmf_check(t, atan2_abs <= 3e-7, __FILE__, __LINE__, "fast atan2 abs error > 3e-7");
    bmf_check(t, exp2_rel <= 3e-7, __FILE__, __LINE__, "fast exp2 rel error > 3e-7");
    bmf_check(t, exp_rel_small <= 1e-6, __FILE__, __LINE__, "fast exp rel error > 1e-6 for |x| <= 10");
    bmf_check(t, exp_rel <= 6e-6, __FILE__, __LINE__, "fast exp rel error > 6e-6");
    bmf_check(t, log2_abs <= 2e-7, __FILE__, __LINE__, "fast log2 error > 2e-7 (1 + |log2 x|)");
    bmf_check(t, pow_rel <= 1e-6, __FILE__, __LINE__, "fast pow rel error > 1e-6 (1 + |y log2 x|)");
    // Edges.
    f32 inf = bmf_f32_from_bits(0x7F800000u);
    math_expect(t, bmf_fast_exp2f(128.0f), inf, __LINE__, "fast exp2 overflow");
    math_expect(t, bmf_fast_exp2f(-127.0f), 0.0f, __LINE__, "fast exp2 underflow");
    math_expect(t, bmf_fast_exp2f(3.0f), 8.0f, __LINE__, "fast exp2 of an integer");
    math_expect(t, bmf_fast_log2f(8.0f), 3.0f, __LINE__, "fast log2 of a power of two");
    math_expect(t, bmf_fast_atan2f(0.0f, -1.0f), 3.14159274f, __LINE__, "fast atan2(0, -1)");
    math_expect(t, bmf_fast_atan2f(-0.0f, 0.0f), -0.0f, __LINE__, "fast atan2(-0, 0)");
    // The forbidden-name table is 0-terminated and covers the whole fast tier.
    i32 count = 0;
    b32 has_fast = 0;
    while (BMF_SIMULATION_FORBIDDEN[count]) { has_fast |= bmf_cstr_eq(BMF_SIMULATION_FORBIDDEN[count], "bmf_fast_"); count++; }
    bmf_check(t, has_fast && count >= 5, __FILE__, __LINE__, "BMF_SIMULATION_FORBIDDEN lists bmf_fast_");
}
