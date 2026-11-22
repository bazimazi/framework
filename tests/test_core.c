// test_core.c: tests for src/core (task 1A-23): game description checks, command-line flags,
// start and init, the fixed-step clock, ticking with input and sfx events, the game swap of hot
// reload, and whole frames on the null and the D3D11 WARP backend. The DLL copy/load/watch part
// of hot reload needs real DLLs: it is checked by running games/_loop (see docs/progress.md).

// ---------------------------------------------------------------- the test game
typedef struct core_test_state {
    u64 seed;
    i32 init_calls;
    b32 zero_at_init;          // the block was all zeros when init ran
    i64 ticks_seen[8];         // cx->tick of the first 8 ticks
    i32 tick_calls;
    i32 sfx_per_tick;          // set by tests
    u64 down[8];               // bmf_input bits of the first 8 ticks
    u64 pressed[8];
    u64 released[8];
    u32 dt_bits;
    i32 value;                 // set by tests (kept / reset checks)
    u8 padding[40];
} core_test_state;

static struct {
    i32 draw_calls;
    f32 last_alpha;
    bmf_v2 last_view_size;
    bmf_color circle_color;
    b32 sfx_in_draw;           // expected-crash test
} core_test_log;

static void core_test_init(bmf_ctx* cx, void* state, u64 seed) {
    core_test_state* s = (core_test_state*)state;
    b32 zero = 1;
    for (i32 i = 0; i < (i32)sizeof *s; i++) zero &= ((const u8*)state)[i] == 0;
    s->zero_at_init = zero;
    s->init_calls++;
    s->seed = seed;
    s->dt_bits = bmf_f32_bits(cx->dt);
}

static void core_test_tick(bmf_ctx* cx, void* state, const bmf_input* in) {
    core_test_state* s = (core_test_state*)state;
    if (s->tick_calls < 8) {
        s->ticks_seen[s->tick_calls] = cx->tick;
        s->down[s->tick_calls] = in->down;
        s->pressed[s->tick_calls] = in->pressed;
        s->released[s->tick_calls] = in->released;
    }
    s->tick_calls++;
    for (i32 i = 0; i < s->sfx_per_tick; i++) bmf_sfx_play(cx, (u32)i, 1.0f, 0.0f);
}

static void core_test_draw(bmf_ctx* cx, const void* state, f32 alpha) {
    core_test_log.draw_calls++;
    core_test_log.last_alpha = alpha;
    core_test_log.last_view_size = cx->view_size;
    if (core_test_log.circle_color) bmf_draw_circle(0, bmf_v2_make(0.0f, 0.0f), 1.0f, core_test_log.circle_color);
    if (core_test_log.sfx_in_draw) bmf_sfx_play(cx, 1, 1.0f, 0.0f);
}

static const bmf_field CORE_TEST_FIELDS[] = {
    { "value", BMF_T_I32, offsetof(core_test_state, value), 1, 0 },
    { "ticks_seen", BMF_T_I64, offsetof(core_test_state, ticks_seen), 8, "tick_calls" },
    { "tick_calls", BMF_T_I32, offsetof(core_test_state, tick_calls), 1, 0 },
};

static const bmf_action CORE_TEST_ACTIONS[] = {
    { "fire", BMF_ACTION_BUTTON, { BMF_KEY_F } },
    { "move", BMF_ACTION_AXIS2, { BMF_KEYS_WASD } },
};

static bmf_game_desc core_test_game(void) {
    bmf_game_desc game;
    bmf_memset(&game, 0, sizeof game);
    game.api_version = BMF_API_VERSION;
    game.name = "core_test";
    game.state_size = (i32)sizeof(core_test_state);
    game.state_layout_hash = bmf_fields_hash(CORE_TEST_FIELDS, 3);
    game.fields = CORE_TEST_FIELDS;
    game.field_count = 3;
    game.actions = CORE_TEST_ACTIONS;
    game.action_count = 2;
    game.init = core_test_init;
    game.tick = core_test_tick;
    game.draw = core_test_draw;
    return game;
}

static bmf_core_options core_test_options(void) {
    bmf_core_options options;
    bmf_memset(&options, 0, sizeof options);
    options.time_scale = 1.0f;
    return options;
}

static core_test_state* core_test_start(bmf_test* t, const bmf_game_desc* game, const bmf_core_options* options) {
    char error[BMF_CORE_ERROR_MAX];
    bmf_err err = bmf_core_start(game, options, error, (i32)sizeof error);
    bmf_check_eq_i64(t, err, BMF_OK, __FILE__, __LINE__, error);
    bmf_memset(&core_test_log, 0, sizeof core_test_log);
    return (core_test_state*)bmf_core_state();
}

static b32 core_test_contains(const char* text, const char* part) {
    i32 n = bmf_cstr_len(text), m = bmf_cstr_len(part);
    for (i32 i = 0; i + m <= n; i++)
        if (bmf_memcmp(text + i, part, m) == 0) return 1;
    return 0;
}

// ---------------------------------------------------------------- core.check_game
static void core_test_noop_init(bmf_ctx* cx, void* state, u64 seed) {}

static void test_core_check_game(bmf_test* t) {
    char error[BMF_CORE_ERROR_MAX];
    bmf_game_desc valid = core_test_game();
    bmf_check_eq_i64(t, bmf_core_check_game(&valid, error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, "the test game is valid");
    bmf_check(t, error[0] == 0, __FILE__, __LINE__, "no message when valid");
    bmf_check_eq_i64(t, bmf_core_check_game(0, error, (i32)sizeof error), BMF_ERR_INVALID, __FILE__, __LINE__, "no description");

    // A count field may come after the array it counts; tick_rate 0 means 60.
    static const bmf_field later_count[] = {
        { "items", BMF_T_U8, 0, 16, "item_count" },
        { "item_count", BMF_T_I32, 16, 1, 0 },
    };
    bmf_game_desc ok = valid;
    ok.fields = later_count;
    ok.field_count = 2;
    ok.state_layout_hash = bmf_fields_hash(later_count, 2);
    ok.tick_rate = 0;
    bmf_check_eq_i64(t, bmf_core_check_game(&ok, error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, error);
    ok.tick_rate = 30;
    bmf_check_eq_i64(t, bmf_core_check_game(&ok, error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, "tick rate 30");
    ok.tick_rate = 240;
    bmf_check_eq_i64(t, bmf_core_check_game(&ok, error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, "tick rate 240");

    static const bmf_field bad_name[] = { { "bad name", BMF_T_I32, 0, 1, 0 } };
    static const bmf_field duplicate[] = { { "a", BMF_T_I32, 0, 1, 0 }, { "a", BMF_T_I32, 4, 1, 0 } };
    static const bmf_field bad_type[] = { { "a", BMF_T_COUNT, 0, 1, 0 } };
    static const bmf_field reserved[] = { { "tick", BMF_T_I64, 0, 1, 0 } };
    static const bmf_field zero_count[] = { { "a", BMF_T_I32, 0, 0, 0 } };
    static const bmf_field past_end[] = { { "a", BMF_T_V2, (i32)sizeof(core_test_state) - 4, 1, 0 } };
    static const bmf_field negative[] = { { "a", BMF_T_I32, -4, 1, 0 } };
    static const bmf_field misaligned[] = { { "a", BMF_T_I64, 4, 1, 0 } };
    static const bmf_field scalar_count[] = { { "a", BMF_T_I32, 0, 1, "b" }, { "b", BMF_T_I32, 4, 1, 0 } };
    static const bmf_field missing_count[] = { { "a", BMF_T_I32, 0, 4, "nope" } };
    static const bmf_field float_count[] = { { "a", BMF_T_I32, 0, 4, "b" }, { "b", BMF_T_F32, 16, 1, 0 } };
    static const bmf_action duplicate_actions[] = { { "a", BMF_ACTION_BUTTON, { 0 } }, { "a", BMF_ACTION_BUTTON, { 0 } } };

    struct { const char* what; const char* expected; } cases[] = {
        { "api_version", "game: api_version 7, but the runtime is 0" },
        { "name", "game: name must be 1..63 bytes" },
        { "state_size", "game \"core_test\": state_size 0 out of range 1..1073741824" },
        { "tick_rate_low", "tick_rate 29 out of range 30..240 (0 = 60)" },
        { "tick_rate_high", "tick_rate 241 out of range" },
        { "memory", "memory.level = -1 out of range 0..4 GiB" },
        { "init", "the init callback is required" },
        { "tick", "the tick callback is required" },
        { "draw", "the draw callback is required" },
        { "no_fields", "field_count 2 out of range 0..4096 (or no table)" },
        { "bad_name", "field 0 \"bad name\": name must be 1..63 bytes of a-z A-Z 0-9 _ ." },
        { "duplicate", "field 1 \"a\": duplicate name (also field 0)" },
        { "bad_type", "field 0 \"a\": unknown type 12" },
        { "reserved", "field 0 \"tick\": the name is a built-in path" },
        { "layout_hash", "state_layout_hash 0x5 does not match the field table: set it to 0x" },
        { "zero_count", "field 0 \"a\": count 0 must be >= 1" },
        { "past_end", "are outside the state block (state_size 328)" },
        { "negative", "field 0 \"a\": bytes -4..0 are outside the state block" },
        { "misaligned", "field 0 \"a\": offset 4 is not a multiple of 8" },
        { "scalar_count", "field 0 \"a\": count_field is only for arrays (count >= 2)" },
        { "missing_count", "count_field \"nope\" is not a field" },
        { "float_count", "count_field \"b\" must be an I32 scalar" },
        { "actions", "game \"core_test\": input: action 1 \"a\": duplicate name (also action 0)" },
    };
    for (i32 c = 0; c < (i32)(sizeof cases / sizeof cases[0]); c++) {
        bmf_game_desc g = valid;
        const char* w = cases[c].what;
        if (bmf_cstr_eq(w, "api_version")) g.api_version = 7;
        if (bmf_cstr_eq(w, "name")) g.name = "";
        if (bmf_cstr_eq(w, "state_size")) g.state_size = 0;
        if (bmf_cstr_eq(w, "tick_rate_low")) g.tick_rate = 29;
        if (bmf_cstr_eq(w, "tick_rate_high")) g.tick_rate = 241;
        if (bmf_cstr_eq(w, "memory")) g.memory.level = -1;
        if (bmf_cstr_eq(w, "init")) g.init = 0;
        if (bmf_cstr_eq(w, "tick")) g.tick = 0;
        if (bmf_cstr_eq(w, "draw")) g.draw = 0;
        if (bmf_cstr_eq(w, "no_fields")) { g.fields = 0; g.field_count = 2; }
        if (bmf_cstr_eq(w, "bad_name")) { g.fields = bad_name; g.field_count = 1; }
        if (bmf_cstr_eq(w, "duplicate")) { g.fields = duplicate; g.field_count = 2; }
        if (bmf_cstr_eq(w, "bad_type")) { g.fields = bad_type; g.field_count = 1; }
        if (bmf_cstr_eq(w, "reserved")) { g.fields = reserved; g.field_count = 1; }
        if (bmf_cstr_eq(w, "layout_hash")) g.state_layout_hash = 5;
        if (bmf_cstr_eq(w, "zero_count")) { g.fields = zero_count; g.field_count = 1; }
        if (bmf_cstr_eq(w, "past_end")) { g.fields = past_end; g.field_count = 1; }
        if (bmf_cstr_eq(w, "negative")) { g.fields = negative; g.field_count = 1; }
        if (bmf_cstr_eq(w, "misaligned")) { g.fields = misaligned; g.field_count = 1; }
        if (bmf_cstr_eq(w, "scalar_count")) { g.fields = scalar_count; g.field_count = 2; }
        if (bmf_cstr_eq(w, "missing_count")) { g.fields = missing_count; g.field_count = 1; }
        if (bmf_cstr_eq(w, "float_count")) { g.fields = float_count; g.field_count = 2; }
        if (bmf_cstr_eq(w, "actions")) { g.actions = duplicate_actions; g.action_count = 2; }
        bmf_err err = bmf_core_check_game(&g, error, (i32)sizeof error);
        char message[BMF_CORE_ERROR_MAX + 128];
        bmf_fmt_buf(message, (i32)sizeof message, "%s: expected \"%s\" in \"%s\"", w, cases[c].expected, error);
        bmf_check(t, err == BMF_ERR_INVALID && core_test_contains(error, cases[c].expected), __FILE__, __LINE__, message);
    }
    bmf_check_eq_i64(t, (i64)sizeof(core_test_state), 328, __FILE__, __LINE__, "test state size (used in a message above)");
    (void)core_test_noop_init;
}

// ---------------------------------------------------------------- core.parse_args
static bmf_err core_test_parse(i32 count, const char* const* values, bmf_core_options* out, char* error) {
    return bmf_core_parse_args(count, values, out, error, BMF_CORE_ERROR_MAX);
}

static void test_core_parse_args(bmf_test* t) {
    char error[BMF_CORE_ERROR_MAX];
    bmf_core_options o;
    const char* none[] = { "game.exe" };
    bmf_check_eq_i64(t, core_test_parse(1, none, &o, error), BMF_OK, __FILE__, __LINE__, "no flags");
    bmf_check(t, !o.headless && !o.control && !o.no_gpu && !o.verify && !o.profile && !o.replay_path && o.seed == 0 && o.tick_rate == 0,
              __FILE__, __LINE__, "defaults are zero");
    bmf_check_eq_u64(t, bmf_f32_bits(o.time_scale), bmf_f32_bits(1.0f), __FILE__, __LINE__, "default time scale 1");

    const char* all[] = { "game.exe", "--headless", "--control", "--no-gpu", "--replay", "a b.bmfr", "--verify",
                          "--seed", "9223372036854775807", "--tick-rate", "120", "--scale", "0.5", "--tick-rate", "30" };
    bmf_check_eq_i64(t, core_test_parse(15, all, &o, error), BMF_OK, __FILE__, __LINE__, error);
    bmf_check(t, o.headless && o.control && o.no_gpu && o.verify, __FILE__, __LINE__, "switches");
    bmf_check(t, o.replay_path && bmf_cstr_eq(o.replay_path, "a b.bmfr"), __FILE__, __LINE__, "replay path");
    bmf_check_eq_u64(t, o.seed, 9223372036854775807ull, __FILE__, __LINE__, "largest seed");
    bmf_check_eq_i64(t, o.tick_rate, 30, __FILE__, __LINE__, "a repeated flag takes the last value");
    bmf_check_eq_u64(t, bmf_f32_bits(o.time_scale), bmf_f32_bits(0.5f), __FILE__, __LINE__, "time scale");
    const char* profiled[] = { "game.exe", "--headless", "--replay", "x.bmfr", "--profile" };
    bmf_check(t, core_test_parse(5, profiled, &o, error) == BMF_OK && o.profile && !o.verify, __FILE__, __LINE__, "--profile with --replay");

    struct { i32 count; const char* values[3]; const char* expected; } bad[] = {
        { 2, { "game.exe", "--bogus" }, "unknown argument \"--bogus\"" },
        { 2, { "game.exe", "headless" }, "unknown argument \"headless\"" },
        { 2, { "game.exe", "--seed" }, "--seed needs a value" },
        { 3, { "game.exe", "--seed", "-1" }, "--seed \"-1\": expected an integer 0..9223372036854775807" },
        { 3, { "game.exe", "--seed", "12x" }, "--seed \"12x\"" },
        { 3, { "game.exe", "--seed", "9223372036854775808" }, "--seed \"9223372036854775808\"" },
        { 3, { "game.exe", "--tick-rate", "29" }, "--tick-rate \"29\": expected an integer 30..240" },
        { 3, { "game.exe", "--tick-rate", "241" }, "--tick-rate \"241\"" },
        { 3, { "game.exe", "--scale", "0" }, "--scale \"0\": expected a number 0.01..100" },
        { 3, { "game.exe", "--scale", "nan" }, "--scale \"nan\"" },
        { 3, { "game.exe", "--scale", "101" }, "--scale \"101\"" },
        { 3, { "game.exe", "--replay", "" }, "--replay needs a file name" },
        { 2, { "game.exe", "--verify" }, "--verify needs --replay FILE" },
        { 2, { "game.exe", "--profile" }, "--profile needs --replay FILE, without --control and --verify" },
        { 3, { "game.exe", "--profile", "--control" }, "--profile needs --replay FILE" },
    };
    for (i32 c = 0; c < (i32)(sizeof bad / sizeof bad[0]); c++) {
        bmf_err err = core_test_parse(bad[c].count, bad[c].values, &o, error);
        char message[BMF_CORE_ERROR_MAX + 128];
        bmf_fmt_buf(message, (i32)sizeof message, "case %i: expected \"%s\" in \"%s\"", c, bad[c].expected, error);
        bmf_check(t, err == BMF_ERR_INVALID && core_test_contains(error, bad[c].expected), __FILE__, __LINE__, message);
    }
}

// ---------------------------------------------------------------- core.start_init
static void test_core_start_init(bmf_test* t) {
    bmf_game_desc game = core_test_game();
    bmf_core_options options = core_test_options();
    options.seed = 1234;
    core_test_state* s = core_test_start(t, &game, &options);
    bmf_check(t, s != 0 && ((u64)(size_t)s & 63) == 0, __FILE__, __LINE__, "state block is 64-byte aligned");
    bmf_check(t, s->zero_at_init, __FILE__, __LINE__, "state block is zero when init runs");
    bmf_check_eq_i64(t, s->init_calls, 1, __FILE__, __LINE__, "init once");
    bmf_check_eq_u64(t, s->seed, 1234, __FILE__, __LINE__, "init gets the seed");
    bmf_ctx* cx = bmf_core_ctx();
    bmf_check_eq_i64(t, cx->tick, 0, __FILE__, __LINE__, "tick 0 after init");
    bmf_check_eq_i64(t, cx->tick_rate, 60, __FILE__, __LINE__, "tick_rate 0 means 60");
    bmf_check_eq_u64(t, bmf_f32_bits(cx->dt), bmf_f32_bits(1.0f / 60.0f), __FILE__, __LINE__, "dt = 1/60");
    bmf_check_eq_u64(t, s->dt_bits, bmf_f32_bits(1.0f / 60.0f), __FILE__, __LINE__, "init sees dt");
    bmf_check_eq_u64(t, cx->seed, 1234, __FILE__, __LINE__, "cx->seed");
    bmf_check(t, cx->level && cx->frame && cx->level != cx->frame, __FILE__, __LINE__, "level and frame arenas");
    bmf_check(t, bmf_core_game()->state_size == game.state_size && bmf_cstr_eq(bmf_core_game()->name, "core_test"), __FILE__, __LINE__, "game copy");

    // --tick-rate overrides the game; the game's rate is used otherwise.
    options.tick_rate = 120;
    s = core_test_start(t, &game, &options);
    bmf_check_eq_u64(t, bmf_f32_bits(bmf_core_ctx()->dt), bmf_f32_bits(1.0f / 120.0f), __FILE__, __LINE__, "--tick-rate 120");
    options.tick_rate = 0;
    game.tick_rate = 30;
    s = core_test_start(t, &game, &options);
    bmf_check_eq_i64(t, bmf_core_ctx()->tick_rate, 30, __FILE__, __LINE__, "the game's tick rate");

    // A bad game does not start.
    char error[BMF_CORE_ERROR_MAX];
    game.state_size = 0;
    bmf_check_eq_i64(t, bmf_core_start(&game, &options, error, (i32)sizeof error), BMF_ERR_INVALID, __FILE__, __LINE__, "bad game");
    bmf_check(t, bmf_core_state() == 0, __FILE__, __LINE__, "nothing running after a failed start");
    bmf_core_stop();
}

// ---------------------------------------------------------------- core.fixed_step
static void test_core_fixed_step(bmf_test* t) {
    bmf_game_desc game = core_test_game();
    bmf_core_options options = core_test_options();
    core_test_start(t, &game, &options);

    bmf_check_eq_i64(t, bmf_core_advance(0), 0, __FILE__, __LINE__, "no time, no tick");
    bmf_check_eq_i64(t, bmf_core_advance(-5000000), 0, __FILE__, __LINE__, "negative time counts as 0");
    bmf_check_eq_i64(t, bmf_core_advance(16666666), 0, __FILE__, __LINE__, "1 ns short of a tick");
    bmf_check_eq_i64(t, bmf_core_advance(1), 1, __FILE__, __LINE__, "16666667 ns in total = one 60 Hz tick");

    // 60 frames of 16666667 ns (1 s plus 20 ns) give exactly 60 ticks: integer accumulator.
    core_test_start(t, &game, &options);
    i32 ticks = 0;
    for (i32 f = 0; f < 60; f++) ticks += bmf_core_advance(16666667);
    bmf_check_eq_i64(t, ticks, 60, __FILE__, __LINE__, "60 frames of 1/60 s");
    bmf_check_eq_i64(t, bmf_core_get_stats().ticks, 60, __FILE__, __LINE__, "stats.ticks");
    bmf_check(t, bmf_core_alpha() < 0.00001f, __FILE__, __LINE__, "almost nothing left over");

    // Alpha is the fraction of a tick accumulated.
    core_test_start(t, &game, &options);
    bmf_core_advance(8333333);   // 499999980 / 1e9 of a tick
    f32 alpha = bmf_core_alpha();
    bmf_check(t, alpha > 0.4999999f && alpha < 0.5f, __FILE__, __LINE__, "alpha about one half");

    // Spiral-of-death protection: at most 4 ticks per frame, the rest of the whole ticks dropped.
    core_test_start(t, &game, &options);
    bmf_core_advance(8333333);
    bmf_check_eq_i64(t, bmf_core_advance(1000000000), BMF_CORE_MAX_STEPS_PER_FRAME, __FILE__, __LINE__, "1 s frame runs 4 ticks");
    bmf_check_eq_i64(t, bmf_core_get_stats().ticks_dropped, 56, __FILE__, __LINE__, "56 of 60.5 owed ticks dropped");
    alpha = bmf_core_alpha();
    bmf_check(t, alpha > 0.4999999f && alpha < 0.5f, __FILE__, __LINE__, "the fraction is kept");
    bmf_check_eq_i64(t, bmf_core_advance(5000000000ll), 4, __FILE__, __LINE__, "5 s is clamped to 1 s");
    bmf_check_eq_i64(t, bmf_core_get_stats().ticks_dropped, 112, __FILE__, __LINE__, "dropped again");
    bmf_check_eq_i64(t, bmf_core_advance(16666667 * 3), 3, __FILE__, __LINE__, "3 ticks owed run normally");

    // Every rate 30..240 runs exactly `rate` ticks per second of 10 ms frames.
    const i32 rates[5] = { 30, 60, 120, 144, 240 };
    for (i32 r = 0; r < 5; r++) {
        options.tick_rate = rates[r];
        core_test_start(t, &game, &options);
        ticks = 0;
        for (i32 f = 0; f < 100; f++) ticks += bmf_core_advance(10000000);
        char message[64];
        bmf_fmt_buf(message, (i32)sizeof message, "%i Hz: ticks in 1 s", rates[r]);
        bmf_check_eq_i64(t, ticks, rates[r], __FILE__, __LINE__, message);
        bmf_check_eq_i64(t, bmf_core_get_stats().ticks_dropped, 0, __FILE__, __LINE__, "no drops at 10 ms frames");
    }

    // Time scale 0.5: 2 s of real time is 1 s of game time.
    options.tick_rate = 0;
    options.time_scale = 0.5f;
    core_test_start(t, &game, &options);
    ticks = 0;
    for (i32 f = 0; f < 200; f++) ticks += bmf_core_advance(10000000);
    bmf_check_eq_i64(t, ticks, 60, __FILE__, __LINE__, "time scale 0.5");
    bmf_core_stop();
}

// ---------------------------------------------------------------- core.tick_input_events
static void test_core_tick_input_events(bmf_test* t) {
    bmf_game_desc game = core_test_game();
    bmf_core_options options = core_test_options();
    core_test_state* s = core_test_start(t, &game, &options);
    i32 fire = bmf_input_action_find(bmf_str_from_cstr("fire"));
    bmf_check_eq_i64(t, fire, 0, __FILE__, __LINE__, "fire is action 0 (the engine's copy of the table)");

    bmf_input_inject_button(fire, 1);
    bmf_check_eq_i64(t, bmf_core_step(3), 3, __FILE__, __LINE__, "step 3");
    bmf_input_inject_button(fire, 0);
    bmf_core_step(1);
    bmf_check_eq_i64(t, s->tick_calls, 4, __FILE__, __LINE__, "4 tick calls");
    for (i32 i = 0; i < 4; i++) bmf_check_eq_i64(t, s->ticks_seen[i], i, __FILE__, __LINE__, "cx->tick inside tick is the tick index");
    bmf_check_eq_i64(t, bmf_core_ctx()->tick, 4, __FILE__, __LINE__, "cx->tick after = ticks completed");
    bmf_check(t, s->pressed[0] == 1 && s->down[0] == 1, __FILE__, __LINE__, "tick 0: pressed and down");
    bmf_check(t, s->pressed[1] == 0 && s->down[1] == 1 && s->down[2] == 1, __FILE__, __LINE__, "ticks 1, 2: held");
    bmf_check(t, s->down[3] == 0 && s->released[3] == 1, __FILE__, __LINE__, "tick 3: released");

    // sfx: delivered after each tick, at most BMF_CORE_MAX_SFX_PER_TICK per tick.
    s->sfx_per_tick = 3;
    bmf_core_step(2);
    bmf_check_eq_i64(t, bmf_core_get_stats().sfx_played, 6, __FILE__, __LINE__, "3 sfx per tick, 2 ticks");
    s->sfx_per_tick = BMF_CORE_MAX_SFX_PER_TICK + 44;
    bmf_core_step(1);
    bmf_check_eq_i64(t, bmf_core_get_stats().sfx_played, 6 + BMF_CORE_MAX_SFX_PER_TICK, __FILE__, __LINE__, "256 delivered");
    bmf_check_eq_i64(t, bmf_core_get_stats().sfx_dropped, 44, __FILE__, __LINE__, "44 dropped");
    s->sfx_per_tick = 1;
    bmf_core_step(1);
    bmf_check_eq_i64(t, bmf_core_get_stats().sfx_played, 7 + BMF_CORE_MAX_SFX_PER_TICK, __FILE__, __LINE__, "the queue is empty again after a tick");
    bmf_core_stop();
}

// ---------------------------------------------------------------- core.swap
static void core_test_other_draw(bmf_ctx* cx, const void* state, f32 alpha) { core_test_log.draw_calls += 100; }

static void test_core_swap(bmf_test* t) {
    char error[BMF_CORE_ERROR_MAX];
    bmf_game_desc game = core_test_game();
    bmf_core_options options = core_test_options();
    options.seed = 99;
    options.state_reserve = 256 * 1024;
    core_test_state* s = core_test_start(t, &game, &options);
    bmf_core_step(5);
    s->value = 77;
    i32 fire = 0;
    bmf_check_eq_i64(t, bmf_input_bind(fire, 0, BMF_KEY_G), BMF_OK, __FILE__, __LINE__, "rebind fire to G");

    // Same layout, new code: kept. The action table is equal (another array): bindings stay.
    static bmf_action same_actions[2];
    same_actions[0] = CORE_TEST_ACTIONS[0];
    same_actions[1] = CORE_TEST_ACTIONS[1];
    bmf_game_desc reloaded = game;
    reloaded.draw = core_test_other_draw;
    reloaded.actions = same_actions;
    bmf_check_eq_i64(t, bmf_core_swap_game(&reloaded, error, (i32)sizeof error), BMF_CORE_SWAP_KEPT, __FILE__, __LINE__, error);
    bmf_check(t, bmf_core_state() == s && s->value == 77 && s->init_calls == 1, __FILE__, __LINE__, "state kept, init not called");
    bmf_check_eq_i64(t, bmf_core_ctx()->tick, 5, __FILE__, __LINE__, "tick continues");
    bmf_check_eq_i64(t, bmf_input_binding(fire, 0), BMF_KEY_G, __FILE__, __LINE__, "equal action table: rebinding kept");
    bmf_core_draw(0.0f);
    bmf_check_eq_i64(t, core_test_log.draw_calls, 100, __FILE__, __LINE__, "the new draw runs");

    // Changed field table (and its hash): reset with the same seed.
    reloaded.fields = CORE_TEST_FIELDS;
    reloaded.field_count = 1;
    reloaded.state_layout_hash = bmf_fields_hash(CORE_TEST_FIELDS, 1);
    bmf_check_eq_i64(t, bmf_core_swap_game(&reloaded, error, (i32)sizeof error), BMF_CORE_SWAP_RESET, __FILE__, __LINE__, error);
    s = (core_test_state*)bmf_core_state();
    bmf_check(t, s->zero_at_init && s->init_calls == 1 && s->value == 0 && s->seed == 99, __FILE__, __LINE__, "zeroed, init again with the seed");
    bmf_check_eq_i64(t, bmf_core_ctx()->tick, 0, __FILE__, __LINE__, "tick 0 after a reset");

    // Bigger state within the reserve: reset with the new size.
    reloaded.state_size = (i32)sizeof(core_test_state) + 64;
    bmf_check_eq_i64(t, bmf_core_swap_game(&reloaded, error, (i32)sizeof error), BMF_CORE_SWAP_RESET, __FILE__, __LINE__, error);
    bmf_check_eq_i64(t, bmf_core_game()->state_size, (i32)sizeof(core_test_state) + 64, __FILE__, __LINE__, "new size");

    // Invalid or too big: rejected, the old game keeps running.
    bmf_game_desc bad = reloaded;
    bad.tick_rate = 7;
    bmf_check_eq_i64(t, bmf_core_swap_game(&bad, error, (i32)sizeof error), BMF_CORE_SWAP_REJECTED, __FILE__, __LINE__, "invalid tick rate");
    bmf_check(t, core_test_contains(error, "tick_rate 7"), __FILE__, __LINE__, error);
    bad = reloaded;
    bad.state_size = 512 * 1024;
    bmf_check_eq_i64(t, bmf_core_swap_game(&bad, error, (i32)sizeof error), BMF_CORE_SWAP_REJECTED, __FILE__, __LINE__, "state beyond the reserve");
    bmf_check(t, core_test_contains(error, "exceeds the reserve of 262144 bytes"), __FILE__, __LINE__, error);
    bmf_check_eq_i64(t, bmf_core_game()->state_size, (i32)sizeof(core_test_state) + 64, __FILE__, __LINE__, "old game still running");
    bmf_check_eq_i64(t, bmf_core_get_stats().reloads, 3, __FILE__, __LINE__, "3 successful swaps");
    bmf_check_eq_i64(t, bmf_core_get_stats().resets, 2, __FILE__, __LINE__, "2 of them resets");

    // A changed action table restarts input with it (rebinding back to the defaults); a new tick
    // rate is followed.
    bmf_check_eq_i64(t, bmf_input_binding(fire, 0), BMF_KEY_G, __FILE__, __LINE__, "still G before the table changes");
    static const bmf_action more_actions[] = {
        { "fire", BMF_ACTION_BUTTON, { BMF_KEY_F } },
        { "move", BMF_ACTION_AXIS2, { BMF_KEYS_WASD } },
        { "jump", BMF_ACTION_BUTTON, { BMF_KEY_SPACE } },
    };
    reloaded.actions = more_actions;
    reloaded.action_count = 3;
    reloaded.tick_rate = 120;
    bmf_check_eq_i64(t, bmf_core_swap_game(&reloaded, error, (i32)sizeof error), BMF_CORE_SWAP_KEPT, __FILE__, __LINE__, error);
    bmf_check_eq_i64(t, bmf_input_binding(fire, 0), BMF_KEY_F, __FILE__, __LINE__, "changed table: default binding");
    bmf_check_eq_i64(t, bmf_input_action_find(bmf_str_from_cstr("jump")), 2, __FILE__, __LINE__, "the new action exists");
    bmf_check_eq_u64(t, bmf_f32_bits(bmf_core_ctx()->dt), bmf_f32_bits(1.0f / 120.0f), __FILE__, __LINE__, "new tick rate");
    bmf_core_stop();
}

// ---------------------------------------------------------------- frames
static void test_core_frame_null(bmf_test* t) {
    bmf_game_desc game = core_test_game();
    bmf_core_options options = core_test_options();
    options.headless = 1;
    options.no_gpu = 1;
    core_test_start(t, &game, &options);
    core_test_log.circle_color = 0xFF0000FFu;
    char error[BMF_CORE_ERROR_MAX];
    bmf_check_eq_i64(t, bmf_core_display_open(error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, error);
    bmf_check_eq_i64(t, bmf_gpu_backend_active(), BMF_GPU_BACKEND_NULL, __FILE__, __LINE__, "--no-gpu: null backend");
    for (i32 f = 0; f < 3; f++) bmf_check(t, bmf_core_frame(), __FILE__, __LINE__, "frame continues");
    bmf_core_stats stats = bmf_core_get_stats();
    bmf_check(t, stats.frames == 3 && stats.ticks == 3, __FILE__, __LINE__, "headless: one tick per frame");
    bmf_check_eq_i64(t, core_test_log.draw_calls, 3, __FILE__, __LINE__, "draw per frame");
    bmf_check_eq_i64(t, bmf_f32_bits(core_test_log.last_alpha), 0, __FILE__, __LINE__, "headless alpha 0");
    bmf_check(t, core_test_log.last_view_size.x == 1280.0f && core_test_log.last_view_size.y == 720.0f, __FILE__, __LINE__, "view 1280x720 logical");
    bmf_check_eq_i64(t, bmf_draw2d_get_stats().instances, 1, __FILE__, __LINE__, "the circle was flushed");
    bmf_check_eq_i64(t, bmf_gpu_get_stats().draws, 1, __FILE__, __LINE__, "one GPU draw");
    bmf_check_eq_i64(t, bmf_core_ctx()->frame->used, 0, __FILE__, __LINE__, "frame arena reset after the frame");
    bmf_core_display_close();
    bmf_core_stop();
}

static void test_core_frame_d3d11(bmf_test* t) {
    bmf_game_desc game = core_test_game();
    bmf_core_options options = core_test_options();
    options.headless = 1;
    core_test_start(t, &game, &options);
    core_test_log.circle_color = 0xFF0000FFu;
    char error[BMF_CORE_ERROR_MAX];
    bmf_check_eq_i64(t, bmf_core_display_open(error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, error);
    bmf_check_eq_i64(t, bmf_gpu_backend_active(), BMF_GPU_BACKEND_D3D11, __FILE__, __LINE__, "headless: D3D11 (WARP)");
    bmf_check(t, bmf_core_frame(), __FILE__, __LINE__, "one frame");

    static u8 pixels[BMF_CORE_DEFAULT_WIDTH * BMF_CORE_DEFAULT_HEIGHT * 4];
    bmf_check_eq_i64(t, bmf_gpu_texture_read_begin(core.offscreen), BMF_OK, __FILE__, __LINE__, "read_begin");
    bmf_check_eq_i64(t, bmf_gpu_texture_read_end(pixels, (i64)sizeof pixels), BMF_OK, __FILE__, __LINE__, "read_end");
    // The world origin is the target center; the circle has a 64-pixel radius (1 m, default camera).
    const u8* center = &pixels[(360 * BMF_CORE_DEFAULT_WIDTH + 640) * 4];
    const u8* inside = &pixels[(360 * BMF_CORE_DEFAULT_WIDTH + 640 + 60) * 4];
    const u8* outside = &pixels[(360 * BMF_CORE_DEFAULT_WIDTH + 640 + 70) * 4];
    const u8* corner = &pixels[0];
    bmf_check(t, center[0] == 255 && center[1] == 0 && center[2] == 0 && center[3] == 255, __FILE__, __LINE__, "circle center is red");
    bmf_check(t, inside[0] == 255 && inside[1] == 0, __FILE__, __LINE__, "60 px from the center: red");
    bmf_check(t, outside[0] == 15 && outside[1] == 18 && outside[2] == 23, __FILE__, __LINE__, "70 px from the center: clear color");
    bmf_check(t, corner[0] == 15 && corner[1] == 18 && corner[2] == 23 && corner[3] == 255, __FILE__, __LINE__, "corner: clear color (0.06, 0.07, 0.09)");
    bmf_core_display_close();
    bmf_core_stop();
}

// Expected crash: bmf_sfx_play is for init and tick only.
static void test_core_sfx_in_draw(bmf_test* t) {
    bmf_game_desc game = core_test_game();
    bmf_core_options options = core_test_options();
    core_test_start(t, &game, &options);
    core_test_log.sfx_in_draw = 1;
    bmf_core_draw(0.0f);
}
