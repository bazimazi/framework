// test_main.c: the bmf unit-test runner (task 1A-08). Built by `build test` as one translation
// unit: generated root -> src/bmf.c (BMF_BUILD_TESTS) -> this file -> tests/tests.c.
//
//   test.exe [filter] [--update-golden NAME]   run every test whose name contains `filter`;
//                             NAME selects engine golden images to rewrite (tools/test/golden.c)
//   test.exe --golden [filter] [--update-golden NAME]   game goldens and the lint (golden.c)
//   test.exe --perf [filter]  perf budgets of games and bench scenes, benchmark history (perf.c)
//   test.exe --run-one NAME   run exactly one test in this process (used for expected-crash tests)
//   test.exe --game-child GAME FLAGS...   run a test game with bmf_core_run (protocol process tests;
//                             tests/test_protocol.c: test_game_child_main)
//
// Output: one JSON line per test, then one summary line. Exit code: 0 if all passed, else 1.
// Expected-crash tests run in a child process (the runner re-invokes itself with --run-one);
// they pass when the child dies with a Windows exception code (>= 0xC0000000).
//
// Output goes through bmf_json_writer and messages through bmf_fmt (src/base/base_string.h).
//
// Test API (used by tests/test_*.c). Checks take the call site explicitly (__FILE__, __LINE__):
// SPEC §16 allows no function-like macro that could capture it.

typedef struct bmf_test {
    const char* name;
    i32 check_count;
    i32 failure_count;
    const char* first_file;    // location and message of the first failed check
    i32 first_line;
    char first_message[512];
    char note[512];            // optional measurement text, printed as "note" (bmf_test_note)
} bmf_test;

static void test_out(const char* s) {
    bmf_platform_write(BMF_STREAM_STDOUT, s, bmf_cstr_len(s));
}

// ---------------------------------------------------------------- checks
// Records one check. On failure, keeps the first failing location and message.
// Cost O(1). Never stops the test: later checks still run.
static inline void bmf_check_at(bmf_test* t, b32 cond, const char* file, i32 line, const char* message) {
    t->check_count++;
    if (cond) return;
    t->failure_count++;
    if (t->failure_count > 1) return;
    t->first_file = file;
    t->first_line = line;
    bmf_fmt_buf(t->first_message, (i32)sizeof t->first_message, "%s", message);
}

// Checks that `cond` is true.
static inline void bmf_check(bmf_test* t, b32 cond, const char* file, i32 line, const char* message) {
    bmf_check_at(t, cond, file, line, message);
}

// Checks actual == expected for signed integers; the failure message shows both values.
static inline void bmf_check_eq_i64(bmf_test* t, i64 actual, i64 expected, const char* file, i32 line, const char* message) {
    if (actual == expected) { bmf_check_at(t, 1, file, line, message); return; }
    char buf[512];
    bmf_fmt_buf(buf, (i32)sizeof buf, "%s: expected %li, got %li", message, expected, actual);
    bmf_check_at(t, 0, file, line, buf);
}

// Checks actual == expected for unsigned values (bits, hashes, handles); shown in hex.
static inline void bmf_check_eq_u64(bmf_test* t, u64 actual, u64 expected, const char* file, i32 line, const char* message) {
    if (actual == expected) { bmf_check_at(t, 1, file, line, message); return; }
    char buf[512];
    bmf_fmt_buf(buf, (i32)sizeof buf, "%s: expected 0x%016lx, got 0x%016lx", message, expected, actual);
    bmf_check_at(t, 0, file, line, buf);
}

// Checks that two byte ranges are identical; the failure message shows the first differing offset.
static inline void bmf_check_eq_mem(bmf_test* t, const void* actual, const void* expected, i64 size, const char* file, i32 line, const char* message) {
    const u8* a = (const u8*)actual;
    const u8* e = (const u8*)expected;
    for (i64 i = 0; i < size; i++) {
        if (a[i] == e[i]) continue;
        char buf[512];
        bmf_fmt_buf(buf, (i32)sizeof buf, "%s: first difference at byte %li (expected %u, got %u)", message, i, (u32)e[i], (u32)a[i]);
        bmf_check_at(t, 0, file, line, buf);
        return;
    }
    bmf_check_at(t, 1, file, line, message);
}

// Attaches a measurement (for example a max ULP error) to the test's JSON line as "note",
// whether the test passes or fails. The last call wins.
static inline void bmf_test_note(bmf_test* t, const char* text) {
    bmf_fmt_buf(t->note, (i32)sizeof t->note, "%s", text);
}

// ---------------------------------------------------------------- registry
typedef void (*bmf_test_fn)(bmf_test* t);
typedef struct {
    const char* name;      // "<module>.<what>", unique
    bmf_test_fn fn;
    b32 expect_crash;      // runs in a child process; passes if the child dies with an exception
} bmf_test_entry;

#include "golden.c"        // golden images for unit tests, the game golden runner and the lint
#include "perf.c"          // perf budgets and the benchmark history (uses golden.c's helpers)
#include "tests/tests.c"   // via -I<root>; defines TESTS[] (explicit table, no auto-registration)

// ---------------------------------------------------------------- running

// Deletes the crash dump files a crash line in `text` names ("replay":"..." and "state":"..."):
// expected crashes leave nothing behind.
static void delete_crash_dump(const char* text) {
    const char* keys[2] = { "\"replay\":\"", "\"state\":\"" };
    for (i32 k = 0; k < 2; k++) {
        i32 at = bmf_str_find(bmf_str_from_cstr(text), bmf_str_from_cstr(keys[k]), 0);
        if (at < 0) continue;
        char path[1024];
        i32 n = 0;
        for (const char* c = text + at + bmf_cstr_len(keys[k]); *c && *c != '"' && n < (i32)sizeof path - 1; c++) {
            if (*c == '\\' && c[1]) c++;
            path[n++] = *c;
        }
        path[n] = 0;
        bmf_platform_file_delete(path);
    }
}

// Runs `--run-one name` as a child process with its output captured (only to delete the crash
// dump it names), and returns the child's exit code.
static u32 run_child(const char* name) {
    WCHAR exe[1024];
    u32 n = GetModuleFileNameW(0, exe, 1024);
    exe[n] = 0;
    WCHAR cmd[2048];
    i32 len = 0;
    cmd[len++] = '"';
    for (u32 i = 0; i < n; i++) cmd[len++] = exe[i];
    const char* rest = "\" --run-one ";
    for (const char* s = rest; *s; s++) cmd[len++] = (WCHAR)*s;
    for (const char* s = name; *s && len < 2000; s++) cmd[len++] = (WCHAR)*s;   // test names are ASCII
    cmd[len] = 0;

    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), 0, 1 };
    HANDLE pipe_read = 0, pipe_write = 0;
    if (!CreatePipe(&pipe_read, &pipe_write, &sa, 0)) return 0;
    SetHandleInformation(pipe_read, WIN32_HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si;
    bmf_memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = WIN32_STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle((u32)WIN32_STD_INPUT_HANDLE);
    si.hStdOutput = pipe_write;
    si.hStdError = pipe_write;
    PROCESS_INFORMATION pi;
    bmf_memset(&pi, 0, sizeof pi);
    b32 started = CreateProcessW(exe, cmd, 0, 0, 1, 0, 0, 0, &si, &pi) != 0;
    CloseHandle(pipe_write);
    static char output[64 * 1024];
    i32 used = 0;
    if (started) {
        char sink[4096];
        u32 got = 0;
        while (ReadFile(pipe_read, sink, sizeof sink, &got, 0) && got) {
            for (u32 i = 0; i < got && used < (i32)sizeof output - 1; i++) output[used++] = sink[i];
        }
    }
    output[used] = 0;
    delete_crash_dump(output);
    CloseHandle(pipe_read);
    if (!started) return 0;
    WaitForSingleObject(pi.hProcess, (u32)WIN32_INFINITE);
    u32 code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code;
}

void bmf_win32_entry(void);
void bmf_win32_entry(void) {
    bmf_platform_init();
    i64 start = bmf_platform_time_ns();

    // Arguments: [filter] or [--run-one name].
    bmf_platform_args args = bmf_platform_get_args();
    const char* first = args.count > 1 ? args.values[1] : "";
    const char* second = args.count > 2 ? args.values[2] : "";
    i32 test_count = (i32)(sizeof TESTS / sizeof TESTS[0]);

    if (bmf_cstr_eq(first, "--game-child")) bmf_platform_exit(test_game_child_main(args.count, args.values));
    // --update-golden NAME (anywhere after the mode) selects goldens to rewrite.
    const char* positional[2] = { "", "" };
    i32 positional_count = 0;
    b32 golden_mode = 0, perf_mode = 0;
    for (i32 i = 1; i < args.count; i++) {
        if (bmf_cstr_eq(args.values[i], "--update-golden") && i + 1 < args.count) { golden_update_name = args.values[++i]; continue; }
        if (bmf_cstr_eq(args.values[i], "--golden")) { golden_mode = 1; continue; }
        if (bmf_cstr_eq(args.values[i], "--perf")) { perf_mode = 1; continue; }
        if (positional_count < 2) positional[positional_count++] = args.values[i];
    }
    if (golden_mode) bmf_platform_exit(golden_main(positional[0]));
    if (perf_mode) bmf_platform_exit(perf_main(positional[0]));
    if (bmf_cstr_eq(first, "--run-one")) {
        // No Windows Error Reporting for expected crashes: it adds ~0.5 s per crash.
        SetErrorMode(WIN32_SEM_FAILCRITICALERRORS | WIN32_SEM_NOGPFAULTERRORBOX | WIN32_SEM_NOOPENFILEERRORBOX);
        for (i32 i = 0; i < test_count; i++) {
            if (!bmf_cstr_eq(TESTS[i].name, second)) continue;
            bmf_test t;
            bmf_memset(&t, 0, sizeof t);
            t.name = TESTS[i].name;
            TESTS[i].fn(&t);
            bmf_platform_exit(t.failure_count ? 1 : 0);
        }
        bmf_platform_exit(2);
    }

    const char* filter = positional[0];
    i32 run = 0, passed = 0;
    for (i32 i = 0; i < test_count; i++) {
        const bmf_test_entry* e = &TESTS[i];
        if (bmf_str_find(bmf_str_from_cstr(e->name), bmf_str_from_cstr(filter), 0) < 0) continue;
        run++;
        bmf_test t;
        bmf_memset(&t, 0, sizeof t);
        t.name = e->name;
        i64 t0 = bmf_platform_time_ns();
        u32 child_code = 0;
        if (e->expect_crash) {
            child_code = run_child(e->name);
            bmf_check(&t, child_code >= 0xC0000000u, __FILE__, __LINE__, "expected the test process to crash");
        } else {
            e->fn(&t);
        }
        i64 ms = (bmf_platform_time_ns() - t0) / 1000000;
        b32 ok = t.failure_count == 0;
        passed += ok;

        char line[2048];
        bmf_json_writer w;
        bmf_json_init(&w, line, (i32)sizeof line - 1);
        bmf_json_object_begin(&w);
        bmf_json_key(&w, "test"); bmf_json_string(&w, e->name);
        bmf_json_key(&w, "ok"); bmf_json_i64(&w, ok ? 1 : 0);
        bmf_json_key(&w, "ms"); bmf_json_i64(&w, ms);
        bmf_json_key(&w, "checks"); bmf_json_i64(&w, t.check_count);
        if (e->expect_crash) {
            char code[24];
            bmf_fmt_buf(code, (i32)sizeof code, "0x%08x", child_code);
            bmf_json_key(&w, "exit_code"); bmf_json_string(&w, code);
        }
        if (t.note[0]) { bmf_json_key(&w, "note"); bmf_json_string(&w, t.note); }
        if (!ok) {
            bmf_json_key(&w, "failures"); bmf_json_i64(&w, t.failure_count);
            bmf_json_key(&w, "file"); bmf_json_string(&w, t.first_file);
            bmf_json_key(&w, "line"); bmf_json_i64(&w, t.first_line);
            bmf_json_key(&w, "msg"); bmf_json_string(&w, t.first_message);
        }
        bmf_json_object_end(&w);
        line[w.len] = '\n';
        line[w.len + 1] = 0;
        test_out(line);
    }
    char summary[512];
    if (run == 0 && filter[0])   // say so: a filter that matches no unit test is easy to mistake for a pass
        bmf_fmt_buf(summary, (i32)sizeof summary, "{\"tests\":0,\"passed\":0,\"failed\":0,\"ms\":%li,\"warning\":\"no unit test name contains '%s'\"}\n",
                    (bmf_platform_time_ns() - start) / 1000000, filter);
    else
        bmf_fmt_buf(summary, (i32)sizeof summary, "{\"tests\":%i,\"passed\":%i,\"failed\":%i,\"ms\":%li}\n",
                    run, passed, run - passed, (bmf_platform_time_ns() - start) / 1000000);
    test_out(summary);
    bmf_platform_exit(run == passed ? 0 : 1);
}
