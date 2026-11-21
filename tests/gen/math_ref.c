// math_ref.c: high-precision reference for tests/test_math.c (task 1A-04a).
//
// No libm and nothing shared with src/base/base_math.c except the f32 bit helpers:
// - big-integer fixed point (24 x 32-bit limbs, about 730 fraction bits) computes pi (Machin's
//   formula), ln 2 (2 atanh(1/3)) and 2/pi (binary long division) from scratch;
// - double-double arithmetic (about 106 bits, plain f64 operations, no FMA) computes function
//   values with different algorithms than the runtime: exact big-integer reduction mod pi/2,
//   long Taylor series, exp by halving-and-squaring, log by Halley iteration on exp, atan by
//   argument halving.
// The reference error is below 2^-100 relative, so a measured error in f32 ULPs is exact to
// many digits. The reference is computed at test time instead of being stored in a generated
// table: 100 000 samples for 13 functions would be tens of megabytes of text.

// ---------------------------------------------------------------- big fixed point
// value = sum of limb[i] * 2^(-32 i); limb[0] is the integer part.
enum { REF_LIMBS = 24 };
typedef struct { u32 limb[REF_LIMBS]; } ref_big;

static void ref_big_zero(ref_big* a) { bmf_memset(a, 0, sizeof *a); }

static b32 ref_big_is_zero(const ref_big* a) {
    for (i32 i = 0; i < REF_LIMBS; i++) if (a->limb[i]) return 0;
    return 1;
}

static void ref_big_add(ref_big* a, const ref_big* b) {
    u64 carry = 0;
    for (i32 i = REF_LIMBS - 1; i >= 0; i--) {
        u64 s = (u64)a->limb[i] + b->limb[i] + carry;
        a->limb[i] = (u32)s;
        carry = s >> 32;
    }
}

static void ref_big_sub(ref_big* a, const ref_big* b) {   // a >= b
    u64 borrow = 0;
    for (i32 i = REF_LIMBS - 1; i >= 0; i--) {
        u64 d = (u64)a->limb[i] - (u64)b->limb[i] - borrow;
        a->limb[i] = (u32)d;
        borrow = d >> 63;
    }
}

static void ref_big_mul_small(ref_big* out, const ref_big* a, u32 m) {   // integer part must stay < 2^32
    u64 carry = 0;
    for (i32 i = REF_LIMBS - 1; i >= 0; i--) {
        u64 p = (u64)a->limb[i] * m + carry;
        out->limb[i] = (u32)p;
        carry = p >> 32;
    }
}

static void ref_big_div_small(ref_big* a, u32 d) {
    u64 rem = 0;
    for (i32 i = 0; i < REF_LIMBS; i++) {
        u64 cur = (rem << 32) | a->limb[i];
        a->limb[i] = (u32)(cur / d);
        rem = cur % d;
    }
}

static i32 ref_big_cmp(const ref_big* a, const ref_big* b) {
    for (i32 i = 0; i < REF_LIMBS; i++) if (a->limb[i] != b->limb[i]) return a->limb[i] < b->limb[i] ? -1 : 1;
    return 0;
}

static void ref_big_shl1(ref_big* a) {
    for (i32 i = 0; i < REF_LIMBS; i++) a->limb[i] = (a->limb[i] << 1) | (i + 1 < REF_LIMBS ? a->limb[i + 1] >> 31 : 0u);
}

// The bit of `a` with weight 2^w (w <= 31).
static u32 ref_big_bit(const ref_big* a, i32 w) {
    i32 pos = 31 - w;                       // 0 = most significant bit of limb 0
    i32 limb = pos / 32;
    if (limb >= REF_LIMBS) return 0;
    return (a->limb[limb] >> (31 - pos % 32)) & 1u;
}

// Sets the bit with weight 2^w (w <= 31).
static void ref_big_set_bit(ref_big* a, i32 w) {
    i32 pos = 31 - w;
    if (pos / 32 < REF_LIMBS) a->limb[pos / 32] |= 1u << (31 - pos % 32);
}

// An exact positive f64 (normal, exponent >= -700) as a big number.
static ref_big ref_big_from_f64(f64 v) {
    ref_big a;
    ref_big_zero(&a);
    u64 bits = bmf_f64_bits(v);
    i32 k = (i32)((bits >> 52) & 0x7FF) - 1075;           // v = mantissa * 2^k
    u64 mantissa = (bits & 0x000FFFFFFFFFFFFFull) | 0x0010000000000000ull;
    for (i32 b = 0; b < 53; b++) if ((mantissa >> b) & 1u) ref_big_set_bit(&a, k + b);
    return a;
}

// atan(1/n) (alternating series) or atanh(1/n).
static void ref_big_arctan_inverse(ref_big* out, u32 n, b32 hyperbolic) {
    ref_big power, term;
    ref_big_zero(out);
    ref_big_zero(&power);
    power.limb[0] = 1;
    ref_big_div_small(&power, n);
    for (u32 k = 1; !ref_big_is_zero(&power); k += 2) {
        term = power;
        ref_big_div_small(&term, k);
        if (!hyperbolic && ((k >> 1) & 1u)) ref_big_sub(out, &term); else ref_big_add(out, &term);
        ref_big_div_small(&power, n);
        ref_big_div_small(&power, n);
    }
}

// ---------------------------------------------------------------- double-double
typedef struct { f64 hi, lo; } ref_dd;

static ref_dd ref_dd_make(f64 hi) { ref_dd r = { hi, 0.0 }; return r; }
static f64 ref_abs64(f64 x) { return x < 0 ? -x : x; }

static ref_dd ref_two_sum(f64 a, f64 b) {
    f64 s = a + b;
    f64 bb = s - a;
    ref_dd r = { s, (a - (s - bb)) + (b - bb) };
    return r;
}
static ref_dd ref_fast_two_sum(f64 a, f64 b) {   // |a| >= |b|
    f64 s = a + b;
    ref_dd r = { s, b - (s - a) };
    return r;
}
static ref_dd ref_two_prod(f64 a, f64 b) {      // Dekker: exact a*b as hi + lo, no FMA
    f64 p = a * b;
    f64 ta = 134217729.0 * a, ah = ta - (ta - a), al = a - ah;
    f64 tb = 134217729.0 * b, bh = tb - (tb - b), bl = b - bh;
    ref_dd r = { p, ((ah * bh - p) + ah * bl + al * bh) + al * bl };
    return r;
}
static ref_dd ref_add(ref_dd a, ref_dd b) {
    ref_dd s = ref_two_sum(a.hi, b.hi), t = ref_two_sum(a.lo, b.lo);
    s.lo += t.hi;
    s = ref_fast_two_sum(s.hi, s.lo);
    s.lo += t.lo;
    return ref_fast_two_sum(s.hi, s.lo);
}
static ref_dd ref_neg(ref_dd a) { a.hi = -a.hi; a.lo = -a.lo; return a; }
static ref_dd ref_sub(ref_dd a, ref_dd b) { return ref_add(a, ref_neg(b)); }
static ref_dd ref_mul(ref_dd a, ref_dd b) {
    ref_dd p = ref_two_prod(a.hi, b.hi);
    p.lo += a.hi * b.lo + a.lo * b.hi;
    return ref_fast_two_sum(p.hi, p.lo);
}
static ref_dd ref_mul_d(ref_dd a, f64 b) { return ref_mul(a, ref_dd_make(b)); }
static ref_dd ref_div(ref_dd a, ref_dd b) {
    f64 q1 = a.hi / b.hi;
    ref_dd r = ref_sub(a, ref_mul_d(b, q1));
    f64 q2 = r.hi / b.hi;
    r = ref_sub(r, ref_mul_d(b, q2));
    f64 q3 = r.hi / b.hi;
    return ref_add(ref_fast_two_sum(q1, q2), ref_dd_make(q3));
}
static ref_dd ref_sqrt(ref_dd a) {
    if (a.hi <= 0.0) return ref_dd_make(0.0);
    f64 s = __builtin_sqrt(a.hi);
    ref_dd d = ref_sub(a, ref_two_prod(s, s));
    return ref_fast_two_sum(s, d.hi / (2.0 * s));
}
static ref_dd ref_scale(ref_dd a, f64 power_of_two) { a.hi *= power_of_two; a.lo *= power_of_two; return a; }
static f64 ref_pow2(i32 k) { return bmf_f64_from_bits((u64)(i64)(k + 1023) << 52); }   // k in [-1022, 1023]

static ref_dd ref_big_to_dd(const ref_big* a) {   // the first 6 limbs (160 fraction bits)
    ref_dd acc = ref_dd_make(0.0);
    for (i32 i = 0; i < 6; i++) acc = ref_add(acc, ref_dd_make((f64)a->limb[i] * ref_pow2(-32 * i)));
    return acc;
}

// A small big number (below 2^-32) rounded to f64: starts at its first nonzero limb.
static f64 ref_big_small_to_f64(const ref_big* a) {
    i32 first = 0;
    while (first < REF_LIMBS && a->limb[first] == 0) first++;
    ref_dd acc = ref_dd_make(0.0);
    for (i32 i = first; i < first + 4 && i < REF_LIMBS; i++) acc = ref_add(acc, ref_dd_make((f64)a->limb[i] * ref_pow2(-32 * i)));
    return acc.hi;
}

// ---------------------------------------------------------------- constants
static ref_big REF_BIG_PI, REF_BIG_LN2, REF_BIG_TWO_OVER_PI;
static ref_dd REF_PI, REF_PIO2, REF_LN2;
static b32 ref_ready;

static void ref_init(void) {
    if (ref_ready) return;
    ref_big a, b;
    ref_big_arctan_inverse(&a, 5, 0);
    ref_big_mul_small(&a, &a, 16);
    ref_big_arctan_inverse(&b, 239, 0);
    ref_big_mul_small(&b, &b, 4);
    REF_BIG_PI = a;
    ref_big_sub(&REF_BIG_PI, &b);                         // pi = 16 atan(1/5) - 4 atan(1/239)
    ref_big_arctan_inverse(&REF_BIG_LN2, 3, 1);
    ref_big_mul_small(&REF_BIG_LN2, &REF_BIG_LN2, 2);     // ln 2 = 2 atanh(1/3)
    ref_big rem;                                          // 2/pi by binary long division
    ref_big_zero(&rem);
    rem.limb[0] = 2;
    ref_big_zero(&REF_BIG_TWO_OVER_PI);
    for (i32 bit = 1; bit < 32 * (REF_LIMBS - 1); bit++) {
        ref_big_shl1(&rem);
        if (ref_big_cmp(&rem, &REF_BIG_PI) >= 0) {
            ref_big_sub(&rem, &REF_BIG_PI);
            REF_BIG_TWO_OVER_PI.limb[1 + (bit - 1) / 32] |= 1u << (31 - (bit - 1) % 32);
        }
    }
    REF_PI = ref_big_to_dd(&REF_BIG_PI);
    REF_PIO2 = ref_scale(REF_PI, 0.5);
    REF_LN2 = ref_big_to_dd(&REF_BIG_LN2);
    ref_ready = 1;
}

// ---------------------------------------------------------------- series
static ref_dd ref_sin_series(ref_dd r) {
    ref_dd r2 = ref_mul(r, r), term = r, sum = r;
    for (i32 n = 2; n < 80; n += 2) {
        term = ref_div(ref_mul(term, r2), ref_dd_make(-(f64)(n * (n + 1))));
        sum = ref_add(sum, term);
        if (ref_abs64(term.hi) <= 1e-40 * ref_abs64(sum.hi)) break;
    }
    return sum;
}
static ref_dd ref_cos_series(ref_dd r) {
    ref_dd r2 = ref_mul(r, r), term = ref_dd_make(1.0), sum = ref_dd_make(1.0);
    for (i32 n = 1; n < 80; n += 2) {
        term = ref_div(ref_mul(term, r2), ref_dd_make(-(f64)(n * (n + 1))));
        sum = ref_add(sum, term);
        if (ref_abs64(term.hi) <= 1e-40) break;
    }
    return sum;
}

// Exact reduction of a finite ax > 0 with the big 2/pi: ax * 2/pi = quadrant + fraction (mod 4),
// fraction in [-1/2, 1/2); returns fraction * pi/2.
static ref_dd ref_reduce(f32 ax, i32* quadrant) {
    if ((f64)ax < 0.78) { *quadrant = 0; return ref_dd_make((f64)ax); }
    u32 bits = bmf_f32_bits(ax);
    i32 e = (i32)(bits >> 23) - 150;                       // ax = m * 2^e, normal here
    u32 m = (bits & 0x007FFFFFu) | 0x00800000u;
    ref_big v;
    ref_big_mul_small(&v, &REF_BIG_TWO_OVER_PI, m);        // ax * 2/pi = v * 2^e
    i32 q = (i32)(ref_big_bit(&v, -e) | (ref_big_bit(&v, 1 - e) << 1));
    u32 chunk[6];
    for (i32 c = 0; c < 6; c++) {
        u32 w = 0;
        for (i32 k = 0; k < 32; k++) w = (w << 1) | ref_big_bit(&v, -e - 1 - 32 * c - k);
        chunk[c] = w;
    }
    ref_dd f = ref_dd_make(0.0);
    for (i32 c = 0; c < 6; c++) f = ref_add(f, ref_dd_make((f64)chunk[c] * ref_pow2(-32 * (c + 1))));
    if (chunk[0] >> 31) { q += 1; f = ref_sub(f, ref_dd_make(1.0)); }
    *quadrant = q & 3;
    return ref_mul(f, REF_PIO2);
}

static ref_dd ref_sin(f32 x) {
    i32 q;
    ref_dd r = ref_reduce(bmf_fabsf(x), &q);
    ref_dd v = (q & 1) ? ref_cos_series(r) : ref_sin_series(r);
    if (q & 2) v = ref_neg(v);
    return (bmf_f32_bits(x) >> 31) ? ref_neg(v) : v;
}
static ref_dd ref_cos(f32 x) {
    i32 q;
    ref_dd r = ref_reduce(bmf_fabsf(x), &q);
    ref_dd v = (q & 1) ? ref_sin_series(r) : ref_cos_series(r);
    return ((q + 1) & 2) ? ref_neg(v) : v;
}
static ref_dd ref_tan(f32 x) {
    i32 q;
    ref_dd r = ref_reduce(bmf_fabsf(x), &q);
    ref_dd s = ref_sin_series(r), c = ref_cos_series(r);
    ref_dd v = (q & 1) ? ref_neg(ref_div(c, s)) : ref_div(s, c);
    return (bmf_f32_bits(x) >> 31) ? ref_neg(v) : v;
}

// e^x for x in about [-160, 130]: x = k ln2 + r, e^r = (e^(r/1024))^1024 by ten squarings of
// e^s - 1 (which keeps full relative precision).
static ref_dd ref_exp(ref_dd x) {
    f64 k = __builtin_floor(x.hi / REF_LN2.hi + 0.5);
    ref_dd r = ref_scale(ref_sub(x, ref_mul_d(REF_LN2, k)), 1.0 / 1024.0);
    ref_dd term = r, sum = r;
    for (i32 n = 2; n < 40; n++) {
        term = ref_div(ref_mul(term, r), ref_dd_make((f64)n));
        sum = ref_add(sum, term);
        if (ref_abs64(term.hi) <= 1e-40) break;
    }
    for (i32 i = 0; i < 10; i++) sum = ref_mul(sum, ref_add(sum, ref_dd_make(2.0)));   // (e^s-1)(e^s+1) = e^2s - 1
    return ref_scale(ref_add(sum, ref_dd_make(1.0)), ref_pow2((i32)k));
}

// log(x) for x > 0 by Halley iteration y += 2 (x - e^y) / (x + e^y) from a crude start
// (error below 2e-4), which converges cubically: three steps reach full precision.
static ref_dd ref_log(ref_dd x) {
    u64 b = bmf_f64_bits(x.hi);
    i32 e = (i32)((b >> 52) & 0x7FF) - 1023;
    f64 m = bmf_f64_from_bits((b & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull);
    f64 s = (m - 1.0) / (m + 1.0);
    ref_dd y = ref_dd_make((f64)e * 0.6931471805599453 + 2.0 * s * (1.0 + s * s / 3.0 + s * s * s * s / 5.0));
    for (i32 i = 0; i < 3; i++) {
        ref_dd ey = ref_exp(y);
        y = ref_add(y, ref_scale(ref_div(ref_sub(x, ey), ref_add(x, ey)), 2.0));
    }
    return y;
}

// atan(t) for finite t >= 0: atan(t) = pi/2 - atan(1/t) for t > 1, then three halvings
// atan(t) = 2 atan(t / (1 + sqrt(1 + t^2))) to t <= tan(pi/32), then the Taylor series.
static ref_dd ref_atan_pos(ref_dd t) {
    b32 invert = t.hi > 1.0;
    if (invert) t = ref_div(ref_dd_make(1.0), t);
    for (i32 i = 0; i < 3; i++) t = ref_div(t, ref_add(ref_dd_make(1.0), ref_sqrt(ref_add(ref_dd_make(1.0), ref_mul(t, t)))));
    ref_dd t2 = ref_mul(t, t), power = t, sum = t;
    for (i32 k = 3; k < 400; k += 2) {
        power = ref_neg(ref_mul(power, t2));
        ref_dd term = ref_div(power, ref_dd_make((f64)k));
        sum = ref_add(sum, term);
        if (ref_abs64(term.hi) <= 1e-40) break;
    }
    sum = ref_scale(sum, 8.0);
    return invert ? ref_sub(REF_PIO2, sum) : sum;
}

static ref_dd ref_asin(f32 x) {
    f64 ax = (f64)bmf_fabsf(x);
    ref_dd v;
    if (ax == 1.0) v = REF_PIO2;
    else {
        ref_dd d = ref_sqrt(ref_sub(ref_dd_make(1.0), ref_two_prod(ax, ax)));
        v = ref_atan_pos(ref_div(ref_dd_make(ax), d));
    }
    return (bmf_f32_bits(x) >> 31) ? ref_neg(v) : v;
}
static ref_dd ref_acos(f32 x) { return ref_sub(REF_PIO2, ref_asin(x)); }

static ref_dd ref_atan(f32 x) {
    ref_dd v = ref_atan_pos(ref_dd_make((f64)bmf_fabsf(x)));
    return (bmf_f32_bits(x) >> 31) ? ref_neg(v) : v;
}

static ref_dd ref_atan2(f32 y, f32 x) {   // finite
    f64 ax = (f64)bmf_fabsf(x), ay = (f64)bmf_fabsf(y);
    ref_dd a;
    if (ay == 0.0) a = ref_dd_make(0.0);
    else if (ax == 0.0) a = REF_PIO2;
    else a = ref_atan_pos(ref_div(ref_dd_make(ay), ref_dd_make(ax)));
    if (bmf_f32_bits(x) >> 31) a = ref_sub(REF_PI, a);
    return (bmf_f32_bits(y) >> 31) ? ref_neg(a) : a;
}

static ref_dd ref_expf(f32 x) { return ref_exp(ref_dd_make((f64)x)); }
static ref_dd ref_exp2f(f32 x) { return ref_exp(ref_mul_d(REF_LN2, (f64)x)); }
static ref_dd ref_logf(f32 x) { return ref_log(ref_dd_make((f64)x)); }
static ref_dd ref_log2f(f32 x) { return ref_div(ref_log(ref_dd_make((f64)x)), REF_LN2); }

static ref_dd ref_powf(f32 x, f32 y) {   // x != 0 finite; x < 0 only with integer y
    ref_dd v = ref_exp(ref_mul_d(ref_log(ref_dd_make((f64)bmf_fabsf(x))), (f64)y));
    b32 odd = bmf_fabsf(y) < 16777216.0f && (((i64)y) & 1);
    return ((bmf_f32_bits(x) >> 31) && odd) ? ref_neg(v) : v;
}

static ref_dd ref_hypot(f32 x, f32 y) {
    return ref_sqrt(ref_add(ref_two_prod((f64)x, (f64)x), ref_two_prod((f64)y, (f64)y)));
}

// ---------------------------------------------------------------- exact references
// Integral rounding through i64 (|x| < 2^23 here); a zero result takes the sign of x (C99).
static f32 ref_integral(f32 x, i32 mode) {   // 0 floor, 1 ceil, 2 trunc, 3 round half away
    if (!(bmf_fabsf(x) < 8388608.0f) || x == 0.0f) return x;
    f64 v;
    if (mode == 3) {
        f64 a = (f64)bmf_fabsf(x) + 0.5;                   // exact in f64
        v = (f64)(i64)a;                                    // floor of a positive value
        if (x < 0.0f) v = -v;
    } else {
        v = (f64)(i64)x;                                    // trunc
        if (mode == 0 && v > (f64)x) v -= 1.0;
        if (mode == 1 && v < (f64)x) v += 1.0;
    }
    f32 out = (f32)v;
    if (out == 0.0f) out = (bmf_f32_bits(x) >> 31) ? -0.0f : 0.0f;
    return out;
}

// fmod by one doubling step at a time: (mx * 2^(ex-ey)) mod my, then scaled in f64 (exact).
static f32 ref_fmod(f32 x, f32 y) {   // finite, y != 0
    u32 ux = bmf_f32_bits(x) & 0x7FFFFFFFu, uy = bmf_f32_bits(y) & 0x7FFFFFFFu;
    b32 negative = bmf_f32_bits(x) >> 31;
    if (ux < uy) return x;
    i32 ex = (i32)(ux >> 23), ey = (i32)(uy >> 23);
    u64 mx = ux & 0x007FFFFFu, my = uy & 0x007FFFFFu;
    if (ex) mx |= 0x00800000u; else ex = 1;
    if (ey) my |= 0x00800000u; else ey = 1;
    u64 r = mx % my;
    for (i32 i = 0; i < ex - ey; i++) { r <<= 1; if (r >= my) r -= my; }
    f32 out = (f32)((f64)r * ref_pow2(ey - 150));
    return negative ? -out : out;
}

// Error of `got` against the reference in units of the f32 ULP at the reference value
// (2^-149 for the denormal range). A reference beyond the f32 range expects Inf.
static f64 ref_ulp_error(f32 got, ref_dd want) {
    f64 aw = ref_abs64(want.hi);
    if (aw >= 0x1.ffffffp+127) {                           // rounds to Inf
        b32 same_sign = (bmf_f32_bits(got) >> 31) == (want.hi < 0);
        return (bmf_fabsf(got) == bmf_f32_from_bits(0x7F800000u) && same_sign) ? 0.0 : 1e30;
    }
    if (!(got == got) || bmf_fabsf(got) == bmf_f32_from_bits(0x7F800000u)) return 1e30;
    i32 e = (i32)((bmf_f64_bits(aw) >> 52) & 0x7FF) - 1023;
    f64 ulp = e < -126 ? ref_pow2(-149) : ref_pow2(e - 23);
    f64 diff = ((f64)got - want.hi) - want.lo;
    return ref_abs64(diff) / ulp;
}
