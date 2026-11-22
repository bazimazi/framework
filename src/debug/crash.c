// crash.c: crash dumps (task 1A-35). Contracts in crash.h, invariants in src/debug/README.md.

static struct {
    b32 ready;
    char dir[BMF_PLATFORM_PATH_MAX];            // "<exe dir>crash", with the exe dir's separator
    char separator;
    char replay_path[BMF_PLATFORM_PATH_MAX];
    char state_path[BMF_PLATFORM_PATH_MAX];
    char error[512];
} crash;

// Days since 1970-01-01 to a civil date (proleptic Gregorian).
static void crash_civil_date(i64 days, i32* year, i32* month, i32* day) {
    days += 719468;
    i64 era = (days >= 0 ? days : days - 146096) / 146097;
    i64 day_of_era = days - era * 146097;
    i64 year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    i64 day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    i64 mp = (5 * day_of_year + 2) / 153;
    *day = (i32)(day_of_year - (153 * mp + 2) / 5 + 1);
    *month = (i32)(mp < 10 ? mp + 3 : mp - 9);
    *year = (i32)(year_of_era + era * 400 + (*month <= 2 ? 1 : 0));
}

// Appends `text` JSON-escaped to out at *len (bounded by capacity).
static void crash_put_escaped(char* out, i32 capacity, i32* len, const char* text) {
    for (const char* c = text; *c && *len < capacity - 2; c++) {
        if (*c == '"' || *c == '\\') out[(*len)++] = '\\';
        out[(*len)++] = (u8)*c < 0x20 ? ' ' : *c;
    }
}

// The platform's crash hook: writes the dump, returns the extra keys of the crash line.
static i32 crash_hook(char* out, i32 capacity) {
    if (!crash.ready || !bmf_core_running()) return 0;
    const bmf_game_desc* game = bmf_core_game();
    i64 tick = bmf_core_ctx()->tick;
    i32 len = bmf_fmt_buf(out, capacity, ",\"tick\":%li,\"phase\":\"%s\"", tick, bmf_core_phase_name());

    i64 now_ms = bmf_platform_utc_ns() / 1000000;
    i64 seconds = now_ms / 1000;
    i32 year, month, day;
    crash_civil_date(seconds / 86400, &year, &month, &day);
    i32 second_of_day = (i32)(seconds % 86400);
    char stamp[64];
    bmf_fmt_buf(stamp, (i32)sizeof stamp, "%u%02u%02u-%02u%02u%02u-%03u", (u32)year, (u32)month, (u32)day,
                (u32)(second_of_day / 3600), (u32)(second_of_day / 60 % 60), (u32)(second_of_day % 60), (u32)(now_ms % 1000));
    char sep[2] = { crash.separator, 0 };
    bmf_fmt_buf(crash.replay_path, (i32)sizeof crash.replay_path, "%s%s%s-%s.bmfr", crash.dir, sep, stamp, game->name);
    bmf_fmt_buf(crash.state_path, (i32)sizeof crash.state_path, "%s%s%s-%s.state", crash.dir, sep, stamp, game->name);

    crash.error[0] = 0;
    b32 replay_ok = 0, state_ok = 0;
    if (bmf_platform_dir_create(crash.dir) != BMF_OK) bmf_fmt_buf(crash.error, (i32)sizeof crash.error, "cannot create '%s'", crash.dir);
    else replay_ok = bmf_replay_write_crash(crash.replay_path, crash.error, (i32)sizeof crash.error) == BMF_OK;
    i64 state_tick = 0;
    u64 state_hash = 0;
    const u8* state = bmf_replay_session_last_state(&state_tick, &state_hash);
    if (replay_ok && !state) bmf_fmt_buf(crash.error, (i32)sizeof crash.error, "no checkpoint state yet");
    if (replay_ok && state) {
        static bmf_state_file_header h;
        bmf_memset(&h, 0, sizeof h);
        bmf_memcpy(h.magic, "BMFS", 4);
        h.version = BMF_STATE_FILE_VERSION;
        h.header_size = (i32)sizeof h;
        h.state_size = game->state_size;
        h.tick = state_tick;
        h.state_hash = state_hash;
        h.state_layout_hash = game->state_layout_hash;
        h.build_id = bmf_core_build_id();
        i32 name_length = bmf_cstr_len(game->name);
        bmf_memcpy(h.game_name, game->name, name_length < 63 ? name_length : 63);
        const void* parts[2] = { &h, state };
        i64 sizes[2] = { (i64)sizeof h, game->state_size };
        state_ok = bmf_platform_file_write_atomic_parts(crash.state_path, parts, sizes, 2) == BMF_OK;
        if (!state_ok) bmf_fmt_buf(crash.error, (i32)sizeof crash.error, "cannot write '%s'", crash.state_path);
    }

    const char* keys[3] = { "replay", "state", "dump_error" };
    const char* values[3] = { crash.replay_path, crash.state_path, crash.error };
    b32 present[3] = { replay_ok, state_ok, crash.error[0] != 0 };
    for (i32 k = 0; k < 3; k++) {
        if (!present[k] || len >= capacity - 16) continue;
        len += bmf_fmt_buf(out + len, capacity - len, ",\"%s\":\"", keys[k]);
        crash_put_escaped(out, capacity, &len, values[k]);
        if (len < capacity - 1) out[len++] = '"';
    }
    return len;
}

void bmf_crash_begin(void) {
    i32 n = bmf_platform_exe_dir(crash.dir, (i32)sizeof crash.dir - 8);
    if (n <= 0) { crash.ready = 0; return; }
    crash.separator = crash.dir[n - 1] == '\\' ? '\\' : '/';
    while (n > 0 && (crash.dir[n - 1] == '/' || crash.dir[n - 1] == '\\')) n--;
    crash.dir[n++] = crash.separator;
    bmf_memcpy(crash.dir + n, "crash", 6);
    crash.ready = 1;
    bmf_platform_set_crash_hook(crash_hook);
}
