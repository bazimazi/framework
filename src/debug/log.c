// log.c: the log ring (bmf_log, SPEC §11.5, task 1A-36). Contracts in bmf.h ("Profiling and
// logging") and profile.h, invariants in src/debug/README.md.

typedef struct log_entry {
    i64 tick;                          // ticks completed when logged (-1: no game running)
    i32 level;
    char text[BMF_LOG_TEXT_MAX];
} log_entry;

static struct {
    i32 lock;                          // spin lock over the ring (any thread may log)
    b32 echo;
    i64 written;                       // messages ever stored
    i64 read;                          // messages drained or dropped; written - read <= BMF_LOG_RING
    i64 dropped;                       // since the last drain
    i64 counts[BMF_LOG_LEVEL_COUNT];
} log_state;

static log_entry log_ring[BMF_LOG_RING];
static char log_line[2 * BMF_LOG_TEXT_MAX + 128];   // the echo line (main thread)

static const char* const LOG_LEVEL_NAMES[BMF_LOG_LEVEL_COUNT] = { "debug", "info", "warn", "error" };

const char* bmf_log_level_name(bmf_log_level level) {
    return level >= 0 && level < BMF_LOG_LEVEL_COUNT ? LOG_LEVEL_NAMES[level] : 0;
}

static void log_lock(void) {
    while (!bmf_atomic_cas_i32(&log_state.lock, 0, 1)) {}
}

static void log_unlock(void) { bmf_atomic_store_i32(&log_state.lock, 0); }

void bmf_log_count(bmf_log_level level) {
    if (level < 0 || level >= BMF_LOG_LEVEL_COUNT) return;
    bmf_atomic_add_i64(&log_state.counts[level], 1);
}

void bmf_log_va(bmf_log_level level, const char* fmt, __builtin_va_list args) {
    if (level < 0 || level >= BMF_LOG_LEVEL_COUNT) level = BMF_LOG_ERROR;
    char text[BMF_LOG_TEXT_MAX];
    bmf_fmt_va(text, (i32)sizeof text, fmt, args);
    i64 tick = bmf_core_running() ? bmf_core_ctx()->tick : -1;
    bmf_log_count(level);
    log_lock();
    if (log_state.written - log_state.read == BMF_LOG_RING) {   // full: the oldest goes
        log_state.read++;
        log_state.dropped++;
    }
    log_entry* e = &log_ring[log_state.written % BMF_LOG_RING];
    e->tick = tick;
    e->level = (i32)level;
    bmf_memcpy(e->text, text, sizeof text);
    log_state.written++;
    b32 echo = log_state.echo;
    log_unlock();
    if (echo && bmf_platform_is_main_thread()) {
        bmf_json_writer w;
        bmf_json_init(&w, log_line, (i32)sizeof log_line - 1);
        bmf_json_object_begin(&w);
        bmf_json_key(&w, "event"); bmf_json_string(&w, "log");
        bmf_json_key(&w, "level"); bmf_json_string(&w, LOG_LEVEL_NAMES[level]);
        bmf_json_key(&w, "tick"); bmf_json_i64(&w, tick);
        bmf_json_key(&w, "msg"); bmf_json_string(&w, text);
        bmf_json_object_end(&w);
        log_line[w.len] = '\n';
        bmf_platform_write(BMF_STREAM_STDERR, log_line, w.len + 1);
    }
}

void bmf_log_set_echo(b32 echo) { log_state.echo = echo; }

void bmf_log_drain(bmf_json_writer* w, bmf_log_level min) {
    log_lock();
    bmf_json_key(w, "entries");
    bmf_json_array_begin(w);
    for (i64 i = log_state.read; i < log_state.written; i++) {
        const log_entry* e = &log_ring[i % BMF_LOG_RING];
        if (e->level < (i32)min) continue;
        bmf_json_object_begin(w);
        bmf_json_key(w, "tick"); bmf_json_i64(w, e->tick);
        bmf_json_key(w, "level"); bmf_json_string(w, LOG_LEVEL_NAMES[e->level]);
        bmf_json_key(w, "msg"); bmf_json_string(w, e->text);
        bmf_json_object_end(w);
    }
    bmf_json_array_end(w);
    bmf_json_key(w, "dropped"); bmf_json_i64(w, log_state.dropped);
    bmf_json_key(w, "counts");
    bmf_json_object_begin(w);
    for (i32 l = 0; l < BMF_LOG_LEVEL_COUNT; l++) {
        bmf_json_key(w, LOG_LEVEL_NAMES[l]);
        bmf_json_i64(w, bmf_atomic_load_i64(&log_state.counts[l]));
    }
    bmf_json_object_end(w);
    log_state.read = log_state.written;
    log_state.dropped = 0;
    log_unlock();
}

void bmf_log_clear(void) {
    log_lock();
    log_state.read = log_state.written;
    log_state.dropped = 0;
    for (i32 l = 0; l < BMF_LOG_LEVEL_COUNT; l++) bmf_atomic_store_i64(&log_state.counts[l], 0);
    log_unlock();
}
