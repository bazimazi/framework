# Spike P0-06: cross-platform float determinism

Date: 2026-11-18. Toolchain: llvm 23.1.6. Browser: Edge 157 headless (V8). CPU: the owner's dev machine. Spike code: `spikes/det/` (throwaway).

## Result: PASS. win64 and wasm32 produce bit-identical state hashes. D-37 stands.

## Test

`det.c`, one file for both targets:
- Draft deterministic `sinf`/`cosf` (Cody-Waite range reduction plus Taylor kernels), `atan2f` (two-step reduction plus Taylor), `expf` (ln2 reduction, Taylor, exponent-bit scaling). All computed in `f64` with only `+ - * /`, `sqrt`, `floor` and compares, rounded to `f32` once.
- 2 000 circles in a 10×30 m box: gravity whose direction rotates through `sincosf(0.3 t)`, damping `expf(-0.05 dt)` each tick, walls with restitution, sort-and-sweep broadphase (insertion sort by `(x, index)`), pairwise overlap response using `sqrt` and `/`, contact angle stored via `atan2f`.
- Detector fields in the state: `heat` (pushed into the denormal range on every contact and decayed ×0.25 per tick: an FTZ/DAZ detector), `cell` (`px * 1e9 - 5e9` converted to `i32`, out of range for most bodies: a cast detector), `angle` (atan2 output).
- 10 000 ticks at 60 Hz; a 64-bit hash of the whole state struct after every 100 ticks (100 checkpoints).
- `USE_SIMD` replaces the integration step with `ext_vector_type(4)` code.

## Configurations that MUST match (all 100 checkpoints compared)

| Config | First hash | Last hash | Match |
|---|---|---|---|
| win64 `-O2 -march=x86-64-v2`, scalar | 96719a00f2d13a8f | c81f14a12e766df9 | reference |
| win64 `-O2`, SIMD integration | same | same | yes |
| win64 `-O0` | same | same | yes |
| win64 `-O2 -flto` | same | same | yes |
| win64 `-O2 -march=x86-64-v3` (AVX2+FMA available, `-ffp-contract=off`) | same | same | yes |
| wasm32 `-O2`, scalar, in Edge | same | same | yes |
| wasm32 `-O2`, SIMD integration, in Edge | same | same | yes |
| wasm32 `-O0`, in Edge | same | same | yes |

So: scalar and SIMD paths give identical bits (rule 7), optimization level and LTO do not change results, and an AVX2/FMA-capable target stays identical as long as contraction is off.

## Deliberate rule breaks (each MUST change the hash)

| Break | Config | Result |
|---|---|---|
| FMA contraction (rule 9) | win64 `-march=x86-64-v3 -ffp-contract=fast` | differs from first checkpoint on (`c21250ff...`) |
| Approximate reciprocal sqrt (rule 9) | win64 `_mm_rsqrt_ss` for normals | differs (`0deadcc1...`) |
| FTZ/DAZ on (rule 10) | win64 `MXCSR |= 0x8040` | differs (`092df3a4...`) |
| Raw float-to-int cast (rule 12) | win64 `(i32)big` | differs (`6986f056...`): x64 gives `INT_MIN` |
| Raw float-to-int cast (rule 12) | wasm32 `(i32)big` | **same as reference**: wasm saturates, which equals our helper's semantics. The break is caught only by the cross-target comparison, which is exactly what the 1B-46 gate does. |
| Relaxed SIMD + contraction (K-03) | wasm32 `-mrelaxed-simd -ffp-contract=fast` | differs (`6b4307a6...`) |

Every break is detectable, so the hash gate (1B-46) and the lint (1A-41) have real teeth. Note that `-ffp-contract=fast` alone on `x86-64-v2` would change nothing (no FMA instructions exist there); the danger appears only on targets with FMA (x86-64-v3, arm64, relaxed wasm). The flag must stay `off` everywhere.

## Timing (10 000 ticks, 2 000 bodies, single thread)

| Target | ns/tick (O2) | ns/tick (O0) |
|---|---|---|
| win64 scalar | 635 670 | 1 303 190 |
| win64 SIMD | 624 137 | - |
| wasm32 in Edge, scalar | 723 925 | 1 596 175 |
| wasm32 in Edge, SIMD | 711 222 | - |

wasm runs at about 0.88× native speed for this float-heavy loop. The toy broadphase dominates; these are not physics-module numbers.

## Notes and recommendations

1. Computing transcendental functions in `f64` and rounding once is the simplest way to get both determinism and accuracy, and it is identical across targets because `f64` basic operations are IEEE-exact on SSE2, wasm and arm64. Recommendation for 1A-04a: keep this structure for the deterministic tier and measure the ULP error against the reference generator. (The drafts here are not yet ULP-verified.)
2. `__builtin_floor` lowers to `roundsd` (SSE4.1, in the x86-64-v2 baseline) and to `f64.floor` on wasm. No libm call at `-O0` or `-O2`.
3. Pass `-fno-math-errno` on every build. The MSVC target defaults to `-fmath-errno`, which allows clang to emit a libm `sqrtf` call instead of the instruction.
4. The determinism lint (1A-41) should flag `-mrelaxed-simd`, `-ffp-contract=` values other than `off`, `_mm_rsqrt*`/`_mm_rcp*`, `_mm_setcsr`, raw float-to-int casts, `__int128` and `long double` in simulation code.
5. arm64 is not tested (no device). Rule 10 requires FPCR setup there; revisit in Phase 3.
