// profile.h: the profiler, the memory report and the log (SPEC §11.3, §11.5, task 1A-36).
//
// Zones (bmf.h bmf_zone_begin/end) record the duration of every call, in timestamp-counter
// cycles, into a ring of the last BMF_PROFILE_SAMPLES calls per zone, plus a call count. Frames
// record their duration into a ring of the same length. `profile N` starts a new window (clears
// counts and rings), runs N ticks and reports per zone over the calls of the window (the last
// BMF_PROFILE_SAMPLES of them): calls, mean_us, p50_us, p99_us, max_us; plus frame times, the last
// frame's draw calls and instances, and the counter frequency.
//
// The counter is rdtsc on x64 (bmf_platform_time_ns elsewhere). Its frequency is calibrated
// against bmf_platform_time_ns (QueryPerformanceCounter) over the whole time since
// bmf_profile_begin: no wait at startup; the first report waits until 10 ms have passed.
//
// Zones are main thread only. They must nest: a stack of up to BMF_PROFILE_MAX_DEPTH open zones;
// bmf_zone_end of another zone than the innermost open one is a nesting error (debug builds
// assert with both names; release builds with BMF_AGENT count it in "nesting_errors").
//
// `mem` reports every arena of the engine and of the debug tools: reserved, committed, used and
// high-water bytes.
//
// The log (bmf.h bmf_log): a ring of BMF_LOG_RING messages (level, tick, text up to
// BMF_LOG_TEXT_MAX - 1 bytes), counts per level, a dropped count; `log [LEVEL]` drains it.
// Optional echo to stderr as {"event":"log","level":L,"tick":T,"msg":M} (the runtime turns it on
// in --control mode). Any thread may log (a spin lock guards the ring); only the main thread
// echoes.
#ifndef BMF_PROFILE_H
#define BMF_PROFILE_H

#include "../bmf.h"
#include "../base/base_string.h"

enum {
    BMF_PROFILE_SAMPLES = 1024,      // calls kept per zone, frames kept
    BMF_PROFILE_MAX_DEPTH = 32,      // open zones at once
    BMF_LOG_RING = 256,              // messages kept
    BMF_LOG_TEXT_MAX = 256,          // bytes per message, including the NUL
};

// Starts the clock calibration and clears everything (zones, frames, open-zone stack). Called by
// bmf_core_start. O(zones x samples) the first time, O(zones) after.
void bmf_profile_begin(void);

// A new window for `profile`: call counts, samples and frames cleared. O(zones).
void bmf_profile_window_reset(void);

// One frame took `ns` nanoseconds (the engine, at the end of every frame). O(1).
void bmf_profile_frame(i64 ns);

// Writes the window's report keys ("zones":{...},"frames":F,...) into an open JSON object. Zone
// names: engine names, then the game's zone_names, else "game.<i>". Zones without calls are left
// out. O(zones x samples x log samples).
void bmf_profile_write_report(bmf_json_writer* w);

// Open zones right now (0 between ticks and frames when every zone nests). O(1).
i32 bmf_profile_depth(void);

// Nesting errors counted (release builds with BMF_AGENT; debug builds assert instead). O(1).
i64 bmf_profile_nesting_errors(void);

// Writes "arenas":[{"name":N,"reserved":R,"committed":C,"used":U,"high_water":H},...] into an open
// JSON object: the game's memory (permanent, game_state, level, frame, scratch), then the debug
// tools' arenas that exist (replay recorders and player, snapshot slots). O(arenas).
void bmf_profile_write_mem(bmf_json_writer* w);

// The log: echo to stderr on/off (off by default). O(1).
void bmf_log_set_echo(b32 echo);

// Writes "entries":[{"tick":T,"level":L,"msg":M},...] with the stored messages of at least `min`
// level, oldest first, then "dropped":D (messages lost to a full ring since the last drain) and
// "counts":{"debug":..,"info":..,"warn":..,"error":..} (since bmf_log_clear); empties the ring.
// O(messages).
void bmf_log_drain(bmf_json_writer* w, bmf_log_level min);

// Empties the ring and zeroes the counts (bmf_core_start). O(1).
void bmf_log_clear(void);

// The spelling of a level ("debug", "info", "warn", "error"), or 0. O(1).
const char* bmf_log_level_name(bmf_log_level level);

#endif // BMF_PROFILE_H
