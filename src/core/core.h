// core.h: the game model and the engine loop (SPEC §8, D-20, D-21, D-32, D-39; task 1A-23).
//
// A game is a C module that exports bmf_game(), which returns a bmf_game_desc: its state size,
// tick rate, reflection fields, input actions and three callbacks (init, tick, draw). The engine
// owns everything else: memory, the fixed-step loop, input sampling, rendering and, in native
// debug builds, hot reload of the game DLL (SPEC §8.5).
//
// The game interface (bmf_game_desc, bmf_ctx, bmf_field, bmf_sfx_play, BMF_API_VERSION and the
// game limits) is public in bmf.h. This header is the engine interface, bmf_core_*: argument
// parsing, desc validation, start, the fixed-step clock, ticking, drawing, the frame loop and
// the game swap for hot reload.
//
// Thread rule for every function: main thread only.
#ifndef BMF_CORE_H
#define BMF_CORE_H

#include "../bmf.h"
#include "../base/base_memory.h"
#include "../draw2d/draw2d.h"
#include "../input/input.h"
#include "../debug/reflect.h"

enum {
    BMF_CORE_MAX_STEPS_PER_FRAME = 4,        // SPEC §8.3 max_steps_per_frame
    BMF_CORE_ERROR_MAX = 512,                // error buffers: one line, NUL-terminated
    BMF_CORE_DEFAULT_WIDTH = 1280,           // window client size / headless target, logical pixels
    BMF_CORE_DEFAULT_HEIGHT = 720,
};

// Command-line options (SPEC §11.1). Zero is the default for every field except time_scale.
typedef struct bmf_core_options {
    b32 headless;              // --headless: no window; WARP (or null) offscreen target; ticks as fast as possible
    b32 control;               // --control: control protocol on stdin/stdout (src/debug/protocol.h); starts paused
    b32 no_gpu;                // --no-gpu: null GPU backend (D-38)
    const char* replay_path;   // --replay FILE (task 1A-33), or 0
    b32 verify;                // --verify (with --replay)
    b32 profile;               // --profile (with --replay, task 1A-42): when the replay ends, print the
                               // profile report of the whole run as one {"event":"profile",...} line
    u64 seed;                  // --seed N (0..2^63-1); default 0
    i32 tick_rate;             // --tick-rate N (30..240) overrides the game's; 0 = the game's
    f32 time_scale;            // --scale X (0.01..100): real-time speed of the windowed loop; default 1
    i32 width, height;         // --size WxH (16..8192 each): the headless target and the initial window
                               // client size; 0 = BMF_CORE_DEFAULT_WIDTH x BMF_CORE_DEFAULT_HEIGHT
    i64 state_reserve;         // address space for the state block (hot reload may grow the state
                               // up to this); 0 = state_size. Not a flag: set by the runtime entry
} bmf_core_options;

// Parses the process arguments values[1..count) into *out (defaults first). Unknown flags,
// missing or malformed values and out-of-range numbers return BMF_ERR_INVALID with a one-line
// message in error[0..error_capacity). Flags may come in any order; a repeated flag takes the
// last value. O(arguments).
bmf_err bmf_core_parse_args(i32 count, const char* const* values, bmf_core_options* out, char* error, i32 error_capacity);

// Checks a game description against every rule of bmf_game_desc and bmf_field (api version,
// name, state size, tick rate, memory sizes, callbacks, the field table: names, uniqueness,
// types, counts, bounds, alignment, count fields) and the action table (bmf_input_init's rules,
// without changing the input module). Returns BMF_OK or BMF_ERR_INVALID with a one-line message
// naming the broken rule (and the field or action) in error[0..error_capacity). O(fields^2 + actions^2).
bmf_err bmf_core_check_game(const bmf_game_desc* game, char* error, i32 error_capacity);

// Starts a game: validates it (bmf_core_check_game), reserves memory (bmf_memory_init with the
// game's budget and options->state_reserve), copies the action table and starts input with it,
// allocates the zeroed state block (64-byte aligned), and calls init(cx, state, seed). The
// renderer is not touched (see bmf_core_display_open). On failure returns BMF_ERR_INVALID (bad
// game) or BMF_ERR_IO (the OS refused the address-space reservation) with a message, and nothing
// is running. Calling it again restarts from scratch (tests). O(state_size).
bmf_err bmf_core_start(const bmf_game_desc* game, const bmf_core_options* options, char* error, i32 error_capacity);

// Ends the running game: forgets it (memory stays reserved; the process is about to end or a
// test starts the next one). O(1).
void bmf_core_stop(void);

// The running game's context and state block (tests, debug tools). O(1).
bmf_ctx* bmf_core_ctx(void);
void* bmf_core_state(void);
// The running game's description (a copy owned by the engine; its pointers point into the game's
// code, valid until the next reload). O(1).
const bmf_game_desc* bmf_core_game(void);

// The running game's memory (arenas; debug tools borrow the permanent arena with mark/pop). O(1).
bmf_memory* bmf_core_memory(void);

// 1 between bmf_core_start and bmf_core_stop. Any thread (crash reports). O(1).
b32 bmf_core_running(void);

// What the engine is doing right now: "init", "tick", "draw" (inside the game's callbacks) or
// "none". Any thread (crash reports). O(1).
const char* bmf_core_phase_name(void);

// Hash of the simulation: bmf_hasher (seed 0) over the tick count (8 bytes, little-endian) and then
// the whole state block. Replays checkpoint it; the protocol's `hash` reports it (src/debug/snap.h).
// Debug builds assert (D-37 rule 11) when a reflected f32/v2/v3/v4 element is NaN or Inf; the
// message names the first such path. O(state).
u64 bmf_core_state_hash(void);

// Replaces the state block with `state` (state_size bytes) and the tick count with `tick`
// (replays, snapshots). The real-time accumulator restarts. Recordings are not told: the caller
// reports the write (bmf_replay_note_state_write). O(state).
void bmf_core_load_state(const void* state, i64 tick);

// Id of the running build: the BMF_BUILD_ID build.c compiles in (a hash of the runtime's sources);
// in debug DLL builds combined with the loaded game DLL's own id (bmf_game_build_id). 0 when the
// binary was not built by build.c. O(1).
u64 bmf_core_build_id(void);

// Reflection over the running game's state block: its field table, the state, and the built-in
// `tick` path (ticks completed). Valid until the next start, stop or game swap. O(1).
bmf_reflect bmf_core_reflect(void);

// Runs exactly `count` ticks now: per tick, sample input (bmf_input_sample_tick), call tick,
// then deliver the tick's events, then the protocol's tick hook (bmf_protocol_after_tick, debug
// and BMF_AGENT builds). Used by headless runs and the control protocol (`step`).
// Returns `count`. O(count x tick cost).
i32 bmf_core_step(i32 count);

// The fixed-step loop for real time (SPEC §8.3): adds `elapsed_ns` (clamped to 0..1 s, then
// multiplied by the time scale) to the accumulator and runs ticks while a whole tick is
// accumulated, at most BMF_CORE_MAX_STEPS_PER_FRAME. When that limit is hit with a tick or more
// still owed, the owed whole ticks are dropped (spiral-of-death protection, counted in
// bmf_core_stats.ticks_dropped) and only the fraction is kept. The accumulator is exact integer
// arithmetic (ns x tick_rate), so 60 frames of 1/60 s give exactly 60 ticks. Returns the ticks run.
i32 bmf_core_advance(i64 elapsed_ns);

// Interpolation factor for draw: accumulated time since the last tick in ticks, in [0, 1). O(1).
f32 bmf_core_alpha(void);

// Calls the game's draw(cx, state, alpha) between bmf_draw2d_begin_frame and the flush of the
// caller (bmf_core_frame does both). Sets cx->view_size from the current view first. O(draw).
void bmf_core_draw(f32 alpha);

// The view (render target size and DPI) for drawing and pointer mapping: updates draw2d's view
// for the next begin_frame, input's view and cx->view_size. O(1).
void bmf_core_set_view(bmf_draw2d_view view);
bmf_draw2d_view bmf_core_view(void);

typedef enum bmf_core_swap_result {
    BMF_CORE_SWAP_KEPT = 0,       // same state_size and state_layout_hash: the state block is kept
    BMF_CORE_SWAP_RESET = 1,      // layout changed: state zeroed, init called again with the same seed, tick 0
    BMF_CORE_SWAP_REJECTED = 2,   // the new game is invalid or its state does not fit the reserve:
                                  // the old game keeps running (message in error)
} bmf_core_swap_result;

// Replaces the running game's description with a newly loaded one (hot reload, SPEC §8.5). The
// action table is copied; when it differs from the old one, input restarts with it (rebinding is
// reset), otherwise input keeps its state. The tick rate follows the new game unless --tick-rate
// was given. O(state_size) on reset.
bmf_core_swap_result bmf_core_swap_game(const bmf_game_desc* game, char* error, i32 error_capacity);

typedef struct bmf_core_stats {
    i64 ticks;            // ticks run since start (not reset by a reload reset)
    i64 frames;           // frames drawn (bmf_core_frame)
    i64 ticks_dropped;    // ticks skipped by the spiral-of-death protection
    i64 sfx_played;       // bmf_sfx_play events delivered to the (stub) audio
    i64 sfx_dropped;      // events dropped: more than BMF_CORE_MAX_SFX_PER_TICK in one tick
    i32 reloads;          // successful game swaps (kept + reset)
    i32 resets;           // swaps that reset the state
} bmf_core_stats;

bmf_core_stats bmf_core_get_stats(void);

// ---------------------------------------------------------------- the run loop (runtime exe, web)
// Opens the display for the run: the window (unless headless) titled with the game name at
// BMF_CORE_DEFAULT_WIDTH x HEIGHT logical pixels, the GPU (D3D11 with the window, D3D11 WARP
// offscreen when headless, the null backend with --no-gpu or when D3D11 is unavailable) and
// draw2d. Prints nothing; returns BMF_ERR_IO with a message when the window cannot open. Needs
// bmf_core_start first.
bmf_err bmf_core_display_open(char* error, i32 error_capacity);

// Releases what bmf_core_display_open made. O(GPU objects).
void bmf_core_display_close(void);

// One frame of the run loop: polls the game DLL for hot reload (DLL builds), applies a pending
// resize, starts the draw2d and GPU frame (D3D11 waits on the swapchain's frame-latency object
// here), pumps platform events into input, advances the fixed-step clock (windowed) or runs one
// tick (headless), draws and presents, and resets the frame arena. While the window is minimized
// it ticks nothing and sleeps 16 ms; on restore the clock restarts (no catch-up). An occluded
// window (covered, locked session) keeps ticking in real time but sleeps 16 ms per frame, since
// the swapchain does not pace it then.
// With --control (debug and BMF_AGENT builds) the frame first executes the queued protocol
// requests (bmf_protocol_poll; headless and paused, it blocks until a request arrives) and ticks
// only while the protocol is running (`run`, real time also when headless, with a 1 ms sleep per
// headless frame); paused, `step` is the only way time advances.
// Returns 0 when the run should end (the window was closed, or quit was requested). O(ticks + draw).
b32 bmf_core_frame(void);

// Renders the current state once, outside bmf_core_frame, into a width x height offscreen target
// (the headless target when that is the size of the current view, else a temporary one), and
// reads it back into `pixels` (width * height * 4 bytes, RGBA, rows top to bottom). The view the
// game's draw sees is the current one when width x height is its size, else width x height at DPI
// scale 1. A GPU frame of its own: presents nothing, changes no state; ctx.view_size is restored.
// Returns BMF_ERR_UNSUPPORTED on the null backend. Needs bmf_core_display_open; not inside a
// frame. O(draw + pixels).
bmf_err bmf_core_capture(i32 width, i32 height, u8* pixels, i64 capacity);

// 1 between bmf_core_display_open and bmf_core_display_close. O(1).
b32 bmf_core_display_is_open(void);

// Makes the next bmf_core_frame return 0 (protocol `quit`, end of stdin). O(1).
void bmf_core_request_quit(void);

// 1 after bmf_core_request_quit or a closed window, until the next bmf_core_display_open. O(1).
b32 bmf_core_quit_requested(void);

// The whole run of the runtime exe after the game is resolved: an error line for options this
// build cannot serve (--replay until task 1A-33; --control in a release build without
// BMF_AGENT: not_supported), bmf_core_start, bmf_core_display_open, the start line
//   {"event":"start","game":NAME,"tick_rate":N,"seed":N,"mode":"windowed"|"headless","gpu":"d3d11"|"null","link":"dll"|"static"}
// then (with --control) bmf_protocol_start, then bmf_core_frame until it returns 0, then
// {"event":"quit","tick":T,"frames":F}. Failures
// print {"event":"error",...} (bmf_core_print_error). Returns the process exit code: 0 after a
// normal quit, 1 after an error.
i32 bmf_core_run(const bmf_game_desc* game, const bmf_core_options* options);

// Writes {"event":"error","error":CODE,"detail":DETAIL} as one line to stdout (DETAIL JSON-escaped).
// Codes: bad_args, game_missing, invalid_game, out_of_memory, window_failed, not_implemented,
// not_supported. O(length).
void bmf_core_print_error(const char* code, const char* detail);

#if defined(BMF_GAME_LINK_DLL)
// ---------------------------------------------------------------- game DLL (native debug, D-13, SPEC §8.5)
// Copies game.dll (next to the exe) to game_0.dll, loads the copy (so the linker can overwrite
// game.dll while it runs), calls its bmf_game() and starts watching game.dll. Returns the game,
// or 0 with a message (DLL missing or not loadable, no bmf_game symbol, bmf_game returned 0).
// bmf_core_frame then polls game.dll: a change that has been stable for one poll interval
// (BMF_FILE_WATCH_INTERVAL_MS) is copied to the other of game_0.dll / game_1.dll, loaded and
// swapped in (bmf_core_swap_game), the old copy is unloaded, and one line is printed:
//   {"event":"reload","state":"kept"|"reset","tick":T,"ms":M}      (M: from seeing the change to done)
//   {"event":"reload","state":"rejected","detail":MESSAGE}        (the old game keeps running)
// A copy that cannot be loaded yet (the linker still writing) is retried at the next change.
const bmf_game_desc* bmf_core_dll_open(char* error, i32 error_capacity);

// Reloads game.dll now, as a detected change would (protocol `reload`), and prints the same
// reload line. Returns the swap result; REJECTED with a message in error[0..error_capacity) when
// the DLL cannot be loaded or the game is refused (the old game keeps running). O(DLL size).
bmf_core_swap_result bmf_core_dll_reload(char* error, i32 error_capacity);
#endif

#endif // BMF_CORE_H
