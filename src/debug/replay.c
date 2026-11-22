// replay.c: recording and replaying input (task 1A-33). Format and contracts in replay.h.

enum { REPLAY_TAG_INPUT = 1, REPLAY_TAG_INPUT_SAME = 2, REPLAY_TAG_STATE = 3 };

// ---------------------------------------------------------------- RLE codec
i64 bmf_replay_rle_bound(i64 n) { return n + n / 128 + 2; }

i64 bmf_replay_rle_encode(const u8* data, i64 n, u8* out) {
    i64 o = 0;
    i64 i = 0;
    while (i < n) {
        // A run of 2..129 equal bytes?
        i64 run = 1;
        while (i + run < n && run < 129 && data[i + run] == data[i]) run++;
        if (run >= 2) {
            out[o++] = (u8)(126 + run);
            out[o++] = data[i];
            i += run;
            continue;
        }
        // Literals up to the next run of 3 or more (a run of 2 inside literals costs the same).
        i64 start = i;
        i64 count = 0;
        while (i < n && count < 128) {
            if (i + 2 < n && data[i] == data[i + 1] && data[i] == data[i + 2]) break;
            i++;
            count++;
        }
        out[o++] = (u8)(count - 1);
        bmf_memcpy(out + o, data + start, count);
        o += count;
    }
    return o;
}

i64 bmf_replay_rle_decode(const u8* in, i64 in_size, u8* out, i64 n) {
    i64 at = 0;
    i64 produced = 0;
    while (produced < n) {
        if (at >= in_size) return -1;
        u8 c = in[at++];
        if (c < 128) {
            i64 count = (i64)c + 1;
            if (produced + count > n || at + count > in_size) return -1;
            bmf_memcpy(out + produced, in + at, count);
            at += count;
            produced += count;
        } else {
            i64 count = (i64)c - 126;
            if (produced + count > n || at >= in_size) return -1;
            bmf_memset(out + produced, in[at++], count);
            produced += count;
        }
    }
    return at;
}

// Skips one record of a stream; returns its size or -1 when malformed.
static i64 replay_record_size(const u8* stream, i64 size, i64 at) {
    static u8 scratch[1 << 16];
    u8 tag = stream[at];
    if (tag == REPLAY_TAG_INPUT_SAME) return 1;
    if (tag == REPLAY_TAG_INPUT) {
        i64 used = bmf_replay_rle_decode(stream + at + 1, size - at - 1, scratch, (i64)sizeof(bmf_input));
        return used < 0 ? -1 : 1 + used;
    }
    if (tag == REPLAY_TAG_STATE) {
        if (size - at < 9) return -1;
        u32 length;
        bmf_memcpy(&length, stream + at + 5, 4);
        // Decode in pieces through the scratch buffer just to measure.
        i64 consumed = 0;
        const u8* in = stream + at + 9;
        i64 in_size = size - at - 9;
        i64 left = length;
        while (left > 0) {
            // Each control byte covers at most 129 bytes; decode control by control.
            if (consumed >= in_size) return -1;
            u8 c = in[consumed];
            i64 count = c < 128 ? (i64)c + 1 : (i64)c - 126;
            i64 bytes = c < 128 ? 1 + count : 2;
            if (count > left || consumed + bytes > in_size) return -1;
            consumed += bytes;
            left -= count;
        }
        return 9 + consumed;
    }
    return -1;
}

static b32 replay_header_valid(const bmf_replay_header* h, i64 size) {
    if (size < (i64)sizeof *h || bmf_memcmp(h->magic, "BMFR", 4) != 0) return 0;
    if (h->version != BMF_REPLAY_VERSION || h->header_size != sizeof *h) return 0;
    if (h->state_size < 1 || h->field_count < 0 || h->field_count > BMF_FIELD_MAX_COUNT || h->checkpoint_count < 0) return 0;
    if (h->end_tick < h->start_tick || h->tick_rate < BMF_CORE_MIN_TICK_RATE || h->tick_rate > BMF_CORE_MAX_TICK_RATE) return 0;
    i64 stride = 16 + 8 * (i64)h->field_count;
    if (h->state_offset < (i64)sizeof *h || h->state_bytes < 0 || h->state_offset + h->state_bytes > size) return 0;
    if (h->stream_offset < 0 || h->stream_bytes < 0 || h->stream_offset + h->stream_bytes > size) return 0;
    if (h->checkpoints_offset < 0 || h->checkpoints_offset + stride * h->checkpoint_count > size) return 0;
    return h->game_name[63] == 0;
}

i64 bmf_replay_input_offset(const u8* file, i64 size, i64 tick) {
    bmf_replay_header h;
    if (size < (i64)sizeof h) return -1;
    bmf_memcpy(&h, file, sizeof h);
    if (!replay_header_valid(&h, size) || tick < h.start_tick) return -1;
    const u8* stream = file + h.stream_offset;
    i64 current = h.start_tick;
    for (i64 at = 0; at < h.stream_bytes;) {
        u8 tag = stream[at];
        if (tag != REPLAY_TAG_STATE && current == tick) return h.stream_offset + at + 1;
        i64 n = replay_record_size(stream, h.stream_bytes, at);
        if (n < 0) return -1;
        if (tag != REPLAY_TAG_STATE) current++;
        at += n;
    }
    return -1;
}

// ---------------------------------------------------------------- events
static char replay_line[64 * 1024];
static char replay_value[16 * 1024];

static void replay_put_hash(bmf_json_writer* w, const char* key, u64 hash) {
    char hex[20];
    bmf_fmt_buf(hex, (i32)sizeof hex, "%016lx", hash);
    bmf_json_key(w, key);
    bmf_json_string(w, hex);
}

// ---------------------------------------------------------------- hashing the state
static void replay_field_hashes(const bmf_reflect* r, u64* out) {
    for (i32 f = 0; f < r->field_count; f++) {
        const bmf_field* fd = &r->fields[f];
        out[f] = bmf_hash(r->state + fd->offset, (i64)bmf_field_type_size(fd->type) * fd->count, 0);
    }
}

// ---------------------------------------------------------------- recorders
typedef struct replay_recorder {
    b32 active;
    const char* broken;           // why the recording cannot be written, or 0
    b32 reserved;
    bmf_arena state;              // the RLE-coded start state
    bmf_arena stream;             // records
    bmf_arena checkpoints;
    bmf_arena last_state;         // session only: the state at the last checkpoint (crash dumps)
    i64 last_state_tick;
    u64 last_state_hash;
    bmf_replay_header header;
    bmf_input previous;
    i64 inputs;                   // input records so far (the file covers start_tick .. start_tick + inputs)
    i64 next_checkpoint_tick;
    i64 last_checkpoint_tick;
    char path[BMF_PLATFORM_PATH_MAX];
    const char* name;
} replay_recorder;

static replay_recorder replay_session = { .name = "replay.session" };
static replay_recorder replay_explicit = { .name = "replay.record" };
static void replay_recorders_restart(const char* broken);

static b32 replay_reserve(bmf_arena* a, const char* name, i64 size) {
    size = (size + BMF_PAGE_GRANULE - 1) / BMF_PAGE_GRANULE * BMF_PAGE_GRANULE;
    u8* base = (u8*)bmf_platform_reserve(size + BMF_PAGE_GRANULE);   // plus a guard granule
    if (!base) return 0;
    bmf_arena_init(a, name, base, size);
    return 1;
}

// n bytes at the end of the arena, or 0 when it is full (recording then breaks, it never panics).
static u8* replay_push(bmf_arena* a, i64 n) {
    if (a->reserved - a->used < n) return 0;
    return (u8*)bmf_arena_push_aligned(a, n, 1);
}

static void replay_checkpoint(replay_recorder* rec) {
    bmf_reflect r = bmf_core_reflect();
    i64 tick = bmf_core_ctx()->tick;
    u8* p = replay_push(&rec->checkpoints, 16 + 8 * (i64)r.field_count);
    if (!p) { rec->broken = "out of checkpoint memory"; return; }
    u64 hash = bmf_core_state_hash();
    bmf_memcpy(p, &tick, 8);
    bmf_memcpy(p + 8, &hash, 8);
    replay_field_hashes(&r, (u64*)(void*)(p + 16));
    rec->header.checkpoint_count++;
    rec->last_checkpoint_tick = tick;
    if (rec->last_state.reserved) {
        // The hash above passed (debug builds assert on NaN/Inf there): a good state for crash dumps.
        bmf_arena_reset(&rec->last_state);
        i32 size = bmf_core_game()->state_size;
        bmf_memcpy(bmf_arena_push_aligned(&rec->last_state, size, 64), bmf_core_state(), size);
        rec->last_state_tick = tick;
        rec->last_state_hash = hash;
    }
}

static void replay_recorder_begin(replay_recorder* rec) {
    i64 state_reserve = BMF_CORE_MAX_STATE_SIZE;   // address space only: any game, any reload
    if (!rec->reserved) {
        rec->reserved = replay_reserve(&rec->state, rec->name, bmf_replay_rle_bound(state_reserve) + 64) &&
                        replay_reserve(&rec->stream, rec->name, BMF_REPLAY_STREAM_RESERVE) &&
                        replay_reserve(&rec->checkpoints, rec->name, BMF_REPLAY_CHECKPOINT_RESERVE) &&
                        (rec != &replay_session || replay_reserve(&rec->last_state, rec->name, state_reserve + 64));
    }
    rec->active = 1;
    rec->broken = rec->reserved ? 0 : "address space reservation failed";
    if (rec->broken) return;
    bmf_arena_reset(&rec->state);
    bmf_arena_reset(&rec->stream);
    bmf_arena_reset(&rec->checkpoints);
    const bmf_game_desc* game = bmf_core_game();
    const bmf_ctx* cx = bmf_core_ctx();
    bmf_replay_header* h = &rec->header;
    bmf_memset(h, 0, sizeof *h);
    bmf_memcpy(h->magic, "BMFR", 4);
    h->version = BMF_REPLAY_VERSION;
    h->header_size = sizeof *h;
    h->state_size = game->state_size;
    h->build_id = bmf_core_build_id();
    h->seed = cx->seed;
    h->state_layout_hash = game->state_layout_hash;
    h->start_tick = cx->tick;
    h->tick_rate = cx->tick_rate;
    h->field_count = game->field_count;
    h->checkpoint_interval = BMF_REPLAY_CHECKPOINT_INTERVAL;
    i32 name_length = bmf_cstr_len(game->name);
    bmf_memcpy(h->game_name, game->name, name_length < 63 ? name_length : 63);
    i64 bound = bmf_replay_rle_bound(game->state_size);
    u8* encoded = replay_push(&rec->state, bound);
    i64 mark = bmf_arena_mark(&rec->state) - bound;
    h->state_bytes = bmf_replay_rle_encode((const u8*)bmf_core_state(), game->state_size, encoded);
    bmf_arena_pop_to(&rec->state, mark + h->state_bytes);
    bmf_memset(&rec->previous, 0, sizeof rec->previous);
    rec->inputs = 0;
    replay_checkpoint(rec);
    rec->next_checkpoint_tick = h->start_tick + BMF_REPLAY_CHECKPOINT_INTERVAL;
}

static void replay_recorder_input(replay_recorder* rec, const bmf_input* in) {
    if (!rec->active || rec->broken) return;
    // XOR in 64-bit words (bmf_input is 224 bytes = 28 words): this runs every tick in debug.
    enum { WORDS = (i32)(sizeof(bmf_input) / 8) };
    BMF_STATIC_ASSERT(sizeof(bmf_input) % 8 == 0, "bmf_input is a whole number of u64 words");
    u64 now[WORDS], before[WORDS], delta[WORDS];
    bmf_memcpy(now, in, sizeof now);
    bmf_memcpy(before, &rec->previous, sizeof before);
    u64 any = 0;
    for (i32 i = 0; i < WORDS; i++) {
        delta[i] = now[i] ^ before[i];
        any |= delta[i];
    }
    b32 same = any == 0;
    rec->previous = *in;
    rec->inputs++;
    if (same) {
        u8* p = replay_push(&rec->stream, 1);
        if (!p) { rec->broken = "out of record memory"; return; }
        p[0] = REPLAY_TAG_INPUT_SAME;
        return;
    }
    i64 bound = 1 + bmf_replay_rle_bound((i64)sizeof delta);
    u8* p = replay_push(&rec->stream, bound);
    if (!p) { rec->broken = "out of record memory"; return; }
    p[0] = REPLAY_TAG_INPUT;
    i64 n = bmf_replay_rle_encode((const u8*)delta, (i64)sizeof delta, p + 1);
    bmf_arena_pop_to(&rec->stream, bmf_arena_mark(&rec->stream) - bound + 1 + n);
}

static void replay_recorder_state_write(replay_recorder* rec, i32 offset, i32 size) {
    if (!rec->active || rec->broken) return;
    i64 bound = 9 + bmf_replay_rle_bound(size);
    u8* p = replay_push(&rec->stream, bound);
    if (!p) { rec->broken = "out of record memory"; return; }
    p[0] = REPLAY_TAG_STATE;
    u32 o = (u32)offset, s = (u32)size;
    bmf_memcpy(p + 1, &o, 4);
    bmf_memcpy(p + 5, &s, 4);
    i64 n = bmf_replay_rle_encode((const u8*)bmf_core_state() + offset, size, p + 9);
    bmf_arena_pop_to(&rec->stream, bmf_arena_mark(&rec->stream) - bound + 9 + n);
}

static void replay_recorder_after_tick(replay_recorder* rec) {
    if (!rec->active || rec->broken) return;
    if (bmf_core_ctx()->tick < rec->next_checkpoint_tick) return;
    replay_checkpoint(rec);
    rec->next_checkpoint_tick += BMF_REPLAY_CHECKPOINT_INTERVAL;
}

// Writes the recording as one .bmfr, atomically, from its parts (no assembled copy). With
// `final_checkpoint` a checkpoint of the current state is added first (unless one was just
// taken); crash dumps pass 0: the state may be half-way through a tick. The file covers
// start_tick .. start_tick + inputs: after a crash inside a tick that includes the crashing tick's
// input, so replaying it reproduces the crash.
static bmf_err replay_recorder_write(replay_recorder* rec, const char* path, b32 final_checkpoint, i64* out_bytes, char* error, i32 error_capacity) {
    if (!rec->active || rec->broken) {
        bmf_fmt_buf(error, error_capacity, rec->active ? "the recording broke: %s" : "not recording", rec->broken);
        return BMF_ERR_INVALID;
    }
    if (final_checkpoint && rec->last_checkpoint_tick != bmf_core_ctx()->tick) replay_checkpoint(rec);
    if (rec->broken) {
        bmf_fmt_buf(error, error_capacity, "the recording broke: %s", rec->broken);
        return BMF_ERR_INVALID;
    }
    bmf_replay_header h = rec->header;
    h.end_tick = h.start_tick + rec->inputs;
    h.state_offset = (i64)sizeof h;
    h.stream_offset = h.state_offset + h.state_bytes;
    h.stream_bytes = rec->stream.used;
    h.checkpoints_offset = h.stream_offset + h.stream_bytes;
    const void* parts[4] = { &h, rec->state.base, rec->stream.base, rec->checkpoints.base };
    i64 sizes[4] = { (i64)sizeof h, h.state_bytes, h.stream_bytes, rec->checkpoints.used };
    bmf_err err = bmf_platform_file_write_atomic_parts(path, parts, sizes, 4);
    if (err != BMF_OK) {
        bmf_fmt_buf(error, error_capacity, "cannot write '%s' (folder missing, or the file is locked)", path);
        return BMF_ERR_IO;
    }
    *out_bytes = h.checkpoints_offset + rec->checkpoints.used;
    return BMF_OK;
}

// ---------------------------------------------------------------- player
static struct {
    b32 active;
    b32 verify;
    b32 mismatch;
    b32 mapped;
    bmf_file_map map;
    bmf_replay_header header;
    const u8* stream;
    i64 at;
    const u8* checkpoints;
    i64 checkpoint_stride;
    i32 next_checkpoint;
    i32 checkpoints_checked;
    bmf_input previous;
    b32 reserved;
    bmf_arena snapshot_arena;
    u8* snapshot;
    i64 last_match_tick;   // -1: none yet
} replay_player;

static void replay_player_close(void) {
    if (replay_player.mapped) bmf_platform_file_unmap(&replay_player.map);
    replay_player.mapped = 0;
    replay_player.active = 0;
}

static void replay_event_end(void) {
    bmf_json_writer w;
    bmf_json_init(&w, replay_line, (i32)sizeof replay_line);
    bmf_json_object_begin(&w);
    bmf_json_key(&w, "event"); bmf_json_string(&w, "replay");
    if (replay_player.verify && !replay_player.mismatch) {
        bmf_json_key(&w, "verify"); bmf_json_string(&w, "ok");
        bmf_json_key(&w, "ticks"); bmf_json_i64(&w, bmf_core_ctx()->tick - replay_player.header.start_tick);
        bmf_json_key(&w, "checkpoints"); bmf_json_i64(&w, replay_player.checkpoints_checked);
        replay_put_hash(&w, "hash", bmf_core_state_hash());
    } else {
        bmf_json_key(&w, "state"); bmf_json_string(&w, "end");
        bmf_json_key(&w, "tick"); bmf_json_i64(&w, bmf_core_ctx()->tick);
    }
    bmf_json_object_end(&w);
    bmf_protocol_write_event(replay_line, w.len);
}

// The mismatch report: fields whose hashes differ, with the replayed value and the value in the
// snapshot of the last matching checkpoint.
static void replay_report_mismatch(const u8* checkpoint, u64 actual) {
    i64 tick;
    u64 expected;
    bmf_memcpy(&tick, checkpoint, 8);
    bmf_memcpy(&expected, checkpoint + 8, 8);
    bmf_reflect r = bmf_core_reflect();
    bmf_reflect at_match = r;
    at_match.state = replay_player.snapshot;
    static u64 hashes[BMF_FIELD_MAX_COUNT];
    replay_field_hashes(&r, hashes);
    bmf_json_writer w;
    bmf_json_init(&w, replay_line, (i32)sizeof replay_line);
    bmf_json_object_begin(&w);
    bmf_json_key(&w, "event"); bmf_json_string(&w, "replay");
    bmf_json_key(&w, "verify"); bmf_json_string(&w, "mismatch");
    bmf_json_key(&w, "tick"); bmf_json_i64(&w, tick);
    bmf_json_key(&w, "last_match"); bmf_json_i64(&w, replay_player.last_match_tick);
    replay_put_hash(&w, "expected", expected);
    replay_put_hash(&w, "actual", actual);
    bmf_json_key(&w, "fields");
    bmf_json_array_begin(&w);
    i32 listed = 0;
    for (i32 f = 0; f < r.field_count && listed < BMF_REPLAY_MAX_DIFF_FIELDS; f++) {
        u64 recorded;
        bmf_memcpy(&recorded, checkpoint + 16 + 8 * (i64)f, 8);
        if (recorded == hashes[f]) continue;
        listed++;
        bmf_str path = bmf_str_from_cstr(r.fields[f].name);
        char detail[64];
        bmf_json_object_begin(&w);
        bmf_json_key(&w, "path"); bmf_json_str(&w, path);
        bmf_json_writer value;
        bmf_json_init(&value, replay_value, (i32)sizeof replay_value);
        bmf_reflect_get(&r, path, &value, detail, (i32)sizeof detail);
        if (!value.overflow) { bmf_json_key(&w, "value"); bmf_json_raw(&w, replay_value); }
        if (replay_player.last_match_tick >= 0) {
            bmf_json_init(&value, replay_value, (i32)sizeof replay_value);
            bmf_reflect_get(&at_match, path, &value, detail, (i32)sizeof detail);
            if (!value.overflow) { bmf_json_key(&w, "at_last_match"); bmf_json_raw(&w, replay_value); }
        }
        bmf_json_object_end(&w);
    }
    bmf_json_array_end(&w);
    if (listed == 0) { bmf_json_key(&w, "note"); bmf_json_string(&w, "no reflected field differs: the difference is in unreflected bytes"); }
    bmf_json_object_end(&w);
    bmf_protocol_write_event(replay_line, w.len);
}

// Checks the checkpoint for the current tick, if there is one.
static void replay_player_check(void) {
    i64 now = bmf_core_ctx()->tick;
    while (replay_player.next_checkpoint < replay_player.header.checkpoint_count) {
        const u8* cp = replay_player.checkpoints + replay_player.checkpoint_stride * replay_player.next_checkpoint;
        i64 tick;
        bmf_memcpy(&tick, cp, 8);
        if (tick > now) return;
        replay_player.next_checkpoint++;
        if (tick < now) continue;
        if (!replay_player.verify) return;
        u64 expected, actual = bmf_core_state_hash();
        bmf_memcpy(&expected, cp + 8, 8);
        replay_player.checkpoints_checked++;
        if (expected == actual) {
            bmf_memcpy(replay_player.snapshot, bmf_core_state(), replay_player.header.state_size);
            replay_player.last_match_tick = now;
            return;
        }
        replay_player.mismatch = 1;
        replay_report_mismatch(cp, actual);
        replay_player_close();   // stop at the first mismatch
        return;
    }
}

// Applies state records up to the next input record; at the end of the stream, ends the replay.
static void replay_player_apply_state_records(void) {
    const u8* s = replay_player.stream;
    i64 size = replay_player.header.stream_bytes;
    while (replay_player.at < size && s[replay_player.at] == REPLAY_TAG_STATE) {
        i64 n = replay_record_size(s, size, replay_player.at);
        u32 offset, length;
        bmf_memcpy(&offset, s + replay_player.at + 1, 4);
        bmf_memcpy(&length, s + replay_player.at + 5, 4);
        if (n < 0 || (u64)offset + length > (u64)replay_player.header.state_size) { replay_player.at = -1; return; }
        bmf_replay_rle_decode(s + replay_player.at + 9, n - 9, (u8*)bmf_core_state() + offset, length);
        bmf_replay_note_state_write((i32)offset, (i32)length);
        replay_player.at += n;
    }
}

// 1 when an input record follows (state records before it are skipped, not applied: they apply
// just before that input, i.e. after anything the caller does between the ticks).
static b32 replay_player_input_ahead(void) {
    const u8* s = replay_player.stream;
    i64 size = replay_player.header.stream_bytes;
    for (i64 at = replay_player.at; at >= 0 && at < size;) {
        if (s[at] != REPLAY_TAG_STATE) return 1;
        i64 n = replay_record_size(s, size, at);
        if (n < 0) return 1;   // malformed: next_input reports it
        at += n;
    }
    return 0;
}

static void replay_player_finish_if_done(void) {
    if (!replay_player.active) return;
    if (replay_player.at >= 0 && replay_player_input_ahead()) return;
    replay_player_apply_state_records();   // trailing state writes, then the end
    if (replay_player.at < 0) {
        bmf_core_print_error("replay_corrupt", "the replay's record stream is malformed");
        replay_player.mismatch = 1;
        replay_player_close();
        return;
    }
    if (replay_player.at < replay_player.header.stream_bytes) return;
    replay_event_end();
    replay_player_close();
}

bmf_err bmf_replay_peek(const char* path, bmf_replay_header* out, char* error, i32 error_capacity) {
    bmf_file_map map;
    bmf_err err = bmf_platform_file_map(path, &map);
    if (err != BMF_OK) {
        bmf_fmt_buf(error, error_capacity, "cannot read '%s'", path);
        return err == BMF_ERR_NOT_FOUND ? BMF_ERR_NOT_FOUND : BMF_ERR_INVALID;
    }
    b32 valid = map.size >= (i64)sizeof *out;
    if (valid) {
        bmf_memcpy(out, map.data, sizeof *out);
        valid = replay_header_valid(out, map.size);
    }
    bmf_platform_file_unmap(&map);
    if (!valid) {
        bmf_fmt_buf(error, error_capacity, "'%s' is not a version %i .bmfr replay", path, (i32)BMF_REPLAY_VERSION);
        return BMF_ERR_INVALID;
    }
    return BMF_OK;
}

bmf_err bmf_replay_play_begin(const char* path, b32 verify, char* error, i32 error_capacity) {
    replay_player_close();
    bmf_replay_header h;
    bmf_err err = bmf_replay_peek(path, &h, error, error_capacity);
    if (err != BMF_OK) return err;
    const bmf_game_desc* game = bmf_core_game();
    const char* problem = !bmf_cstr_eq(h.game_name, game->name) ? "game name"
                        : h.state_size != game->state_size ? "state size"
                        : h.state_layout_hash != game->state_layout_hash ? "state layout hash"
                        : h.field_count != game->field_count ? "field count"
                        : h.tick_rate != bmf_core_ctx()->tick_rate ? "tick rate" : 0;
    if (problem) {
        bmf_fmt_buf(error, error_capacity, "'%s' was recorded with another %s (game \"%s\", %i Hz)", path, problem, h.game_name, h.tick_rate);
        return BMF_ERR_INVALID;
    }
    if (!replay_player.reserved) {
        replay_player.reserved = replay_reserve(&replay_player.snapshot_arena, "replay.snapshot", (i64)BMF_CORE_MAX_STATE_SIZE + 64);
        if (!replay_player.reserved) { bmf_fmt_buf(error, error_capacity, "address space reservation failed"); return BMF_ERR_INVALID; }
    }
    bmf_arena_reset(&replay_player.snapshot_arena);
    replay_player.snapshot = (u8*)bmf_arena_push_aligned(&replay_player.snapshot_arena, h.state_size, 64);
    if (bmf_platform_file_map(path, &replay_player.map) != BMF_OK) { bmf_fmt_buf(error, error_capacity, "cannot read '%s'", path); return BMF_ERR_NOT_FOUND; }
    replay_player.mapped = 1;
    const u8* file = (const u8*)replay_player.map.data;
    if (bmf_replay_rle_decode(file + h.state_offset, h.state_bytes, replay_player.snapshot, h.state_size) < 0) {
        replay_player_close();
        bmf_fmt_buf(error, error_capacity, "'%s': the start state is malformed", path);
        return BMF_ERR_INVALID;
    }
    if (h.build_id != bmf_core_build_id()) {
        bmf_json_writer w;
        bmf_json_init(&w, replay_line, (i32)sizeof replay_line);
        bmf_json_object_begin(&w);
        bmf_json_key(&w, "event"); bmf_json_string(&w, "replay");
        bmf_json_key(&w, "warning"); bmf_json_string(&w, "build_id");
        replay_put_hash(&w, "file", h.build_id);
        replay_put_hash(&w, "running", bmf_core_build_id());
        bmf_json_object_end(&w);
        bmf_protocol_write_event(replay_line, w.len);
    }
    replay_player.header = h;
    replay_player.verify = verify;
    replay_player.mismatch = 0;
    replay_player.stream = file + h.stream_offset;
    replay_player.at = 0;
    replay_player.checkpoints = file + h.checkpoints_offset;
    replay_player.checkpoint_stride = 16 + 8 * (i64)h.field_count;
    replay_player.next_checkpoint = 0;
    replay_player.checkpoints_checked = 0;
    replay_player.last_match_tick = -1;
    bmf_memset(&replay_player.previous, 0, sizeof replay_player.previous);
    bmf_core_load_state(replay_player.snapshot, h.start_tick);
    replay_recorders_restart("a replay was loaded during the recording");
    replay_player.active = 1;
    replay_player_check();               // the start checkpoint
    replay_player_finish_if_done();      // an empty recording ends at once
    return BMF_OK;
}

b32 bmf_replay_playing(void) { return replay_player.active; }
b32 bmf_replay_verified(void) { return !replay_player.mismatch; }

b32 bmf_replay_next_input(bmf_input* out) {
    if (!replay_player.active) return 0;
    replay_player_apply_state_records();
    const u8* s = replay_player.stream;
    i64 size = replay_player.header.stream_bytes;
    if (replay_player.at < 0 || replay_player.at >= size) { replay_player_finish_if_done(); return 0; }
    u8 tag = s[replay_player.at];
    if (tag == REPLAY_TAG_INPUT_SAME) {
        replay_player.at++;
    } else {
        u8 delta[sizeof(bmf_input)];
        i64 used = tag == REPLAY_TAG_INPUT ? bmf_replay_rle_decode(s + replay_player.at + 1, size - replay_player.at - 1, delta, (i64)sizeof delta) : -1;
        if (used < 0) {
            replay_player.at = -1;
            replay_player_finish_if_done();
            return 0;
        }
        replay_player.at += 1 + used;
        u8* previous = (u8*)&replay_player.previous;
        for (i32 i = 0; i < (i32)sizeof delta; i++) previous[i] ^= delta[i];
    }
    *out = replay_player.previous;
    return 1;
}

// ---------------------------------------------------------------- hooks
void bmf_replay_session_begin(void) { replay_recorder_begin(&replay_session); }

void bmf_replay_record_input(const bmf_input* in) {
    replay_recorder_input(&replay_session, in);
    replay_recorder_input(&replay_explicit, in);
}

void bmf_replay_after_tick(void) {
    replay_recorder_after_tick(&replay_session);
    replay_recorder_after_tick(&replay_explicit);
    if (replay_player.active) {
        replay_player_check();
        replay_player_finish_if_done();
    }
}

void bmf_replay_note_state_write(i32 offset, i32 size) {
    replay_recorder_state_write(&replay_session, offset, size);
    replay_recorder_state_write(&replay_explicit, offset, size);
}

// The state block and tick were replaced between ticks (the tick may jump): recordings cannot
// carry that (input record k is for tick start_tick + k), so the session recording starts again
// from here and an explicit recording breaks.
static void replay_recorders_restart(const char* broken) {
    if (replay_explicit.active && !replay_explicit.broken) replay_explicit.broken = broken;
    if (replay_session.active) replay_recorder_begin(&replay_session);
}

void bmf_replay_on_state_replaced(const char* reason, const char* broken) {
    if (replay_player.active) {
        bmf_json_writer w;
        bmf_json_init(&w, replay_line, (i32)sizeof replay_line);
        bmf_json_object_begin(&w);
        bmf_json_key(&w, "event"); bmf_json_string(&w, "replay");
        bmf_json_key(&w, "state"); bmf_json_string(&w, "stopped");
        bmf_json_key(&w, "reason"); bmf_json_string(&w, reason);
        bmf_json_object_end(&w);
        bmf_protocol_write_event(replay_line, w.len);
        replay_player_close();
    }
    replay_recorders_restart(broken);
}

// ---------------------------------------------------------------- protocol entry points
bmf_err bmf_replay_record_start(const char* path, char* error, i32 error_capacity) {
    if (replay_explicit.active) {
        bmf_fmt_buf(error, error_capacity, "already recording to '%s' (record stop first)", replay_explicit.path);
        return BMF_ERR_INVALID;
    }
    i32 length = bmf_cstr_len(path);
    if (length >= (i32)sizeof replay_explicit.path) { bmf_fmt_buf(error, error_capacity, "path too long"); return BMF_ERR_INVALID; }
    bmf_memcpy(replay_explicit.path, path, length + 1);
    replay_recorder_begin(&replay_explicit);
    return BMF_OK;
}

bmf_err bmf_replay_record_stop(i64* ticks, i64* bytes, i32* checkpoints, char* error, i32 error_capacity) {
    if (!replay_explicit.active) { bmf_fmt_buf(error, error_capacity, "not recording (record start PATH first)"); return BMF_ERR_INVALID; }
    bmf_err err = replay_recorder_write(&replay_explicit, replay_explicit.path, 1, bytes, error, error_capacity);
    *ticks = bmf_core_ctx()->tick - replay_explicit.header.start_tick;
    *checkpoints = replay_explicit.header.checkpoint_count;
    replay_explicit.active = 0;
    return err;
}

bmf_err bmf_replay_write_session(const char* path, char* error, i32 error_capacity) {
    i64 bytes = 0;
    return replay_recorder_write(&replay_session, path, 1, &bytes, error, error_capacity);
}

bmf_err bmf_replay_write_crash(const char* path, char* error, i32 error_capacity) {
    i64 bytes = 0;
    return replay_recorder_write(&replay_session, path, 0, &bytes, error, error_capacity);
}

const u8* bmf_replay_session_last_state(i64* out_tick, u64* out_hash) {
    if (!replay_session.active || !replay_session.last_state.reserved || replay_session.header.checkpoint_count == 0) return 0;
    *out_tick = replay_session.last_state_tick;
    *out_hash = replay_session.last_state_hash;
    return replay_session.last_state.base;
}

i32 bmf_replay_arenas(const bmf_arena** out, const char** names, i32 capacity) {
    const bmf_arena* arenas[] = {
        &replay_session.state, &replay_session.stream, &replay_session.checkpoints, &replay_session.last_state,
        &replay_explicit.state, &replay_explicit.stream, &replay_explicit.checkpoints,
        &replay_player.snapshot_arena,
    };
    const char* arena_names[] = {
        "replay.session.state", "replay.session.stream", "replay.session.checkpoints", "replay.session.last_state",
        "replay.record.state", "replay.record.stream", "replay.record.checkpoints",
        "replay.player.snapshot",
    };
    i32 count = 0;
    for (i32 i = 0; i < (i32)(sizeof arenas / sizeof arenas[0]) && count < capacity; i++) {
        if (!arenas[i]->reserved) continue;
        out[count] = arenas[i];
        names[count] = arena_names[i];
        count++;
    }
    return count;
}
