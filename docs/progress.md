# Progress log

Append-only history of session summaries and task proposals. Status lives only in the `PLAN.md` §0 tracker.

---

## 2026-11-18 — P0-02 Repository skeleton

Environment check at session start: clang, lld-link and wasm-ld are **not installed** (`C:\Program Files\LLVM` absent; VS 18 ships only clang-format and clang-tidy). Owner action O-1 is still open, so every task except P0-02 is blocked.

1. What changed:
   - `src/{base,platform/win32,platform/web,gpu/d3d11,gpu/webgpu,draw2d,physics2d,audio,input,asset,job,debug}/.keep`: SPEC §3 runtime tree.
   - `shaders/{d3d11,webgpu}/.keep`, `tools/{bake,serve,test}/.keep`, `games/hello/.keep`, `tests/.keep`: rest of the SPEC §3 tree.
   - `docs/spikes/.keep`: home for Phase 0 spike reports.
   - `.gitignore`: `out/`, `spikes/*/out/`.
   - `.gitattributes`: LF text, binary for png/bmfr/pak/state/wav.
   - `docs/decisions.md`: seeded with D-01…D-37, one line each.
   - `docs/friction.md`, `docs/progress.md`: created.
   - `src/core/` NOT created: it depends on amendment A-05, which is not yet approved.
2. Test results: no build exists yet (no toolchain). Acceptance checked by listing the tree against SPEC §3 and grepping `docs/decisions.md` for all 37 IDs.
3. Perf: n/a.
4. Golden files updated: none.
5. Open issues: O-1 (install LLVM) blocks P0-01 and everything after it. O-2 (git init) still open.

---

## 2026-11-18 — O-1, O-2, P0-01, P0-03 … P0-08 (Phase 0 spikes)

Owner approved in session: install LLVM via winget (O-1) and `git init` (O-2). LLVM 23.1.6 installed; `_framework` is now a git repository with no commits.

1. What changed:
   - `toolchain.txt`: `llvm 23.1.6`.
   - `docs/setup.md`: install, SDK path, system components, verify commands.
   - `docs/spikes/crt_free.md` (P0-03), `wasm32.md` (P0-04), `sanitize.md` (P0-05), `determinism.md` (P0-06), `webtest.md` (P0-07), `d3d11.md` (P0-08): spike reports.
   - `spikes/{crt_free,wasm32,sanitize,det,webtest,d3d11}/`, `spikes/env.sh`: throwaway spike code (outputs in gitignored `spikes/*/out/`).
   - `PLAN.md`: tracker updated; amendment proposals A-11, A-12, A-13 added to §0 and §9.
2. Test results (no `build test` exists yet; spike checks):
   - P0-03: CRT-free exe + DLL load/call: pass. Own `memset`/`memcpy`/`_fltused`/`__chkstk` needed; `-flto` needs `/include:_fltused`.
   - P0-04: wasm module in headless Edge: pass. Only `__int128` needs compiler-rt; forbid it and `long double`.
   - P0-05: UBSan trap mode CRT-free: pass (3/3 traps). ASan: works only with CRT startup and without own mem functions; proposed as a separate `sanitize` config.
   - P0-06: **PASS.** 100 checkpoint hashes over 10 000 ticks identical across win64 (scalar, SIMD, -O0, LTO, x86-64-v3) and wasm32 in Edge (scalar, SIMD, -O0). All deliberate breaks (FMA contraction, rsqrt, FTZ, raw cast, relaxed SIMD) change the hash; the raw-cast break shows only cross-target, as expected.
   - P0-07: headless Edge harness with POST reporting: pass. WebGPU without a GPU: `--enable-unsafe-webgpu --use-webgpu-adapter=swiftshader --disable-gpu`.
   - P0-08: CRT-free WARP render + readback: pass, deterministic across runs; all 18 hand-declared vtable slots match the SDK.
3. Perf: P0-06 toy sim 2 000 bodies: 0.64 ms/tick win64, 0.72 ms/tick wasm (Edge). WARP device creation 4.5–6 ms.
4. Golden files updated: none.
5. Open issues / proposals for the owner (O-3):
   - Review the six spike reports and decide amendments A-01…A-13. A-03 now has a concrete recommendation (UBSan trap in `debug`, separate ASan `sanitize` config). A-12 (CRT-free `build.c` + `bootstrap.bat`) is needed before 1A-01 can be written, because plain `clang build.c` cannot find CRT headers on this machine.
   - Windows Defender result (K-08) not testable here: Defender is disabled on this machine.
   - Phase 0 stays open until O-3 is done; 1A-01 is technically unblocked by its listed deps (P0-01, P0-03, P0-05) but depends on the A-01/A-12 decision.

---

## 2026-11-19 — O-3, amendments A-01…A-13, 1A-01, 1A-02, 1A-03, 1A-05, 1A-07, 1A-08

Owner approved all amendments A-01…A-13 (O-3). Applied to `SPEC.md` (v0.3) and logged in `docs/decisions.md` (D-02, D-04, D-09, D-12 revised; new D-38 null GPU backend, D-39 per-game memory budget, D-40 physics sleeping, D-41 per-backend goldens; `src/core/` added to the layout). Phase 0 closed.

Order note: 1A-08 (test runner) was done before 1A-06 because 1A-02's acceptance needs unit tests; the runner uses a small local text writer until `bmf_json_writer` exists (step added to 1A-06). 1A-05 and 1A-07 were done before 1A-04 (all deps met; both small).

1. What changed:
   - `SPEC.md`, `docs/decisions.md`, `PLAN.md`: amendments applied, tracker and task notes updated.
   - `bootstrap.bat`: finds clang + newest Windows SDK, builds the CRT-free `build.exe`.
   - `build.c`: `win64 debug|release <game>`, `test [filter]`, `clean`; toolchain/SDK discovery, version pin check, flag tables (D-09 as amended, banned-flag check), generated unity roots in `out/gen/`, per-binary input-hash stamps, JSON summary line.
   - `src/bmf.c`: unity root with build modes (runtime exe with DLL/static game link, game DLL, tests).
   - `src/base/base_types.h`, `base_builtins.c`, `base_memory.{h,c}`, `base_simd.h`, `base_hash.{h,c}`, `base_rng.h`, `base_sort.{h,c}`, `src/base/README.md`.
   - `src/platform/win32/win32_api.h` (kernel32 subset), `win32_core.c` (fatal hook, virtual-memory hooks), `win32_main.c` (stub entry until 1A-23), `src/platform/README.md`.
   - `tools/test/test_main.c` + `README.md`; `tests/tests.c`, `test_runner.c`, `test_base_builtins.c`, `test_base_memory.c`, `test_base_simd.c`, `test_base_hash_rng_sort.c`.
   - `games/_empty/game.c`: build-pipeline test game.
   - `.gitignore` (+`build.exe`), `.gitattributes` (+`*.bat` CRLF), `docs/setup.md` (bootstrap), `src/core/.keep`.
2. Test results: `build test` 27/27 passed (clean build). `build win64 debug games/_empty` and `release` build and run (exit 0); second run reports `up_to_date`; a game-only edit rebuilds only `game.dll`; a compile error passes through as `file:line:col: error:`; toolchain mismatch, missing clang and bad usage give JSON errors. All base modules link for wasm32 at -O0 and -O2+LTO with no undefined symbols.
3. Perf: no hot path existed before; no benchmarks yet (profiler arrives in 1A-36). Sizes: release `game.exe` for `_empty` 2 560 bytes. Build times: debug 0.3 s, up-to-date check 0.05 s, `build test` compile 0.2 s.
4. Golden files updated: none.
5. Open issues / proposals:
   - **Proposal (SPEC §16):** allow one test-only macro `BMF_CHECK(t, cond, msg)` that captures `__FILE__`/`__LINE__`. Today every check passes `__FILE__, __LINE__` explicitly because §16 allows no other function-like macro. Owner decision.
   - Scratch arenas are main-thread only. Worker threads (1B-12) need per-thread scratch: CRT-free implicit TLS needs our own `_tls_used` directory, or explicit `TlsAlloc`. To decide in 1B-12.
   - The runtime exe links with `/subsystem:console` in every config for now (agent stdio). Revisit windowed release behavior in 1A-12/1A-23.
   - Next tasks: 1A-04 (deterministic math, L, split a/b/c; includes `bmf_rng_normal`), 1A-06 (strings, shortest round-trip float format, JSON writer). Then M1 is complete.

---

## 2026-11-19 — 1A-04a deterministic scalar math

1. What changed:
   - `src/base/base_math.h`: constants, bit casts, inline exact ops (`bmf_sqrtf`, `floorf/ceilf/truncf/roundf`, `fabsf`, `fminf/fmaxf` with defined -0/NaN rules, saturating `bmf_f32_to_i32`), declarations with contract comments.
   - `src/base/base_math.c`: `sin cos sincos tan asin acos atan atan2 exp exp2 log log2 pow fmod hypot`. f64 internals, one rounding to f32. Exact trig reduction (Cody-Waite below 2^17, Payne-Hanek above); exact integer `fmod`.
   - `src/base/base_rng.h`: `bmf_rng_normal` (Box-Muller, moved here from 1A-07).
   - `src/bmf.c`: includes base_math.
   - `tests/test_math.c`, `tests/gen/math_ref.c`: ULP tests vs a big-integer + double-double reference, output-hash goldens, exact ops, special values, reduction-path agreement, constants, rng normal. `tests/tests.c`: 19 new entries.
   - `tools/test/test_main.c`: `bmf_test_note` and the `"note"` output field.
   - `src/base/README.md`, `tools/test/README.md`, `PLAN.md` (tracker, deviation note).
2. Test results: `build test` 46/46 passed. Every function: max error 0.4999 ULP, 0 of 100 000 samples not correctly rounded (13 functions). Output hashes identical at -O0 + UBSan, -O2 + LTO and `-march=x86-64-v3` (manual builds of the test runner). Extra cross-target check: a driver hashing 21 outputs (all functions plus `rng_normal`) over 4 000 inputs gives identical hashes for win64 (-O0, -O2 LTO) and wasm32 in Node 24 / V8 (-O0, -O2 LTO). The full wasm run of the 100 000-sample suite stays pending until M9 (1A-04 acceptance). wasm32 links with no imports (no libm calls). `_empty` debug and release build and run; release exe still 2 560 bytes.
3. Perf (new code, no before; win64 -O2 LTO throughput, ns/call): sqrt 0.3, floor 0.3, round 0.9 (3.9 before making it branchless), hypot 1.1, exp 5.3, exp2 4.8, log 5.0, log2 5.0, fmod 7.5, atan 7.8, tan 9.1, cos 9.7, sin 11.7, sincos 12.1, atan2 12.3, acos 13.8, asin 14.2, pow 14.9, sin with |x| >= 2^17 16.8, rng_normal 15.2.
4. Golden files updated: none. New golden constants: `MATH_OUTPUT_HASH[]` in `tests/test_math.c` (13 per-function output hashes, first recorded).
5. Open issues / proposals:
   - Deviation from the plan text: no generated `tests/gen/math_ref.inc` (would be tens of MB); the reference runs at test time. Noted in the 1A-04 section.
   - The math ULP tests take about 4 s of `build test` (debug -O0 + UBSan). If iteration speed suffers, a later `build test` could build the runner at -O2 as well (needs a decision on test configs).
   - Building the test runner with `BMF_DEBUG=0` fails on an unused variable in `tests/test_base_builtins.c` (assert test). Tests only build in debug today, so this is latent.

---

## 2026-11-19 — 1A-04b vector and matrix types

1. What changed:
   - `src/base/base_math.h`: inline scalar helpers (`bmf_lerpf`, `clampf`, `saturatef`, `smoothstepf`, `clamp/min/max_i32`), `bmf_v2/v3/v4` operations, `bmf_rot`, `bmf_m3x2` (TRS, mul, apply point/vector, determinant, inverse), `bmf_rect`, `bmf_aabb`, `bmf_color` pack/unpack/to/from v4/premultiply.
   - `src/base/base_rng.h`, `src/base/README.md`: the one-draw-per-expression rule (below).
   - `tests/test_math.c`: 6 new tests (scalar helpers, vectors, rot/transform, rect/aabb, colors, vector output hash golden); sampling fixed to one random draw per full expression. `tests/tests.c`: entries.
   - `PLAN.md`: tracker.
2. Test results: `build test` 52/52 passed. The math tests also pass in manual -O2 + LTO and `-march=x86-64-v3` builds of the runner (there `base.builtins.assert_crashes` fails as expected: `BMF_DEBUG=0` removes asserts). Vector output hash identical on win64 and wasm32 (Node/V8) at -O0 and -O2 + LTO.
3. Perf: all new code is `static inline` with no loops; no hot path uses it yet. No numbers.
4. Golden files updated: none. New golden constant: `test_math_vector_output_hash` = `0xbf5f8646f1e92864`. The 13 `MATH_OUTPUT_HASH[]` values from 1A-04a are unchanged by the sampling fix (verified).
5. Open issues / proposals:
   - **Determinism finding:** the first cross-target run of the vector hash differed between win64 and wasm. Cause: `bmf_v2_make(rng(), rng())`. C leaves argument and operand evaluation order unspecified; clang evaluates call arguments right-to-left on win64 and left-to-right on wasm32. Fixed in the tests and documented in `base_rng.h` and `src/base/README.md`. **Proposal for 1A-41 (determinism lint):** flag any full expression in simulation code that contains two or more calls to `bmf_rng_*` (or more generally two calls with side effects on the same object). Also worth a line in the game-facing `API.md` / `AGENTS.md` once those exist, because game code will hit this.

---

## 2026-11-19 — 1A-04c fast tier (1A-04 complete)

1. What changed:
   - `src/base/base_math.h`: `bmf_fast_sincosf/sinf/cosf/atan2f/exp2f/expf/log2f/logf/powf` (inline f32 polynomials, documented error bounds, not allowed in tick) and `BMF_SIMULATION_FORBIDDEN` (0-terminated name table for the 1A-41 lint: `bmf_fast_`, rsqrt/rcp intrinsics, `_mm_setcsr`, fma, `relaxed_`, `__int128`, `long double`).
   - `src/base/base_math.c`: sin/cos/sincos/tan apply the quadrant with bit selects instead of branches (same output bits: all golden hashes unchanged, win64 = wasm re-verified).
   - `tests/test_math.c`: `base.math.fast_tier` (errors vs the deterministic tier over 200 000 samples, edges, forbidden table). `tests/tests.c`: entry.
   - `src/base/README.md`, `PLAN.md` (1A-04a/b/c and 1A-04 done, acceptance noted).
2. Test results: `build test` 53/53 passed. Measured fast-tier max errors: sincos 1.2e-7 abs (|x| <= 8192), atan2 2.4e-7 abs, exp2 2.5e-7 rel, exp 6.7e-7 rel (|x| <= 10) / 3.9e-6 rel (full range), log2 1.2e-7 * (1 + |log2 x|), pow 1.7e-7 * (1 + |y log2 x|) rel. Output hashes unchanged after the branch removal; the win64/wasm driver still gives identical hashes at -O0 and -O2 + LTO. `_empty` debug and release build and run.
3. Perf (win64 -O2 LTO, random inputs, ns/call), before -> after: sin 11.7 -> 6.9, cos 9.7 -> 6.6, sincos 12.1 -> 7.8, tan 9.1 -> 7.3, rng_normal 15.2 -> 13.8. Fast tier (new): log2 1.5, exp2 2.1, exp 2.4, sincos 4.3 (8.4 with branches), sin 5.5, atan2 5.7, pow 5.8.
4. Golden files updated: none.
5. Open issues / proposals:
   - 1A-04 acceptance, wasm part: the full 100 000-sample suite on wasm is pending M9 (the runner is win64-only). A 4 000-input driver covering every function already matches.
   - `bmf_atan2f`/`asinf`/`acosf` still branch in the atan reduction (11-14 ns). Could get the same bit-select treatment if a hot path needs it.

---

## 2026-11-19 — 1A-06 strings, format, parse, JSON writer (M1 complete)

1. What changed:
   - `src/base/base_string.{h,c}`: `bmf_str` ops and split iterator, C-string helpers, UTF-8 decode/encode, UTF-16 <-> UTF-8, `bmf_fmt_buf/_va/_arena`, `bmf_f32_to_text` (shortest round-trip), exact `%.Nf`, `bmf_parse_i32/i64/f32` (correctly rounded), `bmf_json_writer`.
   - `src/base/base_types.h`: `bmf_err` (`BMF_OK`, `BMF_ERR_INVALID`, `BMF_ERR_RANGE`).
   - `src/bmf.c`: includes base_string.
   - `tools/test/test_main.c`: local text writer removed; output via `bmf_json_writer`, messages via `bmf_fmt_buf`. `tests/test_math.c`, `tests/test_runner.c`: converted.
   - `tests/test_base_string.c` (12 tests), `tests/tests.c`.
   - `src/base/README.md`, `tools/test/README.md`, `PLAN.md` (tracker, M1 checked, algorithm note).
2. Test results: `build test` 65/65 passed. 10M random f32 bit patterns (plus both mantissa ends of every exponent) format then parse back exactly; max text 22 bytes. 100k samples: no shorter decimal round-trips. 30k parse inputs (1-9, 10-29 and 100-119 digits; exact midpoints, just above, just below) agree with an exact big-integer interval check, and fast path = exact path on all of them. A win64/wasm (Node) driver hashing 300k `%f|%.3f` outputs and parse results is identical at -O0 and -O2 + LTO. The parse test found and fixed a negative-shift bug in the exact path (long digit strings with a small negative exponent). `_empty` debug/release build and run; release exe still 2 560 bytes.
3. Perf (new code; win64 -O2 LTO, random f32): `bmf_f32_to_text` 54 ns, `bmf_parse_f32` 29 ns, `bmf_fmt_buf("%i")` 21 ns. Debug -O0: 310 / 162 / 157 ns.
4. Golden files updated: none.
5. Open issues / proposals:
   - `build test` now takes about 13 s at -O0 + UBSan (math ULP ~4 s, 10M round trip ~5 s). Proposal: build the test runner at -O2 too (or a `slow` test tier run less often). Owner decision on test configs.
   - Float algorithms are an own design (grid search + exact verification), not Ryu/Grisu as the plan text suggested; noted in the 1A-06 section. Same guarantees, verified.
   - M1 is complete. Next: M2 (1A-10 Win32 API declarations, 1A-11 platform core, ...).

## 2026-11-19 — 1A-10 Win32 API declarations

1. What changed:
   - `src/platform/win32/win32_api.h` (1485 lines). Additions:
     - kernel32: file mapping, file attributes/type, pipes/console, process info, system info, waitable timers, wait functions, threads (create, priority, description, stack guarantee), semaphores and events, `FreeLibrary`/`GetModuleHandleW`, crash handling (vectored handler, unhandled filter, `RtlCaptureStackBackTrace`).
     - synchronization: `WaitOnAddress`/`WakeByAddress*`.
     - user32: DPI, window class/creation/placement, messages, cursor, Raw Input, pointer input.
     - ole32/avrt: COM init, `CoCreateInstance`, MMCSS.
     - Structs for all of the above plus XInput, WASAPI wave formats, DXGI and D3D11 descriptors.
     - COM vtables: `ID3D11Device`, `ID3D11DeviceContext`, `ID3D11Texture2D`, `ID3D11Debug`, `ID3D11InfoQueue`, `IDXGIDevice`, `IDXGIAdapter`, `IDXGIFactory2`, `IDXGIFactory5`, `IDXGISwapChain2`, `ID3DBlob`, `IMMDeviceEnumerator`, `IMMDevice`, `IAudioClient`, `IAudioRenderClient`. A shared `IUnknownVtbl` covers objects that are only released.
     - 12 GUIDs; `*_fn` typedefs for the runtime-loaded `D3D11CreateDevice`, `CreateDXGIFactory2`, `XInputGetState/SetState` and `D3DCompile`.
     - Fix: `MSG` had the SDK's `_MAC`-only `lPrivate` field (size unchanged, 48).
   - `src/platform/README.md`: file description and invariants. `PLAN.md`: tracker, acceptance, a note for the 1A-14 slot test.
2. Test results:
   - `build test` 65/65 passed. `build.exe` (bootstrap), `_empty` win64 debug and release build with `-Werror` and run.
   - No `windows.h`/SDK header include in `src/`, `build.c`, `tools/` or `tests/`.
   - SDK comparison (throwaway generator: one host program built with our header, one with SDK 10.0.26100 headers, outputs diffed): 316 vtable slots by name and order; 49 structs (size, alignment, offset and size of 301 members); 351 constants; 12 GUIDs (bytes); 5 pointer constants. Mismatches: 0.
   - Parameter lists: 119 imported functions/loader typedefs and 128 typed COM methods match the SDK in parameter count and per-parameter size class (pointer/8/4/2/1); the remaining 6 (`Co*`, `XInput*`) were checked by hand. Mismatches: 0.
   - Both checkers were shown to catch planted errors (constant value, field type, GUID byte, slot shift, missing/extra/wider parameter).
   - A CRT-free program taking the address of all 121 imports links against kernel32/user32/ole32/avrt/synchronization and loads (exit 0).
3. Perf: no hot path touched. Header parse cost is not noticeable in build times (`_empty` full rebuild: debug 0.43 s, release 0.34 s).
4. Golden files updated: none.
5. Open issues / proposals:
   - The SDK comparison was a throwaway script. 1A-14 lands the permanent `tests/test_vtable_slots.c`; the method (two translation units, diffed tables) is noted in the 1A-10 section of `PLAN.md`.
   - Parameter types of COM methods are compared by size class, not exact type (e.g. an enum vs `u32`); exact types are by review against the MSDN names.

## 2026-11-20 — 1A-11 Win32 platform core

1. What changed:
   - `src/platform/platform.h` (new): the internal platform contract for process start/arguments, buffered stdio, time/sleep, files (read, map, atomic write, delete, info), file-change polling, DLLs, futex, semaphore, threads, CPU count; inline FPU setup/check, atomics and `bmf_cpu_pause`.
   - `src/platform/win32/win32_core.c`: Win32 implementation, plus `bmf_win32_split_command_line` (UCRT argv rules).
   - `src/platform/win32/win32_main.c`: calls `bmf_platform_init`; loads `game.dll` and prints through the platform layer.
   - `tools/test/test_main.c`: uses `bmf_platform_init`, platform arguments, time, stdout and exit (its private argument parser and QPC code are gone).
   - `src/base/base_types.h`: `BMF_ERR_NOT_FOUND`, `BMF_ERR_IO`.
   - `src/bmf.c`: includes `platform/platform.h`. `build.c`: links `synchronization` (`WaitOnAddress`).
   - `tests/test_platform.c` (10 tests), `tests/tests.c`.
   - `src/platform/README.md`, `tools/test/README.md`, `PLAN.md` (tracker, acceptance).
2. Test results: `build test` 75/75 passed (10 new `platform.*`). The command-line test cases were also run through a CRT host program: the UCRT's own `argv` matches every expected result. `_empty` debug and release build and run (exit 0; debug loads `game.dll` through `bmf_platform_library_load`).
3. Perf: new code only. `bmf_platform_time_ns` 22 ns/call (debug). A 2 ms `bmf_platform_sleep_ns` returns 0.6 ms late at worst when idle (2.7 ms under a loaded test run), never early. `_empty` full rebuild after `build clean`: debug 0.56 s, release 0.52 s (was 0.43 / 0.34 s without the platform core).
4. Golden files updated: none.
5. Open issues / proposals:
   - Stdio buffers are two static 64 KiB arrays, not arena memory as the plan text says: stdout must work before the arenas exist and at crash time. Same "no heap" property.
   - `bmf_platform_write`/`flush`/`sleep_ns` are main-thread only (debug asserts). Worker logging/sleep waits for the job system (1B-12) and the open TLS question.
   - Atomic write does not flush to disk (atomic against process crash, not power loss). Say if goldens/replays need `FlushFileBuffers`.
   - The FPU test cannot prove that the thread trampoline sets MXCSR (Windows starts every thread at 0x1F80 anyway); it proves the state user code sees.
   - No stdin reading yet; it lands with the control protocol (1A-3x).

## 2026-11-20 — 1A-12 Win32 window and events

1. What changed:
   - `src/platform/platform.h`: `bmf_key` (positional, 117 keys) with `bmf_key_name`/`bmf_key_from_name`; mouse buttons; `bmf_gamepad`; window open/close/size/fullscreen/native handle; `bmf_event` (20 types) and the queue API (`bmf_platform_poll_events`, `bmf_platform_event_pop`, `bmf_platform_events_dropped`). Atomic-write contract now documents the rename retry.
   - `src/platform/platform_common.c` (new): key-name table and the 1024-entry event ring (OS-independent, reused by the web port).
   - `src/platform/win32/win32_window.c` (new): window class/creation (per-monitor DPI v2, logical size), `WM_DPICHANGED`, borderless fullscreen, keyboard through a positional scan-code table, `WM_CHAR` with surrogate joining, mouse messages plus Raw Input deltas, `WM_POINTER` touch/pen (10 slots), focus-loss release, minimize/restore as suspend/resume, Alt/F10 menu suppression, XInput (runtime-loaded, radial dead zones, empty slots probed once per second).
   - `src/platform/win32/win32_main.c`: `--control` opens a window and prints every event as a JSON line (manual test; replaced by the protocol in 1A-31).
   - `src/platform/win32/win32_core.c`: atomic write retries a transiently denied rename (backoff up to 63 ms).
   - `src/platform/win32/win32_api.h`: `WM_MENUCHAR`, `MNC_CLOSE`, `VK_PAUSE/SNAPSHOT/NUMLOCK`, `MAPVK_VK_TO_VSC_EX`, `MapVirtualKeyW`, `SendMessageW`.
   - `src/bmf.c` includes the two new files; `build.c` links `user32`.
   - `tests/test_platform_window.c` (5 tests), `tests/test_platform.c` (atomic-write failure message names the step and error), `tests/tests.c`.
   - `src/platform/README.md`, `PLAN.md` (tracker, acceptance, note).
2. Test results: `build test` 80/80 passed, then 3 more full runs and 60 runs of `build test platform` all passed. The new Win32 constants and functions were compared with SDK 10.0.26100 by the 1A-10 scripts: 357 constants and 121 functions, 0 mismatches. Manual test: `game.exe --control` (debug `_empty`) driven by posted messages from PowerShell printed window_open, resized, focus_gained, key_down/up for W and F11 (repeat flagged), text, fullscreen resize to 1920x1080 and back to 960x540, mouse_move, mouse_wheel, close_requested, and exited. `_empty` release builds and runs (exit 0).
3. Perf: no hot path. The event pump is per frame and O(messages). `_empty` release exe 53 760 bytes.
4. Golden files updated: none.
5. Open issues / proposals:
   - Flaky test found and fixed: `platform.atomic_write` failed in about 1 of 15 runs with `ERROR_ACCESS_DENIED` from `MoveFileExW` (another process, most likely the virus scanner, briefly opens the new file without delete sharing). The rename is now retried with backoff (at most 63 ms); a destination that stays locked still returns `BMF_ERR_IO`.
   - Window dragging/resizing blocks the loop (Windows modal loop). 1A-23 should decide whether to keep rendering from inside it.
   - Holding both Shift keys and releasing one gives no key-up for it (Windows quirk), until focus loss or the other Shift is released.
   - Gamepad and touch paths were exercised only through the pure functions (dead zone, triggers) and code review: no controller or touch screen is attached to this machine.
   - The key enum and gamepad types live in `platform.h` until 1A-22/1A-24 move them into the input module and `bmf.h`.

## 2026-11-21 — 1A-13 GPU API (+ null backend)

1. What changed:
   - `src/gpu/gpu.h` (new): the internal GPU API (D-25, D-38): typed generational handles, buffer/texture/sampler/pipeline/bind set descriptors, frame/pass/draw, two-phase readback, stats, portable limits. Every function has a contract comment.
   - `src/gpu/gpu.c` (new): frontend with handle pools, validation (debug asserts with `"bmf_gpu_<fn>: <rule>"` messages), frame/pass state and stats. With `BMF_GPU_BACKEND_NULL` it is the null backend; D3D11/WebGPU return `BMF_ERR_UNSUPPORTED` until 1A-14/1B-42 and hook in at `// backend:` lines.
   - `src/gpu/README.md` (new).
   - `src/base/base_types.h`: `BMF_ERR_UNSUPPORTED`, `BMF_ERR_PENDING`. `src/base/README.md`: lists the `bmf_err` values.
   - `src/bmf.c`: includes `gpu/gpu.h` and `gpu/gpu.c`.
   - `tests/test_gpu.c` (6 tests), `tests/tests.c`.
   - `PLAN.md`: tracker, acceptance, deviation note.
2. Test results: `build test` 86/86 passed (6 new `gpu.*`, 4 of them expected crashes). Each crash test was also run alone to confirm it stops on the intended assert, not some other one (draw outside a pass, pipeline/pass color format mismatch, overlapping write in one period, vertex read past the end). `_empty` win64 debug and release build (release compiles `gpu.c` with asserts removed: no warnings) and run (exit 0).
3. Perf: no hot path in use yet. `bmf_gpu_draw` in release is a handful of compares and counter updates. `_empty` release exe unchanged at 53 760 bytes (LTO drops the unused GPU code).
4. Golden files updated: none.
5. Open issues / proposals:
   - Deviations from the 1A-13 plan text (details in `PLAN.md` 1A-13 note and `src/gpu/README.md`): no `gpu_null.c` (the frontend is the null backend); one `bmf_gpu_draw(desc)` instead of `set_*` calls, following D-25's "every draw states its pipeline and bindings"; no index/storage buffers yet; bind sets per shader id; two-phase async readback for WebGPU.
   - Handles are one-member structs (`typedef struct { u32 id; } bmf_gpu_buffer;`) rather than bare `u32`, so a texture cannot be passed where a buffer is expected. The representation is still a `u32` (D-25). Owner: say if D-25 should mean bare `u32` typedefs.
   - D3D11 constant-buffer binding offsets (`BMF_GPU_UNIFORM_OFFSET_ALIGNMENT`) need `ID3D11DeviceContext1::VSSetConstantBuffers1` (the D3D11.1 runtime, present on Windows 8+ and in WARP). 1A-14 must add `ID3D11DeviceContext1` to `win32_api.h`, or bind sets must drop offsets.
   - Shader ids are only range-checked until 1A-15 generates the shader table; per-shader slot checks for bind sets come with it.

## 2026-11-21 — 1A-14a D3D11 device and swapchain

1. What changed:
   - `src/gpu/d3d11/gpu_d3d11.c` (new): D3D11 device (hardware, WARP fallback, forced WARP), DXGI flip-model swapchain (FLIP_DISCARD, 2 buffers, frame-latency waitable, maximum latency 1, tearing check), resize, occlusion handling, surface passes (targets, viewport, clears), present, debug-layer message drain, device-lost panic with HRESULT names, a test-only live-object counter.
   - `src/gpu/gpu.c`: calls the D3D11 backend at init, shutdown, resize, begin/end frame and begin pass; tracks whether the surface is usable this frame and whether a pass rendered to it (present only then); surface passes in unusable frames are validated and dropped.
   - `src/bmf.c`: includes `gpu/d3d11/gpu_d3d11.c` before `gpu/gpu.c` on Windows.
   - `src/platform/win32/win32_api.h`: `LoadLibraryExW`, `IsIconic`, `WIN32_LOAD_LIBRARY_SEARCH_SYSTEM32`, `WIN32_SW_SHOWNOACTIVATE/MINIMIZE/RESTORE`.
   - `tests/test_gpu_d3d11.c` (5 tests, 1 expected crash), `tests/tests.c`.
   - `src/gpu/README.md`, `PLAN.md` (tracker, 1A-14 sub-steps, acceptance progress, note).
2. Test results: `build test` 91/91 passed (5 new `gpu.d3d11_*`). The device-lost test was run alone: the crash line reads `bmf_gpu: D3D11 device lost in test: DXGI_ERROR_DEVICE_REMOVED (0x887a0005), removed reason S_OK (0x00000000) ...`. The leak check was proven sensitive: one deliberately leaked `ID3D11Buffer` turns 4 live objects into 5 and fails the test. `_empty` win64 debug and release build and run (exit 0).
3. Perf: no hot path yet. WARP: 1000 swapchain resizes, each followed by a cleared, presented frame, take 379 ms (debug). `_empty` release exe unchanged at 53 760 bytes (LTO drops the unused backend).
4. Golden files updated: none.
5. Open issues / proposals:
   - Device loss panics instead of recovering. Recovery would mean re-creating every resource from saved descriptors (and texture contents); propose leaving it for Phase 2 unless the owner wants it earlier.
   - Debug-layer warnings are printed as `{"event":"warning","source":"d3d11",...}` stderr lines until `bmf_log` (1A-36) exists; 1A-36 should route them through it.
   - Found while testing: on this machine a flip-model `Present` never returns `DXGI_STATUS_OCCLUDED`, not even for a minimized window, and returns at once, so a minimized game would spin unthrottled. `begin_frame` now returns 0 while `IsIconic` (test: minimized 0/10 frames usable in 0 ms, restored 10/10 presents). The `DXGI_STATUS_OCCLUDED` path stays for systems that do report it; it is covered by code review only. 1A-23 should still sleep while suspended (begin_frame returning 0 does not wait).
   - The vtable-slot test (`tests/test_vtable_slots.c`) is moved to 1A-14c, when every interface the backend calls is in use. It needs MSVC/SDK header discovery for a host build (amendments A-01/A-12).
