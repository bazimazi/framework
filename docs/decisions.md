# Decision log

Append-only. Format: `D-xx | date | decision | reason | supersedes`.
Full text of each decision is in `SPEC.md`. Changes are made only by the owner (R8).

D-01 | 2026-11-18 | Runtime contains zero third-party code; algorithms from papers and specs may be implemented from scratch | Control, size, determinism | -
D-02 | 2026-11-18 | Allowed externals: clang/LLVM (lld, compiler-rt builtins, freestanding and intrinsic headers), listed system DLLs, browser APIs, Windows SDK import libs | These are system/toolchain, not dependencies | -
D-03 | 2026-11-18 | Runtime does not use libc/CRT; own memcpy/memset, math, strings, formatting | Binary size, startup, bit-identical math, wasm without Emscripten | -
D-04 | 2026-11-18 | Dev tooling follows the same rule, except web tests may drive a local headless Chromium browser | Web testing needs a browser | -
D-05 | 2026-11-18 | No Emscripten, CMake, Make, Ninja, Node.js or Python; the build is a C program | Zero dependencies | -
D-06 | 2026-11-18 | Language is C11 plus a small listed set of clang extensions (ext_vector_type, builtins, attributes) | Most training data, simplest semantics, lowest agent error rate | -
D-07 | 2026-11-18 | Compiler is clang only, minimum LLVM 19, one exact version pinned in CI | One set of warnings, codegen and builtins on every target | -
D-08 | 2026-11-18 | Unity build: one translation unit per binary (src/bmf.c includes every runtime .c) | Build speed (<2 s debug rebuild), whole-program optimization | -
D-09 | 2026-11-18 | Mandatory flag set for all builds (strict warnings, -ffp-contract=off, -fno-fast-math, freestanding), per-config additions | Correctness and determinism | -
D-10 | 2026-11-18 | Strict aliasing stays on; type punning only via union or bmf_memcpy | Performance | -
D-11 | 2026-11-18 | A game lives in games/<name>/ with game.c, assets/, tests/, perf.budget | One canonical layout | -
D-12 | 2026-11-18 | build.c is a C program that invokes clang; machine-parsable output; nonzero exit on failure; no downloads | Zero dependencies, agent operability | -
D-13 | 2026-11-18 | Debug native builds load the game as game.dll (hot reload); release and wasm link statically | Iteration speed without release cost | -
D-14 | 2026-11-18 | D3D11 shader bytecode is produced at build time via d3dcompiler_47.dll; runtime never compiles shaders | Startup time, no runtime compiler dependency | -
D-15 | 2026-11-18 | Counts, sizes and indices are i32; bits, hashes and handles are u32/u64; sizes > 2 GiB are i64 | Avoid unsigned wraparound bugs | -
D-16 | 2026-11-18 | No general-purpose heap; one reserved range carved into arenas (permanent, game_state, level, frame, scratch[2]) plus fixed pools | Performance, predictability, snapshotting | -
D-17 | 2026-11-18 | Own transcendental math with documented ULP error; deterministic tier for simulation, bmf_fast_* tier for render/audio only | Bit-identical results across targets | -
D-18 | 2026-11-18 | Determinism is a hard requirement across runs and across every shipped target; CI checks all targets | Replays, golden tests, agent debugging | -
D-19 | 2026-11-18 | World y-up meters; screen y-down logical pixels; radians CCW; colors 0xRRGGBBAA sRGB premultiplied; sim time in ticks | One convention set | -
D-20 | 2026-11-18 | A game is a C module exporting one symbol bmf_game() returning bmf_game_desc | Minimal, explicit game model | -
D-21 | 2026-11-18 | All simulation state lives in one contiguous, pointer-free block owned by the engine | Snapshot, rewind, hash, hot reload, replay are all memcpy/hash | -
D-22 | 2026-11-18 | Games declare a reflection field table (name, type, offset, count, count field) | Agent get/set/watch/dump | -
D-23 | 2026-11-18 | No ECS framework; games use fixed-capacity SoA arrays in state, bmf_pool for stable handles | Fastest layout, trivially reflectable | -
D-24 | 2026-11-18 | Simulation sees only actions via a fixed-size POD bmf_input sampled per tick, never devices | Replays independent of bindings and devices | -
D-25 | 2026-11-18 | GPU layer is a small explicit WebGPU-shaped API with u32 handles and no state machine | Maps onto all modern backends | -
D-26 | 2026-11-18 | Phase 1 GPU backends are D3D11 and WebGPU; Vulkan and Metal in Phase 3 | Robustness, WARP headless, least code | -
D-27 | 2026-11-18 | Shaders hand-written per backend (HLSL + WGSL); parity enforced by golden images | Shared shader language deferred to Phase 3 | -
D-28 | 2026-11-18 | Web target is WebGPU only; no WebGL2 backend in any phase | One web backend | -
D-29 | 2026-11-18 | Job API is identical on all platforms; on web Phase 1 jobs run inline | Determinism and one code path | -
D-30 | 2026-11-18 | Physics world is embedded inside the game state block with game-chosen fixed capacities | Follows D-21 | -
D-31 | 2026-11-18 | Every change must pass build test plus every game's golden tests; agents run them | Verification without eyes | -
D-32 | 2026-11-18 | Default tick rate 60 Hz; any rate 30..240 must work; changing it is one line per game | Was Q-02 | -
D-33 | 2026-11-18 | Name is bmf (bazimazi framework); prefixes bmf_/BMF_; replay extension .bmfr | Was Q-01 | -
D-34 | 2026-11-18 | Web graphics are WebGPU only (see D-28) | Was Q-03 | -
D-35 | 2026-11-18 | Android does not move ahead of the API freeze; stays in Phase 3 | Was Q-04 | -
D-36 | 2026-11-18 | Cross-platform determinism is a hard requirement enforced in CI (see D-18) | Was Q-05 | -
D-37 | 2026-11-18 | Simulation number type is IEEE-754 f32 with strict rules (no FMA, no rcp/rsqrt, no FTZ/DAZ, no NaN/Inf in state, saturating float-to-int), not fixed-point | Performance, SIMD, 3D future; basic IEEE ops are bit-identical | -

## 2026-11-19: Phase 0 amendments A-01…A-13 (owner-approved, O-3)

D-02 | 2026-11-19 | Add avrt.dll (audio thread priority) and ws2_32.dll (dev server only) to the allowed system DLLs; Android SDK tools later (A-07, A-13) | Needed by WASAPI backend and dev server | D-02 (2026-11-18)
D-04 | 2026-11-19 | Tools are CRT-free like the runtime; web tests launch the browser with flags+URL and the page reports to the dev server (no DevTools client); test-only host checks needing SDK/MSVC headers and the sanitize config MAY use the CRT and are skipped loudly when MSVC is absent (A-01, A-10, A-12) | Spikes P0-05, P0-07: clang does not detect VS 18; CRT-free tools need only the SDK | D-04 (2026-11-18)
D-09 | 2026-11-19 | Add -fno-math-errno; debug uses UBSan trap mode (no runtime); new win64-only sanitize config with ASan + CRT; release links /include:_fltused; ban -mrelaxed-simd, all -ffast-math sub-flags and -ffp-contract other than off; no __int128/long double (A-03, A-09, A-11) | Spikes P0-03, P0-04, P0-05, P0-06 | D-09 (2026-11-18)
D-12 | 2026-11-19 | build.c is CRT-free and is built by bootstrap.bat (finds clang and the newest Windows SDK) instead of a plain `clang build.c` (A-12) | clang 23 does not detect the VS 18 install | D-12 (2026-11-18)
D-38 | 2026-11-19 | Null GPU backend on every platform; --no-gpu selects it for simulation-only runs (A-02) | Fast tests without a device | -
D-39 | 2026-11-19 | Per-game arena budget in bmf_game_desc.memory, zero fields = defaults (A-04) | wasm memory is sized at build time | -
D-40 | 2026-11-19 | physics2d has deterministic islands and sleeping, state in the block (A-06) | Pile performance budgets | -
D-41 | 2026-11-19 | Golden images are kept per backend (d3d11/WARP, webgpu/SwiftShader) plus a looser cross-backend parity check (A-08) | Rasterizer AA differences | -
Layout | 2026-11-19 | src/core/ (engine loop, context, game loading) added to the SPEC §3 layout (A-05) | Home for 1A-23 | -
