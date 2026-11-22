// Tests for src/input (task 1A-22): action table validation, per-tick edges for frames that run
// 0, 1 or 4 ticks, presses shorter than a frame, axis normalization, bindings and rebinding,
// gamepads, pointers (mouse, touch, DPI, camera mapping), injection and text.

static const bmf_action INPUT_TEST_ACTIONS[] = {
    { "jump",  BMF_ACTION_BUTTON, { BMF_KEY_SPACE, BMF_PAD_A } },
    { "move",  BMF_ACTION_AXIS2,  { BMF_KEYS_WASD, BMF_PAD_LSTICK } },
    { "fire",  BMF_ACTION_BUTTON, { BMF_MOUSE_RIGHT, BMF_PAD_RIGHT_TRIGGER, BMF_KEY_F } },
    { "aim",   BMF_ACTION_POINTER, { 0 } },
    { "look",  BMF_ACTION_AXIS2,  { BMF_KEY_I | BMF_AXIS_UP, BMF_KEY_K | BMF_AXIS_DOWN, BMF_KEY_J | BMF_AXIS_LEFT,
                                    BMF_KEY_L | BMF_AXIS_RIGHT, BMF_PAD_RSTICK } },
    { "pause", BMF_ACTION_BUTTON, { BMF_KEY_ESCAPE, BMF_PAD_START } },
};
enum { INPUT_TEST_JUMP = 0, INPUT_TEST_FIRE = 1, INPUT_TEST_PAUSE = 2 };   // bits
enum { INPUT_TEST_MOVE = 0, INPUT_TEST_LOOK = 1 };                         // axes

static void input_test_init(bmf_test* t) {
    char error[256];
    bmf_err err = bmf_input_init(INPUT_TEST_ACTIONS, (i32)(sizeof INPUT_TEST_ACTIONS / sizeof INPUT_TEST_ACTIONS[0]), error, (i32)sizeof error);
    bmf_check_eq_i64(t, err, BMF_OK, __FILE__, __LINE__, error);
}

static void input_test_key(bmf_key key, b32 down) {
    bmf_event event;
    bmf_memset(&event, 0, sizeof event);
    event.type = down ? BMF_EVENT_KEY_DOWN : BMF_EVENT_KEY_UP;
    event.key.key = key;
    bmf_input_event(&event);
}

static void input_test_mouse_button(bmf_mouse_button button, b32 down) {
    bmf_event event;
    bmf_memset(&event, 0, sizeof event);
    event.type = down ? BMF_EVENT_MOUSE_BUTTON_DOWN : BMF_EVENT_MOUSE_BUTTON_UP;
    event.mouse.button = button;
    bmf_input_event(&event);
}

static void input_test_mouse_move(f32 x, f32 y) {
    bmf_event event;
    bmf_memset(&event, 0, sizeof event);
    event.type = BMF_EVENT_MOUSE_MOVE;
    event.position.x = x;
    event.position.y = y;
    bmf_input_event(&event);
}

static void input_test_touch(bmf_event_type type, i32 source, f32 x, f32 y) {
    bmf_event event;
    bmf_memset(&event, 0, sizeof event);
    event.type = type;
    event.pointer.slot = source;
    event.pointer.x = x;
    event.pointer.y = y;
    bmf_input_event(&event);
}

// Runs `ticks` ticks and appends one character per tick for button `bit` to record:
// 'P' pressed (and down), 'D' down, 'R' released, '.' up; '?' for an impossible combination.
static void input_test_run(i32 ticks, i32 bit, char* record, i32* length) {
    for (i32 i = 0; i < ticks; i++) {
        bmf_input in;
        bmf_input_sample_tick(&in);
        b32 down = bmf_input_down(&in, bit), pressed = bmf_input_pressed(&in, bit), released = bmf_input_released(&in, bit);
        char c = '?';
        if (pressed && down && !released) c = 'P';
        else if (down && !pressed && !released) c = 'D';
        else if (released && !down && !pressed) c = 'R';
        else if (!down && !pressed && !released) c = '.';
        record[(*length)++] = c;
    }
    record[*length] = 0;
}

static void input_test_record_eq(bmf_test* t, const char* record, const char* expected, i32 line, const char* what) {
    char message[160];
    bmf_fmt_buf(message, (i32)sizeof message, "%s: ticks \"%s\", expected \"%s\"", what, record, expected);
    bmf_check(t, bmf_cstr_eq(record, expected), __FILE__, line, message);
}

static b32 input_test_contains(const char* text, const char* needle) {
    return bmf_str_find(bmf_str_from_cstr(text), bmf_str_from_cstr(needle), 0) >= 0;
}

static void test_input_validation(bmf_test* t) {
    char error[256];
    input_test_init(t);
    bmf_check_eq_i64(t, bmf_input_action_find(bmf_str_from_cstr("look")), 4, __FILE__, __LINE__, "find look");
    bmf_check_eq_i64(t, bmf_input_action_find(bmf_str_from_cstr("lookx")), -1, __FILE__, __LINE__, "find unknown");
    bmf_check_eq_i64(t, bmf_input_action_type(4), BMF_ACTION_AXIS2, __FILE__, __LINE__, "look type");
    bmf_check_eq_i64(t, bmf_input_action_slot(0), INPUT_TEST_JUMP, __FILE__, __LINE__, "jump bit");
    bmf_check_eq_i64(t, bmf_input_action_slot(2), INPUT_TEST_FIRE, __FILE__, __LINE__, "fire bit");
    bmf_check_eq_i64(t, bmf_input_action_slot(5), INPUT_TEST_PAUSE, __FILE__, __LINE__, "pause bit");
    bmf_check_eq_i64(t, bmf_input_action_slot(4), INPUT_TEST_LOOK, __FILE__, __LINE__, "look axis");
    bmf_check_eq_i64(t, bmf_input_action_slot(3), 0, __FILE__, __LINE__, "pointer slot");
    bmf_check_eq_i64(t, bmf_input_init(0, 0, error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, "empty table");

    static const struct { bmf_action actions[2]; const char* needle; } bad[] = {
        { { { "jump", BMF_ACTION_BUTTON, { BMF_KEY_SPACE } }, { "jump", BMF_ACTION_BUTTON, { BMF_KEY_A } } }, "action 1 \"jump\": duplicate name (also action 0)" },
        { { { "move left", BMF_ACTION_BUTTON, { BMF_KEY_A } }, { "ok", BMF_ACTION_BUTTON, { 0 } } }, "action 0 \"move left\": name" },
        { { { "ok", BMF_ACTION_BUTTON, { 0 } }, { "", BMF_ACTION_BUTTON, { 0 } } }, "action 1 \"\": name" },
        { { { "ok", BMF_ACTION_BUTTON, { 0 } }, { 0, BMF_ACTION_BUTTON, { 0 } } }, "action 1 \"(null)\": name" },
        { { { "a_name_that_is_32_bytes_long_xyz", BMF_ACTION_BUTTON, { 0 } }, { "ok", BMF_ACTION_BUTTON, { 0 } } }, "action 0 \"a_name_that_is_32_bytes_long_xyz\": name" },
        { { { "ok", (bmf_action_type)7, { 0 } }, { "ok2", BMF_ACTION_BUTTON, { 0 } } }, "action 0 \"ok\": unknown type 7" },
        { { { "ok", BMF_ACTION_BUTTON, { BMF_KEY_A, BMF_PAD_LSTICK } }, { "ok2", BMF_ACTION_BUTTON, { 0 } } }, "action 0 \"ok\": binding 1 (0x302) is not valid for a BUTTON action" },
        { { { "ok", BMF_ACTION_BUTTON, { BMF_KEY_A | BMF_AXIS_UP } }, { "ok2", BMF_ACTION_BUTTON, { 0 } } }, "binding 0 (0x1001) is not valid for a BUTTON" },
        { { { "ok", BMF_ACTION_AXIS2, { BMF_KEYS_WASD, BMF_KEY_A } }, { "ok2", BMF_ACTION_BUTTON, { 0 } } }, "binding 1 (0x1) is not valid for a AXIS2" },
        { { { "ok", BMF_ACTION_AXIS2, { BMF_PAD_LSTICK | BMF_AXIS_LEFT } }, { "ok2", BMF_ACTION_BUTTON, { 0 } } }, "binding 0 (0x3302) is not valid for a AXIS2" },
        { { { "ok", BMF_ACTION_AXIS2, { BMF_KEY_A | 0x5000 } }, { "ok2", BMF_ACTION_BUTTON, { 0 } } }, "binding 0 (0x5001)" },
        { { { "ok", BMF_ACTION_BUTTON, { BMF_KEY_COUNT } }, { "ok2", BMF_ACTION_BUTTON, { 0 } } }, "binding 0" },
        { { { "ok", BMF_ACTION_BUTTON, { BMF_MOUSE_X2 + 1 } }, { "ok2", BMF_ACTION_BUTTON, { 0 } } }, "binding 0 (0x105)" },
        { { { "ok", BMF_ACTION_POINTER, { BMF_MOUSE_LEFT } }, { "ok2", BMF_ACTION_BUTTON, { 0 } } }, "not valid for a POINTER action" },
        { { { "p1", BMF_ACTION_POINTER, { 0 } }, { "p2", BMF_ACTION_POINTER, { 0 } } }, "action 1 \"p2\": more than 1 POINTER action" },
    };
    for (i32 c = 0; c < (i32)(sizeof bad / sizeof bad[0]); c++) {
        bmf_err err = bmf_input_init(bad[c].actions, 2, error, (i32)sizeof error);
        char message[400];
        bmf_fmt_buf(message, (i32)sizeof message, "case %i: error \"%s\", expected to contain \"%s\"", c, error, bad[c].needle);
        bmf_check(t, err == BMF_ERR_INVALID && input_test_contains(error, bad[c].needle), __FILE__, __LINE__, message);
    }

    // Counts: 64 buttons and 8 axes are fine, one more of either is not.
    static bmf_action many[BMF_INPUT_MAX_ACTIONS + 1];
    static char names[BMF_INPUT_MAX_ACTIONS + 1][8];
    for (i32 a = 0; a < BMF_INPUT_MAX_ACTIONS + 1; a++) {
        bmf_fmt_buf(names[a], 8, "a%i", a);
        many[a].name = names[a];
        many[a].type = a < 65 ? BMF_ACTION_BUTTON : BMF_ACTION_AXIS2;
    }
    bmf_check_eq_i64(t, bmf_input_init(many, 64, error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, "64 buttons");
    bmf_check_eq_i64(t, bmf_input_action_slot(63), 63, __FILE__, __LINE__, "bit 63");
    bmf_check(t, bmf_input_init(many, 65, error, (i32)sizeof error) == BMF_ERR_INVALID &&
                 input_test_contains(error, "action 64 \"a64\": more than 64 BUTTON actions"), __FILE__, __LINE__, error);
    bmf_check_eq_i64(t, bmf_input_init(many + 65, 8, error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, "8 axes");
    bmf_check(t, bmf_input_init(many + 65, 9, error, (i32)sizeof error) == BMF_ERR_INVALID &&
                 input_test_contains(error, "action 8 \"a73\": more than 8 AXIS2 actions"), __FILE__, __LINE__, error);
    bmf_check(t, bmf_input_init(many, BMF_INPUT_MAX_ACTIONS + 1, error, (i32)sizeof error) == BMF_ERR_INVALID &&
                 input_test_contains(error, "action_count 74 out of range"), __FILE__, __LINE__, error);
    bmf_check(t, bmf_input_init(many, -1, error, (i32)sizeof error) == BMF_ERR_INVALID, __FILE__, __LINE__, "negative count");
    bmf_check(t, bmf_input_init(0, 2, error, (i32)sizeof error) == BMF_ERR_INVALID, __FILE__, __LINE__, "no table");

    // A failed init leaves an empty table: ticks sample zeros.
    input_test_key(BMF_KEY_SPACE, 1);
    bmf_input in, zero;
    bmf_input_sample_tick(&in);
    bmf_memset(&zero, 0, sizeof zero);
    bmf_check_eq_mem(t, &in, &zero, sizeof in, __FILE__, __LINE__, "empty table samples zeros");
}

static void test_input_edges_frames(bmf_test* t) {
    // A press held across frames that run 0, 1 and 4 ticks: pressed on exactly one tick, released
    // on exactly one tick, whatever the frame boundaries.
    input_test_init(t);
    char record[64];
    i32 length = 0;
    input_test_key(BMF_KEY_SPACE, 1);   // frame 1: press, 0 ticks
    input_test_run(0, INPUT_TEST_JUMP, record, &length);
    input_test_run(1, INPUT_TEST_JUMP, record, &length);   // frame 2: 1 tick
    input_test_run(4, INPUT_TEST_JUMP, record, &length);   // frame 3: 4 ticks
    input_test_key(BMF_KEY_SPACE, 0);   // frame 4: release, 0 ticks
    input_test_run(0, INPUT_TEST_JUMP, record, &length);
    input_test_run(4, INPUT_TEST_JUMP, record, &length);   // frame 5: 4 ticks
    input_test_record_eq(t, record, "PDDDDR...", __LINE__, "held across frames");

    // Press and release in frames that run 4 ticks each.
    length = 0;
    input_test_key(BMF_KEY_SPACE, 1);
    input_test_run(4, INPUT_TEST_JUMP, record, &length);
    input_test_key(BMF_KEY_SPACE, 0);
    input_test_run(4, INPUT_TEST_JUMP, record, &length);
    input_test_record_eq(t, record, "PDDDR...", __LINE__, "4-tick frames");

    // One tick per frame.
    length = 0;
    input_test_key(BMF_KEY_SPACE, 1);
    input_test_run(1, INPUT_TEST_JUMP, record, &length);
    input_test_run(1, INPUT_TEST_JUMP, record, &length);
    input_test_key(BMF_KEY_SPACE, 0);
    input_test_run(1, INPUT_TEST_JUMP, record, &length);
    input_test_run(1, INPUT_TEST_JUMP, record, &length);
    input_test_record_eq(t, record, "PDR.", __LINE__, "1-tick frames");

    // Several bindings: a second source going down while the first is held is not a new press;
    // the action is released only when both are up.
    length = 0;
    bmf_gamepad pads[BMF_GAMEPAD_MAX];
    bmf_memset(pads, 0, sizeof pads);
    input_test_key(BMF_KEY_SPACE, 1);
    input_test_run(1, INPUT_TEST_JUMP, record, &length);
    pads[2].connected = 1;
    pads[2].buttons = BMF_GAMEPAD_A;
    bmf_input_set_gamepads(pads, BMF_GAMEPAD_MAX);
    input_test_run(1, INPUT_TEST_JUMP, record, &length);
    input_test_key(BMF_KEY_SPACE, 0);
    input_test_run(1, INPUT_TEST_JUMP, record, &length);
    pads[2].buttons = 0;
    bmf_input_set_gamepads(pads, BMF_GAMEPAD_MAX);
    input_test_run(2, INPUT_TEST_JUMP, record, &length);
    input_test_record_eq(t, record, "PDDR.", __LINE__, "two sources");

    // Auto-repeat is not a press; other buttons keep their own edges.
    length = 0;
    input_test_key(BMF_KEY_SPACE, 1);
    input_test_run(1, INPUT_TEST_JUMP, record, &length);
    bmf_event repeat;
    bmf_memset(&repeat, 0, sizeof repeat);
    repeat.type = BMF_EVENT_KEY_DOWN;
    repeat.key.key = BMF_KEY_SPACE;
    repeat.key.repeat = 1;
    bmf_input_event(&repeat);
    bmf_input_event(&repeat);
    input_test_run(2, INPUT_TEST_JUMP, record, &length);
    input_test_key(BMF_KEY_SPACE, 0);
    input_test_run(1, INPUT_TEST_JUMP, record, &length);
    input_test_record_eq(t, record, "PDDR", __LINE__, "auto-repeat");
    bmf_input_event(&repeat);   // a stray repeat without a down does nothing either
    length = 0;
    input_test_run(2, INPUT_TEST_JUMP, record, &length);
    input_test_record_eq(t, record, "..", __LINE__, "stray repeat");

    // Edges are per action: "pause" pressed while "jump" is held.
    input_test_key(BMF_KEY_SPACE, 1);
    bmf_input in;
    bmf_input_sample_tick(&in);
    input_test_key(BMF_KEY_ESCAPE, 1);
    bmf_input_sample_tick(&in);
    bmf_check(t, in.down == ((1u << INPUT_TEST_JUMP) | (1u << INPUT_TEST_PAUSE)) && in.pressed == (1u << INPUT_TEST_PAUSE) && in.released == 0,
              __FILE__, __LINE__, "independent bits");
    input_test_key(BMF_KEY_SPACE, 0);
    input_test_key(BMF_KEY_ESCAPE, 0);
    bmf_input_sample_tick(&in);
    bmf_check(t, in.down == 0 && in.pressed == 0 && in.released == ((1u << INPUT_TEST_JUMP) | (1u << INPUT_TEST_PAUSE)), __FILE__, __LINE__, "both released");
}

static void test_input_short_presses(bmf_test* t) {
    // A press and release inside one frame is down and pressed on exactly one tick, released on
    // the next, for frames of 0, 1 and 4 ticks.
    input_test_init(t);
    char record[64];
    i32 length = 0;
    input_test_key(BMF_KEY_SPACE, 1);
    input_test_key(BMF_KEY_SPACE, 0);
    input_test_run(4, INPUT_TEST_JUMP, record, &length);
    input_test_record_eq(t, record, "PR..", __LINE__, "tap in a 4-tick frame");

    length = 0;
    input_test_key(BMF_KEY_SPACE, 1);
    input_test_key(BMF_KEY_SPACE, 0);
    input_test_run(1, INPUT_TEST_JUMP, record, &length);
    input_test_run(1, INPUT_TEST_JUMP, record, &length);
    input_test_run(1, INPUT_TEST_JUMP, record, &length);
    input_test_record_eq(t, record, "PR.", __LINE__, "tap in 1-tick frames");

    length = 0;
    input_test_key(BMF_KEY_SPACE, 1);
    input_test_key(BMF_KEY_SPACE, 0);
    input_test_run(0, INPUT_TEST_JUMP, record, &length);   // frame of 0 ticks: the tap waits
    input_test_run(0, INPUT_TEST_JUMP, record, &length);
    input_test_run(1, INPUT_TEST_JUMP, record, &length);
    input_test_run(4, INPUT_TEST_JUMP, record, &length);
    input_test_record_eq(t, record, "PR...", __LINE__, "tap across 0-tick frames");

    // Two taps in one frame: two presses.
    length = 0;
    for (i32 i = 0; i < 2; i++) { input_test_key(BMF_KEY_SPACE, 1); input_test_key(BMF_KEY_SPACE, 0); }
    input_test_run(5, INPUT_TEST_JUMP, record, &length);
    input_test_record_eq(t, record, "PRPR.", __LINE__, "double tap");

    // Release and press again within a frame while held: the release shows first.
    length = 0;
    input_test_key(BMF_KEY_SPACE, 1);
    input_test_run(2, INPUT_TEST_JUMP, record, &length);
    input_test_key(BMF_KEY_SPACE, 0);
    input_test_key(BMF_KEY_SPACE, 1);
    input_test_run(3, INPUT_TEST_JUMP, record, &length);
    input_test_key(BMF_KEY_SPACE, 0);
    input_test_run(2, INPUT_TEST_JUMP, record, &length);
    input_test_record_eq(t, record, "PDRPDR.", __LINE__, "re-press while held");

    // Tap then hold in one frame.
    length = 0;
    input_test_key(BMF_KEY_SPACE, 1);
    input_test_key(BMF_KEY_SPACE, 0);
    input_test_key(BMF_KEY_SPACE, 1);
    input_test_run(4, INPUT_TEST_JUMP, record, &length);
    input_test_key(BMF_KEY_SPACE, 0);
    input_test_run(2, INPUT_TEST_JUMP, record, &length);
    input_test_record_eq(t, record, "PRPDR.", __LINE__, "tap then hold");

    // At most BMF_INPUT_MAX_PENDING_PRESSES presses wait; more merge.
    length = 0;
    for (i32 i = 0; i < 20; i++) { input_test_key(BMF_KEY_SPACE, 1); input_test_key(BMF_KEY_SPACE, 0); }
    input_test_run(40, INPUT_TEST_JUMP, record, &length);
    i32 presses = 0;
    for (i32 i = 0; i < length; i++) presses += record[i] == 'P';
    bmf_check_eq_i64(t, presses, BMF_INPUT_MAX_PENDING_PRESSES, __FILE__, __LINE__, "pending presses saturate");

    // Mouse buttons as bindings, through the same rules.
    length = 0;
    input_test_mouse_button(BMF_MOUSE_BUTTON_RIGHT, 1);
    input_test_mouse_button(BMF_MOUSE_BUTTON_RIGHT, 0);
    input_test_run(3, INPUT_TEST_FIRE, record, &length);
    input_test_record_eq(t, record, "PR.", __LINE__, "mouse tap");
}

static void input_test_axis_eq(bmf_test* t, bmf_v2 actual, f32 x, f32 y, i32 line, const char* what) {
    char message[160];
    bmf_fmt_buf(message, (i32)sizeof message, "%s: (%f, %f), expected (%f, %f)", what, (f64)actual.x, (f64)actual.y, (f64)x, (f64)y);
    bmf_check(t, bmf_f32_bits(actual.x) == bmf_f32_bits(x) && bmf_f32_bits(actual.y) == bmf_f32_bits(y), __FILE__, line, message);
}

static void test_input_axis_normalization(bmf_test* t) {
    input_test_init(t);
    bmf_input in;
    const f32 diagonal = 1.0f / bmf_sqrtf(2.0f);   // 0.70710677

    bmf_input_sample_tick(&in);
    input_test_axis_eq(t, in.axis[INPUT_TEST_MOVE], 0.0f, 0.0f, __LINE__, "idle");
    input_test_key(BMF_KEY_W, 1);
    bmf_input_sample_tick(&in);
    input_test_axis_eq(t, in.axis[INPUT_TEST_MOVE], 0.0f, 1.0f, __LINE__, "W is +y");
    input_test_key(BMF_KEY_D, 1);
    bmf_input_sample_tick(&in);
    input_test_axis_eq(t, in.axis[INPUT_TEST_MOVE], diagonal, diagonal, __LINE__, "W+D diagonal has length 1");
    bmf_check_eq_u64(t, bmf_f32_bits(diagonal), 0x3F3504F3u, __FILE__, __LINE__, "diagonal value");
    input_test_key(BMF_KEY_A, 1);
    bmf_input_sample_tick(&in);
    input_test_axis_eq(t, in.axis[INPUT_TEST_MOVE], 0.0f, 1.0f, __LINE__, "A and D cancel");
    input_test_key(BMF_KEY_S, 1);
    bmf_input_sample_tick(&in);
    input_test_axis_eq(t, in.axis[INPUT_TEST_MOVE], 0.0f, 0.0f, __LINE__, "all four cancel");
    input_test_key(BMF_KEY_W, 0);
    input_test_key(BMF_KEY_D, 0);
    bmf_input_sample_tick(&in);
    input_test_axis_eq(t, in.axis[INPUT_TEST_MOVE], -diagonal, -diagonal, __LINE__, "A+S");
    input_test_key(BMF_KEY_A, 0);
    input_test_key(BMF_KEY_S, 0);

    // Sticks inside the unit circle pass unchanged; longer vectors are scaled to length 1.
    bmf_gamepad pads[BMF_GAMEPAD_MAX];
    bmf_memset(pads, 0, sizeof pads);
    pads[0].connected = 1;
    pads[0].left_x = 0.6f;
    pads[0].left_y = -0.5f;
    bmf_input_set_gamepads(pads, BMF_GAMEPAD_MAX);
    bmf_input_sample_tick(&in);
    input_test_axis_eq(t, in.axis[INPUT_TEST_MOVE], 0.6f, -0.5f, __LINE__, "stick inside");
    pads[0].left_x = 1.0f;
    pads[0].left_y = 1.0f;
    bmf_input_set_gamepads(pads, BMF_GAMEPAD_MAX);
    bmf_input_sample_tick(&in);
    input_test_axis_eq(t, in.axis[INPUT_TEST_MOVE], diagonal, diagonal, __LINE__, "stick corner");
    pads[0].left_x = 0.3f;
    pads[0].left_y = 0.0f;
    bmf_input_set_gamepads(pads, BMF_GAMEPAD_MAX);
    input_test_key(BMF_KEY_D, 1);
    bmf_input_sample_tick(&in);
    input_test_axis_eq(t, in.axis[INPUT_TEST_MOVE], 1.0f, 0.0f, __LINE__, "key plus stick sums, then clamps");
    input_test_key(BMF_KEY_A, 1);
    bmf_input_sample_tick(&in);
    input_test_axis_eq(t, in.axis[INPUT_TEST_MOVE], 0.3f, 0.0f, __LINE__, "keys cancel, stick stays");
    input_test_key(BMF_KEY_A, 0);
    input_test_key(BMF_KEY_D, 0);

    // The stick pushed furthest wins across pads; a disconnected pad does not count.
    pads[1].connected = 1;
    pads[1].left_x = -0.8f;
    pads[3].left_x = 1.0f;   // not connected
    bmf_input_set_gamepads(pads, BMF_GAMEPAD_MAX);
    bmf_input_sample_tick(&in);
    input_test_axis_eq(t, in.axis[INPUT_TEST_MOVE], -0.8f, 0.0f, __LINE__, "furthest stick");
    bmf_memset(pads, 0, sizeof pads);
    bmf_input_set_gamepads(pads, BMF_GAMEPAD_MAX);

    // Single keys with directions, and the right stick, on the second axis.
    input_test_key(BMF_KEY_I, 1);
    input_test_key(BMF_KEY_J, 1);
    bmf_input_sample_tick(&in);
    input_test_axis_eq(t, in.axis[INPUT_TEST_LOOK], -diagonal, diagonal, __LINE__, "I+J");
    input_test_axis_eq(t, in.axis[INPUT_TEST_MOVE], 0.0f, 0.0f, __LINE__, "move untouched");
    input_test_key(BMF_KEY_I, 0);
    input_test_key(BMF_KEY_J, 0);

    // Injected values are normalized the same way.
    bmf_input_inject_axis(1, 1, bmf_v2_make(3.0f, 4.0f));
    bmf_input_sample_tick(&in);
    input_test_axis_eq(t, in.axis[INPUT_TEST_MOVE], 0.6f, 0.8f, __LINE__, "injected (3, 4)");
    input_test_key(BMF_KEY_W, 1);
    bmf_input_sample_tick(&in);
    input_test_axis_eq(t, in.axis[INPUT_TEST_MOVE], 0.6f, 0.8f, __LINE__, "injection replaces bindings");
    bmf_input_inject_axis(1, 0, bmf_v2_make(0.0f, 0.0f));
    bmf_input_sample_tick(&in);
    input_test_axis_eq(t, in.axis[INPUT_TEST_MOVE], 0.0f, 1.0f, __LINE__, "back to bindings");
    input_test_key(BMF_KEY_W, 0);

    // Axes have no hidden latch: a tap inside one frame does not move the axis.
    input_test_key(BMF_KEY_D, 1);
    input_test_key(BMF_KEY_D, 0);
    bmf_input_sample_tick(&in);
    input_test_axis_eq(t, in.axis[INPUT_TEST_MOVE], 0.0f, 0.0f, __LINE__, "axis tap");
}

static void test_input_bindings(bmf_test* t) {
    input_test_init(t);
    bmf_input in;

    // Rebinding jump from Space to J.
    bmf_check_eq_i64(t, bmf_input_binding(0, 0), BMF_KEY_SPACE, __FILE__, __LINE__, "default binding");
    bmf_check_eq_i64(t, bmf_input_bind(0, 0, BMF_KEY_J), BMF_OK, __FILE__, __LINE__, "bind J");
    bmf_check_eq_i64(t, bmf_input_binding(0, 0), BMF_KEY_J, __FILE__, __LINE__, "bound J");
    input_test_key(BMF_KEY_SPACE, 1);
    bmf_input_sample_tick(&in);
    bmf_check(t, !bmf_input_down(&in, INPUT_TEST_JUMP), __FILE__, __LINE__, "space unbound");
    input_test_key(BMF_KEY_J, 1);
    bmf_input_sample_tick(&in);
    bmf_check(t, bmf_input_pressed(&in, INPUT_TEST_JUMP), __FILE__, __LINE__, "J presses jump");
    input_test_key(BMF_KEY_J, 0);
    input_test_key(BMF_KEY_SPACE, 0);
    bmf_input_sample_tick(&in);

    // Errors leave the binding alone.
    bmf_check_eq_i64(t, bmf_input_bind(0, 0, BMF_PAD_LSTICK), BMF_ERR_INVALID, __FILE__, __LINE__, "stick on a button");
    bmf_check_eq_i64(t, bmf_input_bind(1, 0, BMF_KEY_A), BMF_ERR_INVALID, __FILE__, __LINE__, "plain key on an axis");
    bmf_check_eq_i64(t, bmf_input_bind(3, 0, BMF_MOUSE_LEFT), BMF_ERR_INVALID, __FILE__, __LINE__, "binding on a pointer");
    bmf_check_eq_i64(t, bmf_input_bind(6, 0, BMF_KEY_A), BMF_ERR_RANGE, __FILE__, __LINE__, "action out of range");
    bmf_check_eq_i64(t, bmf_input_bind(0, BMF_ACTION_MAX_BINDINGS, BMF_KEY_A), BMF_ERR_RANGE, __FILE__, __LINE__, "slot out of range");
    bmf_check_eq_i64(t, bmf_input_bind(-1, 0, BMF_KEY_A), BMF_ERR_RANGE, __FILE__, __LINE__, "negative action");
    bmf_check_eq_i64(t, bmf_input_binding(0, 0), BMF_KEY_J, __FILE__, __LINE__, "unchanged after errors");

    // Rebinding an axis: arrows instead of WASD, and a direction key in a free slot.
    bmf_check_eq_i64(t, bmf_input_bind(1, 0, BMF_KEYS_ARROWS), BMF_OK, __FILE__, __LINE__, "bind arrows");
    bmf_check_eq_i64(t, bmf_input_bind(1, 5, BMF_KEY_Q | BMF_AXIS_LEFT), BMF_OK, __FILE__, __LINE__, "bind Q left");
    input_test_key(BMF_KEY_W, 1);
    input_test_key(BMF_KEY_RIGHT, 1);
    bmf_input_sample_tick(&in);
    bmf_check(t, in.axis[INPUT_TEST_MOVE].x == 1.0f && in.axis[INPUT_TEST_MOVE].y == 0.0f, __FILE__, __LINE__, "arrows, not WASD");
    input_test_key(BMF_KEY_Q, 1);
    bmf_input_sample_tick(&in);
    bmf_check(t, in.axis[INPUT_TEST_MOVE].x == 0.0f && in.axis[INPUT_TEST_MOVE].y == 0.0f, __FILE__, __LINE__, "Q cancels right");
    input_test_key(BMF_KEY_W, 0);
    input_test_key(BMF_KEY_RIGHT, 0);
    input_test_key(BMF_KEY_Q, 0);

    // Clearing a slot, and restoring the defaults.
    bmf_check_eq_i64(t, bmf_input_bind(5, 0, 0), BMF_OK, __FILE__, __LINE__, "clear escape");
    input_test_key(BMF_KEY_ESCAPE, 1);
    bmf_input_sample_tick(&in);
    bmf_check(t, !bmf_input_down(&in, INPUT_TEST_PAUSE), __FILE__, __LINE__, "cleared slot");
    input_test_key(BMF_KEY_ESCAPE, 0);
    bmf_input_reset_bindings();
    bmf_check(t, bmf_input_binding(0, 0) == BMF_KEY_SPACE && bmf_input_binding(1, 0) == BMF_KEYS_WASD &&
                 bmf_input_binding(1, 5) == 0 && bmf_input_binding(5, 0) == BMF_KEY_ESCAPE, __FILE__, __LINE__, "defaults restored");
    input_test_key(BMF_KEY_SPACE, 1);
    bmf_input_sample_tick(&in);
    bmf_check(t, bmf_input_pressed(&in, INPUT_TEST_JUMP), __FILE__, __LINE__, "space again");
    input_test_key(BMF_KEY_SPACE, 0);
    bmf_input_sample_tick(&in);

    // Gamepads: any connected pad; the trigger counts at BMF_INPUT_TRIGGER_DOWN; D-pad and Start.
    bmf_gamepad pads[BMF_GAMEPAD_MAX];
    bmf_memset(pads, 0, sizeof pads);
    pads[3].connected = 1;
    pads[3].right_trigger = 0.49f;
    pads[3].buttons = BMF_GAMEPAD_START;
    bmf_input_set_gamepads(pads, BMF_GAMEPAD_MAX);
    bmf_input_sample_tick(&in);
    bmf_check(t, !bmf_input_down(&in, INPUT_TEST_FIRE) && bmf_input_pressed(&in, INPUT_TEST_PAUSE), __FILE__, __LINE__, "trigger below, start");
    pads[3].right_trigger = 0.5f;
    bmf_input_set_gamepads(pads, BMF_GAMEPAD_MAX);
    bmf_input_sample_tick(&in);
    bmf_check(t, bmf_input_pressed(&in, INPUT_TEST_FIRE), __FILE__, __LINE__, "trigger at threshold");
    pads[3].connected = 0;   // unplugged: everything on it goes up
    bmf_input_set_gamepads(pads, BMF_GAMEPAD_MAX);
    bmf_input_sample_tick(&in);
    bmf_check(t, bmf_input_released(&in, INPUT_TEST_FIRE) && bmf_input_released(&in, INPUT_TEST_PAUSE), __FILE__, __LINE__, "disconnect releases");
    bmf_check_eq_i64(t, bmf_input_bind(1, 2, BMF_PAD_DPAD), BMF_OK, __FILE__, __LINE__, "bind dpad");
    pads[0].connected = 1;
    pads[0].buttons = BMF_GAMEPAD_DPAD_LEFT | BMF_GAMEPAD_DPAD_DOWN;
    bmf_input_set_gamepads(pads, 1);
    bmf_input_sample_tick(&in);
    bmf_check(t, in.axis[INPUT_TEST_MOVE].x < 0.0f && in.axis[INPUT_TEST_MOVE].y < 0.0f, __FILE__, __LINE__, "dpad down-left");
}

static void test_input_pointers(bmf_test* t) {
    input_test_init(t);
    bmf_draw2d_view view = { 200, 100, 2.0f };   // 100 x 50 logical pixels
    bmf_input_set_view(view);
    bmf_v2 view_size = bmf_v2_make(100.0f, 50.0f);
    bmf_camera world = bmf_camera_world_default();
    bmf_input in;

    bmf_input_sample_tick(&in);
    bmf_check_eq_i64(t, in.pointer[0].id, 0, __FILE__, __LINE__, "no mouse seen yet");

    // Mouse: physical pixels / DPI = logical; world through the default camera.
    input_test_mouse_move(40.0f, 20.0f);
    bmf_input_sample_tick(&in);
    bmf_v2 expected_world = bmf_camera_screen_to_world(&world, view_size, bmf_v2_make(20.0f, 10.0f));
    bmf_check(t, in.pointer[0].id == 1 && in.pointer[0].screen.x == 20.0f && in.pointer[0].screen.y == 10.0f && !in.pointer[0].down,
              __FILE__, __LINE__, "mouse hover");
    bmf_check(t, in.pointer[0].world.x == expected_world.x && in.pointer[0].world.y == expected_world.y, __FILE__, __LINE__, "mouse world");
    bmf_check(t, expected_world.x == -0.46875f && expected_world.y == 0.234375f, __FILE__, __LINE__, "world = ((20 - 50) / 64, (25 - 10) / 64)");
    input_test_mouse_button(BMF_MOUSE_BUTTON_LEFT, 1);
    input_test_mouse_button(BMF_MOUSE_BUTTON_LEFT, 0);
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[0].pressed && in.pointer[0].down, __FILE__, __LINE__, "mouse click pressed");
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[0].released && !in.pointer[0].down && in.pointer[0].id == 1, __FILE__, __LINE__, "mouse click released");
    bmf_check(t, !bmf_input_down(&in, INPUT_TEST_FIRE), __FILE__, __LINE__, "left button is not fire");

    // Touch while the mouse button is held: slots 1 and 2, ids 2 and 3.
    input_test_mouse_button(BMF_MOUSE_BUTTON_LEFT, 1);
    input_test_touch(BMF_EVENT_POINTER_DOWN, 7, 100.0f, 50.0f);
    input_test_touch(BMF_EVENT_POINTER_DOWN, 2, 10.0f, 12.0f);
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[0].id == 1 && in.pointer[0].pressed, __FILE__, __LINE__, "mouse down in slot 0");
    bmf_check(t, in.pointer[1].id == 2 && in.pointer[1].pressed && in.pointer[1].screen.x == 50.0f && in.pointer[1].screen.y == 25.0f,
              __FILE__, __LINE__, "first touch in slot 1");
    bmf_check(t, in.pointer[1].world.x == 0.0f && in.pointer[1].world.y == 0.0f, __FILE__, __LINE__, "view center is world origin");
    bmf_check(t, in.pointer[2].id == 3 && in.pointer[2].pressed && in.pointer[2].screen.x == 5.0f, __FILE__, __LINE__, "second touch in slot 2");
    input_test_touch(BMF_EVENT_POINTER_MOVE, 7, 120.0f, 50.0f);
    input_test_touch(BMF_EVENT_POINTER_MOVE, 9, 0.0f, 0.0f);   // unknown contact: ignored
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[1].screen.x == 60.0f && in.pointer[1].down && !in.pointer[1].pressed, __FILE__, __LINE__, "touch move");

    // Mouse released: a new touch takes slot 0 and mouse motion no longer moves it.
    input_test_mouse_button(BMF_MOUSE_BUTTON_LEFT, 0);
    bmf_input_sample_tick(&in);
    input_test_touch(BMF_EVENT_POINTER_DOWN, 4, 30.0f, 30.0f);
    input_test_mouse_move(0.0f, 0.0f);
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[0].id == 4 && in.pointer[0].pressed && in.pointer[0].screen.x == 15.0f, __FILE__, __LINE__, "touch in slot 0");
    input_test_touch(BMF_EVENT_POINTER_DOWN, 5, 2.0f, 2.0f);   // slot 3
    input_test_touch(BMF_EVENT_POINTER_DOWN, 6, 4.0f, 4.0f);   // no slot left: ignored
    input_test_touch(BMF_EVENT_POINTER_UP, 6, 4.0f, 4.0f);
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[3].id == 5 && in.pointer[3].pressed, __FILE__, __LINE__, "fourth contact");

    // A touch ends: released with its id and last position (a forced release carries 0, 0),
    // then the slot is empty; the mouse gets slot 0 back when it moves.
    input_test_touch(BMF_EVENT_POINTER_UP, 4, 0.0f, 0.0f);
    input_test_mouse_move(80.0f, 80.0f);   // ignored: the release is not reported yet
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[0].id == 4 && in.pointer[0].released && in.pointer[0].screen.x == 15.0f, __FILE__, __LINE__, "touch released");
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[0].id == 0, __FILE__, __LINE__, "slot empty after release");
    input_test_mouse_move(80.0f, 80.0f);
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[0].id == 1 && in.pointer[0].screen.x == 40.0f, __FILE__, __LINE__, "mouse back in slot 0");

    // A tap shorter than a frame on a touch screen is one press. It takes slot 0 from the
    // hovering mouse (button up), which gets the slot back on its next move.
    input_test_touch(BMF_EVENT_POINTER_UP, 7, 0.0f, 0.0f);
    input_test_touch(BMF_EVENT_POINTER_UP, 2, 0.0f, 0.0f);
    input_test_touch(BMF_EVENT_POINTER_UP, 5, 0.0f, 0.0f);
    bmf_input_sample_tick(&in);
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[1].id == 0 && in.pointer[2].id == 0 && in.pointer[3].id == 0, __FILE__, __LINE__, "all touches ended");
    input_test_touch(BMF_EVENT_POINTER_DOWN, 1, 8.0f, 8.0f);
    input_test_touch(BMF_EVENT_POINTER_UP, 1, 8.0f, 8.0f);
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[0].id == 6 && in.pointer[0].pressed && in.pointer[0].down && in.pointer[0].screen.x == 4.0f, __FILE__, __LINE__, "touch tap pressed");
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[0].id == 6 && in.pointer[0].released, __FILE__, __LINE__, "touch tap released");
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[0].id == 0, __FILE__, __LINE__, "touch tap gone");
    input_test_mouse_move(80.0f, 80.0f);

    // Another camera: a zoomed, moved world camera; a screen camera gives screen coordinates.
    bmf_camera zoomed = { BMF_CAMERA_WORLD, { 3.0f, -2.0f }, 32.0f, 0.0f };
    bmf_input_set_pointer_camera(&zoomed);
    bmf_input_sample_tick(&in);
    expected_world = bmf_camera_screen_to_world(&zoomed, view_size, bmf_v2_make(40.0f, 40.0f));
    bmf_check(t, in.pointer[0].world.x == expected_world.x && in.pointer[0].world.y == expected_world.y &&
                 expected_world.x == 3.0f - 10.0f / 32.0f && expected_world.y == -2.0f - 15.0f / 32.0f, __FILE__, __LINE__, "zoomed camera");
    bmf_camera screen = bmf_camera_screen_default();
    bmf_input_set_pointer_camera(&screen);
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[0].world.x == 40.0f && in.pointer[0].world.y == 40.0f, __FILE__, __LINE__, "screen camera");

    // Without a POINTER action the pointers stay zero.
    static const bmf_action no_pointer[] = { { "jump", BMF_ACTION_BUTTON, { BMF_KEY_SPACE } } };
    char error[128];
    bmf_check_eq_i64(t, bmf_input_init(no_pointer, 1, error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, error);
    input_test_mouse_move(10.0f, 10.0f);
    input_test_mouse_button(BMF_MOUSE_BUTTON_LEFT, 1);
    bmf_input_sample_tick(&in);
    bmf_pointer zero[BMF_INPUT_MAX_POINTERS];
    bmf_memset(zero, 0, sizeof zero);
    bmf_check_eq_mem(t, in.pointer, zero, sizeof zero, __FILE__, __LINE__, "no POINTER action");
}

static void test_input_injection(bmf_test* t) {
    input_test_init(t);
    bmf_draw2d_view view = { 640, 360, 1.0f };
    bmf_input_set_view(view);
    char record[32];
    i32 length = 0;

    // `press jump 3`: down before the first tick, up after the third.
    bmf_input_inject_button(0, 1);
    input_test_run(3, INPUT_TEST_JUMP, record, &length);
    bmf_input_inject_button(0, 0);
    input_test_run(2, INPUT_TEST_JUMP, record, &length);
    input_test_record_eq(t, record, "PDDR.", __LINE__, "injected hold");

    // Injection and a key together: one press, released when both are up.
    length = 0;
    bmf_input_inject_button(0, 1);
    input_test_run(1, INPUT_TEST_JUMP, record, &length);
    input_test_key(BMF_KEY_SPACE, 1);
    bmf_input_inject_button(0, 0);
    input_test_run(1, INPUT_TEST_JUMP, record, &length);
    input_test_key(BMF_KEY_SPACE, 0);
    input_test_run(1, INPUT_TEST_JUMP, record, &length);
    input_test_record_eq(t, record, "PDR", __LINE__, "injection plus key");

    // A one-tick press injected and released before the tick still presses.
    length = 0;
    bmf_input_inject_button(0, 1);
    bmf_input_inject_button(0, 0);
    input_test_run(3, INPUT_TEST_JUMP, record, &length);
    input_test_record_eq(t, record, "PR.", __LINE__, "injected tap");

    // Pointers in camera space: mapped to logical pixels at once with the current camera.
    bmf_input in;
    bmf_input_inject_pointer(2, BMF_INPUT_POINTER_DOWN, bmf_v2_make(1.5f, -0.5f), 1);
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[2].pressed && in.pointer[2].id >= 2 && in.pointer[2].screen.x == 320.0f + 96.0f && in.pointer[2].screen.y == 180.0f + 32.0f,
              __FILE__, __LINE__, "injected world position on screen");
    bmf_check(t, in.pointer[2].world.x == 1.5f && in.pointer[2].world.y == -0.5f, __FILE__, __LINE__, "injected world position round trip");
    u32 id = in.pointer[2].id;
    bmf_input_inject_pointer(2, BMF_INPUT_POINTER_MOVE, bmf_v2_make(10.0f, 20.0f), 0);
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[2].down && in.pointer[2].id == id && in.pointer[2].screen.x == 10.0f && in.pointer[2].screen.y == 20.0f,
              __FILE__, __LINE__, "injected move in pixels");
    bmf_input_inject_pointer(2, BMF_INPUT_POINTER_UP, bmf_v2_make(11.0f, 20.0f), 0);
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[2].released && in.pointer[2].id == id && in.pointer[2].screen.x == 11.0f, __FILE__, __LINE__, "injected up");
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[2].id == 0, __FILE__, __LINE__, "injected pointer gone");

    // An injection takes a touch's slot over; that contact's later events are ignored.
    input_test_touch(BMF_EVENT_POINTER_DOWN, 3, 50.0f, 50.0f);   // slot 0: the mouse was never seen
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[0].pressed, __FILE__, __LINE__, "touch in slot 0");
    bmf_input_inject_pointer(0, BMF_INPUT_POINTER_MOVE, bmf_v2_make(5.0f, 5.0f), 0);
    input_test_touch(BMF_EVENT_POINTER_MOVE, 3, 60.0f, 60.0f);
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[0].released && in.pointer[0].screen.x == 5.0f, __FILE__, __LINE__, "takeover releases the touch");
    input_test_touch(BMF_EVENT_POINTER_UP, 3, 60.0f, 60.0f);
    bmf_input_inject_pointer(0, BMF_INPUT_POINTER_DOWN, bmf_v2_make(6.0f, 5.0f), 0);
    bmf_input_sample_tick(&in);
    bmf_check(t, in.pointer[0].pressed && in.pointer[0].screen.x == 6.0f, __FILE__, __LINE__, "injected press after takeover");

    // Raw keys reach the bindings through bmf_input_event (the protocol's `key NAME down|up`).
    bmf_event event;
    bmf_memset(&event, 0, sizeof event);
    event.type = BMF_EVENT_KEY_DOWN;
    event.key.key = bmf_key_from_name(bmf_str_from_cstr("escape"));
    bmf_input_event(&event);
    bmf_input_sample_tick(&in);
    bmf_check(t, bmf_input_pressed(&in, INPUT_TEST_PAUSE), __FILE__, __LINE__, "raw key by name");
}

static void test_input_text(bmf_test* t) {
    input_test_init(t);
    bmf_event event;
    bmf_memset(&event, 0, sizeof event);
    event.type = BMF_EVENT_TEXT;
    for (u32 i = 0; i < 12; i++) {
        event.text.codepoint = 0x61 + i;
        bmf_input_event(&event);
    }
    bmf_input in;
    bmf_input_sample_tick(&in);
    bmf_check(t, in.text_len == 8 && in.text[0] == 0x61 && in.text[7] == 0x68, __FILE__, __LINE__, "first 8 codepoints");
    bmf_input_sample_tick(&in);
    bmf_check(t, in.text_len == 4 && in.text[0] == 0x69 && in.text[3] == 0x6C && in.text[4] == 0, __FILE__, __LINE__, "remaining 4");
    bmf_input_sample_tick(&in);
    bmf_check_eq_i64(t, in.text_len, 0, __FILE__, __LINE__, "queue empty");

    for (u32 i = 0; i < BMF_INPUT_TEXT_QUEUE + 6; i++) {
        event.text.codepoint = 0x1F600 + i;
        bmf_input_event(&event);
    }
    bmf_check_eq_i64(t, bmf_input_text_dropped(), 6, __FILE__, __LINE__, "queue overflow drops the newest");
    i32 total = 0;
    u32 last = 0;
    for (i32 tick = 0; tick < 10; tick++) {
        bmf_input_sample_tick(&in);
        total += in.text_len;
        if (in.text_len) last = in.text[in.text_len - 1];
    }
    bmf_check(t, total == BMF_INPUT_TEXT_QUEUE && last == 0x1F600 + BMF_INPUT_TEXT_QUEUE - 1, __FILE__, __LINE__, "64 codepoints in order");
}
