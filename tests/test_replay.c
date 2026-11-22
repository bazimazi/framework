// Tests for src/debug/replay.c (task 1A-33): the RLE codec, record then replay giving the same
// hash sequence (protocol and process), verification failing at the expected checkpoint with a
// field diff after one input byte is mutated, the session recording, and the error paths.
// Uses the protocol test game and helpers of tests/test_protocol.c.

// ---------------------------------------------------------------- replay.rle
static void test_replay_rle(bmf_test* t) {
    static u8 data[4096], encoded[4096 + 64], decoded[4096];
    i32 sizes[] = { 0, 1, 2, 3, 127, 128, 129, 130, 257, 4096 };
    for (i32 pattern = 0; pattern < 4; pattern++) {
        for (i32 s = 0; s < (i32)(sizeof sizes / sizeof sizes[0]); s++) {
            i64 n = sizes[s];
            bmf_rng rng;
            bmf_rng_seed(&rng, (u64)(pattern * 100 + s), 3);
            for (i64 i = 0; i < n; i++) {
                u32 r = bmf_rng_next_u32(&rng);
                data[i] = pattern == 0 ? 0                                   // all zero
                        : pattern == 1 ? (u8)r                                // noise
                        : pattern == 2 ? (u8)((i / ((r >> 28) + 1)) & 3)      // short runs
                        : (u8)(i % 300 < 150 ? 7 : (u8)r);                    // long runs and noise
            }
            i64 size = bmf_replay_rle_encode(data, n, encoded);
            char message[96];
            bmf_fmt_buf(message, (i32)sizeof message, "pattern %i, %li bytes", pattern, n);
            bmf_check(t, size <= bmf_replay_rle_bound(n), __FILE__, __LINE__, message);
            bmf_check_eq_i64(t, bmf_replay_rle_decode(encoded, size, decoded, n), size, __FILE__, __LINE__, message);
            bmf_check_eq_mem(t, decoded, data, n, __FILE__, __LINE__, message);
        }
    }
    // 224 zero bytes (an unchanged bmf_input delta) take 4 bytes: two runs.
    bmf_memset(data, 0, 224);
    bmf_check_eq_i64(t, bmf_replay_rle_encode(data, 224, encoded), 4, __FILE__, __LINE__, "224 zeros");
    // Malformed input: truncated literal, truncated run, too much output.
    const u8 truncated_literal[] = { 5, 1, 2 };
    const u8 truncated_run[] = { 200 };
    const u8 too_long[] = { 255, 9 };
    bmf_check_eq_i64(t, bmf_replay_rle_decode(truncated_literal, 3, decoded, 6), -1, __FILE__, __LINE__, "truncated literal");
    bmf_check_eq_i64(t, bmf_replay_rle_decode(truncated_run, 1, decoded, 74), -1, __FILE__, __LINE__, "truncated run");
    bmf_check_eq_i64(t, bmf_replay_rle_decode(too_long, 2, decoded, 10), -1, __FILE__, __LINE__, "run longer than the output");
}

// ---------------------------------------------------------------- helpers
static char replay_test_dir[1024];

static void replay_test_path(char* out, i32 capacity, const char* name) {
    if (!replay_test_dir[0]) bmf_platform_exe_dir(replay_test_dir, (i32)sizeof replay_test_dir);
    bmf_fmt_buf(out, capacity, "%s%s", replay_test_dir, name);
}

static void replay_test_start(bmf_test* t, u64 seed) {
    bmf_game_desc game = protocol_test_game();
    char error[BMF_CORE_ERROR_MAX];
    bmf_core_options options;
    bmf_memset(&options, 0, sizeof options);
    options.time_scale = 1.0f;
    options.control = 1;
    options.seed = seed;
    bmf_check_eq_i64(t, bmf_core_start(&game, &options, error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, error);
    bmf_core_set_view((bmf_draw2d_view){ 1280, 720, 1.0f });
    bmf_protocol_begin(protocol_test_output, 0);
}

static void replay_test_run(const char* request) {
    protocol_capture.length = 0;
    protocol_capture.text[0] = 0;
    bmf_protocol_execute(bmf_str_from_cstr(request));
}

enum { REPLAY_TEST_TICKS = 250 };
static u64 replay_test_hashes[REPLAY_TEST_TICKS + 1];

// Records a session with every kind of input and a protocol `set`, from tick 10 to tick 260:
// 250 ticks; hashes[i] = state hash after i recorded ticks.
static void replay_test_record(bmf_test* t, const char* path) {
    replay_test_start(t, 7);
    replay_test_run("step 10");
    char request[1200];
    bmf_fmt_buf(request, (i32)sizeof request, "record start %s", path);
    replay_test_run(request);
    bmf_check(t, core_test_contains(protocol_capture.text, "\"tick\":10}"), __FILE__, __LINE__, protocol_capture.text);
    replay_test_hashes[0] = bmf_core_state_hash();
    for (i32 i = 1; i <= REPLAY_TEST_TICKS; i++) {
        if (i == 5) replay_test_run("press jump 3");
        if (i == 40) replay_test_run("axis move 0.25 -0.5 30");
        if (i == 75) replay_test_run("pointer 0 1.5 2 down");
        if (i == 80) replay_test_run("pointer 0 1.5 2 up");
        if (i == 100) replay_test_run("set values[1] 9.5");
        if (i == 130) replay_test_run("key space down");
        if (i == 133) replay_test_run("key space up");
        if (i == 200) replay_test_run("press jump");
        bmf_core_step(1);
        replay_test_hashes[i] = bmf_core_state_hash();
    }
    replay_test_run("record stop");
    bmf_check(t, core_test_contains(protocol_capture.text, "{\"ok\":1,\"ticks\":250,\"bytes\":"), __FILE__, __LINE__, protocol_capture.text);
    bmf_check(t, core_test_contains(protocol_capture.text, "\"checkpoints\":6}"), __FILE__, __LINE__, protocol_capture.text);   // 10, 70, 130, 190, 250, 260
    bmf_protocol_end();
    bmf_core_stop();
}

// ---------------------------------------------------------------- replay.roundtrip
static void test_replay_roundtrip(bmf_test* t) {
    char path[1200], request[1300];
    replay_test_path(path, (i32)sizeof path, "replay_test.bmfr");
    replay_test_record(t, path);

    // A fresh run (another seed: the replay brings its own start state) replays it.
    replay_test_start(t, 99);
    bmf_fmt_buf(request, (i32)sizeof request, "replay %s", path);
    replay_test_run(request);
    bmf_check(t, core_test_contains(protocol_capture.text, "\"start_tick\":10,\"end_tick\":260,\"checkpoints\":6}"), __FILE__, __LINE__, protocol_capture.text);
    bmf_check(t, bmf_replay_playing() && bmf_core_ctx()->tick == 10, __FILE__, __LINE__, "playing from tick 10");
    b32 same = bmf_core_state_hash() == replay_test_hashes[0];
    i32 first_difference = -1;
    for (i32 i = 1; i <= REPLAY_TEST_TICKS; i++) {
        protocol_capture.length = 0;
        bmf_core_step(1);
        if (bmf_core_state_hash() != replay_test_hashes[i] && first_difference < 0) first_difference = i;
    }
    same &= first_difference < 0;
    char message[256];
    bmf_fmt_buf(message, (i32)sizeof message, "identical hash sequence (first difference after %i ticks)", first_difference);
    bmf_check(t, same, __FILE__, __LINE__, message);
    // The last tick ends the replay with the verdict; later ticks take live input again.
    bmf_check(t, core_test_contains(protocol_capture.text, "{\"event\":\"replay\",\"verify\":\"ok\",\"ticks\":250,\"checkpoints\":6,\"hash\":\""), __FILE__, __LINE__, protocol_capture.text);
    bmf_check(t, !bmf_replay_playing() && bmf_replay_verified(), __FILE__, __LINE__, "replay over, verified");
    bmf_protocol_end();
    bmf_core_stop();
}

// ---------------------------------------------------------------- replay.mutated
static void test_replay_mutated(bmf_test* t) {
    char path[1200], bad_path[1200], request[1300];
    replay_test_path(path, (i32)sizeof path, "replay_test.bmfr");
    replay_test_path(bad_path, (i32)sizeof bad_path, "replay_mutated.bmfr");
    replay_test_record(t, path);
    bmf_arena* arena = &bmf_core_memory()->permanent;
    i64 mark = bmf_arena_mark(arena);
    bmf_str file;
    bmf_check_eq_i64(t, bmf_platform_file_read(path, arena, &file), BMF_OK, __FILE__, __LINE__, "read the replay");
    u8* bytes = (u8*)file.ptr;

    // Tick 139 (the 130th recorded tick) samples `key space down`: its delta record starts with a
    // literal run of one byte, 0x01 (bit 0 of `down`). Clearing it flips `jump` down for every later
    // tick until a delta flips it back (XOR chain): the state diverges from tick 139 on.
    i64 at = bmf_replay_input_offset(bytes, file.len, 139);
    bmf_check(t, at > 0 && bytes[at] == 0x00 && bytes[at + 1] == 0x01, __FILE__, __LINE__, "tick 139: literal run of one byte 0x01");
    bytes[at + 1] = 0x00;
    bmf_check_eq_i64(t, bmf_platform_file_write_atomic(bad_path, bytes, file.len), BMF_OK, __FILE__, __LINE__, "write the mutated replay");
    bmf_arena_pop_to(arena, mark);

    // Verification fails at the first checkpoint after the divergence (190), names the field that
    // differs (jump.down: 1 instead of 0) and shows its value at the last good checkpoint (130).
    replay_test_start(t, 7);
    bmf_fmt_buf(request, (i32)sizeof request, "replay %s", bad_path);
    replay_test_run(request);
    static char events[1 << 16];
    events[0] = 0;
    i32 events_length = 0;
    for (i32 i = 0; i < REPLAY_TEST_TICKS && bmf_replay_playing(); i++) {
        protocol_capture.length = 0;
        protocol_capture.text[0] = 0;
        bmf_core_step(1);
        if (protocol_capture.length && events_length + protocol_capture.length < (i32)sizeof events) {
            bmf_memcpy(events + events_length, protocol_capture.text, protocol_capture.length + 1);
            events_length += protocol_capture.length;
        }
    }
    bmf_check(t, core_test_contains(events, "{\"event\":\"replay\",\"verify\":\"mismatch\",\"tick\":190,\"last_match\":130,\"expected\":\""), __FILE__, __LINE__, events);
    bmf_check(t, core_test_contains(events, "\"fields\":[{\"path\":\"jump.down\",\"value\":1,\"at_last_match\":0}]}"), __FILE__, __LINE__, events);
    bmf_check(t, !bmf_replay_playing() && !bmf_replay_verified(), __FILE__, __LINE__, "stopped at the mismatch");
    bmf_check_eq_i64(t, bmf_core_ctx()->tick, 190, __FILE__, __LINE__, "the replay stopped at tick 190");
    bmf_protocol_end();
    bmf_core_stop();

    // Second mutation: tick 209 (`press jump`) has the delta [0x00 0x01] [run 7 x 0x00] [0x00 0x01]:
    // clear the `pressed` byte (offset + 5). From tick 210 on the XOR chain keeps `pressed` set, so
    // jump.presses grows each tick; at the last good checkpoint (190) it was 2 (0 at the start).
    mark = bmf_arena_mark(arena);
    bmf_check_eq_i64(t, bmf_platform_file_read(path, arena, &file), BMF_OK, __FILE__, __LINE__, "read the replay again");
    bytes = (u8*)file.ptr;
    at = bmf_replay_input_offset(bytes, file.len, 209);
    bmf_check(t, at > 0 && bytes[at + 1] == 0x01 && bytes[at + 4] == 0x00 && bytes[at + 5] == 0x01, __FILE__, __LINE__, "tick 209 delta layout");
    bytes[at + 5] = 0x00;
    bmf_check_eq_i64(t, bmf_platform_file_write_atomic(bad_path, bytes, file.len), BMF_OK, __FILE__, __LINE__, "write the second mutation");
    bmf_arena_pop_to(arena, mark);
    replay_test_start(t, 7);
    replay_test_run(request);
    events[0] = 0;
    events_length = 0;
    for (i32 i = 0; i < REPLAY_TEST_TICKS && bmf_replay_playing(); i++) {
        protocol_capture.length = 0;
        protocol_capture.text[0] = 0;
        bmf_core_step(1);
        if (protocol_capture.length && events_length + protocol_capture.length < (i32)sizeof events) {
            bmf_memcpy(events + events_length, protocol_capture.text, protocol_capture.length + 1);
            events_length += protocol_capture.length;
        }
    }
    bmf_check(t, core_test_contains(events, "\"verify\":\"mismatch\",\"tick\":250,\"last_match\":190,"), __FILE__, __LINE__, events);
    bmf_check(t, core_test_contains(events, "\"path\":\"jump.presses\",\"value\":") && core_test_contains(events, "\"at_last_match\":2}"), __FILE__, __LINE__, events);
    bmf_protocol_end();
    bmf_core_stop();

#if BMF_OS_WINDOWS
    // The same through --replay --verify in a process: exit 0 for the good file, 1 for the (second) mutated one.
    static char out[1 << 16];
    char flags[1400];
    bmf_fmt_buf(flags, (i32)sizeof flags, "--replay \"%s\" --verify", path);
    u32 code = protocol_test_spawn(flags, "", 0, out, (i32)sizeof out);
    bmf_check(t, code == 0 && core_test_contains(out, "{\"event\":\"replay\",\"verify\":\"ok\",\"ticks\":250,\"checkpoints\":6,"), __FILE__, __LINE__, out);
    bmf_fmt_buf(flags, (i32)sizeof flags, "--replay \"%s\" --verify", bad_path);
    code = protocol_test_spawn(flags, "", 0, out, (i32)sizeof out);
    bmf_check(t, code == 1 && core_test_contains(out, "\"verify\":\"mismatch\",\"tick\":250,\"last_match\":190"), __FILE__, __LINE__, out);
    // Without --verify the replay plays (headless, null GPU) and the run ends with it.
    bmf_fmt_buf(flags, (i32)sizeof flags, "--replay \"%s\" --headless --no-gpu", path);
    code = protocol_test_spawn(flags, "", 0, out, (i32)sizeof out);
    bmf_check(t, code == 0 && core_test_contains(out, "{\"event\":\"replay\",\"state\":\"end\",\"tick\":260}") && core_test_contains(out, "{\"event\":\"quit\",\"tick\":260,"),
              __FILE__, __LINE__, out);
#endif
    bmf_platform_file_delete(bad_path);
    bmf_platform_file_delete(path);
}

// ---------------------------------------------------------------- replay.session
static void test_replay_session(bmf_test* t) {
    char path[1200], request[1300], error[256];
    replay_test_path(path, (i32)sizeof path, "replay_session.bmfr");
    replay_test_start(t, 3);
    replay_test_run("press jump 5");
    replay_test_run("step 77");
    replay_test_run("set count 4");
    replay_test_run("step 5");
    u64 expected = bmf_core_state_hash();
    bmf_check_eq_i64(t, bmf_replay_write_session(path, error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, error);
    replay_test_run("step 3");   // the session keeps recording after a write
    bmf_protocol_end();
    bmf_core_stop();

    replay_test_start(t, 3);
    bmf_fmt_buf(request, (i32)sizeof request, "replay %s", path);
    replay_test_run(request);
    bmf_check(t, core_test_contains(protocol_capture.text, "\"start_tick\":0,\"end_tick\":82,\"checkpoints\":3}"), __FILE__, __LINE__, protocol_capture.text);   // 0, 60, 82
    protocol_capture.length = 0;
    replay_test_run("step 82");
    bmf_check(t, core_test_contains(protocol_capture.text, "{\"event\":\"replay\",\"verify\":\"ok\",\"ticks\":82,\"checkpoints\":3,"), __FILE__, __LINE__, protocol_capture.text);
    bmf_check(t, bmf_core_state_hash() == expected, __FILE__, __LINE__, "the session replays to the same state (the set included)");
    bmf_protocol_end();
    bmf_core_stop();
    bmf_platform_file_delete(path);
}

// ---------------------------------------------------------------- replay.errors
static void test_replay_errors(bmf_test* t) {
    char path[1200], request[1300], error[256];
    replay_test_path(path, (i32)sizeof path, "replay_errors.bmfr");
    replay_test_start(t, 1);
    protocol_expect(t, "record stop", "{\"ok\":0,\"error\":\"record_failed\",\"detail\":\"not recording (record start PATH first)\"}\n", __LINE__);
    protocol_expect(t, "record go", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: record start PATH | record stop\"}\n", __LINE__);
    protocol_expect(t, "replay nothere.bmfr", "{\"ok\":0,\"error\":\"replay_failed\",\"detail\":\"cannot read 'nothere.bmfr'\"}\n", __LINE__);
    bmf_fmt_buf(request, (i32)sizeof request, "record start %s", path);
    replay_test_run(request);
    replay_test_run(request);
    bmf_check(t, core_test_contains(protocol_capture.text, "{\"ok\":0,\"error\":\"record_failed\",\"detail\":\"already recording to"), __FILE__, __LINE__, protocol_capture.text);
    // A hot reload breaks the explicit recording.
    bmf_game_desc same = *bmf_core_game();
    bmf_check_eq_i64(t, bmf_core_swap_game(&same, error, (i32)sizeof error), BMF_CORE_SWAP_KEPT, __FILE__, __LINE__, error);
    protocol_expect(t, "record stop", "{\"ok\":0,\"error\":\"record_failed\",\"detail\":\"the recording broke: the game was hot-reloaded during the recording\"}\n", __LINE__);
    replay_test_run(request);
    replay_test_run("step 3");
    replay_test_run("record stop");
    bmf_check(t, core_test_contains(protocol_capture.text, "{\"ok\":1,\"ticks\":3,"), __FILE__, __LINE__, protocol_capture.text);
    bmf_protocol_end();
    bmf_core_stop();

    // Another game cannot play it.
    bmf_game_desc other = core_test_game();
    bmf_core_options options = core_test_options();
    bmf_check_eq_i64(t, bmf_core_start(&other, &options, error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, error);
    bmf_protocol_begin(protocol_test_output, 0);
    bmf_fmt_buf(request, (i32)sizeof request, "replay %s", path);
    replay_test_run(request);
    bmf_check(t, core_test_contains(protocol_capture.text, "{\"ok\":0,\"error\":\"replay_failed\",\"detail\":") &&
                 core_test_contains(protocol_capture.text, "was recorded with another game name (game \\\"protocol_test\\\", 60 Hz)"), __FILE__, __LINE__, protocol_capture.text);
    // A file that is not a replay.
    bmf_check_eq_i64(t, bmf_platform_file_write_atomic(path, "not a replay at all", 19), BMF_OK, __FILE__, __LINE__, "write junk");
    replay_test_run(request);
    bmf_check(t, core_test_contains(protocol_capture.text, "is not a version 1 .bmfr replay"), __FILE__, __LINE__, protocol_capture.text);
    bmf_protocol_end();
    bmf_core_stop();
    bmf_platform_file_delete(path);
}
