// Spike P0-04: freestanding wasm32 module. Throwaway.
typedef unsigned long long u64; typedef long long i64; typedef unsigned u32; typedef int i32; typedef float f32;
#define EXPORT(n) __attribute__((export_name(n)))
__attribute__((import_module("env"), import_name("js_log_i64"))) void js_log_i64(i64 v);

typedef struct { f32 x, y, vx, vy; } body;
static body bodies[256];
typedef f32 f32x4 __attribute__((ext_vector_type(4)));

EXPORT("add") i32 add(i32 a, i32 b) { return a + b; }

EXPORT("u64_ops") u64 u64_ops(u64 a, u64 b) { return a * b + a / (b | 1) + (a % (b | 1)) + (a << (b & 63)) + (a >> (b & 63)); }

EXPORT("struct_ops") f32 struct_ops(i32 n) {
    body local[64] = {0};                         // memset
    for (i32 i = 0; i < 64; i++) local[i].x = (f32)i;
    for (i32 i = 0; i < n && i < 64; i++) bodies[i] = local[i];   // memcpy
    __builtin_memmove(&bodies[1], &bodies[0], sizeof(body) * 10);
    return bodies[5].x;
}

EXPORT("simd_sum") f32 simd_sum(i32 n) {
    f32x4 acc = {0, 0, 0, 0};
    for (i32 i = 0; i < n; i++) { f32x4 v = {(f32)i, 1.0f, 2.0f, 3.0f}; acc = acc + v * v; acc = __builtin_elementwise_sqrt(acc); }
    return acc.x + acc.y + acc.z + acc.w;
}

EXPORT("f2i") i32 f2i(f32 f) { return (i32)f; }   // trapping vs nontrapping
EXPORT("i64_div") i64 i64_div(i64 a, i64 b) { return a / b; }
EXPORT("log_test") void log_test(void) { js_log_i64(1234567890123ll); }
