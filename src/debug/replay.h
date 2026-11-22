// replay.h: recording and replaying input (SPEC §11.4, task 1A-33).
//
// A replay (.bmfr) holds everything needed to reproduce a run of a game build: the state block
// at the first recorded tick, then one input record per tick, and checkpoints. Since the
// simulation is deterministic (D-18), feeding the same inputs to the same start state gives the
// same states, which the checkpoints verify.
//
// File layout (little-endian, every target):
//   bmf_replay_header (176 bytes)
//   start state       RLE-coded (see below), header.state_bytes bytes, decodes to state_size bytes
//   record stream     header.stream_bytes bytes of records, in time order:
//       0x01 RLE(input XOR previous input)     one tick's bmf_input (224 bytes decoded); the
//                                              "previous input" before the first record is all zero
//       0x02                                   one tick whose input equals the previous one
//       0x03 offset:u32 length:u32 RLE(bytes)  the state was written between ticks (protocol `set`):
//                                              bytes [offset, offset+length). Loads that replace
//                                              the state and tick (`replay`, `snap load`, a hot
//                                              reload) restart the session recording instead
//   checkpoints       checkpoint_count x (tick:i64, state_hash:u64, field_hash:u64 x field_count)
//                     at the start, every checkpoint_interval ticks and at the end. state_hash is
//                     bmf_core_state_hash; field_hash[i] hashes field i's bytes (full capacity).
// RLE: a control byte c; c < 128: c + 1 literal bytes follow; c >= 128: the next byte repeats
// c - 126 times (2..129).
//
// Recorders: in debug and BMF_AGENT builds a session recorder runs from bmf_core_start (tick 0
// after init) and restarts after every hot reload (crash dumps, task 1A-35, write it); the
// protocol's `record start PATH` / `record stop` runs a second, explicit one. Players:
// `--replay FILE` (all ticks, then quit), `--replay FILE --verify` (no display: all ticks as fast
// as possible, every checkpoint checked, exit code 0 on match, 1 on a mismatch), and the protocol's
// `replay PATH` (the following `step`s take their input from the file).
//
// Verification: at every checkpoint the player compares the state hash. On a mismatch it reports
// the checkpoint tick, the last checkpoint that matched, both hashes, and the reflected fields
// whose hashes differ, with their replayed values and their values in the snapshot the player took
// at the last matching checkpoint:
//   {"event":"replay","verify":"mismatch","tick":T,"last_match":P,"expected":H,"actual":H,
//    "fields":[{"path":F,"value":V,"at_last_match":V0},...]}
// A match at the end prints {"event":"replay","verify":"ok","ticks":N,"checkpoints":K,"hash":H}.
// Hashes are 16 hex digits.
//
// Thread rule: main thread only. Memory: each recorder and the player reserve their own address
// ranges (committed on use); nothing comes from the game's budget. Compiled only when
// BMF_PROTOCOL_ENABLED (debug, BMF_AGENT).
#ifndef BMF_REPLAY_H
#define BMF_REPLAY_H

#include "../bmf.h"

enum {
    BMF_REPLAY_VERSION = 1,
    BMF_REPLAY_CHECKPOINT_INTERVAL = 60,         // ticks
    BMF_REPLAY_STREAM_RESERVE = 256 << 20,       // bytes of record stream per recorder (about 50 days of idle 60 Hz input)
    BMF_REPLAY_CHECKPOINT_RESERVE = 64 << 20,    // bytes of checkpoints per recorder
    BMF_REPLAY_MAX_DIFF_FIELDS = 16,             // fields listed in a mismatch report
};

typedef struct bmf_replay_header {
    u8 magic[4];                // "BMFR"
    u32 version;                // BMF_REPLAY_VERSION
    u32 header_size;            // sizeof(bmf_replay_header)
    i32 state_size;
    u64 build_id;               // bmf_core_build_id of the recording build (informational)
    u64 seed;
    u64 state_layout_hash;
    i64 start_tick;             // ticks completed when recording started
    i64 end_tick;               // ticks completed when it stopped
    i32 tick_rate;
    i32 field_count;
    i32 checkpoint_interval;
    i32 checkpoint_count;
    i64 state_offset, state_bytes;
    i64 stream_offset, stream_bytes;
    i64 checkpoints_offset;
    char game_name[64];         // NUL-terminated
} bmf_replay_header;
BMF_STATIC_ASSERT(sizeof(bmf_replay_header) == 176, "bmf_replay_header has no implicit padding");

// ---------------------------------------------------------------- codec (also for tools and tests)
// RLE-encodes n bytes into out (capacity >= bmf_replay_rle_bound(n)); returns the bytes written.
// O(n).
i64 bmf_replay_rle_bound(i64 n);
i64 bmf_replay_rle_encode(const u8* data, i64 n, u8* out);
// Decodes exactly n bytes from in[0..in_size) into out; returns the input bytes consumed, or -1
// when the input is malformed or too short. O(n).
i64 bmf_replay_rle_decode(const u8* in, i64 in_size, u8* out, i64 n);

// File offset of the first byte after the record tag of the input record for tick `tick` in a
// .bmfr file (state records are skipped), or -1. For tools and tests that edit one tick's input.
// O(records before it).
i64 bmf_replay_input_offset(const u8* file, i64 size, i64 tick);

// ---------------------------------------------------------------- engine hooks (core)
// Starts (or restarts) the session recorder at the current tick of the running game. O(state).
void bmf_replay_session_begin(void);
// The input of the tick about to run: when a replay is playing, writes its next input to *out and
// returns 1 (state records before it are applied first); else returns 0. At the end of the replay
// it prints the end/verify event, stops playing and returns 0.
b32 bmf_replay_next_input(bmf_input* out);
// Records the input of the tick about to run into every active recorder. O(input size).
void bmf_replay_record_input(const bmf_input* in);
// After every tick: recorders take checkpoints; the player verifies its checkpoints.
// O(state) at checkpoints, O(1) otherwise.
void bmf_replay_after_tick(void);
// The state was written between ticks (bytes [offset, offset + size) of the state block):
// recorders store the bytes as a state record. O(size).
void bmf_replay_note_state_write(i32 offset, i32 size);
// The state block or tick was replaced between ticks (hot reload, `snap load`): a playing replay
// stops with {"event":"replay","state":"stopped","reason":REASON}, an explicit recording is marked
// broken with the message `broken` (record stop reports it), and the session recording starts
// again from the current state and tick. `replay PATH` does the same to the recorders. O(state).
void bmf_replay_on_state_replaced(const char* reason, const char* broken);

// ---------------------------------------------------------------- recording (protocol)
// Starts the explicit recorder at the current tick; `record stop` writes `path`. BMF_ERR_INVALID
// when one is already running. O(state).
bmf_err bmf_replay_record_start(const char* path, char* error, i32 error_capacity);
// Stops the explicit recorder (with a final checkpoint) and writes its file atomically.
// Errors: BMF_ERR_INVALID (not recording, or the recording broke: hot reload, out of memory),
// BMF_ERR_IO (the file could not be written). Writes the tick count, file size and checkpoint
// count. O(file size).
bmf_err bmf_replay_record_stop(i64* ticks, i64* bytes, i32* checkpoints, char* error, i32 error_capacity);
// Writes the session recording so far (with a final checkpoint) to `path`. O(file size).
bmf_err bmf_replay_write_session(const char* path, char* error, i32 error_capacity);
// Crash dumps (src/debug/crash.h): writes the session recording without a final checkpoint (no
// hashing of a state that may be half-way through a tick); its inputs run up to and including the
// tick that crashed. Uses no memory beyond what the recorder holds. O(file size).
bmf_err bmf_replay_write_crash(const char* path, char* error, i32 error_capacity);
// The state block as it was at the session recording's last checkpoint (the last state known to
// be good: its hash pass passed), with that checkpoint's tick and state hash; 0 when there is
// none. Valid until the next checkpoint. O(1).
const u8* bmf_replay_session_last_state(i64* out_tick, u64* out_hash);

// The recorders' and the player's arenas that exist (reserved), with names such as
// "replay.session.stream", for the `mem` report. Writes at most `capacity`; returns the count.
// O(arenas).
i32 bmf_replay_arenas(const bmf_arena** out, const char** names, i32 capacity);

// ---------------------------------------------------------------- playing
// Reads the header of `path` (for --replay: the seed and tick rate the run must start with).
// BMF_ERR_NOT_FOUND / BMF_ERR_INVALID (not a .bmfr of this version). O(1).
bmf_err bmf_replay_peek(const char* path, bmf_replay_header* out, char* error, i32 error_capacity);
// Starts playing `path` on the running game: checks that the game name, state size, layout hash,
// field count and tick rate match, loads the start state and start tick (bmf_core_load_state) and
// checks the start checkpoint when `verify`. The next ticks take their input from the file.
// A build id that differs is reported as {"event":"replay","warning":"build_id",...} only.
// Errors with a message: BMF_ERR_NOT_FOUND, BMF_ERR_INVALID. O(state).
bmf_err bmf_replay_play_begin(const char* path, b32 verify, char* error, i32 error_capacity);
// 1 while a replay is playing. O(1).
b32 bmf_replay_playing(void);
// Outcome of the last replay: 1 when no checkpoint mismatched. O(1).
b32 bmf_replay_verified(void);

#endif // BMF_REPLAY_H
