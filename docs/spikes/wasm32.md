# Spike P0-04: freestanding wasm32

Date: 2026-11-18. Toolchain: llvm 23.1.6. Browser: Edge 157 headless. Spike code: `spikes/wasm32/` (throwaway).

## Result: works

A freestanding module built with clang + wasm-ld alone instantiates in headless Edge, imports only `env.memory` and the declared JS function, and returns correct results for i32/u64/i64 math, struct copies, SIMD and float-to-int conversion.

## Exact flags

```
--target=wasm32 -std=c11 -O2 -nostdlib -ffreestanding -fno-builtin-printf
-ffp-contract=off -fno-fast-math -msimd128 -mbulk-memory -mnontrapping-fptoint
-Wl,--no-entry -Wl,--export-dynamic -Wl,--import-memory
-Wl,--allow-undefined-file=<imports.txt>      (list of JS-provided functions)
```
Exports use `__attribute__((export_name("x")))`; JS imports use `__attribute__((import_module("env"), import_name("x")))`. `--allow-undefined-file` keeps any *other* undefined symbol a link error, which is what we want.

Feature defaults in LLVM 23 for `wasm32` (generic CPU, no flags): bulk-memory, bulk-memory-opt, multivalue, mutable-globals, nontrapping-fptoint, reference-types, sign-ext. SIMD128 needs `-msimd128`. **Relaxed SIMD is off by default** (`__wasm_relaxed_simd__` not defined) and is enabled only by `-mrelaxed-simd` (K-03). `build.c` must never pass it, and should reject it in any user flag list (amendment A-09).

With `-ffp-contract=off`, `a*b+c` on `f32x4` compiles to `f32x4.mul` + `f32x4.add` (no fused op).

## Builtins needed

| Source pattern | Emitted | Decision |
|---|---|---|
| struct zero-init / copy, `__builtin_memmove` | `memory.fill` / `memory.copy` inline (bulk memory) | Nothing to provide at -O0 or -O2. Still provide own `memcpy`/`memset`/`memmove`/`memcmp` in `base_builtins.c` for explicit calls and for safety. |
| `u64` and `i64` `* / % << >>` | native `i64.*` ops | nothing |
| `unsigned __int128` multiply / divide | `__multi3`, `__udivti3` (link error) | **Forbid `__int128` in the runtime** (lint in 1A-41). LLVM ships no wasm32 compiler-rt, so providing them would be our own code anyway; nothing needs them. |
| `long double` | f128 soft-float (`__multf3`, `__extenddftf2`, ...) | **Forbid `long double`** (lint). In the spike the single use was folded away, so the link did not fail; do not rely on that. |
| float to int casts | `i32.trunc_sat_f32_s` (saturating) | Use `bmf_f32_to_i32` only (D-37 rule 12). Verified: wasm gives `(i32)3e9 = 2147483647`, `(i32)NaN = 0`, `(i32)-3e9 = -2147483648`. x64 `cvttss2si` gives `INT_MIN` for all three, so raw casts *do* differ across targets. |

## JS side

`spikes/wasm32/index.html` (35 lines): fetch, `WebAssembly.compile`, list imports, instantiate with an imported `WebAssembly.Memory`, call exports, POST JSON to the test server (P0-07). BigInt is used for i64 parameters and results.

## Sizes

- spike module (7 exports incl. SIMD loop, struct ops, 4 KiB static data): 1 548 bytes at -O2, 1 410 bytes at -O0.

## Timing

Headless Edge launch to result POST: about 1.5 s (cold profile directory).

## Inputs for later tasks

- 1A-02: no wasm-specific builtins file is needed; one `base_builtins.c` serves both targets (mem functions only on wasm; `_fltused`/`__chkstk` x64 only).
- 1A-41 lint: forbid `__int128`, `long double`, raw float-to-int casts.
- 1B-40: imports list file for `--allow-undefined-file`.
