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

## Quick reference: game runs, protocol, test files (enough to build a game from API.md alone)

- Flags: `--headless` (no window, WARP), `--control` (protocol on stdin/stdout, starts paused), `--no-gpu`, `--seed N`, `--tick-rate N` (30..240), `--size WxH`, `--replay FILE [--verify | --profile]`, `--scale X`. A wrong flag prints the list.
- Protocol: one request per line, one JSON response line per request (`{"ok":1,...}` or `{"ok":0,"error":CODE,"detail":...}`; a bad argument count answers with the usage). `help` lists every command with its usage. Pipe requests from Git Bash (`printf 'step 10\nquit\n' | game.exe ...`); end of stdin quits. A leading UTF-8 BOM is ignored.
- Commands: `step N` (run N ticks now), `run`/`pause` (real time), `press ACTION [TICKS]` (held for TICKS ticks, default 1), `axis ACTION X Y [TICKS]`, `pointer I X Y down|up|move [world|screen]` (default world meters), `key NAME down|up`, `get PATH`, `set PATH VALUE...`, `dump [PREFIX]`, `watch PATH`/`unwatch PATH` (prints `{"event":"watch","path":...,"tick":T,"value":...}` when it changes during a step), `hash`, `snap save|load NAME`, `shot PATH.png [W H]`, `record start PATH`/`record stop`, `replay PATH`, `profile N`, `mem`, `log [LEVEL]`, `reload` (debug: hot reload the game DLL), `quit`.
- Paths are field names from the game's field table: `name`, `name[i]`, `name[*]`; `tick` is built in. `tick` in every response and event = ticks completed (the state after them), the same value `bmf_ctx.tick` has in draw. `hash` covers the tick and the whole state block, so it changes every tick even when the game's own fields do not.
- `state_layout_hash`: after changing the state struct or the field table, run the debug build once; it refuses to start and prints the value to declare.
- Replays (`.bmfr`) are valid only for the same game name, state size, layout hash, field count and tick rate; renaming the game (`bmf_game_desc.name`) invalidates them, and a newer build of the same game only warns.
- Golden tests live in `games/<g>/tests/`: `<scenario>.script` holds protocol lines plus `seed N`, `size WxH` (default 320x180) and `golden LABEL` (a WARP shot); tick counts (`step`, `press`, `axis`) are in the game's own ticks and are scaled for the 120 Hz recording. `build test --update-golden <g>.<scenario>` (`<g>` = the folder name) records `<scenario>.<rate>hz.bmfr` at the game's rate and at 120 Hz plus `golden/d3d11/<scenario>.<rate>hz.<LABEL>.png`; `build test` then checks that the replays still match their hashes and the shots their images. Scripts cannot assert field values; check those with the protocol.
- `games/<g>/perf.budget`: `replay=tests/<file>.bmfr` lines (listed by hand: a new replay is not perf-checked until added) and `zone=<tick|draw|draw2d_flush|...> p99_us<=N` limits, checked on a release `BMF_AGENT=1` build.
- `build test [filter]`: a filter selects unit tests by substring and runs the golden phase only if it starts with `golden`/`lint`, the perf phase only if it starts with `perf`; skipped phases say so. A game file with the line `// bmf-lint: sim` is linted as simulation code (the whole file, draw included: no `bmf_fast_*`, no raw float-to-int casts; int-to-float casts are fine).

## Summary format at the end of a task

1. What changed (files, one line each).
2. Test results (pass/fail counts, exact failing names).
3. Perf before/after for touched hot paths.
4. Golden files updated (name plus reason), or "none".
5. Proposed decision changes or open issues, or "none".
