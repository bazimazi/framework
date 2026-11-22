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

## 2026-11-21 — 1A-14b D3D11 resources

1. What changed:
   - `src/gpu/d3d11/gpu_d3d11.c`: buffers (dynamic: `Map` WRITE_DISCARD on the first write of a frame, NO_OVERWRITE after; default: `UpdateSubresource1`; zeros without data), textures with SRV/RTV/DSV by usage (IMMUTABLE when given data and not a render target, `_SRGB` format for sRGB), samplers, per-slot native object arrays released at shutdown; device requirements (D3D11.1 context, constant buffer offsetting, NO_OVERWRITE on dynamic constant buffers) with WARP fallback; test-only buffer readback and queued-use helpers.
   - `src/gpu/gpu.c`: calls the backend for buffer/texture/sampler create, destroy and write; a buffer starts "never written" so its first write is its period's first; new assert: UNIFORM buffers must be dynamic to be written.
   - `src/gpu/gpu.h`: `bmf_gpu_buffer_write` contract states the uniform rule.
   - `src/platform/win32/win32_api.h`: `ID3D11Device::CheckFeatureSupport` (slot 33), `ID3D11DeviceContext1` slots 112-123 (`UpdateSubresource1` 116, `VSSetConstantBuffers1` 119, `PSSetConstantBuffers1` 123), `WIN32_IID_ID3D11DeviceContext1`, `D3D11_FEATURE_DATA_D3D11_OPTIONS` (56 bytes), `WIN32_D3D11_FEATURE_D3D11_OPTIONS`, `WIN32_D3D11_COPY_*`. Slot numbers, GUID, constants and struct fields were read from SDK 10.0.26100 `d3d11.h`/`d3d11_1.h`.
   - `tests/test_gpu_d3d11.c` (2 new tests; the headless note now also names the hardware adapter), `tests/test_gpu.c` (expected crash `gpu.validation_static_uniform_write`), `tests/tests.c`.
   - `src/gpu/README.md`, `PLAN.md` (tracker, sub-step, note).
2. Test results: `build test` 94/94 passed (3 new). `gpu.validation_static_uniform_write` was run alone: it stops on the intended assert. The ring test was proven sensitive: with WRITE_DISCARD forced on every write it fails ("first append survives the second: first difference at byte 0"); without the queued GPU copy between the appends WARP reused the memory and a wrong DISCARD went unnoticed, so the test now queues one. The hardware request on this machine gets "Intel(R) UHD Graphics 770", feature level 11_1, and passes the requirement check. `_empty` win64 debug and release build and run (exit 0).
3. Perf: no hot path in use yet (draw2d, 1A-20, is the first user of buffer writes). `_empty` release exe unchanged at 53 760 bytes.
4. Golden files updated: none.
5. Open issues / proposals:
   - New API rule (internal `gpu.h`, not `bmf.h`): UNIFORM buffers must be `dynamic` to be written. Found by the D3D11 debug layer (error #288): a partial update of a default constant buffer needs `D3D11_COPY_NO_OVERWRITE` or `DISCARD`, and neither keeps WebGPU queue-write semantics. draw2d will use dynamic uniform buffers anyway.
   - A hardware D3D11 device must offer constant buffer offsetting and NO_OVERWRITE maps of dynamic constant buffers, or bmf runs on WARP. That is every WDDM 1.2+ driver in practice; a machine that falls back would be slow, not broken. Owner: say if a warning line should be printed when the fallback happens.
   - Texture contents are first checked with real pixels in 1A-14c (readback). This task checked creation, views and passes through the debug layer only.

## 2026-11-21 — 1A-14c D3D11 pipelines, draws, readback (1A-14 complete)

1. What changed:
   - `src/gpu/d3d11/gpu_d3d11.c`: shader bytecode table (filled by 1A-15 later, by tests now); pipelines (shaders, `ATTR<L>` input layout with instance step, blend presets, rasterizer, depth-stencil state, topology, strides), bind sets resolved into ready-to-bind arrays that hold references, draws with a per-pass bound-state cache, SRV unbinding at each pass start, staging-texture readback with row pitch and BGRA swap; shutdown releases pipelines, bind sets and a pending readback.
   - `src/gpu/gpu.c`: calls the backend for pipeline/bind set create and destroy, draw (with vertex buffer slots) and readback; `read_begin` keeps the texture info in release builds.
   - `src/gpu/gpu.h`: the pass `clear_color` is linear (sRGB targets encode it).
   - `tests/test_vtable_slots.c` (new): host check of 132 COM method slots, 45 struct sizes and 8 GUIDs of `win32_api.h` against SDK 10.0.26100, compiled as two translation units.
   - `build.c`: `build test` builds (when inputs change) and runs the host check before the unit tests; MSVC header discovery (`BMF_MSVC` or the newest Visual Studio `VC\Tools\MSVC\<ver>`), skipped with a message when absent. `build.exe` rebuilt with `bootstrap.bat`.
   - `tests/test_gpu_d3d11.c` (4 new tests, test-time HLSL compilation through `d3dcompiler_47.dll`, readback/compare helpers), `tests/tests.c`.
   - `src/gpu/README.md`, `tools/test/README.md`, `PLAN.md` (tracker: 1A-14 done; acceptance checked; note).
2. Test results: `build test` host check ok (132 slots, 45 structs, 8 GUIDs, 0 mismatches) and 98/98 unit tests passed. Plan acceptance for 1A-14: clear-color (RGBA8, BGRA8, sRGB) and one-triangle pixels match exactly on WARP; 1000-resize leak check stable (from 1A-14a). Sensitivity: a planted vtable slot shift makes `build test` fail with `{"host_check":"vtable_slots","mismatch":"ID3D11DeviceContextVtbl.PSSetConstantBuffers1","sdk":123,"bmf":124}` (a planted struct field is already stopped by the `BMF_STATIC_ASSERT` sizes); forcing the uniform binding offset to 0 fails the triangle test (1536 bad channels); shrinking the expected coverage by one diagonal fails it (32 bad channels). With the hardware device (Intel UHD 770) instead of WARP all pixel tests also pass exactly. No debug-layer warnings in any D3D11 test. `_empty` win64 debug and release build and run (exit 0).
3. Perf (new hot path `bmf_gpu_draw` with the D3D11 backend; debug build, CPU submit time of 10 000 draws, before = no backend): Intel UHD 770 without debug layer 360 ns/draw with identical state, 670 ns/draw with pipelines alternating every draw; WARP 3.5 / 5.5 µs (software driver cost), with the debug layer 4.7 / 7.1 µs. Release numbers come with draw2d's bench scene (1A-42). `build test` with an unchanged host check adds about 0.05 s; a rebuild of it about 0.9 s. `_empty` release exe unchanged at 53 760 bytes.
4. Golden files updated: none.
5. Open issues / proposals:
   - 1A-15 must generate `gpu_d3d11_shader_codes[]` (and shader slot metadata for bind set checks). Ids 60 and 61 are used by tests; the generated enum must stay below them or the tests move.
   - The vtable host check is skipped on machines without Visual Studio C++ headers (amendment A-01 allows host tools to use them). Owner: say if it should fail instead of skip.
   - D3D11.1 constant-buffer ranges are rounded up to 256 bytes; reading past the buffer end through such a range is what WARP allowed silently. If a driver objects, uniform bindings must be 256-byte multiples (a frontend rule change).
   - Draw submit on WARP is ~10x slower than on the hardware device; headless golden runs with many draws will be CPU-bound in WARP, not in bmf.

## 2026-11-21 — 1A-15 Build-time shader compilation (M2 complete)

1. What changed:
   - `build.c`: `build shaders`; a generator that compiles every top-level `shaders/d3d11/<name>.hlsl` (`vs_main`/`ps_main`, `vs_5_0`/`ps_5_0`, strictness + warnings-as-errors; debug `DEBUG|SKIP_OPTIMIZATION`, release `OPTIMIZATION_LEVEL3`) with `D3DCompile` from System32's `d3dcompiler_47.dll` into `out/gen/shaders_d3d11.h` (`bmf_shader_id` enum, both bytecode sets under `#if BMF_DEBUG`, `gpu_d3d11_shader_codes[]` with each shader's declared b/t/s registers read from the DXBC RDEF chunk); compiler messages reprinted as `<full path>:<line>:<col>: error: <code>: <text>` (include files resolved to full paths); build-time checks for file names, resource kinds, register ranges, shader count and debug/release register agreement; regeneration keyed on the hash of every file under `shaders/d3d11` plus the compiler DLL's size and time; `build win64` and `build test` run it first and report `"shaders":"built|up_to_date"`; `build test` also generates the test shader set. The generated header is an input of the binaries' hash. `build.exe` rebuilt with `bootstrap.bat`.
   - `src/gpu/d3d11/gpu_d3d11.c`: includes the generated table (the struct gained `uniform_mask`/`texture_mask`/`sampler_mask`); `gpu_d3d11_bindset_fits_shader` debug check.
   - `src/gpu/gpu.c`: with the D3D11 backend, `bmf_gpu_bindset_create` asserts the bind set fills exactly the registers the shader declares (the rule `gpu.h` already stated).
   - `src/gpu/gpu.h`: comments name `bmf_shader_id`.
   - `tests/shaders/d3d11/color.hlsl`, `texture.hlsl` (new; moved from strings in the test file), `tests/test_gpu_d3d11.c` (test-time `D3DCompile` removed; generated test shaders copied in at ids 56/57; new `gpu.d3d11_shader_tables` and expected crash `gpu.d3d11_validation_bindset_slots`), `tests/tests.c`.
   - `src/gpu/README.md`, `tools/test/README.md`, `PLAN.md` (tracker: 1A-15 and M2 done; acceptance; note).
2. Test results: `build test` host check ok (132 slots, 45 structs, 8 GUIDs) and 100/100 unit tests passed (2 new). The expected crash stops on `bmf_gpu_bindset_create: the shader has no bytecode, or the bind set does not fill exactly the b/t/s slots it declares` (gpu.c:420). Acceptance (temporary files in `shaders/d3d11`, removed afterwards): a syntax error prints `C:\...\shaders\d3d11\broken.hlsl:1:43: error: X3000: syntax error: unexpected token '}'` and exits 1; also checked: a warning (X3206, fatal), missing `ps_main` (X3501), an error inside an `#include`d `.hlsli` (reported at the `.hlsli` full path, also through `build win64`), a `StructuredBuffer`, a `register(b5)`, a bad file name. A good temporary runtime shader with b1/t2/s3 got masks 0x2/0x4/0x8, the D3D11 tests and a win64 release build passed with it. `_empty` win64 debug and release build and run (exit 0).
3. Perf: no runtime hot path touched (the new check is debug-only, in `bindset_create`). Build: the shader step costs a few ms when up to date (`build shaders` total 37 ms, most of it `clang --version`); compiling one shader for both configurations adds about 5-10 ms. `_empty` release exe unchanged at 53 760 bytes.
4. Golden files updated: none.
5. Open issues / proposals:
   - Debug bytecode is not reproducible (`D3DCOMPILE_DEBUG` embeds a PDB GUID and time), so each regeneration rebuilds debug binaries even when the HLSL is unchanged. Release bytecode is byte-identical across runs. Keep debug info for shader debuggers, or drop `D3DCOMPILE_DEBUG` for reproducible debug builds: owner's choice; no action taken.
   - The debug/release register-agreement check never fired in practice: fxc strips unreferenced resources even with `SKIP_OPTIMIZATION`. It stays as a guard.
   - The bind set register check runs only with the D3D11 backend (the null backend has no shader metadata). When WebGPU (1B-42) arrives, `build.c` should also check that `shaders/webgpu/*.wgsl` has the same names as `shaders/d3d11/*.hlsl`, so both generated enums agree.
   - 1A-20b adds the first runtime shader (`shaders/d3d11/` is empty, so `BMF_SHADER_COUNT` is 0 today).

## 2026-11-21 — 1A-20a draw2d: instances, sorting, batching

1. What changed:
   - `src/draw2d/draw2d.h` (new): the 48-byte `bmf_draw_instance` (static assert), kinds, sort key layout, `bmf_draw_f16`, engine interface `bmf_draw2d_init/shutdown/begin_frame/flush/get_stats`, submit calls `bmf_draw_rect`, `_rect_rounded`, `_circle`, `_ring`, `_line`, `_triangle`, `_sprite` with contract comments.
   - `src/draw2d/draw2d.c` (new): per-frame instance and key arrays in the frame arena, submit with budget drops, stable u64 radix sort, gather, one ring write per flush (appending across flushes), one draw per run of equal pipeline and texture, pipeline cache per color format, bind set cache per (texture slot, flush) with retirement on slot reuse, built-in white texture for shapes.
   - `shaders/d3d11/draw2d.hlsl` (new, `BMF_SHADER_DRAW2D`): per-kind quad placement, plain premultiplied color * texture fill (SDFs: 1A-20b).
   - `src/draw2d/README.md` (new), `src/bmf.c` (includes draw2d after gpu), `tests/test_draw2d.c` (new, 8 tests), `tests/tests.c`, `PLAN.md` (tracker, sub-step boxes, bench acceptance item, note).
2. Test results: `build test` 108/108 passed (8 new), host check ok. Sensitivity: with the sort call removed, `draw2d.batches_null` (6 batches instead of 4), `draw2d.d3d11_pixels` (green over red at (0,0), 512 bad channels) and `draw2d.bench_sprites_10k` (batch count) fail; with the color swizzle removed from the shader, `draw2d.d3d11_pixels` fails (640 bad channels). `_empty` win64 debug and release build and run.
3. Perf (new hot path; before = none): 10 000 sprites, submit + sort + gather + upload + batching, null backend, best of 200, win64 -O2 LTO: 74 µs with 8 layers x 4 textures (32 draws; submit alone 21 µs), 53 µs one layer one texture, 65 µs for 10 000 circles on 8 layers. Target < 500 µs (SPEC §12.3). Debug -O0: about 800 µs (test note). Measured with a throwaway harness in the scratchpad (the runtime exe does not call the game yet, 1A-23). `_empty` release exe unchanged at 53 760 bytes.
4. Golden files updated: none.
5. Open issues / proposals:
   - SPEC §12.2 sort key: depth and seq are left 0 because `bmf_sort_u64` is stable; a seq field would only cost passes and would wrap at 65 536 instances. Proposal: reword §12.2 to "stable sort on layer | pipeline | texture | depth" (owner decision; the bits stay reserved meanwhile).
   - Overlap order inside one layer follows the key: shapes before sprites, sprite textures by GPU slot. Games must use layers for overlap between textures. This is what the SPEC key implies; stated in `draw2d.h`.
   - `bmf_draw_text` is deferred to 1A-21 (needs the stroke font).
   - The SPEC instance params (corner radius, stroke width, edge softness) are stored as f16 to fit 48 bytes; the API exposes corner radius (rounded rect) and stroke (ring) only. Softness and rect outlines wait for a user.
   - Sprites take `bmf_gpu_texture` directly; 1A-24 decides the public texture handle. One linear/clamp sampler for all textures.

## 2026-11-21 — 1A-20b draw2d: SDF shader

1. What changed:
   - `shaders/d3d11/draw2d.hlsl`: vertex shader pads each quad by 1 px + softness and passes the shape parameters (nointerpolation); pixel shader evaluates the exact signed distance per kind (rounded box for RECT/SPRITE, circle, capsule for CAPSULE/GLYPH, triangle of either winding), applies the stroke band, coverage saturate(0.5 - d / (1 + softness)), color * texture (SPRITE) * coverage, premultiplied.
   - `tests/test_draw2d.c`: `draw2d.d3d11_sdf_sheet` (CPU twins of the distance functions; per-pixel comparison of a 128x128 sheet; draw count; writes `out/test/draw2d_sdf_sheet.rgba` for viewing), `tests/tests.c`.
   - `src/draw2d/README.md`, `PLAN.md` (tracker, sub-step, draw-count acceptance item, note).
2. Test results: `build test` 109/109 passed (1 new), host check ok. The sheet matches the CPU reference exactly on WARP (max channel difference 0, 914 partially covered pixels); with the hardware device (Intel UHD 770, temporary local switch) max difference 1. The 1A-20a exact-pixel test still passes (pixel-aligned box edges stay crisp). Sensitivity: shifting the coverage ramp by 0.1 px (0.6 - d) fails both `draw2d.d3d11_pixels` (640 bad channels) and `draw2d.d3d11_sdf_sheet` (3028 bad channels). The sheet image was viewed (4x scaled PNG): shapes, rotation sense (counter-clockwise positive), ring, round caps and both triangle windings are correct.
3. Perf: CPU side unchanged (10k sprites 74 µs release, see 1A-20a). GPU pixel cost is not measured yet (no GPU timestamps until Phase 2); the pixel shader is one distance function per pixel plus one texture sample for sprites.
4. Golden files updated: none (the golden infrastructure is 1A-41; the 1A-20 golden-image acceptance item stays open until then).
5. Open issues / proposals:
   - The plan text says "AA coverage with fwidth"; the shader uses no derivatives because every distance is already an exact pixel distance (gradient length 1). This is more accurate on diagonals (fwidth's L1 norm widens them by up to 1.41) and removes a source of WARP vs WebGPU differences. The WGSL twin (1B-42) must use the same formula.
   - Shapes thinner than 1 px are point-sampled (fainter than their area). If thin lines matter (physics debug draw, 1B-11), a minimum-width rule (draw at 1 px, scale alpha by width) is a small change.

## 2026-11-21 — 1A-20c draw2d: cameras (1A-20 complete)

1. What changed:
   - `src/draw2d/draw2d.h`: `bmf_draw2d_view` (physical size, DPI scale); `bmf_draw2d_begin_frame(frame, view)` (was `(frame)`); `bmf_camera_space`, `bmf_camera` (space, position, pixels_per_unit, rotation), `bmf_camera_world_default`, `bmf_camera_screen_default`, `bmf_camera_world_to_screen`, `bmf_camera_screen_to_world`, `bmf_draw_camera_push/pop`, `BMF_DRAW_CAMERA_STACK_DEPTH` (8), `BMF_DRAW_WORLD_PIXELS_PER_METER` (64); coordinate contract rewritten for camera space.
   - `src/draw2d/draw2d.c`: per-layer camera stacks and current transforms (reset to the default world camera every frame); every submit maps points, lengths and angles to target pixels.
   - `tests/test_draw2d.c`: 4 new tests (camera math, submit mapping with push/pop at DPI 2, expected crash on an empty pop, WARP camera pixels); the 1A-20a/b tests now push the identity screen camera on every layer. `tests/tests.c`.
   - `src/draw2d/README.md`, `PLAN.md` (tracker: 1A-20c and 1A-20 done; acceptance; the golden-image item moved to 1A-41; note).
2. Test results: `build test` 113/113 passed (4 new), host check ok. Sensitivity: with the world y flip removed, `draw2d.camera_math`, `draw2d.camera_submit` and `draw2d.d3d11_camera_pixels` fail (2095 bad channels). The empty-pop crash line names the rule (`bmf_draw_camera_pop: the layer's camera stack is empty`). On WARP: world y-up placement, a rotated world camera, DPI 2 (0.25 m = 32 physical px) are exact, and a world-space sprite shows its texture upright (four corner texels).
3. Perf (submit hot path, release -O2 LTO, null backend, best of 200, 10 000 instances, scratchpad harness): 8 layers x 4 textures 74 µs before, 87-94 µs after (submit 21 -> 34 µs: the per-layer transform); one layer one texture 53 -> 57-60 µs; circles 65 -> 72-77 µs. Target < 500 µs. Debug -O0 bench note 800 -> 1700 µs (the m3x2 helpers are not inlined at -O0).
4. Golden files updated: none.
5. Open issues / proposals:
   - Plan change (owner, please confirm): the 1A-20 acceptance item "WARP golden images of a shape test sheet" moved to 1A-41. 1A-41 depends on M4, which depends on M3 and so on 1A-20: keeping it in 1A-20 would block 1A-21/1A-22/1A-23 until after M4. Until then the per-pixel CPU-reference test and the exact-pixel tests cover draw2d.
   - Submits are placed for begin_frame's view. A second flush to a target of another size (render-to-texture) needs its own view; when a user needs that, `flush` could take the view and submits could be placed per flush (one transform per layer per flush).
   - D-19 says the default camera maps world space "centered"; implemented as world origin at the view center. UI code must push a SCREEN camera on its layer (every layer defaults to WORLD).

## 2026-11-21 — 1A-21 stroke font

1. What changed:
   - `src/draw2d/font_stroke.c` (new): glyph table for ASCII 32..126 plus a replacement box, authored as polyline strings on a 64-unit em grid (baseline 48, caps 44, x-height 32, descender -12, tabular digits); parsed once into a segment table (853 segments); `bmf_text_measure` and `bmf_draw_text` (UTF-8, `\n` lines, left/center/right per line, stroke width parameter with a 0.08 x size default, one GLYPH capsule instance per segment, upright in WORLD and SCREEN cameras).
   - `src/draw2d/draw2d.h`: `bmf_text_align`, `BMF_TEXT_DEFAULT_STROKE`, `BMF_TEXT_MAX_CONTAINED_STROKE`, the two text functions with contract comments; includes `base_string.h`.
   - `src/bmf.c`: includes `draw2d/font_stroke.c`.
   - `tests/test_draw2d_text.c` (new, 5 tests), `tests/tests.c`.
   - `src/draw2d/README.md`, `PLAN.md` (tracker, acceptance, note, the character-sheet golden moved to 1A-41).
2. Test results: `build test` 118/118 passed (5 new), host check ok. Sensitivity: removing the WORLD down-axis flip fails `draw2d.text_submit` (35 instances differ); centering by a quarter instead of half the width fails `draw2d.text_submit` and `draw2d.d3d11_text_sheet` (136 stray pixels outside the measured boxes); letting `\r` take space in measure only fails `draw2d.text_measure` and `draw2d.text_submit`. The WARP sheet matches the CPU reference within 1 level once the reference models WARP's blend (shader output rounded to 8 bits before blending, result rounded after); blending in float differs by up to 3 levels where four faint capsule ends overlap. The sheet image was viewed at sizes 12, 20 and 32: every glyph renders and reads correctly.
3. Perf: new hot path (text submit). Release -O2 LTO, null backend, best of 200, scratchpad harness: 1000 characters centered on 10 lines = 9 050 instances, submit + sort + upload + batch 55-57 µs (submit 22-23 µs). Existing scenarios unchanged (10k sprites 8 layers x 4 textures 84-101 µs, one layer 56-58 µs, circles 69-72 µs). Debug -O0 note: about 1.2 ms. `_empty` release exe still 53 760 bytes.
4. Golden files updated: none.
5. Open issues / proposals:
   - Plan change (owner, please confirm): like 1A-20, the "golden image of the character sheet" acceptance item moved to 1A-41 (golden infrastructure needs M4, which needs M3). `draw2d.d3d11_text_sheet` compares every pixel with a CPU reference meanwhile.
   - Glyph review used the test's WARP image, since `shot` arrives with 1A-32.
   - Text is ASCII only, without kerning, wrapping or vertical alignment; strokes under about 1 target pixel get faint (size 12 at the default stroke is the practical minimum).

## 2026-11-21 — 1A-22 input

1. What changed:
   - `src/input/input.h` (new): `bmf_action_type`, binding values (keys, `BMF_MOUSE_*`, `BMF_PAD_*`, `BMF_KEYS_WASD/ARROWS`, `BMF_PAD_LSTICK/RSTICK/DPAD`, `BMF_AXIS_*` directions), `bmf_action`, `bmf_pointer`, `bmf_input` (224 bytes, no implicit padding), bit helpers, engine interface (init with validation, view, events, gamepads, per-tick sampling), game interface (pointer camera, action lookup, rebinding), injection calls for the protocol; contract comments throughout.
   - `src/input/input.c` (new): validation with one-line messages, bindings, device state, counted presses with per-tick edges, axis normalization, pointer slots (mouse, touch, injected), text queue.
   - `src/input/README.md` (new).
   - `src/platform/platform.h`, `src/platform/win32/win32_window.c`, `tests/test_platform_window.c`: `bmf_mouse_button` values renamed `BMF_MOUSE_LEFT...` to `BMF_MOUSE_BUTTON_LEFT...` (the short names are now the bindings).
   - `src/bmf.c`: includes the input module after draw2d.
   - `tests/test_input.c` (new, 8 tests), `tests/tests.c`.
   - `PLAN.md` (tracker, acceptance, note).
2. Test results: `build test` 126/126 passed (8 new), host check ok. Sensitivity (each restored afterwards): showing no release between a release and a re-press while held fails `input.short_presses`; dropping the press latch fails `input.short_presses`, `input.pointers` and `input.injection`; clamping axes per component instead of by length fails `input.axis_normalization`; ignoring the DPI scale fails `input.pointers`; counting auto-repeat fails `input.edges_frames`.
3. Perf: no hot path touched (a handful of events per frame, up to 4 samples, each O(actions x bindings)); not measured. `_empty` release exe still 53 760 bytes.
4. Golden files updated: none.
5. Open issues / proposals:
   - SPEC §10 shows `{ "aim", BMF_ACTION_POINTER }`, which fails under the game build flags (`-Wextra -Werror`: missing field initializer). Tables write `{ "aim", BMF_ACTION_POINTER, { 0 } }`. Proposal: correct the SPEC example (wording only; no decision changes).
   - Not in Phase 1 input: per-player gamepad assignment, 1D trigger axes, mouse wheel and raw delta in `bmf_input`, a "press the key to bind" capture, binding names for settings files. Each is a small addition when a game needs it.
   - Hot reload with a changed action table must re-run `bmf_input_init` (1A-23), which resets rebinding.

## 2026-11-22 — 1A-23 engine core and game loop

1. What changed:
   - `src/core/core.h`, `src/core/core.c` (new): `bmf_game_desc`, `bmf_ctx`, `bmf_field`, `BMF_API_VERSION`, `bmf_sfx_play`; desc validation (`bmf_core_check_game`), command line (`bmf_core_parse_args`), start (memory, zeroed 64-byte aligned state, action table copy, init), integer fixed-step clock with the 4-tick cap and drop counting, per-tick input sampling, sfx event queue (audio stub), game swap for hot reload (kept/reset/rejected), display (window, D3D11 / WARP offscreen / null), frame loop with frame-latency pacing, the run with start/quit/error JSON lines, game DLL copy/load/watch.
   - `src/core/README.md` (new).
   - `src/platform/win32/win32_main.c`: now only flags, game resolution (DLL or static) and `bmf_core_run`; the 1A-12 event-printing test mode is removed.
   - `src/bmf.c`: every header first, then the implementation; the game DLL build compiles only headers plus `base_builtins.c`. Includes `core/core.h` and `core/core.c`.
   - `build.c`: debug exe built in two steps (object, COFF scan of external `bmf_*` functions into `runtime.def`, link with `/def` and `/implib:game.lib`); the game DLL links `game.lib`; the exe is built before the DLL.
   - `src/input/input.h`, `input.c`: `bmf_input_check_actions` (the init rules without touching input state); `bmf_input_init` calls it. `src/input/README.md` updated.
   - `src/platform/README.md`: entry point and modal-loop decision.
   - `games/_empty/game.c`: now a minimal valid game. `games/_loop/game.c` (new): moving, bouncing circle with interpolation, one action, field table; the hot-reload test game.
   - `tests/test_core.c` (new, 9 tests), `tests/tests.c`.
   - `PLAN.md`: tracker, acceptance, note.
2. Test results: `build test` 134/135 passed, host check ok. Failing: `gpu.d3d11_surface_present` ("a shown window presents its frames", 0/10). The Windows session was locked during the run (`LogonUI` running, 05:18–05:30): D3D11 then reports every present as occluded. Neither the test nor the GPU code changed in this task. Please re-run `build test gpu.d3d11_surface` in an unlocked session; it passed in 1A-22. New tests: `core.check_game`, `core.parse_args`, `core.start_init`, `core.fixed_step`, `core.tick_input_events`, `core.swap`, `core.frame_null`, `core.frame_d3d11`, `core.sfx_in_draw` (expected crash), all pass. Planted bugs, each caught and then restored: no spiral cap (fixed_step), float accumulator (fixed_step), swap always keeps (swap), state not zeroed (start_init), rebinding reset on every reload (swap), input reused across ticks (tick_input_events).
   Acceptance (`games/_loop`, debug): headless WARP and windowed D3D11 both run. Color edit + `build win64 debug games/_loop` (DLL only, about 150 ms) gives `{"event":"reload","state":"kept","tick":146,"ms":266}` 362 ms after the build returned (windowed; headless: 358 ms). Adding a field and bumping `state_layout_hash` gives `{"event":"reload","state":"reset","tick":0,...}` 322 ms after the build. Windowed pacing: tick 146 about 2.4 s after start at 60 Hz. Error paths checked by hand: unknown flag, missing value, `--tick-rate 500`, `--control`, `--replay`, missing game.dll, invalid game (tick_rate 7): one `{"event":"error",...}` line each, exit 1. Release `_loop` and `_empty` run headless (static link).
3. Perf: no existing hot path touched. New loop overhead not measured separately; headless debug `_loop` on WARP runs about 7 400 tick+frame iterations per second. Release `_empty` exe 53 760 to 156 160 bytes (the loop now reaches the GPU, draw2d, font and input code); debug `game.dll` for `_loop` 10 752 bytes (it no longer carries an engine copy).
4. Golden files updated: none.
5. Open issues / proposals:
   - The debug game DLL now imports the engine from the exe (exports from a COFF scan, `game.lib`). This implements D-13/SPEC §8.5 rather than changing them, but it is a build-model detail the owner may want written into SPEC §4 (proposal, wording only).
   - SPEC §11.1 `--scale N` is not defined. Implemented as real-time speed of the windowed loop (0.01..100). If it was meant as window or render scale, say so and 1A-32 changes it.
   - `bmf_ctx` is a struct with public read-only fields and `cx->view_size` must not be read in tick (documented, not enforced). 1A-24 decides whether `bmf.h` exposes it as is.
   - Hot reload keeps the memory budget and a 256 MiB state reserve of the run; a bigger state is rejected with a message (restart needed).
   - The window drag/resize modal loop stalls the game (decided: acceptable for Phase 1a). Rendering from `WM_SIZE`/a timer during the modal loop would be a later task if wanted.
   - The DLL path of hot reload has no automated test (needs real DLL builds); a scripted check could join the protocol tests in 1A-31.

## 2026-11-22 — 1A-24 `bmf.h` public header v0

1. What changed:
   - `src/bmf.h` (new, 1 417 lines): the whole public API, declarations moved out of the module headers: types, asserts, memory, math, SIMD, hash, RNG, sort, strings and formatting, `bmf_key`, drawing (cameras, shapes, text), input (actions, bindings, `bmf_input`, run-time action calls), the game model (`BMF_API_VERSION 0`, game limits, `bmf_field`, `bmf_ctx`, `bmf_game_desc`, `bmf_game`, `bmf_sfx_play`). A header block states the conventions (contracts, "allowed in tick", threads, errors). Group comments added where functions had none (vector, rect, aabb, color, SIMD load/store groups, scratch begin/end, hasher).
   - Removed `src/base/base_types.h`, `base_math.h`, `base_simd.h`, `base_hash.h`, `base_rng.h`, `base_sort.h` (entirely public; content now only in `bmf.h`).
   - `src/base/base_memory.h`, `base_string.h`, `src/platform/platform.h`, `src/gpu/gpu.h`, `src/draw2d/draw2d.h`, `src/input/input.h`, `src/core/core.h`: only the engine side remains; each includes `bmf.h`. `bmf_draw_sprite` stays internal.
   - `src/bmf.c`: `bmf.h` first; the game DLL compiles only `bmf.h` and `base_builtins.c`.
   - `src/base/*.c`, `src/platform/win32/win32_api.h`, `win32_core.c`, `tests/test_vtable_slots.c`, `build.c`: include `bmf.h`; comments updated.
   - `build.c`: new host check `api` in `build test`: every file-scope function in `bmf.h` needs a contract comment above it or its group; every `games/*/game.c` must compile (`-fsyntax-only`, debug flags) with only a copy of `bmf.h` on the include path. `host_check_line` takes the check name.
   - `games/_empty/game.c`, `games/_loop/game.c`: `#include "bmf.h"`.
   - READMEs: `src/base`, `src/draw2d`, `src/input`, `src/core`, `src/platform`, `tools/test`. Test file header comments.
   - `PLAN.md`: tracker (1A-24 and M3 done), acceptance, note.
2. Test results: `build test` 135/135 passed (`gpu.d3d11_surface_present` passes again in an unlocked session), host checks `api` ok (239 functions; games `_empty`, `_loop`) and `vtable_slots` ok. `build win64 debug|release games/_loop` and `games/_empty` build. Hot reload re-run on the new build: color edit `kept` 318 ms after the build returned, layout change `reset` 333 ms. Planted failures, each caught and restored: `_loop` calling `bmf_draw2d_get_stats` (debug DLL build fails with "use of undeclared identifier"; host check `api` reports `game_needs_more_than_bmf_h`), the comment above `bmf_rot_identity` removed (`no_contract_comment` at its line).
3. Perf: no hot path touched; declarations moved only. Release `_empty` exe unchanged at 156 160 bytes, debug `_loop` `game.dll` 10 752 bytes.
4. Golden files updated: none.
5. Open issues / proposals:
   - `bmf_draw_sprite` is not public: it takes a `bmf_gpu_texture` and games have no way to make one. It becomes public with the asset/texture handle of 1B-32/1B-33.
   - Public surface kept small on purpose (adding is not a breaking change, removing is): the JSON writer, UTF-16 conversion, key names, `bmf_arena_init` and the draw2d/input engine calls stay internal. Say so if games should get any of them now.
   - The game limits keep their engine names (`BMF_CORE_MAX_STATE_SIZE`, `BMF_CORE_MIN_TICK_RATE`, ...). SPEC §16 asks for `BMF_<NAME>` constants, which these satisfy, but shorter names (`BMF_MAX_STATE_SIZE`) would be a breaking rename later; decide before the API freeze.
   - The contract-comment check is a line heuristic (a comment above the function or its group); `build api` (1A-43) replaces it with the real parser. `bmf.h` is about 81 KB including inline bodies; API.md (signatures and contracts only) should stay under the 30k-token target.

## 2026-11-22 — 1A-30 reflection

1. What changed:
   - `src/debug/reflect.h`, `src/debug/reflect.c` (new): field types (`bmf_field_type_size` moved from core, `_align`, `_name`), `bmf_reflect_check_fields` (the field-table rules moved from `bmf_core_check_game`, plus the reserved name `tick`), `bmf_fields_hash`, and reflection over a state block: path resolution (`name`, `name[i]`, `name[*]`, built-in read-only `tick`), live counts, `get` (JSON values, shortest floats), `set` (one element, all values checked before writing, NaN/Inf refused, count fields bounded by the arrays they count, odd RNG inc), `dump` with a prefix, the NaN/Inf scan, "did you mean" suggestions (prefix, then edit distance <= 1 + len/3), closed status codes.
   - `src/debug/README.md` (new).
   - `src/bmf.h`: `BMF_T_RNG = 11` (a `bmf_rng` in the state), `BMF_T_COUNT = 12`; `bmf_field_type_size` comment.
   - `src/core/core.c`, `core.h`: field validation through `bmf_reflect_check_fields`; debug builds check `state_layout_hash == bmf_fields_hash`; new `bmf_core_reflect()`. `src/core/README.md` updated.
   - `src/bmf.c`: includes `debug/reflect.h` and `debug/reflect.c`.
   - `games/_loop/game.c`: declares its real layout hash `0x8f33ca16b24c85fc`; comment on the hot-reload layout flow.
   - `tests/test_reflect.c` (new, 9 tests), `tests/tests.c`; `tests/test_core.c`: fixtures declare the computed hash, the unknown-type case uses `BMF_T_COUNT` (12; 11 is now RNG), the reset case changes the field table instead of only the hash, new cases for the reserved name and a hash mismatch.
   - `PLAN.md`: tracker, acceptance, note.
2. Test results: `build test` 144/144 passed (9 new `reflect.*`), host checks `api` (239 functions, games `_empty`, `_loop`) and `vtable_slots` ok. Debug and release builds of `_loop` and `_empty` succeed; release `_empty` runs. Planted bugs, each caught and restored: live count not clamped (arrays), index bounded by capacity instead of live count (arrays, set), hash ignoring offsets (fields_hash), NaN scan over live elements only (nonfinite), set accepting NaN (set), count field not bounded (set), set writing before every value is checked (set), no prefix suggestion (suggest). Hot reload on `games/_loop`: color edit `kept` 334 ms after the build; field added to struct and table: `rejected` with `set it to 0xb05310747d854939` (292 ms); hash updated: `reset` 332 ms.
3. Perf: no hot path. Reflection runs at protocol rate. Release `_empty` exe unchanged at 156 160 bytes (unused reflection is dropped by LTO); release `_loop` 168 960 bytes.
4. Golden files updated: none. (New pinned constant in `reflect.fields_hash`: the layout hash of the test table, 0x32c35914c1f6e7cb.)
5. Open issues / proposals:
   - SPEC §8.4 / plan "built-in path `rng`": the engine owns no RNG in the state, so I added the field type `BMF_T_RNG` (a game reflects its `bmf_rng` as `[state, inc]`). Additive change to `bmf.h` (`BMF_T_COUNT` 11 to 12), API version kept at 0. If the owner meant an engine-owned RNG (e.g. one the engine keeps in a reserved part of the state and seeds from `--seed`), that is a design change to propose separately.
   - The declared `state_layout_hash` is now enforced in debug builds: every game must declare the exact `bmf_fields_hash` value (the error tells it). Games with no fields declare 0.
   - `set` writes the state between ticks; replays (1A-33) must record protocol writes or `set` breaks replay determinism. To be decided in 1A-31/1A-33.

## 2026-11-22 — 1A-31 control protocol

1. What changed:
   - `src/debug/protocol.h`, `src/debug/protocol.c` (new): stdin reader thread and SPSC line queue (64 x 4 KiB, futex waits), tokenizer, dispatch table (one row per command: name, argument range, handler, usage, owning task), commands `hello step run pause press axis pointer key get set dump watch unwatch reload quit`, `not_implemented` rows for `shot record replay snap hash profile mem log phys`, input holds released by an after-tick hook, watch events, closed error-code set, "did you mean" for commands, `BMF_AGENT`/`BMF_PROTOCOL_ENABLED`.
   - `src/core/core.c`, `core.h`: `--control` runs the protocol (paused at start; `run` ticks in real time, also headless; headless and paused, the frame blocks for the next request); after-tick hook; `bmf_core_request_quit`/`_quit_requested`; `bmf_core_dll_reload` (forced reload, the DLL poll now shares `core_dll_reload_now`); release builds without `BMF_AGENT` answer `--control` with `not_supported`. README updated.
   - `src/platform/platform.h`, `win32/win32_core.c`: `bmf_platform_stdin_read`. README updated.
   - `src/base/base_string.h/.c`: `bmf_str_edit_distance`; `src/debug/reflect.c` uses it instead of its own copy. README updated.
   - `src/bmf.c`: includes the protocol.
   - `tools/test/test_main.c`: `--game-child GAME FLAGS...`. README updated.
   - `tests/test_protocol.c` (new: `protocol.commands`, `protocol.large_response`, `protocol.process`), `tests/tests.c`.
   - `src/debug/README.md`, `PLAN.md`.
2. Test results: `build test` 147/147 passed, host checks `api` and `vtable_slots` ok. Planted bugs, each caught and restored: holds never end (commands, process), watch reports unchanged values (commands, process), starts running (commands), last line without newline lost at EOF (process), long lines truncated and executed (process), quit keeps reading (process), response before the step's events (commands, process). By hand on `games/_loop` debug: headless `hello`, `step 60`, `get`, `watch`, `press reverse` (reversals 1), `reload` (`kept`, event line before the response), `dump`, `quit` (exit 0, 1 frame); windowed `step 30`, `run`, 1 s, `pause` (tick 89: real time), `quit`. Release `_loop` with `--control`: `not_supported`, exit 1.
3. Perf: no hot path changed. Every tick now calls `bmf_protocol_after_tick` in debug and agent builds (returns at once when the protocol is inactive). Release builds without `BMF_AGENT` do not compile the call.
4. Golden files updated: none.
5. Open issues / proposals:
   - SPEC §11.2 does not define the responses of `run`, `pause`, `set`, `watch`, `unwatch`, `reload`, `press`, `axis`, `pointer`, `key`, `quit`; the chosen shapes are in the PLAN note and `protocol.h`. Proposal: copy them into SPEC §11.2.
   - Protocol writes into the state (`set`) and injected input are not recorded yet; `record`/`replay` (1A-33) must include them or a replay of a protocol session diverges.
   - The stdin reader thread is never joined (it blocks in `ReadFile` until the process exits); this is the one exception to "join every started thread".

## 2026-11-22 — 1A-32 headless mode and screenshots

1. What changed:
   - `src/base/base_png.h`, `base_png_write.c`, `base_png_read_stored.c` (new): stored-deflate PNG encoder (exact size known up front) and a decoder for stored-deflate PNGs (RGBA/RGB, row filters 0..4, every checksum verified), `bmf_png_info`.
   - `src/debug/shot.h`, `shot.c` (new): `bmf_shot_save` and `bmf_image_diff`.
   - `src/core/core.c`, `core.h`: `--size WxH`; `bmf_core_capture` (render the current state into an offscreen target and read it back, outside the engine frame); `bmf_core_memory`, `bmf_core_display_is_open`; the protocol poll now runs at the start of the frame, outside the GPU frame.
   - `src/debug/protocol.c`, `protocol.h`: `shot PATH.png [W H]`; new error code `io_failed`.
   - `src/bmf.c`: includes the new files.
   - `tests/test_png.c` (new: `png.roundtrip`, `png.reference`, `png.errors`), `tests/test_shot.c` (new: `shot.diff`, `shot.size_flag`, `shot.capture`, `shot.no_gpu`), `tests/test_protocol.c` (the test game draws a circle; shot error cases), `tests/tests.c`.
   - READMEs: `src/base`, `src/core`, `src/debug`. `PLAN.md`.
2. Test results: `build test` 154/154 passed, host checks ok. External decoders: `_loop` shots (640x360, 200x100, 1280x720) open in Windows GDI+ (System.Drawing: 32bppArgb, circle pixel 61,165,244) and decode with Python zlib with every chunk CRC valid. The 1280x720 shot shows the circle and the "bmf _loop" text. A shot from a windowed (hardware) run differs from the headless WARP one in 1 pixel by 1 level. Planted bugs, each caught and restored: Adler-32 missing the filter byte (png.roundtrip, shot.*), BFINAL on every block (png.roundtrip, shot.capture), Paeth predictor wrong (png.reference), average filter rounding (png.reference), Adler-32 not checked (png.errors), diff tolerance off by one (shot.diff), view not restored after a capture (crash in a later check), `--size` ignored (shot.capture), shot memory not returned (shot.capture).
3. Perf: no existing hot path changed. New: about 29 ms per 1280x720 shot (debug build, WARP: render, readback, encode, atomic write of 3.7 MB), measured over 20 shots.
4. Golden files updated: none. (New pinned constant in `png.roundtrip`: the 94-byte encoding of a 3x2 test image hashes to 0x4a6286d8f9e3dfdf.)
5. Open issues / proposals:
   - SPEC §11.1 does not list `--size WxH` yet (only PLAN 1A-32 does); proposal: add it to the run-mode flags.
   - Stored-deflate PNGs are as large as the raw pixels (3.7 MB per 1280x720 shot). Fine for goldens and agents; real compression comes with the inflate/deflate work of 1B-30 if wanted.
   - Shots borrow the permanent arena; the largest shot is limited by its free space (64 MiB default: about 2900 x 2900).

## 2026-11-22 — 1A-33 replay

1. What changed:
   - `src/debug/replay.h`, `replay.c` (new): `.bmfr` format (176-byte header, RLE start state, record stream: XOR-delta+RLE inputs, unchanged-input marker, state writes; checkpoints with state hash and per-field hashes), RLE codec, `bmf_replay_input_offset`, the always-on session recorder, `record start/stop`, `bmf_replay_write_session` (for crash dumps, 1A-35), the player (`--replay`, `--replay --verify`, protocol `replay`) with verification and the mismatch report (fields that differ, their replayed values and their values in the snapshot of the last matching checkpoint).
   - `src/core/core.c`, `core.h`: replay input / recording / after-tick hooks in every tick; session recording from `bmf_core_start`, restarted by a game swap; `bmf_core_state_hash`, `bmf_core_load_state`, `bmf_core_build_id` (with the game DLL's `bmf_game_build_id`); `--replay` modes; README updated.
   - `src/debug/protocol.c`, `protocol.h`: `record start PATH`, `record stop`, `replay PATH`; `set` is reported to the recorders; `bmf_protocol_write_event`; error codes `record_failed`, `replay_failed`.
   - `src/debug/reflect.c`, `reflect.h`: `bmf_reflect_target_bytes`.
   - `build.c`: `BMF_BUILD_ID` in every generated root (location-independent source hash); the debug game DLL exports `bmf_game_build_id()`; `starts_with` moved up.
   - `src/bmf.c`: includes the replay module.
   - `tests/test_replay.c` (new: `replay.rle`, `.roundtrip`, `.mutated`, `.session`, `.errors`), `tests/tests.c`, `tests/test_protocol.c` (record/replay no longer `not_implemented`).
   - `src/debug/README.md`, `PLAN.md`.
2. Test results: `build test` 159/159 passed, host checks ok. Acceptance as in the PLAN note. `games/_loop` debug: `record start`, `press`, `set`, 3600 ticks, `record stop` (6781 bytes, 61 checkpoints); `--replay --verify` exit 0; `--replay --headless` plays and quits at tick 3620. Planted bugs, each caught and restored: raw input instead of XOR delta, state records ignored, verification never failing, field diff listing every field, no snapshot at matching checkpoints (needed a second mutation case: the first one's value at the last match equalled the start value), no start checkpoint, RLE runs one byte short, state records applied one tick early (found as a real bug first, then fixed), protocol `set` not recorded. The roundtrip test found that real bug: state records were applied right after the previous tick instead of just before the next input.
3. Perf: every tick in debug and agent builds now records its input. `_loop` debug, no GPU, 3 M ticks: 819 ms without the recorder, 3680 ms with a first version (byte-wise XOR), 1517 ms after XOR in u64 words: 0.23 us/tick of recording. Release builds without `BMF_AGENT` are unchanged (not compiled).
4. Golden files updated: none.
5. Open issues / proposals:
   - SPEC §11.4 describes the `.bmfr` as header, inputs and `(tick, hash)` checkpoints. The implementation adds the start state, state-write records and per-field hashes per checkpoint (reasons in the PLAN note); proposal: update SPEC §11.4 to this format (wording only, no decision change).
   - PLAN asks for "a ring of the full session"; implemented as a full-session recording in 256 MiB of reserved space (about 50 days of idle 60 Hz input) that breaks instead of wrapping. A real ring would need periodic state keyframes; say if that is wanted.
   - The first diverging tick is reported at checkpoint precision (60 ticks). Exact ticks would need per-tick hashes (8 bytes per tick) or a bisecting re-run; not done.
   - `--replay --verify` prints only the verdict line (no start line).

## 2026-11-22 — 1A-34 snapshots and hashing

1. What changed:
   - `src/debug/snap.h`, `snap.c` (new): `bmf_snap_hash` (NaN/Inf scan, then `bmf_core_state_hash`), `bmf_snap_save`, `bmf_snap_load`, `bmf_snap_clear`; 16 named slots, each reserving address space for the largest state and committing the state size.
   - `src/debug/protocol.c`, `protocol.h`: `hash` and `snap save|load NAME` implemented; error codes `unknown_snap`, `too_many_snaps`, `nonfinite_state`.
   - `src/core/core.c`, `core.h`: the debug state hash pass asserts on NaN/Inf with the path (D-37 rule 11); `bmf_core_start` clears the snapshot slots; README updated.
   - `src/debug/replay.c`, `replay.h`: `bmf_replay_on_state_replaced(reason, broken)` replaces `bmf_replay_on_game_swap`; protocol `replay` now restarts the session recording instead of recording a state write (the tick jump made the session file unreplayable).
   - `src/bmf.c`: includes the snap module.
   - `tests/test_snap.c` (new: `snap.roundtrip`, `.slots`, `.nonfinite`, `.recordings`), `tests/tests.c`, `tests/test_protocol.c` (`hash`/`snap` error responses instead of `not_implemented`, command list, `--game-child snap`, `test_spawn_game`).
   - `src/debug/README.md`, `PLAN.md`.
2. Test results: `build test` 163/163 passed, host checks ok. Acceptance as in the PLAN note. Planted bugs, each caught and restored: load keeps the current tick, save copies nothing, `hash` without the NaN scan (the in-process hash then hits the debug assert and the runner dies), no layout check on load, one slot short, load without restarting recordings, slots not cleared by `bmf_core_start`, no debug NaN assert, session not restarted on state replacement, protocol `replay` recording a state write (the 1A-33 behavior).
3. Perf: the debug checkpoint hash (every 60 ticks) now scans the reflected floats. `_loop` debug, no GPU, 3 M ticks: 1368..1430 ms (3 runs) against 1517 ms measured for 1A-33: no measurable change. Release builds without `BMF_AGENT` unchanged.
4. Golden files updated: none.
5. Open issues / proposals:
   - The acceptance's "NaN written via `set`" conflicts with `set` refusing NaN (1A-30, D-37 rule 11); the test makes the game produce the NaN from values written by `set`. No change proposed.
   - In debug builds a NaN in the state now stops the game at the next replay checkpoint (assert, then a crash dump from 1A-35 on), as D-37 rule 11 says. An agent stepping a buggy game sees a crash line on stderr rather than a response.
   - An explicit recording cannot span a `snap load` or `replay` (it breaks, like across a hot reload). Supporting it would need a "tick set" record in the `.bmfr` stream; not done.

## 2026-11-22 — 1A-35 crash handling

1. What changed:
   - `src/platform/win32/win32_crash.c` (new): `bmf_platform_fatal` (moved from `win32_core.c`), vectored exception handler for faults in our own modules, first-crash-wins guard, crash line with code/address and the hook's keys, 64 KiB stack guarantee, exit with the exception code.
   - `src/platform/win32/win32_core.c`, `win32_api.h`, `platform.h`: `bmf_platform_set_crash_hook`, `bmf_platform_utc_ns`, `bmf_platform_dir_create`, `bmf_platform_file_write_atomic_parts` (the single-buffer write now calls it); library load/unload register module ranges; stack guarantee on platform threads; `GetSystemTimeAsFileTime`.
   - `src/debug/crash.h`, `crash.c` (new): the crash hook writing `<exe dir>/crash/<UTC>-<game>.bmfr` and `.state`, and the `tick`/`phase`/`replay`/`state`/`dump_error` keys.
   - `src/debug/replay.c`, `replay.h`: input record count per recorder, end tick from it; `bmf_replay_write_crash` (no final checkpoint); the session recorder keeps a copy of the state at its last checkpoint (`bmf_replay_session_last_state`); the writer writes from parts (the assembled-file arena is gone).
   - `src/core/core.c`, `core.h`: `bmf_core_running`, `bmf_core_phase_name`; `bmf_core_start` registers the crash hook.
   - `src/bmf.c`: includes `win32_crash.c` and the crash module.
   - `tests/test_crash.c` (new: `crash.null_write`, `.assert`, `.divide_by_zero`, `.panic`, `.stack_overflow`), `tests/tests.c`, `tests/test_protocol.c` (`--game-child crash`), `tests/test_snap.c` (deletes its dump), `tools/test/test_main.c` (expected-crash children: their dumps are deleted).
   - READMEs (platform, debug, core, tools/test), `PLAN.md`.
2. Test results: `build test` 168/168 passed, host checks ok, no files left in the crash folder. Acceptance as in the PLAN note. Planted bugs, each caught and restored: inputs counted only when they change, end tick from the core tick, a final checkpoint in the crash dump, no state copy at checkpoints, `.state` from the crashing state, crash hook never registered, own-module check inverted, no re-entry guard (second line after an assert), no stack guarantee (stack overflow unreported), a 48-byte number buffer (truncated address: a real bug found by reading the first crash line, then pinned by a test).
3. Perf: the session recorder now copies the state block at each checkpoint (every 60 ticks). `_loop` debug, no GPU, 3 M ticks: 1367..1499 ms (3 runs) against 1368..1430 ms before: no measurable change. Release builds without `BMF_AGENT` unchanged (no recorder).
4. Golden files updated: none.
5. Open issues / proposals:
   - SPEC §11.4 / PLAN say `out/crash/<time>.bmfr` and `SetUnhandledExceptionFilter`; the implementation uses `<exe dir>/crash/` and a vectored handler (reasons in the PLAN note). Proposal: update the SPEC wording; no D-xx change (the vectored handler is what keeps D-09's flags as they are).
   - A crash in the debug game DLL is covered by the module table (the DLL is loaded through `bmf_platform_library_load`) but not tested yet: the test games link statically into the test runner. `games/hello` (1A-40) can add a DLL crash test.
   - Plain release builds print the crash line without dump files.

## 2026-11-22 — 1A-36 profiler, memory report and logging

1. What changed:
   - `src/bmf.h`: section "Profiling and logging": `bmf_zone` (engine zones tick, draw, draw2d_flush, gpu_end_frame, debug_hooks; game zones 32..63), `bmf_zone_begin/end` (static inline, nothing without tools), `bmf_profile_zone_begin/end`, `bmf_log_level`, `bmf_log` (static inline varargs), `bmf_log_va`, `bmf_log_count`; `BMF_AGENT` and `BMF_TOOLS_ENABLED` moved here from protocol.h; `bmf_game_desc.zone_names`/`zone_name_count`.
   - `src/debug/profile.h`, `profile.c` (new): zone stack and nesting errors, per-zone call rings, frame ring, calibrated report, `mem` report.
   - `src/debug/log.c` (new): log ring, counts, drain, stderr echo.
   - `src/debug/protocol.c`, `protocol.h`: `profile N`, `mem`, `log [LEVEL]` implemented.
   - `src/debug/replay.c`, `replay.h`, `snap.c`, `snap.h`: `bmf_replay_arenas`, `bmf_snap_arenas` (named arenas for `mem`).
   - `src/core/core.c`: engine zones, frame times, profiler start and log clear in `bmf_core_start`, log echo in `--control` runs, `zone_names` validation.
   - `src/platform/platform.h`, `win32_core.c`: `bmf_platform_is_main_thread`.
   - `src/bmf.c`: includes the profile and log modules.
   - `tests/test_profile.c` (new: `profile.busy_loop`, `.report`, `log.ring`, `log.echo`), `tests/test_crash.c` (`crash.zone_order`, `crash.zone_left_open`), `tests/test_protocol.c` (error responses instead of `not_implemented`, command list, `--game-child profile`), `tests/tests.c`.
   - READMEs (debug, core, tools/test), `PLAN.md`.
2. Test results: `build test` 174/174 passed, host checks ok (api: 246 functions). Acceptance as in the PLAN note. The first version of the two-cost check flaked (1 in 12, then 4 in 20 with tighter bounds: a preempted tick lands in the upper percentiles); only p50 keeps a two-sided 5% bound now, the others lower bounds; 25 runs in a row passed. Planted bugs, each caught and restored: fixed 3 GHz instead of calibration, p50 = highest sample, p99 = median, mean not divided by calls, no nesting check, `profile N` without a new window (caught after adding ticks before the window), drain ignoring the level, full ring without dropping, no echo in `--control` runs, log not cleared at start, `mem` high water = used (caught after giving the permanent arena a high water above its use), no tick zone.
3. Perf: three zone pairs per tick in debug and agent builds. `_loop` debug, no GPU, 3 M ticks: 1568..1716 ms vs 1367..1499 ms before (about 0.06 us per tick, 10 ns per pair at -O0). `profile 1000` on `_loop`: tick p50 0.027 us, debug_hooks p50 0.20 us. Release builds without `BMF_AGENT`: zones compile to nothing.
4. Golden files updated: none.
5. Open issues / proposals:
   - SPEC §11.3 "per-tick ring buffer of 1024 ticks": implemented as per-zone rings of the last 1024 calls (stats are per call); proposal: SPEC wording to match. Report fields beyond §11.3 listed in the PLAN note.
   - `bodies` and `contacts` in the report wait for physics (M6).
   - `BMF_AGENT=1` is not settable through build.c yet (no agent config); the flag only exists as a define.

## 2026-11-22 — M4 closed; 1A-40 games/hello

1. What changed:
   - `PLAN.md`: M4 checked (all its tasks done); 1A-40 claimed and done.
   - `games/hello/game.c` (new): bouncing circles with semi-implicit Euler, gravity, wall bounces with restitution, resting on the floor, click/tap spawn (POINTER action), `jump` impulse (space, gamepad A), score of ceiling hits, stroke-font text, field table (with `circle_count` as the live count of three arrays), actions, constants per second; the header comment is the guide for new games.
2. Test results: `build test` 174/174 passed, host checks ok (the api check now also compiles `games/hello` with only `bmf.h`). Acceptance as in the PLAN note: windowed (free and `--control`), headless (free, `--control`, WARP shots looked at), release build, a 120 Hz recording verified.
3. Perf: no engine hot path touched.
4. Golden files updated: none (the golden infrastructure is 1A-41).
5. Open issues: none.

## 2026-11-22 — 1A-41 golden test infrastructure

1. What changed:
   - `tools/test/golden.c` (new): `test.exe --golden` (lint, replay verification, image goldens per game), the scenario recorder for `--update-golden`, `test_golden_image` for unit tests, the image comparison (SPEC §15 tolerances, actual and diff PNGs on failure), the simulation lint.
   - `tools/test/test_main.c`: `--golden` and `--update-golden NAME` arguments; includes golden.c.
   - `build.c`: `build test [filter] [--update-golden NAME]`; after the unit tests it builds every game with a `tests/` folder (win64 debug) and runs the golden phase.
   - `games/hello/game.c`: camera fitted to the view (and used for pointer input), lint marker, template note on goldens; `games/hello/tests/bounce.script` (new), `bounce.60hz.bmfr`, `bounce.120hz.bmfr`, `golden/d3d11/bounce.{60,120}hz.{start,jump,end}.{png,shot}` (new goldens).
   - `tests/test_draw2d.c`, `tests/test_draw2d_text.c`: engine goldens; `tests/golden/d3d11/draw2d.{shapes,cameras,font}.png` (new goldens).
   - `tests/test_golden.c` (new: `golden.lint`, `golden.compare`, `golden.update_names`), `tests/tests.c`.
   - `tools/test/README.md`, `PLAN.md` (1A-41, and the 1A-20/1A-21 golden items).
2. Test results: unit tests 176/177, goldens 9/9 (8 hello goldens and the hello lint), host checks ok. The one failure, `gpu.d3d11_surface_present` ("a shown window presents its frames": 0/10), is environmental: the workstation is locked (LogonUI is running), so a shown window cannot present; it passed in every run before today's lock and nothing in this change touches the swapchain. Acceptance as in the PLAN note (bounce constant 0.85 to 0.80 fails both replays and four images with readable lines, restored passes).
3. Perf: no engine hot path touched. The golden phase takes about 0.6 s (plus building hello when it changed).
4. Golden files updated (all new, created with `--update-golden`): `tests/golden/d3d11/draw2d.shapes.png`, `draw2d.cameras.png`, `draw2d.font.png` (the WARP sheets moved here from 1A-20/1A-21, equal to the CPU references the tests already check); `games/hello/tests/bounce.60hz.bmfr`, `bounce.120hz.bmfr` and `golden/d3d11/bounce.{60,120}hz.{start,jump,end}.png/.shot` (the first hello goldens).
5. Open issues / proposals:
   - Re-run `build test` once the workstation is unlocked to see `gpu.d3d11_surface_present` pass again.
   - Golden PNGs are uncompressed (stored deflate): hello's six images are 1.4 MB. The full inflate (1B-30) allows compressed goldens; until then keep golden shots small.
   - SPEC §15 point 3 names `games/<g>/tests/golden/<backend>/*.png`; the engine's own goldens use the same layout under `tests/golden/d3d11/` (proposal: mention it in §15).

## 2026-11-22 — 1A-42 perf budget runner and benchmark scenes

1. What changed:
   - `tools/test/perf.c` (new): `test.exe --perf [filter]`: perf.budget parser (`replay=`, `size=`, `zone=<name> <stat><=<us>`), runs each replay (repeated up to 3 times while over budget) on the scene's release agent build with `--headless --no-gpu --replay F --profile`, checks the limits, prints the result line with the zone breakdown, appends to `out/bench/history.jsonl` (UTC time, HEAD commit from .git).
   - `tools/test/test_main.c`: `--perf` mode; includes perf.c.
   - `build.c`: `build win64 release <dir> --agent` (BMF_AGENT=1, `win64-release-agent`); `build test` builds and golden-checks `tests/bench/*` like games and runs the perf phase (builds every folder with a perf.budget as release agent first).
   - `src/core/core.{h,c}`: `--profile` flag (with `--replay`): prints `{"event":"profile",...}` with the whole run's report when the replay ends.
   - `tools/test/golden.c`: bench scenes (`tests/bench/*/tests`) are golden-checked and linted like games.
   - `tests/bench/sprites10k/game.c`, `tests/bench.script`, `tests/bench.{60,120}hz.bmfr`, `perf.budget` (new): the 10 000-sprite scene.
   - `games/hello/perf.budget` (new), `games/hello/game.c` (template note 8).
   - `tests/test_perf.c` (new: `perf.budget_parse`, `perf.check`), `tests/test_core.c` (`--profile` parsing), `tests/tests.c`.
   - `tools/test/README.md`, `src/core/README.md`, `src/debug/README.md` (also a garbled sentence in its header fixed), `src/draw2d/README.md` (bench numbers), `PLAN.md`.
2. Test results: `build test` unit 179/179, goldens 12/12 (hello 8, sprites10k replays 2, lint 2), perf 3/3, host checks ok. Acceptance as in the PLAN note (a spin planted in hello's draw fails both hello budgets with the breakdown and `draw p99_us 107.456245 > 25 (perf.budget line 9)`; restored passes). Planted bugs in perf.c caught by the unit tests: 3 of 3.
3. Perf: no hot path changed (`--profile` is read once after the frame loop). Measured for the budgets (release agent, null backend): sprites10k draw p99 78..81 µs, draw2d_flush p99 66..81 µs (SPEC target: < 500 µs together); hello tick p99 0.05..0.07 µs, draw 1.7 µs, draw2d_flush 3.3..3.6 µs.
4. Golden files updated: `tests/bench/sprites10k/tests/bench.60hz.bmfr` and `bench.120hz.bmfr` (new, `--update-golden sprites10k.bench`): the benchmark replay of the new scene.
5. Open issues / proposals:
   - Perf budgets run on the null GPU backend (CPU work only); the D3D11 ring upload is not budgeted until a headless hardware path or a GPU-timer zone exists. Proposal: state in SPEC §15 point 5 that budgets measure CPU zones on the null backend.
   - SPEC §15 point 5 shows only `zone=... p99_us<=N`; the budget files add `replay=` and `size=` lines and p50/mean/max limits (proposal: document them in §15).
   - circles-2k-pile and bullets-1k come with physics (M6).
   - Timing noise: one full `build test` right after the release builds saw sprites10k draw p99 648 µs (p50 normal). The runner now repeats a run over budget (at most 3 runs; the planted slowdown still fails all 3). If this proves too weak against loaded machines, a dedicated `build bench` outside `build test` is the alternative (owner's call).

## 2026-11-22 — 1A-43 `build api` (API.md generator)

1. What changed:
   - `build.c`: `build api` (`command_api`, `api_build`, `api_generate`: line parser over bmf.h's layout, sections and contents, prose for section introductions, signatures of static inline functions, token estimate bytes / 3.5, 30 000-token target); `api_check_contracts` takes a line prefix (shared by `build api` and the host check); the `api` host check also fails on a stale or missing API.md and on the token target.
   - `API.md` (new, generated): 246 functions, about 19 000 tokens.
   - `tools/test/README.md`, `PLAN.md` (1A-43, the 1A-24 acceptance note).
2. Test results: `build test` unit 179/179, goldens 12/12, perf 3/3, host checks ok (`api` with `api_md_tokens` 19004). Acceptance as in the PLAN note (missing comment, stale API.md, over-target estimate each fail; API.md not written on failure).
3. Perf: no runtime code changed. `build api` takes about 30 ms.
4. Golden files updated: none.
5. Open issues / proposals:
   - The token estimate is a byte ratio, not a real tokenizer count; the real count may differ by some 10 to 20 %, still well under 30 000.
   - SPEC §17 says API.md holds "signatures plus contract comments, nothing else"; it also holds the types, enums, constants and macros a game needs (proposal: say so in §17).

## 2026-11-22 — 1A-44 module READMEs and docs

1. What changed:
   - `src/asset/README.md`, `src/audio/README.md`, `src/job/README.md`, `src/physics2d/README.md` (new placeholders: status, owning tasks, invariants to keep, planned layout, targets).
   - `src/base`, `src/platform`, `src/input`, `src/gpu`, `src/draw2d`, `src/core`, `src/debug` READMEs: review fixes (sizes, outdated and wrong statements, missing names and tests), base gained a Data layout section, every test inventory moved to a Tests section, all under 60 lines.
   - `tools/test/README.md`: review fixes (lint scope, 120 Hz games, vtable compile failure, api host check errors).
   - `docs/setup.md`: optional MSVC headers, everyday commands, test environment notes; a broken `.\bootstrap.bat` fixed.
   - Code comments and one message: `src/debug/protocol.c` (`phys` arrives with 1B-09) with `tests/test_protocol.c`, `src/input/input.h`, `src/draw2d/draw2d.h`, `tools/test/perf.c`, `build.c` (usage header).
   - `PLAN.md`.
2. Test results: `build test` unit 179/179, goldens 12/12, perf 3/3, host checks ok. Review: three separate agents (base/platform/input, gpu/draw2d/core, debug/tools/test/placeholders) checked the READMEs against the code; every finding was fixed or is listed below for the owner.
3. Perf: no code path changed.
4. Golden files updated: none.
5. Open issues / proposals (owner):
   - SPEC §11.4 names `out/crash/<time>.bmfr`; the code writes `<exe dir>/crash/<UTC>-<game>.bmfr` (+ `.state`). Proposal: update §11.4.
   - SPEC §14.1 `BMF_PHYS_DECL(max_bodies, max_shapes, max_joints, max_contacts)` vs PLAN 1B-03 `BMF_PHYS_DECL(name, ...)`: decide before 1B-03.
   - `base_png_read_stored.c` is compiled into every runtime, release included; SPEC §14.3 keeps decoders out of the runtime. Proposal: compile it only into tool builds (`BMF_TOOLS_ENABLED`) and the test runner.

## 2026-11-22 — 1A-45 Phase 1a exit validation (Phase 1a complete)

1. What changed:
   - Exit run: a fresh agent restricted to AGENTS.md, API.md and a copy of games/hello added lives, game over and restart and verified them through the protocol; its game is kept in out/exit-validation/hello_lives (not in the repo).
   - `docs/friction.md`: the 15 friction points with what was done, and the open items.
   - `AGENTS.md`: "Quick reference" for game runs, the protocol, test files, filters and the lint.
   - `src/debug/protocol.c`: `help` command, a leading UTF-8 BOM is skipped; `tests/test_protocol.c` covers both.
   - `build.c`: `build test` says when a filter skips the golden or perf phase; `build api` estimate calibrated (bytes / 2.07) and API.md trimmed (preprocessor blocks, static asserts, "Engine side" declarations, alignment spaces and spec/task references left out). `tools/test/test_main.c`: warning when a filter matches no unit test.
   - `src/bmf.h`: comments only (build macros summary, `BMF_ASSERT(cond)`, `state_layout_hash`, engine-side declarations marked, `bmf_log` contract carries the format/ring details, shorter hash/math/PCG/fast-tier/fatal comments); `API_VERSION` unchanged (no API change). `API.md` regenerated: 61 064 bytes, about 29 520 tokens. `src/base/README.md`: the hash design.
   - `games/hello/game.c`: `HELLO_BUTTON_JUMP` (button bits vs action indices) and the lint note (no state change; goldens unchanged).
   - `PLAN.md`: 1A-45 and Phase 1a checked.
2. Test results: `build test` with the agent's game present: unit 179/179, goldens 31/31, perf 7/7, host checks ok; final run after moving it out: unit 179/179, goldens 12/12, perf 3/3, host checks ok (`api_md_tokens` 29520).
3. Perf: no hot path changed (the protocol BOM check runs once per request).
4. Golden files updated: none in the repo (the agent's re-recorded goldens live with its game outside the repo).
5. Open issues / proposals (owner): API.md token target vs reality (29 520 of 30 000, still two 25k-token reads: raise the target, split API.md, or trim bmf.h comments further); `expect PATH VALUE` in golden scripts; a `--help` flag; plus the earlier proposals (SPEC §11.4 crash path, `BMF_PHYS_DECL` signature, the PNG reader in release runtimes, SPEC §15 perf budget wording, SPEC §17 API.md contents).
