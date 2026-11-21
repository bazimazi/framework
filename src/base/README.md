# src/base

Foundation layer included first by every binary (runtime, game DLL, tests, build.c). No CRT (D-03), no OS calls: everything OS-specific goes through `bmf_platform_*` hooks implemented by `src/platform/`.

## Files
- `base_types.h`: fixed-width types (SPEC §5.1, D-15), `BMF_STATIC_ASSERT`, OS/arch detection (`BMF_OS_*`, `BMF_ARCH_*`, always defined to 0 or 1), `BMF_DEBUG`, `BMF_SANITIZE`, `BMF_EXPORT`, `BMF_ASSERT`, `bmf_assert_fail`, `bmf_panic`.
- `base_builtins.c`: `memcpy`/`memset`/`memmove`/`memcmp`, `_fltused` and `__chkstk` (x64), and the `bmf_mem*` wrappers.
- `base_memory.{h,c}`: `bmf_arena`, the scratch pair, the `bmf_memory` process layout (D-16, D-39) and `bmf_pool`. Platform hooks: `bmf_platform_reserve/commit/decommit`.
- `base_math.{h,c}`: deterministic scalar math (D-17): exact ops inline (`sqrt`, `floor/ceil/trunc/round`, `fabs`, `fmin/fmax`, `bmf_f32_to_i32`, bit casts), transcendental functions in the .c (`sin cos sincos tan asin acos atan atan2 exp exp2 log log2 pow fmod hypot`), `BMF_PI`/`BMF_TAU`/`BMF_HALF_PI`/`BMF_F32_MAX`/`BMF_F32_EPSILON`; inline `lerp/clamp/saturate/smoothstep`, `bmf_v2/v3/v4`, `bmf_rot` (cos/sin pair), `bmf_m3x2` (columns x_axis, y_axis, origin; `mul(a, b)` applies b first), `bmf_rect` (x, y, w, h, half-open), `bmf_aabb` (min, max, closed), `bmf_color` (`u32` 0xRRGGBBAA sRGB: pack, to/from v4, premultiply); fast tier `bmf_fast_sin/cos/sincos/atan2/exp/exp2/log/log2/pow` (f32, render and audio only) and the `BMF_SIMULATION_FORBIDDEN` name table for the lint (1A-41).
- `base_simd.h`: `bmf_f32x4`/`bmf_i32x4`/`bmf_u32x4` (ext_vector_type), load/store, select, min/max, abs, sqrt, fixed-order sum. No rcp/rsqrt.
- `base_string.{h,c}`: `bmf_str` (eq, starts/ends_with, find, slice, trim, split iterator), C-string helpers, UTF-8 decode/encode, UTF-16 <-> UTF-8, `bmf_fmt_buf/_va/_arena` (`%i %u %x %li %lu %lx %f %.Nf %c %s %S %v2 %%`, width, `-`, `0`), `bmf_f32_to_text` (shortest round-trip), `bmf_parse_i32/i64/f32` (correctly rounded), `bmf_json_writer`. `bmf_err` lives in `base_types.h` (`OK`, `INVALID`, `RANGE`, `NOT_FOUND`, `IO`, `UNSUPPORTED`, `PENDING`).
- `base_hash.{h,c}`: `bmf_hash` + streaming `bmf_hasher` (own 4-lane multiply-rotate design, documented in the header), `bmf_crc32`, `bmf_adler32`.
- `base_rng.h`: PCG32 `bmf_rng` (16 bytes, lives in state): seed, next_u32, f32 in [0,1), unbiased range_i32, range_f32, normal (Box-Muller on `bmf_logf`/`bmf_cosf`).
- `base_sort.{h,c}`: stable LSD radix sort for u32/u64 keys with u32 payloads (8-bit digits, constant digits skipped, n <= 32 insertion sort), plus the insertion sorts.

## Invariants
- `BMF_ASSERT` is for programmer errors and compiles to nothing when `BMF_DEBUG=0`. `bmf_panic` is for unrecoverable runtime errors and is active in every build. Both call `bmf_platform_fatal(file, line, kind, what)` and then `__builtin_trap()`.
- Every binary must define `bmf_platform_fatal` exactly once (win32: `src/platform/win32/win32_core.c`; build.c: its own).
- The mem functions carry `__attribute__((no_builtin))` so clang cannot compile their bodies into calls to themselves.
- Under `BMF_SANITIZE=1` none of the CRT symbols is defined: the CRT and ASan provide them (D-09). Arenas then poison/unpoison for ASan.
- `__int128` and `long double` are not used (no compiler-rt helpers on wasm32, spike P0-04).
- Arenas commit in 64 KiB granules (`BMF_PAGE_GRANULE`) and stay committed after pop/reset. A never-committed guard granule follows every arena: running off the end faults. Exceeding a reserve is a `bmf_panic` (budget error, D-39). Debug fill: 0xCD on push, 0xDD on pop/reset.
- `bmf_memory_init` reserves one range: permanent, game_state, level, frame, scratch0, scratch1, each followed by a guard granule. Defaults 64 / state_size / 64 / 32 / 16 / 16 MiB.
- Math: transcendental functions compute in f64 with only + - * / sqrt floor and integer ops, then round to f32 once; no libm, no FMA (-ffp-contract=off), so results are identical on every target and optimization level. Trig reduction is exact for every finite input: Cody-Waite (3-part pi/2) below 2^17, Payne-Hanek with a 2/pi bit table above. All are faithfully rounded (< 1 ULP); measured: correctly rounded on all 1.3M test samples. Special inputs follow C99 Annex F.
- Float text: `%f`/`bmf_f32_to_text`/`bmf_json_f32` print the shortest decimal that parses back to the same f32 (closest among the shortest), JavaScript notation ("0.1", "1e+21", "-0"), at most 22 bytes. Parsing is exact: an f64 fast path (19 digits, error < 2^-51) is used unless the value lies that close to an f32 rounding boundary; then 768-bit integer arithmetic decides (120 significant digits kept, the rest as a sticky digit). `%.Nf` is exact (binary value rounded half to even). No locale, no libc.
- `bmf_fast_*` is never used in simulation code (D-37 rule 4): its precision may change in any release. The determinism lint (1A-41) greps simulation modules for every `BMF_SIMULATION_FORBIDDEN` entry.
- RNG: at most one `bmf_rng_*` draw per full expression. Argument/operand evaluation order is unspecified in C and clang really differs: win64 evaluates call arguments right-to-left, wasm32 left-to-right (found by the 1A-04b cross-target hash check).
- Pools hold no pointers: `bmf_pool` plus one caller-owned `bmf_pool_slot[capacity]` array (12 bytes/slot), so they can live in the state block (D-21). Handles are `index:20 | generation:12`; generations run 1..4095, so handle 0 is never valid. LIFO free list, swap-remove dense list: both orders depend only on the alloc/free sequence (deterministic).

## Hot paths
- x64 `memcpy`/`memset`/`memmove` use `rep movsb`/`rep stosb` (fast with ERMSB/FSRM). Small constant-size copies never reach them: clang inlines `__builtin_memcpy` with constant sizes.
- wasm/arm64 versions are byte loops; wasm normally emits `memory.copy`/`memory.fill` inline instead.
- `bmf_arena_push_aligned`: a few adds and one compare; commit only on growth. Pool alloc/free/valid: O(1), no loops.
- Text throughput (win64 -O2 LTO, random f32 bit patterns): `bmf_f32_to_text` 54 ns, `bmf_parse_f32` 29 ns, `bmf_fmt_buf("%i")` 21 ns. Debug -O0 is about 6x slower.
- Math throughput, ns/call (win64 -O2 LTO, x86-64-v2, random inputs, 2026-11-19): sqrt 0.3, floor 0.3, round 0.9, hypot 1.1, exp 5.1, exp2 4.8, log 5.0, log2 4.9, cos 6.6, sin 6.9, tan 7.3, fmod 7.5, sincos 7.8, atan 8.3, atan2 11.5, acos 14.1, asin 14.3, pow 15.5, sin of |x| >= 2^17 14.3, rng_normal 13.8. Fast tier: log2 1.5, exp2 2.1, exp 2.4, sincos 4.3, sin 5.5, atan2 5.7, pow 5.8. Trig quadrants use bit selects, not branches (random angles mispredict: sin was 11.7 ns with branches).

## Limits
- `__chkstk` is x64-only asm (MSVC ABI). arm64 Windows is not a target.
- Scratch arenas are main-thread only until the job system (1B-12) gives workers their own pair (CRT-free TLS is not decided yet).
- A pool handle held across 4095 reuses of its slot aliases a newer handle (12-bit generation).
- SIMD min/max are compare+select, not minps/f32x4.min (those differ on -0 and NaN across targets). Every SIMD op is tested bit-identical to its scalar twin.
- Math has no SIMD versions yet; a vector path must reproduce the scalar bits exactly (D-37 rule 7).
- `bmf_crc32` builds its table on first use (not thread-safe on the very first call).
- Tests: `tests/test_base_builtins.c` (sizes 0..1024 x 16 alignments, overlap both ways, 256 KiB stack frame, assert/panic crash) and `tests/test_base_memory.c` (alignment, fill patterns, commit growth, guard fault, overflow panic, layout, scratch, 4096 generation cycles, 20 000 random pool steps), `tests/test_base_simd.c` (50 000 random vectors vs scalar, bit-exact), `tests/test_base_hash_rng_sort.c` (stream = one-shot, avalanche, CRC/Adler vectors, PCG32 demo vectors, range uniformity, radix vs insertion sort incl. stability), `tests/test_base_string.c` (str ops, all code points, UTF-16 round trips, every fmt specifier, exact `%.Nf`, 10M format/parse round trips, shortness, parse vs an exact big-integer interval check incl. midpoints, JSON escaping/overflow), `tests/test_math.c` (100 000 samples per function vs the double-double/big-integer reference in `tests/gen/math_ref.c`, output-hash goldens, exact ops, C99 special values, both reductions agree, constants recomputed, rng normal moments).
