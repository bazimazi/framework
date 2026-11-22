// Tests for src/base/base_builtins.c and the types section of src/bmf.h (task 1A-02).
// The mem functions are called through volatile function pointers so the compiler cannot
// replace the calls with inline code: this tests our implementations, not clang's.

static void* (*volatile builtins_memcpy)(void*, const void*, size_t) = memcpy;
static void* (*volatile builtins_memset)(void*, int, size_t) = memset;
static void* (*volatile builtins_memmove)(void*, const void*, size_t) = memmove;
static int (*volatile builtins_memcmp)(const void*, const void*, size_t) = memcmp;

enum { BUILTINS_BUF = 1024 + 64 };
static u8 builtins_src[BUILTINS_BUF], builtins_dst[BUILTINS_BUF], builtins_ref[BUILTINS_BUF];

static void builtins_fill_pattern(u8* p, i32 n, u32 seed) {
    u32 x = seed * 2654435761u + 1u;
    for (i32 i = 0; i < n; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; p[i] = (u8)x; }
}

// memcpy: every size 0..1024 at destination and source offsets 0..15; bytes around the
// destination range must stay untouched.
static void test_builtins_memcpy(bmf_test* t) {
    builtins_fill_pattern(builtins_src, BUILTINS_BUF, 1);
    i32 bad = 0;
    for (i32 size = 0; size <= 1024; size++) {
        for (i32 off = 0; off < 16; off++) {
            i32 src_off = (off * 7) & 15;
            builtins_fill_pattern(builtins_dst, BUILTINS_BUF, 2);
            builtins_fill_pattern(builtins_ref, BUILTINS_BUF, 2);
            for (i32 i = 0; i < size; i++) builtins_ref[off + i] = builtins_src[src_off + i];
            void* ret = builtins_memcpy(builtins_dst + off, builtins_src + src_off, (size_t)size);
            if (ret != builtins_dst + off) bad++;
            for (i32 i = 0; i < BUILTINS_BUF; i++) if (builtins_dst[i] != builtins_ref[i]) { bad++; break; }
        }
    }
    bmf_check_eq_i64(t, bad, 0, __FILE__, __LINE__, "memcpy mismatches over sizes 0..1024 x offsets 0..15");
}

static void test_builtins_memset(bmf_test* t) {
    i32 bad = 0;
    for (i32 size = 0; size <= 1024; size++) {
        for (i32 off = 0; off < 16; off++) {
            i32 value = (size * 31 + off) & 0xFF;
            builtins_fill_pattern(builtins_dst, BUILTINS_BUF, 3);
            builtins_fill_pattern(builtins_ref, BUILTINS_BUF, 3);
            for (i32 i = 0; i < size; i++) builtins_ref[off + i] = (u8)value;
            void* ret = builtins_memset(builtins_dst + off, value | 0x7F00, (size_t)size);   // only the low byte counts
            if (ret != builtins_dst + off) bad++;
            for (i32 i = 0; i < BUILTINS_BUF; i++) if (builtins_dst[i] != builtins_ref[i]) { bad++; break; }
        }
    }
    bmf_check_eq_i64(t, bad, 0, __FILE__, __LINE__, "memset mismatches over sizes 0..1024 x offsets 0..15");
}

// memmove: overlapping ranges in both directions, every shift -32..32, sizes 0..300.
static void test_builtins_memmove_overlap(bmf_test* t) {
    i32 bad = 0;
    for (i32 size = 0; size <= 300; size++) {
        for (i32 shift = -32; shift <= 32; shift++) {
            i32 src_off = 400, dst_off = 400 + shift;
            builtins_fill_pattern(builtins_dst, BUILTINS_BUF, 4);
            builtins_fill_pattern(builtins_ref, BUILTINS_BUF, 4);
            u8 tmp[300];
            for (i32 i = 0; i < size; i++) tmp[i] = builtins_ref[src_off + i];
            for (i32 i = 0; i < size; i++) builtins_ref[dst_off + i] = tmp[i];
            void* ret = builtins_memmove(builtins_dst + dst_off, builtins_dst + src_off, (size_t)size);
            if (ret != builtins_dst + dst_off) bad++;
            for (i32 i = 0; i < BUILTINS_BUF; i++) if (builtins_dst[i] != builtins_ref[i]) { bad++; break; }
        }
    }
    bmf_check_eq_i64(t, bad, 0, __FILE__, __LINE__, "memmove mismatches over sizes 0..300 x shifts -32..32");
}

static void test_builtins_memcmp(bmf_test* t) {
    builtins_fill_pattern(builtins_src, BUILTINS_BUF, 5);
    builtins_memcpy(builtins_dst, builtins_src, BUILTINS_BUF);
    i32 bad = 0;
    for (i32 size = 0; size <= 1024; size += 7) {
        if (builtins_memcmp(builtins_dst, builtins_src, (size_t)size) != 0) bad++;
    }
    for (i32 pos = 0; pos < 1024; pos++) {
        u8 old = builtins_dst[pos];
        builtins_dst[pos] = (u8)(old + 1);   // wraps 255 -> 0, which flips the expected sign
        i32 expected_sign = builtins_dst[pos] > old ? 1 : -1;
        i32 r = builtins_memcmp(builtins_dst, builtins_src, 1024);
        i32 sign = r > 0 ? 1 : r < 0 ? -1 : 0;
        if (sign != expected_sign) bad++;
        if (pos > 0 && builtins_memcmp(builtins_dst, builtins_src, (size_t)pos) != 0) bad++;   // prefix still equal
        builtins_dst[pos] = old;
    }
    bmf_check_eq_i64(t, bad, 0, __FILE__, __LINE__, "memcmp sign/equality errors");
}

// A frame larger than one page exercises __chkstk (x64): without a correct probe this faults.
__attribute__((noinline)) static u64 builtins_big_frame(i32 n) {
    volatile u8 big[256 * 1024];
    for (i32 i = 0; i < (i32)sizeof big; i += 4096) big[i] = (u8)(i >> 12);
    u64 sum = 0;
    for (i32 i = 0; i < (i32)sizeof big; i += 4096) sum += big[i];
    return sum + (u64)n;
}

static void test_builtins_stack_probe(bmf_test* t) {
    u64 sum = builtins_big_frame(0);
    bmf_check_eq_u64(t, sum, 64u * 63u / 2u * 1u, __FILE__, __LINE__, "256 KiB stack frame sum");
}

static void test_builtins_float_use(bmf_test* t) {   // links _fltused
    volatile f32 a = 1.5f, b = 2.25f;
    f32 c = a * b;
    bmf_check_eq_u64(t, *(volatile u32*)&c, 0x40580000u, __FILE__, __LINE__, "1.5 * 2.25 bits");
}

// Expected crash: BMF_ASSERT stops the process in debug builds.
static void test_builtins_assert_crashes(bmf_test* t) {
    volatile i32 zero = 0;
    BMF_ASSERT(zero == 1);
    bmf_check(t, 0, __FILE__, __LINE__, "BMF_ASSERT did not stop the process");
}

static void test_builtins_panic_crashes(bmf_test* t) {
    bmf_panic("expected panic from test_builtins_panic_crashes");
}
