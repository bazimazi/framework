// Tests for src/debug/snap.c (task 1A-34): `hash` and `snap save/load` through the protocol.
// Save, step 100, load, step 100 gives the same hash; slots (16 names, overwrite, layout check,
// cleared by a new run); NaN/Inf reported by `hash` and asserted by the debug hash pass; loading
// a snapshot restarts the session recording and breaks an explicit one.
// Uses the protocol helpers of tests/test_protocol.c and tests/test_replay.c.

// ---------------------------------------------------------------- the snap test game
typedef struct snap_test_state {
    i32 ticks;
    i32 jumps;
    bmf_rng rng;
    i32 pos_count;
    bmf_v2 pos[8];       // random walk: every tick changes every element
    f32 numerator;
    f32 divisor;
    f32 ratio;           // numerator / divisor every tick: 0 / 0 puts a NaN into the state
} snap_test_state;

static const bmf_field SNAP_TEST_FIELDS[] = {
    { "ticks", BMF_T_I32, offsetof(snap_test_state, ticks), 1, 0 },
    { "jumps", BMF_T_I32, offsetof(snap_test_state, jumps), 1, 0 },
    { "rng", BMF_T_RNG, offsetof(snap_test_state, rng), 1, 0 },
    { "pos_count", BMF_T_I32, offsetof(snap_test_state, pos_count), 1, 0 },
    { "pos", BMF_T_V2, offsetof(snap_test_state, pos), 8, "pos_count" },
    { "numerator", BMF_T_F32, offsetof(snap_test_state, numerator), 1, 0 },
    { "divisor", BMF_T_F32, offsetof(snap_test_state, divisor), 1, 0 },
    { "ratio", BMF_T_F32, offsetof(snap_test_state, ratio), 1, 0 },
};

static const bmf_action SNAP_TEST_ACTIONS[] = {
    { "jump", BMF_ACTION_BUTTON, { BMF_KEY_SPACE } },
};

static void snap_test_init(bmf_ctx* cx, void* state, u64 seed) {
    snap_test_state* s = (snap_test_state*)state;
    bmf_rng_seed(&s->rng, seed, 1);
    s->pos_count = 8;
    s->numerator = 1.0f;
    s->divisor = 2.0f;
}

static void snap_test_tick(bmf_ctx* cx, void* state, const bmf_input* in) {
    snap_test_state* s = (snap_test_state*)state;
    s->ticks++;
    s->jumps += bmf_input_pressed(in, 0);
    for (i32 i = 0; i < s->pos_count; i++) {
        f32 dx = bmf_rng_range_f32(&s->rng, -0.5f, 0.5f);
        f32 dy = bmf_rng_range_f32(&s->rng, -0.5f, 0.5f);
        s->pos[i] = bmf_v2_make(s->pos[i].x + dx, s->pos[i].y + dy + (f32)s->jumps);
    }
    s->ratio = s->numerator / s->divisor;
}

static void snap_test_draw(bmf_ctx* cx, const void* state, f32 alpha) {}

static bmf_game_desc snap_test_game(void) {
    bmf_game_desc game;
    bmf_memset(&game, 0, sizeof game);
    game.api_version = BMF_API_VERSION;
    game.name = "snap_test";
    game.state_size = (i32)sizeof(snap_test_state);
    game.fields = SNAP_TEST_FIELDS;
    game.field_count = (i32)(sizeof SNAP_TEST_FIELDS / sizeof SNAP_TEST_FIELDS[0]);
    game.state_layout_hash = bmf_fields_hash(game.fields, game.field_count);
    game.actions = SNAP_TEST_ACTIONS;
    game.action_count = (i32)(sizeof SNAP_TEST_ACTIONS / sizeof SNAP_TEST_ACTIONS[0]);
    game.init = snap_test_init;
    game.tick = snap_test_tick;
    game.draw = snap_test_draw;
    return game;
}

// ---------------------------------------------------------------- helpers
static void snap_test_start(bmf_test* t, u64 seed) {
    bmf_game_desc game = snap_test_game();
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

static void snap_test_finish(void) {
    bmf_protocol_end();
    bmf_core_stop();
}

// The `hash` response for the current state: {"ok":1,"tick":T,"hash":"16 hex"} into out.
static void snap_test_hash(bmf_test* t, char* out, i32 capacity, i32 line) {
    replay_test_run("hash");
    bmf_fmt_buf(out, capacity, "%s", protocol_capture.text);
    char expected[96];
    bmf_fmt_buf(expected, (i32)sizeof expected, "{\"ok\":1,\"tick\":%li,\"hash\":\"%016lx\"}\n", bmf_core_ctx()->tick, bmf_core_state_hash());
    bmf_check(t, bmf_cstr_eq(out, expected), __FILE__, line, out);
}

// ---------------------------------------------------------------- snap.roundtrip
static void test_snap_roundtrip(bmf_test* t) {
    char at_save[128], first[128], again[128], now[128];
    snap_test_start(t, 5);
    replay_test_run("press jump 3");
    replay_test_run("step 10");
    protocol_expect(t, "snap save a", "{\"ok\":1,\"name\":\"a\",\"tick\":10}\n", __LINE__);
    snap_test_hash(t, at_save, (i32)sizeof at_save, __LINE__);
    replay_test_run("step 100");
    snap_test_hash(t, first, (i32)sizeof first, __LINE__);
    bmf_check(t, core_test_contains(first, "\"tick\":110,"), __FILE__, __LINE__, first);
    replay_test_run("set divisor 3");   // a write after the save is undone by the load
    protocol_expect(t, "snap load a", "{\"ok\":1,\"name\":\"a\",\"tick\":10}\n", __LINE__);
    snap_test_hash(t, now, (i32)sizeof now, __LINE__);
    bmf_check(t, bmf_cstr_eq(now, at_save), __FILE__, __LINE__, "the load restores the saved hash");
    protocol_expect(t, "get divisor", "{\"ok\":1,\"path\":\"divisor\",\"value\":2}\n", __LINE__);
    replay_test_run("step 100");
    snap_test_hash(t, again, (i32)sizeof again, __LINE__);
    char message[300];
    bmf_fmt_buf(message, (i32)sizeof message, "save, step 100, load, step 100: %s vs %s", first, again);
    bmf_check(t, bmf_cstr_eq(first, again), __FILE__, __LINE__, message);
    // A snapshot can be loaded again and again, and `hash` matches bmf_core_state_hash.
    protocol_expect(t, "snap load a", "{\"ok\":1,\"name\":\"a\",\"tick\":10}\n", __LINE__);
    snap_test_hash(t, now, (i32)sizeof now, __LINE__);
    bmf_check(t, bmf_cstr_eq(now, at_save), __FILE__, __LINE__, "second load");
    snap_test_finish();
}

// ---------------------------------------------------------------- snap.slots
static void test_snap_slots(bmf_test* t) {
    char request[64], expected[128];
    snap_test_start(t, 1);
    protocol_expect(t, "snap save abcdefghijklmnopqrstuvwxyz01234", "{\"ok\":1,\"name\":\"abcdefghijklmnopqrstuvwxyz01234\",\"tick\":0}\n", __LINE__);   // 31: the longest
    protocol_expect(t, "snap save abcdefghijklmnopqrstuvwxyz012345",
                    "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"snapshot names are 1..31 characters of A-Z a-z 0-9 _ . -\"}\n", __LINE__);
    for (i32 i = 0; i < 15; i++) {
        bmf_fmt_buf(request, (i32)sizeof request, "snap save s%i", i);
        bmf_fmt_buf(expected, (i32)sizeof expected, "{\"ok\":1,\"name\":\"s%i\",\"tick\":%i}\n", i, i);
        protocol_expect(t, request, expected, __LINE__);
        replay_test_run("step 1");
    }
    protocol_expect(t, "snap save s15", "{\"ok\":0,\"error\":\"too_many_snaps\",\"detail\":\"all 16 snapshot slots are taken (save over an existing name)\"}\n", __LINE__);
    protocol_expect(t, "snap save s3", "{\"ok\":1,\"name\":\"s3\",\"tick\":15}\n", __LINE__);   // a full table still overwrites
    protocol_expect(t, "snap load s4", "{\"ok\":1,\"name\":\"s4\",\"tick\":4}\n", __LINE__);
    protocol_expect(t, "snap load s3", "{\"ok\":1,\"name\":\"s3\",\"tick\":15}\n", __LINE__);
    protocol_expect(t, "get ticks", "{\"ok\":1,\"path\":\"ticks\",\"value\":15}\n", __LINE__);

    // A reload that changes the layout (here: one field less) drops the snapshot on its load.
    char error[BMF_CORE_ERROR_MAX];
    bmf_game_desc fewer = snap_test_game();
    fewer.field_count--;
    fewer.state_layout_hash = bmf_fields_hash(fewer.fields, fewer.field_count);
    bmf_check_eq_i64(t, bmf_core_swap_game(&fewer, error, (i32)sizeof error), BMF_CORE_SWAP_RESET, __FILE__, __LINE__, error);
    protocol_expect(t, "snap load s1", "{\"ok\":0,\"error\":\"unknown_snap\",\"detail\":\"snapshot 's1' was saved with another state layout (a reload reset the game); it is dropped\"}\n", __LINE__);
    protocol_expect(t, "snap load s1", "{\"ok\":0,\"error\":\"unknown_snap\",\"detail\":\"no snapshot 's1' (snap save s1 first)\"}\n", __LINE__);
    protocol_expect(t, "snap save s15", "{\"ok\":1,\"name\":\"s15\",\"tick\":0}\n", __LINE__);   // the dropped slot is free again
    snap_test_finish();

    // A new run starts without snapshots.
    snap_test_start(t, 1);
    protocol_expect(t, "snap load s2", "{\"ok\":0,\"error\":\"unknown_snap\",\"detail\":\"no snapshot 's2' (snap save s2 first)\"}\n", __LINE__);
    snap_test_finish();
}

// ---------------------------------------------------------------- snap.nonfinite
static void crash_test_delete_dump(const char* text);   // tests/test_crash.c

static void test_snap_nonfinite(bmf_test* t) {
    char hash[128];
    snap_test_start(t, 2);
    replay_test_run("step 5");
    snap_test_hash(t, hash, (i32)sizeof hash, __LINE__);
    // `set` refuses NaN itself (D-37 rule 11), so the NaN comes from the game: 0 / 0 in the tick.
    protocol_expect(t, "set divisor nan", "{\"ok\":0,\"error\":\"bad_value\",\"detail\":\"'nan': NaN and Inf are not allowed in the state (D-37 rule 11)\"}\n", __LINE__);
    replay_test_run("set numerator 0");
    replay_test_run("set divisor 0");
    replay_test_run("step 1");
    protocol_expect(t, "get ratio", "{\"ok\":1,\"path\":\"ratio\",\"value\":\"NaN\"}\n", __LINE__);
    protocol_expect(t, "hash", "{\"ok\":0,\"error\":\"nonfinite_state\",\"detail\":\"'ratio' is NaN or Inf at tick 6: never allowed in the state (D-37 rule 11)\"}\n", __LINE__);
    replay_test_run("set numerator 1");   // 1 / 0: Inf
    replay_test_run("step 1");
    protocol_expect(t, "hash", "{\"ok\":0,\"error\":\"nonfinite_state\",\"detail\":\"'ratio' is NaN or Inf at tick 7: never allowed in the state (D-37 rule 11)\"}\n", __LINE__);
    replay_test_run("set divisor 4");
    replay_test_run("step 1");
    snap_test_hash(t, hash, (i32)sizeof hash, __LINE__);   // finite again before the checkpoint at tick 60
    snap_test_finish();

#if BMF_OS_WINDOWS && BMF_DEBUG
    // Every other hash pass asserts in debug builds: here the session recording's checkpoint at tick 60.
    static char out[1 << 14];
    const char* script = "set numerator 0\nset divisor 0\nstep 70\nhash\n";
    u32 code = test_spawn_game("snap", "--headless --control --no-gpu", script, bmf_cstr_len(script), out, (i32)sizeof out);
    char message[1 << 14];
    bmf_fmt_buf(message, (i32)sizeof message, "exit 0x%x, output:\n%s", code, out);
    bmf_check(t, code != 0 && code != 0xFFFFFFFFu &&
                 core_test_contains(out, "{\"event\":\"crash\",\"reason\":\"assert\",\"detail\":\"NaN or Inf in the state: 'ratio' at tick 60 (D-37 rule 11)\""),
              __FILE__, __LINE__, message);
    bmf_check(t, !core_test_contains(out, "\"tick\":70"), __FILE__, __LINE__, "stopped at the checkpoint");
    crash_test_delete_dump(out);   // tests/test_crash.c
#endif
}

// ---------------------------------------------------------------- snap.recordings
static void test_snap_recordings(bmf_test* t) {
    char path[1200], session_path[1200], request[1300], error[256];
    replay_test_path(path, (i32)sizeof path, "snap_record.bmfr");
    replay_test_path(session_path, (i32)sizeof session_path, "snap_session.bmfr");
    snap_test_start(t, 4);
    bmf_fmt_buf(request, (i32)sizeof request, "record start %s", path);
    replay_test_run(request);
    replay_test_run("step 20");
    replay_test_run("snap save a");
    replay_test_run("step 30");
    replay_test_run("snap load a");
    protocol_expect(t, "record stop", "{\"ok\":0,\"error\":\"record_failed\",\"detail\":\"the recording broke: a snapshot was loaded during the recording\"}\n", __LINE__);
    // The session recording restarted at the load (tick 20): it replays from there.
    replay_test_run("press jump 2");
    replay_test_run("step 75");
    u64 expected = bmf_core_state_hash();
    bmf_check_eq_i64(t, bmf_replay_write_session(session_path, error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, error);
    // A snapshot load while a replay plays stops the replay first.
    bmf_fmt_buf(request, (i32)sizeof request, "replay %s", session_path);
    replay_test_run(request);
    bmf_check(t, core_test_contains(protocol_capture.text, "\"start_tick\":20,\"end_tick\":95,\"checkpoints\":3}"), __FILE__, __LINE__, protocol_capture.text);   // 20, 80, 95
    replay_test_run("step 2");
    protocol_expect(t, "snap load a", "{\"event\":\"replay\",\"state\":\"stopped\",\"reason\":\"snap load\"}\n{\"ok\":1,\"name\":\"a\",\"tick\":20}\n", __LINE__);
    // A replay loaded during an explicit recording breaks it too.
    bmf_fmt_buf(request, (i32)sizeof request, "record start %s", path);
    replay_test_run(request);
    bmf_fmt_buf(request, (i32)sizeof request, "replay %s", session_path);
    replay_test_run(request);
    protocol_expect(t, "record stop", "{\"ok\":0,\"error\":\"record_failed\",\"detail\":\"the recording broke: a replay was loaded during the recording\"}\n", __LINE__);
    snap_test_finish();

    // A fresh run (another seed) verifies the session file.
    snap_test_start(t, 77);
    bmf_fmt_buf(request, (i32)sizeof request, "replay %s", session_path);
    replay_test_run(request);
    protocol_capture.length = 0;
    replay_test_run("step 75");
    bmf_check(t, core_test_contains(protocol_capture.text, "{\"event\":\"replay\",\"verify\":\"ok\",\"ticks\":75,\"checkpoints\":3,"), __FILE__, __LINE__, protocol_capture.text);
    bmf_check(t, bmf_core_state_hash() == expected, __FILE__, __LINE__, "the session after a snapshot load replays to the same state");
    snap_test_finish();
    bmf_platform_file_delete(session_path);
    bmf_platform_file_delete(path);
}
