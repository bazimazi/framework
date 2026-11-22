// snap.h: state hashing and snapshots for the control protocol (SPEC §11.2 `hash` and `snap`,
// task 1A-34).
//
// `hash` answers with bmf_core_state_hash (the state block plus the tick index), after a scan for
// NaN and Inf in every reflected f32 field (D-37 rule 11): a state that holds one is reported
// instead of hashed. (In debug builds bmf_core_state_hash itself asserts on NaN/Inf, so every
// other hash pass, such as replay checkpoints, stops the process at the first one.)
//
// `snap save NAME` copies the state block and tick into a named slot; `snap load NAME` copies them
// back (bmf_core_load_state). At most BMF_SNAP_MAX_SLOTS names; saving an existing name replaces
// it. A slot remembers the game's state size and layout hash and is dropped when loaded into a
// game with another layout (a hot reload that reset the state). Slots are cleared by
// bmf_core_start. Loading a snapshot replaces the state between ticks, so the session recording
// starts again and an explicit recording breaks (replay.h, bmf_replay_on_state_replaced).
//
// Memory: each slot reserves address space for the largest state block (BMF_CORE_MAX_STATE_SIZE)
// on its first save and commits only what the game's state needs. Not part of the state block.
// Main thread only.
#ifndef BMF_SNAP_H
#define BMF_SNAP_H

#include "../bmf.h"

enum {
    BMF_SNAP_MAX_SLOTS = 16,
    BMF_SNAP_NAME_MAX = 32,   // bytes including the NUL: names are 1..31 of [A-Za-z0-9_.-]
};

typedef enum bmf_snap_status {
    BMF_SNAP_OK = 0,
    BMF_SNAP_BAD_NAME,        // empty, too long or another character
    BMF_SNAP_UNKNOWN,         // no slot of that name (or it was saved with another layout: dropped)
    BMF_SNAP_FULL,            // BMF_SNAP_MAX_SLOTS other names are taken
    BMF_SNAP_NO_MEMORY,       // address space reservation failed
} bmf_snap_status;

// Hashes the running game's state (bmf_core_state_hash). Returns 1 and the hash when no reflected
// f32/v2/v3/v4 element is NaN or Inf; otherwise returns 0 and writes the first such path ("name"
// or "name[i]") into path[0..path_capacity) without hashing. O(state).
b32 bmf_snap_hash(u64* out_hash, char* path, i32 path_capacity);

// Saves the state block and tick under `name`. Errors (message in detail): BMF_SNAP_BAD_NAME,
// BMF_SNAP_FULL, BMF_SNAP_NO_MEMORY. O(state).
bmf_snap_status bmf_snap_save(bmf_str name, char* detail, i32 detail_capacity);

// Loads the snapshot `name` into the running game (state block and tick) and restarts the
// recordings (see above). Writes the loaded tick. Errors: BMF_SNAP_BAD_NAME, BMF_SNAP_UNKNOWN.
// O(state).
bmf_snap_status bmf_snap_load(bmf_str name, i64* out_tick, char* detail, i32 detail_capacity);

// The arenas of slots that hold a snapshot, named "snap.<name>", for the `mem` report. Writes at
// most `capacity`; returns the count. O(slots).
i32 bmf_snap_arenas(const bmf_arena** out, const char** names, i32 capacity);

// Forgets every slot (their address space stays reserved for reuse). Called by bmf_core_start.
// O(slots).
void bmf_snap_clear(void);

#endif // BMF_SNAP_H
