// Spike P0-06: cross-platform float determinism. Throwaway.
// One file, built for win64 (CRT-free exe) and wasm32 (module driven by index.html).
// Build-time switches:
//   USE_SIMD     integration step through ext_vector_type(4) instead of scalar
//   BREAK_RSQRT  normalize contact normals with _mm_rsqrt_ss (x64 only)
//   BREAK_FTZ    set MXCSR FTZ|DAZ at start (x64 only)
//   BREAK_CAST   raw (i32) cast of an out-of-range float instead of the saturating helper
//   (BREAK_FMA is a flag change: -march=x86-64-v3 -ffp-contract=fast)

typedef signed char i8; typedef int i32; typedef long long i64;
typedef unsigned char u8; typedef unsigned u32; typedef unsigned long long u64;
typedef float f32; typedef double f64;
typedef f32 f32x4 __attribute__((ext_vector_type(4)));

#if defined(__x86_64__) && (defined(BREAK_RSQRT) || defined(BREAK_FTZ))
#include <immintrin.h>
#endif

// ---------------------------------------------------------------- deterministic math (draft)
// All functions use only + - * / sqrt floor and compares, in f64 internally, rounded to f32 once
// at the end. Every one of those operations is correctly rounded IEEE on x64 SSE2 and wasm.

static inline f64 det_floor64(f64 x) { return __builtin_floor(x); }   // roundsd (SSE4.1) / f64.floor

static inline i32 det_f32_to_i32(f32 x) {          // saturating, truncating, NaN -> 0 (D-37 rule 12)
    if (!(x == x)) return 0;
    if (x >= 2147483648.0f) return 2147483647;
    if (x <= -2147483648.0f) return (-2147483647 - 1);
    return (i32)x;                                   // in range: identical on every target
}

static const f64 DET_PI = 3.14159265358979323846;
static const f64 DET_PIO2_HI = 1.57079632673412561417e+00;   // first 33 bits of pi/2
static const f64 DET_PIO2_LO = 6.07710050650619224932e-11;   // pi/2 - PIO2_HI

// sin and cos on [-pi/4, pi/4], Taylor to x^15 / x^14 (error < 1e-16 relative there).
static f64 det_sin_kernel(f64 x) {
    f64 x2 = x * x;
    f64 p = 1.0 / 1307674368000.0;                        // 1/15!
    p = p * -x2 + 1.0 / 6227020800.0;                     // 1/13!
    p = p * -x2 + 1.0 / 39916800.0;                       // 1/11!
    p = p * -x2 + 1.0 / 362880.0;                         // 1/9!
    p = p * -x2 + 1.0 / 5040.0;                           // 1/7!
    p = p * -x2 + 1.0 / 120.0;                            // 1/5!
    p = p * -x2 + 1.0 / 6.0;                              // 1/3!
    return x - x * x2 * p;
}
static f64 det_cos_kernel(f64 x) {
    f64 x2 = x * x;
    f64 p = 1.0 / 87178291200.0;                          // 1/14!
    p = p * -x2 + 1.0 / 479001600.0;                      // 1/12!
    p = p * -x2 + 1.0 / 3628800.0;                        // 1/10!
    p = p * -x2 + 1.0 / 40320.0;                          // 1/8!
    p = p * -x2 + 1.0 / 720.0;                            // 1/6!
    p = p * -x2 + 1.0 / 24.0;                             // 1/4!
    p = p * -x2 + 1.0 / 2.0;                              // 1/2!
    return 1.0 - x2 * p;
}
static void det_sincosf(f32 xf, f32* s, f32* c) {
    f64 x = (f64)xf;
    f64 k = det_floor64(x * (2.0 / DET_PI) + 0.5);
    f64 r = (x - k * DET_PIO2_HI) - k * DET_PIO2_LO;
    i64 q = (i64)k & 3;                                   // |k| small for spike inputs; in range
    f64 sr = det_sin_kernel(r), cr = det_cos_kernel(r);
    f64 so, co;
    if (q == 0) { so = sr; co = cr; } else if (q == 1) { so = cr; co = -sr; }
    else if (q == 2) { so = -sr; co = -cr; } else { so = -cr; co = sr; }
    *s = (f32)so; *c = (f32)co;
}
static f32 det_sinf(f32 x) { f32 s, c; det_sincosf(x, &s, &c); return s; }
static f32 det_cosf(f32 x) { f32 s, c; det_sincosf(x, &s, &c); return c; }

// atan on [0, tan(pi/12)] by Taylor to t^19.
static f64 det_atan_kernel(f64 t) {
    f64 t2 = t * t;
    f64 p = 1.0 / 19.0;
    p = 1.0 / 17.0 - t2 * p; p = 1.0 / 15.0 - t2 * p; p = 1.0 / 13.0 - t2 * p; p = 1.0 / 11.0 - t2 * p;
    p = 1.0 / 9.0 - t2 * p;  p = 1.0 / 7.0 - t2 * p;  p = 1.0 / 5.0 - t2 * p;  p = 1.0 / 3.0 - t2 * p;
    return t - t * t2 * p;
}
static f64 det_atan_pos(f64 t) {                           // t >= 0
    f64 base = 0.0; int invert = 0;
    if (t > 1.0) { t = 1.0 / t; invert = 1; }
    if (t > 0.26794919243112270) {                        // tan(pi/12): shift by pi/6
        const f64 sqrt3 = 1.73205080756887729;
        t = (t * sqrt3 - 1.0) / (t + sqrt3);
        base = DET_PI / 6.0;
    }
    f64 a = base + det_atan_kernel(t);
    return invert ? DET_PI / 2.0 - a : a;
}
static f32 det_atan2f(f32 yf, f32 xf) {
    f64 y = (f64)yf, x = (f64)xf;
    if (x == 0.0 && y == 0.0) return 0.0f;
    f64 ay = y < 0 ? -y : y, ax = x < 0 ? -x : x;
    f64 a;
    if (ax >= ay) a = det_atan_pos(ay / ax); else a = DET_PI / 2.0 - det_atan_pos(ax / ay);
    if (x < 0) a = DET_PI - a;
    if (y < 0) a = -a;
    return (f32)a;
}

// exp: k = round(x/ln2), r = x - k ln2, Taylor to r^12, scale by 2^k via exponent bits.
static f32 det_expf(f32 xf) {
    f64 x = (f64)xf;
    if (x > 88.7) x = 88.7;
    if (x < -103.9) return 0.0f;
    const f64 LN2_HI = 6.93147180369123816490e-01, LN2_LO = 1.90821492927058770002e-10;
    f64 k = det_floor64(x * 1.44269504088896338700 + 0.5);
    f64 r = (x - k * LN2_HI) - k * LN2_LO;
    f64 p = 1.0 / 479001600.0;
    p = p * r + 1.0 / 39916800.0; p = p * r + 1.0 / 3628800.0; p = p * r + 1.0 / 362880.0;
    p = p * r + 1.0 / 40320.0;    p = p * r + 1.0 / 5040.0;    p = p * r + 1.0 / 720.0;
    p = p * r + 1.0 / 120.0;      p = p * r + 1.0 / 24.0;      p = p * r + 1.0 / 6.0;
    p = p * r + 0.5;              p = p * r + 1.0;             p = p * r + 1.0;
    i64 ki = (i64)k;
    union { u64 u; f64 d; } scale; scale.u = (u64)(ki + 1023) << 52;   // ki in [-150, 128]: normal f64
    return (f32)(p * scale.d);
}

// ---------------------------------------------------------------- RNG and hash
typedef struct { u64 state, inc; } pcg32;
static u32 pcg32_next(pcg32* r) {
    u64 old = r->state; r->state = old * 6364136223846793005ull + r->inc;
    u32 xs = (u32)(((old >> 18u) ^ old) >> 27u); u32 rot = (u32)(old >> 59u);
    return (xs >> rot) | (xs << ((-rot) & 31u));
}
static f32 pcg32_f01(pcg32* r) { return (f32)(pcg32_next(r) >> 8) * (1.0f / 16777216.0f); }

static u64 hash_bytes(u64 h, const void* data, i64 len) {   // multiply-xorshift over u64 words
    const u8* p = (const u8*)data;
    for (i64 i = 0; i + 8 <= len; i += 8) {
        u64 w; __builtin_memcpy(&w, p + i, 8);
        h ^= w; h *= 0x9E3779B97F4A7C15ull; h ^= h >> 29;
    }
    for (i64 i = len & ~7ll; i < len; i++) { h ^= p[i]; h *= 0x100000001B3ull; }
    return h;
}

// ---------------------------------------------------------------- simulation state (pointer-free)
#define N 2000
typedef struct {
    f32 px[N] __attribute__((aligned(64)));
    f32 py[N] __attribute__((aligned(64)));
    f32 vx[N] __attribute__((aligned(64)));
    f32 vy[N] __attribute__((aligned(64)));
    f32 radius[N];
    f32 heat[N];          // decays through the denormal range every few ticks (FTZ detector)
    f32 angle[N];         // last contact angle via atan2 (atan2 detector)
    i32 cell[N];          // float-to-int of a deliberately out-of-range value (cast detector)
    i64 tick;
    pcg32 rng;
} sim_state;

static sim_state S;
static i32 order[N];      // derived each tick (sort by x); not part of state

static const f32 WORLD_W = 10.0f, WORLD_H = 30.0f;

void det_init(u32 seed) {
    __builtin_memset(&S, 0, sizeof S);
    S.rng.state = 0; S.rng.inc = ((u64)seed << 1u) | 1u; pcg32_next(&S.rng); S.rng.state += 0x853c49e6748fea9bull; pcg32_next(&S.rng);
    for (i32 i = 0; i < N; i++) {
        S.radius[i] = 0.05f + 0.05f * pcg32_f01(&S.rng);
        S.px[i] = 0.2f + (WORLD_W - 0.4f) * pcg32_f01(&S.rng);
        S.py[i] = 0.2f + (WORLD_H - 0.4f) * pcg32_f01(&S.rng);
        S.vx[i] = (pcg32_f01(&S.rng) - 0.5f) * 4.0f;
        S.vy[i] = (pcg32_f01(&S.rng) - 0.5f) * 4.0f;
        order[i] = i;
    }
}

static void integrate(f32 dt, f32 gx, f32 gy, f32 damp) {
#ifdef USE_SIMD
    f32x4 vgx = gx * dt, vgy = gy * dt, vdamp = damp, vdt = dt;
    for (i32 i = 0; i < N; i += 4) {
        f32x4 vx = *(f32x4*)&S.vx[i], vy = *(f32x4*)&S.vy[i];
        f32x4 px = *(f32x4*)&S.px[i], py = *(f32x4*)&S.py[i];
        vx = (vx + vgx) * vdamp; vy = (vy + vgy) * vdamp;
        px = px + vx * vdt;      py = py + vy * vdt;
        *(f32x4*)&S.vx[i] = vx; *(f32x4*)&S.vy[i] = vy; *(f32x4*)&S.px[i] = px; *(f32x4*)&S.py[i] = py;
    }
#else
    f32 sgx = gx * dt, sgy = gy * dt;
    for (i32 i = 0; i < N; i++) {
        S.vx[i] = (S.vx[i] + sgx) * damp; S.vy[i] = (S.vy[i] + sgy) * damp;
        S.px[i] = S.px[i] + S.vx[i] * dt; S.py[i] = S.py[i] + S.vy[i] * dt;
    }
#endif
}

static inline f32 inv_len(f32 d2) {
#ifdef BREAK_RSQRT
    return _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(d2)));
#else
    return 1.0f / __builtin_sqrtf(d2);
#endif
}

void det_tick(void) {
    const f32 dt = 1.0f / 60.0f;
    f32 t = (f32)S.tick * dt;
    f32 sg, cg; det_sincosf(0.3f * t, &sg, &cg);
    f32 gx = 9.81f * 0.25f * sg, gy = -9.81f * (0.75f + 0.25f * cg);
    f32 damp = det_expf(-0.05f * dt);
    integrate(dt, gx, gy, damp);

    for (i32 i = 0; i < N; i++) S.heat[i] = S.heat[i] * 0.25f;   // walks through denormals to 0

    // walls
    for (i32 i = 0; i < N; i++) {
        f32 r = S.radius[i];
        if (S.px[i] < r) { S.px[i] = r; if (S.vx[i] < 0) S.vx[i] = -0.6f * S.vx[i]; }
        if (S.px[i] > WORLD_W - r) { S.px[i] = WORLD_W - r; if (S.vx[i] > 0) S.vx[i] = -0.6f * S.vx[i]; }
        if (S.py[i] < r) { S.py[i] = r; if (S.vy[i] < 0) S.vy[i] = -0.6f * S.vy[i]; }
        if (S.py[i] > WORLD_H - r) { S.py[i] = WORLD_H - r; if (S.vy[i] > 0) S.vy[i] = -0.6f * S.vy[i]; }
    }

    // broadphase: insertion sort of order[] by (px, index); nearly sorted each tick
    for (i32 i = 1; i < N; i++) {
        i32 k = order[i]; f32 kx = S.px[k]; i32 j = i - 1;
        while (j >= 0 && (S.px[order[j]] > kx || (S.px[order[j]] == kx && order[j] > k))) { order[j + 1] = order[j]; j--; }
        order[j + 1] = k;
    }
    // narrowphase + response, in sweep order (deterministic: depends only on state)
    for (i32 oi = 0; oi < N; oi++) {
        i32 a = order[oi];
        for (i32 oj = oi + 1; oj < N; oj++) {
            i32 b = order[oj];
            f32 dx = S.px[b] - S.px[a];
            f32 rs = S.radius[a] + S.radius[b];
            if (dx > rs) break;
            f32 dy = S.py[b] - S.py[a];
            f32 d2 = dx * dx + dy * dy;
            if (d2 >= rs * rs || d2 == 0.0f) continue;
            f32 il = inv_len(d2);
            f32 nx = dx * il, ny = dy * il;
            f32 dist = d2 * il;
            f32 pen = rs - dist;
            S.px[a] -= nx * pen * 0.5f; S.py[a] -= ny * pen * 0.5f;
            S.px[b] += nx * pen * 0.5f; S.py[b] += ny * pen * 0.5f;
            f32 rvn = (S.vx[b] - S.vx[a]) * nx + (S.vy[b] - S.vy[a]) * ny;
            if (rvn < 0.0f) {
                f32 j = -(1.0f + 0.5f) * rvn * 0.5f;
                S.vx[a] -= j * nx; S.vy[a] -= j * ny;
                S.vx[b] += j * nx; S.vy[b] += j * ny;
                S.heat[a] += 3.0e-38f * (-rvn); S.heat[b] += 3.0e-38f * (-rvn);
            }
            f32 ang = det_atan2f(ny, nx);
            S.angle[a] = ang; S.angle[b] = ang + 1.0f;
        }
    }
    for (i32 i = 0; i < N; i++) {
        f32 big = S.px[i] * 1.0e9f - 5.0e9f;          // out of i32 range for most bodies
#ifdef BREAK_CAST
        S.cell[i] = (i32)big;
#else
        S.cell[i] = det_f32_to_i32(big);
#endif
    }
    S.tick++;
}

u64 det_hash(void) { return hash_bytes(0x243F6A8885A308D3ull, &S, (i64)sizeof S); }

#ifdef __wasm__
__attribute__((export_name("init"))) void wasm_init(u32 seed) { det_init(seed); }
__attribute__((export_name("run"))) void wasm_run(i32 ticks) { for (i32 i = 0; i < ticks; i++) det_tick(); }
__attribute__((export_name("hash"))) u64 wasm_hash(void) { return det_hash(); }
__attribute__((export_name("probe"))) f32 wasm_probe(i32 which, f32 x, f32 y) {
    if (which == 0) return det_sinf(x); if (which == 1) return det_cosf(x);
    if (which == 2) return det_atan2f(y, x); return det_expf(x);
}
#else
// ---------------------------------------------------------------- win64 driver
#define WINAPI __attribute__((stdcall))
typedef void* HANDLE;
__declspec(dllimport) void WINAPI ExitProcess(u32 code);
__declspec(dllimport) HANDLE WINAPI GetStdHandle(u32 n);
__declspec(dllimport) i32 WINAPI WriteFile(HANDLE h, const void* buf, u32 n, u32* written, void* ov);
__declspec(dllimport) i32 WINAPI QueryPerformanceCounter(i64* c);
__declspec(dllimport) i32 WINAPI QueryPerformanceFrequency(i64* f);
static char obuf[1 << 16]; static i32 olen;
static void put(const char* s) { while (*s) obuf[olen++] = *s++; }
static void put_hex(u64 v) { for (i32 i = 15; i >= 0; i--) obuf[olen++] = "0123456789abcdef"[(v >> (i * 4)) & 15]; }
static void put_u(u64 v) { char b[24]; i32 i = 23; b[i] = 0; do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v); put(b + i); }
static void flush(void) { u32 w; WriteFile(GetStdHandle((u32)-11), obuf, (u32)olen, &w, 0); olen = 0; }

void bmf_win32_entry(void) {
#ifdef BREAK_FTZ
    _mm_setcsr(_mm_getcsr() | 0x8040);
#endif
    i64 f, t0, t1; QueryPerformanceFrequency(&f);
    det_init(12345);
    put("{\"hashes\":[");
    QueryPerformanceCounter(&t0);
    for (i32 block = 0; block < 100; block++) {
        for (i32 i = 0; i < 100; i++) det_tick();
        put(block ? ",\"" : "\""); put_hex(det_hash()); put("\"");
    }
    QueryPerformanceCounter(&t1);
    u64 ns_per_tick = (u64)((t1 - t0) * 1000000000ll / f / 10000);
    put("],\"ns_per_tick\":"); put_u(ns_per_tick); put("}\n");
    flush();
    ExitProcess(0);
}
#endif
