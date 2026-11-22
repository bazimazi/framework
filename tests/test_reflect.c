// Tests for src/debug/reflect.c (task 1A-30): field-table checks and hash, path parsing, get/set
// of every field type, live-count bounds, dump, suggestions, the NaN/Inf scan, and the engine's
// reflection of a running game. Uses core_test_game and core_test_contains from test_core.c.

typedef struct reflect_test_state {
    i32 score;              // 0
    u32 flags;              // 4
    i64 big;                // 8
    u64 ubig;               // 16
    f32 speed;              // 24
    bmf_v2 pos;             // 28
    bmf_v3 p3;              // 36
    bmf_v4 p4;              // 48
    b32 alive;              // 64
    bmf_handle target;      // 68
    u8 lives;               // 72
    u8 padding[3];
    i32 ball_count;         // 76
    bmf_rng rng;            // 80
    bmf_v2 ball_pos[8];     // 96
    i32 ball_hp[8];         // 160
    f32 ball_mass[4];       // 192
} reflect_test_state;

static const bmf_field REFLECT_TEST_FIELDS[] = {
    { "score", BMF_T_I32, offsetof(reflect_test_state, score), 1, 0 },
    { "flags", BMF_T_U32, offsetof(reflect_test_state, flags), 1, 0 },
    { "big", BMF_T_I64, offsetof(reflect_test_state, big), 1, 0 },
    { "ubig", BMF_T_U64, offsetof(reflect_test_state, ubig), 1, 0 },
    { "speed", BMF_T_F32, offsetof(reflect_test_state, speed), 1, 0 },
    { "pos", BMF_T_V2, offsetof(reflect_test_state, pos), 1, 0 },
    { "p3", BMF_T_V3, offsetof(reflect_test_state, p3), 1, 0 },
    { "p4", BMF_T_V4, offsetof(reflect_test_state, p4), 1, 0 },
    { "alive", BMF_T_B32, offsetof(reflect_test_state, alive), 1, 0 },
    { "target", BMF_T_HANDLE, offsetof(reflect_test_state, target), 1, 0 },
    { "lives", BMF_T_U8, offsetof(reflect_test_state, lives), 1, 0 },
    { "ball.count", BMF_T_I32, offsetof(reflect_test_state, ball_count), 1, 0 },
    { "rng", BMF_T_RNG, offsetof(reflect_test_state, rng), 1, 0 },
    { "ball.pos", BMF_T_V2, offsetof(reflect_test_state, ball_pos), 8, "ball.count" },
    { "ball.hp", BMF_T_I32, offsetof(reflect_test_state, ball_hp), 8, 0 },
    { "ball.mass", BMF_T_F32, offsetof(reflect_test_state, ball_mass), 4, "ball.count" },
};
enum { REFLECT_TEST_FIELD_COUNT = (i32)(sizeof REFLECT_TEST_FIELDS / sizeof REFLECT_TEST_FIELDS[0]) };

static reflect_test_state reflect_test_data;
static i64 reflect_test_tick;

static bmf_reflect reflect_test_make(void) {
    bmf_reflect r = { REFLECT_TEST_FIELDS, REFLECT_TEST_FIELD_COUNT, (u8*)&reflect_test_data, (i32)sizeof reflect_test_data, &reflect_test_tick };
    return r;
}

// get `path` and compare the JSON text.
static void reflect_test_get(bmf_test* t, const bmf_reflect* r, const char* path, const char* expected, i32 line) {
    char buf[2048];
    bmf_json_writer w;
    bmf_json_init(&w, buf, (i32)sizeof buf);
    char detail[256];
    bmf_reflect_status status = bmf_reflect_get(r, bmf_str_from_cstr(path), &w, detail, (i32)sizeof detail);
    char message[2600];
    bmf_fmt_buf(message, (i32)sizeof message, "get %s: expected %s, got %s (status %s: %s)", path, expected, buf, bmf_reflect_status_code(status), detail);
    bmf_check(t, status == BMF_REFLECT_OK && bmf_cstr_eq(buf, expected), __FILE__, line, message);
}

// get `path` must fail with `expected_status` and a detail containing `expected_detail`.
static void reflect_test_get_error(bmf_test* t, const bmf_reflect* r, const char* path, bmf_reflect_status expected_status,
                                   const char* expected_detail, i32 line) {
    char buf[256];
    bmf_json_writer w;
    bmf_json_init(&w, buf, (i32)sizeof buf);
    char detail[256];
    bmf_reflect_status status = bmf_reflect_get(r, bmf_str_from_cstr(path), &w, detail, (i32)sizeof detail);
    char message[800];
    bmf_fmt_buf(message, (i32)sizeof message, "get '%s': expected %s \"%s\", got %s \"%s\"", path, bmf_reflect_status_code(expected_status),
                expected_detail, bmf_reflect_status_code(status), detail);
    bmf_check(t, status == expected_status && core_test_contains(detail, expected_detail) && w.len == 0, __FILE__, line, message);
}

// set `path` to the space-separated `values`; expects `expected_status` (and a detail containing
// `expected_detail` on failure).
static void reflect_test_set(bmf_test* t, const bmf_reflect* r, const char* path, const char* values, bmf_reflect_status expected_status,
                             const char* expected_detail, i32 line) {
    bmf_str tokens[8];
    i32 count = 0;
    bmf_str_splitter it = bmf_str_split(bmf_str_from_cstr(values), ' ');
    bmf_str piece;
    while (count < 8 && bmf_str_split_next(&it, &piece)) if (piece.len > 0) tokens[count++] = piece;
    char detail[256];
    bmf_reflect_status status = bmf_reflect_set(r, bmf_str_from_cstr(path), tokens, count, detail, (i32)sizeof detail);
    char message[800];
    bmf_fmt_buf(message, (i32)sizeof message, "set %s %s: expected %s \"%s\", got %s \"%s\"", path, values,
                bmf_reflect_status_code(expected_status), expected_detail ? expected_detail : "", bmf_reflect_status_code(status), detail);
    b32 ok = status == expected_status && (expected_status == BMF_REFLECT_OK || core_test_contains(detail, expected_detail));
    bmf_check(t, ok, __FILE__, line, message);
}

static void reflect_test_fill(void) {
    bmf_memset(&reflect_test_data, 0, sizeof reflect_test_data);
    reflect_test_data.score = -7;
    reflect_test_data.flags = 4000000000u;
    reflect_test_data.big = -9223372036854775807ll - 1;
    reflect_test_data.ubig = 18446744073709551615ull;
    reflect_test_data.speed = 0.1f;
    reflect_test_data.pos = bmf_v2_make(1.5f, -2.0f);
    reflect_test_data.p3 = bmf_v3_make(0.0f, -0.0f, 1e-7f);
    reflect_test_data.p4 = bmf_v4_make(1.0f, 2.0f, 3.0f, 1e21f);
    reflect_test_data.alive = 1;
    reflect_test_data.target = 0x00100003u;
    reflect_test_data.lives = 255;
    reflect_test_data.ball_count = 3;
    reflect_test_data.rng.state = 12345678901234567890ull;
    reflect_test_data.rng.inc = 7;
    for (i32 i = 0; i < 8; i++) {
        reflect_test_data.ball_pos[i] = bmf_v2_make((f32)i, (f32)(i * 10));
        reflect_test_data.ball_hp[i] = 100 + i;
    }
    for (i32 i = 0; i < 4; i++) reflect_test_data.ball_mass[i] = 0.5f * (f32)(i + 1);
    reflect_test_tick = 42;
}

// ---------------------------------------------------------------- reflect.fields_hash
static void test_reflect_fields_hash(bmf_test* t) {
    char error[256];
    bmf_check_eq_i64(t, bmf_reflect_check_fields(REFLECT_TEST_FIELDS, REFLECT_TEST_FIELD_COUNT, (i32)sizeof(reflect_test_state), error, (i32)sizeof error),
                     BMF_OK, __FILE__, __LINE__, error);
    bmf_check_eq_i64(t, (i64)sizeof(reflect_test_state), 208, __FILE__, __LINE__, "test state layout");

    // Every type: size, alignment, name.
    const i32 sizes[BMF_T_COUNT] = { 4, 4, 8, 8, 4, 8, 12, 16, 4, 1, 4, 16 };
    const i32 aligns[BMF_T_COUNT] = { 4, 4, 8, 8, 4, 4, 4, 4, 4, 1, 4, 8 };
    const char* const names[BMF_T_COUNT] = { "i32", "u32", "i64", "u64", "f32", "v2", "v3", "v4", "b32", "u8", "handle", "rng" };
    for (i32 type = 0; type < BMF_T_COUNT; type++) {
        bmf_check_eq_i64(t, bmf_field_type_size((bmf_field_type)type), sizes[type], __FILE__, __LINE__, names[type]);
        bmf_check_eq_i64(t, bmf_field_type_align((bmf_field_type)type), aligns[type], __FILE__, __LINE__, names[type]);
        bmf_check(t, bmf_cstr_eq(bmf_field_type_name((bmf_field_type)type), names[type]), __FILE__, __LINE__, names[type]);
    }
    bmf_check_eq_i64(t, bmf_field_type_size(BMF_T_COUNT), 0, __FILE__, __LINE__, "invalid type size");
    bmf_check_eq_i64(t, bmf_field_type_align(BMF_T_COUNT), 0, __FILE__, __LINE__, "invalid type align");
    bmf_check(t, bmf_cstr_eq(bmf_field_type_name(BMF_T_COUNT), "?"), __FILE__, __LINE__, "invalid type name");

    // RNG needs 8-byte alignment.
    static const bmf_field rng_misaligned[] = { { "rng", BMF_T_RNG, 4, 1, 0 } };
    bmf_check_eq_i64(t, bmf_reflect_check_fields(rng_misaligned, 1, 64, error, (i32)sizeof error), BMF_ERR_INVALID, __FILE__, __LINE__, "rng at offset 4");
    bmf_check(t, core_test_contains(error, "field 0 \"rng\": offset 4 is not a multiple of 8"), __FILE__, __LINE__, error);

    // The hash: pinned (identical on every target), 0 only for an empty table, and sensitive to
    // the name, type, offset, count and order of every field but not to count_field.
    u64 hash = bmf_fields_hash(REFLECT_TEST_FIELDS, REFLECT_TEST_FIELD_COUNT);
    char message[128];
    bmf_fmt_buf(message, (i32)sizeof message, "pinned layout hash, got 0x%lx", hash);
    bmf_check(t, hash == 0x32c35914c1f6e7cbull, __FILE__, __LINE__, message);
    bmf_check(t, bmf_fields_hash(REFLECT_TEST_FIELDS, 0) == 0 && bmf_fields_hash(0, 0) == 0, __FILE__, __LINE__, "empty table hashes to 0");
    bmf_field changed[REFLECT_TEST_FIELD_COUNT];
    for (i32 variant = 0; variant < 6; variant++) {
        for (i32 f = 0; f < REFLECT_TEST_FIELD_COUNT; f++) changed[f] = REFLECT_TEST_FIELDS[f];
        const char* what = "";
        if (variant == 0) { changed[3].name = "ubiG"; what = "name"; }
        if (variant == 1) { changed[1].type = BMF_T_I32; what = "type"; }
        if (variant == 2) { changed[14].offset += 4; what = "offset"; }
        if (variant == 3) { changed[14].count = 7; what = "count"; }
        if (variant == 4) { changed[0] = REFLECT_TEST_FIELDS[1]; changed[1] = REFLECT_TEST_FIELDS[0]; what = "order"; }
        if (variant == 5) { changed[13].count_field = 0; what = "count_field (not hashed)"; }
        u64 other = bmf_fields_hash(changed, REFLECT_TEST_FIELD_COUNT);
        bmf_check(t, variant == 5 ? other == hash : other != hash, __FILE__, __LINE__, what);
    }
    // Name boundaries are part of the hash: "ab" + "c" differs from "a" + "bc".
    bmf_field split_a[2] = { { "ab", BMF_T_I32, 0, 1, 0 }, { "c", BMF_T_I32, 4, 1, 0 } };
    bmf_field split_b[2] = { { "a", BMF_T_I32, 0, 1, 0 }, { "bc", BMF_T_I32, 4, 1, 0 } };
    bmf_check(t, bmf_fields_hash(split_a, 2) != bmf_fields_hash(split_b, 2), __FILE__, __LINE__, "name boundaries");
}

// ---------------------------------------------------------------- reflect.get
static void test_reflect_get(bmf_test* t) {
    reflect_test_fill();
    bmf_reflect r = reflect_test_make();
    reflect_test_get(t, &r, "score", "-7", __LINE__);
    reflect_test_get(t, &r, "flags", "4000000000", __LINE__);
    reflect_test_get(t, &r, "big", "-9223372036854775808", __LINE__);
    reflect_test_get(t, &r, "ubig", "18446744073709551615", __LINE__);
    reflect_test_get(t, &r, "speed", "0.1", __LINE__);
    reflect_test_get(t, &r, "pos", "[1.5,-2]", __LINE__);
    reflect_test_get(t, &r, "p3", "[0,-0,1e-7]", __LINE__);
    reflect_test_get(t, &r, "p4", "[1,2,3,1e+21]", __LINE__);
    reflect_test_get(t, &r, "alive", "true", __LINE__);
    reflect_test_data.alive = 0;
    reflect_test_get(t, &r, "alive", "false", __LINE__);
    reflect_test_data.alive = 5;
    reflect_test_get(t, &r, "alive", "5", __LINE__);   // not 0 or 1: shown as it is
    reflect_test_get(t, &r, "target", "1048579", __LINE__);
    reflect_test_get(t, &r, "lives", "255", __LINE__);
    reflect_test_get(t, &r, "ball.count", "3", __LINE__);
    reflect_test_get(t, &r, "rng", "[12345678901234567890,7]", __LINE__);
    reflect_test_get(t, &r, "tick", "42", __LINE__);
    // Without a tick pointer there is no built-in tick path.
    bmf_reflect no_tick = r;
    no_tick.tick = 0;
    reflect_test_get_error(t, &no_tick, "tick", BMF_REFLECT_UNKNOWN_PATH, "no field 'tick'", __LINE__);
}

// ---------------------------------------------------------------- reflect.arrays
static void test_reflect_arrays(bmf_test* t) {
    reflect_test_fill();
    bmf_reflect r = reflect_test_make();
    // Live count 3: whole array, [*] and single elements are bounded by it.
    reflect_test_get(t, &r, "ball.pos", "[[0,0],[1,10],[2,20]]", __LINE__);
    reflect_test_get(t, &r, "ball.pos[*]", "[[0,0],[1,10],[2,20]]", __LINE__);
    reflect_test_get(t, &r, "ball.pos[2]", "[2,20]", __LINE__);
    reflect_test_get(t, &r, "ball.pos[0]", "[0,0]", __LINE__);
    reflect_test_get_error(t, &r, "ball.pos[3]", BMF_REFLECT_INDEX_OUT_OF_RANGE, "ball.pos[3]: index 3 out of range: live count 3 (ball.count), capacity 8", __LINE__);
    reflect_test_get_error(t, &r, "ball.pos[8]", BMF_REFLECT_INDEX_OUT_OF_RANGE, "live count 3", __LINE__);
    reflect_test_get(t, &r, "ball.mass", "[0.5,1,1.5]", __LINE__);
    bmf_check_eq_i64(t, bmf_reflect_live_count(&r, 13), 3, __FILE__, __LINE__, "live count of ball.pos");
    // No count field: every element is live.
    reflect_test_get(t, &r, "ball.hp", "[100,101,102,103,104,105,106,107]", __LINE__);
    reflect_test_get(t, &r, "ball.hp[7]", "107", __LINE__);
    reflect_test_get_error(t, &r, "ball.hp[8]", BMF_REFLECT_INDEX_OUT_OF_RANGE, "ball.hp[8]: index 8 out of range: count 8", __LINE__);
    // The live count is clamped to 0..capacity per array.
    reflect_test_data.ball_count = 6;
    reflect_test_get(t, &r, "ball.mass", "[0.5,1,1.5,2]", __LINE__);
    bmf_check_eq_i64(t, bmf_reflect_live_count(&r, 15), 4, __FILE__, __LINE__, "clamped to capacity 4");
    bmf_check_eq_i64(t, bmf_reflect_live_count(&r, 13), 6, __FILE__, __LINE__, "6 of 8");
    reflect_test_data.ball_count = 100;
    bmf_check_eq_i64(t, bmf_reflect_live_count(&r, 13), 8, __FILE__, __LINE__, "clamped to capacity 8");
    reflect_test_data.ball_count = -5;
    reflect_test_get(t, &r, "ball.pos", "[]", __LINE__);
    reflect_test_get_error(t, &r, "ball.pos[0]", BMF_REFLECT_INDEX_OUT_OF_RANGE, "live count 0", __LINE__);
    bmf_check_eq_i64(t, bmf_reflect_live_count(&r, 0), 1, __FILE__, __LINE__, "a scalar has 1");
}

// ---------------------------------------------------------------- reflect.paths
static void test_reflect_paths(bmf_test* t) {
    reflect_test_fill();
    bmf_reflect r = reflect_test_make();
    reflect_test_get_error(t, &r, "", BMF_REFLECT_BAD_PATH, "empty field name", __LINE__);
    reflect_test_get_error(t, &r, "[1]", BMF_REFLECT_BAD_PATH, "empty field name", __LINE__);
    reflect_test_get_error(t, &r, "score[0]", BMF_REFLECT_BAD_PATH, "'score' is not an array", __LINE__);
    reflect_test_get_error(t, &r, "tick[0]", BMF_REFLECT_BAD_PATH, "'tick' is not an array", __LINE__);
    reflect_test_get_error(t, &r, "ball.pos[", BMF_REFLECT_BAD_PATH, "use name, name[i] or name[*]", __LINE__);
    reflect_test_get_error(t, &r, "ball.pos[]", BMF_REFLECT_BAD_PATH, "use name, name[i] or name[*]", __LINE__);
    reflect_test_get_error(t, &r, "ball.pos[1]x", BMF_REFLECT_BAD_PATH, "use name, name[i] or name[*]", __LINE__);
    reflect_test_get_error(t, &r, "ball.pos[-1]", BMF_REFLECT_BAD_PATH, "bad index '-1'", __LINE__);
    reflect_test_get_error(t, &r, "ball.pos[1x]", BMF_REFLECT_BAD_PATH, "bad index '1x'", __LINE__);
    reflect_test_get_error(t, &r, "ball.pos[+1]", BMF_REFLECT_BAD_PATH, "bad index '+1'", __LINE__);
    reflect_test_get_error(t, &r, "ball.pos[99999999999]", BMF_REFLECT_BAD_PATH, "bad index '99999999999'", __LINE__);
    reflect_test_get_error(t, &r, "ball.pos[**]", BMF_REFLECT_BAD_PATH, "bad index '**'", __LINE__);
    reflect_test_get_error(t, &r, "ball.pos]", BMF_REFLECT_UNKNOWN_PATH, "no field 'ball.pos]'", __LINE__);
    reflect_test_get_error(t, &r, "Score", BMF_REFLECT_UNKNOWN_PATH, "did you mean 'score'?", __LINE__);   // names are case-sensitive
    bmf_reflect_target target;
    char detail[128];
    bmf_check_eq_i64(t, bmf_reflect_resolve(&r, bmf_str_from_cstr("ball.pos[2]"), &target, detail, (i32)sizeof detail), BMF_REFLECT_OK, __FILE__, __LINE__, detail);
    bmf_check(t, target.field == 13 && target.index == 2, __FILE__, __LINE__, "resolved field and index");
    bmf_check_eq_i64(t, bmf_reflect_resolve(&r, bmf_str_from_cstr("tick"), &target, detail, (i32)sizeof detail), BMF_REFLECT_OK, __FILE__, __LINE__, detail);
    bmf_check(t, target.field == BMF_REFLECT_TICK && target.index == -1, __FILE__, __LINE__, "tick resolves to the built-in");
    for (i32 s = 0; s < BMF_REFLECT_STATUS_COUNT; s++) bmf_check(t, !bmf_cstr_eq(bmf_reflect_status_code((bmf_reflect_status)s), "?"), __FILE__, __LINE__, "status code");
    bmf_check(t, bmf_cstr_eq(bmf_reflect_status_code(BMF_REFLECT_INDEX_OUT_OF_RANGE), "index_out_of_range"), __FILE__, __LINE__, "status code text");
}

// ---------------------------------------------------------------- reflect.set
static void test_reflect_set(bmf_test* t) {
    reflect_test_fill();
    bmf_reflect r = reflect_test_make();
    // Every type, read back with get.
    reflect_test_set(t, &r, "score", "-2147483648", BMF_REFLECT_OK, 0, __LINE__);
    reflect_test_get(t, &r, "score", "-2147483648", __LINE__);
    reflect_test_set(t, &r, "flags", "0xFFFFFFFF", BMF_REFLECT_OK, 0, __LINE__);
    reflect_test_get(t, &r, "flags", "4294967295", __LINE__);
    reflect_test_set(t, &r, "big", "9223372036854775807", BMF_REFLECT_OK, 0, __LINE__);
    reflect_test_get(t, &r, "big", "9223372036854775807", __LINE__);
    reflect_test_set(t, &r, "ubig", "0xfedcba9876543210", BMF_REFLECT_OK, 0, __LINE__);
    reflect_test_get(t, &r, "ubig", "18364758544493064720", __LINE__);
    reflect_test_set(t, &r, "speed", "-0.25", BMF_REFLECT_OK, 0, __LINE__);
    reflect_test_get(t, &r, "speed", "-0.25", __LINE__);
    reflect_test_set(t, &r, "pos", "3 4.5", BMF_REFLECT_OK, 0, __LINE__);
    reflect_test_get(t, &r, "pos", "[3,4.5]", __LINE__);
    reflect_test_set(t, &r, "p3", "1 2 3", BMF_REFLECT_OK, 0, __LINE__);
    reflect_test_get(t, &r, "p3", "[1,2,3]", __LINE__);
    reflect_test_set(t, &r, "p4", "0.1 0.2 0.3 0.4", BMF_REFLECT_OK, 0, __LINE__);
    reflect_test_get(t, &r, "p4", "[0.1,0.2,0.3,0.4]", __LINE__);
    reflect_test_set(t, &r, "alive", "false", BMF_REFLECT_OK, 0, __LINE__);
    reflect_test_get(t, &r, "alive", "false", __LINE__);
    reflect_test_set(t, &r, "alive", "1", BMF_REFLECT_OK, 0, __LINE__);
    reflect_test_get(t, &r, "alive", "true", __LINE__);
    reflect_test_set(t, &r, "target", "17", BMF_REFLECT_OK, 0, __LINE__);
    reflect_test_get(t, &r, "target", "17", __LINE__);
    reflect_test_set(t, &r, "lives", "0", BMF_REFLECT_OK, 0, __LINE__);
    reflect_test_get(t, &r, "lives", "0", __LINE__);
    reflect_test_set(t, &r, "rng", "0x10 3", BMF_REFLECT_OK, 0, __LINE__);
    reflect_test_get(t, &r, "rng", "[16,3]", __LINE__);
    reflect_test_set(t, &r, "ball.pos[1]", "-1 -2", BMF_REFLECT_OK, 0, __LINE__);
    reflect_test_get(t, &r, "ball.pos", "[[0,0],[-1,-2],[2,20]]", __LINE__);
    reflect_test_set(t, &r, "ball.hp[7]", "5", BMF_REFLECT_OK, 0, __LINE__);
    reflect_test_get(t, &r, "ball.hp[7]", "5", __LINE__);

    // Errors.
    reflect_test_set(t, &r, "pos", "1", BMF_REFLECT_BAD_VALUE, "pos is v2: needs 2 values, got 1", __LINE__);
    reflect_test_set(t, &r, "score", "1 2", BMF_REFLECT_BAD_VALUE, "score is i32: needs 1 value, got 2", __LINE__);
    reflect_test_set(t, &r, "rng", "1", BMF_REFLECT_BAD_VALUE, "rng is rng: needs 2 values, got 1", __LINE__);
    reflect_test_set(t, &r, "score", "2147483648", BMF_REFLECT_BAD_VALUE, "'2147483648' is not an i32", __LINE__);
    reflect_test_set(t, &r, "score", "1.5", BMF_REFLECT_BAD_VALUE, "'1.5' is not an i32", __LINE__);
    reflect_test_set(t, &r, "big", "x", BMF_REFLECT_BAD_VALUE, "'x' is not an i64", __LINE__);
    reflect_test_set(t, &r, "flags", "0x100000000", BMF_REFLECT_BAD_VALUE, "is not a u32 (0..4294967295", __LINE__);
    reflect_test_set(t, &r, "flags", "-1", BMF_REFLECT_BAD_VALUE, "'-1' is not a u32", __LINE__);
    reflect_test_set(t, &r, "flags", "0x", BMF_REFLECT_BAD_VALUE, "'0x' is not a u32", __LINE__);
    reflect_test_set(t, &r, "lives", "256", BMF_REFLECT_BAD_VALUE, "'256' is not a u8 (0..255", __LINE__);
    reflect_test_set(t, &r, "ubig", "18446744073709551616", BMF_REFLECT_BAD_VALUE, "is not a u64", __LINE__);
    reflect_test_set(t, &r, "alive", "yes", BMF_REFLECT_BAD_VALUE, "'yes' is not a b32", __LINE__);
    reflect_test_set(t, &r, "speed", "abc", BMF_REFLECT_BAD_VALUE, "'abc' is not a number", __LINE__);
    reflect_test_set(t, &r, "speed", "nan", BMF_REFLECT_BAD_VALUE, "NaN and Inf are not allowed in the state", __LINE__);
    reflect_test_set(t, &r, "speed", "-inf", BMF_REFLECT_BAD_VALUE, "NaN and Inf are not allowed", __LINE__);
    reflect_test_set(t, &r, "speed", "1e39", BMF_REFLECT_BAD_VALUE, "NaN and Inf are not allowed", __LINE__);
    reflect_test_set(t, &r, "rng", "1 2", BMF_REFLECT_BAD_VALUE, "rng: inc 2 must be odd", __LINE__);
    reflect_test_set(t, &r, "tick", "5", BMF_REFLECT_READ_ONLY, "tick is read-only", __LINE__);
    reflect_test_set(t, &r, "ball.pos", "1 2", BMF_REFLECT_BAD_PATH, "set writes one element: use 'ball.pos[i]'", __LINE__);
    reflect_test_set(t, &r, "ball.pos[*]", "1 2", BMF_REFLECT_BAD_PATH, "set writes one element", __LINE__);
    reflect_test_set(t, &r, "ball.pos[3]", "1 2", BMF_REFLECT_INDEX_OUT_OF_RANGE, "live count 3 (ball.count), capacity 8", __LINE__);
    reflect_test_set(t, &r, "bal.pos[0]", "1 2", BMF_REFLECT_UNKNOWN_PATH, "did you mean 'ball.pos'?", __LINE__);

    // A failed set changes nothing (all values are checked before writing).
    reflect_test_state before = reflect_test_data;
    reflect_test_set(t, &r, "p4", "1 2 nan 4", BMF_REFLECT_BAD_VALUE, "NaN", __LINE__);
    reflect_test_set(t, &r, "rng", "5 6", BMF_REFLECT_BAD_VALUE, "must be odd", __LINE__);
    bmf_check(t, bmf_memcmp(&before, &reflect_test_data, sizeof before) == 0, __FILE__, __LINE__, "state unchanged by failed sets");

    // A count field stays within the capacity of every array it counts (ball.pos 8, ball.mass 4).
    reflect_test_set(t, &r, "ball.count", "4", BMF_REFLECT_OK, 0, __LINE__);
    reflect_test_get(t, &r, "ball.pos[3]", "[3,30]", __LINE__);
    reflect_test_set(t, &r, "ball.count", "5", BMF_REFLECT_BAD_VALUE, "ball.count is the live count of ball.mass (capacity 4): 5 is outside 0..4", __LINE__);
    reflect_test_set(t, &r, "ball.count", "-1", BMF_REFLECT_BAD_VALUE, "outside 0..8", __LINE__);
    reflect_test_set(t, &r, "ball.count", "0", BMF_REFLECT_OK, 0, __LINE__);
    reflect_test_get(t, &r, "ball.pos", "[]", __LINE__);
}

// ---------------------------------------------------------------- reflect.dump
static void test_reflect_dump(bmf_test* t) {
    reflect_test_fill();
    bmf_reflect r = reflect_test_make();
    char buf[4096];
    bmf_json_writer w;
    bmf_json_init(&w, buf, (i32)sizeof buf);
    bmf_reflect_dump(&r, bmf_str_from_cstr("ball"), &w);
    const char* expected = "{\"ball.count\":3,\"ball.pos\":[[0,0],[1,10],[2,20]],\"ball.hp\":[100,101,102,103,104,105,106,107],\"ball.mass\":[0.5,1,1.5]}";
    char message[4600];
    bmf_fmt_buf(message, (i32)sizeof message, "dump ball: %s", buf);
    bmf_check(t, bmf_cstr_eq(buf, expected), __FILE__, __LINE__, message);

    bmf_json_init(&w, buf, (i32)sizeof buf);
    bmf_reflect_dump(&r, bmf_str_from_cstr(""), &w);
    const char* all = "{\"tick\":42,\"score\":-7,\"flags\":4000000000,\"big\":-9223372036854775808,\"ubig\":18446744073709551615,"
                      "\"speed\":0.1,\"pos\":[1.5,-2],\"p3\":[0,-0,1e-7],\"p4\":[1,2,3,1e+21],\"alive\":true,\"target\":1048579,"
                      "\"lives\":255,\"ball.count\":3,\"rng\":[12345678901234567890,7],\"ball.pos\":[[0,0],[1,10],[2,20]],"
                      "\"ball.hp\":[100,101,102,103,104,105,106,107],\"ball.mass\":[0.5,1,1.5]}";
    bmf_fmt_buf(message, (i32)sizeof message, "dump all: %s", buf);
    bmf_check(t, bmf_cstr_eq(buf, all) && !w.overflow, __FILE__, __LINE__, message);

    bmf_json_init(&w, buf, (i32)sizeof buf);
    bmf_reflect_dump(&r, bmf_str_from_cstr("ti"), &w);
    bmf_check(t, bmf_cstr_eq(buf, "{\"tick\":42}"), __FILE__, __LINE__, buf);
    bmf_json_init(&w, buf, (i32)sizeof buf);
    bmf_reflect_dump(&r, bmf_str_from_cstr("zzz"), &w);
    bmf_check(t, bmf_cstr_eq(buf, "{}"), __FILE__, __LINE__, buf);
}

// ---------------------------------------------------------------- reflect.suggest
static void test_reflect_suggest(bmf_test* t) {
    reflect_test_fill();
    bmf_reflect r = reflect_test_make();
    struct { const char* name; const char* expected; } cases[] = {
        { "bal.pos", "ball.pos" },     // one deletion
        { "ball.poss", "ball.pos" },   // one insertion
        { "scroe", "score" },          // transposition = 2 edits, within 1 + 5/3
        { "ball", "ball.count" },      // a prefix: the first field under it
        { "tikc", "tick" },            // built-in paths too
        { "ball.mas", "ball.mass" },
        { "xyzzy", 0 },                // nothing close
        { "s", 0 },                    // 1-byte names only get distance 1: "s" -> no field of length <= 2
        { "", 0 },
    };
    for (i32 c = 0; c < (i32)(sizeof cases / sizeof cases[0]); c++) {
        const char* got = bmf_reflect_suggest(&r, bmf_str_from_cstr(cases[c].name));
        char message[160];
        bmf_fmt_buf(message, (i32)sizeof message, "suggest '%s': expected %s, got %s", cases[c].name, cases[c].expected, got);
        bmf_check(t, cases[c].expected ? (got && bmf_cstr_eq(got, cases[c].expected)) : got == 0, __FILE__, __LINE__, message);
    }
    reflect_test_get_error(t, &r, "bal.pos", BMF_REFLECT_UNKNOWN_PATH, "no field 'bal.pos'; did you mean 'ball.pos'?", __LINE__);
    reflect_test_get_error(t, &r, "xyzzy", BMF_REFLECT_UNKNOWN_PATH, "no field 'xyzzy' (dump lists the fields)", __LINE__);
    // A name longer than any field name gets only prefix suggestions.
    reflect_test_get_error(t, &r, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", BMF_REFLECT_UNKNOWN_PATH, "dump lists the fields", __LINE__);
}

// ---------------------------------------------------------------- reflect.nonfinite
static void test_reflect_nonfinite(bmf_test* t) {
    reflect_test_fill();
    bmf_reflect r = reflect_test_make();
    char path[64];
    bmf_check(t, !bmf_reflect_find_nonfinite(&r, path, (i32)sizeof path) && path[0] == 0, __FILE__, __LINE__, "clean state");
    // A NaN in a dead element (index 7 >= live count 3) is still found: dead elements are state.
    reflect_test_data.ball_pos[7].y = bmf_f32_from_bits(0x7FC00000u);
    bmf_check(t, bmf_reflect_find_nonfinite(&r, path, (i32)sizeof path) && bmf_cstr_eq(path, "ball.pos[7]"), __FILE__, __LINE__, path);
    // The first offending field in table order wins; scalars are named without an index.
    reflect_test_data.p3.z = bmf_f32_from_bits(0xFF800000u);   // -Inf
    bmf_check(t, bmf_reflect_find_nonfinite(&r, path, (i32)sizeof path) && bmf_cstr_eq(path, "p3"), __FILE__, __LINE__, path);
    reflect_test_fill();
    reflect_test_data.ball_mass[3] = bmf_f32_from_bits(0x7F800000u);
    bmf_check(t, bmf_reflect_find_nonfinite(&r, path, (i32)sizeof path) && bmf_cstr_eq(path, "ball.mass[3]"), __FILE__, __LINE__, path);
    // Integer fields with NaN-like bit patterns are not floats and are ignored.
    reflect_test_fill();
    reflect_test_data.flags = 0x7FC00000u;
    reflect_test_data.ball_hp[0] = 0x7F800000;
    bmf_check(t, !bmf_reflect_find_nonfinite(&r, path, (i32)sizeof path), __FILE__, __LINE__, "integers are not scanned");
}

// ---------------------------------------------------------------- reflect.core
static void test_reflect_core(bmf_test* t) {
    char error[BMF_CORE_ERROR_MAX];
    bmf_game_desc game = core_test_game();
    bmf_core_options options;
    bmf_memset(&options, 0, sizeof options);
    options.time_scale = 1.0f;
    options.seed = 5;
    bmf_check_eq_i64(t, bmf_core_start(&game, &options, error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, error);
    bmf_core_step(3);
    bmf_reflect r = bmf_core_reflect();
    reflect_test_get(t, &r, "tick", "3", __LINE__);
    reflect_test_get(t, &r, "tick_calls", "3", __LINE__);
    reflect_test_get(t, &r, "ticks_seen", "[0,1,2]", __LINE__);
    reflect_test_set(t, &r, "value", "1234", BMF_REFLECT_OK, 0, __LINE__);
    bmf_check_eq_i64(t, ((core_test_state*)bmf_core_state())->value, 1234, __FILE__, __LINE__, "set writes the running game's state");
    bmf_core_stop();
}
