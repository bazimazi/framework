// bmf.h: the public API of bmf, the bazimazi framework (SPEC §16, task 1A-24).
//
// The only header a game includes. A game is one C file (games/<name>/game.c, D-11) that includes
// this header and defines bmf_game() (see "The game model", the last section). Everything else
// under src/ is internal to the engine and may change at any time; API.md is generated from this
// file (signatures and contract comments).
//
// Conventions (SPEC §16):
// - Names: functions bmf_<module>_<verb>[_<noun>], types bmf_<noun>, enum values
//   BMF_<MODULE>_<VALUE>, constants BMF_<NAME>.
// - Contracts: every function has a contract comment directly above it, or above the group of
//   declarations it belongs to (no blank line in between): what it does, preconditions,
//   ownership and lifetime, cost, and whether it is allowed in tick. A rule stated in a section's
//   introduction applies to every function of that section.
// - "Allowed in tick" means the function is deterministic (bit-identical on every target, D-37)
//   and may be called from the game's tick, directly or indirectly. A function whose contract or
//   section does not say so must not be called from tick.
// - Threads: pure functions (math, SIMD, hashing, sorting, strings, an RNG on its own state) may
//   run on any thread. Everything else is main thread only; game callbacks run on the main thread.
// - Errors: functions that can fail because of the outside world return bmf_err and write
//   results through out-params (last). Programmer errors (bad index, broken precondition) are
//   BMF_ASSERTs, checked in debug builds.
// - Structs up to 16 bytes are passed by value, larger ones by const pointer.
// - Counts and indices are i32; handles, bit sets and hashes are u32/u64 (D-15).
#ifndef BMF_H
#define BMF_H

#include <stddef.h>   // offsetof, size_t (compiler-provided freestanding header, D-02)

// Version of the public API a game was compiled against (bmf_game_desc.api_version). Bumped on
// every breaking change to this header (R7); frozen at 1 at the end of Phase 2.
#define BMF_API_VERSION 0

// ================================================================================================
// Types, platform macros, asserts (SPEC §5.1, D-15)
// ================================================================================================
// ---------------------------------------------------------------- types (SPEC §5.1)
typedef signed char        i8;
typedef short              i16;
typedef int                i32;
typedef long long          i64;
typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned           u32;
typedef unsigned long long u64;
typedef float              f32;
typedef double             f64;
typedef i32                b32;

// Result of a function that can fail (SPEC §16). Results go through out-params. Closed enum:
// modules add values here, never their own error types.
typedef enum bmf_err {
    BMF_OK = 0,
    BMF_ERR_INVALID = 1,     // malformed input (parse error, bad argument value from outside)
    BMF_ERR_RANGE = 2,       // well-formed but out of the representable range
    BMF_ERR_NOT_FOUND = 3,   // a file or path does not exist
    BMF_ERR_IO = 4,          // the OS refused or failed a file operation (access denied, disk full, locked)
    BMF_ERR_UNSUPPORTED = 5, // the active backend or device cannot do this (e.g. a GPU readback on the null backend)
    BMF_ERR_PENDING = 6,     // an asynchronous operation has not finished yet; call again later
} bmf_err;

#define BMF_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)

BMF_STATIC_ASSERT(sizeof(i8) == 1 && sizeof(u8) == 1, "8-bit types");
BMF_STATIC_ASSERT(sizeof(i16) == 2 && sizeof(u16) == 2, "16-bit types");
BMF_STATIC_ASSERT(sizeof(i32) == 4 && sizeof(u32) == 4, "32-bit types");
BMF_STATIC_ASSERT(sizeof(i64) == 8 && sizeof(u64) == 8, "64-bit types");
BMF_STATIC_ASSERT(sizeof(f32) == 4 && sizeof(f64) == 8, "float types");

// ---------------------------------------------------------------- platform and architecture
// Build macros, each 0 or 1 (the preprocessor blocks that set them are left out of API.md):
// BMF_OS_WINDOWS, BMF_OS_WEB and BMF_ARCH_X64, BMF_ARCH_WASM32, BMF_ARCH_ARM64 name the target;
// BMF_DEBUG is 1 in debug builds, BMF_SANITIZE in sanitizer builds. BMF_EXPORT marks a function
// exported from the game DLL (bmf_game).

#if defined(_WIN32)
#define BMF_OS_WINDOWS 1
#elif defined(__wasm__)
#define BMF_OS_WEB 1
#else
#error "bmf: unsupported OS"
#endif
#ifndef BMF_OS_WINDOWS
#define BMF_OS_WINDOWS 0
#endif
#ifndef BMF_OS_WEB
#define BMF_OS_WEB 0
#endif

#if defined(__x86_64__)
#define BMF_ARCH_X64 1
#elif defined(__wasm32__)
#define BMF_ARCH_WASM32 1
#elif defined(__aarch64__)
#define BMF_ARCH_ARM64 1
#else
#error "bmf: unsupported architecture"
#endif
#ifndef BMF_ARCH_X64
#define BMF_ARCH_X64 0
#endif
#ifndef BMF_ARCH_WASM32
#define BMF_ARCH_WASM32 0
#endif
#ifndef BMF_ARCH_ARM64
#define BMF_ARCH_ARM64 0
#endif

#ifndef BMF_DEBUG
#define BMF_DEBUG 0
#endif
// BMF_AGENT=1 keeps the agent tools (control protocol, profiler zones, the full log) in a release
// build (perf testing, SPEC §11); debug builds always have them. BMF_TOOLS_ENABLED says which.
#ifndef BMF_AGENT
#define BMF_AGENT 0
#endif
#define BMF_TOOLS_ENABLED (BMF_DEBUG || BMF_AGENT)
#ifndef BMF_SANITIZE
#define BMF_SANITIZE 0
#endif

#if BMF_OS_WINDOWS
#define BMF_EXPORT __declspec(dllexport)
#else
#define BMF_EXPORT __attribute__((visibility("default")))
#endif

// ---------------------------------------------------------------- fatal errors and asserts
// Engine side (games do not call it; left out of API.md): the fatal-error hook behind BMF_ASSERT
// and bmf_panic. Reports `what` with file and line (plus the crash dump in debug and BMF_AGENT
// runtimes) and returns; the caller traps. Any thread.
void bmf_platform_fatal(const char* file, i32 line, const char* kind, const char* what);

// Reports a failed assert and stops the process (called by BMF_ASSERT). Never returns. Any thread.
static inline __attribute__((noreturn, noinline, cold)) void bmf_assert_fail(const char* file, i32 line, const char* expr) {
    bmf_platform_fatal(file, line, "assert", expr);
    __builtin_trap();
}

// Reports an unrecoverable runtime error (corrupt data, out of memory: not programmer errors,
// which are BMF_ASSERTs) and stops the process, in debug and release. Never returns. Any thread.
// The crash report carries the return address instead of a file and line.
static inline __attribute__((noreturn, noinline, cold)) void bmf_panic(const char* message) {
    bmf_platform_fatal("?", 0, "panic", message);
    __builtin_trap();
}

// BMF_ASSERT(cond) checks programmer errors in debug builds and compiles to nothing in release.
#if BMF_DEBUG
#define BMF_ASSERT(cond) ((cond) ? (void)0 : bmf_assert_fail(__FILE__, __LINE__, #cond))
#else
#define BMF_ASSERT(cond) ((void)0)
#endif

// ================================================================================================
// Memory: arenas, scratch arenas, pools (SPEC §5.2, D-16, D-39)
// ================================================================================================
// Games get two arenas from the engine (bmf_ctx.level, bmf_ctx.frame) and allocate from them;
// they never reserve memory themselves. An arena or pool is used by one thread at a time.
// Allocating is allowed in tick (arena memory is not part of the state block; data derived from
// the state may live there, D-21), but nothing in the state block may point into an arena.
// ---------------------------------------------------------------- arena
// Linear allocator over a reserved address range. Memory is committed on demand in 64 KiB
// granules and stays committed after pop/reset. One never-committed guard granule follows the
// reserved range, so running off the end faults instead of corrupting the next arena.
// Debug builds fill pushed bytes with 0xCD and popped/reset bytes with 0xDD. Games read the
// fields and never write them.
typedef struct bmf_arena {
    u8* base;
    i64 reserved;     // usable bytes (guard granule not included)
    i64 committed;
    i64 used;
    i64 high_water;   // max `used` ever reached (reported by the `mem` protocol command)
    const char* name;
} bmf_arena;

// Returns `size` bytes aligned to `align` (power of two, 1..4096). Contents are undefined
// (0xCD in debug); use bmf_arena_push_zero for zeroed memory. Panics when the arena's reserve
// is exhausted (a memory budget error, D-39). O(1) plus commit cost on growth.
void* bmf_arena_push_aligned(bmf_arena* a, i64 size, i64 align);

// bmf_arena_push_aligned with 16-byte alignment (SPEC §5.2 default).
void* bmf_arena_push(bmf_arena* a, i64 size);

// Like bmf_arena_push_aligned, but zero-filled. O(size).
void* bmf_arena_push_zero(bmf_arena* a, i64 size, i64 align);

// Current position, for bmf_arena_pop_to. O(1).
i64 bmf_arena_mark(const bmf_arena* a);

// Frees everything pushed after `mark`. O(1) (O(popped bytes) in debug for the 0xDD fill).
void bmf_arena_pop_to(bmf_arena* a, i64 mark);

// Frees everything. Keeps memory committed.
void bmf_arena_reset(bmf_arena* a);

// ---------------------------------------------------------------- scratch arenas
// Two scratch arenas so a function can use scratch memory while its caller holds the other one:
// pass the arena your caller gave you as `conflict` and you get the other. Always pair
// bmf_scratch_begin with bmf_scratch_end.
// Limit: main thread only until the job system (1B-12) gives workers their own pair.
typedef struct bmf_scratch {
    bmf_arena* arena;
    i64 mark;
} bmf_scratch;

// Starts using a scratch arena: the one that is not `conflict` (0 when the caller holds none).
// Returns the arena and its current mark. O(1).
bmf_scratch bmf_scratch_begin(const bmf_arena* conflict);

// Frees everything pushed onto s.arena since the matching bmf_scratch_begin. O(1) (O(freed
// bytes) in debug).
void bmf_scratch_end(bmf_scratch s);

// ---------------------------------------------------------------- memory budget (D-16, D-39)
// Arena sizes a game asks for (bmf_game_desc.memory), in bytes; 0 = the default. The engine
// reserves them at startup; running past one is a fatal memory-budget error.
typedef struct bmf_memory_desc {
    i64 permanent;   // default 64 MiB
    i64 level;       // default 64 MiB
    i64 frame;       // default 32 MiB
    i64 scratch;     // each of the two; default 16 MiB
} bmf_memory_desc;

// ---------------------------------------------------------------- pool (pointer-free, D-21)
// Fixed-capacity slot allocator with generational handles. It allocates slot indices only: the
// caller keeps item data in its own arrays indexed by slot index. All state is in `bmf_pool` plus
// one caller-owned `bmf_pool_slot[capacity]` array, so a pool can live inside the game state block.
typedef u32 bmf_handle;   // index:20 | generation:12. 0 is never a valid handle.

enum { BMF_POOL_MAX_CAPACITY = (1 << 20) - 1, BMF_HANDLE_GENERATION_BITS = 12 };

typedef struct bmf_pool_slot {
    u32 generation;   // 1..4095 while in use or after first use; bit 31 = alive
    i32 link;         // alive: position in the dense list; free: next free slot (-1 = end)
    i32 dense;        // dense list element at this position: slot index of a live item
} bmf_pool_slot;

typedef struct bmf_pool {
    i32 capacity;
    i32 live_count;   // dense list length
    i32 free_head;    // first free slot, -1 when full
    i32 high_water;   // max live_count ever reached
} bmf_pool;

// Prepares a pool of `capacity` slots (1..BMF_POOL_MAX_CAPACITY). O(capacity).
void bmf_pool_init(bmf_pool* p, bmf_pool_slot* slots, i32 capacity);

// Takes a free slot. Returns its handle, or 0 when the pool is full. O(1).
bmf_handle bmf_pool_alloc(bmf_pool* p, bmf_pool_slot* slots);

// Releases a live handle (asserts it is valid). Its generation advances, so copies of the handle
// become stale. O(1). Changes the dense order (the last live item moves into the hole).
void bmf_pool_free(bmf_pool* p, bmf_pool_slot* slots, bmf_handle h);

// True if `h` refers to a live slot of this pool. O(1).
// Limit: 12-bit generations wrap after 4095 reuses of one slot, after which a very old stale
// handle to that slot compares valid again.
b32 bmf_pool_valid(const bmf_pool* p, const bmf_pool_slot* slots, bmf_handle h);

// Slot index of a live handle (asserts it is valid). O(1).
i32 bmf_pool_index(const bmf_pool* p, const bmf_pool_slot* slots, bmf_handle h);

// Dense iteration: for (i32 i = 0; i < p->live_count; i++) { i32 slot = bmf_pool_dense_slot(slots, i); ... }
// The order depends only on the sequence of alloc/free calls (deterministic). O(1).
static inline i32 bmf_pool_dense_slot(const bmf_pool_slot* slots, i32 i) { return slots[i].dense; }

// Handle of the live item in `slot`. O(1).
bmf_handle bmf_pool_handle_of(const bmf_pool_slot* slots, i32 slot);

// ================================================================================================
// Deterministic math: scalars, vectors, rotations, transforms, rectangles, colors (SPEC §5.3, D-17, D-37)
// ================================================================================================
// Every function in this section returns bit-identical results on every target (x64 SSE, wasm32, arm64)
// and at every optimization level. They use only IEEE basic operations (+ - * / sqrt), the
// exact rounding instructions (floor/ceil/trunc), compares and integer arithmetic. Transcendental
// functions compute in f64 and round to f32 once at the end. All are allowed in `tick`.
//
// Accuracy: every transcendental function is faithfully rounded (max error < 1 ULP); sqrt, floor,
// ceil, round, trunc, fabs, fmin, fmax and fmod are exact.
//
// Domain rules: NaN and Inf MUST NOT reach the state block (D-37 rule 11). The functions follow
// C99 Annex F for special inputs (NaN in gives NaN out, sin(Inf) is NaN, log(0) is -Inf, and so
// on), so callers must keep inputs inside the documented domain.
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

// ---------------------------------------------------------------- scalar helpers (1A-04b)
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

// ---------------------------------------------------------------- vectors (1A-04b)
// Plain structs passed by value; every operation is a static inline function with a fixed
// operation order. All: O(1), allowed in tick. Lengths use the sqrt instruction, never rsqrt.

typedef struct bmf_v2 { f32 x, y; } bmf_v2;
typedef struct bmf_v3 { f32 x, y, z; } bmf_v3;
typedef struct bmf_v4 { f32 x, y, z, w; } bmf_v4;

// Construction and component-wise arithmetic (mul: per component; div: real division, s != 0).
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

// bmf_v3: the bmf_v2 operations in 3D, plus the cross product.
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

// bmf_v4: component-wise arithmetic, dot product and lerp.
static inline bmf_v4 bmf_v4_make(f32 x, f32 y, f32 z, f32 w) { bmf_v4 r = { x, y, z, w }; return r; }
static inline bmf_v4 bmf_v4_add(bmf_v4 a, bmf_v4 b) { bmf_v4 r = { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w }; return r; }
static inline bmf_v4 bmf_v4_sub(bmf_v4 a, bmf_v4 b) { bmf_v4 r = { a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w }; return r; }
static inline bmf_v4 bmf_v4_mul(bmf_v4 a, bmf_v4 b) { bmf_v4 r = { a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w }; return r; }
static inline bmf_v4 bmf_v4_scale(bmf_v4 a, f32 s) { bmf_v4 r = { a.x * s, a.y * s, a.z * s, a.w * s }; return r; }
static inline f32 bmf_v4_dot(bmf_v4 a, bmf_v4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
static inline bmf_v4 bmf_v4_lerp(bmf_v4 a, bmf_v4 b, f32 t) {
    return bmf_v4_make(bmf_lerpf(a.x, b.x, t), bmf_lerpf(a.y, b.y, t), bmf_lerpf(a.z, b.z, t), bmf_lerpf(a.w, b.w, t));
}

// ---------------------------------------------------------------- rotation (1A-04b)
// A 2D rotation stored as its cosine and sine, so applying it needs no trig. Composing many
// rotations drifts from unit length slowly; re-create from an angle or call bmf_rot_normalize.
typedef struct bmf_rot { f32 c, s; } bmf_rot;

// No rotation (cos 1, sin 0).
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

// ---------------------------------------------------------------- 2D affine transform (1A-04b)
// p' = x_axis * p.x + y_axis * p.y + origin (column vectors: the images of (1,0), (0,1) and the
// origin). bmf_m3x2_mul(a, b) applies b first, then a.
typedef struct bmf_m3x2 { bmf_v2 x_axis, y_axis, origin; } bmf_m3x2;

// The identity and a pure translation by `t`.
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

// ---------------------------------------------------------------- rectangles (1A-04b)
// bmf_rect: position and size, half-open [x, x + w) x [y, y + h) (pixels, UV rects, UI).
// bmf_aabb: closed box [min, max] (geometry, physics). Touching boxes overlap.
typedef struct bmf_rect { f32 x, y, w, h; } bmf_rect;
typedef struct bmf_aabb { bmf_v2 min, max; } bmf_aabb;

// Construction, half-open point test, center, and the closed box with the same extent.
static inline bmf_rect bmf_rect_make(f32 x, f32 y, f32 w, f32 h) { bmf_rect r = { x, y, w, h }; return r; }
static inline b32 bmf_rect_contains(bmf_rect r, bmf_v2 p) { return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h; }
static inline bmf_v2 bmf_rect_center(bmf_rect r) { return bmf_v2_make(r.x + r.w * 0.5f, r.y + r.h * 0.5f); }
static inline bmf_aabb bmf_rect_to_aabb(bmf_rect r) { bmf_aabb b = { { r.x, r.y }, { r.x + r.w, r.y + r.h } }; return b; }

// A box from its corners (min <= max per axis).
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

// ---------------------------------------------------------------- colors (1A-04b)
// 0xRRGGBBAA in sRGB (SPEC §7); blending is premultiplied alpha in sRGB space.
typedef u32 bmf_color;

// Packs four 0..255 channels / reads one channel back.
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

// ---------------------------------------------------------------- fast tier (1A-04c, D-17)
// Lower-precision f32 versions for render and audio code. NEVER use them in simulation code
// (anything reachable from tick, D-37 rule 4): their precision may change in any release, which
// would change game state. All: O(1), inline. NOT allowed in tick.

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

// ================================================================================================
// SIMD: portable 4-lane vectors (D-06)
// ================================================================================================
// Portable 4-lane SIMD through clang ext_vector_type: one code path for x64 SSE, wasm simd128 and
// NEON. Every operation is O(1), allowed in tick (see Determinism) and safe on any thread.
//
// Determinism (D-37 rules 7 and 9): every operation here gives the same bits as the scalar code
// written next to it, on every target. Therefore:
// - There is deliberately NO reciprocal or reciprocal-sqrt estimate: use `/` and bmf_f32x4_sqrt.
// - min/max are defined as compare+select (`a < b ? a : b`), not as the target's min/max
//   instruction: x64 minps and wasm f32x4.min disagree on NaN and on -0 vs +0.
// - Arithmetic uses the plain operators (+ - * /), which are IEEE-exact lanewise. With
//   -ffp-contract=off (D-09) clang never fuses them.
typedef f32 bmf_f32x4 __attribute__((ext_vector_type(4)));
typedef i32 bmf_i32x4 __attribute__((ext_vector_type(4)));
typedef u32 bmf_u32x4 __attribute__((ext_vector_type(4)));

// Arithmetic: use the operators directly: a + b, a - b, a * b, a / b (also with a scalar,
// which is splatted), and & | ^ ~ << >> on the integer types. Comparisons (a < b, a == b, ...)
// return a bmf_i32x4 mask with -1 (all bits set) for true and 0 for false.

// ---------------------------------------------------------------- construction and memory
// All lanes = v (splat) / lanes = x, y, z, w (set).
static inline bmf_f32x4 bmf_f32x4_splat(f32 v) { bmf_f32x4 r = { v, v, v, v }; return r; }
static inline bmf_f32x4 bmf_f32x4_set(f32 x, f32 y, f32 z, f32 w) { bmf_f32x4 r = { x, y, z, w }; return r; }
static inline bmf_i32x4 bmf_i32x4_splat(i32 v) { bmf_i32x4 r = { v, v, v, v }; return r; }
static inline bmf_u32x4 bmf_u32x4_splat(u32 v) { bmf_u32x4 r = { v, v, v, v }; return r; }

// Loads 4 floats from a 16-byte-aligned address (asserted in debug).
static inline bmf_f32x4 bmf_f32x4_load(const f32* p) {
    BMF_ASSERT(((u64)p & 15u) == 0);
    return *(const bmf_f32x4*)p;
}
// Loads 4 floats from any address.
static inline bmf_f32x4 bmf_f32x4_load_unaligned(const f32* p) {
    bmf_f32x4 r;
    __builtin_memcpy(&r, p, sizeof r);
    return r;
}
// Stores 4 floats to a 16-byte-aligned address (asserted in debug) / to any address.
static inline void bmf_f32x4_store(f32* p, bmf_f32x4 v) {
    BMF_ASSERT(((u64)p & 15u) == 0);
    *(bmf_f32x4*)p = v;
}
static inline void bmf_f32x4_store_unaligned(f32* p, bmf_f32x4 v) { __builtin_memcpy(p, &v, sizeof v); }

// The same loads and stores for i32 lanes (aligned: 16 bytes, asserted in debug).
static inline bmf_i32x4 bmf_i32x4_load(const i32* p) {
    BMF_ASSERT(((u64)p & 15u) == 0);
    return *(const bmf_i32x4*)p;
}
static inline bmf_i32x4 bmf_i32x4_load_unaligned(const i32* p) {
    bmf_i32x4 r;
    __builtin_memcpy(&r, p, sizeof r);
    return r;
}
static inline void bmf_i32x4_store(i32* p, bmf_i32x4 v) {
    BMF_ASSERT(((u64)p & 15u) == 0);
    *(bmf_i32x4*)p = v;
}
static inline void bmf_i32x4_store_unaligned(i32* p, bmf_i32x4 v) { __builtin_memcpy(p, &v, sizeof v); }

// Unaligned load and store for u32 lanes.
static inline bmf_u32x4 bmf_u32x4_load_unaligned(const u32* p) {
    bmf_u32x4 r;
    __builtin_memcpy(&r, p, sizeof r);
    return r;
}
static inline void bmf_u32x4_store_unaligned(u32* p, bmf_u32x4 v) { __builtin_memcpy(p, &v, sizeof v); }

// ---------------------------------------------------------------- bit casts (no conversion)
static inline bmf_i32x4 bmf_f32x4_as_i32x4(bmf_f32x4 v) { return (bmf_i32x4)v; }
static inline bmf_f32x4 bmf_i32x4_as_f32x4(bmf_i32x4 v) { return (bmf_f32x4)v; }
static inline bmf_u32x4 bmf_i32x4_as_u32x4(bmf_i32x4 v) { return (bmf_u32x4)v; }
static inline bmf_i32x4 bmf_u32x4_as_i32x4(bmf_u32x4 v) { return (bmf_i32x4)v; }

// ---------------------------------------------------------------- select, min, max, abs, sqrt
// Lanewise mask ? a : b. `mask` lanes must be 0 or -1 (as produced by comparisons).
static inline bmf_f32x4 bmf_f32x4_select(bmf_i32x4 mask, bmf_f32x4 a, bmf_f32x4 b) {
    return (bmf_f32x4)((mask & (bmf_i32x4)a) | (~mask & (bmf_i32x4)b));
}
static inline bmf_i32x4 bmf_i32x4_select(bmf_i32x4 mask, bmf_i32x4 a, bmf_i32x4 b) {
    return (mask & a) | (~mask & b);
}

// Lanewise a < b ? a : b. Equals the scalar expression bit for bit (b is returned when equal).
static inline bmf_f32x4 bmf_f32x4_min(bmf_f32x4 a, bmf_f32x4 b) { return bmf_f32x4_select(a < b, a, b); }
// Lanewise a > b ? a : b. Equals the scalar expression bit for bit (b is returned when equal).
static inline bmf_f32x4 bmf_f32x4_max(bmf_f32x4 a, bmf_f32x4 b) { return bmf_f32x4_select(a > b, a, b); }
static inline bmf_i32x4 bmf_i32x4_min(bmf_i32x4 a, bmf_i32x4 b) { return bmf_i32x4_select(a < b, a, b); }
static inline bmf_i32x4 bmf_i32x4_max(bmf_i32x4 a, bmf_i32x4 b) { return bmf_i32x4_select(a > b, a, b); }

// Clears the sign bit (exact).
static inline bmf_f32x4 bmf_f32x4_abs(bmf_f32x4 v) { return (bmf_f32x4)((bmf_i32x4)v & bmf_i32x4_splat(0x7FFFFFFF)); }

// Correctly rounded IEEE square root per lane (sqrtps / f32x4.sqrt).
static inline bmf_f32x4 bmf_f32x4_sqrt(bmf_f32x4 v) { return __builtin_elementwise_sqrt(v); }

// ---------------------------------------------------------------- reductions (fixed order, D-37 rule 13)
// ((x + y) + z) + w: the same order as the scalar loop over the 4 lanes.
static inline f32 bmf_f32x4_sum(bmf_f32x4 v) { return ((v.x + v.y) + v.z) + v.w; }
// True if any lane of a comparison mask is set.
static inline b32 bmf_i32x4_any(bmf_i32x4 mask) { return (mask.x | mask.y | mask.z | mask.w) != 0; }

// ================================================================================================
// Hashing (SPEC §5.4)
// ================================================================================================
// 64-bit hash (one-shot and streaming), CRC-32 and Adler-32. Deterministic on every target (the
// same bits everywhere), allowed in tick, any thread. The bmf hash is an own 4-lane design (see
// src/base/README.md), not cryptographic; one-shot and streaming give identical results.

typedef struct bmf_hasher {
    u64 lanes[4];
    u8  buffer[32];
    i32 buffer_len;
    i64 total_len;
    u64 seed;
} bmf_hasher;

// Streaming form: bmf_hasher_init, then bmf_hasher_update any number of times, then
// bmf_hasher_final. bmf_hash is the one-shot form of the same function.
void bmf_hasher_init(bmf_hasher* h, u64 seed);                    // O(1)
void bmf_hasher_update(bmf_hasher* h, const void* data, i64 len); // O(len)
u64  bmf_hasher_final(const bmf_hasher* h);                       // O(1); does not modify h
u64  bmf_hash(const void* data, i64 len, u64 seed);               // O(len)

// CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320), as used by PNG and zlib.
// Start with crc = 0 and pass the previous result to continue. O(len).
u32 bmf_crc32(u32 crc, const void* data, i64 len);

// Adler-32 (RFC 1950), as used by zlib. Start with adler = 1. O(len).
u32 bmf_adler32(u32 adler, const void* data, i64 len);

// ================================================================================================
// Random numbers: PCG32 (SPEC §5.4, D-37 rule 3)
// ================================================================================================
// PCG32 random number generator (O'Neill 2014, XSH-RR 64/32). The state is 16 bytes, pointer-free, and lives in the game state
// block (SPEC §5.4, D-37 rule 3). Every function is deterministic on every target and allowed in
// tick; a generator is used by one thread at a time.
//
// Draw at most ONE random number per full expression. C leaves the evaluation order of function
// arguments and of operands unspecified, and clang evaluates them in different orders on win64
// and wasm32: `bmf_v2_make(bmf_rng_f32(r), bmf_rng_f32(r))` gives swapped x/y across targets.
// Use one statement per draw.
typedef struct bmf_rng {
    u64 state;
    u64 inc;      // stream selector, always odd
} bmf_rng;

// Seeds the generator. `stream` selects one of 2^63 independent sequences. O(1). Allowed in tick.
static inline u32 bmf_rng_next_u32(bmf_rng* r);
static inline void bmf_rng_seed(bmf_rng* r, u64 seed, u64 stream) {
    r->state = 0;
    r->inc = (stream << 1) | 1u;
    bmf_rng_next_u32(r);
    r->state += seed;
    bmf_rng_next_u32(r);
}

// Next uniformly distributed u32. O(1). Allowed in tick.
static inline u32 bmf_rng_next_u32(bmf_rng* r) {
    u64 old = r->state;
    r->state = old * 6364136223846793005ull + r->inc;
    u32 xorshifted = (u32)(((old >> 18) ^ old) >> 27);
    u32 rot = (u32)(old >> 59);
    return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
}

// Uniform f32 in [0, 1) with 24 random bits (every value is exactly representable). O(1).
static inline f32 bmf_rng_f32(bmf_rng* r) {
    return (f32)(bmf_rng_next_u32(r) >> 8) * (1.0f / 16777216.0f);
}

// Uniform integer in [lo, hi] (inclusive; lo <= hi), unbiased (rejection). Expected O(1).
static inline i32 bmf_rng_range_i32(bmf_rng* r, i32 lo, i32 hi) {
    BMF_ASSERT(lo <= hi);
    u32 bound = (u32)((i64)hi - (i64)lo) + 1u;   // 0 means the full 2^32 range
    if (bound == 0) return (i32)bmf_rng_next_u32(r);
    u32 threshold = (0u - bound) % bound;        // values below this would bias the modulo
    for (;;) {
        u32 x = bmf_rng_next_u32(r);
        if (x >= threshold) return (i32)((i64)lo + (i64)(x % bound));
    }
}

// Uniform f32 in [lo, hi). O(1).
static inline f32 bmf_rng_range_f32(bmf_rng* r, f32 lo, f32 hi) {
    return lo + (hi - lo) * bmf_rng_f32(r);
}

// Standard normal sample (mean 0, variance 1) by the Box-Muller transform, using the
// deterministic bmf_logf/bmf_cosf/bmf_sqrtf. Consumes two u32. u1 is in (0, 1], so the result is
// finite and |result| <= sqrt(-2 ln 2^-24) = 5.77. O(1). Allowed in tick.
static inline f32 bmf_rng_normal(bmf_rng* r) {
    f32 u1 = (f32)((bmf_rng_next_u32(r) >> 8) + 1u) * (1.0f / 16777216.0f);
    f32 u2 = bmf_rng_f32(r);
    return bmf_sqrtf(-2.0f * bmf_logf(u1)) * bmf_cosf(BMF_TAU * u2);
}

// ================================================================================================
// Sorting (SPEC §5.4)
// ================================================================================================
// Stable sorts for integer keys with u32 payloads. Used by draw2d (sort keys), physics (pair and
// cell keys) and the baker. Results depend only on the input, never on addresses (D-37 rule 5).
// Allowed in tick; any thread.

// Sorts keys[0..n) ascending, moving payload[i] with keys[i]. Stable: equal keys keep their input
// order. `payload` may be 0 (keys only). `keys_tmp` and `payload_tmp` are caller scratch of n
// elements (payload_tmp may be 0 when payload is 0). The result is in keys/payload.
// LSD radix sort with 8-bit digits; a pass is skipped when every key has the same digit there.
// n <= 32 uses insertion sort. O(n * passes), passes <= 4.
void bmf_sort_u32(u32* keys, u32* payload, i32 n, u32* keys_tmp, u32* payload_tmp);

// As bmf_sort_u32 for u64 keys. passes <= 8.
void bmf_sort_u64(u64* keys, u32* payload, i32 n, u64* keys_tmp, u32* payload_tmp);

// Stable insertion sorts (any n; O(n^2)). Exposed for small arrays and as the test reference.
void bmf_insertion_sort_u32(u32* keys, u32* payload, i32 n);
void bmf_insertion_sort_u64(u64* keys, u32* payload, i32 n);

// ================================================================================================
// Strings and formatting (SPEC §5.4)
// ================================================================================================
// bmf_str is a pointer and a length; it is never NUL-terminated by contract (SPEC §5.1). The
// functions here never allocate except bmf_fmt_arena, which pushes onto the given arena.
// Everything is deterministic (no locale, no platform calls), so it is allowed in tick unless a
// comment says otherwise; formatting floats or parsing is still slow work, keep it out of hot loops.
//
// Float text: bmf_fmt "%f" prints the SHORTEST decimal that parses back to the
// same f32 (round-trip exact, ties to the closest), in JavaScript Number notation: "0.1", "100",
// "1e+21", "1.5e-7", "-0". bmf_parse_f32 is correctly rounded (ties to even) for any input length.
typedef struct bmf_str { const u8* ptr; i32 len; } bmf_str;

// ---------------------------------------------------------------- C strings
// Length of a NUL-terminated string. O(n).
i32 bmf_cstr_len(const char* s);
// Byte equality of two NUL-terminated strings (0 pointers compare equal only to each other). O(n).
b32 bmf_cstr_eq(const char* a, const char* b);

// ---------------------------------------------------------------- bmf_str
// All O(1) unless noted; none allocate. Slices point into the original bytes.
static inline bmf_str bmf_str_make(const void* ptr, i32 len) { bmf_str s = { (const u8*)ptr, len }; return s; }
// Wraps a NUL-terminated string (not including the NUL). O(n).
static inline bmf_str bmf_str_from_cstr(const char* s) { return bmf_str_make(s, s ? bmf_cstr_len(s) : 0); }
// Bytes [start, end) with both clamped to [0, len].
bmf_str bmf_str_slice(bmf_str s, i32 start, i32 end);
// Byte equality. O(n).
b32 bmf_str_eq(bmf_str a, bmf_str b);
b32 bmf_str_eq_cstr(bmf_str a, const char* b);
b32 bmf_str_starts_with(bmf_str s, bmf_str prefix);
b32 bmf_str_ends_with(bmf_str s, bmf_str suffix);
// Index of the first occurrence of `needle` at or after `from`, or -1. An empty needle matches
// at `from`. O(n * m) worst case.
i32 bmf_str_find(bmf_str s, bmf_str needle, i32 from);
// Index of the first byte equal to `byte` at or after `from`, or -1. O(n).
i32 bmf_str_find_byte(bmf_str s, u8 byte, i32 from);
// Without leading/trailing ASCII whitespace (space, \t, \n, \r, \v, \f). O(n).
bmf_str bmf_str_trim(bmf_str s);

// Splits on a separator byte: "a,,b" gives "a", "", "b"; "" gives one empty piece.
//   bmf_str_splitter it = bmf_str_split(s, ','); bmf_str piece;
//   while (bmf_str_split_next(&it, &piece)) { ... }
typedef struct bmf_str_splitter { bmf_str rest; u8 separator; b32 done; } bmf_str_splitter;
static inline bmf_str_splitter bmf_str_split(bmf_str s, u8 separator) { bmf_str_splitter it = { s, separator, 0 }; return it; }
// Writes the next piece and returns 1, or returns 0 when there are no more pieces. O(piece).
b32 bmf_str_split_next(bmf_str_splitter* it, bmf_str* piece);

// ---------------------------------------------------------------- UTF-8
// Decodes the code point starting at byte `at` (0 <= at < s.len). Returns the number of bytes
// consumed (1..4). Invalid or truncated sequences, overlong forms, surrogates and values above
// U+10FFFF decode as U+FFFD and consume 1 byte. O(1).
i32 bmf_utf8_decode(bmf_str s, i32 at, u32* codepoint);
// Encodes one code point into out[0..3] and returns the byte count (1..4). Surrogates and values
// above U+10FFFF encode U+FFFD. O(1).
i32 bmf_utf8_encode(u32 codepoint, u8* out);
// ---------------------------------------------------------------- formatting
// printf-like formatting. Specifiers:
//   %i %u %x       i32, u32, u32 as lowercase hex (no prefix)
//   %li %lu %lx    the same for i64 / u64
//   %f             a float (f32 or f64 argument, printed as f32): shortest round-trip text
//   %.Nf           fixed N decimals (N = 0..9), exactly rounded half to even: "%.2f" of 0.125 -> "0.12"
//   %c             one byte (int argument)
//   %s             const char* (0 prints "(null)")
//   %S             bmf_str, by value
//   %v2            bmf_v2, by value, printed as "[x,y]" with shortest floats
//   %%             a percent sign
// Flags and width for every specifier: "%5i" pads left with spaces, "%-5i" pads right, "%05i" pads
// with zeros after the sign, "%016lx". An unknown specifier is a programmer error (asserted) and
// prints as written. Argument types must match exactly (varargs are not type-checked).
//
// bmf_fmt_buf writes at most capacity - 1 bytes plus a NUL (capacity > 0) and returns the full
// length the text needs (excluding the NUL): a result >= capacity means it was truncated.
// O(output length). Allowed in tick (deterministic) but slow-ish for floats (~100 ns each).
i32 bmf_fmt_buf(char* buf, i32 capacity, const char* fmt, ...);
i32 bmf_fmt_va(char* buf, i32 capacity, const char* fmt, __builtin_va_list args);
// Formats onto an arena (exact size plus a NUL that is not counted in the returned length).
// The text lives as long as the arena allocation. O(output length).
bmf_str bmf_fmt_arena(bmf_arena* arena, const char* fmt, ...);

// Shortest round-trip text of an f32 into buf (at most 22 bytes, e.g. "-100000000000000000000",
// plus NUL: capacity >= 23 never truncates). Returns the length. NaN prints "nan", infinities
// "inf" / "-inf". O(1).
i32 bmf_f32_to_text(f32 x, char* buf, i32 capacity);

// ---------------------------------------------------------------- parsing
// The whole string must be the number: no surrounding whitespace (trim first), no trailing
// characters. On error *out is unchanged unless stated.
// Integers: optional + or -, decimal digits. BMF_ERR_RANGE when the value does not fit.
bmf_err bmf_parse_i32(bmf_str s, i32* out);
bmf_err bmf_parse_i64(bmf_str s, i64* out);
// Floats: [+-] digits [. digits] [e|E [+-] digits], at least one digit in the significand
// (".5" and "5." are accepted), or "inf", "infinity", "nan" (any case, optional sign).
// Correctly rounded to nearest, ties to even, for any number of digits. Overflow writes +-Inf to
// *out and returns BMF_ERR_RANGE; underflow rounds to 0 or a denormal and is BMF_OK.
bmf_err bmf_parse_f32(bmf_str s, f32* out);

// ================================================================================================
// Keys (SPEC §10)
// ================================================================================================
// Physical key positions, named after the US layout (like the web `KeyboardEvent.code`): BMF_KEY_Q
// is the key left of W whatever the layout prints on it. Text input comes separately as
// BMF_EVENT_TEXT. Every key is a binding
// for input actions (bmf_action.bindings).
typedef enum bmf_key {
    BMF_KEY_NONE = 0,
    BMF_KEY_A, BMF_KEY_B, BMF_KEY_C, BMF_KEY_D, BMF_KEY_E, BMF_KEY_F, BMF_KEY_G, BMF_KEY_H, BMF_KEY_I,
    BMF_KEY_J, BMF_KEY_K, BMF_KEY_L, BMF_KEY_M, BMF_KEY_N, BMF_KEY_O, BMF_KEY_P, BMF_KEY_Q, BMF_KEY_R,
    BMF_KEY_S, BMF_KEY_T, BMF_KEY_U, BMF_KEY_V, BMF_KEY_W, BMF_KEY_X, BMF_KEY_Y, BMF_KEY_Z,
    BMF_KEY_0, BMF_KEY_1, BMF_KEY_2, BMF_KEY_3, BMF_KEY_4, BMF_KEY_5, BMF_KEY_6, BMF_KEY_7, BMF_KEY_8, BMF_KEY_9,
    BMF_KEY_ESCAPE, BMF_KEY_ENTER, BMF_KEY_TAB, BMF_KEY_BACKSPACE, BMF_KEY_SPACE,
    BMF_KEY_MINUS, BMF_KEY_EQUAL, BMF_KEY_LEFT_BRACKET, BMF_KEY_RIGHT_BRACKET, BMF_KEY_BACKSLASH,
    BMF_KEY_SEMICOLON, BMF_KEY_APOSTROPHE, BMF_KEY_GRAVE, BMF_KEY_COMMA, BMF_KEY_PERIOD, BMF_KEY_SLASH,
    BMF_KEY_INTL_BACKSLASH,   // the extra key right of left Shift on ISO keyboards
    BMF_KEY_CAPS_LOCK, BMF_KEY_SCROLL_LOCK, BMF_KEY_NUM_LOCK, BMF_KEY_PRINT_SCREEN, BMF_KEY_PAUSE,
    BMF_KEY_F1, BMF_KEY_F2, BMF_KEY_F3, BMF_KEY_F4, BMF_KEY_F5, BMF_KEY_F6, BMF_KEY_F7, BMF_KEY_F8,
    BMF_KEY_F9, BMF_KEY_F10, BMF_KEY_F11, BMF_KEY_F12, BMF_KEY_F13, BMF_KEY_F14, BMF_KEY_F15, BMF_KEY_F16,
    BMF_KEY_F17, BMF_KEY_F18, BMF_KEY_F19, BMF_KEY_F20, BMF_KEY_F21, BMF_KEY_F22, BMF_KEY_F23, BMF_KEY_F24,
    BMF_KEY_INSERT, BMF_KEY_DELETE, BMF_KEY_HOME, BMF_KEY_END, BMF_KEY_PAGE_UP, BMF_KEY_PAGE_DOWN,
    BMF_KEY_LEFT, BMF_KEY_RIGHT, BMF_KEY_UP, BMF_KEY_DOWN,
    BMF_KEY_KP_0, BMF_KEY_KP_1, BMF_KEY_KP_2, BMF_KEY_KP_3, BMF_KEY_KP_4, BMF_KEY_KP_5, BMF_KEY_KP_6,
    BMF_KEY_KP_7, BMF_KEY_KP_8, BMF_KEY_KP_9, BMF_KEY_KP_DECIMAL, BMF_KEY_KP_DIVIDE, BMF_KEY_KP_MULTIPLY,
    BMF_KEY_KP_SUBTRACT, BMF_KEY_KP_ADD, BMF_KEY_KP_ENTER,
    BMF_KEY_LEFT_SHIFT, BMF_KEY_RIGHT_SHIFT, BMF_KEY_LEFT_CTRL, BMF_KEY_RIGHT_CTRL,
    BMF_KEY_LEFT_ALT, BMF_KEY_RIGHT_ALT, BMF_KEY_LEFT_SUPER, BMF_KEY_RIGHT_SUPER, BMF_KEY_MENU,
    BMF_KEY_COUNT
} bmf_key;
BMF_STATIC_ASSERT(BMF_KEY_COUNT <= 128, "key state is kept in two u64 bitsets");

// ================================================================================================
// Drawing: cameras, shapes, text (SPEC §12.2, D-19)
// ================================================================================================
// Games draw from their draw callback only: every function here appends to the frame the engine
// is rendering. Not allowed in tick (rendering is presentation, SPEC §8.3) unless the contract
// says so (the pure camera math is).
//
// Order: each call names a layer (0..255). Layers draw in order; within a layer, shapes keep
// submission order.
//
// Coordinates (D-19): positions, sizes and lengths are in the layer's current camera space: a
// WORLD camera (y up, meters; the default for every layer: 64 logical pixels per meter, world
// origin at the view center) or a SCREEN camera (y down, logical pixels, origin top-left). Each
// layer has a camera stack (bmf_draw_camera_push/pop) that is reset every frame. Angles are
// radians, counter-clockwise on screen positive, in every camera space.
// Colors are bmf_color (0xRRGGBBAA sRGB, straight alpha).
enum {
    BMF_DRAW_CAMERA_STACK_DEPTH = 8,             // per layer, pushed cameras
};

// The default world scale (D-19): logical pixels per meter.
static const f32 BMF_DRAW_WORLD_PIXELS_PER_METER = 64.0f;

// ---------------------------------------------------------------- cameras
typedef enum bmf_camera_space {
    BMF_CAMERA_WORLD = 0,    // y up, meters; `position` is the world point at the view center
    BMF_CAMERA_SCREEN = 1,   // y down, logical pixels; `position` is the point at the top-left corner
} bmf_camera_space;

// A view onto a camera space. `rotation` turns the view counter-clockwise (the content appears
// turned clockwise by it), around the view center (WORLD) or the top-left corner (SCREEN).
typedef struct bmf_camera {
    bmf_camera_space space;
    bmf_v2 position;
    f32 pixels_per_unit;     // logical pixels per meter (WORLD) or per logical pixel (SCREEN); > 0
    f32 rotation;            // radians
} bmf_camera;

// The default world camera: origin at the view center, BMF_DRAW_WORLD_PIXELS_PER_METER, no
// rotation. O(1). Allowed in tick.
bmf_camera bmf_camera_world_default(void);

// The identity screen camera: logical pixels, origin top-left. O(1). Allowed in tick.
bmf_camera bmf_camera_screen_default(void);

// Camera space to screen logical pixels (y down, origin top-left of a view of `view_size`
// logical pixels). Deterministic math (bmf_sincosf), so input mapping may use it in tick.
// O(1). Allowed in tick.
bmf_v2 bmf_camera_world_to_screen(const bmf_camera* camera, bmf_v2 view_size, bmf_v2 point);

// The inverse of bmf_camera_world_to_screen (screen logical pixels to camera space), e.g. for
// pointer positions. O(1). Allowed in tick.
bmf_v2 bmf_camera_screen_to_world(const bmf_camera* camera, bmf_v2 view_size, bmf_v2 screen);

// Makes `camera` the current camera of `layer` for the following submits, on top of the
// layer's stack (at most BMF_DRAW_CAMERA_STACK_DEPTH deep). From the draw callback. O(1).
void bmf_draw_camera_push(i32 layer, const bmf_camera* camera);

// Returns `layer` to the camera below the top (the default world camera when the stack becomes
// empty). The stack must not be empty. O(1).
void bmf_draw_camera_pop(i32 layer);

// ---------------------------------------------------------------- shapes
// Every call adds one shape to the frame (O(1)), from the draw callback. Past the per-frame
// limit (65 536 shapes and glyph strokes) shapes are dropped and counted. `layer` is 0..255.
// Positions, sizes and lengths are in the layer's current camera space; lengths scale with the
// camera, and rotations are counter-clockwise on screen.

// Axis-aligned or rotated filled rectangle: `center`, full `size`, `rotation` (radians, CCW).
void bmf_draw_rect(i32 layer, bmf_v2 center, bmf_v2 size, f32 rotation, bmf_color color);

// Rectangle with rounded corners; `corner_radius` is clamped by the shader to half the shorter side.
void bmf_draw_rect_rounded(i32 layer, bmf_v2 center, bmf_v2 size, f32 rotation, f32 corner_radius, bmf_color color);

// Filled circle.
void bmf_draw_circle(i32 layer, bmf_v2 center, f32 radius, bmf_color color);

// Ring: the band of width `thickness` inside `radius` (outer edge at `radius`).
void bmf_draw_ring(i32 layer, bmf_v2 center, f32 radius, f32 thickness, bmf_color color);

// Line from `a` to `b`, `thickness` wide, with round caps (a capsule).
void bmf_draw_line(i32 layer, bmf_v2 a, bmf_v2 b, f32 thickness, bmf_color color);

// Filled triangle (any winding).
void bmf_draw_triangle(i32 layer, bmf_v2 a, bmf_v2 b, bmf_v2 c, bmf_color color);

// ---------------------------------------------------------------- text (built-in stroke font, task 1A-21)
// Glyphs for ASCII 32..126 are line segments drawn as capsules, so text is sharp at every size and needs no assets. Metrics, as fractions of the font size
// `size` (the line height): caps and ascenders 0.6875, lowercase x-height 0.5, descenders 0.1875
// below the baseline, baseline 0.75 below the line top. Digits share one advance.
typedef enum bmf_text_align {
    BMF_TEXT_ALIGN_LEFT = 0,     // `position` is the left edge of every line
    BMF_TEXT_ALIGN_CENTER = 1,   // ... the center of every line
    BMF_TEXT_ALIGN_RIGHT = 2,    // ... the right edge of every line
} bmf_text_align;

// The stroke width bmf_draw_text uses for stroke_width 0, as a fraction of the font size.
static const f32 BMF_TEXT_DEFAULT_STROKE = 0.08f;
// Up to this stroke width (fraction of the font size), every glyph's ink lies inside the box
// bmf_text_measure returns (before antialiasing, which adds up to 1 target pixel).
static const f32 BMF_TEXT_MAX_CONTAINED_STROKE = 0.09375f;

// The box `text` (UTF-8) takes when drawn at font size `size` (> 0), in the same units as `size`:
// width = the widest line's advance sum, height = line count * size. "\n" starts a line (an
// empty text is one empty line); "\r" takes no space; every other code point, and each byte of
// invalid UTF-8, takes its glyph's advance (the replacement glyph outside 32..126).
// bmf_draw_text lays out with exactly these advances. Deterministic. O(text.len).
// Allowed in tick.
bmf_v2 bmf_text_measure(f32 size, bmf_str text);

// Draws `text` (UTF-8) with the built-in stroke font, laid out as bmf_text_measure describes.
// Code points outside 32..126 (and invalid UTF-8 bytes) draw the replacement glyph, a box;
// "\r" draws nothing. `position` (camera space) is the top of the first line's box, at its left
// edge, center or right edge per `align`; each line is aligned on its own and the next line
// starts `size` further down the screen. Text is upright on screen when the layer's camera is
// not rotated, and turns with the camera. `size` and `stroke_width` are camera-space lengths;
// stroke_width 0 means BMF_TEXT_DEFAULT_STROKE * size. Costs one shape per stroke segment (about
// 9 per visible character; spaces cost nothing) against the per-frame limit. Same frame rules as
// the shape calls. O(text.len).
void bmf_draw_text(i32 layer, bmf_v2 position, f32 size, f32 stroke_width, bmf_color color, bmf_text_align align, bmf_str text);

// ================================================================================================
// Input: actions, bindings, the per-tick bmf_input (SPEC §10, D-24)
// ================================================================================================
// Simulation never sees devices, only actions. A game declares a table of named actions with
// default bindings (bmf_action, bmf_game_desc.actions). The engine samples them once per tick into
// a bmf_input, the only input tick gets (replays store it per tick).
//
// Edges are per tick: `pressed`/`released` describe the change since the previous tick, so no
// press is lost when a frame runs 0 ticks or several. A press released again before the next tick
// is still down (and pressed) on exactly one tick, and released on the next one. Every press
// counts: two taps within one frame give pressed, released, pressed, released on four ticks.
//
// Thread rule: main thread only. Not allowed in tick unless the contract says so: tick reads only
// the bmf_input it is given (D-37 rule 8).
// ---------------------------------------------------------------- actions and bindings
typedef enum bmf_action_type {
    BMF_ACTION_BUTTON = 0,    // digital: one bit of bmf_input.down/pressed/released
    BMF_ACTION_AXIS2 = 1,     // a vector in bmf_input.axis[]: length <= 1, +x right, +y up
    BMF_ACTION_POINTER = 2,   // the game reads bmf_input.pointer[] (without one, pointers stay 0); no bindings
    BMF_ACTION_TYPE_COUNT
} bmf_action_type;

enum {
    BMF_INPUT_MAX_BUTTONS = 64,             // BUTTON actions (bits of a u64)
    BMF_INPUT_MAX_AXES = 8,                 // AXIS2 actions
    BMF_INPUT_MAX_POINTER_ACTIONS = 1,
    BMF_INPUT_MAX_ACTIONS = BMF_INPUT_MAX_BUTTONS + BMF_INPUT_MAX_AXES + BMF_INPUT_MAX_POINTER_ACTIONS,
    BMF_INPUT_MAX_POINTERS = 4,             // bmf_input.pointer slots
    BMF_INPUT_MAX_TEXT = 8,                 // codepoints per tick
    BMF_INPUT_TEXT_QUEUE = 64,              // codepoints waiting for ticks; more are dropped
    BMF_INPUT_MAX_PENDING_PRESSES = 15,     // presses waiting for ticks per button or pointer; more are merged
    BMF_ACTION_MAX_BINDINGS = 8,
    BMF_ACTION_NAME_MAX = 31,               // bytes
};

// A binding is a u16: a source in bits 0..11 and, for AXIS2 actions, a direction in bits 12..14
// that makes a digital source push the axis one way (BMF_KEY_I | BMF_AXIS_UP).
// Every bmf_key value (1 .. BMF_KEY_COUNT - 1) is a binding for that physical key.
enum {
    // Digital sources (BUTTON actions, or AXIS2 with a direction):
    BMF_MOUSE_LEFT = 0x100, BMF_MOUSE_RIGHT, BMF_MOUSE_MIDDLE, BMF_MOUSE_X1, BMF_MOUSE_X2,
    // Gamepad buttons (Xbox names and positions). Any connected gamepad counts.
    BMF_PAD_A = 0x200, BMF_PAD_B, BMF_PAD_X, BMF_PAD_Y, BMF_PAD_LEFT_SHOULDER, BMF_PAD_RIGHT_SHOULDER,
    BMF_PAD_BACK, BMF_PAD_START, BMF_PAD_LEFT_STICK_PRESS, BMF_PAD_RIGHT_STICK_PRESS,
    BMF_PAD_DPAD_UP, BMF_PAD_DPAD_DOWN, BMF_PAD_DPAD_LEFT, BMF_PAD_DPAD_RIGHT,
    BMF_PAD_LEFT_TRIGGER, BMF_PAD_RIGHT_TRIGGER,   // down at BMF_INPUT_TRIGGER_DOWN or more
    // Two-dimensional sources (AXIS2 only, no direction). The sticks use the gamepad whose stick
    // is pushed furthest (the lowest index on a tie).
    BMF_KEYS_WASD = 0x300, BMF_KEYS_ARROWS, BMF_PAD_LSTICK, BMF_PAD_RSTICK, BMF_PAD_DPAD,
    // Directions, OR'ed onto a digital source in an AXIS2 action.
    BMF_AXIS_UP = 0x1000, BMF_AXIS_DOWN = 0x2000, BMF_AXIS_LEFT = 0x3000, BMF_AXIS_RIGHT = 0x4000,
    BMF_BINDING_SOURCE_MASK = 0x0FFF,
    BMF_BINDING_DIRECTION_MASK = 0x7000,
};

// Trigger value (0..1, after the platform threshold) at which BMF_PAD_*_TRIGGER counts as down.
static const f32 BMF_INPUT_TRIGGER_DOWN = 0.5f;

// One entry of the game's action table, e.g.
//   { "jump", BMF_ACTION_BUTTON, { BMF_KEY_SPACE, BMF_PAD_A } },
//   { "move", BMF_ACTION_AXIS2,  { BMF_KEYS_WASD, BMF_PAD_LSTICK } },
//   { "aim",  BMF_ACTION_POINTER },
// BUTTON actions get the bits 0, 1, 2, ... of bmf_input.down/pressed/released in table order
// (counting BUTTON actions only); AXIS2 actions get bmf_input.axis[0], [1], ... the same way.
typedef struct bmf_action {
    const char* name;                         // unique, 1..31 bytes of a-z A-Z 0-9 _ . - (a protocol token)
    bmf_action_type type;
    u16 bindings[BMF_ACTION_MAX_BINDINGS];    // default bindings; 0 = unused slot
} bmf_action;

// ---------------------------------------------------------------- the per-tick input
// Pointer slot rules: the mouse is slot 0 (id 1; down = left button). Touch and pen contacts take
// the lowest free slot (slot 0 too, while the mouse button is up; mouse motion is ignored for
// slot 0 until that contact ends) and get ids 2, 3, ... in contact order. More contacts than
// slots are ignored.
typedef struct bmf_pointer {
    bmf_v2 world;      // position in the pointer camera's space (bmf_input_set_pointer_camera; default world, y up)
    bmf_v2 screen;     // logical pixels, y down, origin top-left
    u32 id;            // 0 = no pointer in this slot; stays the same while the contact lasts
    u8 down, pressed, released;
    u8 reserved;       // 0
} bmf_pointer;
BMF_STATIC_ASSERT(sizeof(bmf_pointer) == 24, "bmf_pointer has no implicit padding");

typedef struct bmf_input {
    u64 down, pressed, released;                 // bit per BUTTON action
    bmf_v2 axis[BMF_INPUT_MAX_AXES];             // AXIS2 actions
    bmf_pointer pointer[BMF_INPUT_MAX_POINTERS];
    u32 text[BMF_INPUT_MAX_TEXT];                // text typed since the previous tick (UTF-32, no control characters)
    i32 text_len;
    i32 reserved;                                // 0
} bmf_input;
BMF_STATIC_ASSERT(sizeof(bmf_input) == 224, "bmf_input has no implicit padding (replays compare bytes)");

// Bit `button` (0..63) of in->down / pressed / released. O(1). Allowed in tick.
static inline b32 bmf_input_down(const bmf_input* in, i32 button)     { return (b32)((in->down >> button) & 1u); }
static inline b32 bmf_input_pressed(const bmf_input* in, i32 button)  { return (b32)((in->pressed >> button) & 1u); }
static inline b32 bmf_input_released(const bmf_input* in, i32 button) { return (b32)((in->released >> button) & 1u); }

// ---------------------------------------------------------------- actions at run time
// The camera that bmf_pointer.world positions are mapped with (usually the game's world camera,
// set from draw). Copied; stays until changed or the game restarts. camera->pixels_per_unit > 0.
// O(1).
void bmf_input_set_pointer_camera(const bmf_camera* camera);

// Index of the action named `name` in the current table, or -1. O(actions x name length).
i32 bmf_input_action_find(bmf_str name);

// The action's type and its slot: the bit (BUTTON), the axis index (AXIS2) or 0 (POINTER).
// 0 <= action < action count. O(1). Allowed in tick.
bmf_action_type bmf_input_action_type(i32 action);
i32 bmf_input_action_slot(i32 action);

// Rebinding. Replaces binding `slot` (0..BMF_ACTION_MAX_BINDINGS-1) of `action`; 0 clears it.
// Takes effect at the next event or gamepad update (a held key that is unbound stays down until
// then). Errors: BMF_ERR_RANGE (action or slot out of range), BMF_ERR_INVALID (binding not valid
// for the action's type, e.g. a stick on a BUTTON). Allowed in tick: bindings only change how
// later ticks are sampled, and replays store the sampled bmf_input. O(1).
bmf_err bmf_input_bind(i32 action, i32 slot, u16 binding);

// Binding `slot` of `action` (0 = empty). Both in range. O(1). Allowed in tick.
u16 bmf_input_binding(i32 action, i32 slot);

// Restores every action's default bindings from the table. Allowed in tick. O(actions).
void bmf_input_reset_bindings(void);

// ================================================================================================
// Profiling and logging (SPEC §11.3, §11.5)
// ================================================================================================
// Zones time named stretches of code on the main thread; the protocol's `profile` reports them.
// The engine times its own (below BMF_ZONE_ENGINE_COUNT); a game uses BMF_ZONE_GAME_FIRST + 0..31
// and may name them in bmf_game_desc.zone_names. Zones must nest: every bmf_zone_end closes the
// zone opened last (debug builds assert otherwise; a zone left open in tick is caught when the
// engine closes its tick zone). Without BMF_TOOLS_ENABLED both calls compile to nothing.
// Timing never changes the state: zones are allowed in tick.
typedef enum bmf_zone {
    BMF_ZONE_TICK = 0,          // the game's tick
    BMF_ZONE_DRAW = 1,          // the game's draw
    BMF_ZONE_DRAW2D_FLUSH = 2,  // sorting, batching and submitting the frame's 2D draws
    BMF_ZONE_GPU_END_FRAME = 3, // ending the GPU frame (present)
    BMF_ZONE_DEBUG_HOOKS = 4,   // per-tick recording, checkpoints and watches (debug and agent builds)
    BMF_ZONE_ENGINE_COUNT = 5,
    BMF_ZONE_GAME_FIRST = 32,   // a game's zones: BMF_ZONE_GAME_FIRST + 0 .. 31
    BMF_ZONE_COUNT = 64,
} bmf_zone;

// Engine side of bmf_zone_begin/end (call those instead; left out of API.md). 0 <= zone <
// BMF_ZONE_COUNT. Main thread. O(1): a timestamp and a stack push or pop.
void bmf_profile_zone_begin(i32 zone);
void bmf_profile_zone_end(i32 zone);

// Opens / closes a zone (see above). Main thread. O(1). Allowed in tick.
static inline void bmf_zone_begin(i32 zone) {
#if BMF_TOOLS_ENABLED
    bmf_profile_zone_begin(zone);
#else
    (void)zone;
#endif
}
static inline void bmf_zone_end(i32 zone) {
#if BMF_TOOLS_ENABLED
    bmf_profile_zone_end(zone);
#else
    (void)zone;
#endif
}

// Log levels. Release builds without BMF_AGENT drop debug and info messages and only count warn
// and error ones.
typedef enum bmf_log_level {
    BMF_LOG_DEBUG = 0,
    BMF_LOG_INFO = 1,
    BMF_LOG_WARN = 2,
    BMF_LOG_ERROR = 3,
    BMF_LOG_LEVEL_COUNT = 4,
} bmf_log_level;

// Engine side of bmf_log (call that instead; left out of API.md). Any thread. O(message length).
void bmf_log_va(bmf_log_level level, const char* fmt, __builtin_va_list args);

// Engine side of bmf_log in release builds without BMF_AGENT (left out of API.md): counts a
// message of `level` without storing it. Any thread. O(1).
void bmf_log_count(bmf_log_level level);

// Logs one message: bmf_fmt rules (%i %li %u %lu %x %lx %f %s %S, widths) into the log ring (256
// messages of up to 255 bytes; the oldest is dropped and counted when it is full), stamped with
// the tick; the protocol's `log` drains them, and in --control mode each is also written to stderr
// as {"event":"log",...} (main thread only). Any thread. Allowed in tick: messages are not state.
// O(message length).
static inline void bmf_log(bmf_log_level level, const char* fmt, ...) {
#if BMF_TOOLS_ENABLED
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    bmf_log_va(level, fmt, args);
    __builtin_va_end(args);
#else
    (void)fmt;
    if (level >= BMF_LOG_WARN) bmf_log_count(level);
#endif
}

// ================================================================================================
// The game model (SPEC §8, D-20, D-21, D-22)
// ================================================================================================
// A game is a C module that includes only this header and defines bmf_game(), which returns a
// bmf_game_desc: its state size, tick rate, reflected fields, input actions and three callbacks
// (init, tick, draw). The engine owns everything else: memory, the fixed-step loop, input
// sampling, rendering and, in native debug builds, hot reload of the game DLL (SPEC §8.5).
// All simulation state lives in one block of state_size bytes without pointers (D-21).
// Callbacks run on the main thread.

// Limits of a game description (checked at startup, see bmf_game_desc).
enum {
    BMF_CORE_DEFAULT_TICK_RATE = 60,
    BMF_CORE_MIN_TICK_RATE = 30,
    BMF_CORE_MAX_TICK_RATE = 240,
    BMF_CORE_MAX_STATE_SIZE = 1 << 30,       // 1 GiB
    BMF_CORE_MAX_SFX_PER_TICK = 256,
    BMF_CORE_NAME_MAX = 63,                  // bytes of bmf_game_desc.name
};

// ---------------------------------------------------------------- reflection fields (SPEC §8.4)
// Types of reflected state fields. The control protocol reads and writes the state through the
// field table (get/set/dump, task 1A-30); the engine validates the table at startup.
typedef enum bmf_field_type {
    BMF_T_I32 = 0,
    BMF_T_U32 = 1,
    BMF_T_I64 = 2,
    BMF_T_U64 = 3,
    BMF_T_F32 = 4,
    BMF_T_V2 = 5,
    BMF_T_V3 = 6,
    BMF_T_V4 = 7,
    BMF_T_B32 = 8,
    BMF_T_U8 = 9,
    BMF_T_HANDLE = 10,   // bmf_handle (u32)
    BMF_T_RNG = 11,      // bmf_rng (two u64: state, inc), e.g. the game's random number generator
    BMF_T_COUNT = 12,
} bmf_field_type;

// One reflected field of the state block. `count` > 1 makes it an array; `count_field` then may
// name an I32 scalar field that holds the live element count (0 = all `count` elements live).
typedef struct bmf_field {
    const char* name;          // path segment(s): 1..63 bytes of a-z A-Z 0-9 _ . ; unique
    bmf_field_type type;
    i32 offset;                // bytes from the start of the state block, aligned to the type
    i32 count;                 // elements, >= 1
    const char* count_field;   // name of the live-count field, or 0
} bmf_field;

enum {
    BMF_FIELD_NAME_MAX = 63,
    BMF_FIELD_MAX_COUNT = 4096,   // fields per game
};

// Size in bytes of one element of a field type (I32 4, I64 8, V2 8, V3 12, V4 16, U8 1, RNG 16,
// ...); 0 for an invalid type. O(1). Allowed in tick.
i32 bmf_field_type_size(bmf_field_type type);

// ---------------------------------------------------------------- context
// Passed to every game callback. The engine owns it; games read its fields and never write them.
// The same pointer is passed to every call for the whole run.
typedef struct bmf_ctx {
    i64 tick;             // init: 0. tick: index of the tick running (0 for the first). draw: ticks completed
    i32 tick_rate;        // ticks per second (30..240, D-32)
    f32 dt;               // seconds per tick, exactly 1.0f / tick_rate (real division, R2)
    u64 seed;             // the run's seed (--seed, default 0), also given to init
    bmf_v2 view_size;     // draw only: the render target in logical pixels. Never read it in tick
                          // (presentation state, D-37 rule 8)
    bmf_arena* level;     // reset by the game on demand (D-16); not part of the state block
    bmf_arena* frame;     // reset by the engine after every rendered frame
} bmf_ctx;

// ---------------------------------------------------------------- the game
typedef struct bmf_game_desc {
    u32 api_version;            // BMF_API_VERSION
    const char* name;           // 1..63 bytes; the window title
    i32 state_size;             // bytes of the state block, 1..BMF_CORE_MAX_STATE_SIZE
    u64 state_layout_hash;      // hash of the field table: a debug build refuses a wrong value and prints
                                // the right one; a change resets the state on hot reload
    i32 tick_rate;              // Hz, 30..240; 0 = 60 (D-32)
    bmf_memory_desc memory;     // arena sizes in bytes, each 0 (default) or 64 KiB..4 GiB (D-39)
    const bmf_field* fields;  i32 field_count;     // reflection (SPEC §8.4); may be 0, 0
    const bmf_action* actions; i32 action_count;   // input actions (SPEC §10); may be 0, 0
    const char* const* zone_names; i32 zone_name_count;   // names of zones BMF_ZONE_GAME_FIRST + i (0..32); may be 0, 0
    // Called once with the zeroed state block (and again after a hot reload that resets it).
    // Deterministic: everything it does must depend only on `seed`.
    void (*init)(bmf_ctx* cx, void* state, u64 seed);
    // One fixed step of dt seconds. Deterministic (D-18, D-37): reads only the state and `in`.
    void (*tick)(bmf_ctx* cx, void* state, const bmf_input* in);
    // Renders the state with the bmf_draw_* functions; must not change it. `alpha` in [0, 1) is how far real time
    // is between the last tick and the next (interpolate prev/current positions with it).
    void (*draw)(bmf_ctx* cx, const void* state, f32 alpha);
} bmf_game_desc;

// The one symbol a game exports (D-20). Defined in the game's game.c.
BMF_EXPORT const bmf_game_desc* bmf_game(void);

// ---------------------------------------------------------------- simulation-to-presentation events
// Queues a sound to play after the current tick (SPEC §8.3). Presentation only: not part of the
// state, not hashed, never read back by the simulation. `sound` is a sound id (assets arrive in
// M8; until then sounds are counted, not played), `volume` 0..1, `pan` -1 (left) .. 1 (right).
// Only in init and tick. At most BMF_CORE_MAX_SFX_PER_TICK per tick; more are dropped and
// counted. O(1). Allowed in tick (it is meant for tick).
void bmf_sfx_play(bmf_ctx* cx, u32 sound, f32 volume, f32 pan);

#endif // BMF_H
