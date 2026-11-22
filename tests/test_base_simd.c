// Tests for the SIMD section of src/bmf.h (task 1A-05): every operation equals the scalar code bit for bit.

static u32 simd_bits(f32 f) { u32 u; __builtin_memcpy(&u, &f, 4); return u; }
static f32 simd_from_bits(u32 u) { f32 f; __builtin_memcpy(&f, &u, 4); return f; }

// Random finite floats across the whole exponent range, plus ±0, denormals, ±1 and extremes.
static f32 simd_random_float(u32* x) {
    *x ^= *x << 13; *x ^= *x >> 17; *x ^= *x << 5;
    static const u32 specials[10] = { 0x00000000u, 0x80000000u, 0x00000001u, 0x807FFFFFu, 0x3F800000u,
                                      0xBF800000u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x00800000u, 0x3F000000u };
    if ((*x & 15) == 0) return simd_from_bits(specials[(*x >> 4) % 10]);
    u32 u = *x * 2654435761u;
    if (((u >> 23) & 0xFF) == 0xFF) u &= ~(1u << 30);   // no NaN/Inf inputs (D-37 rule 11)
    return simd_from_bits(u);
}

static void test_simd_matches_scalar(bmf_test* t) {
    u32 x = 777;
    i32 bad[10] = { 0 };
    for (i32 iter = 0; iter < 50000; iter++) {
        f32 a[4], b[4];
        for (i32 i = 0; i < 4; i++) { a[i] = simd_random_float(&x); b[i] = simd_random_float(&x); }
        if (iter % 7 == 0) b[1] = a[1];   // equal lanes exercise the tie rules of min/max
        bmf_f32x4 va = bmf_f32x4_load_unaligned(a), vb = bmf_f32x4_load_unaligned(b);
        f32 r[10][4];
        bmf_f32x4_store_unaligned(r[0], va + vb);
        bmf_f32x4_store_unaligned(r[1], va - vb);
        bmf_f32x4_store_unaligned(r[2], va * vb);
        bmf_f32x4_store_unaligned(r[3], va / vb);
        bmf_f32x4_store_unaligned(r[4], bmf_f32x4_sqrt(bmf_f32x4_abs(va)));
        bmf_f32x4_store_unaligned(r[5], bmf_f32x4_min(va, vb));
        bmf_f32x4_store_unaligned(r[6], bmf_f32x4_max(va, vb));
        bmf_f32x4_store_unaligned(r[7], bmf_f32x4_select(va < vb, va * 2.0f, vb - 1.0f));
        bmf_f32x4_store_unaligned(r[8], bmf_f32x4_abs(va));
        bmf_i32x4 eq = va == vb;
        for (i32 i = 0; i < 4; i++) {
            f32 s[9];
            s[0] = a[i] + b[i];
            s[1] = a[i] - b[i];
            s[2] = a[i] * b[i];
            s[3] = a[i] / b[i];
            s[4] = __builtin_sqrtf(simd_from_bits(simd_bits(a[i]) & 0x7FFFFFFFu));
            s[5] = a[i] < b[i] ? a[i] : b[i];
            s[6] = a[i] > b[i] ? a[i] : b[i];
            s[7] = a[i] < b[i] ? a[i] * 2.0f : b[i] - 1.0f;
            s[8] = simd_from_bits(simd_bits(a[i]) & 0x7FFFFFFFu);
            for (i32 op = 0; op < 9; op++) if (simd_bits(r[op][i]) != simd_bits(s[op])) bad[op]++;
            if ((eq[i] != 0) != (a[i] == b[i]) || (eq[i] != 0 && eq[i] != -1)) bad[9]++;
        }
    }
    const char* names[10] = { "add", "sub", "mul", "div", "sqrt", "min", "max", "select", "abs", "compare" };
    for (i32 op = 0; op < 10; op++) bmf_check_eq_i64(t, bad[op], 0, __FILE__, __LINE__, names[op]);
}

static void test_simd_integer_and_memory(bmf_test* t) {
    __attribute__((aligned(16))) i32 in[8] = { -5, 3, 2147483647, -2147483647 - 1, 7, 0, -1, 100 };
    __attribute__((aligned(16))) i32 out[4];
    bmf_i32x4 a = bmf_i32x4_load(in), b = bmf_i32x4_load_unaligned(in + 3);
    bmf_i32x4_store(out, bmf_i32x4_min(a, b));
    i32 bad = 0;
    for (i32 i = 0; i < 4; i++) if (out[i] != (in[i] < in[i + 3] ? in[i] : in[i + 3])) bad++;
    bmf_i32x4_store(out, bmf_i32x4_max(a, b));
    for (i32 i = 0; i < 4; i++) if (out[i] != (in[i] > in[i + 3] ? in[i] : in[i + 3])) bad++;
    bmf_i32x4_store_unaligned(out, (a + b) ^ (a >> 3));   // wrapping add is defined for vectors
    for (i32 i = 0; i < 4; i++) if (out[i] != (i32)(((u32)in[i] + (u32)in[i + 3]) ^ (u32)(in[i] >> 3))) bad++;
    bmf_check_eq_i64(t, bad, 0, __FILE__, __LINE__, "integer min/max/add/xor/shift lanes");
    bmf_f32x4 v = bmf_f32x4_set(1.0f, 2.0f, 4.0f, 8.0f);
    bmf_check_eq_u64(t, simd_bits(bmf_f32x4_sum(v)), simd_bits(15.0f), __FILE__, __LINE__, "fixed-order sum");
    bmf_check(t, bmf_i32x4_any(v > 7.0f) && !bmf_i32x4_any(v > 8.0f), __FILE__, __LINE__, "any");
}
