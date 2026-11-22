// profile.c: the profiler and the memory report (task 1A-36). Contracts in profile.h and bmf.h
// ("Profiling and logging"), invariants in src/debug/README.md.

static const char* const PROFILE_ENGINE_ZONE_NAMES[BMF_ZONE_ENGINE_COUNT] = {
    "tick", "draw", "draw2d_flush", "gpu_end_frame", "debug_hooks",
};

static struct {
    u64 counter_origin;                       // counter and clock at bmf_profile_begin (calibration)
    i64 ns_origin;
    i32 depth;
    i32 open_zone[BMF_PROFILE_MAX_DEPTH];
    u64 open_start[BMF_PROFILE_MAX_DEPTH];
    i64 calls[BMF_ZONE_COUNT];                // in the window
    u64 total[BMF_ZONE_COUNT];                // counter ticks, in the window
    u64 max[BMF_ZONE_COUNT];
    i32 sample_next[BMF_ZONE_COUNT];
    i64 frames;                               // in the window
    i32 frame_next;
    i64 nesting_errors;
} profile;

static u64 profile_samples[BMF_ZONE_COUNT][BMF_PROFILE_SAMPLES];   // counter ticks per call (ring)
static u64 profile_frame_ns[BMF_PROFILE_SAMPLES];                  // ns per frame (ring)
static u64 profile_sort_keys[BMF_PROFILE_SAMPLES], profile_sort_tmp[BMF_PROFILE_SAMPLES];
static char profile_message[256];                                  // nesting errors (static: the fatal hook keeps it)

static inline u64 profile_now(void) {
#if BMF_ARCH_X64
    return __builtin_ia32_rdtsc();
#else
    return (u64)bmf_platform_time_ns();
#endif
}

static const char* profile_zone_name(i32 zone, char* buffer, i32 capacity) {
    if (zone >= 0 && zone < BMF_ZONE_ENGINE_COUNT) return PROFILE_ENGINE_ZONE_NAMES[zone];
    i32 game_index = zone - BMF_ZONE_GAME_FIRST;
    if (game_index >= 0 && bmf_core_running()) {
        const bmf_game_desc* game = bmf_core_game();
        if (game_index < game->zone_name_count) return game->zone_names[game_index];
        bmf_fmt_buf(buffer, capacity, "game.%i", game_index);
        return buffer;
    }
    bmf_fmt_buf(buffer, capacity, "zone.%i", zone);
    return buffer;
}

void bmf_profile_begin(void) {
    profile.counter_origin = profile_now();
    profile.ns_origin = bmf_platform_time_ns();
    profile.depth = 0;
    profile.nesting_errors = 0;
    bmf_profile_window_reset();
}

void bmf_profile_window_reset(void) {
    for (i32 z = 0; z < BMF_ZONE_COUNT; z++) {
        profile.calls[z] = 0;
        profile.total[z] = 0;
        profile.max[z] = 0;
        profile.sample_next[z] = 0;
    }
    profile.frames = 0;
    profile.frame_next = 0;
}

// A nesting error: debug builds stop with both names; agent release builds count it.
static void profile_nesting_error(const char* what, i32 zone) {
    profile.nesting_errors++;
#if BMF_DEBUG
    char name[32], open_name[32];
    const char* open = profile.depth > 0 ? profile_zone_name(profile.open_zone[profile.depth - 1], open_name, (i32)sizeof open_name) : 0;
    if (open) bmf_fmt_buf(profile_message, (i32)sizeof profile_message, "%s(%s): the innermost open zone is %s (zones must nest)", what, profile_zone_name(zone, name, (i32)sizeof name), open);
    else bmf_fmt_buf(profile_message, (i32)sizeof profile_message, "%s(%s): %s", what, profile_zone_name(zone, name, (i32)sizeof name),
                     profile.depth >= BMF_PROFILE_MAX_DEPTH ? "more than 32 zones open" : "no zone is open");
    bmf_assert_fail(__FILE__, __LINE__, profile_message);
#else
    (void)what;
    (void)zone;
#endif
}

void bmf_profile_zone_begin(i32 zone) {
    BMF_ASSERT(zone >= 0 && zone < BMF_ZONE_COUNT);
    if (profile.depth >= BMF_PROFILE_MAX_DEPTH) { profile_nesting_error("bmf_zone_begin", zone); return; }
    profile.open_zone[profile.depth] = zone;
    profile.open_start[profile.depth] = profile_now();
    profile.depth++;
}

void bmf_profile_zone_end(i32 zone) {
    u64 now = profile_now();
    BMF_ASSERT(zone >= 0 && zone < BMF_ZONE_COUNT);
    if (profile.depth == 0 || profile.open_zone[profile.depth - 1] != zone) {
        profile_nesting_error("bmf_zone_end", zone);   // debug builds stop here
        // Release with BMF_AGENT: close the zones opened inside `zone` (not timed), if it is open.
        i32 at = profile.depth - 1;
        while (at >= 0 && profile.open_zone[at] != zone) at--;
        if (at < 0) return;
        profile.depth = at + 1;
    }
    profile.depth--;
    u64 cycles = now - profile.open_start[profile.depth];
    profile.calls[zone]++;
    profile.total[zone] += cycles;
    if (cycles > profile.max[zone]) profile.max[zone] = cycles;
    profile_samples[zone][profile.sample_next[zone]] = cycles;
    profile.sample_next[zone] = (profile.sample_next[zone] + 1) % BMF_PROFILE_SAMPLES;
}

void bmf_profile_frame(i64 ns) {
    profile.frames++;
    profile_frame_ns[profile.frame_next] = (u64)(ns > 0 ? ns : 0);
    profile.frame_next = (profile.frame_next + 1) % BMF_PROFILE_SAMPLES;
}

i32 bmf_profile_depth(void) { return profile.depth; }
i64 bmf_profile_nesting_errors(void) { return profile.nesting_errors; }

// Counter ticks per second, measured against the clock since bmf_profile_begin (at least 10 ms).
static f64 profile_counter_hz(void) {
#if BMF_ARCH_X64
    i64 elapsed = bmf_platform_time_ns() - profile.ns_origin;
    while (elapsed < 10000000) elapsed = bmf_platform_time_ns() - profile.ns_origin;   // once per process at most
    u64 counter = profile_now();
    elapsed = bmf_platform_time_ns() - profile.ns_origin;
    return (f64)(counter - profile.counter_origin) * 1e9 / (f64)elapsed;
#else
    return 1e9;
#endif
}

// p50 / p99 (nearest rank) of n values copied into the sort buffers. n >= 1.
static void profile_percentiles(i32 n, u64* p50, u64* p99) {
    bmf_sort_u64(profile_sort_keys, 0, n, profile_sort_tmp, 0);
    *p50 = profile_sort_keys[(n - 1) * 50 / 100];
    *p99 = profile_sort_keys[(n - 1) * 99 / 100];
}

void bmf_profile_write_report(bmf_json_writer* w) {
    f64 hz = profile_counter_hz();
    f64 us_per_count = 1e6 / hz;
    bmf_json_key(w, "zones");
    bmf_json_object_begin(w);
    for (i32 z = 0; z < BMF_ZONE_COUNT; z++) {
        if (profile.calls[z] == 0) continue;
        i32 n = profile.calls[z] < BMF_PROFILE_SAMPLES ? (i32)profile.calls[z] : BMF_PROFILE_SAMPLES;
        for (i32 i = 0; i < n; i++) profile_sort_keys[i] = profile_samples[z][i];
        u64 p50, p99;
        profile_percentiles(n, &p50, &p99);
        char name_buffer[32];
        bmf_json_key(w, profile_zone_name(z, name_buffer, (i32)sizeof name_buffer));
        bmf_json_object_begin(w);
        bmf_json_key(w, "calls"); bmf_json_i64(w, profile.calls[z]);
        bmf_json_key(w, "mean_us"); bmf_json_f32(w, (f32)((f64)profile.total[z] / (f64)profile.calls[z] * us_per_count));
        bmf_json_key(w, "p50_us"); bmf_json_f32(w, (f32)((f64)p50 * us_per_count));
        bmf_json_key(w, "p99_us"); bmf_json_f32(w, (f32)((f64)p99 * us_per_count));
        bmf_json_key(w, "max_us"); bmf_json_f32(w, (f32)((f64)profile.max[z] * us_per_count));
        bmf_json_object_end(w);
    }
    bmf_json_object_end(w);
    bmf_json_key(w, "frames"); bmf_json_i64(w, profile.frames);
    if (profile.frames > 0) {
        i32 n = profile.frames < BMF_PROFILE_SAMPLES ? (i32)profile.frames : BMF_PROFILE_SAMPLES;
        u64 sum = 0, max = 0;
        for (i32 i = 0; i < n; i++) {
            profile_sort_keys[i] = profile_frame_ns[i];
            sum += profile_frame_ns[i];
            if (profile_frame_ns[i] > max) max = profile_frame_ns[i];
        }
        u64 p50, p99;
        profile_percentiles(n, &p50, &p99);
        bmf_json_key(w, "frame_ms");
        bmf_json_object_begin(w);
        bmf_json_key(w, "mean"); bmf_json_f32(w, (f32)((f64)sum / (f64)n / 1e6));
        bmf_json_key(w, "p50"); bmf_json_f32(w, (f32)((f64)p50 / 1e6));
        bmf_json_key(w, "p99"); bmf_json_f32(w, (f32)((f64)p99 / 1e6));
        bmf_json_key(w, "max"); bmf_json_f32(w, (f32)((f64)max / 1e6));
        bmf_json_object_end(w);
    }
    bmf_draw2d_stats stats = bmf_draw2d_get_stats();
    bmf_json_key(w, "draw_calls"); bmf_json_i64(w, stats.batches);
    bmf_json_key(w, "instances"); bmf_json_i64(w, stats.instances);
    bmf_json_key(w, "nesting_errors"); bmf_json_i64(w, profile.nesting_errors);
    bmf_json_key(w, "counter_hz"); bmf_json_i64(w, (i64)hz);
}

static void profile_write_arena(bmf_json_writer* w, const char* name, const bmf_arena* a) {
    bmf_json_object_begin(w);
    bmf_json_key(w, "name"); bmf_json_string(w, name);
    bmf_json_key(w, "reserved"); bmf_json_i64(w, a->reserved);
    bmf_json_key(w, "committed"); bmf_json_i64(w, a->committed);
    bmf_json_key(w, "used"); bmf_json_i64(w, a->used);
    bmf_json_key(w, "high_water"); bmf_json_i64(w, a->high_water);
    bmf_json_object_end(w);
}

void bmf_profile_write_mem(bmf_json_writer* w) {
    bmf_memory* m = bmf_core_memory();
    bmf_json_key(w, "arenas");
    bmf_json_array_begin(w);
    profile_write_arena(w, "permanent", &m->permanent);
    profile_write_arena(w, "game_state", &m->game_state);
    profile_write_arena(w, "level", &m->level);
    profile_write_arena(w, "frame", &m->frame);
    profile_write_arena(w, "scratch.0", &m->scratch[0]);
    profile_write_arena(w, "scratch.1", &m->scratch[1]);
    const bmf_arena* arenas[32];
    const char* names[32];
    i32 count = bmf_replay_arenas(arenas, names, 32);
    count += bmf_snap_arenas(arenas + count, names + count, 32 - count);
    for (i32 i = 0; i < count; i++) profile_write_arena(w, names[i], arenas[i]);
    bmf_json_array_end(w);
}
