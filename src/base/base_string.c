// base_string.c: see base_string.h.
//
// Float <-> text design (own design on top of IEEE f64 arithmetic, no tables beyond powers of 10):
// - Parsing: the first 19 significant digits give an f64 approximation r of the value with
//   relative error below 2^-51. Rounding r to f32 is the correctly rounded result unless r lies
//   within that error of an f32 rounding boundary (the midpoint between two neighbours); then an
//   exact big-integer path decides. For random input the exact path runs about once in 10^7.
// - Shortest formatting: the f32 x owns the rounding interval between the midpoints to its
//   neighbours (exact in f64). For decimal grids 10^k from coarse to fine, the integers n with
//   n * 10^k inside the (slightly widened) interval are candidates; the first grid with a
//   candidate that parses back to x exactly gives the shortest digits, and among those the one
//   closest to x is taken. Parsing is exact, so widening only costs a few extra checks.
#include "base_string.h"

// 10^0 .. 10^64 rounded to f64 (exact up to 10^22). The compiler rounds the literals correctly.
static const f64 STR_POW10[65] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
    1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22, 1e23, 1e24, 1e25, 1e26, 1e27, 1e28, 1e29, 1e30, 1e31,
    1e32, 1e33, 1e34, 1e35, 1e36, 1e37, 1e38, 1e39, 1e40, 1e41, 1e42, 1e43, 1e44, 1e45, 1e46, 1e47,
    1e48, 1e49, 1e50, 1e51, 1e52, 1e53, 1e54, 1e55, 1e56, 1e57, 1e58, 1e59, 1e60, 1e61, 1e62, 1e63,
    1e64,
};
static const u32 STR_POW10_U32[10] = { 1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u, 10000000u, 100000000u, 1000000000u };

// ================================================================ C strings, bmf_str
i32 bmf_cstr_len(const char* s) {
    i32 n = 0;
    while (s[n]) n++;
    return n;
}

b32 bmf_cstr_eq(const char* a, const char* b) {
    if (!a || !b) return a == b;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

bmf_str bmf_str_slice(bmf_str s, i32 start, i32 end) {
    if (start < 0) start = 0;
    if (end > s.len) end = s.len;
    if (start > end) start = end;
    return bmf_str_make(s.ptr + start, end - start);
}

b32 bmf_str_eq(bmf_str a, bmf_str b) {
    if (a.len != b.len) return 0;
    for (i32 i = 0; i < a.len; i++) if (a.ptr[i] != b.ptr[i]) return 0;
    return 1;
}

b32 bmf_str_eq_cstr(bmf_str a, const char* b) { return bmf_str_eq(a, bmf_str_from_cstr(b)); }

b32 bmf_str_starts_with(bmf_str s, bmf_str prefix) {
    return prefix.len <= s.len && bmf_str_eq(bmf_str_make(s.ptr, prefix.len), prefix);
}

b32 bmf_str_ends_with(bmf_str s, bmf_str suffix) {
    return suffix.len <= s.len && bmf_str_eq(bmf_str_make(s.ptr + s.len - suffix.len, suffix.len), suffix);
}

i32 bmf_str_find(bmf_str s, bmf_str needle, i32 from) {
    if (from < 0) from = 0;
    for (i32 i = from; i + needle.len <= s.len; i++) {
        i32 j = 0;
        while (j < needle.len && s.ptr[i + j] == needle.ptr[j]) j++;
        if (j == needle.len) return i;
    }
    return -1;
}

i32 bmf_str_find_byte(bmf_str s, u8 byte, i32 from) {
    for (i32 i = from < 0 ? 0 : from; i < s.len; i++) if (s.ptr[i] == byte) return i;
    return -1;
}

static b32 str_is_space(u8 c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

bmf_str bmf_str_trim(bmf_str s) {
    i32 start = 0, end = s.len;
    while (start < end && str_is_space(s.ptr[start])) start++;
    while (end > start && str_is_space(s.ptr[end - 1])) end--;
    return bmf_str_make(s.ptr + start, end - start);
}

b32 bmf_str_split_next(bmf_str_splitter* it, bmf_str* piece) {
    if (it->done) return 0;
    i32 at = bmf_str_find_byte(it->rest, it->separator, 0);
    if (at < 0) {
        *piece = it->rest;
        it->done = 1;
        return 1;
    }
    *piece = bmf_str_make(it->rest.ptr, at);
    it->rest = bmf_str_make(it->rest.ptr + at + 1, it->rest.len - at - 1);
    return 1;
}

// ================================================================ UTF-8 / UTF-16
i32 bmf_utf8_decode(bmf_str s, i32 at, u32* codepoint) {
    BMF_ASSERT(at >= 0 && at < s.len);
    const u8* p = s.ptr + at;
    i32 left = s.len - at;
    u32 b0 = p[0];
    if (b0 < 0x80) { *codepoint = b0; return 1; }
    i32 n;
    u32 c, min;
    if ((b0 & 0xE0) == 0xC0) { n = 2; c = b0 & 0x1F; min = 0x80; }
    else if ((b0 & 0xF0) == 0xE0) { n = 3; c = b0 & 0x0F; min = 0x800; }
    else if ((b0 & 0xF8) == 0xF0) { n = 4; c = b0 & 0x07; min = 0x10000; }
    else { *codepoint = 0xFFFD; return 1; }
    if (left < n) { *codepoint = 0xFFFD; return 1; }
    for (i32 i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) { *codepoint = 0xFFFD; return 1; }
        c = (c << 6) | (p[i] & 0x3Fu);
    }
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) { *codepoint = 0xFFFD; return 1; }
    *codepoint = c;
    return n;
}

i32 bmf_utf8_encode(u32 c, u8* out) {
    if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) c = 0xFFFD;
    if (c < 0x80) { out[0] = (u8)c; return 1; }
    if (c < 0x800) { out[0] = (u8)(0xC0 | (c >> 6)); out[1] = (u8)(0x80 | (c & 0x3F)); return 2; }
    if (c < 0x10000) {
        out[0] = (u8)(0xE0 | (c >> 12)); out[1] = (u8)(0x80 | ((c >> 6) & 0x3F)); out[2] = (u8)(0x80 | (c & 0x3F));
        return 3;
    }
    out[0] = (u8)(0xF0 | (c >> 18)); out[1] = (u8)(0x80 | ((c >> 12) & 0x3F));
    out[2] = (u8)(0x80 | ((c >> 6) & 0x3F)); out[3] = (u8)(0x80 | (c & 0x3F));
    return 4;
}

i32 bmf_utf16_to_utf8(const u16* src, i32 src_len, u8* dst, i32 capacity) {
    i32 needed = 0;
    b32 truncated = 0;
    for (i32 i = 0; i < src_len; i++) {
        u32 c = src[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < src_len && src[i + 1] >= 0xDC00 && src[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (src[i + 1] - 0xDC00u);
            i++;
        } else if (c >= 0xD800 && c <= 0xDFFF) {
            c = 0xFFFD;                                    // unpaired surrogate
        }
        u8 bytes[4];
        i32 n = bmf_utf8_encode(c, bytes);
        if (!truncated && needed + n <= capacity) { for (i32 k = 0; k < n; k++) dst[needed + k] = bytes[k]; }
        else truncated = 1;                                // stop writing at a code point boundary
        needed += n;
    }
    return needed;
}

i32 bmf_utf8_to_utf16(bmf_str src, u16* dst, i32 capacity) {
    i32 needed = 0;
    b32 truncated = 0;
    for (i32 at = 0; at < src.len;) {
        u32 c;
        at += bmf_utf8_decode(src, at, &c);
        u16 units[2];
        i32 n = 1;
        if (c >= 0x10000) {
            c -= 0x10000;
            units[0] = (u16)(0xD800 + (c >> 10));
            units[1] = (u16)(0xDC00 + (c & 0x3FF));
            n = 2;
        } else {
            units[0] = (u16)c;
        }
        if (!truncated && needed + n <= capacity) { for (i32 k = 0; k < n; k++) dst[needed + k] = units[k]; }
        else truncated = 1;
        needed += n;
    }
    return needed;
}

// ================================================================ small big integers
// Little-endian 32-bit limbs; `used` has no leading zero limbs. 768 bits: enough for 120
// decimal digits times 2^150 and for 10^166 times 2^25 (see str_decimal_to_f32_exact).
enum { STR_BIG_LIMBS = 24 };
typedef struct str_big { u32 limb[STR_BIG_LIMBS]; i32 used; } str_big;

static void str_big_set_u64(str_big* a, u64 v) {
    a->limb[0] = (u32)v;
    a->limb[1] = (u32)(v >> 32);
    a->used = (v >> 32) ? 2 : (v ? 1 : 0);
}

static void str_big_mul_add_small(str_big* a, u32 m, u32 add) {   // a = a * m + add
    u64 carry = add;
    for (i32 i = 0; i < a->used; i++) {
        u64 p = (u64)a->limb[i] * m + carry;
        a->limb[i] = (u32)p;
        carry = p >> 32;
    }
    if (carry) {
        BMF_ASSERT(a->used < STR_BIG_LIMBS);
        a->limb[a->used++] = (u32)carry;
    }
}

static void str_big_mul_pow10(str_big* a, i32 n) {
    while (n >= 9) { str_big_mul_add_small(a, 1000000000u, 0); n -= 9; }
    if (n > 0) str_big_mul_add_small(a, STR_POW10_U32[n], 0);
}

static u32 str_big_div_small(str_big* a, u32 d) {   // a /= d, returns the remainder
    u64 rem = 0;
    for (i32 i = a->used - 1; i >= 0; i--) {
        u64 cur = (rem << 32) | a->limb[i];
        a->limb[i] = (u32)(cur / d);
        rem = cur % d;
    }
    while (a->used > 0 && a->limb[a->used - 1] == 0) a->used--;
    return (u32)rem;
}

static void str_big_shl(str_big* a, i32 bits) {
    BMF_ASSERT(bits >= 0);
    if (a->used == 0 || bits == 0) return;
    i32 words = bits / 32, shift = bits % 32;
    i32 new_used = a->used + words + (shift ? 1 : 0);
    BMF_ASSERT(new_used <= STR_BIG_LIMBS);
    for (i32 i = new_used - 1; i >= 0; i--) {
        i32 src = i - words;
        u32 hi = (src >= 0 && src < a->used) ? a->limb[src] : 0u;
        u32 lo = (src - 1 >= 0 && src - 1 < a->used) ? a->limb[src - 1] : 0u;
        a->limb[i] = shift ? (hi << shift) | (lo >> (32 - shift)) : hi;
    }
    a->used = new_used;
    while (a->used > 0 && a->limb[a->used - 1] == 0) a->used--;
}

static void str_big_shr1(str_big* a) {
    for (i32 i = 0; i < a->used; i++) a->limb[i] = (a->limb[i] >> 1) | (i + 1 < a->used ? a->limb[i + 1] << 31 : 0u);
    while (a->used > 0 && a->limb[a->used - 1] == 0) a->used--;
}

static i32 str_big_bitlen(const str_big* a) {
    if (a->used == 0) return 0;
    return 32 * a->used - __builtin_clz(a->limb[a->used - 1]);
}

static i32 str_big_cmp(const str_big* a, const str_big* b) {
    if (a->used != b->used) return a->used < b->used ? -1 : 1;
    for (i32 i = a->used - 1; i >= 0; i--) if (a->limb[i] != b->limb[i]) return a->limb[i] < b->limb[i] ? -1 : 1;
    return 0;
}

static void str_big_sub(str_big* a, const str_big* b) {   // a >= b
    u64 borrow = 0;
    for (i32 i = 0; i < a->used; i++) {
        u64 d = (u64)a->limb[i] - (i < b->used ? b->limb[i] : 0u) - borrow;
        a->limb[i] = (u32)d;
        borrow = d >> 63;
    }
    while (a->used > 0 && a->limb[a->used - 1] == 0) a->used--;
}

// Bits [lo, lo + count) of a (count <= 32), and whether any bit below lo is set.
static u32 str_big_bits(const str_big* a, i32 lo, i32 count, b32* below_nonzero) {
    u32 v = 0;
    for (i32 i = count - 1; i >= 0; i--) {
        i32 bit = lo + i;
        u32 b = (bit >= 0 && bit / 32 < a->used) ? (a->limb[bit / 32] >> (bit % 32)) & 1u : 0u;
        v = (v << 1) | b;
    }
    b32 nonzero = 0;
    for (i32 bit = 0; bit < lo && !nonzero; bit++) nonzero = (a->limb[bit / 32] >> (bit % 32)) & 1u;
    *below_nonzero = nonzero;
    return v;
}

// ================================================================ decimal -> f32
// A positive decimal 0.d1 d2 ... dn x 10^dec_exp (digits without leading zeros), plus `sticky`
// when nonzero digits beyond the 120 kept ones were dropped.
enum { STR_MAX_DIGITS = 120 };
typedef struct str_decimal {
    u8 digit[STR_MAX_DIGITS];
    i32 count;
    i32 dec_exp;
    b32 sticky;
} str_decimal;

// Assembles f32 bits from a 25-bit Q = mantissa * 2 + round bit with leading bit at 2^exponent
// (normal), or from a denormal Q in units of 2^-150. Round to nearest, ties to even.
static f32 str_round_to_f32(u32 q, b32 sticky, i32 exponent, b32 denormal) {
    u32 m = q >> 1;
    if ((q & 1u) && (sticky || (m & 1u))) m++;
    if (denormal) return bmf_f32_from_bits(m);           // m == 2^23 is the smallest normal: same bits
    if (exponent > 127) return bmf_f32_from_bits(0x7F800000u);
    u32 bits = ((u32)(exponent + 127) << 23) + (m - 0x00800000u);   // a carry into the exponent is right
    return bmf_f32_from_bits(bits >= 0x7F800000u ? 0x7F800000u : bits);
}

// Exact conversion with big integers. Precondition: -46 < dec_exp < 40, count >= 1.
static f32 str_decimal_to_f32_exact(const str_decimal* d) {
    str_big digits;
    str_big_set_u64(&digits, 0);
    for (i32 i = 0; i < d->count; i++) str_big_mul_add_small(&digits, 10, d->digit[i]);
    i32 q = d->dec_exp - d->count;                       // value = digits * 10^q
    if (d->sticky) { str_big_mul_add_small(&digits, 10, 1); q -= 1; }   // just above the kept digits
    if (q >= 0) {
        str_big_mul_pow10(&digits, q);
        i32 len = str_big_bitlen(&digits);
        if (len > 129) return bmf_f32_from_bits(0x7F800000u);
        b32 below = 0;
        u32 top = len >= 25 ? str_big_bits(&digits, len - 25, 25, &below) : digits.limb[0] << (25 - len);
        return str_round_to_f32(top, below, len - 1, 0);
    }
    str_big p;
    str_big_set_u64(&p, 1);
    str_big_mul_pow10(&p, -q);
    i32 s = str_big_bitlen(&p) - str_big_bitlen(&digits) + 24;   // Q = digits * 2^s / p is in [2^23, 2^25)
    b32 denormal = 0;
    for (i32 attempt = 0; attempt < 3; attempt++) {
        if (24 - s < -126 || denormal) { s = 150; denormal = 1; }   // units of 2^-150
        str_big n = digits;
        str_big divisor = p;
        if (s >= 0) str_big_shl(&n, s);                  // digits * 2^s / p: scale whichever side
        else str_big_shl(&divisor, -s);
        str_big_shl(&divisor, 25);
        u32 quotient = 0;
        for (i32 bit = 25; bit >= 0; bit--) {             // binary long division, 26 quotient bits
            if (str_big_cmp(&n, &divisor) >= 0) { str_big_sub(&n, &divisor); quotient |= 1u << bit; }
            str_big_shr1(&divisor);
        }
        if (!denormal && quotient < (1u << 24)) { s++; continue; }
        if (!denormal && quotient >= (1u << 25)) { s--; continue; }
        return str_round_to_f32(quotient, n.used != 0, 24 - s, denormal);
    }
    BMF_ASSERT(0);
    return 0.0f;
}

// The f64 midpoints between f and its neighbours. Infinity counts as 2^128.
static b32 str_near_f32_boundary(f64 r, f32 f, f64 tolerance) {
    u32 b = bmf_f32_bits(f);
    if (b >= 0x7F800000u) return r - 0x1.ffffffp+127 <= tolerance;   // Inf: was it really above the cut?
    f64 fd = (f64)f;
    f64 up = b == 0x7F7FFFFFu ? 0x1p+128 : (f64)bmf_f32_from_bits(b + 1);
    f64 mid_up = (fd + up) * 0.5;                        // exact in f64
    f64 d = r - mid_up;
    if (d <= tolerance && d >= -tolerance) return 1;
    if (b == 0) return 0;
    f64 mid_down = (fd + (f64)bmf_f32_from_bits(b - 1)) * 0.5;
    d = r - mid_down;
    return d <= tolerance && d >= -tolerance;
}

// Correctly rounded positive f32 of a decimal.
static f32 str_decimal_to_f32(const str_decimal* d) {
    if (d->count == 0) return 0.0f;
    if (d->dec_exp >= 40) return bmf_f32_from_bits(0x7F800000u);   // >= 10^39
    if (d->dec_exp <= -46) return 0.0f;                  // < 10^-46, below half the smallest denormal
    i32 k = d->count < 19 ? d->count : 19;
    u64 w = 0;
    for (i32 i = 0; i < k; i++) w = w * 10 + d->digit[i];
    i32 q = d->dec_exp - k;                              // -64 .. 39
    f64 r = q >= 0 ? (f64)w * STR_POW10[q] : (f64)w / STR_POW10[-q];   // relative error < 2^-51
    f32 f = (f32)r;
    if (!str_near_f32_boundary(r, f, r * 0x1p-50)) return f;
    return str_decimal_to_f32_exact(d);
}

// Correctly rounded f32 of n * 10^k for 1 <= n < 10^19 and -64 <= k <= 38 (the formatter's
// candidates): the same fast path without building a digit array.
static f32 str_u64_pow10_to_f32(u64 n, i32 k) {
    f64 r = k >= 0 ? (f64)n * STR_POW10[k] : (f64)n / STR_POW10[-k];
    f32 f = (f32)r;
    if (!str_near_f32_boundary(r, f, r * 0x1p-50)) return f;
    str_decimal d;
    char tmp[24];
    i32 len = 0;
    for (u64 t = n; t; t /= 10) tmp[len++] = (char)(t % 10);
    for (i32 i = 0; i < len; i++) d.digit[i] = (u8)tmp[len - 1 - i];
    d.count = len;
    d.dec_exp = k + len;
    d.sticky = 0;
    return str_decimal_to_f32(&d);
}

// ================================================================ f32 -> shortest digits
// For finite x > 0: writes n and k with x = round_to_f32(n * 10^k), n with the fewest digits
// (no trailing zeros) and, among those, closest to x.
static void str_f32_shortest(f32 x, u64* digits_out, i32* exp10_out) {
    u32 b = bmf_f32_bits(x);
    f64 xd = (f64)x;
    f64 lo_mid = (xd + (b > 0 ? (f64)bmf_f32_from_bits(b - 1) : 0.0)) * 0.5;
    f64 hi_mid = (xd + (b == 0x7F7FFFFFu ? 0x1p+128 : (f64)bmf_f32_from_bits(b + 1))) * 0.5;
    i32 e2 = (i32)((bmf_f64_bits(xd) >> 52) & 0x7FF) - 1023;
    i32 e10 = e2 >= 0 ? (e2 * 78913) >> 18 : -((-e2 * 78913 + 262143) >> 18);   // floor(e2 log10 2), may be 1 low
    // If grid 10^k holds a candidate, every finer grid holds it too, so the coarsest grid with a
    // candidate is found by binary search; then grids are tried from there towards finer ones
    // until a candidate verifies (a widened-interval candidate may fail the exact check).
    i32 k_lo = e10 - 9, k_hi = e10 + 1;                  // 10 digits always contain a candidate
    while (k_lo < k_hi) {
        i32 mid = (k_lo + k_hi + 1) / 2;
        f64 a = mid >= 0 ? lo_mid / STR_POW10[mid] : lo_mid * STR_POW10[-mid];
        f64 c = mid >= 0 ? hi_mid / STR_POW10[mid] : hi_mid * STR_POW10[-mid];
        f64 top = __builtin_floor(c * (1.0 + 0x1p-50));
        if (top >= 1.0 && top >= __builtin_ceil(a * (1.0 - 0x1p-50))) k_lo = mid; else k_hi = mid - 1;
    }
    for (i32 k = k_lo; k >= e10 - 9; k--) {              // grids 10^k from coarse to fine
        f64 v, a, c;
        if (k >= 0) { v = xd / STR_POW10[k]; a = lo_mid / STR_POW10[k]; c = hi_mid / STR_POW10[k]; }
        else { v = xd * STR_POW10[-k]; a = lo_mid * STR_POW10[-k]; c = hi_mid * STR_POW10[-k]; }
        f64 n_lo = __builtin_ceil(a * (1.0 - 0x1p-50));
        f64 n_hi = __builtin_floor(c * (1.0 + 0x1p-50));
        if (n_hi < n_lo || n_hi < 1.0) continue;
        if (n_lo < 1.0) n_lo = 1.0;
        f64 nearest = __builtin_floor(v + 0.5);
        if (nearest - v == 0.5 && ((u64)nearest & 1u)) nearest -= 1.0;   // exact tie: the even one
        if (nearest < n_lo) nearest = n_lo;
        if (nearest > n_hi) nearest = n_hi;
        for (i32 step = 0; step < 64; step++) {          // nearest, then alternately farther candidates
            i32 distance = (step + 1) / 2;
            f64 cand = (step & 1) ? nearest + (f64)distance : nearest - (f64)distance;
            if (nearest + (f64)distance > n_hi && nearest - (f64)distance < n_lo) break;
            if (cand < n_lo || cand > n_hi) continue;
            u64 n = (u64)cand;                           // < 10^11: exact
            if (bmf_f32_bits(str_u64_pow10_to_f32(n, k)) != b) continue;
            while (n % 10 == 0) { n /= 10; k++; }
            *digits_out = n;
            *exp10_out = k;
            return;
        }
    }
    BMF_ASSERT(0);   // 9 significant digits always round-trip, so a grid above must have matched
    *digits_out = 0;
    *exp10_out = 0;
}

// ================================================================ output helper
typedef struct str_out { char* buf; i32 capacity; i32 len; } str_out;

static void str_put_byte(str_out* o, char c) {
    if (o->len < o->capacity - 1) o->buf[o->len] = c;
    o->len++;
}
static void str_put_bytes(str_out* o, const char* s, i32 n) { for (i32 i = 0; i < n; i++) str_put_byte(o, s[i]); }

// Writes body (with an optional sign byte, 0 for none) into a field of `width`.
static void str_put_field(str_out* o, char sign, const char* body, i32 body_len, i32 width, b32 left, b32 zero) {
    i32 total = body_len + (sign ? 1 : 0);
    i32 pad = width > total ? width - total : 0;
    if (!left && !zero) for (i32 i = 0; i < pad; i++) str_put_byte(o, ' ');
    if (sign) str_put_byte(o, sign);
    if (!left && zero) for (i32 i = 0; i < pad; i++) str_put_byte(o, '0');
    str_put_bytes(o, body, body_len);
    if (left) for (i32 i = 0; i < pad; i++) str_put_byte(o, ' ');
}

static i32 str_u64_digits(u64 v, char* out, u32 base) {   // returns length; out needs 20 bytes
    char tmp[24];
    i32 n = 0;
    do { tmp[n++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
    for (i32 i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    return n;
}

// Shortest text without sign handling of special values; JS Number notation.
static i32 str_f32_text(f32 x, char* out) {   // out needs 17 bytes
    u32 b = bmf_f32_bits(x);
    i32 n = 0;
    if ((b & 0x7F800000u) == 0x7F800000u) {
        const char* s = (b & 0x007FFFFFu) ? "nan" : ((b >> 31) ? "-inf" : "inf");
        while (*s) out[n++] = *s++;
        return n;
    }
    if (b >> 31) out[n++] = '-';
    if ((b & 0x7FFFFFFFu) == 0) { out[n++] = '0'; return n; }
    u64 digits;
    i32 k;
    str_f32_shortest(bmf_fabsf(x), &digits, &k);
    char d[24];
    i32 len = str_u64_digits(digits, d, 10);
    i32 point = len + k;                                 // value = 0.d1...dlen x 10^point
    if (len <= point && point <= 21) {                   // integer: digits then zeros
        for (i32 i = 0; i < len; i++) out[n++] = d[i];
        for (i32 i = len; i < point; i++) out[n++] = '0';
    } else if (0 < point && point <= 21) {               // point inside the digits
        for (i32 i = 0; i < point; i++) out[n++] = d[i];
        out[n++] = '.';
        for (i32 i = point; i < len; i++) out[n++] = d[i];
    } else if (-6 < point && point <= 0) {               // 0.000ddd
        out[n++] = '0';
        out[n++] = '.';
        for (i32 i = 0; i < -point; i++) out[n++] = '0';
        for (i32 i = 0; i < len; i++) out[n++] = d[i];
    } else {                                             // d.ddde+-x
        out[n++] = d[0];
        if (len > 1) { out[n++] = '.'; for (i32 i = 1; i < len; i++) out[n++] = d[i]; }
        out[n++] = 'e';
        i32 e = point - 1;
        out[n++] = e < 0 ? '-' : '+';
        if (e < 0) e = -e;
        if (e >= 10) out[n++] = (char)('0' + e / 10);
        out[n++] = (char)('0' + e % 10);
    }
    return n;
}

i32 bmf_f32_to_text(f32 x, char* buf, i32 capacity) {
    char tmp[24];
    i32 n = str_f32_text(x, tmp);
    str_out o = { buf, capacity, 0 };
    str_put_bytes(&o, tmp, n);
    if (capacity > 0) buf[o.len < capacity ? o.len : capacity - 1] = 0;
    return n;
}

// Fixed decimals, exactly rounded half to even. Body without sign; returns length (out >= 64 bytes).
static i32 str_f32_fixed(f32 x, i32 decimals, char* out) {
    u32 b = bmf_f32_bits(x) & 0x7FFFFFFFu;
    u32 field = b >> 23;
    u64 m = field ? (b & 0x007FFFFFu) | 0x00800000u : (b & 0x007FFFFFu);
    i32 e = field ? (i32)field - 150 : -149;             // |x| = m * 2^e
    char digits[64];
    i32 len = 0;
    if (e >= 0) {                                        // integer: m * 2^e * 10^decimals, up to ~2^158
        str_big v;
        str_big_set_u64(&v, m);
        str_big_shl(&v, e);
        str_big_mul_pow10(&v, decimals);
        char rev[64];
        i32 r = 0;
        while (v.used > 0) {
            u32 chunk = str_big_div_small(&v, 1000000000u);
            for (i32 i = 0; i < 9; i++) { rev[r++] = (char)('0' + chunk % 10); chunk /= 10; }
        }
        while (r > 1 && rev[r - 1] == '0') r--;
        for (i32 i = 0; i < r; i++) digits[len++] = rev[r - 1 - i];
    } else {
        u64 num = m * STR_POW10_U32[decimals];           // < 2^54
        i32 sh = -e;
        u64 whole = 0;
        if (sh < 63) {
            whole = num >> sh;
            u64 rem = num & ((1ull << sh) - 1);
            u64 half = 1ull << (sh - 1);
            if (rem > half || (rem == half && (whole & 1))) whole++;
        }
        len = str_u64_digits(whole, digits, 10);
    }
    i32 n = 0;
    if (len <= decimals) {                               // pad to at least one integer digit
        out[n++] = '0';
        if (decimals > 0) out[n++] = '.';
        for (i32 i = len; i < decimals; i++) out[n++] = '0';
        for (i32 i = 0; i < len; i++) out[n++] = digits[i];
        return n;
    }
    for (i32 i = 0; i < len - decimals; i++) out[n++] = digits[i];
    if (decimals > 0) {
        out[n++] = '.';
        for (i32 i = len - decimals; i < len; i++) out[n++] = digits[i];
    }
    return n;
}

// ================================================================ bmf_fmt
i32 bmf_fmt_va(char* buf, i32 capacity, const char* fmt, __builtin_va_list args) {
    str_out o = { buf, capacity, 0 };
    for (const char* p = fmt; *p; p++) {
        if (*p != '%') { str_put_byte(&o, *p); continue; }
        const char* spec_start = p;
        p++;
        b32 left = 0, zero = 0;
        for (;; p++) {
            if (*p == '-') left = 1;
            else if (*p == '0') zero = 1;
            else break;
        }
        i32 width = 0;
        while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        i32 precision = -1;
        if (*p == '.') { p++; precision = 0; while (*p >= '0' && *p <= '9') precision = precision * 10 + (*p++ - '0'); }
        b32 wide = 0;
        if (*p == 'l') { wide = 1; p++; }
        char body[96];
        i32 n = 0;
        char sign = 0;
        switch (*p) {
        case 'i': {
            i64 v = wide ? __builtin_va_arg(args, i64) : (i64)__builtin_va_arg(args, i32);
            u64 mag = v < 0 ? (u64)0 - (u64)v : (u64)v;
            if (v < 0) sign = '-';
            n = str_u64_digits(mag, body, 10);
            break;
        }
        case 'u': {
            u64 v = wide ? __builtin_va_arg(args, u64) : (u64)__builtin_va_arg(args, u32);
            n = str_u64_digits(v, body, 10);
            break;
        }
        case 'x': {
            u64 v = wide ? __builtin_va_arg(args, u64) : (u64)__builtin_va_arg(args, u32);
            n = str_u64_digits(v, body, 16);
            break;
        }
        case 'f': {
            f32 v = (f32)__builtin_va_arg(args, f64);
            u32 bits = bmf_f32_bits(v);
            if (precision >= 0 && (bits & 0x7F800000u) != 0x7F800000u) {
                BMF_ASSERT(precision <= 9);
                if (precision > 9) precision = 9;
                if (bits >> 31) sign = '-';
                n = str_f32_fixed(v, precision, body);
            } else {
                n = str_f32_text(v, body);
                if (body[0] == '-') { sign = '-'; for (i32 i = 1; i < n; i++) body[i - 1] = body[i]; n--; }
            }
            break;
        }
        case 'c':
            body[n++] = (char)__builtin_va_arg(args, i32);
            zero = 0;
            break;
        case 's': {
            const char* s = __builtin_va_arg(args, const char*);
            if (!s) s = "(null)";
            i32 len = bmf_cstr_len(s);
            str_put_field(&o, 0, s, len, width, left, 0);
            continue;
        }
        case 'S': {
            bmf_str s = __builtin_va_arg(args, bmf_str);
            str_put_field(&o, 0, (const char*)s.ptr, s.len, width, left, 0);
            continue;
        }
        case 'v':
            if (p[1] == '2') {
                p++;
                bmf_v2 v = __builtin_va_arg(args, bmf_v2);
                body[n++] = '[';
                n += str_f32_text(v.x, body + n);
                body[n++] = ',';
                n += str_f32_text(v.y, body + n);
                body[n++] = ']';
                zero = 0;
                break;
            }
            BMF_ASSERT(0);                               // unknown format specifier
            str_put_bytes(&o, spec_start, (i32)(p - spec_start) + 1);
            continue;
        case '%':
            str_put_byte(&o, '%');
            continue;
        default:
            BMF_ASSERT(0);                               // unknown format specifier
            if (!*p) { str_put_bytes(&o, spec_start, (i32)(p - spec_start)); p--; continue; }
            str_put_bytes(&o, spec_start, (i32)(p - spec_start) + 1);
            continue;
        }
        str_put_field(&o, sign, body, n, width, left, zero);
    }
    if (capacity > 0) buf[o.len < capacity ? o.len : capacity - 1] = 0;
    return o.len;
}

i32 bmf_fmt_buf(char* buf, i32 capacity, const char* fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    i32 n = bmf_fmt_va(buf, capacity, fmt, args);
    __builtin_va_end(args);
    return n;
}

bmf_str bmf_fmt_arena(bmf_arena* arena, const char* fmt, ...) {
    __builtin_va_list args, again;
    __builtin_va_start(args, fmt);
    __builtin_va_copy(again, args);
    i32 n = bmf_fmt_va(0, 0, fmt, args);
    char* text = (char*)bmf_arena_push_aligned(arena, n + 1, 1);
    bmf_fmt_va(text, n + 1, fmt, again);
    __builtin_va_end(again);
    __builtin_va_end(args);
    return bmf_str_make(text, n);
}

// ================================================================ parsing
static bmf_err str_parse_integer(bmf_str s, i64 min, i64 max, i64* out) {
    i32 i = 0;
    b32 negative = 0;
    if (s.len > 0 && (s.ptr[0] == '+' || s.ptr[0] == '-')) { negative = s.ptr[0] == '-'; i = 1; }
    if (i >= s.len) return BMF_ERR_INVALID;
    u64 limit = negative ? (u64)0 - (u64)min : (u64)max;
    u64 v = 0;
    b32 too_big = 0;
    for (; i < s.len; i++) {
        u8 c = s.ptr[i];
        if (c < '0' || c > '9') return BMF_ERR_INVALID;
        if (v > (limit - (u64)(c - '0')) / 10) too_big = 1;   // v * 10 + digit > limit
        else v = v * 10 + (u64)(c - '0');
    }
    if (too_big) return BMF_ERR_RANGE;
    *out = negative ? (i64)((u64)0 - v) : (i64)v;
    return BMF_OK;
}

bmf_err bmf_parse_i32(bmf_str s, i32* out) {
    i64 v;
    bmf_err e = str_parse_integer(s, -2147483647 - 1, 2147483647, &v);
    if (e == BMF_OK) *out = (i32)v;
    return e;
}

bmf_err bmf_parse_i64(bmf_str s, i64* out) {
    return str_parse_integer(s, -9223372036854775807ll - 1, 9223372036854775807ll, out);
}

static b32 str_eq_nocase(bmf_str s, i32 at, const char* word) {
    i32 n = bmf_cstr_len(word);
    if (s.len - at != n) return 0;
    for (i32 i = 0; i < n; i++) {
        u8 c = s.ptr[at + i];
        if (c >= 'A' && c <= 'Z') c = (u8)(c + 32);
        if (c != (u8)word[i]) return 0;
    }
    return 1;
}

bmf_err bmf_parse_f32(bmf_str s, f32* out) {
    i32 i = 0;
    b32 negative = 0;
    if (s.len > 0 && (s.ptr[0] == '+' || s.ptr[0] == '-')) { negative = s.ptr[0] == '-'; i = 1; }
    if (str_eq_nocase(s, i, "inf") || str_eq_nocase(s, i, "infinity")) {
        *out = bmf_f32_from_bits(0x7F800000u | (negative ? 0x80000000u : 0u));
        return BMF_OK;
    }
    if (str_eq_nocase(s, i, "nan")) { *out = bmf_f32_from_bits(0x7FC00000u); return BMF_OK; }
    str_decimal d;
    d.count = 0;
    d.dec_exp = 0;
    d.sticky = 0;
    b32 any_digit = 0, seen_point = 0;
    for (; i < s.len; i++) {
        u8 c = s.ptr[i];
        if (c == '.') {
            if (seen_point) return BMF_ERR_INVALID;
            seen_point = 1;
            continue;
        }
        if (c < '0' || c > '9') break;
        any_digit = 1;
        if (c == '0' && d.count == 0) {                  // leading zero: only moves the point
            if (seen_point) d.dec_exp--;
            continue;
        }
        if (d.count < STR_MAX_DIGITS) d.digit[d.count++] = (u8)(c - '0');
        else if (c != '0') d.sticky = 1;
        if (!seen_point) d.dec_exp++;
    }
    if (!any_digit) return BMF_ERR_INVALID;
    if (i < s.len && (s.ptr[i] == 'e' || s.ptr[i] == 'E')) {
        i++;
        b32 exp_negative = 0;
        if (i < s.len && (s.ptr[i] == '+' || s.ptr[i] == '-')) { exp_negative = s.ptr[i] == '-'; i++; }
        if (i >= s.len) return BMF_ERR_INVALID;
        i32 e = 0;
        for (; i < s.len; i++) {
            u8 c = s.ptr[i];
            if (c < '0' || c > '9') return BMF_ERR_INVALID;
            if (e < 100000) e = e * 10 + (c - '0');      // clamped: far beyond any f32 exponent
        }
        d.dec_exp += exp_negative ? -e : e;
    }
    if (i != s.len) return BMF_ERR_INVALID;
    while (d.count > 0 && d.digit[d.count - 1] == 0 && !d.sticky) d.count--;   // trailing zeros carry no value
    if (d.count == 0) { *out = negative ? -0.0f : 0.0f; return BMF_OK; }
    f32 v = str_decimal_to_f32(&d);
    *out = negative ? -v : v;
    return bmf_f32_bits(v) == 0x7F800000u ? BMF_ERR_RANGE : BMF_OK;
}

// ================================================================ JSON writer
void bmf_json_init(bmf_json_writer* w, char* buf, i32 capacity) {
    BMF_ASSERT(capacity > 0);
    w->buf = buf;
    w->len = 0;
    w->capacity = capacity;
    w->depth = 0;
    w->has_items = 0;
    w->after_key = 0;
    w->overflow = 0;
    buf[0] = 0;
}

static void json_put(bmf_json_writer* w, const char* s, i32 n) {
    for (i32 i = 0; i < n; i++) {
        if (w->len >= w->capacity - 1) { w->overflow = 1; break; }
        w->buf[w->len++] = s[i];
    }
    w->buf[w->len] = 0;
}

static void json_before_value(bmf_json_writer* w) {
    if (w->after_key) { w->after_key = 0; return; }
    if (w->depth == 0) return;
    u32 bit = 1u << w->depth;
    if (w->has_items & bit) json_put(w, ",", 1);
    w->has_items |= bit;
}

static void json_open(bmf_json_writer* w, const char* bracket) {
    json_before_value(w);
    json_put(w, bracket, 1);
    w->depth++;
    BMF_ASSERT(w->depth < 32);
    w->has_items &= ~(1u << w->depth);
}

static void json_close(bmf_json_writer* w, const char* bracket) {
    BMF_ASSERT(w->depth > 0 && !w->after_key);
    w->depth--;
    json_put(w, bracket, 1);
}

void bmf_json_object_begin(bmf_json_writer* w) { json_open(w, "{"); }
void bmf_json_object_end(bmf_json_writer* w) { json_close(w, "}"); }
void bmf_json_array_begin(bmf_json_writer* w) { json_open(w, "["); }
void bmf_json_array_end(bmf_json_writer* w) { json_close(w, "]"); }

static void json_put_string(bmf_json_writer* w, bmf_str s) {
    json_put(w, "\"", 1);
    for (i32 at = 0; at < s.len;) {
        u32 c;
        at += bmf_utf8_decode(s, at, &c);
        char esc[8];
        if (c == '"') json_put(w, "\\\"", 2);
        else if (c == '\\') json_put(w, "\\\\", 2);
        else if (c == '\n') json_put(w, "\\n", 2);
        else if (c == '\r') json_put(w, "\\r", 2);
        else if (c == '\t') json_put(w, "\\t", 2);
        else if (c < 0x20) {
            esc[0] = '\\'; esc[1] = 'u'; esc[2] = '0'; esc[3] = '0';
            esc[4] = "0123456789abcdef"[c >> 4]; esc[5] = "0123456789abcdef"[c & 15];
            json_put(w, esc, 6);
        } else {
            u8 bytes[4];
            i32 n = bmf_utf8_encode(c, bytes);           // invalid input arrives here as U+FFFD
            json_put(w, (const char*)bytes, n);
        }
    }
    json_put(w, "\"", 1);
}

void bmf_json_key(bmf_json_writer* w, const char* key) {
    BMF_ASSERT(!w->after_key);
    json_before_value(w);
    json_put_string(w, bmf_str_from_cstr(key));
    json_put(w, ":", 1);
    w->after_key = 1;
}

void bmf_json_str(bmf_json_writer* w, bmf_str s) { json_before_value(w); json_put_string(w, s); }

void bmf_json_string(bmf_json_writer* w, const char* s) {
    if (!s) { bmf_json_null(w); return; }
    bmf_json_str(w, bmf_str_from_cstr(s));
}

void bmf_json_i64(bmf_json_writer* w, i64 v) {
    char tmp[24];
    i32 n = 0;
    if (v < 0) tmp[n++] = '-';
    n += str_u64_digits(v < 0 ? (u64)0 - (u64)v : (u64)v, tmp + n, 10);
    json_before_value(w);
    json_put(w, tmp, n);
}

void bmf_json_u64(bmf_json_writer* w, u64 v) {
    char tmp[24];
    i32 n = str_u64_digits(v, tmp, 10);
    json_before_value(w);
    json_put(w, tmp, n);
}

void bmf_json_f32(bmf_json_writer* w, f32 v) {
    u32 b = bmf_f32_bits(v);
    if ((b & 0x7F800000u) == 0x7F800000u) {
        bmf_json_string(w, (b & 0x007FFFFFu) ? "NaN" : ((b >> 31) ? "-Infinity" : "Infinity"));
        return;
    }
    char tmp[24];
    i32 n = str_f32_text(v, tmp);
    json_before_value(w);
    json_put(w, tmp, n);
}

void bmf_json_bool(bmf_json_writer* w, b32 v) { json_before_value(w); json_put(w, v ? "true" : "false", v ? 4 : 5); }
void bmf_json_null(bmf_json_writer* w) { json_before_value(w); json_put(w, "null", 4); }
void bmf_json_raw(bmf_json_writer* w, const char* json) { json_before_value(w); json_put(w, json, bmf_cstr_len(json)); }
