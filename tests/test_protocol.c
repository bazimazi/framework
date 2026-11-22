// Tests for src/debug/protocol.c (task 1A-31): every command's exact response in-process, and the
// real stdin/stdout path in a child process (`test.exe --game-child protocol --headless --control
// --no-gpu` with a piped script).

// ---------------------------------------------------------------- the protocol test game
typedef struct protocol_test_state {
    i32 ticks;
    i32 jump_down;        // `jump` down on the last tick
    i32 jump_presses;     // pressed edges seen
    i32 jump_releases;    // released edges seen
    bmf_v2 move;          // axis of `move` on the last tick
    bmf_v2 aim;           // pointer 0 (world) on the last tick
    i32 aim_down;
    i32 count;            // live count of `values`
    f32 values[4];
} protocol_test_state;

static const bmf_field PROTOCOL_TEST_FIELDS[] = {
    { "ticks", BMF_T_I32, offsetof(protocol_test_state, ticks), 1, 0 },
    { "jump.down", BMF_T_I32, offsetof(protocol_test_state, jump_down), 1, 0 },
    { "jump.presses", BMF_T_I32, offsetof(protocol_test_state, jump_presses), 1, 0 },
    { "jump.releases", BMF_T_I32, offsetof(protocol_test_state, jump_releases), 1, 0 },
    { "move", BMF_T_V2, offsetof(protocol_test_state, move), 1, 0 },
    { "aim", BMF_T_V2, offsetof(protocol_test_state, aim), 1, 0 },
    { "aim.down", BMF_T_I32, offsetof(protocol_test_state, aim_down), 1, 0 },
    { "count", BMF_T_I32, offsetof(protocol_test_state, count), 1, 0 },
    { "values", BMF_T_F32, offsetof(protocol_test_state, values), 4, "count" },
};

static const bmf_action PROTOCOL_TEST_ACTIONS[] = {
    { "jump", BMF_ACTION_BUTTON, { BMF_KEY_SPACE } },
    { "move", BMF_ACTION_AXIS2, { BMF_KEYS_WASD } },
    { "aim", BMF_ACTION_POINTER, { 0 } },
};

static void protocol_test_init(bmf_ctx* cx, void* state, u64 seed) {
    protocol_test_state* s = (protocol_test_state*)state;
    s->count = 2;
    for (i32 i = 0; i < 4; i++) s->values[i] = 0.5f + (f32)i;
}

static void protocol_test_tick(bmf_ctx* cx, void* state, const bmf_input* in) {
    protocol_test_state* s = (protocol_test_state*)state;
    s->ticks++;
    s->jump_down = bmf_input_down(in, 0);
    s->jump_presses += bmf_input_pressed(in, 0);
    s->jump_releases += bmf_input_released(in, 0);
    s->move = in->axis[0];
    s->aim = in->pointer[0].world;
    s->aim_down = in->pointer[0].down;
}

// A red 1 m circle at (values[0], 0): what the shot tests (tests/test_shot.c) look for.
static void protocol_test_draw(bmf_ctx* cx, const void* state, f32 alpha) {
    const protocol_test_state* s = (const protocol_test_state*)state;
    bmf_draw_circle(0, bmf_v2_make(s->values[0], 0.0f), 1.0f, 0xFF0000FFu);
}

static bmf_game_desc protocol_test_game(void) {
    bmf_game_desc game;
    bmf_memset(&game, 0, sizeof game);
    game.api_version = BMF_API_VERSION;
    game.name = "protocol_test";
    game.state_size = (i32)sizeof(protocol_test_state);
    game.fields = PROTOCOL_TEST_FIELDS;
    game.field_count = (i32)(sizeof PROTOCOL_TEST_FIELDS / sizeof PROTOCOL_TEST_FIELDS[0]);
    game.state_layout_hash = bmf_fields_hash(game.fields, game.field_count);
    game.actions = PROTOCOL_TEST_ACTIONS;
    game.action_count = (i32)(sizeof PROTOCOL_TEST_ACTIONS / sizeof PROTOCOL_TEST_ACTIONS[0]);
    game.init = protocol_test_init;
    game.tick = protocol_test_tick;
    game.draw = protocol_test_draw;
    return game;
}

static bmf_game_desc snap_test_game(void);    // tests/test_snap.c
static bmf_game_desc crash_test_game(void);   // tests/test_crash.c
static bmf_game_desc profile_test_game(void); // tests/test_profile.c

// `test.exe --game-child GAME FLAGS...`: runs a test game like a runtime exe. GAME is `protocol`
// (the game above), `snap` (tests/test_snap.c), `crash` (tests/test_crash.c) or `profile`
// (tests/test_profile.c).
static i32 test_game_child_main(i32 count, const char* const* values) {
    if (count < 3) return 3;
    b32 snap = bmf_cstr_eq(values[2], "snap"), crashes = bmf_cstr_eq(values[2], "crash"), profiled = bmf_cstr_eq(values[2], "profile");
    if (!snap && !crashes && !profiled && !bmf_cstr_eq(values[2], "protocol")) return 3;
    bmf_core_options options;
    char error[BMF_CORE_ERROR_MAX];
    if (bmf_core_parse_args(count - 2, values + 2, &options, error, (i32)sizeof error) != BMF_OK) {
        bmf_core_print_error("bad_args", error);
        return 1;
    }
    bmf_game_desc game = snap ? snap_test_game() : crashes ? crash_test_game() : profiled ? profile_test_game() : protocol_test_game();
    i32 code = bmf_core_run(&game, &options);
    bmf_platform_flush(BMF_STREAM_STDOUT);
    return code;
}

// ---------------------------------------------------------------- in-process harness
static struct {
    char text[2 * 1024 * 1024];
    i32 length;
} protocol_capture;

static void protocol_test_output(const char* line, i32 length, void* user) {
    (void)user;
    if (protocol_capture.length + length + 2 > (i32)sizeof protocol_capture.text) return;
    bmf_memcpy(protocol_capture.text + protocol_capture.length, line, length);
    protocol_capture.length += length;
    protocol_capture.text[protocol_capture.length++] = '\n';
    protocol_capture.text[protocol_capture.length] = 0;
}

// Executes `request` and checks that the output (events and the response, each ending in '\n')
// is exactly `expected`.
static void protocol_expect(bmf_test* t, const char* request, const char* expected, i32 line) {
    protocol_capture.length = 0;
    protocol_capture.text[0] = 0;
    bmf_protocol_execute(bmf_str_from_cstr(request));
    char message[1024];
    bmf_fmt_buf(message, (i32)sizeof message, "'%s': expected %s got %s", request, expected, protocol_capture.text);
    bmf_check(t, bmf_cstr_eq(protocol_capture.text, expected), __FILE__, line, message);
}

static void protocol_test_start(bmf_test* t, const bmf_game_desc* game) {
    char error[BMF_CORE_ERROR_MAX];
    bmf_core_options options;
    bmf_memset(&options, 0, sizeof options);
    options.time_scale = 1.0f;
    options.control = 1;
    bmf_check_eq_i64(t, bmf_core_start(game, &options, error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, error);
    bmf_draw2d_view view = { 1280, 720, 1.0f };
    bmf_core_set_view(view);
    bmf_protocol_begin(protocol_test_output, 0);
}

static void protocol_test_finish(void) {
    bmf_protocol_end();
    bmf_core_stop();
}

// ---------------------------------------------------------------- protocol.commands
static void test_protocol_commands(bmf_test* t) {
    bmf_game_desc game = protocol_test_game();
    protocol_test_start(t, &game);

    protocol_expect(t, "hello", "{\"ok\":1,\"bmf\":0,\"game\":\"protocol_test\",\"tick_rate\":60}\n", __LINE__);
    protocol_expect(t, "", "", __LINE__);                       // not a request: no response
    protocol_expect(t, "   \t ", "", __LINE__);
    protocol_expect(t, "# step 100", "", __LINE__);
    bmf_check(t, !bmf_protocol_running(), __FILE__, __LINE__, "paused at start");

    // step and get.
    protocol_expect(t, "step 2", "{\"ok\":1,\"tick\":2}\n", __LINE__);
    protocol_expect(t, "step 0", "{\"ok\":1,\"tick\":2}\n", __LINE__);
    protocol_expect(t, "  get   ticks ", "{\"ok\":1,\"path\":\"ticks\",\"value\":2}\n", __LINE__);
    protocol_expect(t, "get tick", "{\"ok\":1,\"path\":\"tick\",\"value\":2}\n", __LINE__);

    // press: down from the next tick for TICKS ticks, then released.
    protocol_expect(t, "press jump 2", "{\"ok\":1}\n", __LINE__);
    protocol_expect(t, "step 1", "{\"ok\":1,\"tick\":3}\n", __LINE__);
    protocol_expect(t, "get jump.down", "{\"ok\":1,\"path\":\"jump.down\",\"value\":1}\n", __LINE__);
    protocol_expect(t, "step 1", "{\"ok\":1,\"tick\":4}\n", __LINE__);
    protocol_expect(t, "dump jump", "{\"ok\":1,\"fields\":{\"jump.down\":1,\"jump.presses\":1,\"jump.releases\":0}}\n", __LINE__);
    protocol_expect(t, "step 1", "{\"ok\":1,\"tick\":5}\n", __LINE__);
    protocol_expect(t, "dump jump", "{\"ok\":1,\"fields\":{\"jump.down\":0,\"jump.presses\":1,\"jump.releases\":1}}\n", __LINE__);
    protocol_expect(t, "press jump", "{\"ok\":1}\n", __LINE__);   // default 1 tick
    protocol_expect(t, "step 2", "{\"ok\":1,\"tick\":7}\n", __LINE__);
    protocol_expect(t, "dump jump", "{\"ok\":1,\"fields\":{\"jump.down\":0,\"jump.presses\":2,\"jump.releases\":2}}\n", __LINE__);

    // axis, with a watch: events come before the response of the step that caused them.
    protocol_expect(t, "watch move", "{\"ok\":1,\"path\":\"move\",\"value\":[0,0]}\n", __LINE__);
    protocol_expect(t, "watch move", "{\"ok\":1,\"path\":\"move\",\"value\":[0,0]}\n", __LINE__);   // no duplicate
    protocol_expect(t, "axis move 0.5 -0.25 2", "{\"ok\":1}\n", __LINE__);
    protocol_expect(t, "step 3",
                    "{\"event\":\"watch\",\"path\":\"move\",\"tick\":8,\"value\":[0.5,-0.25]}\n"
                    "{\"event\":\"watch\",\"path\":\"move\",\"tick\":10,\"value\":[0,0]}\n"
                    "{\"ok\":1,\"tick\":10}\n", __LINE__);
    protocol_expect(t, "step 1", "{\"ok\":1,\"tick\":11}\n", __LINE__);   // unchanged: no event
    protocol_expect(t, "unwatch move", "{\"ok\":1,\"path\":\"move\"}\n", __LINE__);
    protocol_expect(t, "unwatch move", "{\"ok\":0,\"error\":\"not_watched\",\"detail\":\"'move' is not watched\"}\n", __LINE__);
    protocol_expect(t, "axis move 1 0", "{\"ok\":1}\n", __LINE__);
    protocol_expect(t, "step 1", "{\"ok\":1,\"tick\":12}\n", __LINE__);   // unwatched: no event
    protocol_expect(t, "get move", "{\"ok\":1,\"path\":\"move\",\"value\":[1,0]}\n", __LINE__);

    // A watch on an element that stops existing reports the error, then the value again.
    protocol_expect(t, "watch values[1]", "{\"ok\":1,\"path\":\"values[1]\",\"value\":1.5}\n", __LINE__);
    protocol_expect(t, "set count 1", "{\"ok\":1,\"path\":\"count\",\"value\":1}\n", __LINE__);
    protocol_expect(t, "step 1", "{\"event\":\"watch\",\"path\":\"values[1]\",\"tick\":13,\"error\":\"index_out_of_range\"}\n{\"ok\":1,\"tick\":13}\n", __LINE__);
    protocol_expect(t, "set count 2", "{\"ok\":1,\"path\":\"count\",\"value\":2}\n", __LINE__);
    protocol_expect(t, "step 1", "{\"event\":\"watch\",\"path\":\"values[1]\",\"tick\":14,\"value\":1.5}\n{\"ok\":1,\"tick\":14}\n", __LINE__);
    protocol_expect(t, "unwatch values[1]", "{\"ok\":1,\"path\":\"values[1]\"}\n", __LINE__);

    // pointer (world by default, or screen) and raw keys through the bindings.
    protocol_expect(t, "pointer 0 1.5 2 down", "{\"ok\":1}\n", __LINE__);
    protocol_expect(t, "step 1", "{\"ok\":1,\"tick\":15}\n", __LINE__);
    protocol_expect(t, "dump aim", "{\"ok\":1,\"fields\":{\"aim\":[1.5,2],\"aim.down\":1}}\n", __LINE__);
    protocol_expect(t, "pointer 0 736 232 move screen", "{\"ok\":1}\n", __LINE__);
    protocol_expect(t, "pointer 0 -1 -1 up world", "{\"ok\":1}\n", __LINE__);
    protocol_expect(t, "step 1", "{\"ok\":1,\"tick\":16}\n", __LINE__);
    protocol_expect(t, "dump aim", "{\"ok\":1,\"fields\":{\"aim\":[-1,-1],\"aim.down\":0}}\n", __LINE__);
    protocol_expect(t, "key space down", "{\"ok\":1}\n", __LINE__);
    protocol_expect(t, "step 1", "{\"ok\":1,\"tick\":17}\n", __LINE__);
    protocol_expect(t, "key space up", "{\"ok\":1}\n", __LINE__);
    protocol_expect(t, "step 1", "{\"ok\":1,\"tick\":18}\n", __LINE__);
    protocol_expect(t, "dump jump", "{\"ok\":1,\"fields\":{\"jump.down\":0,\"jump.presses\":3,\"jump.releases\":3}}\n", __LINE__);

    // set, get, dump and their errors (reflection).
    protocol_expect(t, "set values[1] 2.5", "{\"ok\":1,\"path\":\"values[1]\",\"value\":2.5}\n", __LINE__);
    protocol_expect(t, "get values", "{\"ok\":1,\"path\":\"values\",\"value\":[0.5,2.5]}\n", __LINE__);
    protocol_expect(t, "dump val", "{\"ok\":1,\"fields\":{\"values\":[0.5,2.5]}}\n", __LINE__);
    protocol_expect(t, "dump",
                    "{\"ok\":1,\"fields\":{\"tick\":18,\"ticks\":18,\"jump.down\":0,\"jump.presses\":3,\"jump.releases\":3,"
                    "\"move\":[0,0],\"aim\":[0,0],\"aim.down\":0,\"count\":2,\"values\":[0.5,2.5]}}\n", __LINE__);   // pointer slot empty again
    protocol_expect(t, "get values[2]", "{\"ok\":0,\"error\":\"index_out_of_range\",\"detail\":\"values[2]: index 2 out of range: live count 2 (count), capacity 4\"}\n", __LINE__);
    protocol_expect(t, "get valuse", "{\"ok\":0,\"error\":\"unknown_path\",\"detail\":\"no field 'valuse'; did you mean 'values'?\"}\n", __LINE__);
    protocol_expect(t, "get ticks[0]", "{\"ok\":0,\"error\":\"bad_path\",\"detail\":\"'ticks' is not an array\"}\n", __LINE__);
    protocol_expect(t, "set move 1", "{\"ok\":0,\"error\":\"bad_value\",\"detail\":\"move is v2: needs 2 values, got 1\"}\n", __LINE__);
    protocol_expect(t, "set tick 5", "{\"ok\":0,\"error\":\"read_only\",\"detail\":\"tick is read-only (it advances with step)\"}\n", __LINE__);

    // run and pause.
    protocol_expect(t, "run", "{\"ok\":1,\"tick\":18,\"running\":true}\n", __LINE__);
    bmf_check(t, bmf_protocol_running(), __FILE__, __LINE__, "running after run");
    protocol_expect(t, "pause", "{\"ok\":1,\"tick\":18,\"running\":false}\n", __LINE__);
    bmf_check(t, !bmf_protocol_running(), __FILE__, __LINE__, "paused after pause");

    // Argument and command errors.
    protocol_expect(t, "\xEF\xBB\xBFhello", "{\"ok\":1,\"bmf\":0,\"game\":\"protocol_test\",\"tick_rate\":60}\n", __LINE__);   // a BOM is skipped
    protocol_expect(t, "help",
        "{\"ok\":1,\"commands\":[\"help\",\"hello\",\"step N (N >= 0)\",\"run\",\"pause\",\"press ACTION [TICKS >= 1]\",\"axis ACTION X Y [TICKS >= 1]\","
        "\"pointer I(0..3) X Y down|up|move [world|screen]\",\"key NAME down|up\",\"get PATH\",\"set PATH VALUE...\",\"dump [PREFIX]\",\"watch PATH\",\"unwatch PATH\","
        "\"reload\",\"quit\",\"shot PATH.png [W H]\",\"record start PATH | record stop\",\"replay PATH\",\"snap save|load NAME\",\"hash\",\"profile N (N >= 0)\",\"mem\","
        "\"log [debug|info|warn|error]\",\"phys query ... (task 1B-09)\"]}\n", __LINE__);
    protocol_expect(t, "help me", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: help\"}\n", __LINE__);
    protocol_expect(t, "stpe 1", "{\"ok\":0,\"error\":\"unknown_command\",\"detail\":\"no command 'stpe'; did you mean 'step'?\"}\n", __LINE__);
    protocol_expect(t, "frobnicate", "{\"ok\":0,\"error\":\"unknown_command\",\"detail\":\"no command 'frobnicate' (help, hello, step, run, pause, press, axis, pointer, key, get, set, dump, watch, unwatch, hash, snap, shot, record, replay, profile, mem, log, reload, quit)\"}\n", __LINE__);
    protocol_expect(t, "step", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: step N (N >= 0)\"}\n", __LINE__);
    protocol_expect(t, "step -1", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: step N (N >= 0)\"}\n", __LINE__);
    protocol_expect(t, "step x", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: step N (N >= 0)\"}\n", __LINE__);
    protocol_expect(t, "step 1 2", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: step N (N >= 0)\"}\n", __LINE__);
    protocol_expect(t, "hello there", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: hello\"}\n", __LINE__);
    protocol_expect(t, "press jump 0", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: press ACTION [TICKS >= 1]\"}\n", __LINE__);
    protocol_expect(t, "press nothere", "{\"ok\":0,\"error\":\"unknown_action\",\"detail\":\"no action 'nothere'\"}\n", __LINE__);
    protocol_expect(t, "press move", "{\"ok\":0,\"error\":\"unknown_action\",\"detail\":\"'move' is not a button action\"}\n", __LINE__);
    protocol_expect(t, "axis jump 1 0", "{\"ok\":0,\"error\":\"unknown_action\",\"detail\":\"'jump' is not an axis2 action\"}\n", __LINE__);
    protocol_expect(t, "axis move nan 0", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: axis ACTION X Y [TICKS >= 1]\"}\n", __LINE__);
    protocol_expect(t, "pointer 4 0 0 down", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: pointer I(0..3) X Y down|up|move [world|screen]\"}\n", __LINE__);
    protocol_expect(t, "pointer 0 0 0 sideways", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: pointer I(0..3) X Y down|up|move [world|screen]\"}\n", __LINE__);
    protocol_expect(t, "pointer 0 0 0 down sky", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: pointer I(0..3) X Y down|up|move [world|screen]\"}\n", __LINE__);
    protocol_expect(t, "key nokey down", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"no key 'nokey' (names like a, 1, space, left_shift, f12)\"}\n", __LINE__);
    protocol_expect(t, "key space sideways", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: key NAME down|up\"}\n", __LINE__);
    protocol_expect(t, "watch nothere", "{\"ok\":0,\"error\":\"unknown_path\",\"detail\":\"no field 'nothere' (dump lists the fields)\"}\n", __LINE__);
    protocol_expect(t, "get a b c d e f g h i j k l m n o p q",
                    "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: get PATH\"}\n", __LINE__);   // 18 tokens: over the limit
    protocol_expect(t, "shot a.png", "{\"ok\":0,\"error\":\"not_supported\",\"detail\":\"no display is open (shots need the runtime's renderer)\"}\n", __LINE__);
    protocol_expect(t, "shot a.png 10", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: shot PATH.png [W H]\"}\n", __LINE__);
    protocol_expect(t, "reload", "{\"ok\":0,\"error\":\"not_supported\",\"detail\":\"reload needs the debug build, where the game is a DLL (D-13)\"}\n", __LINE__);

    // hash and snap (task 1A-34; values and round trips in tests/test_snap.c).
    protocol_expect(t, "hash now", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: hash\"}\n", __LINE__);
    protocol_expect(t, "snap save", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: snap save|load NAME\"}\n", __LINE__);
    protocol_expect(t, "snap keep a", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: snap save|load NAME\"}\n", __LINE__);
    protocol_expect(t, "snap save a/b", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"snapshot names are 1..31 characters of A-Z a-z 0-9 _ . -\"}\n", __LINE__);
    protocol_expect(t, "snap load never", "{\"ok\":0,\"error\":\"unknown_snap\",\"detail\":\"no snapshot 'never' (snap save never first)\"}\n", __LINE__);

    // profile, mem, log (task 1A-36; reports in tests/test_profile.c).
    protocol_expect(t, "profile", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: profile N (N >= 0)\"}\n", __LINE__);
    protocol_expect(t, "profile -1", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: profile N (N >= 0)\"}\n", __LINE__);
    protocol_expect(t, "mem all", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"usage: mem\"}\n", __LINE__);
    protocol_expect(t, "log loud", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"no log level 'loud' (debug, info, warn, error)\"}\n", __LINE__);

    // Commands of later tasks.
    struct { const char* request; const char* expected; } later[] = {
        { "phys query ray 0 0 1 0", "{\"ok\":0,\"error\":\"not_implemented\",\"detail\":\"phys arrives with task 1B-09\"}\n" },
    };
    for (i32 i = 0; i < (i32)(sizeof later / sizeof later[0]); i++) protocol_expect(t, later[i].request, later[i].expected, __LINE__);

    // Error codes are a closed set with spellings.
    for (i32 e = 0; e < BMF_PROTOCOL_ERROR_COUNT; e++)
        bmf_check(t, !bmf_cstr_eq(bmf_protocol_error_code((bmf_protocol_error)e), "?"), __FILE__, __LINE__, "error code spelling");

    // quit.
    bmf_check(t, !bmf_core_quit_requested(), __FILE__, __LINE__, "no quit yet");
    protocol_expect(t, "quit", "{\"ok\":1}\n", __LINE__);
    bmf_check(t, bmf_core_quit_requested(), __FILE__, __LINE__, "quit requested");

    // A hold still active when the protocol ends is released (nothing stays injected).
    protocol_expect(t, "press jump 100", "{\"ok\":1}\n", __LINE__);
    bmf_protocol_end();
    bmf_core_step(2);
    protocol_test_state* s = (protocol_test_state*)bmf_core_state();
    bmf_check(t, s->jump_down == 0, __FILE__, __LINE__, "hold released by bmf_protocol_end");
    bmf_core_stop();
}

// ---------------------------------------------------------------- protocol.large_response
typedef struct protocol_big_state { u8 bytes[400000]; } protocol_big_state;
static const bmf_field PROTOCOL_BIG_FIELDS[] = { { "bytes", BMF_T_U8, 0, 400000, 0 } };
static void protocol_big_init(bmf_ctx* cx, void* state, u64 seed) { bmf_memset(state, 255, sizeof(protocol_big_state)); }

static void test_protocol_large_response(bmf_test* t) {
    bmf_game_desc game = protocol_test_game();
    game.name = "protocol_big";
    game.state_size = (i32)sizeof(protocol_big_state);
    game.fields = PROTOCOL_BIG_FIELDS;
    game.field_count = 1;
    game.state_layout_hash = bmf_fields_hash(PROTOCOL_BIG_FIELDS, 1);
    game.init = protocol_big_init;   // tick and draw stay the protocol test game's: never called here
    protocol_test_start(t, &game);
    // 400 000 x "255," is 1.6 MB: over the 1 MiB response limit.
    protocol_expect(t, "dump", "{\"ok\":0,\"error\":\"response_too_large\",\"detail\":\"the response is over 1048576 bytes (use dump PREFIX or get PATH[i])\"}\n", __LINE__);
    protocol_expect(t, "get bytes[399999]", "{\"ok\":1,\"path\":\"bytes[399999]\",\"value\":255}\n", __LINE__);
    protocol_test_finish();
}

// ---------------------------------------------------------------- protocol.process
#if BMF_OS_WINDOWS
// Runs this test exe as `--game-child GAME <flags>` with `script` on stdin; collects stdout
// (and stderr) into out[0..capacity) and returns the exit code (0xFFFFFFFF if it did not start).
static u32 test_spawn_game(const char* game_name, const char* flags, const char* script, i32 script_length, char* out, i32 capacity) {
    WCHAR exe[1024];
    u32 n = GetModuleFileNameW(0, exe, 1024);
    exe[n] = 0;
    WCHAR cmd[2048];
    i32 len = 0;
    cmd[len++] = '"';
    for (u32 i = 0; i < n; i++) cmd[len++] = exe[i];
    const char* rest = "\" --game-child ";
    for (const char* c = rest; *c; c++) cmd[len++] = (WCHAR)*c;
    for (const char* c = game_name; *c && len < 1900; c++) cmd[len++] = (WCHAR)*c;
    cmd[len++] = ' ';
    for (const char* c = flags; *c && len < 2000; c++) cmd[len++] = (WCHAR)*c;   // ASCII flags
    cmd[len] = 0;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), 0, 1 };
    HANDLE in_read = 0, in_write = 0, out_read = 0, out_write = 0;
    if (!CreatePipe(&in_read, &in_write, &sa, 1 << 16)) return 0xFFFFFFFFu;
    if (!CreatePipe(&out_read, &out_write, &sa, 1 << 16)) return 0xFFFFFFFFu;
    SetHandleInformation(in_write, WIN32_HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_read, WIN32_HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si;
    bmf_memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = WIN32_STARTF_USESTDHANDLES;
    si.hStdInput = in_read;
    si.hStdOutput = out_write;
    si.hStdError = out_write;
    PROCESS_INFORMATION pi;
    bmf_memset(&pi, 0, sizeof pi);
    b32 started = CreateProcessW(exe, cmd, 0, 0, 1, 0, 0, 0, &si, &pi) != 0;
    CloseHandle(in_read);
    CloseHandle(out_write);
    if (started) {
        u32 written = 0;
        WriteFile(in_write, script, (u32)script_length, &written, 0);
    }
    CloseHandle(in_write);   // end of stdin
    i32 used = 0;
    if (started) {
        u32 got = 0;
        while (used < capacity - 1 && ReadFile(out_read, out + used, (u32)(capacity - 1 - used), &got, 0) && got) used += (i32)got;
    }
    out[used] = 0;
    CloseHandle(out_read);
    if (!started) return 0xFFFFFFFFu;
    WaitForSingleObject(pi.hProcess, (u32)WIN32_INFINITE);
    u32 code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code;
}

static u32 protocol_test_spawn(const char* flags, const char* script, i32 script_length, char* out, i32 capacity) {
    return test_spawn_game("protocol", flags, script, script_length, out, capacity);
}
#endif

static void test_protocol_process(bmf_test* t) {
#if BMF_OS_WINDOWS
    static char script[8192];
    static char out[16384];
    // A scripted session: responses in order, watch events before their step's response, a line
    // over the limit answered with line_too_long, then quit.
    i32 length = bmf_fmt_buf(script, (i32)sizeof script, "hello\r\nstep 3\npress jump\nwatch jump.presses\nstep 2\nget jump.releases\ndump jump\n");
    for (i32 i = 0; i < 5000; i++) script[length++] = 'x';
    script[length++] = '\n';
    length += bmf_fmt_buf(script + length, (i32)sizeof script - length, "bogus\n# a comment\n\nquit\nstep 1\n");
    u32 code = protocol_test_spawn("--headless --control --no-gpu", script, length, out, (i32)sizeof out);
    const char* expected =
        "{\"event\":\"start\",\"game\":\"protocol_test\",\"tick_rate\":60,\"seed\":0,\"mode\":\"headless\",\"gpu\":\"null\",\"link\":\"static\"}\n"
        "{\"ok\":1,\"bmf\":0,\"game\":\"protocol_test\",\"tick_rate\":60}\n"
        "{\"ok\":1,\"tick\":3}\n"
        "{\"ok\":1}\n"
        "{\"ok\":1,\"path\":\"jump.presses\",\"value\":0}\n"
        "{\"event\":\"watch\",\"path\":\"jump.presses\",\"tick\":4,\"value\":1}\n"
        "{\"ok\":1,\"tick\":5}\n"
        "{\"ok\":1,\"path\":\"jump.releases\",\"value\":1}\n"
        "{\"ok\":1,\"fields\":{\"jump.down\":0,\"jump.presses\":1,\"jump.releases\":1}}\n"
        "{\"ok\":0,\"error\":\"line_too_long\",\"detail\":\"a request line is at most 4095 bytes\"}\n"
        "{\"ok\":0,\"error\":\"unknown_command\",\"detail\":\"no command 'bogus' (help, hello, step, run, pause, press, axis, pointer, key, get, set, dump, watch, unwatch, hash, snap, shot, record, replay, profile, mem, log, reload, quit)\"}\n"
        "{\"ok\":1}\n"
        "{\"event\":\"quit\",\"tick\":5,";   // frames depends on how lines were batched
    char message[2048];
    bmf_fmt_buf(message, (i32)sizeof message, "exit %u, output:\n%s", code, out);
    bmf_check(t, code == 0 && bmf_str_starts_with(bmf_str_from_cstr(out), bmf_str_from_cstr(expected)), __FILE__, __LINE__, message);
    i32 lines = 0;
    for (const char* c = out; *c; c++) lines += *c == '\n';
    bmf_check_eq_i64(t, lines, 13, __FILE__, __LINE__, "nothing after the quit line (the step after quit is not run)");

    // The end of stdin quits too (no quit command; the last line has no newline).
    code = protocol_test_spawn("--headless --control --no-gpu", "step 4\nget ticks", 16, out, (i32)sizeof out);
    const char* expected_eof =
        "{\"ok\":1,\"tick\":4}\n"
        "{\"ok\":1,\"path\":\"ticks\",\"value\":4}\n"
        "{\"event\":\"quit\",\"tick\":4,";
    bmf_fmt_buf(message, (i32)sizeof message, "exit %u, output:\n%s", code, out);
    i32 after_start = bmf_str_find_byte(bmf_str_from_cstr(out), '\n', 0) + 1;
    bmf_check(t, code == 0 && after_start > 0 && bmf_str_starts_with(bmf_str_from_cstr(out + after_start), bmf_str_from_cstr(expected_eof)), __FILE__, __LINE__, message);

    // Without --control nothing is read from stdin: a headless run ticks freely until killed, so
    // only the control path is tested here. A bad flag still fails before the protocol starts.
    code = protocol_test_spawn("--control --tick-rate 7", "", 0, out, (i32)sizeof out);
    bmf_check(t, code == 1 && bmf_str_find(bmf_str_from_cstr(out), bmf_str_from_cstr("\"error\":\"bad_args\""), 0) >= 0, __FILE__, __LINE__, out);
#endif
}
