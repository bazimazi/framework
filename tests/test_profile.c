// Tests for src/debug/profile.c and log.c (task 1A-36): a busy loop of known cost measured within
// 5%, the report's window semantics, the memory report, the log ring (levels, drain, overflow) and
// its stderr echo in a --control process. Zone-nesting errors are tested in tests/test_crash.c
// (they stop a debug build). Uses the protocol helpers of tests/test_protocol.c and test_replay.c.

// ---------------------------------------------------------------- the profile test game
typedef struct profile_test_state {
    i32 ticks;
    i32 spin_us;      // busy time per tick inside the zone "spin" (set by the test)
    i32 slow_spin_us; // when not 0: the busy time of every 4th tick instead
    i32 log_every;    // 0 = no logging; else an info message every log_every ticks and a warn at tick 25
} profile_test_state;

static const bmf_field PROFILE_TEST_FIELDS[] = {
    { "ticks", BMF_T_I32, offsetof(profile_test_state, ticks), 1, 0 },
    { "spin_us", BMF_T_I32, offsetof(profile_test_state, spin_us), 1, 0 },
    { "slow_spin_us", BMF_T_I32, offsetof(profile_test_state, slow_spin_us), 1, 0 },
    { "log_every", BMF_T_I32, offsetof(profile_test_state, log_every), 1, 0 },
};

static const char* const PROFILE_TEST_ZONES[] = { "spin" };

static void profile_test_init(bmf_ctx* cx, void* state, u64 seed) {}

// Test fixture only: the spin reads the wall clock (never do this in a real tick, R2). Its cost
// is known from the clock, which is what the profiler's counter must agree with.
static void profile_test_tick(bmf_ctx* cx, void* state, const bmf_input* in) {
    profile_test_state* s = (profile_test_state*)state;
    s->ticks++;
    bmf_zone_begin(BMF_ZONE_GAME_FIRST);
    i32 spin = s->slow_spin_us && s->ticks % 4 == 0 ? s->slow_spin_us : s->spin_us;
    if (spin > 0) {
        i64 end = bmf_platform_time_ns() + (i64)spin * 1000;
        while (bmf_platform_time_ns() < end) {}
    }
    bmf_zone_end(BMF_ZONE_GAME_FIRST);
    if (s->log_every > 0 && s->ticks % s->log_every == 0) bmf_log(BMF_LOG_INFO, "tick %i", s->ticks);
    if (s->log_every > 0 && s->ticks == 25) bmf_log(BMF_LOG_WARN, "warn at %i: \"quoted\"", s->ticks);
}

static void profile_test_draw(bmf_ctx* cx, const void* state, f32 alpha) {}

static bmf_game_desc profile_test_game(void) {
    bmf_game_desc game;
    bmf_memset(&game, 0, sizeof game);
    game.api_version = BMF_API_VERSION;
    game.name = "profile_test";
    game.state_size = (i32)sizeof(profile_test_state);
    game.fields = PROFILE_TEST_FIELDS;
    game.field_count = (i32)(sizeof PROFILE_TEST_FIELDS / sizeof PROFILE_TEST_FIELDS[0]);
    game.state_layout_hash = bmf_fields_hash(game.fields, game.field_count);
    game.zone_names = PROFILE_TEST_ZONES;
    game.zone_name_count = 1;
    game.init = profile_test_init;
    game.tick = profile_test_tick;
    game.draw = profile_test_draw;
    return game;
}

// ---------------------------------------------------------------- helpers
static void profile_test_start(bmf_test* t) {
    bmf_game_desc game = profile_test_game();
    char error[BMF_CORE_ERROR_MAX];
    bmf_core_options options;
    bmf_memset(&options, 0, sizeof options);
    options.time_scale = 1.0f;
    options.control = 1;
    bmf_check_eq_i64(t, bmf_core_start(&game, &options, error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, error);
    bmf_core_set_view((bmf_draw2d_view){ 1280, 720, 1.0f });
    bmf_protocol_begin(protocol_test_output, 0);
}

// The number after `"key":` inside the object of zone `zone` in the captured response; -1 if absent.
static f32 profile_test_number(const char* zone, const char* key) {
    char pattern[64];
    bmf_fmt_buf(pattern, (i32)sizeof pattern, "\"%s\":{", zone);
    bmf_str text = bmf_str_from_cstr(protocol_capture.text);
    i32 at = bmf_str_find(text, bmf_str_from_cstr(pattern), 0);
    if (at < 0) return -1.0f;
    bmf_fmt_buf(pattern, (i32)sizeof pattern, "\"%s\":", key);
    i32 k = bmf_str_find(text, bmf_str_from_cstr(pattern), at);
    if (k < 0) return -1.0f;
    i32 start = k + bmf_cstr_len(pattern), end = start;
    while (end < text.len && text.ptr[end] != ',' && text.ptr[end] != '}') end++;
    f32 value = -1.0f;
    if (bmf_parse_f32(bmf_str_slice(text, start, end), &value) != BMF_OK) return -1.0f;
    return value;
}

// ---------------------------------------------------------------- profile.busy_loop
static void test_profile_busy_loop(bmf_test* t) {
    profile_test_start(t);
    i32 costs[] = { 50, 200, 1000 };
    for (i32 c = 0; c < 3; c++) {
        char request[64], message[2048];
        bmf_fmt_buf(request, (i32)sizeof request, "set spin_us %i", costs[c]);
        replay_test_run(request);
        replay_test_run("profile 60");
        f32 p50 = profile_test_number("spin", "p50_us");
        f32 mean = profile_test_number("spin", "mean_us");
        f32 calls = profile_test_number("spin", "calls");
        f32 expected = (f32)costs[c];
        bmf_fmt_buf(message, (i32)sizeof message, "%i us spin: p50 %f, mean %f, calls %f: %s", costs[c], (f64)p50, (f64)mean, (f64)calls, protocol_capture.text);
        // The spin ends at the first clock read past its end: up to one clock read (~30 ns) long.
        bmf_check(t, calls == 60.0f, __FILE__, __LINE__, message);
        bmf_check(t, p50 >= expected * 0.95f && p50 <= expected * 1.05f, __FILE__, __LINE__, message);
        // The mean takes every call, so one preempted tick can move it: only a lower bound here.
        bmf_check(t, mean >= expected * 0.95f, __FILE__, __LINE__, message);
        // The engine's tick zone contains the spin.
        bmf_check(t, profile_test_number("tick", "p50_us") >= p50 && profile_test_number("tick", "calls") == 60.0f, __FILE__, __LINE__, message);
    }
    // Two costs, 3 of 4 ticks 100 us, every 4th 300 us: p50 is the lower, p99 and max the higher,
    // the mean 150 us. Preemption (seen: single ticks 300 -> 900 us under load) only makes calls
    // longer, so p99, max and the mean get lower bounds (the mean a loose upper one).
    replay_test_run("set spin_us 100");
    replay_test_run("set slow_spin_us 300");
    replay_test_run("profile 80");
    char message[2048];
    f32 p50 = profile_test_number("spin", "p50_us"), p99 = profile_test_number("spin", "p99_us");
    f32 mean = profile_test_number("spin", "mean_us"), max = profile_test_number("spin", "max_us");
    bmf_fmt_buf(message, (i32)sizeof message, "100/300 us: p50 %f, p99 %f, mean %f, max %f", (f64)p50, (f64)p99, (f64)mean, (f64)max);
    bmf_check(t, p50 >= 95.0f && p50 <= 105.0f && p99 >= 285.0f, __FILE__, __LINE__, message);   // preempted ticks only add: no upper bounds but p50's
    bmf_check(t, mean >= 142.0f && mean <= 250.0f && max >= 285.0f, __FILE__, __LINE__, message);
    bmf_protocol_end();
    bmf_core_stop();
}

// ---------------------------------------------------------------- profile.report
static void test_profile_report(bmf_test* t) {
    profile_test_start(t);
    replay_test_run("step 3");        // ticks before `profile N` are not in its window
    replay_test_run("profile 10");
    char message[4096];
    bmf_fmt_buf(message, (i32)sizeof message, "%s", protocol_capture.text);
    bmf_check(t, core_test_contains(message, "{\"ok\":1,\"tick\":13,\"ticks\":10,\"zones\":{\"tick\":{\"calls\":10,"), __FILE__, __LINE__, message);
    bmf_check(t, core_test_contains(message, "\"debug_hooks\":{\"calls\":20,") && core_test_contains(message, "\"spin\":{\"calls\":10,"), __FILE__, __LINE__, message);
    bmf_check(t, core_test_contains(message, "},\"frames\":0,\"draw_calls\":") && core_test_contains(message, ",\"nesting_errors\":0,\"counter_hz\":"), __FILE__, __LINE__, message);
    bmf_check(t, !core_test_contains(message, "\"draw\":{"), __FILE__, __LINE__, "no frames: no draw zone");
    // `profile 0` reports the window since the last report: nothing ran.
    replay_test_run("profile 0");
    bmf_check(t, core_test_contains(protocol_capture.text, "{\"ok\":1,\"tick\":13,\"ticks\":0,\"zones\":{},\"frames\":0,"), __FILE__, __LINE__, protocol_capture.text);
    replay_test_run("step 3");
    replay_test_run("profile 0");
    bmf_check(t, core_test_contains(protocol_capture.text, "\"zones\":{\"tick\":{\"calls\":3,"), __FILE__, __LINE__, protocol_capture.text);
    bmf_check(t, bmf_profile_depth() == 0, __FILE__, __LINE__, "every zone closed");

    // mem: the game's arenas, then the debug tools'. The permanent arena gets a high water above its use.
    bmf_arena* permanent = &bmf_core_memory()->permanent;
    i64 mark = bmf_arena_mark(permanent);
    bmf_arena_push_aligned(permanent, 5000, 16);
    bmf_arena_pop_to(permanent, mark);
    replay_test_run("snap save a");
    replay_test_run("mem");
    bmf_fmt_buf(message, (i32)sizeof message, "%s", protocol_capture.text);
    bmf_check(t, core_test_contains(message, "{\"ok\":1,\"arenas\":[{\"name\":\"permanent\",\"reserved\":"), __FILE__, __LINE__, message);
    const char* names[] = { "game_state", "level", "frame", "scratch.0", "scratch.1", "replay.session.stream", "replay.session.last_state", "snap.a" };
    for (i32 i = 0; i < (i32)(sizeof names / sizeof names[0]); i++) {
        char pattern[64];
        bmf_fmt_buf(pattern, (i32)sizeof pattern, "{\"name\":\"%s\",", names[i]);
        bmf_check(t, core_test_contains(message, pattern), __FILE__, __LINE__, pattern);
    }
    // The numbers agree with the arena itself.
    const bmf_arena* checked[2] = { &bmf_core_memory()->game_state, permanent };
    const char* checked_names[2] = { "game_state", "permanent" };
    for (i32 i = 0; i < 2; i++) {
        char expected[256];
        bmf_fmt_buf(expected, (i32)sizeof expected, "{\"name\":\"%s\",\"reserved\":%li,\"committed\":%li,\"used\":%li,\"high_water\":%li}",
                    checked_names[i], checked[i]->reserved, checked[i]->committed, checked[i]->used, checked[i]->high_water);
        bmf_check(t, core_test_contains(message, expected), __FILE__, __LINE__, expected);
    }
    bmf_check(t, permanent->high_water >= mark + 5000 && permanent->used == mark, __FILE__, __LINE__, "high water above use");
    bmf_protocol_end();
    bmf_core_stop();
}

// ---------------------------------------------------------------- log.ring
static void test_log_ring(bmf_test* t) {
    profile_test_start(t);
    replay_test_run("set log_every 10");
    replay_test_run("step 30");
    protocol_expect(t, "log warn", "{\"ok\":1,\"entries\":[{\"tick\":24,\"level\":\"warn\",\"msg\":\"warn at 25: \\\"quoted\\\"\"}],\"dropped\":0,\"counts\":{\"debug\":0,\"info\":3,\"warn\":1,\"error\":0}}\n", __LINE__);
    // Drained: the info messages went with it.
    protocol_expect(t, "log", "{\"ok\":1,\"entries\":[],\"dropped\":0,\"counts\":{\"debug\":0,\"info\":3,\"warn\":1,\"error\":0}}\n", __LINE__);
    replay_test_run("step 10");
    protocol_expect(t, "log", "{\"ok\":1,\"entries\":[{\"tick\":39,\"level\":\"info\",\"msg\":\"tick 40\"}],\"dropped\":0,\"counts\":{\"debug\":0,\"info\":4,\"warn\":1,\"error\":0}}\n", __LINE__);
    // Overflow: 300 messages keep the newest 256; 44 are dropped.
    for (i32 i = 0; i < 300; i++) bmf_log(BMF_LOG_DEBUG, "m%i", i);
    replay_test_run("log");
    bmf_check(t, core_test_contains(protocol_capture.text, "{\"ok\":1,\"entries\":[{\"tick\":40,\"level\":\"debug\",\"msg\":\"m44\"},"), __FILE__, __LINE__, protocol_capture.text);
    bmf_check(t, core_test_contains(protocol_capture.text, "{\"tick\":40,\"level\":\"debug\",\"msg\":\"m299\"}],\"dropped\":44,\"counts\":{\"debug\":300,"), __FILE__, __LINE__, protocol_capture.text);
    // A long message is cut to 255 bytes.
    char long_text[400];
    for (i32 i = 0; i < 399; i++) long_text[i] = 'x';
    long_text[399] = 0;
    bmf_log(BMF_LOG_ERROR, "%s", long_text);
    replay_test_run("log error");
    char expected[300];
    long_text[255] = 0;   // what the ring keeps
    bmf_fmt_buf(expected, (i32)sizeof expected, "\"msg\":\"%s\"}]", long_text);
    bmf_check(t, core_test_contains(protocol_capture.text, expected), __FILE__, __LINE__, protocol_capture.text);
    bmf_protocol_end();
    bmf_core_stop();

    // A new run starts with an empty log.
    profile_test_start(t);
    protocol_expect(t, "log", "{\"ok\":1,\"entries\":[],\"dropped\":0,\"counts\":{\"debug\":0,\"info\":0,\"warn\":0,\"error\":0}}\n", __LINE__);
    bmf_protocol_end();
    bmf_core_stop();
}

// ---------------------------------------------------------------- log.echo
static void test_log_echo(bmf_test* t) {
#if BMF_OS_WINDOWS
    static char out[1 << 16];
    const char* script = "set log_every 100\nstep 30\nprofile 5\nquit\n";
    u32 code = test_spawn_game("profile", "--headless --control --no-gpu", script, bmf_cstr_len(script), out, (i32)sizeof out);
    char message[1 << 16];
    bmf_fmt_buf(message, (i32)sizeof message, "exit %u, output:\n%s", code, out);
    bmf_check(t, code == 0 && core_test_contains(out, "{\"event\":\"log\",\"level\":\"warn\",\"tick\":24,\"msg\":\"warn at 25: \\\"quoted\\\"\"}\n"), __FILE__, __LINE__, message);
    // A real run has frames: the report of a process shows them.
    bmf_check(t, core_test_contains(out, "\"spin\":{\"calls\":5,") && core_test_contains(out, "\"frames\":"), __FILE__, __LINE__, message);
#else
    bmf_fmt_buf(t->note, (i32)sizeof t->note, "process tests: Windows only so far");
#endif
}
