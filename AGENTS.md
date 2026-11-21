# AGENTS.md: rules for agents working on bmf (bazimazi framework)

Read this file fully at the start of every session. Architecture and decisions are in `SPEC.md` (cite them as `D-xx`). The public API is in `src/bmf.h` and the generated `API.md`.

Work status lives in the `PLAN.md` §0 progress tracker. Start every session there: resume a claimed, unfinished task at its first unchecked step, or else pick the next available task. Follow the lifecycle in `PLAN.md` §8.

Priority when rules conflict: runtime performance > agent operability > zero dependencies > portability > iteration speed.

## Hard rules (never break)

- R1. No third-party code in the runtime or tools: no vendoring, no pasted snippets. No libc/CRT in the runtime. Allowed: clang toolchain, compiler freestanding/intrinsic headers, OS/GPU/browser system APIs (SPEC §1).
- R2. Simulation code (anything reachable from `tick`) is deterministic: no wall-clock time, no `bmf_fast_*` math, no FMA or approximate reciprocals, no raw float-to-int casts, no NaN/Inf in state, RNG only from state, no address-dependent ordering, no uninitialized reads, no hardcoded tick rate. Results MUST be bit-identical on every target (SPEC §6).
- R3. All simulation state lives in the single game state block, with **no pointers** inside it. Use indices and handles (D-21).
- R4. No heap allocation in the runtime. Use arenas and pools with fixed capacities (D-16).
- R5. Never weaken a test, budget or golden file to make it pass. Golden updates only via `--update-golden`, and you MUST state each one and its reason in your summary.
- R6. Work is not done until `build test` and all game golden tests pass. Run them yourself and report actual results.
- R7. Do not add a new public API without a contract comment (SPEC §16). Breaking `bmf.h` changes bump `BMF_API_VERSION`.
- R8. Do not change a `D-xx` decision on your own. Propose it in your summary; the owner decides and the change is logged in `docs/decisions.md`.

## How to write code here

- Explicit over clever. Duplication is acceptable. Abstraction needs a measured reason (perf or fewer bugs), not elegance.
- Locality: everything needed to understand a function should be in that function or directly above it. Prefer long linear functions over chains of tiny helpers.
- One canonical way per task. If a way already exists, use it; do not add a second one.
- Data layout first: struct-of-arrays for hot data, 64-byte alignment for hot arrays, dense iteration, no per-element indirection.
- No function-like macros except those listed in SPEC §16. Use `static inline` functions.
- Counts and indices are `i32`. Handles, bits and hashes are `u32`/`u64` (D-15).
- Name things fully (`contact_count`, not `cc`). Tokenizers make abbreviations save nothing.
- Hot-path changes need numbers: run `profile` or the bench scenes before and after, and put both in your summary.
- Every module keeps its `README.md` header (invariants, data layout, hot paths, limits) current. Update it in the same change.

## How to verify (you have no eyes, so use these)

- `build win64 debug games/<g>` and then `out/<g>/win64-debug/game.exe --headless --control`.
- Drive the game with the protocol: `step`, `press`, `get`, `dump`, `hash`, `shot`, `profile`, `snap` (SPEC §11.2).
- View screenshots from `shot` when visual correctness matters.
- Reproduce crashes from the `.bmfr` replay printed in the crash JSON line: `game.exe --replay <file>`.
- For web parity: `build wasm release games/<g>` plus the `tools/serve` determinism test.

## Summary format at the end of a task

1. What changed (files, one line each).
2. Test results (pass/fail counts, exact failing names).
3. Perf before/after for touched hot paths.
4. Golden files updated (name plus reason), or "none".
5. Proposed decision changes or open issues, or "none".
