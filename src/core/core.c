// core.c: the game model and the engine loop (task 1A-23). See core.h for the contracts and
// src/core/README.md for invariants and data layout.

// ---------------------------------------------------------------- state
typedef enum core_phase { CORE_PHASE_NONE = 0, CORE_PHASE_INIT = 1, CORE_PHASE_TICK = 2, CORE_PHASE_DRAW = 3 } core_phase;

enum { CORE_NS_PER_SECOND = 1000000000 };

typedef struct core_sfx_event {
    u32 sound;
    f32 volume;
    f32 pan;
} core_sfx_event;

static struct {
    b32 running;
    bmf_game_desc game;          // copy of the running game's description
    bmf_core_options options;
    bmf_memory memory;
    void* state;
    bmf_ctx ctx;
    core_phase phase;
    i64 tick;                    // ticks completed since init (cx->tick outside tick)
    i64 accumulator;             // real time owed to the simulation, in ns x tick_rate
    bmf_core_stats stats;
    // The engine's copy of the action table, so input never points into an unloaded game DLL.
    bmf_action actions[BMF_INPUT_MAX_ACTIONS];
    char action_names[BMF_INPUT_MAX_ACTIONS][BMF_ACTION_NAME_MAX + 1];
    i32 action_count;
    // Simulation-to-presentation events of the current tick.
    core_sfx_event sfx[BMF_CORE_MAX_SFX_PER_TICK];
    i32 sfx_count;
    // Display (bmf_core_display_open).
    b32 display_open;
    b32 window_open;
    bmf_draw2d_view view;
    bmf_gpu_texture offscreen;   // headless render target
    b32 resize_pending;
    bmf_window_size pending_size;
    b32 suspended;
    b32 quit;
    i64 last_frame_ns;           // 0 = the next frame starts the clock
} core;

// ---------------------------------------------------------------- validation
bmf_err bmf_core_check_game(const bmf_game_desc* game, char* error, i32 error_capacity) {
    BMF_ASSERT(error && error_capacity > 0);
    error[0] = 0;
    if (!game) {
        bmf_fmt_buf(error, error_capacity, "game: no description (bmf_game() returned 0)");
        return BMF_ERR_INVALID;
    }
    if (game->api_version != BMF_API_VERSION) {
        bmf_fmt_buf(error, error_capacity, "game: api_version %u, but the runtime is %u (rebuild the game against this bmf.h)",
                    game->api_version, (u32)BMF_API_VERSION);
        return BMF_ERR_INVALID;
    }
    i32 name_length = game->name ? bmf_cstr_len(game->name) : 0;
    if (name_length < 1 || name_length > BMF_CORE_NAME_MAX) {
        bmf_fmt_buf(error, error_capacity, "game: name must be 1..%i bytes", BMF_CORE_NAME_MAX);
        return BMF_ERR_INVALID;
    }
    const char* name = game->name;
    if (game->state_size < 1 || game->state_size > BMF_CORE_MAX_STATE_SIZE) {
        bmf_fmt_buf(error, error_capacity, "game \"%s\": state_size %i out of range 1..%i", name, game->state_size, BMF_CORE_MAX_STATE_SIZE);
        return BMF_ERR_INVALID;
    }
    if (game->tick_rate != 0 && (game->tick_rate < BMF_CORE_MIN_TICK_RATE || game->tick_rate > BMF_CORE_MAX_TICK_RATE)) {
        bmf_fmt_buf(error, error_capacity, "game \"%s\": tick_rate %i out of range %i..%i (0 = %i)",
                    name, game->tick_rate, BMF_CORE_MIN_TICK_RATE, BMF_CORE_MAX_TICK_RATE, BMF_CORE_DEFAULT_TICK_RATE);
        return BMF_ERR_INVALID;
    }
    const i64 memory_sizes[4] = { game->memory.permanent, game->memory.level, game->memory.frame, game->memory.scratch };
    const char* const memory_names[4] = { "permanent", "level", "frame", "scratch" };
    for (i32 i = 0; i < 4; i++) {
        if (memory_sizes[i] < 0 || memory_sizes[i] > (4ll << 30)) {
            bmf_fmt_buf(error, error_capacity, "game \"%s\": memory.%s = %li out of range 0..4 GiB (0 = default)", name, memory_names[i], memory_sizes[i]);
            return BMF_ERR_INVALID;
        }
    }
    const char* missing = !game->init ? "init" : !game->tick ? "tick" : !game->draw ? "draw" : 0;
    if (missing) {
        bmf_fmt_buf(error, error_capacity, "game \"%s\": the %s callback is required", name, missing);
        return BMF_ERR_INVALID;
    }

    char field_error[BMF_CORE_ERROR_MAX];
    if (bmf_reflect_check_fields(game->fields, game->field_count, game->state_size, field_error, (i32)sizeof field_error) != BMF_OK) {
        bmf_fmt_buf(error, error_capacity, "game \"%s\": %s", name, field_error);
        return BMF_ERR_INVALID;
    }
#if BMF_DEBUG
    // SPEC §8.4: the declared layout hash must match the field table (debug builds compute it).
    u64 layout_hash = bmf_fields_hash(game->fields, game->field_count);
    if (game->state_layout_hash != layout_hash) {
        bmf_fmt_buf(error, error_capacity, "game \"%s\": state_layout_hash 0x%lx does not match the field table: set it to 0x%lx",
                    name, game->state_layout_hash, layout_hash);
        return BMF_ERR_INVALID;
    }
#endif

    char input_error[BMF_CORE_ERROR_MAX];
    if (bmf_input_check_actions(game->actions, game->action_count, input_error, (i32)sizeof input_error) != BMF_OK) {
        bmf_fmt_buf(error, error_capacity, "game \"%s\": %s", name, input_error);
        return BMF_ERR_INVALID;
    }
    if (game->zone_name_count < 0 || game->zone_name_count > BMF_ZONE_COUNT - BMF_ZONE_GAME_FIRST || (game->zone_name_count > 0 && !game->zone_names)) {
        bmf_fmt_buf(error, error_capacity, "game \"%s\": zone_name_count %i: 0..%i names (zones BMF_ZONE_GAME_FIRST + i)",
                    name, game->zone_name_count, (i32)(BMF_ZONE_COUNT - BMF_ZONE_GAME_FIRST));
        return BMF_ERR_INVALID;
    }
    for (i32 i = 0; i < game->zone_name_count; i++) {
        i32 length = game->zone_names[i] ? bmf_cstr_len(game->zone_names[i]) : 0;
        if (length < 1 || length > 63) {
            bmf_fmt_buf(error, error_capacity, "game \"%s\": zone_names[%i]: 1..63 bytes", name, i);
            return BMF_ERR_INVALID;
        }
    }
    return BMF_OK;
}

// ---------------------------------------------------------------- arguments
bmf_err bmf_core_parse_args(i32 count, const char* const* values, bmf_core_options* out, char* error, i32 error_capacity) {
    BMF_ASSERT(out && error && error_capacity > 0);
    bmf_memset(out, 0, sizeof *out);
    out->time_scale = 1.0f;
    error[0] = 0;
    for (i32 i = 1; i < count; i++) {
        const char* flag = values[i];
        if (bmf_cstr_eq(flag, "--headless")) { out->headless = 1; continue; }
        if (bmf_cstr_eq(flag, "--control")) { out->control = 1; continue; }
        if (bmf_cstr_eq(flag, "--no-gpu")) { out->no_gpu = 1; continue; }
        if (bmf_cstr_eq(flag, "--verify")) { out->verify = 1; continue; }
        if (bmf_cstr_eq(flag, "--profile")) { out->profile = 1; continue; }
        b32 takes_value = bmf_cstr_eq(flag, "--replay") || bmf_cstr_eq(flag, "--seed") ||
                          bmf_cstr_eq(flag, "--tick-rate") || bmf_cstr_eq(flag, "--scale") || bmf_cstr_eq(flag, "--size");
        if (!takes_value) {
            bmf_fmt_buf(error, error_capacity, "unknown argument \"%s\" (flags: --headless --control --no-gpu --replay FILE --verify --profile --seed N --tick-rate N --scale X --size WxH)", flag);
            return BMF_ERR_INVALID;
        }
        if (i + 1 >= count) {
            bmf_fmt_buf(error, error_capacity, "%s needs a value", flag);
            return BMF_ERR_INVALID;
        }
        const char* value = values[++i];
        bmf_str text = bmf_str_from_cstr(value);
        if (bmf_cstr_eq(flag, "--replay")) {
            if (!value[0]) { bmf_fmt_buf(error, error_capacity, "--replay needs a file name"); return BMF_ERR_INVALID; }
            out->replay_path = value;
        } else if (bmf_cstr_eq(flag, "--seed")) {
            i64 seed = 0;
            if (bmf_parse_i64(text, &seed) != BMF_OK || seed < 0) {
                bmf_fmt_buf(error, error_capacity, "--seed \"%s\": expected an integer 0..9223372036854775807", value);
                return BMF_ERR_INVALID;
            }
            out->seed = (u64)seed;
        } else if (bmf_cstr_eq(flag, "--tick-rate")) {
            i32 rate = 0;
            if (bmf_parse_i32(text, &rate) != BMF_OK || rate < BMF_CORE_MIN_TICK_RATE || rate > BMF_CORE_MAX_TICK_RATE) {
                bmf_fmt_buf(error, error_capacity, "--tick-rate \"%s\": expected an integer %i..%i", value, BMF_CORE_MIN_TICK_RATE, BMF_CORE_MAX_TICK_RATE);
                return BMF_ERR_INVALID;
            }
            out->tick_rate = rate;
        } else if (bmf_cstr_eq(flag, "--size")) {
            i32 x = bmf_str_find_byte(text, 'x', 0);
            i32 w = 0, h = 0;
            if (x < 0 || bmf_parse_i32(bmf_str_slice(text, 0, x), &w) != BMF_OK || bmf_parse_i32(bmf_str_slice(text, x + 1, text.len), &h) != BMF_OK ||
                w < 16 || h < 16 || w > 8192 || h > 8192) {
                bmf_fmt_buf(error, error_capacity, "--size \"%s\": expected WxH, each 16..8192 (e.g. 1280x720)", value);
                return BMF_ERR_INVALID;
            }
            out->width = w;
            out->height = h;
        } else {
            f32 scale = 0.0f;
            if (bmf_parse_f32(text, &scale) != BMF_OK || !(scale >= 0.01f && scale <= 100.0f)) {
                bmf_fmt_buf(error, error_capacity, "--scale \"%s\": expected a number 0.01..100", value);
                return BMF_ERR_INVALID;
            }
            out->time_scale = scale;
        }
    }
    if (out->verify && !out->replay_path) {
        bmf_fmt_buf(error, error_capacity, "--verify needs --replay FILE");
        return BMF_ERR_INVALID;
    }
    if (out->profile && (!out->replay_path || out->control || out->verify)) {
        bmf_fmt_buf(error, error_capacity, "--profile needs --replay FILE, without --control and --verify (it profiles the frames of a replay run)");
        return BMF_ERR_INVALID;
    }
    return BMF_OK;
}

// ---------------------------------------------------------------- actions copy
static b32 core_actions_equal(const bmf_action* actions, i32 action_count) {
    if (action_count != core.action_count) return 0;
    for (i32 a = 0; a < action_count; a++) {
        if (!bmf_cstr_eq(actions[a].name, core.actions[a].name) || actions[a].type != core.actions[a].type) return 0;
        if (bmf_memcmp(actions[a].bindings, core.actions[a].bindings, sizeof core.actions[a].bindings) != 0) return 0;
    }
    return 1;
}

// Copies a validated table into the engine and restarts input with the copy.
static void core_actions_install(const bmf_action* actions, i32 action_count) {
    core.action_count = action_count;
    for (i32 a = 0; a < action_count; a++) {
        core.actions[a] = actions[a];
        i32 length = bmf_cstr_len(actions[a].name);   // 1..BMF_ACTION_NAME_MAX (checked)
        bmf_memcpy(core.action_names[a], actions[a].name, length);
        core.action_names[a][length] = 0;
        core.actions[a].name = core.action_names[a];
    }
    char error[BMF_CORE_ERROR_MAX];
    bmf_err err = bmf_input_init(core.actions, core.action_count, error, (i32)sizeof error);
    BMF_ASSERT(err == BMF_OK && "core_actions_install: the table was checked before");
    (void)err;
    if (core.view.dpi_scale > 0.0f) bmf_input_set_view(core.view);
}

// ---------------------------------------------------------------- start and stop
static i32 core_effective_tick_rate(const bmf_game_desc* game) {
    if (core.options.tick_rate) return core.options.tick_rate;
    return game->tick_rate ? game->tick_rate : BMF_CORE_DEFAULT_TICK_RATE;
}

static void core_set_tick_rate(i32 tick_rate) {
    if (core.ctx.tick_rate != tick_rate) core.accumulator = 0;
    core.ctx.tick_rate = tick_rate;
    core.ctx.dt = 1.0f / (f32)tick_rate;   // real division (R2): the same bits on every target
}

static void core_deliver_events(void) {
    // Audio stub until M7: events are counted, then dropped.
    core.stats.sfx_played += core.sfx_count;
    core.sfx_count = 0;
}

// Zeroes a fresh state block of the game's size and runs init.
static void core_init_state(void) {
    bmf_arena_reset(&core.memory.game_state);
    core.state = bmf_arena_push_zero(&core.memory.game_state, core.game.state_size, 64);
    core.tick = 0;
    core.ctx.tick = 0;
    core.accumulator = 0;
    core.phase = CORE_PHASE_INIT;
    core.game.init(&core.ctx, core.state, core.ctx.seed);
    core.phase = CORE_PHASE_NONE;
    core_deliver_events();
}

bmf_err bmf_core_start(const bmf_game_desc* game, const bmf_core_options* options, char* error, i32 error_capacity) {
    BMF_ASSERT(options && error && error_capacity > 0);
    bmf_core_stop();
    bmf_err err = bmf_core_check_game(game, error, error_capacity);
    if (err != BMF_OK) return err;
    core.options = *options;
    if (core.options.time_scale <= 0.0f) core.options.time_scale = 1.0f;
    i64 reserve = options->state_reserve > game->state_size ? options->state_reserve : game->state_size;
    if (!bmf_memory_init(&core.memory, &game->memory, reserve)) {
        bmf_fmt_buf(error, error_capacity, "game \"%s\": could not reserve address space for the arenas (memory budget too large?)", game->name);
        return BMF_ERR_IO;
    }
    core.game = *game;
    core.ctx.seed = options->seed;
    core.ctx.level = &core.memory.level;
    core.ctx.frame = &core.memory.frame;
    core.ctx.tick_rate = 0;
    core_set_tick_rate(core_effective_tick_rate(game));
    core_actions_install(game->actions, game->action_count);
    core.running = 1;
    core_init_state();
#if BMF_PROTOCOL_ENABLED
    bmf_snap_clear();             // snapshots belong to one run
    bmf_profile_begin();          // zones, frames, counter calibration (src/debug/profile.h)
    bmf_log_clear();
    bmf_crash_begin();            // crash dumps of this game (src/debug/crash.h)
    bmf_replay_session_begin();   // always-on session recording (crash dumps, SPEC §11.4)
#endif
    return BMF_OK;
}

void bmf_core_stop(void) {
    core.running = 0;
    core.state = 0;
    core.phase = CORE_PHASE_NONE;
    core.tick = 0;
    core.accumulator = 0;
    core.sfx_count = 0;
    core.action_count = 0;
    bmf_memset(&core.ctx, 0, sizeof core.ctx);
    bmf_memset(&core.stats, 0, sizeof core.stats);
    bmf_memset(&core.game, 0, sizeof core.game);
}

bmf_ctx* bmf_core_ctx(void) { return &core.ctx; }
void* bmf_core_state(void) { return core.state; }
const bmf_game_desc* bmf_core_game(void) { return &core.game; }

bmf_memory* bmf_core_memory(void) { return &core.memory; }

b32 bmf_core_running(void) { return core.running; }

const char* bmf_core_phase_name(void) {
    return core.phase == CORE_PHASE_INIT ? "init" : core.phase == CORE_PHASE_TICK ? "tick" : core.phase == CORE_PHASE_DRAW ? "draw" : "none";
}

u64 bmf_core_state_hash(void) {
    BMF_ASSERT(core.running);
#if BMF_DEBUG
    {
        // D-37 rule 11: the debug state hash pass asserts on NaN/Inf in reflected floats (their
        // bit patterns are not deterministic on wasm). The protocol's `hash` checks first and
        // reports instead (snap.h).
        bmf_reflect r = { core.game.fields, core.game.field_count, (u8*)core.state, core.game.state_size, &core.tick };
        char path[BMF_FIELD_NAME_MAX + 32];
        if (bmf_reflect_find_nonfinite(&r, path, (i32)sizeof path)) {
            static char message[256];   // static, not on the stack: the fatal hook keeps the pointer
            bmf_fmt_buf(message, (i32)sizeof message, "NaN or Inf in the state: '%s' at tick %li (D-37 rule 11)", path, core.tick);
            bmf_assert_fail(__FILE__, __LINE__, message);
        }
    }
#endif
    bmf_hasher h;
    bmf_hasher_init(&h, 0);
    u64 tick = (u64)core.tick;
    u8 bytes[8];
    for (i32 i = 0; i < 8; i++) bytes[i] = (u8)(tick >> (8 * i));
    bmf_hasher_update(&h, bytes, 8);
    bmf_hasher_update(&h, core.state, core.game.state_size);
    return bmf_hasher_final(&h);
}

void bmf_core_load_state(const void* state, i64 tick) {
    BMF_ASSERT(core.running && tick >= 0);
    bmf_memcpy(core.state, state, core.game.state_size);
    core.tick = tick;
    core.ctx.tick = tick;
    core.accumulator = 0;
    core.last_frame_ns = 0;
}

#ifndef BMF_BUILD_ID
#define BMF_BUILD_ID 0ull
#endif


bmf_reflect bmf_core_reflect(void) {
    BMF_ASSERT(core.running);
    bmf_reflect r = { core.game.fields, core.game.field_count, (u8*)core.state, core.game.state_size, &core.tick };
    return r;
}
bmf_core_stats bmf_core_get_stats(void) { return core.stats; }

// ---------------------------------------------------------------- ticking
static void core_tick_once(void) {
    BMF_ASSERT(bmf_fpu_check() && "core: the FPU control state changed (D-37 rule 10)");
    bmf_input in;
#if BMF_PROTOCOL_ENABLED
    if (!bmf_replay_next_input(&in)) bmf_input_sample_tick(&in);   // a playing replay supplies the input
    bmf_zone_begin(BMF_ZONE_DEBUG_HOOKS);
    bmf_replay_record_input(&in);
    bmf_zone_end(BMF_ZONE_DEBUG_HOOKS);
#else
    bmf_input_sample_tick(&in);
#endif
    core.ctx.tick = core.tick;
    core.phase = CORE_PHASE_TICK;
    bmf_zone_begin(BMF_ZONE_TICK);
    core.game.tick(&core.ctx, core.state, &in);
    bmf_zone_end(BMF_ZONE_TICK);   // also catches a zone the game left open (nesting error)
    core.phase = CORE_PHASE_NONE;
    core.tick++;
    core.ctx.tick = core.tick;
    core.stats.ticks++;
    core_deliver_events();
#if BMF_PROTOCOL_ENABLED
    bmf_zone_begin(BMF_ZONE_DEBUG_HOOKS);
    bmf_replay_after_tick();
    bmf_protocol_after_tick();
    bmf_zone_end(BMF_ZONE_DEBUG_HOOKS);
#endif
}

i32 bmf_core_step(i32 count) {
    BMF_ASSERT(core.running && count >= 0);
    for (i32 i = 0; i < count; i++) core_tick_once();
    return count;
}

i32 bmf_core_advance(i64 elapsed_ns) {
    BMF_ASSERT(core.running);
    if (elapsed_ns < 0) elapsed_ns = 0;
    if (elapsed_ns > CORE_NS_PER_SECOND) elapsed_ns = CORE_NS_PER_SECOND;
    // Presentation-side time (the scale is not simulation state): f64, then truncation.
    if (core.options.time_scale != 1.0f) elapsed_ns = (i64)((f64)elapsed_ns * (f64)core.options.time_scale);
    core.accumulator += elapsed_ns * (i64)core.ctx.tick_rate;
    i32 ran = 0;
    while (core.accumulator >= CORE_NS_PER_SECOND && ran < BMF_CORE_MAX_STEPS_PER_FRAME) {
        core_tick_once();
        core.accumulator -= CORE_NS_PER_SECOND;
        ran++;
    }
    if (core.accumulator >= CORE_NS_PER_SECOND) {
        core.stats.ticks_dropped += core.accumulator / CORE_NS_PER_SECOND;
        core.accumulator %= CORE_NS_PER_SECOND;
    }
    return ran;
}

f32 bmf_core_alpha(void) {
    f32 alpha = (f32)((f64)core.accumulator / (f64)CORE_NS_PER_SECOND);
    return alpha < 1.0f ? alpha : 0.99999994f;   // f32 rounding of 0.999999999 gives 1.0
}

void bmf_sfx_play(bmf_ctx* cx, u32 sound, f32 volume, f32 pan) {
    BMF_ASSERT(cx == &core.ctx && "bmf_sfx_play: cx is not the engine's context");
    BMF_ASSERT((core.phase == CORE_PHASE_TICK || core.phase == CORE_PHASE_INIT) && "bmf_sfx_play: only in init and tick");
    BMF_ASSERT(volume >= 0.0f && volume <= 1.0f && "bmf_sfx_play: volume must be 0..1");
    BMF_ASSERT(pan >= -1.0f && pan <= 1.0f && "bmf_sfx_play: pan must be -1..1");
    (void)cx;
    if (core.sfx_count >= BMF_CORE_MAX_SFX_PER_TICK) {
        core.stats.sfx_dropped++;
        return;
    }
    core_sfx_event* e = &core.sfx[core.sfx_count++];
    e->sound = sound;
    e->volume = volume;
    e->pan = pan;
}

// ---------------------------------------------------------------- drawing
// The view in logical pixels; zero before a display is open.
static bmf_v2 core_view_size(void) {
    if (core.view.dpi_scale <= 0.0f) return bmf_v2_make(0.0f, 0.0f);
    return bmf_v2_make((f32)core.view.width / core.view.dpi_scale, (f32)core.view.height / core.view.dpi_scale);
}

void bmf_core_set_view(bmf_draw2d_view view) {
    BMF_ASSERT(view.width >= 0 && view.height >= 0 && view.dpi_scale > 0.0f);
    core.view = view;
    core.ctx.view_size = core_view_size();
    bmf_input_set_view(view);
}

bmf_draw2d_view bmf_core_view(void) { return core.view; }

void bmf_core_draw(f32 alpha) {
    BMF_ASSERT(core.running);
    core.ctx.view_size = core_view_size();
    core.phase = CORE_PHASE_DRAW;
    bmf_zone_begin(BMF_ZONE_DRAW);
    core.game.draw(&core.ctx, core.state, alpha);
    bmf_zone_end(BMF_ZONE_DRAW);
    core.phase = CORE_PHASE_NONE;
}

// ---------------------------------------------------------------- hot reload swap
bmf_core_swap_result bmf_core_swap_game(const bmf_game_desc* game, char* error, i32 error_capacity) {
    BMF_ASSERT(core.running && error && error_capacity > 0);
    if (bmf_core_check_game(game, error, error_capacity) != BMF_OK) return BMF_CORE_SWAP_REJECTED;
    if (game->state_size > core.memory.game_state.reserved) {
        bmf_fmt_buf(error, error_capacity, "game \"%s\": state_size %i exceeds the reserve of %li bytes (restart the game)",
                    game->name, game->state_size, core.memory.game_state.reserved);
        return BMF_CORE_SWAP_REJECTED;
    }
    b32 keep = game->state_size == core.game.state_size && game->state_layout_hash == core.game.state_layout_hash;
    core.game = *game;
    core_set_tick_rate(core_effective_tick_rate(game));
    if (!core_actions_equal(game->actions, game->action_count)) core_actions_install(game->actions, game->action_count);
    core.stats.reloads++;
    if (!keep) {
        core.stats.resets++;
        core_init_state();
    }
#if BMF_PROTOCOL_ENABLED
    bmf_replay_on_state_replaced("hot reload", "the game was hot-reloaded during the recording");   // recordings of the old code end here
#endif
    return keep ? BMF_CORE_SWAP_KEPT : BMF_CORE_SWAP_RESET;
}

// ---------------------------------------------------------------- output
static void core_write_line(bmf_json_writer* w, char* line) {
    line[w->len] = '\n';
    bmf_platform_write(BMF_STREAM_STDOUT, line, w->len + 1);
}

void bmf_core_print_error(const char* code, const char* detail) {
    char line[BMF_CORE_ERROR_MAX * 2 + 128];
    bmf_json_writer w;
    bmf_json_init(&w, line, (i32)sizeof line - 1);
    bmf_json_object_begin(&w);
    bmf_json_key(&w, "event"); bmf_json_string(&w, "error");
    bmf_json_key(&w, "error"); bmf_json_string(&w, code);
    bmf_json_key(&w, "detail"); bmf_json_string(&w, detail);
    bmf_json_object_end(&w);
    core_write_line(&w, line);
}

// ---------------------------------------------------------------- game DLL (native debug)
#if defined(BMF_GAME_LINK_DLL)
static struct {
    char source_path[BMF_PLATFORM_PATH_MAX * 3];      // <exe dir>game.dll, written by the build
    char copy_paths[2][BMF_PLATFORM_PATH_MAX * 3];    // <exe dir>game_0.dll, game_1.dll
    i32 current;                                      // index of the loaded copy
    bmf_library library;
    u64 game_build_id;                                // bmf_game_build_id() of the loaded copy
    bmf_file_watch watch;
    b32 change_pending;
    i64 change_seen_ns;
} core_dll;

// Copies game.dll to copy `index`, loads it and calls bmf_game(). On failure nothing stays loaded.
static const bmf_game_desc* core_dll_load(i32 index, bmf_library* out_library, u64* out_build_id, char* error, i32 error_capacity) {
    bmf_file_map map;
    bmf_err err = bmf_platform_file_map(core_dll.source_path, &map);
    if (err != BMF_OK) {
        bmf_fmt_buf(error, error_capacity, "cannot read %s (%s)", core_dll.source_path, err == BMF_ERR_NOT_FOUND ? "not found" : "locked or unreadable");
        return 0;
    }
    err = bmf_platform_file_write_atomic(core_dll.copy_paths[index], map.data, map.size);
    bmf_platform_file_unmap(&map);
    if (err != BMF_OK) {
        bmf_fmt_buf(error, error_capacity, "cannot write %s (another game.exe running?)", core_dll.copy_paths[index]);
        return 0;
    }
    bmf_library library = bmf_platform_library_load(core_dll.copy_paths[index]);
    if (!library) {
        bmf_fmt_buf(error, error_capacity, "cannot load %s (incomplete file, or built against another game.exe)", core_dll.copy_paths[index]);
        return 0;
    }
    typedef const bmf_game_desc* (*game_fn)(void);
    game_fn fn = (game_fn)bmf_platform_library_symbol(library, "bmf_game");
    const bmf_game_desc* game = fn ? fn() : 0;
    if (!game) {
        bmf_platform_library_unload(library);
        bmf_fmt_buf(error, error_capacity, fn ? "bmf_game() in %s returned 0" : "%s exports no bmf_game", core_dll.copy_paths[index]);
        return 0;
    }
    typedef u64 (*build_id_fn)(void);
    build_id_fn id = (build_id_fn)bmf_platform_library_symbol(library, "bmf_game_build_id");
    *out_build_id = id ? id() : 0;   // written into the DLL's generated root by build.c
    *out_library = library;
    return game;
}

const bmf_game_desc* bmf_core_dll_open(char* error, i32 error_capacity) {
    char dir[BMF_PLATFORM_PATH_MAX * 3];
    i32 n = bmf_platform_exe_dir(dir, (i32)sizeof dir - 16);
    if (n <= 0) {
        bmf_fmt_buf(error, error_capacity, "cannot find the folder of the exe");
        return 0;
    }
    bmf_fmt_buf(core_dll.source_path, (i32)sizeof core_dll.source_path, "%sgame.dll", dir);
    bmf_fmt_buf(core_dll.copy_paths[0], (i32)sizeof core_dll.copy_paths[0], "%sgame_0.dll", dir);
    bmf_fmt_buf(core_dll.copy_paths[1], (i32)sizeof core_dll.copy_paths[1], "%sgame_1.dll", dir);
    bmf_platform_file_watch_init(&core_dll.watch, core_dll.source_path, bmf_platform_time_ns());
    core_dll.current = 0;
    core_dll.change_pending = 0;
    return core_dll_load(0, &core_dll.library, &core_dll.game_build_id, error, error_capacity);
}

// Loads game.dll into the other copy and swaps it in; prints the reload line. `since_ns`: when the
// change was seen (for the "ms" field).
static bmf_core_swap_result core_dll_reload_now(i64 since_ns, char* error, i32 error_capacity) {
    char line[BMF_CORE_ERROR_MAX * 2 + 128];
    bmf_json_writer w;
    bmf_json_init(&w, line, (i32)sizeof line - 1);
    bmf_json_object_begin(&w);
    bmf_json_key(&w, "event"); bmf_json_string(&w, "reload");
    i32 next = core_dll.current ^ 1;
    bmf_library library = 0;
    u64 build_id = 0;
    const bmf_game_desc* game = core_dll_load(next, &library, &build_id, error, error_capacity);
    u64 old_build_id = core_dll.game_build_id;
    core_dll.game_build_id = build_id;   // the swap (recordings restart) sees the new id
    if (!game) core_dll.game_build_id = old_build_id;
    bmf_core_swap_result result = game ? bmf_core_swap_game(game, error, error_capacity) : BMF_CORE_SWAP_REJECTED;
    if (result == BMF_CORE_SWAP_REJECTED) {
        if (library) bmf_platform_library_unload(library);
        core_dll.game_build_id = old_build_id;
        bmf_json_key(&w, "state"); bmf_json_string(&w, "rejected");
        bmf_json_key(&w, "detail"); bmf_json_string(&w, error);
    } else {
        bmf_platform_library_unload(core_dll.library);
        core_dll.library = library;
        core_dll.current = next;
        bmf_json_key(&w, "state"); bmf_json_string(&w, result == BMF_CORE_SWAP_KEPT ? "kept" : "reset");
        bmf_json_key(&w, "tick"); bmf_json_i64(&w, core.tick);
        bmf_json_key(&w, "ms"); bmf_json_i64(&w, (bmf_platform_time_ns() - since_ns) / 1000000);
    }
    bmf_json_object_end(&w);
    core_write_line(&w, line);
    return result;
}

// Reloads once game.dll changed and then stayed unchanged for one poll interval (the linker has
// finished writing it).
static void core_dll_poll(i64 now_ns) {
    b32 checked = now_ns >= core_dll.watch.next_check_ns;
    if (bmf_platform_file_watch_poll(&core_dll.watch, now_ns)) {
        if (!core_dll.change_pending) core_dll.change_seen_ns = now_ns;
        core_dll.change_pending = 1;
        return;
    }
    if (!checked || !core_dll.change_pending || core_dll.watch.last.size < 0) return;
    core_dll.change_pending = 0;
    char error[BMF_CORE_ERROR_MAX];
    core_dll_reload_now(core_dll.change_seen_ns, error, (i32)sizeof error);
}

bmf_core_swap_result bmf_core_dll_reload(char* error, i32 error_capacity) {
    BMF_ASSERT(core.running && error && error_capacity > 0);
    error[0] = 0;
    core_dll.change_pending = 0;
    return core_dll_reload_now(bmf_platform_time_ns(), error, error_capacity);
}
#endif

u64 bmf_core_build_id(void) {
#if defined(BMF_GAME_LINK_DLL)
    u64 ids[2] = { (u64)BMF_BUILD_ID, core_dll.game_build_id };
    return bmf_hash(ids, (i64)sizeof ids, 0);
#else
    return (u64)BMF_BUILD_ID;
#endif
}

// ---------------------------------------------------------------- display and frame loop
bmf_err bmf_core_display_open(char* error, i32 error_capacity) {
    BMF_ASSERT(core.running && !core.display_open);
    error[0] = 0;
    i32 width = core.options.width > 0 ? core.options.width : BMF_CORE_DEFAULT_WIDTH;
    i32 height = core.options.height > 0 ? core.options.height : BMF_CORE_DEFAULT_HEIGHT;
    bmf_window_size size = { width, height, 1.0f };
    if (!core.options.headless) {
        bmf_window_desc window = { core.game.name, width, height, 0 };
        if (!bmf_platform_window_open(&window)) {
            bmf_fmt_buf(error, error_capacity, "the window could not be opened");
            return BMF_ERR_IO;
        }
        core.window_open = 1;
        size = bmf_platform_window_size();
    }
    bmf_gpu_desc gpu;
    bmf_memset(&gpu, 0, sizeof gpu);
    gpu.backend = core.options.no_gpu ? BMF_GPU_BACKEND_NULL : BMF_GPU_BACKEND_D3D11;
    gpu.window = core.window_open ? bmf_platform_window_native() : 0;
    gpu.surface_width = size.width > 0 ? size.width : 1;
    gpu.surface_height = size.height > 0 ? size.height : 1;
    gpu.software = core.options.headless;
    gpu.vsync = !core.options.headless;
    gpu.debug_layer = BMF_DEBUG;
    if (bmf_gpu_init(&gpu) != BMF_OK) {
        gpu.backend = BMF_GPU_BACKEND_NULL;
        bmf_gpu_init(&gpu);   // the null backend always starts
    }
    bmf_draw2d_desc draw;
    bmf_memset(&draw, 0, sizeof draw);
    bmf_draw2d_init(&draw);
    if (core.options.headless) {
        bmf_gpu_texture_desc target;
        bmf_memset(&target, 0, sizeof target);
        target.format = bmf_gpu_surface_format();
        target.usage = BMF_GPU_TEXTURE_RENDER_TARGET | BMF_GPU_TEXTURE_COPY_SRC;
        target.width = size.width;
        target.height = size.height;
        target.label = "core.offscreen";
        core.offscreen = bmf_gpu_texture_create(&target);
    }
    bmf_draw2d_view view = { size.width, size.height, size.dpi_scale > 0.0f ? size.dpi_scale : 1.0f };
    bmf_core_set_view(view);
    core.display_open = 1;
    core.resize_pending = 0;
    core.suspended = 0;
    core.quit = 0;
    core.last_frame_ns = 0;
    return BMF_OK;
}

void bmf_core_display_close(void) {
    if (!core.display_open) return;
    if (core.offscreen.id) bmf_gpu_texture_destroy(core.offscreen);
    core.offscreen.id = 0;
    bmf_draw2d_shutdown();
    bmf_gpu_shutdown();
    if (core.window_open) bmf_platform_window_close();
    core.window_open = 0;
    core.display_open = 0;
}

// Platform events of this frame: window state for the engine, everything for input.
static void core_pump_events(void) {
    bmf_platform_poll_events();
    bmf_event event;
    while (bmf_platform_event_pop(&event)) {
        switch (event.type) {
        case BMF_EVENT_CLOSE_REQUESTED: core.quit = 1; break;
        case BMF_EVENT_RESIZED: core.resize_pending = 1; core.pending_size = event.size; break;
        case BMF_EVENT_SUSPENDED: core.suspended = 1; break;
        case BMF_EVENT_RESUMED: core.suspended = 0; core.last_frame_ns = 0; break;
        default: break;
        }
        bmf_input_event(&event);
    }
    if (core.window_open) {
        bmf_gamepad pads[BMF_GAMEPAD_MAX];
        for (i32 i = 0; i < BMF_GAMEPAD_MAX; i++) bmf_platform_gamepad_get(i, &pads[i]);
        bmf_input_set_gamepads(pads, BMF_GAMEPAD_MAX);
    }
}

b32 bmf_core_frame(void) {
    BMF_ASSERT(core.running && core.display_open);
#if defined(BMF_GAME_LINK_DLL)
    core_dll_poll(bmf_platform_time_ns());
#endif
    if (core.resize_pending) {   // outside the GPU frame (bmf_gpu_surface_resize rule)
        core.resize_pending = 0;
        bmf_window_size size = core.pending_size;
        if (size.width > 0 && size.height > 0) {
            bmf_gpu_surface_resize(size.width, size.height);
            bmf_draw2d_view view = { size.width, size.height, size.dpi_scale > 0.0f ? size.dpi_scale : 1.0f };
            bmf_core_set_view(view);
        }
    }
    b32 protocol_running = 0;
#if BMF_PROTOCOL_ENABLED
    // Requests run outside the GPU frame: `step` ticks and `shot` renders a frame of its own.
    if (core.options.control) {
        bmf_protocol_poll(core.options.headless && !bmf_protocol_running());
        if (core.quit) return 0;
        protocol_running = bmf_protocol_running();
        if (!protocol_running) core.last_frame_ns = 0;   // `run` later starts the clock fresh
    }
#endif
    if (core.suspended || core.view.width <= 0 || core.view.height <= 0) {
        core_pump_events();
        bmf_platform_sleep_ns(16000000);
        return !core.quit;
    }

    i64 frame_start = bmf_platform_time_ns();
    bmf_draw2d_begin_frame(&core.memory.frame, core.view);
    b32 surface_usable = bmf_gpu_begin_frame();   // D3D11 windowed: waits for the swapchain here
    core_pump_events();
    if (core.options.control) {
        if (protocol_running) {
            i64 now = bmf_platform_time_ns();
            i64 elapsed = core.last_frame_ns ? now - core.last_frame_ns : 0;
            core.last_frame_ns = now;
            bmf_core_advance(elapsed);
        }
    } else if (core.options.headless) {
        bmf_core_step(1);
    } else {
        i64 now = bmf_platform_time_ns();
        i64 elapsed = core.last_frame_ns ? now - core.last_frame_ns : 0;
        core.last_frame_ns = now;
        bmf_core_advance(elapsed);
    }
    bmf_core_draw(core.options.headless && !core.options.control ? 0.0f : bmf_core_alpha());
#if BMF_PROTOCOL_ENABLED
    if (core.options.replay_path && !core.options.control && !bmf_replay_playing()) core.quit = 1;   // --replay ends with the recording
#endif

    bmf_gpu_pass_desc pass;
    bmf_memset(&pass, 0, sizeof pass);
    pass.color = core.options.headless ? core.offscreen : bmf_gpu_surface();
    pass.color_load = BMF_GPU_LOAD_CLEAR;
    pass.clear_color = bmf_v4_make(0.06f, 0.07f, 0.09f, 1.0f);
    pass.label = "core.frame";
    bmf_gpu_begin_pass(&pass);
    bmf_zone_begin(BMF_ZONE_DRAW2D_FLUSH);
    bmf_draw2d_flush(core.view.width, core.view.height, bmf_gpu_surface_format());
    bmf_zone_end(BMF_ZONE_DRAW2D_FLUSH);
    bmf_gpu_end_pass();
    bmf_zone_begin(BMF_ZONE_GPU_END_FRAME);
    bmf_gpu_end_frame();
    bmf_zone_end(BMF_ZONE_GPU_END_FRAME);
    bmf_arena_reset(&core.memory.frame);
    core.stats.frames++;
#if BMF_TOOLS_ENABLED
    bmf_profile_frame(bmf_platform_time_ns() - frame_start);   // the frame's work, without the sleeps below
#else
    (void)frame_start;
#endif
    // An occluded window (covered, locked session) presents nothing and begin_frame does not
    // wait then: sleep instead of spinning. The clock still runs, so ticks keep real time.
    if (!surface_usable && core.window_open) bmf_platform_sleep_ns(16000000);
    if (protocol_running && core.options.headless) bmf_platform_sleep_ns(1000000);   // real time without vsync
    return !core.quit;
}

bmf_err bmf_core_capture(i32 width, i32 height, u8* pixels, i64 capacity) {
    BMF_ASSERT(core.running && core.display_open && width > 0 && height > 0 && capacity >= (i64)width * height * 4);
    if (bmf_gpu_backend_active() == BMF_GPU_BACKEND_NULL) return BMF_ERR_UNSUPPORTED;
    bmf_draw2d_view saved = core.view;
    b32 same_size = width == core.view.width && height == core.view.height;
    bmf_draw2d_view view = { width, height, same_size ? core.view.dpi_scale : 1.0f };
    bmf_gpu_texture target = { 0 };
    b32 temporary = !(same_size && core.offscreen.id);
    if (temporary) {
        bmf_gpu_texture_desc desc;
        bmf_memset(&desc, 0, sizeof desc);
        desc.format = bmf_gpu_surface_format();
        desc.usage = BMF_GPU_TEXTURE_RENDER_TARGET | BMF_GPU_TEXTURE_COPY_SRC;
        desc.width = width;
        desc.height = height;
        desc.label = "core.capture";
        target = bmf_gpu_texture_create(&desc);
    } else {
        target = core.offscreen;
    }
    core.view = view;   // what bmf_core_draw gives the game as ctx.view_size
    bmf_draw2d_begin_frame(&core.memory.frame, view);
    bmf_gpu_begin_frame();
    bmf_core_draw(bmf_core_alpha());
    bmf_gpu_pass_desc pass;
    bmf_memset(&pass, 0, sizeof pass);
    pass.color = target;
    pass.color_load = BMF_GPU_LOAD_CLEAR;
    pass.clear_color = bmf_v4_make(0.06f, 0.07f, 0.09f, 1.0f);
    pass.label = "core.capture";
    bmf_gpu_begin_pass(&pass);
    bmf_draw2d_flush(width, height, bmf_gpu_surface_format());
    bmf_gpu_end_pass();
    bmf_err err = bmf_gpu_texture_read_begin(target);
    bmf_gpu_end_frame();
    if (err == BMF_OK) {
        do { err = bmf_gpu_texture_read_end(pixels, capacity); } while (err == BMF_ERR_PENDING);   // D3D11: at once
    }
    bmf_arena_reset(&core.memory.frame);
    if (temporary) bmf_gpu_texture_destroy(target);
    core.view = saved;
    core.ctx.view_size = core_view_size();
    return err;
}

b32 bmf_core_display_is_open(void) { return core.display_open; }
void bmf_core_request_quit(void) { core.quit = 1; }
b32 bmf_core_quit_requested(void) { return core.quit; }

// ---------------------------------------------------------------- the run
i32 bmf_core_run(const bmf_game_desc* game, const bmf_core_options* options) {
    char error[BMF_CORE_ERROR_MAX];
    if ((options->control || options->replay_path) && !BMF_PROTOCOL_ENABLED) {
        bmf_core_print_error("not_supported", options->control ? "--control needs a debug build or a release build with BMF_AGENT=1"
                                                                : "--replay needs a debug build or a release build with BMF_AGENT=1");
        return 1;
    }
    bmf_core_options run_options = *options;
#if BMF_PROTOCOL_ENABLED
    if (options->replay_path) {
        // The run starts with the recording's seed and tick rate, then loads its start state.
        bmf_replay_header header;
        if (bmf_replay_peek(options->replay_path, &header, error, (i32)sizeof error) != BMF_OK) {
            bmf_core_print_error("replay_failed", error);
            return 1;
        }
        run_options.seed = header.seed;
        run_options.tick_rate = header.tick_rate;
    }
#endif
    options = &run_options;
    bmf_err err = bmf_core_start(game, options, error, (i32)sizeof error);
    if (err != BMF_OK) {
        bmf_core_print_error(err == BMF_ERR_IO ? "out_of_memory" : "invalid_game", error);
        return 1;
    }
#if BMF_PROTOCOL_ENABLED
    if (options->replay_path && options->verify && !options->control) {
        // Verification needs no display: every tick as fast as possible, then the verdict.
        if (bmf_replay_play_begin(options->replay_path, 1, error, (i32)sizeof error) != BMF_OK) {
            bmf_core_print_error("replay_failed", error);
            return 1;
        }
        while (bmf_replay_playing()) bmf_core_step(1);
        bmf_platform_flush(BMF_STREAM_STDOUT);
        return bmf_replay_verified() ? 0 : 1;
    }
#endif
    if (bmf_core_display_open(error, (i32)sizeof error) != BMF_OK) {
        bmf_core_print_error("window_failed", error);
        return 1;
    }
#if BMF_PROTOCOL_ENABLED
    // With --control the replay is loaded and the protocol's steps consume it (verified with --verify).
    if (options->replay_path && bmf_replay_play_begin(options->replay_path, options->verify, error, (i32)sizeof error) != BMF_OK) {
        bmf_core_print_error("replay_failed", error);
        return 1;
    }
#endif
    char line[512];
    bmf_json_writer w;
    bmf_json_init(&w, line, (i32)sizeof line - 1);
    bmf_json_object_begin(&w);
    bmf_json_key(&w, "event"); bmf_json_string(&w, "start");
    bmf_json_key(&w, "game"); bmf_json_string(&w, core.game.name);
    bmf_json_key(&w, "tick_rate"); bmf_json_i64(&w, core.ctx.tick_rate);
    bmf_json_key(&w, "seed"); bmf_json_u64(&w, core.ctx.seed);
    bmf_json_key(&w, "mode"); bmf_json_string(&w, core.options.headless ? "headless" : "windowed");
    bmf_json_key(&w, "gpu"); bmf_json_string(&w, bmf_gpu_backend_active() == BMF_GPU_BACKEND_D3D11 ? "d3d11" : "null");
#if defined(BMF_GAME_LINK_DLL)
    bmf_json_key(&w, "link"); bmf_json_string(&w, "dll");
#else
    bmf_json_key(&w, "link"); bmf_json_string(&w, "static");
#endif
    bmf_json_object_end(&w);
    core_write_line(&w, line);
    bmf_platform_flush(BMF_STREAM_STDOUT);
#if BMF_PROTOCOL_ENABLED
    if (core.options.control) {
        bmf_log_set_echo(1);   // log messages as {"event":"log",...} lines on stderr
        bmf_protocol_start();
    }
#endif

    while (bmf_core_frame()) {}
#if BMF_PROTOCOL_ENABLED
    bmf_protocol_end();
    if (core.options.profile) {
        // The profile of the whole run (zones, frames, draw stats) as one line (perf budgets, 1A-42).
        static char profile_line[32 * 1024];
        bmf_json_init(&w, profile_line, (i32)sizeof profile_line - 1);
        bmf_json_object_begin(&w);
        bmf_json_key(&w, "event"); bmf_json_string(&w, "profile");
        bmf_json_key(&w, "ticks"); bmf_json_i64(&w, core.stats.ticks);
        bmf_profile_write_report(&w);
        bmf_json_object_end(&w);
        core_write_line(&w, profile_line);
    }
#endif

    bmf_json_init(&w, line, (i32)sizeof line - 1);
    bmf_json_object_begin(&w);
    bmf_json_key(&w, "event"); bmf_json_string(&w, "quit");
    bmf_json_key(&w, "tick"); bmf_json_i64(&w, core.tick);
    bmf_json_key(&w, "frames"); bmf_json_i64(&w, core.stats.frames);
    bmf_json_object_end(&w);
    core_write_line(&w, line);
    bmf_core_display_close();
    return 0;
}
