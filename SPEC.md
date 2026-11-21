# bmf (bazimazi framework) — Specification

Status: DRAFT v0.3 (2026-11-19: amendments A-01…A-13 applied after Phase 0, see `docs/decisions.md`)
Name: **bmf**. All public identifiers are prefixed `bmf_` (functions and types) and `BMF_` (macros, enums and constants).
Audience: AI agents that build and use this framework, and the human owner who approves decisions.
Normative words: MUST, MUST NOT, SHOULD, MAY. Every decision has a stable ID (`D-xx`) so code comments and agent notes can cite it.

---

## 0. Purpose

A from-scratch, cross-platform game framework that AI agents use to build games, and that AI agents themselves develop.

Priority order. When two goals conflict, the higher one wins:

1. **Runtime performance** of shipped games (CPU time, memory, binary size, startup, input latency).
2. **Agent operability**: an agent with no eyes, no hands and no memory between sessions can build, run, observe, control, test and debug a game through text and files.
3. **Zero third-party dependencies.**
4. **Portability.**
5. **Iteration speed**: build time, hot reload.

Readability for humans is explicitly **not** a goal. Explicitness, locality and verifiability for agents **are** goals (see `AGENTS.md`).

Non-goals (for now): visual editor, scene graph, general ECS framework, scripting language, networking, 3D (Phase 4), consoles (Phase 5).

---

## 1. Dependency policy

**D-01** Shipped runtime code MUST contain zero third-party code. No vendored libraries, no copy-pasted snippets from libraries, including permissively licensed ones. Algorithms from papers, books and public specifications MAY be implemented from scratch.

**D-02** Allowed external things (system or toolchain, not dependencies):
- Compiler: clang/LLVM, including `lld`, `compiler-rt` builtins and compiler-provided freestanding headers (`stddef.h`, `stdint.h`, `stdbool.h`, `stdalign.h`, `float.h`, `limits.h`) and intrinsic headers (`immintrin.h`, `wasm_simd128.h`, `arm_neon.h`).
- OS APIs and system DLLs: `kernel32`, `user32`, `gdi32`, `d3d11`, `dxgi`, `d3dcompiler_47` (build time only), `ole32`, `mmdevapi` (WASAPI), `avrt` (audio thread priority), `xinput1_4`, `ntdll`, `ws2_32` (dev server only). Later: platform SDK tools for Android packaging (Phase 3).
- Browser APIs: WebGPU, WebAudio, Pointer/Keyboard/Gamepad, `fetch`.
- Windows SDK import libraries, for linking only.

**D-03** The runtime MUST NOT depend on the C runtime (libc/CRT). The framework provides its own `memcpy`, `memset`, math, string and formatting functions. Reasons: binary size, startup time, cross-platform bit-identical math (§6), and wasm without Emscripten.

**D-04** Dev tooling (baker, build program, dev server, test runner) follows the same rule, including D-03 (no CRT), with these exceptions:
- The web test path MAY drive a locally installed Chromium-based browser in headless mode. The browser is launched with flags and a URL only; the test page reports results to the dev server itself (HTTP POST or WebSocket). No DevTools-protocol client is used.
- Test-only host checks that must compile against Windows SDK or MSVC headers (for example the COM vtable-slot test) and the `sanitize` build configuration MAY use the CRT and SDK headers. They never ship. When no MSVC install is found they are skipped with a clear message, never silently.

**D-05** Emscripten, CMake, Make, Ninja, Node.js and Python are NOT used. The build is a C program (§4).

---

## 2. Language and toolchain

**D-06** Language: **C11** (`-std=c11`), plus these clang extensions only:
- `__attribute__((ext_vector_type(N)))`: portable SIMD across x64 SSE, wasm simd128 and NEON with one code path.
- `__builtin_expect`, `__builtin_ctz`/`clz`/`popcount`, `__builtin_assume_aligned`, `__builtin_unreachable`, `__builtin_memcpy`, `__builtin_trap`.
- `__attribute__((aligned(N)))`, `__attribute__((noinline))`, `__attribute__((always_inline))`.

Rationale: C has the most training data and the simplest semantics, so agent error rates are lowest. Zig and C23 have thin training data. C++ and Rust compile slowly and give agents more ways to fail.

**D-07** Compiler: **clang only**, minimum LLVM 19. CI pins one exact version. One compiler gives one set of warnings, one codegen behavior and one set of builtins on every target.

**D-08** Build model: **unity build**. Each binary is exactly one translation unit: `src/bmf.c` `#include`s every runtime `.c` file. Target: a full debug rebuild of runtime plus game in under 2 s.

**D-09** Mandatory flags, all builds:
```
-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow -Wstrict-prototypes
-Wno-unused-parameter -ffp-contract=off -fno-fast-math -fno-math-errno
-ffreestanding -nostdlib -fno-builtin-printf
-fvisibility=hidden -fno-exceptions -fno-asynchronous-unwind-tables
```
- Debug adds `-O0 -g -DBMF_DEBUG=1`, plus `-fsanitize=undefined -fsanitize-trap=undefined` on native targets (trap mode needs no runtime library; a trap is an illegal-instruction crash handled by the crash handler).
- Sanitize (win64 only, never shipped) adds `-O0 -g -DBMF_DEBUG=1 -DBMF_SANITIZE=1 -fsanitize=address,undefined -fsanitize-trap=undefined`. It links the dynamic CRT, enters through the CRT `main`, and copies the ASan DLL next to the exe. Under `BMF_SANITIZE` the runtime does not define its own `memcpy`/`memset`/`memmove`/`memcmp`, `_fltused` or `__chkstk`, and arenas and pools poison and unpoison their memory for ASan. ASan cannot run without CRT startup (spike P0-05).
- Release adds `-O2 -DBMF_DEBUG=0 -flto` (on win64 also the linker flag `/include:_fltused`, which LTO would otherwise drop).
- x64 baseline is `-march=x86-64-v2` (SSE4.2). AVX2 MAY be added later through runtime dispatch for hot kernels only.
- wasm adds `--target=wasm32 -msimd128 -mbulk-memory -mnontrapping-fptoint`.
- Banned in every configuration: `-mrelaxed-simd`, `-ffast-math` and every one of its sub-flags (`-ffinite-math-only`, `-fno-honor-nans`, `-fno-honor-infinities`, `-fassociative-math`, `-freciprocal-math`, `-fno-signed-zeros`, `-fapprox-func`, `-funsafe-math-optimizations`), and any `-ffp-contract` value other than `off`. `build.c` rejects them.
- `-fno-math-errno` is mandatory because the MSVC target defaults to `-fmath-errno`, which lets clang call a libm `sqrtf` instead of emitting the instruction.
- `__int128` and `long double` are not used in the runtime: they need compiler-rt helpers that do not exist for wasm32.

**D-10** Strict aliasing stays ON for performance. Type punning only through `union` or `bmf_memcpy`. UBSan in debug builds catches violations.

---

## 3. Repository layout

```
_framework/
  AGENTS.md            rules for agents (load every session)
  SPEC.md              this file
  API.md               generated compact public-API reference (§17)
  build.c              the build program (§4)
  bootstrap.bat        builds build.exe from build.c (§4)
  src/
    bmf.h               the ONLY public header: whole public API
    bmf.c               unity root: #includes all runtime .c files
    base/              types, memory, math, simd, strings, hash, rng, sort, format
    core/              engine loop, bmf_ctx, game loading and hot reload
    platform/
      win32/           window, events, timing, files, threads, audio (WASAPI)
      web/             wasm side + bmf_web.js (hand-written JS shim, no bundler)
    gpu/               backend-agnostic GPU API
      d3d11/
      webgpu/
    draw2d/            batched instanced 2D renderer, built-in stroke font
    physics2d/         2D rigid body physics
    audio/             mixer + procedural synth
    input/             devices to actions to per-tick bmf_input
    asset/             pak loader, procedural texture generator
    job/               job system
    debug/             control protocol, reflection, profiler, replay, crash dump
  shaders/
    d3d11/*.hlsl       compiled at BUILD time to bytecode headers
    webgpu/*.wgsl      embedded as strings
  tools/
    bake/              asset baker: PNG, WAV, (later TTF) to pak
    serve/             dev HTTP + WebSocket server for web builds (§12.4)
    test/              test runner
  games/
    hello/             minimal sample, golden tests, perf budget
  tests/               framework unit tests, benchmark scenes
  out/                 build outputs (gitignored)
```

**D-11** A game lives in `games/<name>/` with `game.c` (unity root for the game), `assets/`, `tests/` and `perf.budget`. Games outside this repo use the same layout and point the build at the framework path.

---

## 4. Build

**D-12** `build.c` is a CRT-free C program (D-04) compiled by clang that invokes clang. It is bootstrapped by `bootstrap.bat`, which finds clang (same order as `build.c`: `CLANG` env var, `PATH`, `C:\Program Files\LLVM\bin`) and the newest Windows SDK import-library folder, and runs one clang command. A plain `clang build.c` is not enough: clang does not reliably detect Visual Studio installs, and a CRT-free `build.c` needs only the SDK import libraries.
```
bootstrap                         # once: builds build.exe
build win64 debug  games/hello    # outputs out/hello/win64-debug/
build win64 release games/hello
build wasm  release games/hello   # outputs .wasm + bmf_web.js + index.html
build test                        # framework unit tests
build bake  games/hello           # assets to pak + generated assets.h
build shaders                     # HLSL to bytecode headers (via d3dcompiler_47.dll)
```
- Output is machine-parsable. Each diagnostic is one line: `file:line:col: error: msg`, the clang format, passed through unchanged.
- The exit code is nonzero on any failure.
- The build program MUST NOT download anything.

**D-13** Debug native builds compile the game as a DLL (`game.dll`) loaded by the runtime exe, which enables hot reload (§8.5). Release builds and all wasm builds link the game statically into one binary.

**D-14** Shader bytecode for D3D11 is produced at build time by `build.c`, which loads `d3dcompiler_47.dll` (a Windows system component) and calls `D3DCompile`. The output is written as `out/gen/shaders_d3d11.h`. The runtime never compiles shaders.

---

## 5. Base layer (`src/base/`)

### 5.1 Types
```c
typedef signed char i8;  typedef short i16;  typedef int i32;  typedef long long i64;
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned u32; typedef unsigned long long u64;
typedef float f32; typedef double f64; typedef i32 b32;
typedef struct { f32 x, y; } bmf_v2;      // plus v3, v4, m3x2 (2D affine), rect, color
typedef struct { const u8* ptr; i32 len; } bmf_str;   // never NUL-terminated by contract
```
**D-15** Counts, sizes and indices use `i32`, which avoids unsigned-wraparound loop bugs. Bitfields, hashes and handles use `u32`/`u64`. Sizes above 2 GiB use `i64`.

### 5.2 Memory
**D-16** No general-purpose heap in the runtime. At startup the platform reserves one virtual range and the engine carves it into arenas:
- `permanent`: lives for the process.
- `game_state`: the game's single state block (§8.2).
- `level`: reset by the game on demand.
- `frame`: reset every rendered frame.
- `scratch[2]`: temporary use, with two arenas to allow nested scratch.

**D-39** Each game declares its arena budget in `bmf_game_desc.memory` (`level`, `frame` and `scratch` sizes; `game_state` is `state_size`). Zero fields take engine defaults. Reason: wasm linear memory must be sized at build time, and the budget makes memory use explicit and testable.

Rules:
- Arena allocations are 16-byte aligned by default. Hot SoA arrays are 64-byte (cache line) aligned.
- Each arena tracks its high-water mark, which the debug protocol reports.
- `bmf_pool`: a fixed-capacity slot pool with `u32` handles (index:20 | generation:12). Stale handles fail validation.

### 5.3 Math
**D-17** The framework implements all transcendental functions itself (`sin`, `cos`, `atan2`, `exp`, `log`, `pow`, `sqrt` through the IEEE instruction): own polynomial implementations with documented max ULP error, using only `+ - * /` and `sqrt`. This guarantees bit-identical results on x64, wasm and arm64 (§6). There are two tiers:
- `bmf_sinf`, etc.: deterministic, used in simulation.
- `bmf_fast_sinf`, etc.: approximations allowed only in render and audio code.

### 5.4 Other primitives
- `bmf_rng`: PCG32. The state is 16 bytes and lives inside the game state block.
- `bmf_hash`: own 64-bit hash for bytes, plus a streaming hasher for state hashing.
- Sorting: radix sort for `u32`/`u64` keys with payload, used by renderer and physics. Insertion sort for n ≤ 32.
- Formatting: `bmf_fmt` (printf-like, no varargs abuse: `i`, `u`, `f`, `s`, `x`, `v2`), writing into an arena or a fixed buffer.
- Text parsing: `bmf_parse_i32`, `bmf_parse_f32`, and a JSON writer. There is no JSON reader in the runtime. The protocol uses a line format for requests (§11.2).

---

## 6. Determinism

**D-18** Determinism is a hard requirement, **including across platforms** (D-36). With the same game source, the same initial seed and the same input sequence, the game state after N ticks MUST be bit-identical across runs and across every shipped target: win64 and wasm now, and arm64 (Android/Apple) and x64 Linux when they arrive. CI checks all targets. A target that cannot pass this check does not ship.

**D-37** The simulation number type is **IEEE-754 `f32`**, not fixed-point. Reasons:
- Performance: native SIMD on every target, while 64-bit fixed-point multiplies are slow on wasm32 and cannot vectorize well.
- Future-proofing: Phase 4 (3D) and GPU interop are float-native, and fixed-point would force a rewrite there.
- Proven feasible: IEEE basic operations (`+ - * /` and `sqrt`) are correctly rounded and bit-identical on x64 SSE, wasm and arm64, given rules 9–14 below.

Large worlds use origin rebasing per region, not wider types.

Rules for simulation code (anything reachable from `tick`):
1. No wall-clock time. Time is `tick_index * dt` with a constant `dt`.
2. No reads of uninitialized memory. The state block is zeroed at creation.
3. Random numbers only from a `bmf_rng` stored in game state.
4. Only `bmf_` deterministic math. No `bmf_fast_*`. `-ffp-contract=off` is mandatory (D-09).
5. No iteration order that depends on addresses or hash-table layout. Iterate dense arrays in index order. Physics sorts contact pairs by `(min_id, max_id)`.
6. Parallel jobs in simulation MUST write to disjoint, pre-partitioned outputs that are merged in a fixed order. The result MUST NOT depend on thread count or scheduling.
7. SIMD and scalar paths of the same function MUST produce identical bits, or the simulation uses only one of them on all targets.
8. Simulation never reads audio, render or platform state, except the `bmf_input` given to `tick`.
9. No x87, no FMA (contracted or explicit), and no approximate reciprocal instructions (`rcpps`, `rsqrtps`, NEON `frecpe`/`frsqrte`). Their results differ between CPU vendors. Use real `/` and `sqrt`.
10. Denormals are preserved on every thread: FTZ/DAZ is off. The platform layer sets MXCSR (x64) and FPCR (arm64) explicitly at every thread start. Rounding mode is round-to-nearest-even.
11. NaN and Inf MUST NOT exist in the state block. wasm NaN bit patterns are nondeterministic. In debug builds, the state hash pass asserts on any NaN/Inf found in reflected `f32` fields.
12. Float-to-int conversion only through `bmf_f32_to_i32` (defined saturation and truncation). Raw casts of out-of-range values differ between x64 (`INT_MIN`) and wasm/arm64 (saturating).
13. No reduction whose operation order depends on SIMD width, thread count or target. Reductions in simulation use a fixed scalar order or a fixed-shape tree.
14. All targets build with the same pinned clang version (D-07). A compiler upgrade is a determinism event: CI re-verifies every golden replay on every target before the upgrade lands.
15. Nothing may assume a specific tick rate (D-32). Simulation constants are expressed per second and scaled by `dt`. Golden tests for `games/hello` and the physics benchmarks run at both 60 Hz and 120 Hz.

---

## 7. Coordinate and value conventions

**D-19**
- World space: **y-up**, units are **meters**. Physics is tuned for objects 0.05–20 m.
- Screen/UI space: **y-down**, units are **logical pixels** (DPI-scaled). Origin at top-left.
- Every draw call names its space through the active camera. The default camera maps world space so that 1 m = 64 logical pixels, centered.
- Angles are radians, counter-clockwise positive.
- Colors are `u32` `0xRRGGBBAA` in sRGB. Blending uses premultiplied alpha in sRGB space. Linear blending is not used in Phase 1.
- Time is in seconds as `f32` for rendering and audio, and in ticks as `i64` for simulation.

---

## 8. Game model

### 8.1 A game is a C module
**D-20** A game is C code compiled against `bmf.h` that exports exactly one symbol:

```c
typedef struct bmf_game_desc {
    u32  api_version;            // BMF_API_VERSION
    const char* name;
    i32  state_size;             // bytes of the game state block
    u64  state_layout_hash;      // hash of the field table; changes invalidate hot reload
    i32  tick_rate;              // Hz; default 60 (D-32); any value 30..240 must work
    bmf_memory_desc memory;      // arena sizes in bytes; 0 fields = engine defaults (D-39)
    const bmf_field*  fields;  i32 field_count;    // reflection (§8.4)
    const bmf_action* actions; i32 action_count;   // named input actions (§10)
    void (*init)(bmf_ctx* cx, void* state, u64 seed);
    void (*tick)(bmf_ctx* cx, void* state, const bmf_input* in);   // deterministic
    void (*draw)(bmf_ctx* cx, const void* state, f32 alpha);      // read-only state
} bmf_game_desc;

BMF_EXPORT const bmf_game_desc* bmf_game(void);
```

### 8.2 One state block, pointer-free
**D-21** All simulation state (everything that influences future ticks, including the physics world, RNG, warm-starting caches and contact events) lives in **one contiguous block** of `state_size` bytes owned by the engine.
- The block MUST NOT contain pointers. Use indices and handles. The block is position-independent.
- The payoff: snapshot is one `memcpy`, rewind is a `memcpy` back, state hash is one hash pass, hot reload keeps the block, replay verification compares hashes, and a crash dump is the block plus the input log.
- Derived, rebuildable data (broadphase grids, render caches) MAY live in `level` or `frame` arenas, but MUST be rebuilt identically from the state block.

### 8.3 Loop (owned by the engine; games never see it)
```
accumulate real time
while accumulator >= dt and steps < max_steps_per_frame (default 4):
    in = input.sample_tick()           // or from replay
    game.tick(cx, state, &in)
    debug.record(in)                   // replay log
    tick_index++
draw(cx, state, alpha = accumulator/dt)
gpu.present()
```
- `draw` receives interpolation `alpha`. Games that want smooth rendering keep `prev_pos` arrays in state and lerp between them. The engine does NOT copy the whole state for interpolation (cost).
- Audio and visual effects that must not affect the simulation are emitted from `tick` as **events** into `cx` (e.g. `bmf_sfx_play`), which the engine consumes after the tick. They are not part of the state hash.
- Web: the same loop runs inside `requestAnimationFrame`. Native: the loop waits on a DXGI waitable swapchain object.

### 8.4 Reflection (for agents)
**D-22** Games declare a field table that maps names to offsets. The debug protocol uses it for `get`/`set`/`watch`/`dump`:
```c
static const bmf_field fields[] = {
    { "player.pos",  BMF_T_V2,  offsetof(game_state, player_pos), 1, 0 },
    { "ball.pos",    BMF_T_V2,  offsetof(game_state, ball_pos), MAX_BALLS, "ball.count" },
    { "ball.count",  BMF_T_I32, offsetof(game_state, ball_count), 1, 0 },
    { "score",       BMF_T_I32, offsetof(game_state, score), 1, 0 },
};
```
- The fifth column is the name of the field holding the live element count of an array field (or 0).
- The engine provides built-in reflected paths for engine-owned sub-structs embedded in state (`phys.*`, `rng`).
- `state_layout_hash` is computed by `bmf_fields_hash()` at runtime in debug builds and compared to the declared value. A mismatch is a startup error with a clear message.

### 8.5 Hot reload (native debug only)
- The runtime watches `game.dll`. On change it copies the DLL to a temp name (avoids the Windows lock), loads it, and calls `bmf_game()`.
- If `state_size` and `state_layout_hash` are unchanged, the state block is kept. Otherwise the game is re-initialized with the same seed and the protocol reports `{"event":"reload","state":"reset"}`.

### 8.6 No ECS framework
**D-23** The framework does not impose an entity system. Games store entities as fixed-capacity struct-of-arrays inside their state block (`ball_pos[MAX_BALLS]`, `ball_vel[MAX_BALLS]`, `ball_count`), and use `bmf_pool` when stable handles are needed. Reason: direct arrays are the fastest layout, need no indirection, and are trivially reflectable and snapshottable.

---

## 9. Platform layer (`src/platform/`)

Contract every platform implements. These are internal functions, not in `bmf.h`:
- Window: create, resize events, DPI scale, fullscreen toggle, close.
- Events: keyboard (scancodes plus text input), mouse (raw delta and position), touch/pointer (up to 10), gamepad (up to 4), focus, suspend/resume.
- Time: monotonic ns counter, high-resolution sleep/wait.
- Files: read whole file, memory-map read-only, write file atomically (write temp, then rename). Web: fetch-based async load at startup only. Runtime file I/O on web is limited to what was preloaded.
- Threads: spawn N workers, atomics (clang `__atomic_*` builtins), semaphores/futex-wait. Web Phase 1: no threads; jobs run inline (§13).
- Audio device: open a stereo f32 48 kHz output stream with a pull callback.
- GPU surface: hand the native window or canvas to the GPU backend.
- Stdio: line-in and line-out for the control protocol (native).

### 9.1 Win32 specifics
- No `windows.h`. `platform/win32/win32_api.h` declares only the needed functions, structs and constants (compile speed). Each declaration carries a comment with the MSDN name for verification.
- Entry point `bmf_win32_entry` with no CRT (D-03). Provide `_fltused`, the `__chkstk` stack probe, and `memcpy`/`memset`/`memmove`/`memcmp`, as required by clang codegen (spike P0-03).
- Per-monitor DPI v2. Raw Input for mouse. `WM_POINTER` for touch and pen. XInput for gamepads.
- Swapchain: `DXGI_SWAP_EFFECT_FLIP_DISCARD`, frame-latency waitable object, max frame latency 1, tearing allowed when vsync is off.
- `--headless`: no window. D3D11 runs on the **WARP** software device (a system component), rendering to an offscreen texture. Screenshots work on machines without a GPU, and golden images are produced on WARP for reproducibility.

### 9.2 Web specifics
- Built with `clang --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export-dynamic -Wl,--import-memory`.
- `bmf_web.js` is one hand-written ES module, with a target of under 1500 lines and no bundler or minifier dependency. `build.c` MAY do trivial whitespace stripping.
- WASM to JS crossings are minimized. Resource creation uses direct imports. Per-frame rendering writes a compact **command stream** into linear memory, and JS executes it with one `bmf_web_gpu_submit(ptr, len)` call per frame.
- Output: `index.html` + `bmf_web.js` + `game.wasm` + `game.pak`, served as static files.

---

## 10. Input (`src/input/`)

**D-24** Simulation never sees devices, only **actions**. The game declares actions with default bindings:
```c
static const bmf_action actions[] = {
    { "jump",  BMF_ACTION_BUTTON, { BMF_KEY_SPACE, BMF_PAD_A } },
    { "move",  BMF_ACTION_AXIS2,  { BMF_KEYS_WASD, BMF_PAD_LSTICK } },
    { "aim",   BMF_ACTION_POINTER },
};
```
`bmf_input` is a fixed-size POD sampled once per tick:
```c
typedef struct bmf_input {
    u64    down, pressed, released;   // bit per button action (max 64 button actions)
    bmf_v2  axis[8];                   // AXIS2 actions
    bmf_pointer pointer[4];            // world + screen pos, down/pressed/released, id
    u32    text[8]; i32 text_len;     // text input this tick (UTF-32)
} bmf_input;
```
- Replays store `bmf_input` per tick, so they are independent of bindings and devices.
- Edges (`pressed`/`released`) are computed per tick, not per frame, so no press is lost when a frame runs 0 or several ticks.
- Mobile web: touch maps to `pointer[]`. On-screen virtual controls are a game-side `draw2d` helper, not engine magic.

---

## 11. Debug and agent interface (`src/debug/`), the core of agent operability

Compiled only when `BMF_DEBUG=1`, or in release with `BMF_AGENT=1` for automated testing of release performance. Zero cost when off.

### 11.1 Run modes
```
game.exe                         normal windowed
game.exe --headless              no window, WARP offscreen, ticks as fast as possible
game.exe --control               windowed + control protocol on stdin/stdout
game.exe --headless --control    agent mode (most common)
game.exe --replay f.bmfr [--verify]   play replay; --verify checks embedded hashes, exit code
game.exe --seed N --tick-rate N --scale N
game.exe --headless --control --no-gpu   simulation only: null GPU backend (D-38), fastest tests
```
In `--control` mode, time advances **only** on `step` commands. The game is paused by default, so agent experiments are deterministic.

### 11.2 Control protocol
Requests are one line of space-separated tokens (easy for agents to write). Responses are one line of compact JSON (easy for agents to read). Every request gets exactly one response. Asynchronous notifications are JSON lines with an `"event"` key.

| Request | Effect / response |
|---|---|
| `step N` | run N ticks, `{"ok":1,"tick":T}` |
| `run` / `pause` | free-run in real time / stop |
| `press ACTION [TICKS]` | hold a button action for TICKS (default 1) starting at the next tick |
| `axis ACTION X Y [TICKS]` | set an axis action |
| `pointer I X Y down\|up\|move [world\|screen]` | inject pointer |
| `key NAME down\|up` | raw key (tests bindings) |
| `get PATH` | `{"ok":1,"path":"ball.pos[3]","value":[1.5,2.0]}` |
| `set PATH VALUE...` | write a reflected field |
| `dump [PREFIX]` | all reflected fields (respecting live counts) |
| `watch PATH` / `unwatch PATH` | emit `{"event":"watch",...}` each tick when the value changes |
| `hash` | `{"ok":1,"tick":T,"hash":"16 hex"}`: state-block hash |
| `snap save NAME` / `snap load NAME` | in-memory snapshot and restore of the state block plus tick index |
| `shot PATH.png [W H]` | render the current state, write PNG (own encoder, stored deflate) |
| `record start PATH` / `record stop` | write replay |
| `replay PATH` | load replay. Following `step`s consume its inputs |
| `profile N` | run N ticks, return per-zone stats (§11.3) |
| `mem` | arena usage and high-water marks |
| `phys query ...` | ray, point and AABB queries for test assertions |
| `log [LEVEL]` | drain the log ring buffer as a JSON array |
| `reload` | force game DLL reload |
| `quit` | exit 0 |

Errors are reported as `{"ok":0,"error":"unknown_path","detail":"no field 'bal.pos'; did you mean 'ball.pos'?"}`. Error codes are a closed enum, and suggestions are included where cheap.

### 11.3 Profiler
- Zones are a static enum (`BMF_ZONE_PHYS_BROADPHASE`, ...). Games may append their own: `bmf_zone_begin(id)` / `bmf_zone_end(id)` are `static inline` no-ops when off.
- Timer: `rdtsc` on x64, `performance.now` on web. Per-tick ring buffer of 1024 ticks.
- `profile` output per zone: `calls`, `mean_us`, `p50_us`, `p99_us`, `max_us`. Plus `frame_ms` stats, `draw_calls`, `instances`, `bodies`, `contacts`.
- GPU timestamp queries: Phase 2.

### 11.4 Replays and crash dumps
- Replay file `.bmfr`: header (magic, version, build id, game name, seed, tick_rate, state_layout_hash), then one `bmf_input` per tick (delta + RLE compressed with own codec), then optional `(tick, hash)` checkpoints.
- On assert failure, crash or `bmf_panic`, the runtime writes `out/crash/<time>.bmfr` (full input log since start) and `.state` (state block at the last good tick), and prints one JSON line to stderr: `{"event":"crash","reason":"...","file":"...","line":N,"tick":T,"replay":"..."}`. Agents reproduce with `--replay`.

### 11.5 Logging
- `bmf_log(level, fmt, ...)` writes into a ring buffer, and to stderr as JSON lines in `--control` mode.
- Release builds strip `debug`/`info`. `warn`/`error` stay as counters only.

---

## 12. Rendering

### 12.1 GPU layer (`src/gpu/`)
**D-25** A small, explicit, WebGPU-shaped API: buffers, textures, samplers, pipelines (with blend, topology and vertex layout baked in), bind sets and passes. Handles are `u32`. There is no state machine, and every draw states its pipeline and bindings.

**D-38** Every platform also has a **null GPU backend** that accepts every call and renders nothing. `--no-gpu` selects it for simulation-only runs and fast tests; `shot` then returns `{"ok":0,"error":"no_gpu"}`.

**D-26** Phase 1 backends: **D3D11** (Windows: robust on every GPU, WARP for headless, least code) and **WebGPU** (web). Phase 3 adds Vulkan (Linux, Android) and Metal (Apple). The GPU API is designed so that D3D12/Vulkan/Metal map onto it without redesign.

**D-27** Shaders are hand-written **per backend** (HLSL + WGSL), one file each per pipeline, under `shaders/<backend>/<name>`. Duplication is accepted because agents write both. Parity is enforced by golden-image tests on every backend (§15). A shared shader language is deferred to Phase 3, when SPIR-V/MSL are needed.

**D-28** The web target is **WebGPU only**. There is no WebGL2 backend, in any phase (D-34). Browsers without WebGPU get a static "unsupported browser" page from `index.html`.

### 12.2 draw2d (`src/draw2d/`)
- One instance format, 48 bytes: position, size, rotation, uv rect, color, kind, and params (corner radius, stroke width, edge softness).
- Kinds, all rendered by a single pipeline as SDF in the fragment shader: textured sprite, rect (rounded), circle/ring, capsule/line, triangle, and glyph. Circles and lines are pixel-perfect at any zoom, which matters for ball and physics games.
- Each submit records a `u64` sort key: layer(8) | pipeline(4) | texture(12) | depth/order(24) | seq(16). One radix sort per frame. Consecutive instances with equal state merge into one instanced draw.
- Instance data is written into one persistently mapped ring buffer per frame (D3D11: `MAP_WRITE_NO_OVERWRITE`, WebGPU: `writeBuffer` of one contiguous range).
- Cameras: a world camera (y-up meters) and a screen camera (y-down pixels). Cameras form a stack per layer.
- Built-in font: a **stroke font** where glyphs are line segments rendered as capsule SDF instances, covering ASCII 32–126. It is authored in code, needs no asset files and is resolution independent. TTF to MSDF atlas via the baker comes in Phase 2.
- Procedural textures: `bmf_tex_gen_*` (noise, gradients, SDF shape rasterization into an atlas) at load time, plus a runtime atlas packer (skyline).

### 12.3 Performance targets (reference: 4-core x86-64-v2 desktop; mid-range 2023 Android phone in Chrome)
| Metric | Desktop | Mobile web |
|---|---|---|
| 10 000 sprites, CPU submit+sort | < 0.5 ms | < 2 ms |
| Draw calls for a typical 2D scene | < 20 | < 20 |
| Startup to first frame (hello) | < 100 ms | < 300 ms after wasm fetch |
| Release binary (hello) | < 300 KB exe | < 200 KB wasm (br) |
| Input-to-photon added latency | ≤ 1 frame | ≤ 2 frames |

### 12.4 Dev server (`tools/serve/`)
A C program that provides:
- Static file serving with correct MIME types and COOP/COEP headers, ready for threads later.
- A WebSocket endpoint that bridges the §11.2 control protocol to the page, so `bmfctl`-style agent control works for web builds too.
- A file-watch auto-reload.

It is written from scratch, including SHA-1 and base64 for the WebSocket handshake.

---

## 13. Jobs and threading (`src/job/`)

- Fixed worker pool sized `min(cores-1, 7)`, with a lock-free MPMC queue (own implementation) and a parallel-for over index ranges with a fixed chunk size.
- **D-29** The job API is identical on all platforms. On web in Phase 1 (no threads), jobs execute inline on the calling thread. Because of rule 6 in §6, results are identical.
- Main thread: platform events, simulation, render submit. Audio thread: mixer. Workers: physics narrowphase/solver islands, draw2d sorting, procedural generation.

---

## 14. Physics, audio, assets

### 14.1 physics2d (`src/physics2d/`)
**D-30** The physics world is a struct embedded **inside the game state block** (D-21), with capacities fixed by the game: `bmf_phys_world` plus storage arrays sized by `BMF_PHYS_DECL(max_bodies, max_shapes, max_joints, max_contacts)`.
- Bodies: static, kinematic, dynamic, stored SoA. Shapes: circle, capsule, convex polygon (≤ 8 vertices), segment, chain (static only). Sensors. Collision filtering with `category`/`mask` `u32`, plus a group index.
- Solver: substepped soft-constraint solver (TGS-soft family). Defaults are 4 substeps and 1 relax iteration, with restitution and friction per shape pair (mix rules: max/geometric mean). Warm starting is stored in state.
- Broadphase: uniform spatial hash with cell size chosen per world. It is rebuilt each tick (derived data) using radix-sorted cell keys.
- CCD: speculative contacts for all dynamic bodies, plus a TOI sweep for bodies flagged `bullet`. Fast balls MUST NOT tunnel through 0.1 m walls at 100 m/s at 60 Hz.
- **D-40** Sleeping and islands: islands are built deterministically (union-find over contacts and joints in contact-table order); bodies at rest past a sleep timer stop being simulated; contact or explicit API wakes them. Island and sleep state lives in the state block. Needed for pile performance.
- Joints (Phase 1): distance, revolute, weld, mouse/target.
- Events: begin-touch, end-touch, hit (with approach speed above a threshold), and sensor enter/exit. They are written to fixed arrays in state and readable within the same tick.
- Queries: raycast, shape cast, point, and AABB overlap, all deterministic in result order.
- Targets: 2 000 dynamic circles in a pile, tick < 2 ms desktop and < 6 ms mobile web, single thread.

### 14.2 Audio (`src/audio/`)
- Mixer: f32 stereo 48 kHz, fixed voice count (default 64), per-voice gain/pan/pitch, linear-interpolation resampler, and buses (master/music/sfx) with a smoothed gain.
- **Procedural synth**, the first-class sound source for agents: a `bmf_sfx_desc` POD (oscillator waveform, ADSR, pitch slide, vibrato, noise, a simple low-pass/high-pass filter, duty, repeat). Agents author sounds as data in code. `bmf_sfx_bake` renders to a sample buffer at load time.
- Sampled audio: 16-bit PCM from the pak. Streaming music: Phase 2 (own simple ADPCM-style codec; no Vorbis/MP3 decoders).
- Backends: WASAPI shared mode, event-driven, ~10 ms period. Web: AudioWorklet fed by `postMessage` chunks (~40 ms latency), upgraded to a SharedArrayBuffer ring when `crossOriginIsolated`.

### 14.3 Assets (`src/asset/`, `tools/bake/`)
- The baker reads `games/<g>/assets/` and writes `game.pak` plus `out/gen/<g>/assets.h`.
- `assets.h` contains an enum of asset IDs, such as `BMF_ASSET_TEX_PLAYER`. A typo in an asset name is a **compile error**, not a runtime failure.
- Pak format: header, table of contents (id, type, offset, size, params), and 64-byte-aligned blobs in final GPU/mixer-ready format (RGBA8 textures pre-packed into atlases, PCM16 audio). The runtime does no decoding or parsing beyond the table of contents.
- Native: the pak is memory-mapped. Web: it is fetched once into linear memory.
- Baker decoders written from scratch: PNG (inflate, all filter types, 8-bit RGB/RGBA/palette/gray) and WAV (PCM16/float32). Phase 2: TTF (glyf quadratic outlines) to MSDF.

---

## 15. Testing and verification

**D-31** Every change MUST pass `build test` plus every game's golden tests before it is considered done. Agents run these themselves.

1. **Unit tests** (`tests/`): base, math (ULP bounds vs high-precision reference tables), sort, pool, physics primitives, and the PNG round-trip. A plain C runner produces JSON-line results.
2. **Golden replays** (`games/<g>/tests/*.bmfr`): run with `--replay --verify`. Embedded `(tick, hash)` checkpoints must match.
3. **Golden images** (`games/<g>/tests/golden/<backend>/*.png`, D-41): kept **per backend** (`d3d11` rendered on WARP, `webgpu` rendered on SwiftShader), because rasterizers differ slightly in antialiasing. Per-pixel tolerance is ±2 per channel, and the max differing-pixel fraction is 0.1%. A cross-backend comparison at a looser tolerance checks shader parity. A failure writes a diff PNG to `out/test/`.
4. **Cross-platform determinism**: the same replays run in the wasm build (headless Chromium through `tools/serve`) and must produce identical hashes to win64.
5. **Perf budgets** (`games/<g>/perf.budget`): lines like `zone=tick p99_us<=1500`, run against benchmark replays in release `BMF_AGENT=1` builds. An over-budget result fails the test, and the output gives the zone breakdown.
6. **Framework benchmark scenes** (`tests/bench/`): sprites-10k, circles-2k-pile, bullets-1k. Results go to `out/bench/history.jsonl`, so perf regressions are visible over time.

Golden file updates MUST be explicit (`build test --update-golden NAME`) and called out in the change summary. They are never silent.

---

## 16. Public API conventions (`bmf.h`)

- Prefix: functions `bmf_<module>_<verb>[_<noun>]` (`bmf_phys_body_create`, `bmf_draw_circle`), types `bmf_<noun>`, enum values `BMF_<MODULE>_<VALUE>`, constants `BMF_<NAME>`.
- Every public function has a contract comment directly above it: what it does, preconditions, ownership/lifetime, thread rules, cost (O-notation), and whether it is allowed in `tick`.
- Structs are passed by value when ≤ 16 bytes, otherwise by const pointer. Out-params come last.
- Errors: functions that can fail return a `bmf_err` enum and write results through out-params. Programmer errors (bad handle, out of range) are `BMF_ASSERT`s, not error codes.
- No callbacks in the public API except `bmf_game_desc`. No varargs except `bmf_log`/`bmf_fmt`.
- Macros are limited to: `BMF_ASSERT`, `BMF_STATIC_ASSERT`, `BMF_EXPORT`, `BMF_PHYS_DECL`, compile-time constants, and config flags. Everything else is a function or a `static inline` function.
- `BMF_API_VERSION` is bumped on any breaking change to `bmf.h`. Phase 2 ends with an API freeze at v1.

---

## 17. Agent knowledge files

- `AGENTS.md`: the working rules, short. Loaded every session.
- `SPEC.md`: this file, the architecture and decisions. Read the relevant section before changing a module.
- `API.md`: **generated** by `build api` from `bmf.h` (signatures plus contract comments, nothing else). It targets under 30k tokens so it fits in context next to game code. Game-building agents read `API.md`, not the framework sources.
- Each module directory has a `README.md` header block, under 60 lines, listing invariants, data layout, hot paths and known limits. This is the module's "memory".
- `docs/decisions.md`: append-only log. Format: `D-xx | date | decision | reason | supersedes`.

---

## 18. Phases and exit criteria

### Phase 1a: skeleton plus agent loop (Windows)
base, win32 platform, D3D11, draw2d (shapes plus stroke font), input, game DLL plus hot reload, headless/WARP, control protocol, reflection, replay, crash dump, profiler, build.c, test runner.
**Exit:** `games/hello` (bouncing circles without the physics module, plus score text) passes golden replay and image tests. An agent with no prior context can follow `AGENTS.md` and `API.md` to add a feature and verify it through the protocol alone.

### Phase 1b: full 2D stack plus web
physics2d, audio plus synth, baker plus pak, job system, web platform plus WebGPU, dev server, cross-platform determinism tests.
**Exit:** one existing bazimazi game (smallest of the bounce-family titles by LOC) is re-implemented with feature parity. The perf comparison against the TS original is recorded. win64 and wasm hashes match on all golden replays.

### Phase 2: harden through real games
Build at least 3 new games entirely by agents. Collect friction into `docs/friction.md`. Add TTF/MSDF text, streaming music, GPU timestamps and AVX2 hot paths. **Exit:** API v1 freeze.

### Phase 3: platforms
Android (Vulkan + AAudio + NDK `NativeActivity`, no Java beyond the minimal manifest glue required), Linux (Vulkan, X11 + Wayland, PipeWire/ALSA), macOS/iOS (Metal, Cocoa/UIKit through the `objc_msgSend` runtime from C), and a shared shader language. No native platform starts before the Phase 2 API freeze (D-35); until then mobile is reached through the web build. Order within Phase 3 is decided at its start. Each new target MUST pass the cross-platform determinism suite (D-18) before it ships.

### Phase 4: 3D
### Phase 5: consoles (requires NDAs and SDKs; outside zero-dependency scope by nature)

---

## 19. Resolved questions (2026-11-18, owner)

- **D-33** (was Q-01) Name **bmf** (bazimazi framework). Prefixes `bmf_` / `BMF_`. Replay extension `.bmfr`.
- **D-32** (was Q-02) Default tick rate **60 Hz**. Raising it later MUST be a one-line change per game, which is guaranteed by §6 rule 15 and the dual-rate golden tests.
- **D-34** (was Q-03) Web graphics: **WebGPU only** (D-28).
- **D-35** (was Q-04) Android does **not** move ahead of the API freeze. It stays in Phase 3.
- **D-36** (was Q-05) Cross-platform determinism is a **hard requirement** enforced in CI (D-18).
- **D-37** (was Q-06) Simulation uses **`f32` with strict IEEE rules**, not fixed-point (§6).

No open questions at this time. New ones are added here as `Q-xx` and resolved into `D-xx`.
