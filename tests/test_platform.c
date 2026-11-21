// Tests for src/platform/platform.h on win32 (task 1A-11): command-line splitting, file
// round trips, atomic write, file-change polling, threads with atomics, futex semaphore, FPU state
// in spawned threads, time and sleep, buffered stdio, DLL loading.
// Temporary files go next to test.exe and are deleted by the test that made them.

// Full path "<exe dir><name>" into out.
static void platform_test_path(char* out, i32 capacity, const char* name) {
    i32 n = bmf_platform_exe_dir(out, capacity);
    bmf_fmt_buf(out + n, capacity - n, "%s", name);
}

// ---------------------------------------------------------------- command line
static void test_platform_command_line_split(bmf_test* t) {
    // Expected results follow the Universal CRT (the same rules as CommandLineToArgvW for
    // arguments after the first). Each expected list is '|'-separated.
    static const struct { const WCHAR* line; const char* expected; } cases[] = {
        { u"prog.exe", "prog.exe" },
        { u"prog.exe a b  c", "prog.exe|a|b|c" },
        { u"prog.exe\ta\t\tb   ", "prog.exe|a|b" },
        { u"\"C:\\Program Files\\x.exe\" --flag", "C:\\Program Files\\x.exe|--flag" },
        { u"C:\\dir\\x.exe \"a b\" c", "C:\\dir\\x.exe|a b|c" },
        { u"p \"a\\\"b\"", "p|a\"b" },                       // \" inside quotes: literal quote
        { u"p a\\\\\"b c\"", "p|a\\b c" },                   // 2 backslashes + quote: 1 backslash, quote opens
        { u"p a\\\\\\\"b", "p|a\\\"b" },                     // 3 backslashes + quote: 1 backslash + literal quote
        { u"p a\\b\\\\c", "p|a\\b\\\\c" },                   // backslashes without a quote stay literal
        { u"p \"a\"\"b\"", "p|a\"b" },                       // "" inside quotes: literal quote
        { u"p \"\" x", "p||x" },                             // empty quoted argument
        { u"p \"ab", "p|ab" },                               // unterminated quote runs to the end
        { u"p a\"b c\"d", "p|ab cd" },                       // quotes in the middle of an argument
        { u"p \x00e9\x4e2d \xd83d\xde00", "p|\xc3\xa9\xe4\xb8\xad|\xf0\x9f\x98\x80" },   // UTF-16 to UTF-8, surrogate pair
        { u"p \xd800x", "p|\xef\xbf\xbdx" },                 // unpaired surrogate becomes U+FFFD
    };
    for (i32 c = 0; c < (i32)(sizeof cases / sizeof cases[0]); c++) {
        char bytes[256];
        const char* argv[16];
        i32 count = bmf_win32_split_command_line(cases[c].line, bytes, (i32)sizeof bytes, argv, 16);
        char joined[256];
        i32 len = 0;
        for (i32 i = 0; i < count; i++) len += bmf_fmt_buf(joined + len, (i32)sizeof joined - len, i ? "|%s" : "%s", argv[i]);
        joined[len] = 0;
        char message[512];
        bmf_fmt_buf(message, (i32)sizeof message, "case %i: got [%s], expected [%s]", c, joined, cases[c].expected);
        bmf_check(t, count > 0 && bmf_cstr_eq(joined, cases[c].expected), __FILE__, __LINE__, message);
    }
    // Capacity overflow reports -1 instead of writing out of bounds.
    char small[8];
    const char* argv[4];
    bmf_check_eq_i64(t, bmf_win32_split_command_line(u"p abcdefghij", small, 8, argv, 4), -1, __FILE__, __LINE__, "bytes overflow");
    char bytes[64];
    bmf_check_eq_i64(t, bmf_win32_split_command_line(u"p a b c d e", bytes, 64, argv, 4), -1, __FILE__, __LINE__, "argv overflow");

    // The runner's own arguments came through the same function.
    bmf_platform_args args = bmf_platform_get_args();
    bmf_check(t, args.count >= 1 && bmf_str_ends_with(bmf_str_from_cstr(args.values[0]), bmf_str_from_cstr("test.exe")),
              __FILE__, __LINE__, "argv[0] is test.exe");
}

// ---------------------------------------------------------------- files
static void test_platform_file_roundtrip(bmf_test* t) {
    char path[1024];
    platform_test_path(path, (i32)sizeof path, "platform_roundtrip.tmp");
    u8 data[100000];
    for (i32 i = 0; i < (i32)sizeof data; i++) data[i] = (u8)(i * 31 + (i >> 8));
    bmf_check_eq_i64(t, bmf_platform_file_write_atomic(path, data, sizeof data), BMF_OK, __FILE__, __LINE__, "write");

    bmf_file_info info;
    bmf_check_eq_i64(t, bmf_platform_file_info(path, &info), BMF_OK, __FILE__, __LINE__, "info");
    bmf_check_eq_i64(t, info.size, (i64)sizeof data, __FILE__, __LINE__, "info size");
    bmf_check(t, info.modified != 0, __FILE__, __LINE__, "info modified time");

    bmf_arena arena = memory_test_arena(1 << 20);
    bmf_str read;
    bmf_check_eq_i64(t, bmf_platform_file_read(path, &arena, &read), BMF_OK, __FILE__, __LINE__, "read");
    bmf_check_eq_i64(t, read.len, (i64)sizeof data, __FILE__, __LINE__, "read size");
    bmf_check_eq_mem(t, read.ptr, data, sizeof data, __FILE__, __LINE__, "read content");
    bmf_check_eq_i64(t, read.ptr[read.len], 0, __FILE__, __LINE__, "read adds a NUL");
    bmf_check_eq_u64(t, (u64)read.ptr & 15, 0, __FILE__, __LINE__, "read is 16-byte aligned");

    bmf_file_map map;
    bmf_check_eq_i64(t, bmf_platform_file_map(path, &map), BMF_OK, __FILE__, __LINE__, "map");
    bmf_check_eq_i64(t, map.size, (i64)sizeof data, __FILE__, __LINE__, "map size");
    bmf_check_eq_mem(t, map.data, data, sizeof data, __FILE__, __LINE__, "map content");
    bmf_platform_file_unmap(&map);
    bmf_check(t, map.data == 0 && map.size == 0, __FILE__, __LINE__, "unmap clears");

    // Empty file: read gives an empty string, map gives no data.
    bmf_check_eq_i64(t, bmf_platform_file_write_atomic(path, 0, 0), BMF_OK, __FILE__, __LINE__, "write empty");
    bmf_check_eq_i64(t, bmf_platform_file_read(path, &arena, &read), BMF_OK, __FILE__, __LINE__, "read empty");
    bmf_check_eq_i64(t, read.len, 0, __FILE__, __LINE__, "read empty size");
    bmf_check_eq_i64(t, bmf_platform_file_map(path, &map), BMF_OK, __FILE__, __LINE__, "map empty");
    bmf_check(t, map.data == 0 && map.size == 0, __FILE__, __LINE__, "map empty is null");
    bmf_platform_file_unmap(&map);

    // Missing file: every call reports NOT_FOUND and allocates nothing.
    bmf_check_eq_i64(t, bmf_platform_file_delete(path), BMF_OK, __FILE__, __LINE__, "delete");
    i64 mark = bmf_arena_mark(&arena);
    bmf_check_eq_i64(t, bmf_platform_file_read(path, &arena, &read), BMF_ERR_NOT_FOUND, __FILE__, __LINE__, "read missing");
    bmf_check_eq_i64(t, bmf_arena_mark(&arena), mark, __FILE__, __LINE__, "failed read frees");
    bmf_check_eq_i64(t, bmf_platform_file_map(path, &map), BMF_ERR_NOT_FOUND, __FILE__, __LINE__, "map missing");
    bmf_check_eq_i64(t, bmf_platform_file_info(path, &info), BMF_ERR_NOT_FOUND, __FILE__, __LINE__, "info missing");
    bmf_check_eq_i64(t, info.size, -1, __FILE__, __LINE__, "info missing size");
    bmf_check_eq_i64(t, bmf_platform_file_delete(path), BMF_ERR_NOT_FOUND, __FILE__, __LINE__, "delete missing");
    char no_dir[1024];
    platform_test_path(no_dir, (i32)sizeof no_dir, "no_such_folder/x.tmp");
    bmf_check_eq_i64(t, bmf_platform_file_write_atomic(no_dir, data, 10), BMF_ERR_NOT_FOUND, __FILE__, __LINE__, "write into missing folder");

    // Non-ASCII file names go through UTF-16.
    char unicode[1024];
    platform_test_path(unicode, (i32)sizeof unicode, "platform_\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80.tmp");
    bmf_check_eq_i64(t, bmf_platform_file_write_atomic(unicode, "x", 1), BMF_OK, __FILE__, __LINE__, "write unicode name");
    bmf_check_eq_i64(t, bmf_platform_file_info(unicode, &info), BMF_OK, __FILE__, __LINE__, "info unicode name");
    bmf_check_eq_i64(t, bmf_platform_file_delete(unicode), BMF_OK, __FILE__, __LINE__, "delete unicode name");
}

// Counts files matching "<exe dir>platform_atomic.tmp.*.tmp" (leftover temp files).
static i32 platform_count_temp_files(void) {
    char pattern[1024];
    platform_test_path(pattern, (i32)sizeof pattern, "platform_atomic.tmp.*.tmp");
    WCHAR wide[1024];
    i32 n = bmf_utf8_to_utf16(bmf_str_from_cstr(pattern), wide, 1023);
    wide[n] = 0;
    WIN32_FIND_DATAW found;
    HANDLE find = FindFirstFileW(wide, &found);
    if (find == WIN32_INVALID_HANDLE_VALUE) return 0;
    i32 count = 1;
    while (FindNextFileW(find, &found)) count++;
    FindClose(find);
    return count;
}

static void test_platform_atomic_write(bmf_test* t) {
    char path[1024];
    platform_test_path(path, (i32)sizeof path, "platform_atomic.tmp");
    bmf_check_eq_i64(t, bmf_platform_file_write_atomic(path, "old contents", 12), BMF_OK, __FILE__, __LINE__, "write old");

    // A reader that has the old file mapped keeps seeing the old bytes; the replace still succeeds.
    bmf_file_map old_map;
    bmf_check_eq_i64(t, bmf_platform_file_map(path, &old_map), BMF_OK, __FILE__, __LINE__, "map old");
    bmf_check_eq_i64(t, bmf_platform_file_write_atomic(path, "new", 3), BMF_OK, __FILE__, __LINE__, "replace while mapped");
    bmf_check_eq_mem(t, old_map.data, "old contents", 12, __FILE__, __LINE__, "old view unchanged");
    bmf_platform_file_unmap(&old_map);

    bmf_arena arena = memory_test_arena(1 << 20);
    bmf_str read;
    bmf_check_eq_i64(t, bmf_platform_file_read(path, &arena, &read), BMF_OK, __FILE__, __LINE__, "read new");
    bmf_check(t, bmf_str_eq(read, bmf_str_from_cstr("new")), __FILE__, __LINE__, "new contents");

    // A destination held open without delete sharing cannot be replaced: IO error, old file intact,
    // temp file removed.
    WCHAR wide[1024];
    i32 n = bmf_utf8_to_utf16(bmf_str_from_cstr(path), wide, 1023);
    wide[n] = 0;
    HANDLE locked = CreateFileW(wide, (u32)WIN32_GENERIC_READ, WIN32_FILE_SHARE_READ, 0, WIN32_OPEN_EXISTING, WIN32_FILE_ATTRIBUTE_NORMAL, 0);
    bmf_check(t, locked != WIN32_INVALID_HANDLE_VALUE, __FILE__, __LINE__, "lock destination");
    bmf_check_eq_i64(t, bmf_platform_file_write_atomic(path, "blocked", 7), BMF_ERR_IO, __FILE__, __LINE__, "replace locked file");
    CloseHandle(locked);
    bmf_check_eq_i64(t, platform_count_temp_files(), 0, __FILE__, __LINE__, "no temp file left after failure");
    bmf_check_eq_i64(t, bmf_platform_file_read(path, &arena, &read), BMF_OK, __FILE__, __LINE__, "read after failed replace");
    bmf_check(t, bmf_str_eq(read, bmf_str_from_cstr("new")), __FILE__, __LINE__, "contents intact after failed replace");

    // Many replaces in a row: always complete contents, no temp files.
    char text[64];
    char failure[128] = "";
    for (i32 i = 0; i < 200 && !failure[0]; i++) {
        i32 len = bmf_fmt_buf(text, (i32)sizeof text, "generation %i with some padding %i", i, i * 7);
        bmf_err err = bmf_platform_file_write_atomic(path, text, len);
        if (err != BMF_OK) { bmf_fmt_buf(failure, (i32)sizeof failure, "replace %i: write error %i (GetLastError %u)", i, (i32)err, GetLastError()); break; }
        bmf_arena_reset(&arena);
        err = bmf_platform_file_read(path, &arena, &read);
        if (err != BMF_OK) bmf_fmt_buf(failure, (i32)sizeof failure, "replace %i: read error %i (GetLastError %u)", i, (i32)err, GetLastError());
        else if (!bmf_str_eq(read, bmf_str_make(text, len))) bmf_fmt_buf(failure, (i32)sizeof failure, "replace %i: contents differ", i);
    }
    bmf_check(t, failure[0] == 0, __FILE__, __LINE__, failure[0] ? failure : "200 replaces read back complete");
    bmf_check_eq_i64(t, platform_count_temp_files(), 0, __FILE__, __LINE__, "no temp files left");
    bmf_platform_file_delete(path);
}

static void test_platform_file_watch(bmf_test* t) {
    char path[1024];
    platform_test_path(path, (i32)sizeof path, "platform_watch.tmp");
    bmf_platform_file_delete(path);
    i64 now = 1000000000;   // the watch takes time as input, so the test controls it
    i64 interval = (i64)BMF_FILE_WATCH_INTERVAL_MS * 1000000;
    bmf_file_watch watch;
    bmf_platform_file_watch_init(&watch, path, now);
    bmf_check_eq_i64(t, watch.last.size, -1, __FILE__, __LINE__, "missing file at init");

    bmf_platform_file_write_atomic(path, "1", 1);
    bmf_check_eq_i64(t, bmf_platform_file_watch_poll(&watch, now + interval - 1), 0, __FILE__, __LINE__, "no check inside the interval");
    bmf_check_eq_i64(t, bmf_platform_file_watch_poll(&watch, now + interval), 1, __FILE__, __LINE__, "file appeared");
    bmf_check_eq_i64(t, bmf_platform_file_watch_poll(&watch, now + 2 * interval), 0, __FILE__, __LINE__, "reported once");

    // Same size, new write time: compare against an explicitly older write time so the test does
    // not depend on the file system's timestamp granularity.
    watch.last.modified -= 1;
    bmf_check_eq_i64(t, bmf_platform_file_watch_poll(&watch, now + 3 * interval), 1, __FILE__, __LINE__, "write time change");
    bmf_platform_file_write_atomic(path, "22", 2);
    bmf_check_eq_i64(t, bmf_platform_file_watch_poll(&watch, now + 4 * interval), 1, __FILE__, __LINE__, "size change");
    bmf_platform_file_delete(path);
    bmf_check_eq_i64(t, bmf_platform_file_watch_poll(&watch, now + 5 * interval), 1, __FILE__, __LINE__, "file removed");
    bmf_check_eq_i64(t, bmf_platform_file_watch_poll(&watch, now + 6 * interval), 0, __FILE__, __LINE__, "stays removed");
}

// ---------------------------------------------------------------- threads, atomics, semaphore
typedef struct {
    i32 counter;
    i64 counter64;
    i32 increments_per_thread;
} platform_counter_test;

static void platform_counter_thread(void* user) {
    platform_counter_test* c = (platform_counter_test*)user;
    for (i32 i = 0; i < c->increments_per_thread; i++) {
        bmf_atomic_add_i32(&c->counter, 1);
        i64 seen = bmf_atomic_load_i64(&c->counter64);
        while (!bmf_atomic_cas_i64(&c->counter64, seen, seen + 2)) seen = bmf_atomic_load_i64(&c->counter64);
    }
}

static void test_platform_threads_atomic_counter(bmf_test* t) {
    platform_counter_test c = { 0, 0, 20000 };
    bmf_thread threads[8];
    for (i32 i = 0; i < 8; i++) threads[i] = bmf_platform_thread_start(platform_counter_thread, &c, 256 * 1024, "bmf test counter");
    for (i32 i = 0; i < 8; i++) bmf_platform_thread_join(&threads[i]);
    bmf_check_eq_i64(t, c.counter, 160000, __FILE__, __LINE__, "fetch_add counter");
    bmf_check_eq_i64(t, c.counter64, 320000, __FILE__, __LINE__, "cas counter");
    bmf_check(t, threads[0].handle == 0, __FILE__, __LINE__, "join clears the handle");
    bmf_check(t, bmf_platform_cpu_count() >= 1, __FILE__, __LINE__, "cpu count");

    // More threads than start slots, started and joined in waves: slots are recycled.
    for (i32 wave = 0; wave < 3; wave++) {
        bmf_thread many[BMF_THREAD_MAX_STARTING];
        platform_counter_test small = { 0, 0, 10 };
        for (i32 i = 0; i < BMF_THREAD_MAX_STARTING; i++) many[i] = bmf_platform_thread_start(platform_counter_thread, &small, 64 * 1024, 0);
        for (i32 i = 0; i < BMF_THREAD_MAX_STARTING; i++) bmf_platform_thread_join(&many[i]);
        bmf_check_eq_i64(t, small.counter, BMF_THREAD_MAX_STARTING * 10, __FILE__, __LINE__, "wave counter");
    }
}

typedef struct {
    bmf_semaphore ping, pong;
    i32 rounds;
    bmf_semaphore items;
    i32 consumed;
    i32 items_per_consumer;
} platform_semaphore_test;

static void platform_pong_thread(void* user) {
    platform_semaphore_test* s = (platform_semaphore_test*)user;
    for (i32 i = 0; i < s->rounds; i++) {
        bmf_semaphore_wait(&s->ping);
        bmf_semaphore_signal(&s->pong, 1);
    }
}

static void platform_consumer_thread(void* user) {
    platform_semaphore_test* s = (platform_semaphore_test*)user;
    for (i32 i = 0; i < s->items_per_consumer; i++) {
        bmf_semaphore_wait(&s->items);
        bmf_atomic_add_i32(&s->consumed, 1);
    }
}

static void test_platform_semaphore(bmf_test* t) {
    platform_semaphore_test s;
    bmf_memset(&s, 0, sizeof s);
    s.rounds = 20000;
    bmf_thread pong = bmf_platform_thread_start(platform_pong_thread, &s, 64 * 1024, "bmf test pong");
    for (i32 i = 0; i < s.rounds; i++) {
        bmf_semaphore_signal(&s.ping, 1);
        bmf_semaphore_wait(&s.pong);
    }
    bmf_platform_thread_join(&pong);
    bmf_check_eq_i64(t, s.ping.count + s.pong.count, 0, __FILE__, __LINE__, "ping-pong balanced");

    // 4 consumers block on an empty semaphore; batches of signals (count > 1 wakes all) feed them.
    s.items_per_consumer = 5000;
    bmf_thread consumers[4];
    for (i32 i = 0; i < 4; i++) consumers[i] = bmf_platform_thread_start(platform_consumer_thread, &s, 64 * 1024, "bmf test consumer");
    for (i32 i = 0; i < 4 * 5000 / 10; i++) {
        bmf_semaphore_signal(&s.items, (i & 1) ? 1 : 2);
        bmf_semaphore_signal(&s.items, (i & 1) ? 9 : 8);
    }
    for (i32 i = 0; i < 4; i++) bmf_platform_thread_join(&consumers[i]);
    bmf_check_eq_i64(t, s.consumed, 20000, __FILE__, __LINE__, "all items consumed");
    bmf_check_eq_i64(t, s.items.count, 0, __FILE__, __LINE__, "no units left");
    bmf_check_eq_i64(t, s.items.waiters, 0, __FILE__, __LINE__, "no waiters left");

    // futex_wait returns at once when the value already differs.
    i32 word = 5;
    bmf_platform_futex_wait(&word, 4);
    bmf_check(t, 1, __FILE__, __LINE__, "futex_wait with a stale value returns");
}

// ---------------------------------------------------------------- FPU state (D-37 rule 10)
typedef struct {
    u32 mxcsr;
    b32 check;
    f32 denormal_product;
} platform_fpu_test;

static void platform_fpu_thread(void* user) {
    platform_fpu_test* f = (platform_fpu_test*)user;
    f->mxcsr = __builtin_ia32_stmxcsr();
    f->check = bmf_fpu_check();
    volatile f32 small = 1e-38f;   // normal; the product is a denormal (flushed to 0 under FTZ)
    volatile f32 scale = 1e-3f;
    f->denormal_product = small * scale;
}

static void test_platform_thread_fpu_state(bmf_test* t) {
    bmf_check(t, bmf_fpu_check(), __FILE__, __LINE__, "main thread FPU state after bmf_platform_init");
    // Break the creating thread's state (FTZ, DAZ, round toward zero): a new thread must not
    // inherit it. Windows itself starts threads with MXCSR 0x1F80, so this cannot prove that the
    // trampoline's bmf_fpu_setup ran; it proves the state user code sees in a platform thread.
    u32 saved = __builtin_ia32_stmxcsr();
    __builtin_ia32_ldmxcsr(0x1F80 | 0x8000 | 0x0040 | 0x6000);
    bmf_check(t, !bmf_fpu_check(), __FILE__, __LINE__, "check detects a wrong state");
    platform_fpu_test f = { 0, 0, 0.0f };
    bmf_thread thread = bmf_platform_thread_start(platform_fpu_thread, &f, 64 * 1024, "bmf test fpu");
    bmf_platform_thread_join(&thread);
    __builtin_ia32_ldmxcsr(saved);
    bmf_check_eq_u64(t, f.mxcsr & ~(u32)BMF_MXCSR_STATUS_FLAGS, BMF_MXCSR_REQUIRED, __FILE__, __LINE__, "spawned thread MXCSR");
    bmf_check(t, f.check, __FILE__, __LINE__, "bmf_fpu_check in spawned thread");
    bmf_check(t, f.denormal_product != 0.0f, __FILE__, __LINE__, "denormal kept in spawned thread");
    bmf_check(t, bmf_fpu_check(), __FILE__, __LINE__, "main thread restored");
}

// ---------------------------------------------------------------- time
static void test_platform_time_and_sleep(bmf_test* t) {
    i64 previous = bmf_platform_time_ns();
    i64 loop_start = previous;
    b32 monotonic = 1;
    for (i32 i = 0; i < 100000; i++) {
        i64 now = bmf_platform_time_ns();
        if (now < previous) monotonic = 0;
        previous = now;
    }
    bmf_check(t, monotonic, __FILE__, __LINE__, "time never decreases");
    i64 ns_per_call = (previous - loop_start) / 100000;

    i64 worst_late = 0, worst_early = 0;
    for (i32 i = 0; i < 10; i++) {
        i64 request = 2000000;   // 2 ms
        i64 start = bmf_platform_time_ns();
        bmf_platform_sleep_ns(request);
        i64 elapsed = bmf_platform_time_ns() - start;
        if (elapsed - request > worst_late) worst_late = elapsed - request;
        if (request - elapsed > worst_early) worst_early = request - elapsed;
    }
    bmf_check_eq_i64(t, worst_early, 0, __FILE__, __LINE__, "sleep never returns early");
    i64 start = bmf_platform_time_ns();
    bmf_platform_sleep_ns(0);
    bmf_platform_sleep_ns(-5);
    bmf_check(t, bmf_platform_time_ns() - start < 1000000, __FILE__, __LINE__, "sleep(<=0) returns at once");
    char note[128];
    bmf_fmt_buf(note, (i32)sizeof note, "time_ns %i ns/call; 2 ms sleep: worst late %i us", (i32)ns_per_call, (i32)(worst_late / 1000));
    bmf_test_note(t, note);
}

// ---------------------------------------------------------------- stdio buffering (white box)
static i32 platform_pipe_available(HANDLE pipe) {
    u32 available = 0;
    PeekNamedPipe(pipe, 0, 0, 0, &available, 0);
    return (i32)available;
}

static void test_platform_stdio_line_buffering(bmf_test* t) {
    bmf_platform_flush(BMF_STREAM_STDOUT);
    HANDLE pipe_read = 0, pipe_write = 0;
    bmf_check(t, CreatePipe(&pipe_read, &pipe_write, 0, 1 << 20) != 0, __FILE__, __LINE__, "pipe");
    HANDLE saved = win32_core.std_handles[BMF_STREAM_STDOUT];
    win32_core.std_handles[BMF_STREAM_STDOUT] = pipe_write;

    bmf_platform_write(BMF_STREAM_STDOUT, "ab", 2);
    i32 after_partial = platform_pipe_available(pipe_read);
    bmf_platform_write(BMF_STREAM_STDOUT, "c\nd", 3);
    i32 after_newline = platform_pipe_available(pipe_read);
    bmf_platform_flush(BMF_STREAM_STDOUT);
    i32 after_flush = platform_pipe_available(pipe_read);
    // A write larger than the buffer goes straight through, after what was buffered.
    static char big[70000];
    bmf_memset(big, 'x', sizeof big);
    bmf_platform_write(BMF_STREAM_STDOUT, "e", 1);
    bmf_platform_write(BMF_STREAM_STDOUT, big, (i32)sizeof big);
    i32 after_big = platform_pipe_available(pipe_read);

    win32_core.std_handles[BMF_STREAM_STDOUT] = saved;
    char got[16];
    u32 got_len = 0;
    ReadFile(pipe_read, got, 6, &got_len, 0);
    CloseHandle(pipe_read);
    CloseHandle(pipe_write);
    bmf_check_eq_i64(t, after_partial, 0, __FILE__, __LINE__, "partial line stays buffered");
    bmf_check_eq_i64(t, after_newline, 4, __FILE__, __LINE__, "newline writes through the last newline");
    bmf_check_eq_i64(t, after_flush, 5, __FILE__, __LINE__, "flush writes the rest");
    bmf_check_eq_i64(t, after_big, 5 + 1 + (i32)sizeof big, __FILE__, __LINE__, "large write goes through");
    bmf_check_eq_mem(t, got, "abc\nde", 6, __FILE__, __LINE__, "byte order");
}

// ---------------------------------------------------------------- DLLs
static void test_platform_library_load(bmf_test* t) {
    bmf_library kernel = bmf_platform_library_load("kernel32.dll");
    bmf_check(t, kernel != 0, __FILE__, __LINE__, "load kernel32.dll");
    typedef u32 (*get_pid_fn)(void);
    get_pid_fn get_pid = (get_pid_fn)bmf_platform_library_symbol(kernel, "GetCurrentProcessId");
    bmf_check(t, get_pid && get_pid() == GetCurrentProcessId(), __FILE__, __LINE__, "symbol works");
    bmf_check(t, bmf_platform_library_symbol(kernel, "NoSuchFunctionInKernel32") == 0, __FILE__, __LINE__, "missing symbol is 0");
    bmf_platform_library_unload(kernel);
    bmf_check(t, bmf_platform_library_load("no_such_library_bmf.dll") == 0, __FILE__, __LINE__, "missing library is 0");
}
