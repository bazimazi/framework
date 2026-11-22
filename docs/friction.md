# Friction log

Points of friction found while building games or the framework (API gaps, confusing contracts, missing protocol commands, tooling pain).
Append entries; triage happens at the Phase 1 retrospective (1B-52).

Format: `date | who/task | friction | suggested fix`

## 2026-11-22 | 1A-45 exit validation: a fresh agent with only AGENTS.md, API.md and the game folder

Task given: "add a lives counter that decreases when a circle touches the bottom edge, plus a game-over screen; verify through the protocol" (in a copy of games/hello). Result: done without reading framework sources: lives 3, a circle crossing the bottom is removed and costs a life, game over freezes the simulation and shows an overlay, `restart` (R, Enter, Start) begins a new round; verified with `watch lives` (tick 85, 89, 90), `get`, `press`, screenshots and a recorded replay verified with `--replay --verify`; goldens re-recorded with `--update-golden`, `build test` green (179/179, goldens 31/31, perf 7/7). No blocker. The copy now lives in out/exit-validation/hello_lives (not in the repo).

Friction found (severity as rated by the agent) and what was done:

2026-11-22 | 1A-45 agent | MAJOR: the control protocol is not described in the allowed files (AGENTS.md pointed to SPEC §11.2; API.md has no protocol section); syntax found by sending bad commands and reading usage errors | Fixed: AGENTS.md "Quick reference" (flags, every command, paths, tick meaning, events) and a new `help` command listing every usage.
2026-11-22 | 1A-45 agent | MAJOR: the golden `.script` format is undocumented outside tools/test/README.md; no way to assert a field value in a script | Fixed (docs): AGENTS.md quick reference describes scripts, golden names and updates. Open: an `expect PATH VALUE` script line (proposal for 1B or Phase 2).
2026-11-22 | 1A-45 agent | MINOR: PowerShell 5.1 prepends a UTF-8 BOM to piped stdin, so the first request was `no command '\uFEFFhelp'` | Fixed: the protocol skips a leading BOM (tested); AGENTS.md recommends Git Bash `printf`.
2026-11-22 | 1A-45 agent | MINOR: no `help` command, no `--help` flag | Fixed: `help` (protocol). Open: `--help` (a wrong flag already prints the list).
2026-11-22 | 1A-45 agent | MINOR: `bmf_fields_hash` is not in API.md though the template says state_layout_hash must equal it | Fixed: the `state_layout_hash` field comment and AGENTS.md say that the debug build prints the value to declare.
2026-11-22 | 1A-45 agent | MINOR: golden update name had to be inferred (`hello_lives.bounce`) | Fixed (docs): AGENTS.md: `<folder>.<scenario>`.
2026-11-22 | 1A-45 agent | MINOR: `build test hello_lives` silently ran nothing | Fixed: a filter that matches no unit test prints a warning, and skipped golden/perf phases print why and how their tests are named.
2026-11-22 | 1A-45 agent | MINOR: renaming the game invalidates every replay, documented nowhere | Fixed (docs): AGENTS.md lists what a replay is tied to.
2026-11-22 | 1A-45 agent | MINOR: `tick` in watch events: index or ticks completed? | Fixed (docs): AGENTS.md: ticks completed, as `bmf_ctx.tick` in draw.
2026-11-22 | 1A-45 agent | MINOR: `hash` changes while the game is frozen | Fixed (docs): AGENTS.md: the hash covers the tick counter and the state block.
2026-11-22 | 1A-45 agent | MINOR: the template's `HELLO_ACTION_JUMP = 0 // indices into hello_actions; jump is button bit 0` mixes action indices and button bits | Fixed: games/hello uses `HELLO_BUTTON_JUMP` with a comment on how button bits are numbered.
2026-11-22 | 1A-45 agent | MINOR: perf.budget lists replays by hand; new golden replays are not perf-checked until added | Documented in AGENTS.md; kept by design (a benchmark replay is a choice).
2026-11-22 | 1A-45 agent | MINOR: API.md was too big to read in one go (the file reader stopped at line 927 of 1402: 32 133 tokens) | Fixed: the `build api` estimate was bytes / 3.5 (19 004, wrong); now calibrated on that real count (bytes / 2.07), and API.md shrank to about 29 500 tokens (preprocessor blocks and static asserts left out, engine-side declarations behind wrappers left out, alignment spaces collapsed, spec/decision/task references dropped, the hash design moved to src/base/README.md). Still more than one 25 000-token read; open below.
2026-11-22 | 1A-45 agent | MINOR: unclear whether the lint rules apply to draw in a `bmf-lint: sim` file | Fixed (docs): games/hello comment and AGENTS.md: the whole file is linted; int-to-float casts are fine.
2026-11-22 | 1A-45 agent | NOTE: wanted to diff against games/hello but that was outside the allowed files | None needed.

Open for the owner (spec level, R8):
- SPEC §17's 30k-token target for API.md: at the calibrated ratio API.md is at 29 500 with 1.7 % headroom, and an agent's file reader still needs two reads (25 000 per read). Options: raise the target, split API.md by module, or move more rationale out of bmf.h comments.
- A script-level assertion (`expect PATH VALUE`) for goldens, and a `--help` flag.
