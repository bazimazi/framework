// protocol.c: the control protocol (task 1A-31). Contracts in protocol.h, invariants in
// src/debug/README.md.

// ---------------------------------------------------------------- state
typedef struct protocol_watch {
    char path[BMF_FIELD_NAME_MAX + 32];   // name plus [index]
    u64 last_hash;                        // hash of the last reported value text (or error code)
} protocol_watch;

static struct {
    b32 active;
    b32 running;
    bmf_protocol_output_fn output;
    void* output_user;
    i32 button_hold[BMF_INPUT_MAX_ACTIONS];   // ticks left per BUTTON action held by `press`
    i32 axis_hold[BMF_INPUT_MAX_ACTIONS];     // ticks left per AXIS2 action set by `axis`
    protocol_watch watches[BMF_PROTOCOL_MAX_WATCHES];
    i32 watch_count;
} protocol;

// One response or event line is built here (static: no heap, too big for the stack).
static char protocol_response[BMF_PROTOCOL_RESPONSE_MAX];
static char protocol_event[64 * 1024];
static char protocol_value[64 * 1024];   // a watched value, before it goes into an event or response

const char* bmf_protocol_error_code(bmf_protocol_error error) {
    switch (error) {
    case BMF_PROTOCOL_OK: return "ok";
    case BMF_PROTOCOL_UNKNOWN_COMMAND: return "unknown_command";
    case BMF_PROTOCOL_BAD_ARGS: return "bad_args";
    case BMF_PROTOCOL_UNKNOWN_ACTION: return "unknown_action";
    case BMF_PROTOCOL_UNKNOWN_PATH: return "unknown_path";
    case BMF_PROTOCOL_BAD_PATH: return "bad_path";
    case BMF_PROTOCOL_INDEX_OUT_OF_RANGE: return "index_out_of_range";
    case BMF_PROTOCOL_BAD_VALUE: return "bad_value";
    case BMF_PROTOCOL_READ_ONLY: return "read_only";
    case BMF_PROTOCOL_NOT_WATCHED: return "not_watched";
    case BMF_PROTOCOL_TOO_MANY_WATCHES: return "too_many_watches";
    case BMF_PROTOCOL_RESPONSE_TOO_LARGE: return "response_too_large";
    case BMF_PROTOCOL_LINE_TOO_LONG: return "line_too_long";
    case BMF_PROTOCOL_NOT_SUPPORTED: return "not_supported";
    case BMF_PROTOCOL_NOT_IMPLEMENTED: return "not_implemented";
    case BMF_PROTOCOL_RELOAD_REJECTED: return "reload_rejected";
    case BMF_PROTOCOL_IO_FAILED: return "io_failed";
    case BMF_PROTOCOL_RECORD_FAILED: return "record_failed";
    case BMF_PROTOCOL_REPLAY_FAILED: return "replay_failed";
    case BMF_PROTOCOL_UNKNOWN_SNAP: return "unknown_snap";
    case BMF_PROTOCOL_TOO_MANY_SNAPS: return "too_many_snaps";
    case BMF_PROTOCOL_NONFINITE_STATE: return "nonfinite_state";
    default: return "?";
    }
}

static bmf_protocol_error protocol_from_reflect(bmf_reflect_status status) {
    switch (status) {
    case BMF_REFLECT_OK: return BMF_PROTOCOL_OK;
    case BMF_REFLECT_UNKNOWN_PATH: return BMF_PROTOCOL_UNKNOWN_PATH;
    case BMF_REFLECT_BAD_PATH: return BMF_PROTOCOL_BAD_PATH;
    case BMF_REFLECT_INDEX_OUT_OF_RANGE: return BMF_PROTOCOL_INDEX_OUT_OF_RANGE;
    case BMF_REFLECT_BAD_VALUE: return BMF_PROTOCOL_BAD_VALUE;
    case BMF_REFLECT_READ_ONLY: return BMF_PROTOCOL_READ_ONLY;
    default: return BMF_PROTOCOL_BAD_PATH;
    }
}

static void protocol_write_line(const char* line, i32 length) {
    if (protocol.output) { protocol.output(line, length, protocol.output_user); return; }
    bmf_platform_write(BMF_STREAM_STDOUT, line, length);
    bmf_platform_write(BMF_STREAM_STDOUT, "\n", 1);
    bmf_platform_flush(BMF_STREAM_STDOUT);
}

void bmf_protocol_write_event(const char* line, i32 length) { protocol_write_line(line, length); }

// ---------------------------------------------------------------- lifecycle
void bmf_protocol_begin(bmf_protocol_output_fn output, void* user) {
    bmf_memset(&protocol, 0, sizeof protocol);
    protocol.active = 1;
    protocol.output = output;
    protocol.output_user = user;
}

void bmf_protocol_end(void) {
    if (!protocol.active) return;
    for (i32 a = 0; a < BMF_INPUT_MAX_ACTIONS; a++) {
        if (protocol.button_hold[a] > 0) bmf_input_inject_button(a, 0);
        if (protocol.axis_hold[a] > 0) bmf_input_inject_axis(a, 0, bmf_v2_make(0.0f, 0.0f));
    }
    bmf_memset(&protocol, 0, sizeof protocol);
}

b32 bmf_protocol_running(void) { return protocol.running; }

// ---------------------------------------------------------------- watches
// Adds "value":V (or "error":CODE when the path no longer resolves, e.g. the live count shrank)
// to `w` and returns a hash of what was added.
static u64 protocol_watch_value(const protocol_watch* watch, bmf_json_writer* w) {
    bmf_reflect r = bmf_core_reflect();
    char detail[256];
    bmf_json_writer value;
    bmf_json_init(&value, protocol_value, (i32)sizeof protocol_value);
    bmf_reflect_status status = bmf_reflect_get(&r, bmf_str_from_cstr(watch->path), &value, detail, (i32)sizeof detail);
    i32 start = w->len;
    if (status == BMF_REFLECT_OK) {
        bmf_json_key(w, "value");
        bmf_json_raw(w, protocol_value);
    } else {
        bmf_json_key(w, "error");
        bmf_json_string(w, bmf_reflect_status_code(status));
    }
    return bmf_hash(w->buf + start, w->len - start, 0);
}

void bmf_protocol_after_tick(void) {
    if (!protocol.active) return;
    for (i32 a = 0; a < BMF_INPUT_MAX_ACTIONS; a++) {
        if (protocol.button_hold[a] > 0 && --protocol.button_hold[a] == 0) bmf_input_inject_button(a, 0);
        if (protocol.axis_hold[a] > 0 && --protocol.axis_hold[a] == 0) bmf_input_inject_axis(a, 0, bmf_v2_make(0.0f, 0.0f));
    }
    for (i32 i = 0; i < protocol.watch_count; i++) {
        protocol_watch* watch = &protocol.watches[i];
        bmf_json_writer w;
        bmf_json_init(&w, protocol_event, (i32)sizeof protocol_event);
        bmf_json_object_begin(&w);
        bmf_json_key(&w, "event"); bmf_json_string(&w, "watch");
        bmf_json_key(&w, "path"); bmf_json_string(&w, watch->path);
        bmf_json_key(&w, "tick"); bmf_json_i64(&w, bmf_core_ctx()->tick);
        u64 hash = protocol_watch_value(watch, &w);
        bmf_json_object_end(&w);
        if (hash == watch->last_hash) continue;
        watch->last_hash = hash;
        protocol_write_line(protocol_event, w.len);
    }
}

// ---------------------------------------------------------------- commands
// A handler gets the arguments after the command name (count within the table's bounds) and a
// writer that already holds {"ok":1 ; it adds its fields, or returns an error with a detail.
typedef bmf_protocol_error (*protocol_handler)(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity);

static b32 protocol_parse_count(bmf_str token, i32 min, i32* out) {
    i32 v;
    if (bmf_parse_i32(token, &v) != BMF_OK || v < min) return 0;
    *out = v;
    return 1;
}

static b32 protocol_parse_finite(bmf_str token, f32* out) {
    f32 v;
    if (bmf_parse_f32(token, &v) != BMF_OK || (bmf_f32_bits(v) & 0x7F800000u) == 0x7F800000u) return 0;
    *out = v;
    return 1;
}

// Finds an action by name and checks its type.
static bmf_protocol_error protocol_find_action(bmf_str name, bmf_action_type type, i32* out, char* detail, i32 detail_capacity) {
    i32 action = bmf_input_action_find(name);
    if (action < 0) {
        bmf_fmt_buf(detail, detail_capacity, "no action '%S'", name);
        return BMF_PROTOCOL_UNKNOWN_ACTION;
    }
    if (bmf_input_action_type(action) != type) {
        bmf_fmt_buf(detail, detail_capacity, "'%S' is not %s action", name, type == BMF_ACTION_BUTTON ? "a button" : "an axis2");
        return BMF_PROTOCOL_UNKNOWN_ACTION;
    }
    *out = action;
    return BMF_PROTOCOL_OK;
}

static bmf_protocol_error protocol_cmd_hello(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    bmf_json_key(w, "bmf"); bmf_json_i64(w, BMF_API_VERSION);
    bmf_json_key(w, "game"); bmf_json_string(w, bmf_core_game()->name);
    bmf_json_key(w, "tick_rate"); bmf_json_i64(w, bmf_core_ctx()->tick_rate);
    return BMF_PROTOCOL_OK;
}

static bmf_protocol_error protocol_cmd_step(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    i32 ticks;
    if (!protocol_parse_count(args[0], 0, &ticks)) return BMF_PROTOCOL_BAD_ARGS;
    bmf_core_step(ticks);
    bmf_json_key(w, "tick"); bmf_json_i64(w, bmf_core_ctx()->tick);
    return BMF_PROTOCOL_OK;
}

static bmf_protocol_error protocol_cmd_run(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    protocol.running = 1;
    bmf_json_key(w, "tick"); bmf_json_i64(w, bmf_core_ctx()->tick);
    bmf_json_key(w, "running"); bmf_json_bool(w, 1);
    return BMF_PROTOCOL_OK;
}

static bmf_protocol_error protocol_cmd_pause(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    protocol.running = 0;
    bmf_json_key(w, "tick"); bmf_json_i64(w, bmf_core_ctx()->tick);
    bmf_json_key(w, "running"); bmf_json_bool(w, 0);
    return BMF_PROTOCOL_OK;
}

static bmf_protocol_error protocol_cmd_press(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    i32 ticks = 1;
    if (count > 1 && !protocol_parse_count(args[1], 1, &ticks)) return BMF_PROTOCOL_BAD_ARGS;
    i32 action;
    bmf_protocol_error e = protocol_find_action(args[0], BMF_ACTION_BUTTON, &action, detail, detail_capacity);
    if (e != BMF_PROTOCOL_OK) return e;
    bmf_input_inject_button(action, 1);
    protocol.button_hold[action] = ticks;
    return BMF_PROTOCOL_OK;
}

static bmf_protocol_error protocol_cmd_axis(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    f32 x, y;
    i32 ticks = 1;
    if (!protocol_parse_finite(args[1], &x) || !protocol_parse_finite(args[2], &y)) return BMF_PROTOCOL_BAD_ARGS;
    if (count > 3 && !protocol_parse_count(args[3], 1, &ticks)) return BMF_PROTOCOL_BAD_ARGS;
    i32 action;
    bmf_protocol_error e = protocol_find_action(args[0], BMF_ACTION_AXIS2, &action, detail, detail_capacity);
    if (e != BMF_PROTOCOL_OK) return e;
    bmf_input_inject_axis(action, 1, bmf_v2_make(x, y));
    protocol.axis_hold[action] = ticks;
    return BMF_PROTOCOL_OK;
}

static bmf_protocol_error protocol_cmd_pointer(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    i32 slot;
    f32 x, y;
    if (!protocol_parse_count(args[0], 0, &slot) || slot >= BMF_INPUT_MAX_POINTERS) return BMF_PROTOCOL_BAD_ARGS;
    if (!protocol_parse_finite(args[1], &x) || !protocol_parse_finite(args[2], &y)) return BMF_PROTOCOL_BAD_ARGS;
    bmf_input_pointer_change change;
    if (bmf_str_eq_cstr(args[3], "down")) change = BMF_INPUT_POINTER_DOWN;
    else if (bmf_str_eq_cstr(args[3], "up")) change = BMF_INPUT_POINTER_UP;
    else if (bmf_str_eq_cstr(args[3], "move")) change = BMF_INPUT_POINTER_MOVE;
    else return BMF_PROTOCOL_BAD_ARGS;
    b32 world = 1;
    if (count > 4) {
        if (bmf_str_eq_cstr(args[4], "screen")) world = 0;
        else if (!bmf_str_eq_cstr(args[4], "world")) return BMF_PROTOCOL_BAD_ARGS;
    }
    bmf_input_inject_pointer(slot, change, bmf_v2_make(x, y), world);
    return BMF_PROTOCOL_OK;
}

static bmf_protocol_error protocol_cmd_key(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    bmf_key key = bmf_key_from_name(args[0]);
    if (key == BMF_KEY_NONE) {
        bmf_fmt_buf(detail, detail_capacity, "no key '%S' (names like a, 1, space, left_shift, f12)", args[0]);
        return BMF_PROTOCOL_BAD_ARGS;
    }
    bmf_event event;
    bmf_memset(&event, 0, sizeof event);
    if (bmf_str_eq_cstr(args[1], "down")) event.type = BMF_EVENT_KEY_DOWN;
    else if (bmf_str_eq_cstr(args[1], "up")) event.type = BMF_EVENT_KEY_UP;
    else return BMF_PROTOCOL_BAD_ARGS;
    event.key.key = key;
    bmf_input_event(&event);
    return BMF_PROTOCOL_OK;
}

static bmf_protocol_error protocol_cmd_get(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    bmf_reflect r = bmf_core_reflect();
    bmf_json_key(w, "path"); bmf_json_str(w, args[0]);
    bmf_json_key(w, "value");
    return protocol_from_reflect(bmf_reflect_get(&r, args[0], w, detail, detail_capacity));
}

static bmf_protocol_error protocol_cmd_set(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    bmf_reflect r = bmf_core_reflect();
    bmf_reflect_status status = bmf_reflect_set(&r, args[0], args + 1, count - 1, detail, detail_capacity);
    if (status != BMF_REFLECT_OK) return protocol_from_reflect(status);
    // The state changed outside a tick: recordings must carry the write (replay.h, record 0x03).
    bmf_reflect_target target;
    bmf_reflect_resolve(&r, args[0], &target, detail, detail_capacity);
    i32 offset, size;
    bmf_reflect_target_bytes(&r, target, &offset, &size);
    bmf_replay_note_state_write(offset, size);
    bmf_json_key(w, "path"); bmf_json_str(w, args[0]);
    bmf_json_key(w, "value");
    return protocol_from_reflect(bmf_reflect_get(&r, args[0], w, detail, detail_capacity));
}

static bmf_protocol_error protocol_cmd_dump(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    bmf_reflect r = bmf_core_reflect();
    bmf_json_key(w, "fields");
    bmf_reflect_dump(&r, count > 0 ? args[0] : bmf_str_make(0, 0), w);
    return BMF_PROTOCOL_OK;
}

static i32 protocol_watch_find(bmf_str path) {
    for (i32 i = 0; i < protocol.watch_count; i++)
        if (bmf_str_eq_cstr(path, protocol.watches[i].path)) return i;
    return -1;
}

static bmf_protocol_error protocol_cmd_watch(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    bmf_reflect r = bmf_core_reflect();
    bmf_reflect_target target;
    bmf_reflect_status status = bmf_reflect_resolve(&r, args[0], &target, detail, detail_capacity);
    if (status != BMF_REFLECT_OK) return protocol_from_reflect(status);
    i32 index = protocol_watch_find(args[0]);
    if (index < 0) {
        if (protocol.watch_count >= BMF_PROTOCOL_MAX_WATCHES) {
            bmf_fmt_buf(detail, detail_capacity, "at most %i watches", (i32)BMF_PROTOCOL_MAX_WATCHES);
            return BMF_PROTOCOL_TOO_MANY_WATCHES;
        }
        if (args[0].len >= (i32)sizeof protocol.watches[0].path) return BMF_PROTOCOL_BAD_ARGS;   // resolve already bounds names
        index = protocol.watch_count++;
        protocol_watch* watch = &protocol.watches[index];
        bmf_memcpy(watch->path, args[0].ptr, args[0].len);
        watch->path[args[0].len] = 0;
    }
    bmf_json_key(w, "path"); bmf_json_str(w, args[0]);
    protocol.watches[index].last_hash = protocol_watch_value(&protocol.watches[index], w);
    return BMF_PROTOCOL_OK;
}

static bmf_protocol_error protocol_cmd_unwatch(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    i32 index = protocol_watch_find(args[0]);
    if (index < 0) {
        bmf_fmt_buf(detail, detail_capacity, "'%S' is not watched", args[0]);
        return BMF_PROTOCOL_NOT_WATCHED;
    }
    for (i32 i = index; i + 1 < protocol.watch_count; i++) protocol.watches[i] = protocol.watches[i + 1];   // events keep watch order
    protocol.watch_count--;
    bmf_json_key(w, "path"); bmf_json_str(w, args[0]);
    return BMF_PROTOCOL_OK;
}

static bmf_protocol_error protocol_cmd_reload(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
#if defined(BMF_GAME_LINK_DLL)
    bmf_core_swap_result result = bmf_core_dll_reload(detail, detail_capacity);
    if (result == BMF_CORE_SWAP_REJECTED) return BMF_PROTOCOL_RELOAD_REJECTED;
    bmf_json_key(w, "state"); bmf_json_string(w, result == BMF_CORE_SWAP_KEPT ? "kept" : "reset");
    bmf_json_key(w, "tick"); bmf_json_i64(w, bmf_core_ctx()->tick);
    return BMF_PROTOCOL_OK;
#else
    bmf_fmt_buf(detail, detail_capacity, "reload needs the debug build, where the game is a DLL (D-13)");
    return BMF_PROTOCOL_NOT_SUPPORTED;
#endif
}

static bmf_protocol_error protocol_cmd_shot(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    if (count == 2) return BMF_PROTOCOL_BAD_ARGS;
    i32 width = 0, height = 0;
    if (count == 3 && (!protocol_parse_count(args[1], 1, &width) || !protocol_parse_count(args[2], 1, &height))) return BMF_PROTOCOL_BAD_ARGS;
    char path[BMF_PLATFORM_PATH_MAX];
    if (args[0].len >= (i32)sizeof path) return BMF_PROTOCOL_BAD_ARGS;
    bmf_memcpy(path, args[0].ptr, args[0].len);
    path[args[0].len] = 0;
    i32 out_width = 0, out_height = 0;
    i64 bytes = 0;
    bmf_err err = bmf_shot_save(path, width, height, &out_width, &out_height, &bytes, detail, detail_capacity);
    if (err == BMF_ERR_UNSUPPORTED) return BMF_PROTOCOL_NOT_SUPPORTED;
    if (err == BMF_ERR_IO) return BMF_PROTOCOL_IO_FAILED;
    if (err != BMF_OK) return BMF_PROTOCOL_BAD_ARGS;
    bmf_json_key(w, "path"); bmf_json_string(w, path);
    bmf_json_key(w, "width"); bmf_json_i64(w, out_width);
    bmf_json_key(w, "height"); bmf_json_i64(w, out_height);
    bmf_json_key(w, "bytes"); bmf_json_i64(w, bytes);
    bmf_json_key(w, "tick"); bmf_json_i64(w, bmf_core_ctx()->tick);
    return BMF_PROTOCOL_OK;
}

// Copies a token into a NUL-terminated path buffer; 0 when it does not fit.
static b32 protocol_path(bmf_str token, char* path, i32 capacity) {
    if (token.len >= capacity) return 0;
    bmf_memcpy(path, token.ptr, token.len);
    path[token.len] = 0;
    return 1;
}

static bmf_protocol_error protocol_cmd_record(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    char path[BMF_PLATFORM_PATH_MAX];
    if (bmf_str_eq_cstr(args[0], "start") && count == 2) {
        if (!protocol_path(args[1], path, (i32)sizeof path)) return BMF_PROTOCOL_BAD_ARGS;
        if (bmf_replay_record_start(path, detail, detail_capacity) != BMF_OK) return BMF_PROTOCOL_RECORD_FAILED;
        bmf_json_key(w, "path"); bmf_json_string(w, path);
        bmf_json_key(w, "tick"); bmf_json_i64(w, bmf_core_ctx()->tick);
        return BMF_PROTOCOL_OK;
    }
    if (bmf_str_eq_cstr(args[0], "stop") && count == 1) {
        i64 ticks = 0, bytes = 0;
        i32 checkpoints = 0;
        bmf_err err = bmf_replay_record_stop(&ticks, &bytes, &checkpoints, detail, detail_capacity);
        if (err == BMF_ERR_IO) return BMF_PROTOCOL_IO_FAILED;
        if (err != BMF_OK) return BMF_PROTOCOL_RECORD_FAILED;
        bmf_json_key(w, "ticks"); bmf_json_i64(w, ticks);
        bmf_json_key(w, "bytes"); bmf_json_i64(w, bytes);
        bmf_json_key(w, "checkpoints"); bmf_json_i64(w, checkpoints);
        return BMF_PROTOCOL_OK;
    }
    return BMF_PROTOCOL_BAD_ARGS;
}

static bmf_protocol_error protocol_cmd_replay(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    char path[BMF_PLATFORM_PATH_MAX];
    if (!protocol_path(args[0], path, (i32)sizeof path)) return BMF_PROTOCOL_BAD_ARGS;
    bmf_replay_header h;
    if (bmf_replay_peek(path, &h, detail, detail_capacity) != BMF_OK) return BMF_PROTOCOL_REPLAY_FAILED;
    if (bmf_replay_play_begin(path, 1, detail, detail_capacity) != BMF_OK) return BMF_PROTOCOL_REPLAY_FAILED;
    bmf_json_key(w, "path"); bmf_json_string(w, path);
    bmf_json_key(w, "start_tick"); bmf_json_i64(w, h.start_tick);
    bmf_json_key(w, "end_tick"); bmf_json_i64(w, h.end_tick);
    bmf_json_key(w, "checkpoints"); bmf_json_i64(w, h.checkpoint_count);
    return BMF_PROTOCOL_OK;
}

static bmf_protocol_error protocol_cmd_hash(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    u64 hash = 0;
    char path[BMF_FIELD_NAME_MAX + 32];
    if (!bmf_snap_hash(&hash, path, (i32)sizeof path)) {
        bmf_fmt_buf(detail, detail_capacity, "'%s' is NaN or Inf at tick %li: never allowed in the state (D-37 rule 11)", path, bmf_core_ctx()->tick);
        return BMF_PROTOCOL_NONFINITE_STATE;
    }
    char hex[20];
    bmf_fmt_buf(hex, (i32)sizeof hex, "%016lx", hash);
    bmf_json_key(w, "tick"); bmf_json_i64(w, bmf_core_ctx()->tick);
    bmf_json_key(w, "hash"); bmf_json_string(w, hex);
    return BMF_PROTOCOL_OK;
}

static bmf_protocol_error protocol_cmd_snap(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    bmf_snap_status status;
    if (bmf_str_eq_cstr(args[0], "save")) status = bmf_snap_save(args[1], detail, detail_capacity);
    else if (bmf_str_eq_cstr(args[0], "load")) {
        i64 tick = 0;
        status = bmf_snap_load(args[1], &tick, detail, detail_capacity);
    } else return BMF_PROTOCOL_BAD_ARGS;
    if (status == BMF_SNAP_UNKNOWN) return BMF_PROTOCOL_UNKNOWN_SNAP;
    if (status == BMF_SNAP_FULL) return BMF_PROTOCOL_TOO_MANY_SNAPS;
    if (status == BMF_SNAP_NO_MEMORY) return BMF_PROTOCOL_NOT_SUPPORTED;
    if (status != BMF_SNAP_OK) return BMF_PROTOCOL_BAD_ARGS;
    bmf_json_key(w, "name"); bmf_json_str(w, args[1]);
    bmf_json_key(w, "tick"); bmf_json_i64(w, bmf_core_ctx()->tick);
    return BMF_PROTOCOL_OK;
}

static bmf_protocol_error protocol_cmd_profile(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    i32 ticks = 0;
    if (!protocol_parse_count(args[0], 0, &ticks)) return BMF_PROTOCOL_BAD_ARGS;
    if (ticks > 0) {
        bmf_profile_window_reset();   // the report covers exactly these ticks
        bmf_core_step(ticks);
    }
    bmf_json_key(w, "tick"); bmf_json_i64(w, bmf_core_ctx()->tick);
    bmf_json_key(w, "ticks"); bmf_json_i64(w, ticks);
    bmf_profile_write_report(w);
    bmf_profile_window_reset();       // `profile 0` later reports what happened since this one
    return BMF_PROTOCOL_OK;
}

static bmf_protocol_error protocol_cmd_mem(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    bmf_profile_write_mem(w);
    return BMF_PROTOCOL_OK;
}

static bmf_protocol_error protocol_cmd_log(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    bmf_log_level min = BMF_LOG_DEBUG;
    if (count == 1) {
        i32 found = -1;
        for (i32 l = 0; l < BMF_LOG_LEVEL_COUNT; l++)
            if (bmf_str_eq_cstr(args[0], bmf_log_level_name((bmf_log_level)l))) found = l;
        if (found < 0) {
            bmf_fmt_buf(detail, detail_capacity, "no log level '%S' (debug, info, warn, error)", args[0]);
            return BMF_PROTOCOL_BAD_ARGS;
        }
        min = (bmf_log_level)found;
    }
    bmf_log_drain(w, min);
    return BMF_PROTOCOL_OK;
}

static bmf_protocol_error protocol_cmd_quit(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    bmf_core_request_quit();
    return BMF_PROTOCOL_OK;
}

// The dispatch table: adding a command is one row. `task` names the task that implements a
// command not available yet (its handler is 0).
typedef struct protocol_command {
    const char* name;
    i32 min_args;
    i32 max_args;
    protocol_handler handler;
    const char* usage;
    const char* task;
} protocol_command;

static bmf_protocol_error protocol_cmd_help(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity);

static const protocol_command PROTOCOL_COMMANDS[] = {
    { "help",    0, 0, protocol_cmd_help,    "help", 0 },
    { "hello",   0, 0, protocol_cmd_hello,   "hello", 0 },
    { "step",    1, 1, protocol_cmd_step,    "step N (N >= 0)", 0 },
    { "run",     0, 0, protocol_cmd_run,     "run", 0 },
    { "pause",   0, 0, protocol_cmd_pause,   "pause", 0 },
    { "press",   1, 2, protocol_cmd_press,   "press ACTION [TICKS >= 1]", 0 },
    { "axis",    3, 4, protocol_cmd_axis,    "axis ACTION X Y [TICKS >= 1]", 0 },
    { "pointer", 4, 5, protocol_cmd_pointer, "pointer I(0..3) X Y down|up|move [world|screen]", 0 },
    { "key",     2, 2, protocol_cmd_key,     "key NAME down|up", 0 },
    { "get",     1, 1, protocol_cmd_get,     "get PATH", 0 },
    { "set",     2, 5, protocol_cmd_set,     "set PATH VALUE...", 0 },
    { "dump",    0, 1, protocol_cmd_dump,    "dump [PREFIX]", 0 },
    { "watch",   1, 1, protocol_cmd_watch,   "watch PATH", 0 },
    { "unwatch", 1, 1, protocol_cmd_unwatch, "unwatch PATH", 0 },
    { "reload",  0, 0, protocol_cmd_reload,  "reload", 0 },
    { "quit",    0, 0, protocol_cmd_quit,    "quit", 0 },
    { "shot",    1, 3, protocol_cmd_shot, "shot PATH.png [W H]", 0 },
    { "record",  1, 2, protocol_cmd_record, "record start PATH | record stop", 0 },
    { "replay",  1, 1, protocol_cmd_replay, "replay PATH", 0 },
    { "snap",    2, 2, protocol_cmd_snap, "snap save|load NAME", 0 },
    { "hash",    0, 0, protocol_cmd_hash, "hash", 0 },
    { "profile", 1, 1, protocol_cmd_profile, "profile N (N >= 0)", 0 },
    { "mem",     0, 0, protocol_cmd_mem, "mem", 0 },
    { "log",     0, 1, protocol_cmd_log, "log [debug|info|warn|error]", 0 },
    { "phys",    1, BMF_PROTOCOL_MAX_TOKENS - 1, 0, "phys query ...", "1B-09" },
};
enum { PROTOCOL_COMMAND_COUNT = (i32)(sizeof PROTOCOL_COMMANDS / sizeof PROTOCOL_COMMANDS[0]) };

// Every command's usage line (commands of later tasks say which task brings them).
static bmf_protocol_error protocol_cmd_help(const bmf_str* args, i32 count, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    bmf_json_key(w, "commands");
    bmf_json_array_begin(w);
    for (i32 c = 0; c < PROTOCOL_COMMAND_COUNT; c++) {
        char text[128];
        if (PROTOCOL_COMMANDS[c].task) bmf_fmt_buf(text, (i32)sizeof text, "%s (task %s)", PROTOCOL_COMMANDS[c].usage, PROTOCOL_COMMANDS[c].task);
        else bmf_fmt_buf(text, (i32)sizeof text, "%s", PROTOCOL_COMMANDS[c].usage);
        bmf_json_string(w, text);
    }
    bmf_json_array_end(w);
    return BMF_PROTOCOL_OK;
}

static void protocol_write_error(bmf_protocol_error error, const char* detail) {
    bmf_json_writer w;
    bmf_json_init(&w, protocol_response, (i32)sizeof protocol_response);
    bmf_json_object_begin(&w);
    bmf_json_key(&w, "ok"); bmf_json_i64(&w, 0);
    bmf_json_key(&w, "error"); bmf_json_string(&w, bmf_protocol_error_code(error));
    bmf_json_key(&w, "detail"); bmf_json_string(&w, detail);
    bmf_json_object_end(&w);
    protocol_write_line(protocol_response, w.len);
}

void bmf_protocol_execute(bmf_str line) {
    BMF_ASSERT(protocol.active);
    // A UTF-8 byte order mark (Windows PowerShell 5.1 pipes one in front of the first line) is skipped.
    if (line.len >= 3 && line.ptr[0] == 0xEF && line.ptr[1] == 0xBB && line.ptr[2] == 0xBF) line = bmf_str_slice(line, 3, line.len);
    // Tokens: split on spaces and tabs.
    bmf_str tokens[BMF_PROTOCOL_MAX_TOKENS];
    i32 count = 0;
    b32 too_many = 0;
    for (i32 i = 0; i < line.len;) {
        while (i < line.len && (line.ptr[i] == ' ' || line.ptr[i] == '\t' || line.ptr[i] == '\r')) i++;
        if (i >= line.len) break;
        i32 start = i;
        while (i < line.len && line.ptr[i] != ' ' && line.ptr[i] != '\t' && line.ptr[i] != '\r') i++;
        if (count < BMF_PROTOCOL_MAX_TOKENS) tokens[count++] = bmf_str_slice(line, start, i);
        else too_many = 1;
    }
    if (count == 0 || tokens[0].ptr[0] == '#') return;   // not a request

    char detail[1024];
    detail[0] = 0;
    const protocol_command* command = 0;
    for (i32 c = 0; c < PROTOCOL_COMMAND_COUNT; c++)
        if (bmf_str_eq_cstr(tokens[0], PROTOCOL_COMMANDS[c].name)) command = &PROTOCOL_COMMANDS[c];
    if (!command) {
        const char* best = 0;
        i32 best_distance = 3;   // suggest within 2 edits
        for (i32 c = 0; c < PROTOCOL_COMMAND_COUNT; c++) {
            i32 d = bmf_str_edit_distance(tokens[0], bmf_str_from_cstr(PROTOCOL_COMMANDS[c].name));
            if (d >= 0 && d < best_distance) { best = PROTOCOL_COMMANDS[c].name; best_distance = d; }
        }
        if (best) bmf_fmt_buf(detail, (i32)sizeof detail, "no command '%S'; did you mean '%s'?", tokens[0], best);
        else bmf_fmt_buf(detail, (i32)sizeof detail, "no command '%S' (help, hello, step, run, pause, press, axis, pointer, key, get, set, dump, watch, unwatch, hash, snap, shot, record, replay, profile, mem, log, reload, quit)", tokens[0]);
        protocol_write_error(BMF_PROTOCOL_UNKNOWN_COMMAND, detail);
        return;
    }
    i32 args = count - 1;
    if (too_many || args < command->min_args || args > command->max_args) {
        bmf_fmt_buf(detail, (i32)sizeof detail, "usage: %s", command->usage);
        protocol_write_error(BMF_PROTOCOL_BAD_ARGS, detail);
        return;
    }
    if (!command->handler) {
        bmf_fmt_buf(detail, (i32)sizeof detail, "%s arrives with task %s", command->name, command->task);
        protocol_write_error(BMF_PROTOCOL_NOT_IMPLEMENTED, detail);
        return;
    }
    bmf_json_writer w;
    bmf_json_init(&w, protocol_response, (i32)sizeof protocol_response);
    bmf_json_object_begin(&w);
    bmf_json_key(&w, "ok"); bmf_json_i64(&w, 1);
    bmf_protocol_error error = command->handler(tokens + 1, args, &w, detail, (i32)sizeof detail);
    if (error == BMF_PROTOCOL_OK) bmf_json_object_end(&w);   // on an error the half-written response is dropped
    if (error == BMF_PROTOCOL_BAD_ARGS && !detail[0]) bmf_fmt_buf(detail, (i32)sizeof detail, "usage: %s", command->usage);
    if (error == BMF_PROTOCOL_OK && w.overflow) {
        error = BMF_PROTOCOL_RESPONSE_TOO_LARGE;
        bmf_fmt_buf(detail, (i32)sizeof detail, "the response is over %i bytes (use dump PREFIX or get PATH[i])", (i32)BMF_PROTOCOL_RESPONSE_MAX);
    }
    if (error != BMF_PROTOCOL_OK) { protocol_write_error(error, detail); return; }
    protocol_write_line(protocol_response, w.len);
}

// ---------------------------------------------------------------- stdin reader thread
typedef struct protocol_line {
    i32 length;
    b32 truncated;   // longer than BMF_PROTOCOL_LINE_MAX - 1: answered with line_too_long
    b32 eof;         // the end of stdin (no text)
    char text[BMF_PROTOCOL_LINE_MAX];
} protocol_line;

// Single producer (the reader thread) / single consumer (the main thread). `written` and `read`
// only grow; slot = counter % BMF_PROTOCOL_QUEUE_LINES. Each side waits on the other's counter.
static struct {
    protocol_line lines[BMF_PROTOCOL_QUEUE_LINES];
    i32 written;
    i32 read;
    bmf_thread thread;
} protocol_queue;

static protocol_line* protocol_queue_slot_for_writing(void) {
    i32 written = protocol_queue.written;   // only this thread writes it
    for (;;) {
        i32 read = bmf_atomic_load_i32(&protocol_queue.read);
        if (written - read < BMF_PROTOCOL_QUEUE_LINES) break;
        bmf_platform_futex_wait(&protocol_queue.read, read);   // full: wait for the main thread
    }
    protocol_line* line = &protocol_queue.lines[written % BMF_PROTOCOL_QUEUE_LINES];
    line->length = 0;
    line->truncated = 0;
    line->eof = 0;
    return line;
}

static void protocol_queue_publish(void) {
    bmf_atomic_store_i32(&protocol_queue.written, protocol_queue.written + 1);
    bmf_platform_futex_wake_all(&protocol_queue.written);
}

static void protocol_reader(void* user) {
    (void)user;
    char chunk[4096];
    protocol_line* line = protocol_queue_slot_for_writing();
    for (;;) {
        i32 got = bmf_platform_stdin_read(chunk, (i32)sizeof chunk);
        if (got <= 0) break;
        for (i32 i = 0; i < got; i++) {
            char c = chunk[i];
            if (c == '\n') {
                protocol_queue_publish();
                line = protocol_queue_slot_for_writing();
                continue;
            }
            if (line->length < BMF_PROTOCOL_LINE_MAX - 1) line->text[line->length++] = c;
            else line->truncated = 1;
        }
    }
    if (line->length > 0 || line->truncated) {   // a last line without a newline
        protocol_queue_publish();
        line = protocol_queue_slot_for_writing();
    }
    line->eof = 1;
    protocol_queue_publish();
}

void bmf_protocol_start(void) {
    bmf_protocol_begin(0, 0);
    protocol_queue.written = 0;
    protocol_queue.read = 0;
    protocol_queue.thread = bmf_platform_thread_start(protocol_reader, 0, 64 * 1024, "bmf stdin");
}

void bmf_protocol_poll(b32 wait) {
    for (;;) {
        i32 read = protocol_queue.read;   // only this thread writes it
        i32 written = bmf_atomic_load_i32(&protocol_queue.written);
        if (written == read) {
            if (!wait) return;
            bmf_platform_futex_wait(&protocol_queue.written, written);
            continue;
        }
        protocol_line* line = &protocol_queue.lines[read % BMF_PROTOCOL_QUEUE_LINES];
        b32 eof = line->eof;
        if (eof) {
            bmf_core_request_quit();
        } else if (line->truncated) {
            char detail[128];
            bmf_fmt_buf(detail, (i32)sizeof detail, "a request line is at most %i bytes", (i32)BMF_PROTOCOL_LINE_MAX - 1);
            protocol_write_error(BMF_PROTOCOL_LINE_TOO_LONG, detail);
        } else {
            bmf_protocol_execute(bmf_str_make(line->text, line->length));
        }
        bmf_atomic_store_i32(&protocol_queue.read, read + 1);
        bmf_platform_futex_wake_one(&protocol_queue.read);
        if (eof || bmf_core_quit_requested()) return;
        wait = 0;   // one request handled: let the frame continue
    }
}
