# bmf — Implementation Plan

Status: v1 (2026-11-18). Based on `SPEC.md` v0.2.
Audience: AI agents implementing bmf, and the owner who approves decisions and owner-only actions.

This plan is executable. Each task has an ID, size, dependencies, the files it touches, steps, acceptance criteria and verification commands. An agent works on one task per session unless the task is size S and the next task is a direct continuation.

Spec decisions are cited as `D-xx`, agent rules as `R-x` (`AGENTS.md`), plan tasks as `P0-xx` / `1A-xx` / `1B-xx`, and risks as `K-xx`.

---

## 0. Progress tracker (single source of truth for status)

How to use it:
- **Resume:** the next task is the first unchecked task below whose dependencies (in its section) are all checked and that carries no `claimed` note.
- **Claim:** append ` — claimed YYYY-MM-DD` to the task line before starting.
- **Done:** change `[ ]` to `[x]` and replace the claim note with ` — done YYYY-MM-DD`. A task is done only when its Definition of Done (§8) holds.
- **Group boxes** (phases, milestones, tasks with sub-tasks) are checked only when every child is checked.
- **Inside a task section**, steps and acceptance criteria are checkboxes too. Check them as you go, so a task split across sessions resumes at the first unchecked step.
- **Blocked:** append ` — blocked: <reason>` and move on to another available task.

### Owner actions
- [x] O-1 Install LLVM (latest stable) — done 2026-11-18 (llvm 23.1.6 via winget, owner-approved)
- [x] O-2 Decide on `git init` for `_framework` — done 2026-11-18 (git init approved; no commits)
- [x] O-3 Review spike reports and amendments (after Phase 0) — done 2026-11-19 (all A-01…A-13 approved)
- [ ] O-4 Choose the Phase 1b port target (after 1B-50)
- [ ] O-5 Code signing decision (before first public release)

### Spec amendments (§9), checked when the owner approves and the change is applied to `SPEC.md`
- [x] A-01 Host tools may use the CRT and SDK headers — applied 2026-11-19
- [x] A-02 Null GPU backend and `--no-gpu` — applied 2026-11-19
- [x] A-03 Sanitizer strategy from P0-05 — applied 2026-11-19
- [x] A-04 Per-game memory budget in `bmf_game_desc` — applied 2026-11-19
- [x] A-05 `src/core/` in the layout — applied 2026-11-19
- [x] A-06 Physics sleeping and islands — applied 2026-11-19
- [x] A-07 `avrt.dll` in the system DLL list — applied 2026-11-19
- [x] A-08 Per-backend golden images — applied 2026-11-19
- [x] A-09 Ban `-mrelaxed-simd` and fast-math sub-flags — applied 2026-11-19
- [x] A-10 Web tests report page-to-server (no DevTools client) — applied 2026-11-19
- [x] A-11 `-fno-math-errno` in D-09 (from P0-06) — applied 2026-11-19
- [x] A-12 `build.c` CRT-free, bootstrapped by `bootstrap.bat` (from P0-05) — applied 2026-11-19
- [x] A-13 `ws2_32.dll` in the system DLL list (from P0-07) — applied 2026-11-19

### Phase 0 — Bootstrap and risk spikes
- [x] **Phase 0 complete** (all spike reports exist, P0-06 passed, O-3 done)
  - [x] P0-01 Toolchain pinning — done 2026-11-18
  - [x] P0-02 Repository skeleton — done 2026-11-18
  - [x] P0-03 Spike: CRT-free win32 exe — done 2026-11-18
  - [x] P0-04 Spike: freestanding wasm32 — done 2026-11-18
  - [x] P0-05 Spike: sanitizers without the CRT — done 2026-11-18
  - [x] P0-06 Spike: cross-platform float determinism — done 2026-11-18
  - [x] P0-07 Spike: headless browser test harness — done 2026-11-18
  - [x] P0-08 Spike: D3D11 at build time and headless — done 2026-11-18

### Phase 1a — Windows skeleton plus agent loop
- [ ] **Phase 1a complete** (exit criteria in 1A-45)
  - [x] **M1** Build program, base layer, tests
    - [x] 1A-01 `build.c` v1 — done 2026-11-19
    - [x] 1A-02 base: types, asserts, builtins — done 2026-11-19
    - [x] 1A-03 base: memory — done 2026-11-19
    - [x] 1A-04 base: deterministic math — done 2026-11-19
      - [x] 1A-04a scalar functions — done 2026-11-19
      - [x] 1A-04b vector and matrix types — done 2026-11-19
      - [x] 1A-04c fast tier — done 2026-11-19
    - [x] 1A-05 base: SIMD — done 2026-11-19
    - [x] 1A-06 base: strings, format, parse, JSON writer — done 2026-11-19
    - [x] 1A-07 base: hash, RNG, sort — done 2026-11-19
    - [x] 1A-08 Test runner — done 2026-11-19
  - [ ] **M2** Win32 platform and GPU
    - [x] 1A-10 Win32 API declarations — done 2026-11-19
    - [x] 1A-11 Win32 platform core — done 2026-11-20
    - [x] 1A-12 Win32 window and events — done 2026-11-20
    - [x] 1A-13 GPU API (+ null backend) — done 2026-11-21
    - [ ] 1A-14 D3D11 backend
      - [x] 1A-14a device and swapchain — done 2026-11-21
      - [ ] 1A-14b resources
      - [ ] 1A-14c draw, passes, readback
    - [ ] 1A-15 Build-time shader compilation
  - [ ] **M3** Engine core, input, draw2d
    - [ ] 1A-20 draw2d
      - [ ] 1A-20a instances, sorting, batching
      - [ ] 1A-20b SDF shader
      - [ ] 1A-20c cameras
    - [ ] 1A-21 Stroke font
    - [ ] 1A-22 Input
    - [ ] 1A-23 Engine core and game loop
    - [ ] 1A-24 `bmf.h` public header v0
  - [ ] **M4** Debug and agent interface
    - [ ] 1A-30 Reflection
    - [ ] 1A-31 Control protocol
    - [ ] 1A-32 Headless mode and screenshots
    - [ ] 1A-33 Replay
    - [ ] 1A-34 Snapshots and hashing
    - [ ] 1A-35 Crash handling
    - [ ] 1A-36 Profiler, memory report, logging
  - [ ] **M5** Hello game, golden infrastructure, API.md
    - [ ] 1A-40 `games/hello`
    - [ ] 1A-41 Golden test infrastructure
    - [ ] 1A-42 Perf budget runner and benchmark scenes
    - [ ] 1A-43 `build api` (API.md generator)
    - [ ] 1A-44 Module READMEs and docs
    - [ ] 1A-45 Phase 1a exit validation

### Phase 1b — Full 2D stack plus web
- [ ] **Phase 1b complete** (exit criteria in 1B-51)
  - [ ] **M6** physics2d and jobs
    - [ ] 1B-01 Geometry and mass
    - [ ] 1B-02 Narrowphase manifolds
    - [ ] 1B-03 World storage and API
    - [ ] 1B-04 Broadphase
    - [ ] 1B-05 Contact persistence and events
    - [ ] 1B-06 Solver
    - [ ] 1B-07 Joints
    - [ ] 1B-08 Continuous collision
    - [ ] 1B-09 Queries
    - [ ] 1B-10 Sleeping and islands
    - [ ] 1B-11 Physics debug draw and validation
    - [ ] 1B-12 Job system
  - [ ] **M7** Audio
    - [ ] 1B-20 Mixer
    - [ ] 1B-21 Procedural synth
    - [ ] 1B-22 WASAPI backend
  - [ ] **M8** Assets
    - [ ] 1B-30 Inflate, PNG and WAV decoding
    - [ ] 1B-31 Baker, pak writer, `assets.h`
    - [ ] 1B-32 Runtime pak loader
    - [ ] 1B-33 Procedural textures
  - [ ] **M9** Web
    - [ ] 1B-40 wasm build target
    - [ ] 1B-41 Web platform (wasm side + `bmf_web.js`)
    - [ ] 1B-42 WebGPU backend
    - [ ] 1B-43 Web audio
    - [ ] 1B-44 Web input polish
    - [ ] 1B-45 Dev server
    - [ ] 1B-46 Cross-platform determinism gate
  - [ ] **M10** Port of an existing game
    - [ ] 1B-50 Port target selection
    - [ ] 1B-51 Port implementation
    - [ ] 1B-52 Phase 1 retrospective

### Phase 2 — Harden through real games (detailed plan written at phase start)
- [ ] **Phase 2 complete** (API v1 frozen)
  - [ ] Detailed Phase 2 plan written into this file
  - [ ] Three new agent-built games
  - [ ] TTF reader and MSDF text
  - [ ] Streaming music codec
  - [ ] GPU timestamp queries
  - [ ] AVX2 dispatch for hot kernels
  - [ ] API review and `BMF_API_VERSION 1` freeze

### Phase 3 — Platforms (detailed plan written at phase start)
- [ ] **Phase 3 complete**
  - [ ] Detailed Phase 3 plan written into this file
  - [ ] Android
  - [ ] Linux
  - [ ] macOS
  - [ ] iOS
  - [ ] Shared shader language

### Phase 4 — 3D
- [ ] Phase 4 planned
- [ ] Phase 4 complete

### Phase 5 — Consoles
- [ ] Phase 5 planned
- [ ] Phase 5 complete

---

## 1. Current environment (checked 2026-11-18)

| Item | State | Consequence |
|---|---|---|
| clang / lld / wasm-ld | **Not installed.** VS 18 Community ships only `clang-format` and `clang-tidy`. | Owner action O-1 (§2). Blocks everything. |
| Windows SDK | 10.0.26100.0 present | Import libraries for linking are available (D-02). |
| `d3dcompiler_47.dll`, `d3d10warp.dll` (WARP) | Present in System32 | Build-time shader compiler (D-14) and headless rendering (§9.1 of spec) are available. |
| Edge, Chrome | Both installed | Headless web test path (D-04) is available. Edge is the default because it ships with Windows. |
| winget | v1.29 | Can install LLVM. |
| git | 2.50 installed. `_framework` is **not** a git repository. | Owner action O-2. |
| Python, Node | Installed on machine | MUST NOT be used by bmf (D-05). |

---

## 2. Owner actions (agents cannot do these alone)

- **O-1** Install LLVM, the latest stable release: `winget install LLVM.LLVM`. Then record the exact version in `toolchain.txt` (P0-01). Agents MUST NOT install software without approval.
- **O-2** Decide whether `_framework` becomes its own git repository, and approve `git init`. Agents never create commits or branches unless asked.
- **O-3** Review the spike reports at the end of Phase 0 (§4) and approve the proposed spec amendments (§9).
- **O-4** Choose the Phase 1b port target after 1B-50 measures the candidates.
- **O-5** Decide on code signing before the first public release (K-08).

---

## 3. Overview: milestones, sizes and critical path

Size: **S** < 500 LOC (about 1 session), **M** 500–1500 LOC (1–3 sessions), **L** > 1500 LOC (split into sub-tasks when started).

| Milestone | Content | Est. LOC | Gate |
|---|---|---|---|
| **M0** Phase 0 | toolchain, skeleton, 7 risk spikes | ~1.5k (throwaway) | O-3 approves spike results |
| **M1** | `build.c`, base layer, test runner | ~6k | `build test` green |
| **M2** | win32 platform, GPU API, D3D11 backend | ~6k | window + clear color + WARP readback |
| **M3** | engine core, input, draw2d, stroke font, `bmf.h` | ~5k | game DLL draws shapes and text, with hot reload |
| **M4** | debug and agent interface | ~5k | full §11 protocol works headless |
| **M5** | `games/hello`, golden infrastructure, perf, `API.md` | ~3k | **Phase 1a exit** |
| **M6** | physics2d + jobs | ~10k | physics scenario suite + benchmarks green |
| **M7** | audio | ~3k | offline-render hashes stable |
| **M8** | assets: inflate, PNG, WAV, baker, pak | ~4k | `build bake` + runtime pak load |
| **M9** | web: wasm, JS shim, WebGPU, dev server | ~7k | hello runs in browser; win64 and wasm hashes match |
| **M10** | port of an existing game | ~3–6k (game) | **Phase 1b exit** |

Critical path: O-1, P0, then 1A-01…1A-08 (M1), 1A-10…1A-15 (M2), 1A-20…1A-24 (M3), 1A-30…1A-36 (M4), 1A-40…1A-45 (M5), M9, M10.

### Parallel lanes (after M1 is done)

Several agents can work at once without file conflicts:
- **Lane A** (critical): M2, M3, M4, M5.
- **Lane B**: M6 physics2d. It is pure computation, tested through unit and scenario tests with no window. It needs only M1. Its visualization (debug draw) waits for M3.
- **Lane C**: M8 inflate, PNG and WAV decoders, atlas packer and pak writer. Tools only, needs only M1.
- **Lane D**: M7 mixer and synth (offline rendering to a buffer). Needs only M1. The WASAPI backend waits for M2.
- **Lane E**: 1B-45 dev server (HTTP, WebSocket, SHA-1). Needs only M1.

Lane rule: each lane owns its own directories. A change to `src/bmf.h` or `src/base/` from a non-owner lane MUST be a separate small task, coordinated in `docs/progress.md`.

---

## 4. Phase 0 — Bootstrap and risk spikes

The spikes retire the biggest technical risks before any architecture code exists. Spike code lives in `spikes/<name>/` and is thrown away. Each spike's lasting output is a report `docs/spikes/<name>.md` (findings, exact flags, numbers, and a recommendation).

### P0-01 Toolchain pinning — S
- Deps: O-1.
- Files: `toolchain.txt`, `docs/setup.md`.
- Steps:
  - [x] 1. Locate clang (`where clang`, then `C:\Program Files\LLVM\bin`).
  - [x] 2. Record `clang --version` in `toolchain.txt` (one line: `llvm <version>`).
  - [x] 3. Confirm `lld-link.exe` and `wasm-ld.exe` exist, and that `clang --print-targets` lists `wasm32` and `x86-64`.
  - [x] 4. Write `docs/setup.md`: install steps, the SDK path, and how to verify.
- [x] Acceptance: all four checks pass. `docs/setup.md` lets a new machine reach the same state.

### P0-02 Repository skeleton — S
- Deps: O-2 (git part only; the folders do not need git).
- Files: the directory tree from SPEC §3 (empty dirs with `.keep`), `.gitignore` (`out/`, `spikes/*/out/`), `.gitattributes` (`* text=auto eol=lf`; `*.png *.bmfr *.pak *.state *.wav binary`), `docs/progress.md`, `docs/decisions.md` (seeded with D-01…D-37, one line each), `docs/friction.md`.
- [x] Acceptance: the tree matches the spec. `docs/decisions.md` lists every D-xx.

### P0-03 Spike: CRT-free win32 exe — S
- Goal: confirm the D-03 toolchain path.
- Steps:
  - [x] 1. Write `main.c` with entry `bmf_win32_entry`, which calls `MessageBoxA`/`CreateWindowExW` and `ExitProcess`, declared by hand (no `windows.h`).
  - [x] 2. Compile and link with `clang --target=x86_64-pc-windows-msvc -nostdlib -ffreestanding -fuse-ld=lld -Wl,/entry:bmf_win32_entry -Wl,/subsystem:windows` plus `kernel32.lib user32.lib`.
  - [x] 3. Find every unresolved symbol clang emits (`memset`, `memcpy`, `_fltused`, `__chkstk`). Decide for each: provide our own, or link `clang_rt.builtins-x86_64.lib` (compiler-rt is allowed by D-02).
  - [x] 4. Check that a large stack array triggers `__chkstk`, and document the chosen handling: own probe, or `-mstack-probe-size` plus `/STACK:reserve,commit`.
  - [x] 5. Build a DLL variant with entry `bmf_dll_entry`, load it from the exe, and call one exported function (proves hot reload works without the CRT, D-13).
  - [x] 6. Measure the exe size, and check whether Windows Defender flags it (K-08). — size done; Defender not testable on this machine (disabled), open item in report.
- [x] Report: exact flags, the list of symbols we must provide, exe size, Defender result.

### P0-04 Spike: freestanding wasm32 — S
- Steps:
  - [x] 1. `clang --target=wasm32 -nostdlib -ffreestanding -msimd128 -mbulk-memory -mnontrapping-fptoint -Wl,--no-entry -Wl,--export-dynamic -Wl,--import-memory -O2`.
  - [x] 2. Find the compiler-rt builtins clang emits (`__multi3`, `__ashlti3`, f128 helpers, `memcpy`/`memset` lowering with bulk memory). Decide: own `src/base/builtins_wasm.c`, or forbid the source pattern (e.g. no `__int128`).
  - [x] 3. Write a minimal `index.html` + JS that instantiates the module, calls an export and prints the result. Run it in Edge headless: `msedge --headless=new --disable-gpu-sandbox <url>`, with the page reporting back as in P0-07.
  - [x] 4. Confirm that `-mrelaxed-simd` is **not** enabled by default (relaxed SIMD is nondeterministic, K-03).
- [x] Report: flags, needed builtins, binary size of a trivial module.

### P0-05 Spike: sanitizers without the CRT — S
- Problem: clang's ASan runtime on Windows expects the MSVC CRT, which conflicts with D-03.
- Steps:
  - [x] 1. Try building the P0-03 exe with `-fsanitize=address` plus the CRT (`msvcrt.lib` from the VS 18 install, if present) as a **debug-sanitize-only** variant.
  - [x] 2. Try `-fsanitize=undefined -fsanitize-trap=undefined` (trap mode needs no runtime).
- Possible outcomes:
  - (a) An ASan variant links the CRT only in a `win64 sanitize` build configuration (proposed amendment A-03).
  - (b) Fallback: no ASan. Use UBSan in trap mode, plus arena guard pages and canaries (1A-03).
- [x] Report: the outcome and the recommended build configuration.

### P0-06 Spike: cross-platform float determinism — M (most important spike)
- Goal: retire K-03 before the architecture depends on it (D-36, D-37).
- Steps:
  - [x] 1. Write one C file `det.c` with:
     - draft deterministic `sinf`/`cosf`/`atan2f`/`expf`;
     - a 2000-body "toy physics" loop (circles, gravity, pairwise collision response with `sqrt` and `/`) running 10 000 ticks;
     - a hash of all state after every 100 ticks;
     - a SIMD variant using `ext_vector_type(4)` of the integration step.
  - [x] 2. Build it for win64 (`-march=x86-64-v2`) and wasm32 with the D-09 flags. Run natively and in Edge headless.
  - [x] 3. Compare the hash sequences. They MUST match exactly.
  - [x] 4. Then deliberately break each rule from SPEC §6 (enable `-ffp-contract=fast`; use `_mm_rsqrt_ps`; set FTZ; use a raw cast of an out-of-range float to int) and confirm that each one changes the hash. This proves the tests can detect violations.
- [x] Report: pass/fail per configuration, and per-tick timing on both targets.
- **If the hashes cannot be made identical, stop and escalate to the owner before Phase 1. D-37 would be reopened.**

### P0-07 Spike: headless browser test harness with no CDP client — S
- Design: the test page itself opens a WebSocket (or `fetch` POST) to the local test server and reports results. The browser is launched with only a URL, and the server exits with the test result code. No Chrome DevTools Protocol client is needed.
- Steps:
  - [x] 1. Write a minimal C HTTP server: static files plus `POST /result`. Use a plain POST in the spike; WebSocket comes in 1B-45.
  - [x] 2. Launch `msedge --headless=new --user-data-dir=<tmp> --no-first-run <url>` from the server process, wait for the result with a timeout, then kill the browser.
  - [x] 3. Check WebGPU availability in headless mode: `navigator.gpu.requestAdapter()` with default flags, with `--enable-unsafe-webgpu`, and with a software adapter (SwiftShader: `--use-webgpu-adapter=swiftshader` or the current equivalent flag). Record which flags give a working adapter **without a GPU** (needed for CI and K-04).
  - [x] 4. Read back a rendered texture (a WebGPU clear plus one triangle) to PNG bytes, and POST them.
- [x] Report: the working flags, adapter info, and timings.

### P0-08 Spike: D3D11 at build time and in headless mode — S
- Steps:
  - [x] 1. From a small C program, `LoadLibraryA("d3dcompiler_47.dll")` and call `D3DCompile` on a trivial HLSL vertex/pixel shader. Write the bytecode out as a C array.
  - [x] 2. Create a D3D11 device with `D3D_DRIVER_TYPE_WARP`, render one triangle to an offscreen texture, read it back with a staging texture and map, and write a raw dump.
  - [x] 3. Write the minimal COM vtable declarations in C (the `ID3D11Device` and `ID3D11DeviceContext` methods used), without `d3d11.h`.
- [x] Report: required vtable slots, WARP device creation time, readback correctness.

### P0 exit
Every spike report exists. O-3 has approved the amendments in §9. P0-06 passed.

---

## 5. Phase 1a — Windows skeleton plus agent loop

### M1 — Build program, base layer, tests

#### 1A-01 `build.c` v1 — M
- Deps: P0-01, P0-03, P0-05.
- Files: `build.c`.
- Steps:
  - [x] 1. Command parser: `build <target> <config> [game_dir]`, `build test [filter]`, `build shaders`, `build bake <game>`, `build api`, `build clean`. Each new command is added by the task that needs it; 1A-01 implements `win64`, `test` and `clean`.
  - [x] 2. Toolchain discovery: `CLANG` env var, then PATH, then `C:\Program Files\LLVM\bin`. Verify the version against `toolchain.txt`; on a mismatch, error out with both versions.
  - [x] 3. Windows SDK discovery: newest folder under `Windows Kits\10\Lib`, with the `um\x64` library path. Overridable through the `BMF_WINSDK` env var.
  - [x] 4. Process spawning with `CreateProcessW`, piping clang's stdout/stderr through unchanged, and exit-code propagation.
  - [x] 5. Flag sets exactly as D-09, plus the P0-03/P0-05 findings, defined in one table per (target, config).
  - [x] 6. Outputs go to `out/<game>/<target>-<config>/`. Generated files go to `out/gen/`.
  - [x] 7. Skip-if-unchanged: hash all input file contents plus the flags. If they match the last build's stamp file, print `{"build":"up_to_date"}` and exit 0.
  - [x] 8. Print one final JSON summary line: `{"build":"ok","target":...,"ms":...,"out":...}` or `{"build":"fail",...}`.
- Note: `build.c` is CRT-free (D-04, D-12 as amended by A-12) and is built by `bootstrap.bat`. It reuses `src/base/base_types.h`, `src/base/base_builtins.c` and `src/platform/win32/win32_api.h`.
- [x] Acceptance: `build win64 debug games/_empty` builds and links a trivial game against an empty runtime. A second run reports `up_to_date`. Errors pass through in `file:line:col` form.

#### 1A-02 base: types, asserts, builtins — S
- Deps: 1A-01.
- Files: `src/base/base_types.h`, `src/base/base_builtins.c`, `src/bmf.c` (unity root, initial).
- Content:
  - The typedefs from SPEC §5.1, plus `BMF_STATIC_ASSERT` checks on their sizes.
  - Platform and architecture detection (`BMF_OS_WINDOWS`, `BMF_OS_WEB`, `BMF_ARCH_X64`, `BMF_ARCH_WASM32`, `BMF_ARCH_ARM64`).
  - `BMF_ASSERT(cond)`: in debug it calls `bmf_assert_fail(file, line, expr)` (whose platform hook writes a crash dump from 1A-35 onward); in release it compiles to nothing.
  - `bmf_panic`.
  - Own `memcpy`, `memset`, `memmove`, `memcmp` for x64 and wasm, marked `__attribute__((no_builtin))` so the compiler cannot turn their loops back into calls to themselves (infinite recursion).
  - `_fltused`, plus the stack probe from P0-03.
  - The wasm builtins from P0-04.
- [x] Acceptance: unit tests for the mem functions (overlap, alignment, sizes 0–1024). No unresolved symbols on either target.

#### 1A-03 base: memory — M
- Deps: 1A-02. The virtual-memory hooks live in `src/platform/win32/win32_core.c` (the 1A-11 file) from the start.
- Files: `src/base/base_memory.{h,c}`.
- Content:
  - `bmf_arena`: reserve/commit, push with alignment, pop-to-mark, reset, high-water mark, debug fill patterns (`0xCD` on push, `0xDD` on reset).
  - Guard page after each arena's reserved range.
  - Scratch arenas `scratch_begin(conflict)` / `scratch_end`, two per thread.
  - `bmf_pool`: fixed capacity, free list, generational `u32` handles (index:20 | generation:12), `valid`, `get`, `alloc`, `free`, and dense iteration.
  - The memory budget layout of the reserved range (configurable per game through `bmf_game_desc` extensions, D-39, with defaults).
- [x] Acceptance: unit tests cover alignment, reset, high-water marks, a pool's stale-handle detection after 4096 reuse cycles, and the guard page faulting (tested via an expected-crash subprocess test).

#### 1A-04 base: deterministic math — L (split: 1A-04a scalar, 1A-04b vector and matrix, 1A-04c fast tier)
- Deps: 1A-02, P0-06 (reuse its validated functions).
- Files: `src/base/base_math.{h,c}`, `tests/test_math.c`, `tests/gen/math_ref.c` (reference generator). Also `bmf_rng_normal` (Box–Muller) in `src/base/base_rng.h`, moved here from 1A-07.
- Note (1A-04a, 2026-11-19): the reference is computed at test time by `tests/gen/math_ref.c` (big-integer pi, ln 2 and 2/pi; double-double function values) instead of being emitted as `math_ref.inc`: 100 000 samples for 13 functions would be tens of megabytes. Cross-target identity is checked through per-function output hashes in `tests/test_math.c`.
- Content:
  - **a.** `bmf_sqrtf` (instruction), `floorf`/`ceilf`/`roundf`/`truncf` (SSE4.1 `roundss` / wasm `f32.floor`, which are exact), `fabsf`, `fminf`/`fmaxf` with defined NaN behavior, `bmf_f32_to_i32` (saturating), `sinf`/`cosf`/`sincosf` (range reduction plus minimax polynomials), `tanf`, `asinf`, `acosf`, `atanf`, `atan2f`, `expf`, `exp2f`, `logf`, `log2f`, `powf`, `fmodf`, `hypotf` (scaled). Each has documented max ULP error and domain notes.
  - **b.** `bmf_v2/v3/v4` operations as `static inline` functions, `bmf_m3x2` (2D affine), `bmf_rect`, `bmf_aabb`, `bmf_rot` (cos/sin pair), lerp/clamp/smoothstep, and color pack/unpack.
  - **c.** `bmf_fast_*`: lower-precision versions for render and audio. They are listed in an internal `simulation_forbidden` table that a test greps for in sim modules (1A-41).
  - Reference generator: computes f64 results with long Taylor/Chebyshev series in double (no libm), rounds to f32, and emits `tests/gen/math_ref.inc` with 100 000 sampled inputs per function including edge cases.
- [x] Acceptance: ULP bounds hold for all samples. win64 and wasm produce identical output bits for all samples (the wasm part is verified once M9 exists; until then it is recorded as a pending check in `docs/progress.md`). (2026-11-19: ULP bounds hold, max 0.4999 ULP; win64 = wasm/Node verified on a 4 000-input driver; the full 100 000-sample wasm run is pending M9.)

#### 1A-05 base: SIMD — S
- Deps: 1A-02.
- Files: `src/base/base_simd.h`.
- Content: `bmf_f32x4`, `bmf_i32x4` and `bmf_u32x4` through `ext_vector_type`; load/store (aligned and unaligned), arithmetic, compare, select, min/max, and the `sqrt` and `/` operations only. There are deliberately **no** rcp/rsqrt operations (D-37 rule 9).
- [x] Acceptance: the tests compare every operation against the scalar path bit-for-bit.

#### 1A-06 base: strings, format, parse, JSON writer — M
- Deps: 1A-03.
- Files: `src/base/base_string.{h,c}`.
- Content:
  - `bmf_str` operations (eq, starts_with, find, split-iterator, trim), UTF-8 decode/encode, and UTF-16 to UTF-8 conversion (for Win32).
  - `bmf_fmt` into an arena or a fixed buffer, with specifiers `%i %u %x %f %s %S(bmf_str) %v2`. Float formatting must be shortest round-trip (needed for exact JSON values), via an own Ryu-style or Grisu-style algorithm implemented from the paper.
  - `bmf_parse_i32/i64/f32` (correctly rounded f32 parse).
  - `bmf_json_writer` with object/array/key/value and escaping.
  - Switch `tools/test/test_main.c` output from its local text writer to `bmf_json_writer`.
- Note (2026-11-19): the float algorithms are an own design instead of Ryu/Grisu: shortest digits by searching decimal grids inside the f32 rounding interval (computed exactly in f64) and verifying each candidate with the exact parser; parsing by an f64 fast path with an error bound plus a 768-bit integer fallback near rounding boundaries. Same guarantees (shortest, closest, exact round trip, correctly rounded parse), no generated tables, verified by the tests. `bmf_err` was added to `base_types.h` (first fallible API).
- [x] Acceptance: float format then parse round-trips every one of 10M random f32 bit patterns exactly. The tests cover every specifier and edge case.

#### 1A-07 base: hash, RNG, sort — S
- Deps: 1A-02.
- Files: `src/base/base_hash.{h,c}`, `base_rng.h`, `base_sort.{h,c}`.
- Content:
  - A 64-bit hash, designed from multiply-xorshift mixing and documented, with avalanche tests. Streaming variant: `bmf_hasher` with init/update/final.
  - CRC32 (for PNG) and Adler-32 (for zlib).
  - PCG32 `bmf_rng`: seed, next u32, f32 in [0,1), range, normal (Box–Muller using deterministic math; deferred to 1A-04a, which provides log and cos).
  - LSD radix sort for `u32` and `u64` keys with `u32` payloads (8-bit or 11-bit digits, skipping a pass when all keys share the digit); insertion sort for n ≤ 32. Both are stable.
- [x] Acceptance: published PCG32 test vectors match. The sort is verified against the insertion sort on random data, including stability.

#### 1A-08 Test runner — S
- Deps: 1A-01, 1A-06. (Done before 1A-06 on 2026-11-19 because 1A-02's acceptance needs tests; the runner uses a local text writer, and 1A-06 switches it to `bmf_json_writer`.)
- Files: `tools/test/test_main.c`, `tests/tests.c` (registry), `tests/test_*.c`.
- Content:
  - Tests are plain functions `void test_x(bmf_test* t)` listed in one explicit table. No auto-registration macros.
  - Checks: `bmf_check(t, cond, "msg")`, `bmf_check_eq_u64`, and so on, which record file and line.
  - Output: one JSON line per test, `{"test":"math.sin_ulp","ok":1,"ms":3}`, then a summary line. The exit code is the failure count, clamped to 1.
  - Expected-crash tests run in a subprocess (the runner re-invokes itself with `--run-one name`).
  - `build test [filter]` builds and runs the runner.
- [x] Acceptance: all M1 tests pass. A deliberately failing test reports its file and line.

### M2 — Win32 platform and GPU

#### 1A-10 Win32 API declarations — M
- Deps: P0-03, P0-08.
- Files: `src/platform/win32/win32_api.h`.
- Content: hand-written declarations for only the functions, structs and constants used, grouped by DLL. Each has a comment with the MSDN name. COM interfaces (`ID3D11*`, `IDXGI*`, `IMMDevice*`, `IAudioClient*`) are declared as C vtable structs **with every slot up to the last one we use, in exact order**. Slot order is verified against the SDK headers once by a test-only check (1A-14).
- [x] Acceptance: compiles with `-Werror` and has no `windows.h` include anywhere in `src/`.
- Note (1A-10): every slot, struct layout, constant, GUID and parameter list was compared with the SDK 10.0.26100 headers by a throwaway generator (two host programs, ours vs SDK, outputs diffed). For the 1A-14 test: SDK and our names collide (`HANDLE`, `RECT`, ...), so compile two separate translation units that each print the same table (offsets, sizes, constants, GUID bytes) and compare, rather than one file that includes both headers. In C mode the WASAPI IIDs and `CLSID_MMDeviceEnumerator` have no linkable definition; take them from the `MIDL_INTERFACE`/`DECLSPEC_UUID` strings, and `KSDATAFORMAT_SUBTYPE_IEEE_FLOAT` from its `STATIC_` macro.

#### 1A-11 Win32 platform core — M
- Deps: 1A-10, 1A-03.
- Files: `src/platform/platform.h` (the internal contract, SPEC §9), `src/platform/win32/win32_core.c`.
- Content:
  - Entry point; command line via `GetCommandLineW` converted to UTF-8 argv.
  - stdout/stderr through `WriteFile` (line-buffered in arena buffers).
  - `VirtualAlloc` reserve/commit/decommit; `QueryPerformanceCounter` time in nanoseconds; high-resolution waitable timer (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`) for sleep.
  - Files: read whole file, memory-map read-only, atomic write (temp file then `MoveFileExW` with `REPLACE_EXISTING`).
  - Threads (`CreateThread` with explicit stack size), `__atomic_*` wrappers, `WaitOnAddress`/`WakeByAddress` futex, and a semaphore.
  - **FPU setup at every thread start: MXCSR = round-to-nearest, FTZ=0, DAZ=0, all exceptions masked** (D-37 rule 10). `bmf_fpu_check()` asserts this in debug at every tick.
  - DLL loading: `LoadLibraryW`, `GetProcAddress`, plus file-change polling (cheap mtime check every 250 ms).
- [x] Acceptance: unit tests for file round-trip, atomic write, a thread spawn/join with an atomic counter, and an MXCSR check inside a spawned thread.

#### 1A-12 Win32 window and events — M
- Deps: 1A-11.
- Files: `src/platform/win32/win32_window.c`.
- Content:
  - `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)`; window class and creation; `WM_DPICHANGED` resize; borderless fullscreen toggle.
  - Raw Input registration for the mouse (relative delta); `WM_INPUT`; keyboard via `WM_KEYDOWN/UP` scancodes mapped to the `BMF_KEY_*` enum (a positional scancode table); `WM_CHAR` (UTF-16 surrogate handling) to the text queue.
  - `WM_POINTER*` for touch and pen (`EnableMouseInPointer(FALSE)`).
  - Focus loss releases all keys; `WM_SIZE` with minimize/restore and suspend.
  - XInput through `LoadLibraryW("xinput1_4.dll")`, polled per frame, with dead-zone handling.
  - Events are pushed into a fixed-size platform event queue that the input module (1A-22) consumes.
- [x] Acceptance: a manual test window prints events as JSON lines in `--control` mode. Automated tests cover the scancode mapping table and the UTF-16 surrogate decoding.
- Note (1A-12): `--control` currently means "print platform events" (manual test in `win32_main.c`); 1A-31 replaces it with the control protocol. The key enum, key names, gamepad struct and event queue live in `platform.h`/`platform_common.c` until the input module (1A-22) and `bmf.h` (1A-24) take them over.

#### 1A-13 GPU API — S
- Deps: 1A-03.
- Files: `src/gpu/gpu.h`.
- Content (D-25):
  - Handles `bmf_gpu_buffer`, `_texture`, `_sampler`, `_pipeline`, `_bindset` (u32, generational).
  - Descriptor structs: buffer (usage vertex/index/uniform/storage/instance, size, dynamic flag); texture (format: rgba8_unorm, rgba8_unorm_srgb, bgra8, r8, depth24s8; usage sampled/render-target/copy-src); pipeline (shader id, vertex layouts with instance step, blend preset premultiplied/additive/opaque, topology, cull, depth); bindset (uniform buffer range plus textures and samplers per slot).
  - Frame API: `begin_frame`, `begin_pass(target, clear)`, `set_pipeline`, `set_bindset`, `set_vertex_buffer`, `draw_instanced`, `end_pass`, `end_frame`/`present`, `write_buffer(offset, data)`, `read_texture(tex) → rgba8 pixels` (screenshots).
  - The shader id is an enum generated per backend from the `shaders/` directory listing.
  - **Null backend** `gpu_null.c`: accepts every call and renders nothing. Used for `--no-gpu` simulation-only runs and tests (amendment A-02).
- [x] Acceptance: the null backend passes an API usage test. The header has contract comments. (2026-11-21: `gpu.null_api_usage` checks exact per-frame stats over two frames; four expected-crash tests prove validation fires with the intended message.)
- Note (1A-13, 2026-11-21): deviations from the content list above, all recorded in `src/gpu/README.md`: (1) no separate `gpu_null.c`: the frontend `gpu.c` validates and counts every call and, with the null backend, makes no device calls; D3D11/WebGPU hook in at `// backend:` lines. (2) Per D-25 ("every draw states its pipeline and bindings") there are no `set_pipeline/set_bindset/set_vertex_buffer` calls: one `bmf_gpu_draw(desc)` names them all. (3) Buffer usage is VERTEX (covers instance streams; the step mode is in the pipeline) and UNIFORM only; index and storage buffers wait for a first user. (4) Bind sets are made per shader id (WebGPU bind group layouts). (5) Readback is two-phase (`read_begin`/`read_end` with `BMF_ERR_PENDING`) because WebGPU maps asynchronously. (6) Pipelines carry `color_format`/`depth_format`, and `bmf_gpu_surface()` is a texture handle for the back buffer. New `bmf_err` values `UNSUPPORTED`, `PENDING`.

#### 1A-14 D3D11 backend — L (split: 1A-14a device and swapchain, 1A-14b resources, 1A-14c draw, passes and readback)
- Deps: 1A-12, 1A-13, P0-08.
- Files: `src/gpu/d3d11/gpu_d3d11.c`.
- Content:
  - **a.** `D3D11CreateDevice` (hardware, falling back to WARP; forced WARP in headless mode); DXGI factory 2 swapchain with `FLIP_DISCARD`, 2 buffers, `FRAME_LATENCY_WAITABLE_OBJECT`, `SetMaximumFrameLatency(1)`; tearing support check; resize; device-removed detection with a clean error message.
  - **b.** Buffers (dynamic instance ring with `MAP_WRITE_DISCARD` on wrap and `NO_OVERWRITE` otherwise), textures, samplers, render targets, and an sRGB view where requested.
  - **c.** Pipeline creation from embedded bytecode (input layout plus blend, rasterizer and depth states, all created up front and cached in the pipeline); draw calls; readback through a staging texture.
  - A test-only check, `tests/test_vtable_slots.c`, compiles **with** the SDK `d3d11.h` (test build only) and asserts that our vtable offsets match the SDK's. This is a host test, not part of the runtime (amendment A-01 covers host tools).
- [x] a. device and swapchain (2026-11-21)
- [ ] b. resources
- [ ] c. pipelines, draws, readback, `tests/test_vtable_slots.c`
- [ ] Acceptance: on WARP, a clear-color test and a one-triangle test match expected pixels exactly. A swapchain resize stress test (1000 resizes) shows no leaks (the live-object count from `ID3D11Debug` stays stable in debug). (2026-11-21, 1A-14a: the resize part passes: `gpu.d3d11_resize_stress`, 4 live objects before and after; a deliberately leaked buffer makes it fail. Pixel tests need 1A-14c readback.)
- Note (1A-14a, 2026-11-21): `src/gpu/d3d11/gpu_d3d11.c` is included by `bmf.c` before `gpu.c`; `gpu.c` calls `gpu_d3d11_*` at its `backend:` points inside `#if BMF_OS_WINDOWS`. Device lost = `bmf_panic` with the operation, HRESULT name and removed reason (no recovery in Phase 1). Debug-layer errors panic (they are backend bugs, the frontend validated), warnings go to stderr as JSON lines until `bmf_log` (1A-36). Flip-model `Present` never reports occlusion here, not even minimized (it returns at once), so `begin_frame` checks `IsIconic` to honor its "0 when minimized" contract. The vtable-slot test moves to 1A-14c, when every interface the backend calls is in use.

#### 1A-15 Build-time shader compilation — S
- Deps: 1A-01, P0-08.
- Files: `build.c` (`build shaders` command, also run automatically when shader files change), `shaders/d3d11/*.hlsl`.
- Content: for each `shaders/d3d11/<name>.hlsl` with entries `vs_main`/`ps_main`, compile with `D3DCompile` (target `vs_5_0`/`ps_5_0`, `D3DCOMPILE_OPTIMIZATION_LEVEL3` in release, `DEBUG|SKIP_OPTIMIZATION` in debug) into `out/gen/shaders_d3d11.h`, which contains the `bmf_shader_id` enum and the bytecode arrays. Compiler errors are re-printed in `file:line:col: error:` form.
- [ ] Acceptance: a deliberately broken shader produces a clickable error line and exit code 1.

### M3 — Engine core, input, draw2d

#### 1A-20 draw2d — L (split: 1A-20a instances, sorting and batching; 1A-20b SDF shader; 1A-20c cameras)
- Deps: 1A-14, 1A-15, 1A-07.
- Files: `src/draw2d/draw2d.{h,c}`, `shaders/d3d11/draw2d.hlsl`.
- Content:
  - **a.** The 48-byte instance struct (SPEC §12.2) with a `BMF_STATIC_ASSERT` on its size. Per-frame instance array in the frame arena; `u64` sort keys; radix sort; batch building (runs with equal pipeline and texture become one draw); upload to the GPU ring in one write.
  - Public calls: `bmf_draw_rect`, `_rect_rounded`, `_circle`, `_ring`, `_line` (capsule), `_triangle`, `_sprite`, `_text`. Each takes an explicit layer.
  - **b.** One HLSL pipeline: the vertex shader expands a unit quad per instance (rotation, size, padding for antialiasing); the pixel shader picks an SDF by `kind` (rounded-box, circle, ring, capsule, triangle, glyph-capsule, textured sprite), computes AA coverage with `fwidth`, and outputs premultiplied color.
  - **c.** World camera (y-up meters, default 64 px/m, D-19) and screen camera (y-down logical pixels); a camera stack per layer; `bmf_camera_world_to_screen` and `screen_to_world` (used by input for pointer mapping).
- [ ] Acceptance:
  - [ ] WARP golden images of a shape test sheet.
  - [ ] Bench sprites-10k: CPU submit plus sort < 0.5 ms (SPEC §12.3 targets), measured by the profiler once 1A-36 lands; until then by QPC around the call.
  - [ ] Draw call count for the test sheet ≤ 3.

#### 1A-21 Stroke font — M
- Deps: 1A-20.
- Files: `src/draw2d/font_stroke.c` (glyph data plus layout).
- Content:
  - Glyph data for ASCII 32–126 as line segments on an integer grid, authored in code.
  - Per-glyph advance width; kerning: none.
  - `bmf_text_measure` and `bmf_draw_text(pos, size, color, align, str)`; multi-line text; alignment left/center/right.
  - Stroke width as a parameter.
  - Glyph quality is reviewed by an agent viewing a `shot` of the full character sheet at 3 sizes.
- [ ] Acceptance: a golden image of the character sheet. Every printable ASCII character renders. Measuring is consistent with drawing.

#### 1A-22 Input — M
- Deps: 1A-12, 1A-20c.
- Files: `src/input/input.{h,c}`.
- Content:
  - Action table validation: unique names, at most 64 buttons, at most 8 axes.
  - Default bindings plus a rebinding API.
  - Per-frame device state aggregated from the event queue; per-tick sampling into `bmf_input` with edges computed per tick (SPEC §10).
  - Pointer mapping to world and screen positions using the camera.
  - `bmf_input` injection API used by the protocol (1A-31) and replay (1A-33).
- [ ] Acceptance: unit tests cover edge detection when frames run 0, 1 or 4 ticks; a press-and-release within one frame still produces a press on exactly one tick; and axis normalization.

#### 1A-23 Engine core and game loop — M
- Deps: 1A-11, 1A-22, 1A-20.
- Files: `src/core/core.{h,c}` (new folder, proposed in amendment A-05), `src/platform/win32/win32_main.c`.
- Content:
  - `bmf_ctx`.
  - `bmf_game_desc` validation: API version, state size > 0, tick rate 30–240, field and action tables. Each failure is a startup error JSON line.
  - State-block allocation (zeroed); fixed-step loop exactly as SPEC §8.3, with `max_steps_per_frame` and spiral-of-death protection.
  - Event queue for simulation-to-presentation events (sfx; an audio stub until M7).
  - Waitable-swapchain frame pacing.
  - Static and DLL game linking modes; hot reload per SPEC §8.5 (copy the DLL to `game_<n>.dll`; compare size and layout hash; keep or reset the state).
  - The command-line flags from SPEC §11.1 parsed here.
- [ ] Acceptance: a template game that draws a moving circle runs. Editing its color and rebuilding the DLL hot-reloads within 1 s with the state kept. Changing the state layout triggers a reset event.

#### 1A-24 `bmf.h` public header v0 — S
- Deps: 1A-20…1A-23.
- Files: `src/bmf.h`.
- Content: the public API assembled from module headers. Only public types and functions, each with a contract comment (SPEC §16), and `BMF_API_VERSION 0`. Internal module headers are not exposed.
- [ ] Acceptance: a game compiles including only `bmf.h`. Every public function has a contract comment (checked by `build api` in 1A-43; until then by review).

### M4 — Debug and agent interface

#### 1A-30 Reflection — M
- Deps: 1A-23, 1A-06.
- Files: `src/debug/reflect.{h,c}`.
- Content:
  - `bmf_field` (name, type, offset, count, count_field) and type enum (`I32, U32, I64, U64, F32, V2, V3, V4, B32, U8, HANDLE`).
  - Path parser for `name`, `name[i]` and `name[*]`.
  - Get/set with type checking and bounds checking (bounded by the live count).
  - `dump` with a prefix filter; JSON values written in shortest round-trip float form.
  - `bmf_fields_hash` (hash of names, types, offsets and counts) compared to the declared hash.
  - Built-in engine paths: `rng`, `tick`, and `phys.*` once M6 lands.
  - NaN/Inf scan over all reflected f32 fields (D-37 rule 11).
  - "Did you mean" suggestions by edit distance.
- [ ] Acceptance: unit tests on a synthetic state struct cover every type, out-of-range indices, live-count bounds and suggestions.

#### 1A-31 Control protocol — M
- Deps: 1A-30, 1A-22.
- Files: `src/debug/protocol.{h,c}`.
- Content:
  - stdin reader thread to a lock-free line queue to the main thread.
  - Tokenizer; dispatch table (command name, argument spec, handler) so adding a command is one table row.
  - The full command set from SPEC §11.2, except commands owned by later tasks (`shot` in 1A-32, `record`/`replay` in 1A-33, `snap`/`hash` in 1A-34, `profile`/`mem`/`log` in 1A-36, `phys` in 1B-11). Those return `{"ok":0,"error":"not_implemented"}` until their task lands.
  - Paused-by-default semantics in `--control` mode; `run`/`pause`.
  - Input holds (`press X N`) scheduled per tick.
  - `watch` events.
  - Strict one-response-per-request ordering; error code enum.
  - The protocol version is reported by a `hello` command: `{"ok":1,"bmf":API_VERSION,"game":name,"tick_rate":60}`.
- [ ] Acceptance: a scripted protocol test (the test runner spawns the game with `--headless --control --no-gpu` and pipes a command script) checks exact responses for every command.

#### 1A-32 Headless mode and screenshots — M
- Deps: 1A-14, 1A-31.
- Files: `src/debug/shot.c`, `src/base/base_png_write.c`, `src/base/base_png_read_stored.c`.
- Content:
  - `--headless` uses WARP with an offscreen target at a configurable size (default 1280×720, `--size WxH`).
  - The `shot` command calls `draw` for the current state into the offscreen target, reads it back and encodes it to PNG (stored deflate blocks, CRC32, Adler-32).
  - A minimal PNG reader for stored-deflate files only, used by golden comparisons in 1A-41. The full inflate comes in 1B-30.
  - Image diff: per-channel tolerance and differing-fraction metric; writes a diff PNG.
- [ ] Acceptance: `shot` output opens in standard image viewers. Encode then decode round-trips. The diff tool detects a one-pixel change.

#### 1A-33 Replay — M
- Deps: 1A-31, 1A-07.
- Files: `src/debug/replay.{h,c}`.
- Content:
  - The `.bmfr` format (SPEC §11.4): header including `build_id` (hash of the runtime and game sources) and `tick_rate`.
  - Input stream codec: XOR delta against the previous tick's `bmf_input`, then byte-level RLE.
  - Checkpoints every N ticks (default 60).
  - `record start/stop`; `--replay`; `--replay --verify` (exit 0 on match; on mismatch, prints the first diverging tick, both hashes, and the reflected fields that differ, using a state snapshot taken at the previous checkpoint).
  - Recording is always on in debug (ring of the full session) so crash dumps have the inputs.
- [ ] Acceptance: record then replay produces an identical hash sequence. Mutating one input byte makes verification fail at the expected tick with a field diff.

#### 1A-34 Snapshots and hashing — S
- Deps: 1A-30.
- Files: `src/debug/snap.c`.
- Content: `hash` (streaming hash of the state block plus the tick index); `snap save/load` into named slots in a debug arena (max 16); NaN scan wired into the hash pass.
- [ ] Acceptance: save, step 100, load, step 100 gives an identical hash. A NaN deliberately written via `set` is reported.

#### 1A-35 Crash handling — S
- Deps: 1A-33, 1A-34.
- Files: `src/debug/crash.c`, `src/platform/win32/win32_crash.c`.
- Content:
  - `SetUnhandledExceptionFilter` plus the assert hook.
  - On a crash, write `out/crash/<UTC>.bmfr` (inputs from tick 0 to the crash tick) and `.state` (the last checkpoint state), and print the crash JSON line (SPEC §11.4) with exception code and address.
  - Re-entrancy guard. Crash-time code uses only pre-allocated memory.
- [ ] Acceptance: expected-crash tests (null dereference, assert, divide by zero via an integer path) produce a replay, and replaying it reproduces the crash at the same tick.

#### 1A-36 Profiler, memory report and logging — M
- Deps: 1A-31.
- Files: `src/debug/profile.{h,c}`, `src/debug/log.c`.
- Content:
  - Zone enum and game extension range; `bmf_zone_begin/end` as `static inline` with debug and agent builds only.
  - `rdtsc` calibrated against QPC at startup.
  - Per-tick ring of 1024 ticks; `profile N` computes calls, mean, p50, p99 and max per zone plus frame stats and counters.
  - `mem` reports every arena's used, committed and high-water values.
  - `bmf_log` ring buffer with levels; JSON lines to stderr in control mode; `log` drains it.
- [ ] Acceptance: profile output for a synthetic busy loop of known cost is within 5% of the expected value. A zone-nesting error is detected in debug.

### M5 — Hello game, golden infrastructure, API.md

#### 1A-40 `games/hello` — S
- Deps: M4.
- Files: `games/hello/game.c`.
- Content: N bouncing circles with manual integration (no physics module), wall collisions, a click/tap to spawn, a `jump` action to impulse all circles, a score counter, text with stroke font, field table, actions. All constants per second (D-37 rule 15).
- [ ] Acceptance: it runs windowed, headless and under control mode. It is used as the template for new games.

#### 1A-41 Golden test infrastructure — M
- Deps: 1A-32, 1A-33, 1A-40.
- Files: `tools/test/golden.c`, `games/hello/tests/*`.
- Content:
  - `build test` discovers every `games/*/tests/*.bmfr` and runs `--replay --verify` at the game's tick rate **and** at 120 Hz, with replays recorded per rate.
  - Each `*.png` golden with a sidecar `*.shot` (tick, size) is compared using the tolerances in SPEC §15.
  - `--update-golden NAME` regenerates goldens and prints each changed file.
  - A static check greps simulation code paths (`tick` call graph approximated by file list: `src/physics2d/`, and game files declared sim-only) for `bmf_fast_`, `rdtsc`, raw `(i32)` casts of floats, and `__builtin_fma`. Lint failures are test failures.
- [ ] Acceptance: hello goldens pass. A deliberate change of the bounce constant fails both replay and image goldens with readable output.

#### 1A-42 Perf budget runner and benchmark scenes — S
- Deps: 1A-36, 1A-41.
- Files: `tools/test/perf.c`, `tests/bench/sprites10k/`, `games/hello/perf.budget`.
- Content: release `BMF_AGENT=1` build; runs the benchmark replay; parses the budget lines (`zone=<name> p99_us<=N`); appends results to `out/bench/history.jsonl` with the git revision if available, otherwise a timestamp.
- [ ] Acceptance: an artificially slowed zone fails its budget with the breakdown printed.

#### 1A-43 `build api` (API.md generator) — S
- Deps: 1A-24.
- Files: `build.c` (api command).
- Content: parses `src/bmf.h` (simple line parser relying on the strict header formatting rules in SPEC §16) and emits `API.md`: grouped by module, signature plus contract comment, with a token estimate at the top. It fails if any public function lacks a contract comment.
- [ ] Acceptance: `API.md` is generated and under the 30k-token target, and the missing-comment check works.

#### 1A-44 Module READMEs and docs — S
- Deps: all M1–M4 tasks.
- Files: `src/*/README.md`, `docs/setup.md`, `docs/decisions.md`.
- Content: per SPEC §17 (invariants, data layout, hot paths, limits, at most 60 lines each).
- [ ] Acceptance: every module directory has a README. They are reviewed against the code by a separate review agent.

#### 1A-45 Phase 1a exit validation — S
- Deps: 1A-40…1A-44.
- Steps:
  - [ ] 1. Spawn a fresh agent with **only** `AGENTS.md`, `API.md` and `games/hello`, with the task "add a lives counter that decreases when a circle touches the bottom edge, plus a game-over screen; verify through the protocol."
  - [ ] 2. Record every point of friction in `docs/friction.md`.
  - [ ] 3. Fix the blockers. Spec-level issues go to the owner (R8).
- [ ] Exit criteria (SPEC §18): hello passes golden replay and image tests at 60 Hz and 120 Hz; the fresh agent succeeds without reading framework sources; sprites-10k meets budget.

---

## 6. Phase 1b — Full 2D stack plus web

### M6 — physics2d and jobs (Lane B; can start after M1)

All physics code follows D-21/D-30 (state inside the block, no pointers) and SPEC §6. Every task adds tests to `tests/physics/`.

#### 1B-01 Geometry and mass — M
- Files: `src/physics2d/phys_geom.c`.
- Content: shape structs (circle, capsule, polygon ≤ 8 vertices with precomputed normals and radius, segment, chain with ghost vertices); convex hull (deterministic, gift-wrap or monotone chain); mass, centroid and inertia per shape; AABB per shape; transform helpers (`bmf_rot`).
- [ ] Acceptance: mass properties match analytic values within 1e-6 relative error. The hull is correct on degenerate input (collinear, duplicate points).

#### 1B-02 Narrowphase manifolds — L (split by shape pair)
- Files: `src/physics2d/phys_manifold.c`, `phys_distance.c`.
- Content:
  - Manifold functions for all pairs: circle–circle, circle–capsule, capsule–capsule, polygon–circle, polygon–capsule, polygon–polygon (SAT reference face plus clipping, up to 2 points), segment and chain against every shape with one-sided chain collision.
  - Feature IDs per point for warm-start matching.
  - GJK distance and a shape-cast primitive for TOI and queries.
  - Speculative distance margin parameter.
- [ ] Acceptance: per-pair tests on hand-computed configurations (separated, touching, overlapping, deep, rotated); feature IDs stay stable under small motion.

#### 1B-03 World storage and API — M
- Files: `src/physics2d/phys_world.c`, part of `bmf.h`.
- Content:
  - `BMF_PHYS_DECL(name, max_bodies, max_shapes, max_joints, max_contacts)` declares a struct type with the world header and all SoA arrays, for embedding in the game state.
  - Body and shape pools with generational handles.
  - Create/destroy body, shape and joint; setters and getters (position, velocity, mass override, gravity scale, damping, bullet flag, sensor, filter, friction, restitution, user data `u32`).
  - Reflected paths `phys.body[i].pos` and so on (1A-30 integration).
- [ ] Acceptance: create/destroy churn tests; the state block stays pointer-free (a test copies the state to a different address mid-simulation and the hashes continue identically).

#### 1B-04 Broadphase — M
- Files: `src/physics2d/phys_broad.c`.
- Content: per tick, compute fattened AABBs, then `u64` cell keys (cell x, cell y, shape index) in the frame/level arena (derived data); radix sort; pair generation within cells; deduplication via sorted `(min, max)` pair keys; filter checks. Output: a sorted pair array (D-18 rule 5). The cell size heuristic is 2× the median shape size, overridable.
- [ ] Acceptance: pairs equal a brute-force O(n²) reference on random scenes; output order is deterministic; 2k circles take < 0.3 ms.

#### 1B-05 Contact persistence and events — M
- Files: `src/physics2d/phys_contact.c`.
- Content: the contact table in state, keyed by shape pair and sorted; matching new manifolds to old by feature ID for warm-start impulses; begin/end touch, hit (approach speed above threshold) and sensor enter/exit events into fixed state arrays with an overflow counter.
- [ ] Acceptance: event sequences on scripted scenarios are exactly as expected; warm-start values carry across ticks.

#### 1B-06 Solver — L
- Files: `src/physics2d/phys_solver.c`.
- Content:
  - Soft-step substepped solver (TGS-soft family, implemented from published descriptions): per substep, integrate velocities, warm start, solve contacts with soft constraints (contact hertz and damping ratio), integrate positions, relax, then apply restitution.
  - Friction (Coulomb, with anchor), rolling resistance optional (off by default).
  - Speculative contacts.
  - Parameters: substeps (default 4), contact hertz, joint hertz, push-out max velocity.
  - Constraint ordering is deterministic (contact table order).
- [ ] Acceptance:
  - [ ] Scenario tests: a stack of 10 boxes stays stable for 10 s; a 2k circle pile settles without explosion; restitution 1.0 bounce height is preserved within 2% over 10 bounces; friction slope tests behave as expected at known angles.
  - [ ] Bench circles-2k-pile: tick < 2 ms on desktop.

#### 1B-07 Joints — M
- Files: `src/physics2d/phys_joint.c`.
- Content: distance (with spring and limits), revolute (limits, motor), weld (soft), mouse/target. All soft-constraint form, integrated in the solver substeps.
- [ ] Acceptance: scenario tests (pendulum period within 1%, chain of 20 links stable, ragdoll-like structure, motor reaching target speed).

#### 1B-08 Continuous collision — M
- Files: `src/physics2d/phys_toi.c`.
- Content: TOI via conservative advancement plus GJK for `bullet` bodies against static geometry; the body is moved to the TOI pose and its remaining motion is clipped (no sub-stepping of the whole world).
- [ ] Acceptance: **a ball at 100 m/s at 60 Hz never tunnels through a 0.1 m wall** over 10 000 random angles (SPEC §14.1). Bench bullets-1k stays within budget.

#### 1B-09 Queries — S
- Files: `src/physics2d/phys_query.c`.
- Content: raycast (closest, all-sorted), shape cast, point, and AABB overlap with filters; results sorted by `(fraction, shape index)` so ordering is deterministic. The protocol `phys query` command is added.
- [ ] Acceptance: compared against brute-force references.

#### 1B-10 Sleeping and islands — M
- Files: `src/physics2d/phys_island.c`.
- Content: islands are built deterministically (union-find over contacts and joints in contact-table order); sleep timers; wake on contact or explicit API.
- This is proposed amendment A-06: the spec does not mention sleeping, but piles need it to meet budgets.
- [ ] Acceptance: a settled pile goes to sleep and the cost drops by more than 80%; waking is correct; determinism holds.

#### 1B-11 Physics debug draw and validation — S
- Files: `src/physics2d/phys_debug.c`.
- Content: `bmf_phys_debug_draw(world, flags)` through draw2d (shapes, AABBs, contacts, normals, joints) and a protocol toggle `phys draw on|off`.
- [ ] Acceptance: golden images of the scenario scenes.

#### 1B-12 Job system — M
- Files: `src/job/job.{h,c}`.
- Content:
  - Worker pool (`min(cores-1, 7)`), with MXCSR set per worker (D-37 rule 10).
  - Bounded MPMC queue (own Vyukov-style ring implemented from the published algorithm), parallel-for with fixed chunk sizes, wait with participation (the caller also runs jobs).
  - The inline mode for wasm and for `--jobs 0`.
  - Physics narrowphase is parallelized over the pair array with outputs written by index (disjoint).
- [ ] Acceptance: physics hashes are identical with 0, 1, 3 and 7 workers across 10 000 ticks; a speedup is measured on circles-2k-pile and recorded.

### M7 — Audio (Lane D; can start after M1; the backend needs M2)

#### 1B-20 Mixer — M
- Files: `src/audio/mixer.{h,c}`.
- Content:
  - A voice array (64): source buffer, position (fixed-point 32.32), rate, gain, pan, loop, bus.
  - Buses master/music/sfx with smoothed gain ramps (no clicks); a linear resampler; soft clipper on master.
  - Command queue simulation→audio (SPSC lock-free ring): play, stop, set-param, with voice handles.
  - Voice stealing policy: oldest lowest-priority voice.
  - Offline render API for tests.
- [ ] Acceptance: offline renders hash identically across runs; there is no click at voice start or stop (checked by max sample-delta tests).

#### 1B-21 Procedural synth — M
- Files: `src/audio/synth.c`.
- Content: `bmf_sfx_desc` (SPEC §14.2): oscillators square/saw/sine/triangle/noise, duty sweep, ADSR, pitch slide and delta-slide, vibrato, low-pass and high-pass with sweep, repeat, phaser optional. `bmf_sfx_bake(desc, arena) → sample buffer` at load time. A gallery of 8 presets (jump, coin, hit, explosion, power-up, blip, laser, bounce) is exposed as starting points for agents.
- [ ] Acceptance: preset renders hash stable; WAV files written for listening (agents cannot hear, but the owner can).

#### 1B-22 WASAPI backend — M
- Deps: M2.
- Files: `src/platform/win32/win32_audio.c`.
- Content: COM init; default device; `IAudioClient3` low-latency shared mode when available, falling back to `IAudioClient` with event callback; 48 kHz float32 stereo (with a format converter if the mix format differs); audio thread with `AvSetMmThreadCharacteristics` (avrt.dll, a system DLL; add to D-02 list via amendment A-07); device-change handling (reopen on default-device change). Headless mode uses a null audio device.
- [ ] Acceptance: measured period ≤ 10 ms when supported; unplugging or changing the device recovers within 1 s; no glitches in a 10-minute soak (underrun counter = 0).

### M8 — Assets (Lane C; can start after M1)

#### 1B-30 Inflate, PNG and WAV decoding — M
- Files: `src/base/base_inflate.c` (tools only), `tools/bake/png.c`, `tools/bake/wav.c`.
- Content: a full inflate (stored, fixed and dynamic Huffman), implemented from RFC 1951; PNG per the PNG specification (all color types at bit depth 8, palette with tRNS, Adam7 interlace, all 5 filters, CRC check); WAV RIFF parse (PCM16, float32, mono/stereo, any rate with resampling to 48 kHz at bake time).
- [ ] Acceptance: a test corpus of PNGs generated by our own encoder plus hand-crafted edge cases (filters, interlace, palette). Malformed files fail with an error, never a crash (fuzz loop of 100k mutated inputs).

#### 1B-31 Baker, pak writer and `assets.h` — M
- Files: `tools/bake/bake.c`, `build.c` (bake command).
- Content:
  - Scans `games/<g>/assets/` (`img/`, `sfx/`, `music/`); a skyline atlas packer (deterministic order: sorted by height, then name) with padding and extrusion.
  - Pak writer (SPEC §14.3: header with magic, version and content hash; TOC; 64-byte-aligned blobs).
  - `assets.h` with `BMF_ASSET_<TYPE>_<NAME>` IDs, sanitized names, and collision detection.
  - Incremental: skips the bake if the input hash is unchanged.
- [ ] Acceptance: a deterministic pak (identical bytes across runs); an asset name typo in game code produces a compile error.

#### 1B-32 Runtime pak loader — S
- Files: `src/asset/asset.{h,c}`.
- Content: memory-map (native) or pre-fetched buffer (web); TOC validation (magic, version, bounds); GPU texture creation from atlas pages; sound lookup; `bmf_asset_sprite(id) → atlas uv rect`.
- [ ] Acceptance: corrupted pak files are rejected with a clear error.

#### 1B-33 Procedural textures — S
- Files: `src/asset/texgen.c`.
- Content: `bmf_tex_gen_*`: value/gradient noise (own implementation), fBm, gradients, SDF shape rasterization (circle, rounded rect, star, polygon) into an atlas region at load time, using the runtime skyline packer shared with the baker.
- [ ] Acceptance: golden images.

### M9 — Web (needs M5; Lane E can start the dev server after M1)

#### 1B-40 wasm build target — S
- Files: `build.c`.
- Content: the `wasm` target with D-09 flags plus the P0-04 findings; `--import-memory` with initial and maximum memory computed from the arena budget; `-z stack-size`; exported function list; output folder with `index.html` (template), `bmf_web.js` copy, `game.wasm`, `game.pak`.
- [ ] Acceptance: hello compiles to wasm with no undefined imports except the declared JS imports.

#### 1B-41 Web platform (wasm side plus `bmf_web.js`) — L
- Files: `src/platform/web/web_platform.c`, `src/platform/web/bmf_web.js`, `src/platform/web/index.html`.
- Content:
  - JS: feature check (WebGPU missing gives the unsupported page, D-28); fetch and instantiate; memory; time (`performance.now`); logging to the console and to the protocol bridge; a `requestAnimationFrame` loop calling the exported `bmf_web_frame(dt_ns)`; canvas sizing with `devicePixelRatio` and `ResizeObserver`; visibility change (suspend).
  - Input: keyboard (`code`-based to `BMF_KEY_*`), pointer events (mouse, touch, pen, with `setPointerCapture`), gamepad polling, text via a hidden input element for mobile keyboards. All are written into an event ring in linear memory, so there is no call per event.
  - The asset pak is fetched into linear memory before init.
  - The JS stays under the 1500-line target; whitespace stripping in release.
- [ ] Acceptance: hello runs in Edge and Chrome on desktop, and in Chrome on Android (manual check by the owner, recorded in `docs/progress.md`).

#### 1B-42 WebGPU backend — L
- Files: `src/gpu/webgpu/gpu_webgpu.c` (command encoder), the WebGPU part of `bmf_web.js` (decoder and executor), `shaders/webgpu/draw2d.wgsl`.
- Content:
  - Resource creation via direct imports returning handles.
  - Per-frame command stream (op enum plus packed args) decoded by one JS function per frame (SPEC §9.2).
  - Instance data uploaded with one `writeBuffer`.
  - WGSL port of the draw2d shader (SDF math must match HLSL operation for operation).
  - Canvas context configuration (`premultiplied` alpha mode, preferred format).
  - Screenshot readback via `copyTextureToBuffer` and `mapAsync`, returned to wasm, encoded by our PNG writer.
  - Device-lost handling.
- [ ] Acceptance: draw2d golden images on the WebGPU backend within the per-backend tolerance (amendment A-08); sprites-10k meets the mobile-web budget.

#### 1B-43 Web audio — S
- Files: the audio part of `bmf_web.js`, `src/platform/web/web_audio.c`.
- Content: an AudioWorklet processor (inline Blob URL); the main-thread mixer renders about 40 ms ahead in chunks and posts them (transferable `Float32Array`); upgrades to a SharedArrayBuffer ring when `crossOriginIsolated`; resumes the AudioContext on the first user gesture.
- [ ] Acceptance: no underruns in a 5-minute play session at a steady 60 fps (counter exposed via the protocol).

#### 1B-44 Web input polish — S
- Content: virtual-control helpers for touch (game-side draw2d helper per SPEC §10); prevention of default browser gestures (`touch-action: none`, context menu, pinch zoom); fullscreen API; pointer lock optional.
- [ ] Acceptance: a manual checklist on Android Chrome.

#### 1B-45 Dev server — M (Lane E)
- Files: `tools/serve/serve.c`.
- Content:
  - HTTP/1.1 static serving (keep-alive, range not needed), MIME table (`.wasm` as `application/wasm`), COOP/COEP headers, caching disabled.
  - WebSocket (RFC 6455: own SHA-1 and base64, text frames, masking, ping/pong, close).
  - Protocol bridge: the server's stdin and stdout are connected to the page's WebSocket, so an agent drives a web build exactly like a native one (`serve games/hello --control`).
  - File-watch auto-reload (poll mtimes, then a `reload` message to the page).
  - Test mode: `serve --test <game>` launches headless Edge (Chrome as fallback) with the P0-07 flags, runs a replay-verify script, collects JSON results and exits with the result code.
- [ ] Acceptance: SHA-1 against published test vectors; WebSocket interop with Edge and Chrome; the protocol test script from 1A-31 passes against the web build.

#### 1B-46 Cross-platform determinism gate — S
- Files: `tools/test/golden.c` (web mode).
- Content: `build test` also builds every game for wasm and runs every golden replay through `serve --test`; the win64 and wasm hash sequences must be identical (D-18, D-36). The math reference test (1A-04) runs on wasm too. Golden images: per-backend sets (amendment A-08), plus a cross-backend parity check at looser tolerance.
- [ ] Acceptance: all hashes identical. One deliberately introduced `bmf_fast_sinf` in sim code is caught by either the lint or the hash gate.

### M10 — Port of an existing game

#### 1B-50 Port target selection — S
- Steps: measure LOC (excluding `node_modules`/`dist`) of the bounce-family games under `c:\dev\github\bazimazi\`; list their features (from source and README); propose the 2 smallest candidates with feature lists to the owner (O-4).
- [ ] Output: `docs/port/<game>.md` with the feature parity checklist and baseline measurements of the TS original: load time, bundle size, frame-time p99 in Chrome, memory.

#### 1B-51 Port implementation — L
- Files: `games/<port>/`.
- Content: the full feature parity checklist; golden replays covering each core mechanic; perf budget; native and web builds.
- [ ] Acceptance: SPEC §18 Phase 1b exit — parity checklist complete, perf comparison recorded in `docs/port/<game>.md`, and win64/wasm hashes matching on all goldens.

#### 1B-52 Phase 1 retrospective — S
- [ ] Output: `docs/friction.md` triaged into API changes for Phase 2; spec amendment proposals to the owner; LOC and time actuals compared with this plan's estimates.

---

## 7. Later phases (outline; detailed plan written at each phase start)

### Phase 2 — Harden through real games
1. Three new games built entirely by agents from `API.md` only; each adds a friction log.
2. TTF reader (`glyf` quadratic outlines, cmap format 4/12, hmtx) and MSDF generator in the baker; draw2d MSDF glyph kind.
3. Streaming music: own ADPCM-style codec (bake-time encoder, runtime decoder in the mixer).
4. GPU timestamp queries (D3D11 query objects; WebGPU timestamp-query where available).
5. AVX2 runtime dispatch for measured hot kernels only (sort, broadphase, instance building), with bit-identical results to the baseline path enforced by tests (D-37 rule 7).
6. API review, then `BMF_API_VERSION 1` freeze (SPEC §18).

### Phase 3 — Platforms (no start before the v1 freeze, D-35)
For each platform: platform layer, GPU backend, audio backend, input, the determinism gate (D-18) passing on real hardware, and its golden image set.
- Android: NDK `NativeActivity` (minimal manifest), Vulkan backend, AAudio, touch, and an APK packaging step in `build.c` using SDK tools (amendment needed: SDK build tools as a toolchain item, D-02).
- Linux: X11 and Wayland (dynamic loading), Vulkan, PipeWire with ALSA fallback.
- macOS/iOS: Cocoa/UIKit via `objc_msgSend` from C, Metal backend, CoreAudio.
- Shared shader language: one source producing HLSL, WGSL, MSL and SPIR-V (own compiler); the hand-written per-backend shaders are replaced only after golden parity.

### Phase 4 — 3D. Phase 5 — Consoles.
Planned when reached.

---

## 8. Process for agents

### Task lifecycle
1. **Pick:** the first unchecked, unclaimed task in the §0 tracker whose dependencies are checked. If the previous session left a task claimed but unfinished, resume it at its first unchecked step.
2. **Claim:** append ` — claimed YYYY-MM-DD` to its tracker line.
3. **Read:** this task's section, the cited SPEC sections, and the READMEs of touched modules.
4. **Implement:** only files listed in the task, plus tests. Check each step box in the task section as it is completed. Anything outside the task becomes a new task proposal in `docs/progress.md`.
5. **Verify:** run every acceptance check and check its box only after seeing it pass, plus `build test` (R6).
6. **Record:** check the task in the §0 tracker (` — done YYYY-MM-DD`), check parent milestone and phase boxes if all their children are now done, append the session summary (format in `AGENTS.md`) to `docs/progress.md`, and update module READMEs.
7. **Escalate:** spec conflicts or decision changes go to the owner (R8). Do not resolve them silently.

`docs/progress.md` is the append-only history of session summaries and task proposals. Status lives only in the §0 tracker.

### Definition of done (every task)
- The acceptance criteria are met and verified with real output.
- `build test` passes (all unit, golden, lint and perf tests).
- No new compiler warnings (they are errors anyway, D-09).
- Public API changes have contract comments, and `API.md` has been regenerated.
- The module README is current.
- Determinism holds: golden hashes are unchanged, or the update is explicitly justified (R5).

### Session sizing
A task that will not fit in one session is split at its listed sub-task boundaries (a/b/c). Each sub-task must leave `build test` green.

---

## 9. Proposed spec amendments (owner approval needed, O-3)

Writing this plan exposed gaps in `SPEC.md`. Agents MUST NOT apply these until approved (R8).

- **A-01** Host tools (`build.c`, test-only checks such as the vtable-slot test) MAY use the CRT and SDK headers. They never ship. D-03 applies to the runtime only.
- **A-02** Add a **null GPU backend** and a `--no-gpu` flag for simulation-only runs and fast tests.
- **A-03** Sanitizer strategy is decided by spike P0-05: either a `win64 sanitize` configuration that links the CRT, or no ASan with UBSan trap mode plus guard pages. This replaces the D-09 sanitizer clause.
- **A-04** The memory budget per arena is declared per game in `bmf_game_desc` (with defaults), since wasm memory must be sized at build time.
- **A-05** Add `src/core/` (engine loop, context, game loading) to the SPEC §3 layout.
- **A-06** Add **sleeping and islands** to physics2d (SPEC §14.1). They are needed for pile performance.
- **A-07** Add `avrt.dll` (audio thread priority) to the D-02 system DLL list. Later, platform SDK tools for Android packaging.
- **A-08** Golden images are kept **per backend** (`tests/golden/d3d11/`, `tests/golden/webgpu/`), because WARP and browser rasterizers differ slightly in antialiasing. A cross-backend comparison at looser tolerance checks shader parity. This refines SPEC §15 point 3.
- **A-09** D-09 adds an explicit ban on `-mrelaxed-simd` and on any `-ffast-math` sub-flag in every configuration.
- **A-10** Web tests report results from the page to the dev server (no DevTools-protocol client). This clarifies D-04.
- **A-11** (added after Phase 0, spike P0-06) D-09 adds `-fno-math-errno` to every configuration. The MSVC target defaults to `-fmath-errno`, which lets clang call a libm `sqrtf` instead of emitting the instruction.
- **A-12** (added after Phase 0, spike P0-05) `build.c` is CRT-free like the runtime, and is bootstrapped by a one-line `bootstrap.bat` that finds the newest Windows SDK and calls clang. Reason: clang 23 does not detect the VS 18 install, so the D-12 command `clang build.c -o build.exe` cannot find `stdio.h`; a CRT-free `build.c` needs only the SDK import libraries. If approved, A-01 still covers test-only host checks (vtable-slot test) and the `sanitize` configuration, which need MSVC discovery.
- **A-13** (added after Phase 0, spike P0-07) Add `ws2_32.dll` (Winsock, used by the dev server) to the D-02 system DLL list.

---

## 10. Risk register

| ID | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| K-01 | CRT-free toolchain friction (missing builtins, stack probes, DLL entry) | High | Medium | P0-03 and P0-04 spikes; a builtins file per target. |
| K-02 | ASan unusable without the CRT | High | Low | P0-05; fallback to UBSan trap mode plus guard pages and canaries. |
| K-03 | Cross-platform float determinism fails (relaxed SIMD, FMA, compiler differences) | Medium | **Critical** | P0-06 before any design work; lint (1A-41); hash gate (1B-46); escalate to the owner if it fails. |
| K-04 | WebGPU unavailable in headless CI browsers without a GPU | Medium | Medium | P0-07 tests the SwiftShader adapter; fallback is a manual web golden run on a machine with a GPU. |
| K-05 | WARP and WebGPU rasterization differences make shared goldens flaky | High | Low | A-08 per-backend goldens. |
| K-06 | Physics solver quality (stacking, jitter, tunneling) takes longer than estimated | Medium | High | Scenario suite defined first (1B-06 acceptance); physics lane starts right after M1; most time budget goes here. |
| K-07 | Agent context drift across a ~45k-LOC codebase (inconsistent conventions) | High | Medium | Module READMEs, `API.md`, one-way-only rule, review agent at each milestone. |
| K-08 | Windows Defender or SmartScreen flags small unsigned CRT-free executables | Medium | Medium | Checked in P0-03; code signing decision O-5. |
| K-09 | Hand-declared COM vtables wrong (silent crashes) | Medium | High | 1A-14 vtable-slot test against SDK headers. |
| K-10 | Web audio latency or glitches on mobile | Medium | Low | AudioWorklet + SharedArrayBuffer upgrade; underrun counter test. |
| K-11 | WebGPU coverage on mobile browsers is smaller than expected (D-34 rules out WebGL2) | Low–Medium | Medium | Measure at 1B-41 on target phones; report to the owner. |
| K-12 | Compiler upgrade changes float codegen | Low | High | Version pin (P0-01); upgrades are determinism events (D-37 rule 14). |

---

## 11. Estimates

Rough Phase 1 runtime plus tools: about **45–50k LOC** (base 6k, platform win32 4k, GPU 5k, draw2d and font 3k, core and input 2k, debug 5k, physics 10k, audio 3k, assets and baker 4k, web 7k, build and test tools 3k), plus the port game.

At a typical 500–1500 LOC per agent session including tests, Phase 1 is roughly **60–90 agent sessions**. With 3–4 parallel lanes after M1, wall-clock time is set by the critical path (M0 to M5 to M9 to M10, about 35–45 sessions).
