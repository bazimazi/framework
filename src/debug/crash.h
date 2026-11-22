// crash.h: crash dumps (SPEC §11.4, task 1A-35).
//
// When the running game crashes (assert, bmf_panic, an unhandled exception; the platform's crash
// report, platform.h), the crash hook writes two files into the folder `crash` next to the exe:
//   <UTC>-<game>.bmfr    the session recording (replay.h) from its start (tick 0, or the last hot
//                        reload / snapshot load / replay load) up to and including the tick that
//                        crashed: `game.exe --replay FILE` crashes again at the same tick
//   <UTC>-<game>.state   the state block at the session's last checkpoint (the last state whose
//                        hash pass passed), after a bmf_state_file_header
// and adds to the crash line: "tick":T (ticks completed), "phase":"tick"|"draw"|"init"|"none",
// "replay":PATH, "state":PATH (or "dump_error":TEXT when the files could not be written).
// <UTC> is YYYYMMDD-HHMMSS-mmm.
//
// Debug builds and release builds with BMF_AGENT=1 (the session recording exists only there).
// Crash-time code uses only memory that exists already: static buffers, the recorder's arenas.
#ifndef BMF_CRASH_H
#define BMF_CRASH_H

#include "../bmf.h"

// The .state file: this header, then state_size bytes of the state block.
typedef struct bmf_state_file_header {
    char magic[4];              // "BMFS"
    i32 version;                // BMF_STATE_FILE_VERSION
    i32 header_size;            // sizeof(bmf_state_file_header)
    i32 state_size;
    i64 tick;                   // ticks completed when the state was taken
    u64 state_hash;             // bmf_core_state_hash of it
    u64 state_layout_hash;
    u64 build_id;
    char game_name[64];
} bmf_state_file_header;
BMF_STATIC_ASSERT(sizeof(bmf_state_file_header) == 112, "bmf_state_file_header layout");

enum { BMF_STATE_FILE_VERSION = 1 };

// Registers the crash hook and fixes the dump folder (<exe dir>crash). Called by bmf_core_start.
// The folder is created only when a crash happens. O(path length).
void bmf_crash_begin(void);

#endif // BMF_CRASH_H
