// Tests for src/base/base_hash.*, base_rng.h and base_sort.* (task 1A-07).

static u64 hrs_xorshift(u64* x) { *x ^= *x << 13; *x ^= *x >> 7; *x ^= *x << 17; return *x; }

// ---------------------------------------------------------------- hash
static void test_hash_stream_equals_oneshot(bmf_test* t) {
    static u8 data[1000];
    u64 x = 99;
    for (i32 i = 0; i < 1000; i++) data[i] = (u8)hrs_xorshift(&x);
    i32 bad = 0;
    for (i32 len = 0; len <= 1000; len += (len < 80 ? 1 : 37)) {
        u64 one = bmf_hash(data, len, 7);
        for (i32 split_try = 0; split_try < 6; split_try++) {   // random chunkings
            bmf_hasher h;
            bmf_hasher_init(&h, 7);
            i32 at = 0;
            while (at < len) {
                i32 chunk = 1 + (i32)(hrs_xorshift(&x) % 70);
                if (chunk > len - at) chunk = len - at;
                bmf_hasher_update(&h, data + at, chunk);
                at += chunk;
            }
            if (bmf_hasher_final(&h) != one) bad++;
        }
    }
    bmf_check_eq_i64(t, bad, 0, __FILE__, __LINE__, "streaming result differs from one-shot");
    bmf_check(t, bmf_hash(data, 100, 1) != bmf_hash(data, 100, 2), __FILE__, __LINE__, "seed changes the hash");
    bmf_check(t, bmf_hash(data, 0, 0) != bmf_hash(data, 1, 0), __FILE__, __LINE__, "length matters");
    u8 zeros[16] = { 0 };
    bmf_check(t, bmf_hash(zeros, 15, 0) != bmf_hash(zeros, 16, 0), __FILE__, __LINE__, "trailing zero bytes matter");
}

// Strict avalanche: flipping any single input bit flips each output bit with probability ~1/2.
// For every (input bit, output bit) pair over 400 random inputs, the flip rate must be in
// [0.35, 0.65] (expected std dev 0.025, so 6 sigma).
static void test_hash_avalanche(bmf_test* t) {
    enum { LEN = 24, TRIALS = 400 };
    static i32 flips[LEN * 8][64];
    bmf_memset(flips, 0, sizeof flips);
    u64 x = 4242;
    for (i32 trial = 0; trial < TRIALS; trial++) {
        u8 in[LEN];
        for (i32 i = 0; i < LEN; i++) in[i] = (u8)hrs_xorshift(&x);
        u64 base = bmf_hash(in, LEN, 0);
        for (i32 bit = 0; bit < LEN * 8; bit++) {
            in[bit >> 3] ^= (u8)(1u << (bit & 7));
            u64 d = bmf_hash(in, LEN, 0) ^ base;
            in[bit >> 3] ^= (u8)(1u << (bit & 7));
            for (i32 o = 0; o < 64; o++) flips[bit][o] += (i32)((d >> o) & 1);
        }
    }
    i32 worst_low = TRIALS, worst_high = 0;
    for (i32 bit = 0; bit < LEN * 8; bit++)
        for (i32 o = 0; o < 64; o++) {
            if (flips[bit][o] < worst_low) worst_low = flips[bit][o];
            if (flips[bit][o] > worst_high) worst_high = flips[bit][o];
        }
    bmf_check(t, worst_low >= TRIALS * 35 / 100, __FILE__, __LINE__, "some output bit flips too rarely");
    bmf_check(t, worst_high <= TRIALS * 65 / 100, __FILE__, __LINE__, "some output bit flips too often");
}

static void test_hash_crc32_adler32_vectors(bmf_test* t) {
    const char* check = "123456789";
    bmf_check_eq_u64(t, bmf_crc32(0, check, 9), 0xCBF43926u, __FILE__, __LINE__, "CRC-32 check value");
    bmf_check_eq_u64(t, bmf_crc32(bmf_crc32(0, check, 4), check + 4, 5), 0xCBF43926u, __FILE__, __LINE__, "CRC-32 continued");
    bmf_check_eq_u64(t, bmf_crc32(0, check, 0), 0, __FILE__, __LINE__, "CRC-32 of nothing");
    bmf_check_eq_u64(t, bmf_adler32(1, "Wikipedia", 9), 0x11E60398u, __FILE__, __LINE__, "Adler-32 of 'Wikipedia'");
    static u8 big[100000];
    for (i32 i = 0; i < 100000; i++) big[i] = 0xFF;   // worst case for the deferred modulo
    u32 a = 1, b = 0;
    for (i32 i = 0; i < 100000; i++) { a = (a + big[i]) % 65521u; b = (b + a) % 65521u; }
    bmf_check_eq_u64(t, bmf_adler32(1, big, 100000), (b << 16) | a, __FILE__, __LINE__, "Adler-32 vs per-byte modulo");
}

// ---------------------------------------------------------------- rng
// Reference output of the PCG32 demo program (pcg32_srandom(42, 54)).
static void test_rng_pcg32_vectors(bmf_test* t) {
    bmf_rng r;
    bmf_rng_seed(&r, 42, 54);
    static const u32 expected[6] = { 0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu };
    i32 bad = 0;
    for (i32 i = 0; i < 6; i++) if (bmf_rng_next_u32(&r) != expected[i]) bad++;
    bmf_check_eq_i64(t, bad, 0, __FILE__, __LINE__, "PCG32 demo sequence");
    bmf_check_eq_i64(t, (i64)sizeof(bmf_rng), 16, __FILE__, __LINE__, "rng state is 16 bytes");
}

static void test_rng_ranges(bmf_test* t) {
    bmf_rng r;
    bmf_rng_seed(&r, 1, 2);
    i32 counts[7] = { 0 }, out_of_range = 0;
    for (i32 i = 0; i < 70000; i++) {
        i32 v = bmf_rng_range_i32(&r, -3, 3);
        if (v < -3 || v > 3) out_of_range++; else counts[v + 3]++;
        f32 f = bmf_rng_f32(&r);
        if (!(f >= 0.0f && f < 1.0f)) out_of_range++;
    }
    bmf_check_eq_i64(t, out_of_range, 0, __FILE__, __LINE__, "values outside the range");
    i32 lopsided = 0;
    for (i32 i = 0; i < 7; i++) if (counts[i] < 9400 || counts[i] > 10600) lopsided++;   // 10000 +- 6 sigma
    bmf_check_eq_i64(t, lopsided, 0, __FILE__, __LINE__, "range_i32 buckets outside 10000 +- 600");
    i32 full = bmf_rng_range_i32(&r, -2147483647 - 1, 2147483647);   // bound wraps to 0: full range
    (void)full;
    bmf_check_eq_i64(t, bmf_rng_range_i32(&r, 5, 5), 5, __FILE__, __LINE__, "single-value range");
}

// ---------------------------------------------------------------- sort
static void test_sort_matches_insertion_and_is_stable(bmf_test* t) {
    enum { MAX_N = 5000 };
    static u32 k32[MAX_N], p32[MAX_N], rk32[MAX_N], rp32[MAX_N], tk32[MAX_N], tp32[MAX_N];
    static u64 k64[MAX_N], rk64[MAX_N], tk64[MAX_N];
    static const i32 sizes[9] = { 0, 1, 2, 31, 32, 33, 100, 1000, MAX_N };
    u64 x = 31337;
    i32 bad32 = 0, bad64 = 0, unstable = 0;
    for (i32 si = 0; si < 9; si++) {
        for (i32 mode = 0; mode < 4; mode++) {   // 0 random, 1 few distinct, 2 high bits only, 3 constant
            i32 n = sizes[si];
            for (i32 i = 0; i < n; i++) {
                u64 r = hrs_xorshift(&x);
                u64 key = mode == 0 ? r : mode == 1 ? r % 5 : mode == 2 ? (r % 7) << 56 : 42;
                k64[i] = rk64[i] = key;
                k32[i] = rk32[i] = (u32)(mode == 2 ? key >> 32 : key);
                p32[i] = rp32[i] = (u32)i;   // payload = original index: detects instability
            }
            bmf_sort_u32(k32, p32, n, tk32, tp32);
            bmf_insertion_sort_u32(rk32, rp32, n);
            for (i32 i = 0; i < n; i++) if (k32[i] != rk32[i] || p32[i] != rp32[i]) { bad32++; break; }
            for (i32 i = 1; i < n; i++) if (k32[i] == k32[i - 1] && p32[i] < p32[i - 1]) { unstable++; break; }
            for (i32 i = 0; i < n; i++) p32[i] = rp32[i] = (u32)i;
            bmf_sort_u64(k64, p32, n, tk64, tp32);
            bmf_insertion_sort_u64(rk64, rp32, n);
            for (i32 i = 0; i < n; i++) if (k64[i] != rk64[i] || p32[i] != rp32[i]) { bad64++; break; }
        }
    }
    bmf_check_eq_i64(t, bad32, 0, __FILE__, __LINE__, "u32 radix sort differs from insertion sort");
    bmf_check_eq_i64(t, bad64, 0, __FILE__, __LINE__, "u64 radix sort differs from insertion sort");
    bmf_check_eq_i64(t, unstable, 0, __FILE__, __LINE__, "equal keys reordered");
    // Keys only (payload 0).
    for (i32 i = 0; i < 1000; i++) k32[i] = rk32[i] = (u32)hrs_xorshift(&x);
    bmf_sort_u32(k32, 0, 1000, tk32, 0);
    bmf_insertion_sort_u32(rk32, 0, 1000);
    bmf_check_eq_mem(t, k32, rk32, 4000, __FILE__, __LINE__, "keys-only sort");
}
