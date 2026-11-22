// Tests for crash dumps (src/debug/crash.c, src/platform/win32/win32_crash.c; task 1A-35).
// A test game crashes on purpose at a chosen tick (null write, assert, integer division by zero,
// panic) in a `--game-child crash` process; the crash line must name the tick and the dump files,
// and `--replay` of the dump must crash again the same way at the same tick.
// Uses the process helpers of tests/test_protocol.c.

// ---------------------------------------------------------------- the crash test game
typedef struct crash_test_state {
    i32 ticks;
    i32 jumps;          // the crash needs a jump first, so the replay must carry the input
    i32 mode;           // 1 null write, 2 assert, 3 integer division by zero, 4 panic, 5 stack overflow,
                        // 6 zones closed out of order, 7 a zone left open in tick (set by the test)
    i32 crash_tick;     // the tick (count after its increment) that crashes
    i32 zero;           // always 0: the null address and the divisor come from the state
    bmf_rng rng;
    bmf_v2 pos[4];
} crash_test_state;

static const bmf_field CRASH_TEST_FIELDS[] = {
    { "ticks", BMF_T_I32, offsetof(crash_test_state, ticks), 1, 0 },
    { "jumps", BMF_T_I32, offsetof(crash_test_state, jumps), 1, 0 },
    { "mode", BMF_T_I32, offsetof(crash_test_state, mode), 1, 0 },
    { "crash_tick", BMF_T_I32, offsetof(crash_test_state, crash_tick), 1, 0 },
    { "zero", BMF_T_I32, offsetof(crash_test_state, zero), 1, 0 },
    { "rng", BMF_T_RNG, offsetof(crash_test_state, rng), 1, 0 },
    { "pos", BMF_T_V2, offsetof(crash_test_state, pos), 4, 0 },
};

static const bmf_action CRASH_TEST_ACTIONS[] = {
    { "jump", BMF_ACTION_BUTTON, { BMF_KEY_SPACE } },
};

static void crash_test_init(bmf_ctx* cx, void* state, u64 seed) {
    crash_test_state* s = (crash_test_state*)state;
    bmf_rng_seed(&s->rng, seed, 1);
}

// The sanitizers of debug builds would turn these into UBSan traps; the test wants the real
// hardware exceptions.
static __attribute__((noinline, no_sanitize("null"))) void crash_test_write(i32 address, i32 value) {
    volatile i32* p = (volatile i32*)(u64)(u32)address;
    *p = value;
}

static __attribute__((noinline, no_sanitize("integer-divide-by-zero"))) i32 crash_test_divide(i32 a, i32 b) {
    return a / b;
}

// Recursion that never ends in practice (the depth limit only keeps -Winfinite-recursion quiet).
static __attribute__((noinline)) i32 crash_test_recurse(i32 depth, i32 limit) {
    volatile u8 frame[4096];
    frame[0] = (u8)depth;
    if (depth >= limit) return frame[0];
    return crash_test_recurse(depth + 1, limit) + frame[0];
}

static void crash_test_tick(bmf_ctx* cx, void* state, const bmf_input* in) {
    crash_test_state* s = (crash_test_state*)state;
    s->ticks++;
    s->jumps += bmf_input_pressed(in, 0);
    for (i32 i = 0; i < 4; i++) s->pos[i].x += bmf_rng_range_f32(&s->rng, -1.0f, 1.0f);
    if (s->jumps == 0 || s->ticks != s->crash_tick) return;
    if (s->mode == 1) crash_test_write(s->zero, s->ticks);
    if (s->mode == 2) BMF_ASSERT(s->mode != 2 && "crash test: assert");
    if (s->mode == 3) s->pos[0].y = (f32)crash_test_divide(s->ticks, s->zero);
    if (s->mode == 4) bmf_panic("crash test: panic");
    if (s->mode == 5) s->pos[0].y = (f32)crash_test_recurse(0, 1 << 30);
    if (s->mode == 6) {
        bmf_zone_begin(BMF_ZONE_GAME_FIRST);
        bmf_zone_begin(BMF_ZONE_GAME_FIRST + 1);
        bmf_zone_end(BMF_ZONE_GAME_FIRST);
    }
    if (s->mode == 7) bmf_zone_begin(BMF_ZONE_GAME_FIRST);   // never closed: the engine's tick zone finds it
}

static void crash_test_draw(bmf_ctx* cx, const void* state, f32 alpha) {}

static bmf_game_desc crash_test_game(void) {
    bmf_game_desc game;
    bmf_memset(&game, 0, sizeof game);
    game.api_version = BMF_API_VERSION;
    game.name = "crash_test";
    game.state_size = (i32)sizeof(crash_test_state);
    game.fields = CRASH_TEST_FIELDS;
    game.field_count = (i32)(sizeof CRASH_TEST_FIELDS / sizeof CRASH_TEST_FIELDS[0]);
    game.state_layout_hash = bmf_fields_hash(game.fields, game.field_count);
    game.actions = CRASH_TEST_ACTIONS;
    game.action_count = (i32)(sizeof CRASH_TEST_ACTIONS / sizeof CRASH_TEST_ACTIONS[0]);
    game.init = crash_test_init;
    game.tick = crash_test_tick;
    game.draw = crash_test_draw;
    return game;
}

// ---------------------------------------------------------------- helpers
// Copies the JSON string value of `"key":"` from the first crash line in `text` into out (JSON
// escapes undone); 0 when it is not there.
static b32 crash_test_value(const char* text, const char* key, char* out, i32 capacity) {
    const char* line = text;
    while (*line && !bmf_str_starts_with(bmf_str_from_cstr(line), bmf_str_from_cstr("{\"event\":\"crash\""))) {
        while (*line && *line != '\n') line++;
        if (*line) line++;
    }
    char pattern[64];
    bmf_fmt_buf(pattern, (i32)sizeof pattern, "\"%s\":\"", key);
    i32 at = bmf_str_find(bmf_str_from_cstr(line), bmf_str_from_cstr(pattern), 0);
    if (!*line || at < 0) { out[0] = 0; return 0; }
    const char* c = line + at + bmf_cstr_len(pattern);
    i32 n = 0;
    while (*c && *c != '"' && n < capacity - 1) {
        if (*c == '\\' && c[1]) c++;
        out[n++] = *c++;
    }
    out[n] = 0;
    return 1;
}

// Deletes the dump files a crash line names (tests leave nothing behind).
static void crash_test_delete_dump(const char* text) {
    char path[BMF_PLATFORM_PATH_MAX];
    if (crash_test_value(text, "replay", path, (i32)sizeof path)) bmf_platform_file_delete(path);
    if (crash_test_value(text, "state", path, (i32)sizeof path)) bmf_platform_file_delete(path);
}

// One crash kind end to end: crash in a process, check the line and the files, replay the dump
// in a second process, check that it crashes the same way at the same tick.
static void crash_test_kind(bmf_test* t, i32 mode, u32 expected_code, const char* expected_reason_detail) {
#if BMF_OS_WINDOWS
    static char out[1 << 16], again[1 << 16];
    char script[256], message[1 << 15];
    i32 length = bmf_fmt_buf(script, (i32)sizeof script, "press jump 2\nstep 20\nset mode %i\nset crash_tick 75\nstep 100\nquit\n", mode);
    u32 code = test_spawn_game("crash", "--headless --control --no-gpu", script, length, out, (i32)sizeof out);
    bmf_fmt_buf(message, (i32)sizeof message, "exit 0x%08x (expected 0x%08x), output:\n%s", code, expected_code, out);
    bmf_check(t, code == expected_code, __FILE__, __LINE__, message);
    bmf_check(t, core_test_contains(out, expected_reason_detail), __FILE__, __LINE__, message);
    i32 crash_lines = 0;
    for (i32 at = 0; (at = bmf_str_find(bmf_str_from_cstr(out), bmf_str_from_cstr("{\"event\":\"crash\""), at)) >= 0; at++) crash_lines++;
    bmf_check_eq_i64(t, crash_lines, 1, __FILE__, __LINE__, "one crash line (the trap after an assert or panic reports nothing more)");
    bmf_check(t, core_test_contains(out, "\"tick\":74,\"phase\":\"tick\",\"replay\":\""), __FILE__, __LINE__, message);   // during the 75th tick
    bmf_check(t, !core_test_contains(out, "\"tick\":75}") && !core_test_contains(out, "\"event\":\"quit\""), __FILE__, __LINE__, "the run ended at the crash");
    char address[64];
    bmf_check(t, crash_test_value(out, "address", address, (i32)sizeof address) == (mode == 1 || mode == 3 || mode == 5), __FILE__, __LINE__, "address only for exceptions");
    if (mode == 1 || mode == 3 || mode == 5) bmf_check_eq_i64(t, bmf_cstr_len(address), 18, __FILE__, __LINE__, address);

    // The files: the replay covers ticks 0..75 (with the crashing tick's input), the state is the tick-60 checkpoint.
    char replay_path[BMF_PLATFORM_PATH_MAX], state_path[BMF_PLATFORM_PATH_MAX];
    bmf_check(t, crash_test_value(out, "replay", replay_path, (i32)sizeof replay_path) &&
                 crash_test_value(out, "state", state_path, (i32)sizeof state_path), __FILE__, __LINE__, message);
    static bmf_arena files;   // our own: no core needs to be running in the runner
    if (!files.base) bmf_arena_init(&files, "crash_test", (u8*)bmf_platform_reserve(1 << 20), 1 << 20);
    bmf_arena* arena = &files;
    i64 mark = bmf_arena_mark(arena);
    bmf_str file;
    if (bmf_platform_file_read(replay_path, arena, &file) == BMF_OK && file.len >= (i32)sizeof(bmf_replay_header)) {
        bmf_replay_header h;
        bmf_memcpy(&h, file.ptr, sizeof h);
        bmf_check_eq_i64(t, h.start_tick, 0, __FILE__, __LINE__, "replay start tick");
        bmf_check_eq_i64(t, h.end_tick, 75, __FILE__, __LINE__, "replay end tick: the crashing tick's input is in");
        bmf_check(t, bmf_cstr_eq(h.game_name, "crash_test"), __FILE__, __LINE__, "replay game name");
    } else {
        bmf_check(t, 0, __FILE__, __LINE__, replay_path);
    }
    bmf_arena_pop_to(arena, mark);
    if (bmf_platform_file_read(state_path, arena, &file) == BMF_OK && file.len == (i32)(sizeof(bmf_state_file_header) + sizeof(crash_test_state))) {
        bmf_state_file_header h;
        bmf_memcpy(&h, file.ptr, sizeof h);
        crash_test_state s;
        bmf_memcpy(&s, file.ptr + sizeof h, sizeof s);
        bmf_check(t, bmf_memcmp(h.magic, "BMFS", 4) == 0 && h.version == BMF_STATE_FILE_VERSION && h.state_size == (i32)sizeof s, __FILE__, __LINE__, "state header");
        bmf_check_eq_i64(t, h.tick, 60, __FILE__, __LINE__, "the last checkpoint before the crash");
        bmf_check(t, s.ticks == 60 && s.jumps == 1 && s.mode == mode, __FILE__, __LINE__, "the state of tick 60");
    } else {
        bmf_check(t, 0, __FILE__, __LINE__, state_path);
    }
    bmf_arena_pop_to(arena, mark);

    // Replaying the dump crashes again, the same way, at the same tick.
    char flags[BMF_PLATFORM_PATH_MAX + 64];
    bmf_fmt_buf(flags, (i32)sizeof flags, "--replay \"%s\" --verify", replay_path);
    code = test_spawn_game("crash", flags, "", 0, again, (i32)sizeof again);
    bmf_fmt_buf(message, (i32)sizeof message, "replay: exit 0x%08x (expected 0x%08x), output:\n%s", code, expected_code, again);
    bmf_check(t, code == expected_code && core_test_contains(again, expected_reason_detail) &&
                 core_test_contains(again, "\"tick\":74,\"phase\":\"tick\",\"replay\":\""), __FILE__, __LINE__, message);
    crash_test_delete_dump(again);
    crash_test_delete_dump(out);
#else
    bmf_fmt_buf(t->note, (i32)sizeof t->note, "crash dumps: Windows only so far");
#endif
}

// ---------------------------------------------------------------- crash.*
static void test_crash_null_write(bmf_test* t) {
    crash_test_kind(t, 1, 0xC0000005u, "{\"event\":\"crash\",\"reason\":\"exception\",\"detail\":\"access_violation writing 0x0000000000000000\",\"file\":\"?\",\"line\":0,\"code\":\"0xc0000005\",\"address\":\"0x");
}

static void test_crash_assert(bmf_test* t) {
    crash_test_kind(t, 2, 0xC000001Du, "{\"event\":\"crash\",\"reason\":\"assert\",\"detail\":\"s->mode != 2 && \\\"crash test: assert\\\"\",\"file\":\"");
}

static void test_crash_divide_by_zero(bmf_test* t) {
    crash_test_kind(t, 3, 0xC0000094u, "{\"event\":\"crash\",\"reason\":\"exception\",\"detail\":\"int_divide_by_zero\",\"file\":\"?\",\"line\":0,\"code\":\"0xc0000094\",\"address\":\"0x");
}

static void test_crash_panic(bmf_test* t) {
    crash_test_kind(t, 4, 0xC000001Du, "{\"event\":\"crash\",\"reason\":\"panic\",\"detail\":\"crash test: panic\",\"file\":\"?\",\"line\":0,\"tick\":74,");
}

static void test_crash_stack_overflow(bmf_test* t) {
    crash_test_kind(t, 5, 0xC00000FDu, "{\"event\":\"crash\",\"reason\":\"exception\",\"detail\":\"stack_overflow\",\"file\":\"?\",\"line\":0,\"code\":\"0xc00000fd\",\"address\":\"0x");
}

// Zone nesting errors stop debug builds (task 1A-36), with both zone names; the dump replays them.
static void test_crash_zone_order(bmf_test* t) {
    crash_test_kind(t, 6, 0xC000001Du, "{\"event\":\"crash\",\"reason\":\"assert\",\"detail\":\"bmf_zone_end(game.0): the innermost open zone is game.1 (zones must nest)\",\"file\":\"");
}

static void test_crash_zone_left_open(bmf_test* t) {
    crash_test_kind(t, 7, 0xC000001Du, "{\"event\":\"crash\",\"reason\":\"assert\",\"detail\":\"bmf_zone_end(tick): the innermost open zone is game.0 (zones must nest)\",\"file\":\"");
}
