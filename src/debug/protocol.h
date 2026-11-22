// protocol.h: the control protocol (SPEC §11.2, task 1A-31).
//
// With --control the runtime reads requests from stdin, one per line: space-separated tokens,
// the command name first. Every request gets exactly one response line of compact JSON, in
// request order: {"ok":1,...} or {"ok":0,"error":CODE,"detail":TEXT}. Asynchronous
// notifications are JSON lines with an "event" key (watch changes, reloads); events caused by a
// request (watch changes during `step`) come before its response. Empty lines and lines starting
// with '#' are not requests and get no response.
//
// Commands (one row each in the dispatch table in protocol.c):
//   hello                         {"ok":1,"bmf":API_VERSION,"game":NAME,"tick_rate":R}
//   step N                        runs N ticks now (N >= 0): {"ok":1,"tick":T}
//   run | pause                   free-run in real time / stop: {"ok":1,"tick":T,"running":B}
//   press ACTION [TICKS]          holds a BUTTON action for TICKS ticks (default 1) from the next tick
//   axis ACTION X Y [TICKS]       sets an AXIS2 action to (X, Y) for TICKS ticks (default 1)
//   pointer I X Y down|up|move [world|screen]   injects pointer slot I (0..3) at once; default world
//   key NAME down|up              a raw key event (bmf_key_name spelling), through the bindings
//   get PATH                      {"ok":1,"path":PATH,"value":V}
//   set PATH VALUE...             writes one element: {"ok":1,"path":PATH,"value":V} (the new value)
//   dump [PREFIX]                 {"ok":1,"fields":{PATH:V,...}} (live elements only)
//   watch PATH | unwatch PATH     after every tick whose end changes the value (or its error):
//                                 {"event":"watch","path":PATH,"tick":T,"value":V} (or "error")
//   shot PATH.png [W H]           renders the current state to a PNG (src/debug/shot.h):
//                                 {"ok":1,"path":P,"width":W,"height":H,"bytes":N,"tick":T}
//   record start PATH | record stop   records input to a .bmfr (src/debug/replay.h):
//                                 {"ok":1,"path":P,"tick":T} / {"ok":1,"path":P,"ticks":N,"bytes":B,"checkpoints":K}
//   replay PATH                   loads a replay (its start state and tick); the following steps take
//                                 their input from it and verify its checkpoints:
//                                 {"ok":1,"path":P,"start_tick":S,"end_tick":E,"checkpoints":K}
//   hash                          {"ok":1,"tick":T,"hash":"16 hex"} (src/debug/snap.h); a state with
//                                 NaN/Inf is answered with nonfinite_state and the path instead
//   snap save NAME | snap load NAME   snapshot / restore of the state block and tick (16 names):
//                                 {"ok":1,"name":NAME,"tick":T}
//   reload                        forces a game DLL reload (debug DLL builds): {"ok":1,"state":S}
//   quit                          {"ok":1}, then the runtime ends with exit code 0 (also at stdin EOF)
//   profile N                     a new profiling window, N ticks run now, then the report (src/debug/profile.h):
//                                 {"ok":1,"tick":T,"ticks":N,"zones":{NAME:{"calls":C,"mean_us":..,"p50_us":..,
//                                 "p99_us":..,"max_us":..},...},"frames":F[,"frame_ms":{...}],"draw_calls":D,
//                                 "instances":I,"nesting_errors":E,"counter_hz":H}; N = 0 reports the window so far
//   mem                           {"ok":1,"arenas":[{"name":N,"reserved":R,"committed":C,"used":U,"high_water":H},...]}
//   log [LEVEL]                   drains the log: {"ok":1,"entries":[{"tick":T,"level":L,"msg":M},...],
//                                 "dropped":D,"counts":{"debug":..,"info":..,"warn":..,"error":..}}
//   phys
//                                 {"ok":0,"error":"not_implemented"} until their tasks land
// Time advances only through `step` and `run` in --control mode: the game starts paused.
// T in responses is the number of ticks completed.
//
// Threads: one reader thread blocks on stdin and hands complete lines to the main thread through
// a single-producer single-consumer queue of 64 lines (4 KiB each; longer lines are answered with
// line_too_long). Everything else runs on the main thread, between ticks or from the tick hooks.
#ifndef BMF_PROTOCOL_H
#define BMF_PROTOCOL_H

#include "../bmf.h"
#include "../base/base_string.h"

// The protocol exists in debug builds and release builds with BMF_AGENT=1 (bmf.h).
#define BMF_PROTOCOL_ENABLED BMF_TOOLS_ENABLED

enum {
    BMF_PROTOCOL_LINE_MAX = 4096,       // bytes per request line, including the newline
    BMF_PROTOCOL_QUEUE_LINES = 64,
    BMF_PROTOCOL_MAX_TOKENS = 16,
    BMF_PROTOCOL_MAX_WATCHES = 32,
    BMF_PROTOCOL_RESPONSE_MAX = 1 << 20,   // bytes of one response line; more is response_too_large
};

// The closed set of error codes (SPEC §11.2). bmf_protocol_error_code gives the JSON spelling.
typedef enum bmf_protocol_error {
    BMF_PROTOCOL_OK = 0,
    BMF_PROTOCOL_UNKNOWN_COMMAND,       // "unknown_command"
    BMF_PROTOCOL_BAD_ARGS,              // "bad_args": wrong count or malformed argument (detail has the usage)
    BMF_PROTOCOL_UNKNOWN_ACTION,        // "unknown_action": no action of that name, or of the wrong type
    BMF_PROTOCOL_UNKNOWN_PATH,          // "unknown_path"        (reflection, task 1A-30)
    BMF_PROTOCOL_BAD_PATH,              // "bad_path"
    BMF_PROTOCOL_INDEX_OUT_OF_RANGE,    // "index_out_of_range"
    BMF_PROTOCOL_BAD_VALUE,             // "bad_value"
    BMF_PROTOCOL_READ_ONLY,             // "read_only"
    BMF_PROTOCOL_NOT_WATCHED,           // "not_watched": unwatch of a path that is not watched
    BMF_PROTOCOL_TOO_MANY_WATCHES,      // "too_many_watches"
    BMF_PROTOCOL_RESPONSE_TOO_LARGE,    // "response_too_large": e.g. dump without a prefix of a huge state
    BMF_PROTOCOL_LINE_TOO_LONG,         // "line_too_long"
    BMF_PROTOCOL_NOT_SUPPORTED,         // "not_supported": not possible in this build (reload without a DLL)
    BMF_PROTOCOL_NOT_IMPLEMENTED,       // "not_implemented": a command of a later task
    BMF_PROTOCOL_RELOAD_REJECTED,       // "reload_rejected": the new game DLL was refused; the old one runs
    BMF_PROTOCOL_IO_FAILED,             // "io_failed": a file could not be written (shot, record)
    BMF_PROTOCOL_RECORD_FAILED,         // "record_failed": record start/stop refused (already/not recording, broken recording)
    BMF_PROTOCOL_REPLAY_FAILED,         // "replay_failed": the replay file is missing, invalid or for another game
    BMF_PROTOCOL_UNKNOWN_SNAP,          // "unknown_snap": snap load of a name never saved (or saved with another layout)
    BMF_PROTOCOL_TOO_MANY_SNAPS,        // "too_many_snaps": snap save of a 17th name
    BMF_PROTOCOL_NONFINITE_STATE,       // "nonfinite_state": hash found NaN/Inf in a reflected float (D-37 rule 11)
    BMF_PROTOCOL_ERROR_COUNT,
} bmf_protocol_error;

// JSON spelling of an error code ("ok" for BMF_PROTOCOL_OK). O(1).
const char* bmf_protocol_error_code(bmf_protocol_error error);

// Where response and event lines go: `write(line, length, user)` gets one line without its
// newline. Default (0): stdout, flushed after every line.
typedef void (*bmf_protocol_output_fn)(const char* line, i32 length, void* user);

// Makes the protocol active for the running game (bmf_core_start done): paused, no holds, no
// watches, output to `output` (0 = stdout). Tests call it directly; the runtime calls it through
// bmf_protocol_start. O(1).
void bmf_protocol_begin(bmf_protocol_output_fn output, void* user);

// Ends the protocol: releases held buttons and axes, drops watches, inactive again (the tick hooks
// do nothing). O(actions + watches).
void bmf_protocol_end(void);

// bmf_protocol_begin with stdout, plus the stdin reader thread. Once per process: the reader is
// never joined (it blocks in a read until the process exits). O(1) plus thread start.
void bmf_protocol_start(void);

// Executes every request line the reader has queued. When `wait` is set and no line is queued,
// blocks until one arrives (headless and paused: nothing else to do). At the end of stdin it
// requests quit (bmf_core_request_quit). O(lines).
void bmf_protocol_poll(b32 wait);

// Executes one request line (without the newline) and writes its response, and the events it
// causes, to the output. O(the command's work).
void bmf_protocol_execute(bmf_str line);

// Writes one event line (no newline) where responses go: the protocol's output while it is active
// with an output callback, else stdout (flushed). Used by replay and other debug modules. O(length).
void bmf_protocol_write_event(const char* line, i32 length);

// 1 while `run` is in effect (free-running in real time), 0 when paused (the start state). O(1).
b32 bmf_protocol_running(void);

// Tick hook, called by the engine after every tick and its events (core_tick_once). While the
// protocol is active, input holds count down (a hold that ends is released before the next
// tick samples input) and watches report changes. O(actions + watches x value size).
void bmf_protocol_after_tick(void);

#endif // BMF_PROTOCOL_H
