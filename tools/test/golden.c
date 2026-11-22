// golden.c: golden tests (SPEC §15, task 1A-41), part of the test runner (test_main.c).
//
// Game goldens live in games/<g>/tests/ (and the same for the bench scenes, tests/bench/<scene>/tests/):
//   <scenario>.script              protocol requests that play the scenario (see "Scripts")
//   <scenario>.<R>hz.bmfr          its recording at R Hz: the game's own rate and 120 Hz (D-32)
//   golden/d3d11/<scenario>.<R>hz.<label>.png   a WARP shot of the replay at a tick ...
//   golden/d3d11/<scenario>.<R>hz.<label>.shot  ... named by this sidecar: "replay F", "tick T", "size WxH"
// `test.exe --golden [filter]` (run by `build test` after building each such game in debug):
//   - lint: simulation sources must not use bmf_fast_*, rdtsc, __builtin_fma or raw float-to-int
//     casts (D-37 rules 1, 4, 9, 12); scanned: src/physics2d/*.c and every game or bench scene .c
//     file with a line "// bmf-lint: sim" (the whole file is treated as simulation code);
//   - every tests/*.bmfr: `game.exe --replay F --verify` must exit 0 (all checkpoints match);
//   - every golden/d3d11/*.shot: the replay is played to the tick (`--replay F --control --headless
//     --size WxH`, `step`, `shot`) and compared with the .png: a pixel differs when a channel is
//     off by more than 2, and at most 0.1% of the pixels may differ (SPEC §15). A failure writes
//     the actual image and a diff image to out/test/golden/.
//   - a script without recordings fails (run the update).
// Engine goldens (images rendered by unit tests on WARP, e.g. the draw2d sheets) live in
// tests/golden/d3d11/<name>.png and are checked by test_golden_image from the unit test itself.
//
// Updates are explicit: `build test --update-golden NAME` (test.exe ... --update-golden NAME)
// rewrites the goldens whose name is NAME or starts with NAME + "." (game goldens are named
// <game>.<scenario>, engine goldens by their unit test) and prints one line per file:
// {"golden":"updated","file":F,"changed":B} (replays compare without their build id).
//
// Scripts: one request per line, as for --control, with these differences: tick counts (`step N`,
// `press A N`, `axis A X Y N`) are in ticks of the game's own rate and are scaled to each recorded
// rate (they must divide evenly); `golden LABEL` takes a golden shot; `seed N` and `size WxH`
// (default 320x180) set the run's options. The recording covers the whole script from tick 0.
// Paths must not contain spaces (protocol tokens).
//
// Windows only so far (process spawning, directory listing); the web mode is task 1B-50.

enum {
    GOLDEN_TOLERANCE = 2,                 // per channel
    GOLDEN_MAX_NAMES = 256,
    GOLDEN_OUTPUT = 1 << 20,              // captured output of one game process
};
static const f64 GOLDEN_MAX_FRACTION = 0.001;   // of pixels that may differ

static const char* golden_update_name;    // --update-golden NAME, or 0
static char golden_root_dir[1024];        // the framework root, '/' separators, no trailing '/'
static bmf_arena golden_arena;            // file contents and images (mark/pop per use)
static char golden_output[GOLDEN_OUTPUT];
static char golden_input[64 * 1024];

static void golden_init(void) {
    if (golden_arena.base) return;
    char dir[1024];
    i32 n = bmf_platform_exe_dir(dir, (i32)sizeof dir);   // <root>/out/test/win64-debug/
    for (i32 i = 0; i < n; i++) if (dir[i] == '\\') dir[i] = '/';
    while (n > 0 && dir[n - 1] == '/') n--;
    for (i32 up = 0; up < 3; up++) {                      // up from win64-debug, test, out
        while (n > 0 && dir[n - 1] != '/') n--;
        while (n > 0 && dir[n - 1] == '/') n--;
    }
    dir[n] = 0;
    bmf_fmt_buf(golden_root_dir, (i32)sizeof golden_root_dir, "%s", dir);
    i64 size = 1ll << 30;
    bmf_arena_init(&golden_arena, "golden", (u8*)bmf_platform_reserve(size + BMF_PAGE_GRANULE), size);
}

// 1 when NAME selects `name` for an update: equal, or a prefix that ends at a '.'.
static b32 golden_update_matches(const char* name) {
    if (!golden_update_name) return 0;
    i32 n = bmf_cstr_len(golden_update_name);
    bmf_str s = bmf_str_from_cstr(name);
    if (!bmf_str_starts_with(s, bmf_str_from_cstr(golden_update_name))) return 0;
    return s.len == n || name[n] == '.';
}

static void golden_print_update(const char* file, b32 changed) {
    char line[1400];
    bmf_json_writer w;
    bmf_json_init(&w, line, (i32)sizeof line - 1);
    bmf_json_object_begin(&w);
    bmf_json_key(&w, "golden"); bmf_json_string(&w, "updated");
    bmf_json_key(&w, "file"); bmf_json_string(&w, file + (bmf_str_starts_with(bmf_str_from_cstr(file), bmf_str_from_cstr(golden_root_dir)) ? bmf_cstr_len(golden_root_dir) + 1 : 0));
    bmf_json_key(&w, "changed"); bmf_json_bool(&w, changed);
    bmf_json_object_end(&w);
    line[w.len] = '\n';
    line[w.len + 1] = 0;
    test_out(line);
}

// Reads a whole file into the golden arena; 0 when it cannot be read.
static b32 golden_read(const char* path, bmf_str* out) {
    return bmf_platform_file_read(path, &golden_arena, out) == BMF_OK;
}

// Writes `data` to `path` and reports it, with "changed" from a byte comparison against the old
// file (ignoring [ignore_offset, ignore_offset + ignore_size), e.g. a replay's build id).
static b32 golden_write(const char* path, const void* data, i64 size, i64 ignore_offset, i64 ignore_size) {
    i64 mark = bmf_arena_mark(&golden_arena);
    bmf_str old;
    b32 changed = 1;
    if (golden_read(path, &old) && old.len == size) {
        changed = 0;
        const u8* a = (const u8*)data;
        for (i64 i = 0; i < size && !changed; i++)
            if ((i < ignore_offset || i >= ignore_offset + ignore_size) && a[i] != old.ptr[i]) changed = 1;
    }
    bmf_arena_pop_to(&golden_arena, mark);
    if (bmf_platform_file_write_atomic(path, data, size) != BMF_OK) return 0;
    golden_print_update(path, changed);
    return 1;
}

// ---------------------------------------------------------------- image comparison
// Compares `actual` with the golden PNG at `golden_path`. On failure writes `<out_stem>.png` (the
// actual image) and `<out_stem>.diff.png`, and a readable message. Returns 1 when within tolerance.
static b32 golden_compare_image(const char* golden_path, const u8* actual, i32 width, i32 height, const char* out_stem,
                                char* message, i32 message_capacity) {
    i64 mark = bmf_arena_mark(&golden_arena);
    b32 ok = 0;
    bmf_str file;
    i32 gw = 0, gh = 0;
    if (!golden_read(golden_path, &file)) {
        bmf_fmt_buf(message, message_capacity, "no golden '%s' (create it with build test --update-golden NAME)", golden_path);
    } else if (bmf_png_info(file.ptr, file.len, &gw, &gh) != BMF_OK || gw != width || gh != height) {
        bmf_fmt_buf(message, message_capacity, "golden '%s' is %ix%i, the image is %ix%i", golden_path, gw, gh, width, height);
    } else {
        i64 bytes = (i64)width * height * 4;
        u8* expected = (u8*)bmf_arena_push_aligned(&golden_arena, bytes, 64);
        u8* diff = (u8*)bmf_arena_push_aligned(&golden_arena, bytes, 64);
        if (bmf_png_read_stored(file.ptr, file.len, expected, bytes, &gw, &gh) != BMF_OK) {
            bmf_fmt_buf(message, message_capacity, "golden '%s' is not a stored-deflate PNG", golden_path);
        } else {
            bmf_image_diff_result r = bmf_image_diff(expected, actual, width, height, GOLDEN_TOLERANCE, diff);
            ok = r.differing_fraction <= GOLDEN_MAX_FRACTION;
            if (!ok) {
                char path[1200];
                i64 png_capacity = bmf_png_write_size(width, height);
                u8* png = (u8*)bmf_arena_push_aligned(&golden_arena, png_capacity, 16);
                bmf_fmt_buf(path, (i32)sizeof path, "%s/out/test/golden", golden_root_dir);
                bmf_platform_dir_create(path);
                bmf_fmt_buf(path, (i32)sizeof path, "%s.png", out_stem);
                bmf_platform_file_write_atomic(path, png, bmf_png_write(actual, width, height, png, png_capacity));
                bmf_fmt_buf(path, (i32)sizeof path, "%s.diff.png", out_stem);
                bmf_platform_file_write_atomic(path, png, bmf_png_write(diff, width, height, png, png_capacity));
                bmf_fmt_buf(message, message_capacity,
                            "%li of %li pixels differ by more than %i (%f%%, at most %f%%), largest channel difference %i, first at (%i, %i); actual %s.png, diff %s.diff.png",
                            r.differing, r.pixels, (i32)GOLDEN_TOLERANCE, r.differing_fraction * 100.0, GOLDEN_MAX_FRACTION * 100.0,
                            r.max_channel_diff, r.first_x, r.first_y, out_stem, out_stem);
            }
        }
    }
    bmf_arena_pop_to(&golden_arena, mark);
    return ok;
}

// For unit tests that render on WARP: compares `rgba` with tests/golden/d3d11/<name>.png, or
// rewrites that file when --update-golden selects `name`. One check on `t`.
static void test_golden_image(bmf_test* t, const char* name, const u8* rgba, i32 width, i32 height) {
    golden_init();
    char golden_path[1200], out_stem[1200], message[1024];
    bmf_fmt_buf(golden_path, (i32)sizeof golden_path, "%s/tests/golden/d3d11/%s.png", golden_root_dir, name);
    if (golden_update_matches(name)) {
        i64 mark = bmf_arena_mark(&golden_arena);
        i64 capacity = bmf_png_write_size(width, height);
        u8* png = (u8*)bmf_arena_push_aligned(&golden_arena, capacity, 16);
        bmf_fmt_buf(message, (i32)sizeof message, "%s/tests/golden", golden_root_dir);
        bmf_platform_dir_create(message);
        bmf_fmt_buf(message, (i32)sizeof message, "%s/tests/golden/d3d11", golden_root_dir);
        bmf_platform_dir_create(message);
        b32 written = golden_write(golden_path, png, bmf_png_write(rgba, width, height, png, capacity), 0, 0);
        bmf_arena_pop_to(&golden_arena, mark);
        bmf_check(t, written, __FILE__, __LINE__, golden_path);
        return;
    }
    bmf_fmt_buf(out_stem, (i32)sizeof out_stem, "%s/out/test/golden/%s", golden_root_dir, name);
    b32 ok = golden_compare_image(golden_path, rgba, width, height, out_stem, message, (i32)sizeof message);
    bmf_check(t, ok, __FILE__, __LINE__, message);
}

// ---------------------------------------------------------------- lint (D-37)
// Copies `text` with comments, string and character literals replaced by spaces (newlines kept).
static void golden_lint_strip(const char* text, i32 len, char* out) {
    i32 i = 0;
    while (i < len) {
        char c = text[i];
        if (c == '/' && i + 1 < len && text[i + 1] == '/') {
            while (i < len && text[i] != '\n') out[i++] = ' ';
        } else if (c == '/' && i + 1 < len && text[i + 1] == '*') {
            out[i++] = ' '; out[i++] = ' ';
            while (i < len && !(text[i] == '*' && i + 1 < len && text[i + 1] == '/')) { out[i] = text[i] == '\n' ? '\n' : ' '; i++; }
            if (i < len) { out[i++] = ' '; out[i++] = ' '; }
        } else if (c == '"' || c == '\'') {
            out[i++] = ' ';
            while (i < len && text[i] != c && text[i] != '\n') {
                if (text[i] == '\\' && i + 1 < len) { out[i++] = ' '; }
                out[i] = ' ';
                i++;
            }
            if (i < len && text[i] == c) out[i++] = ' ';
        } else {
            out[i] = c;
            i++;
        }
    }
}

static b32 golden_lint_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// 1 when s[0..n) holds a float literal: digits with a '.', an exponent or an f suffix.
static b32 golden_lint_has_float_literal(const char* s, i32 n) {
    for (i32 i = 0; i < n; i++) {
        if (!(s[i] >= '0' && s[i] <= '9') || (i > 0 && golden_lint_ident_char(s[i - 1]))) continue;
        i32 j = i;
        b32 is_float = 0;
        while (j < n && ((s[j] >= '0' && s[j] <= '9') || s[j] == '.')) { if (s[j] == '.') is_float = 1; j++; }
        if (j < n && (s[j] == 'e' || s[j] == 'E' || s[j] == 'f' || s[j] == 'F')) is_float = 1;
        if (i > 0 && s[i - 1] == '.') is_float = 1;   // ".5f"
        if (j + 1 < n && s[j] == 'x' ) is_float = 0;
        if (is_float) return 1;
        i = j;
    }
    return 0;
}

// Scans one source (label = its path for messages). Returns the number of findings and writes
// "label:line: what" lines into report (as many as fit). Heuristics, by design: a cast is flagged
// when its operand is a float literal, a parenthesized expression containing one, or a call of a
// function whose name ends in 'f' or "f32" (bmf_floorf, bmf_rng_f32, ...; not sizeof, offsetof,
// typeof); casts of float variables are not seen.
static i32 golden_lint_source(const char* label, const char* text, i32 len, char* report, i32 report_capacity) {
    i64 mark = bmf_arena_mark(&golden_arena);
    char* s = (char*)bmf_arena_push_aligned(&golden_arena, len + 1, 16);
    golden_lint_strip(text, len, s);
    s[len] = 0;
    i32 findings = 0, used = 0, line = 1;
    static const char* const casts[] = { "(i8)", "(i16)", "(i32)", "(i64)", "(u8)", "(u16)", "(u32)", "(u64)", "(int)", "(long)", "(short)", "(unsigned)" };
    for (i32 i = 0; i < len; i++) {
        if (s[i] == '\n') { line++; continue; }
        const char* what = 0;
        b32 starts_word = i == 0 || !golden_lint_ident_char(s[i - 1]);
        bmf_str rest = bmf_str_make(s + i, len - i);
        if (starts_word && bmf_str_starts_with(rest, bmf_str_from_cstr("bmf_fast_"))) what = "bmf_fast_* math is not deterministic across targets (D-37 rule 4)";
        else if (bmf_str_starts_with(rest, bmf_str_from_cstr("rdtsc"))) what = "rdtsc reads the clock (D-37 rule 1)";
        else if (starts_word && bmf_str_starts_with(rest, bmf_str_from_cstr("__builtin_fma"))) what = "FMA differs between CPUs (D-37 rule 9)";
        else if (s[i] == '(') {
            for (i32 c = 0; c < (i32)(sizeof casts / sizeof casts[0]) && !what; c++) {
                if (!bmf_str_starts_with(rest, bmf_str_from_cstr(casts[c]))) continue;
                i32 j = i + bmf_cstr_len(casts[c]);
                while (j < len && (s[j] == ' ' || s[j] == '\t')) j++;
                b32 flagged = 0;
                if (j < len && ((s[j] >= '0' && s[j] <= '9') || s[j] == '.')) {
                    i32 k = j;
                    while (k < len && (golden_lint_ident_char(s[k]) || s[k] == '.')) k++;
                    flagged = golden_lint_has_float_literal(s + j, k - j);
                } else if (j < len && s[j] == '(') {
                    i32 depth = 0, k = j;
                    for (; k < len; k++) {
                        if (s[k] == '(') depth++;
                        if (s[k] == ')' && --depth == 0) break;
                    }
                    flagged = golden_lint_has_float_literal(s + j, k - j);
                } else if (j < len && golden_lint_ident_char(s[j])) {
                    i32 k = j;
                    while (k < len && golden_lint_ident_char(s[k])) k++;
                    i32 m = k;
                    while (m < len && (s[m] == ' ' || s[m] == '\t')) m++;
                    b32 call = m < len && s[m] == '(';
                    bmf_str name = bmf_str_make(s + j, k - j);
                    b32 keyword = bmf_str_eq_cstr(name, "sizeof") || bmf_str_eq_cstr(name, "offsetof") || bmf_str_eq_cstr(name, "typeof");
                    b32 float_name = !keyword && ((name.len > 1 && s[k - 1] == 'f') || bmf_str_ends_with(name, bmf_str_from_cstr("f32")));
                    flagged = call && float_name;
                }
                if (flagged) what = "raw float-to-int cast: use bmf_f32_to_i32 (D-37 rule 12)";
            }
        }
        if (!what) continue;
        findings++;
        if (used < report_capacity - 1) used += bmf_fmt_buf(report + used, report_capacity - used, "%s:%i: %s\n", label, line, what);
    }
    bmf_arena_pop_to(&golden_arena, mark);
    return findings;
}

// ---------------------------------------------------------------- processes and directories
// Runs `"exe" args` with `input` on stdin; stdout and stderr into golden_output. Returns the exit
// code (0xFFFFFFFF when it did not start).
static u32 golden_spawn(const char* exe, const char* args, const char* stdin_text, i32 stdin_length) {
    static char command[4096];
    static WCHAR wide_exe[1024], wide_command[4096];
    bmf_fmt_buf(command, (i32)sizeof command, "\"%s\" %s", exe, args);
    i32 n = bmf_utf8_to_utf16(bmf_str_from_cstr(exe), (u16*)wide_exe, 1023);
    wide_exe[n < 0 ? 0 : n] = 0;
    n = bmf_utf8_to_utf16(bmf_str_from_cstr(command), (u16*)wide_command, 4095);
    wide_command[n < 0 ? 0 : n] = 0;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), 0, 1 };
    HANDLE in_read = 0, in_write = 0, out_read = 0, out_write = 0;
    golden_output[0] = 0;
    if (!CreatePipe(&in_read, &in_write, &sa, 1 << 16) || !CreatePipe(&out_read, &out_write, &sa, 1 << 16)) return 0xFFFFFFFFu;
    SetHandleInformation(in_write, WIN32_HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_read, WIN32_HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si;
    bmf_memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = WIN32_STARTF_USESTDHANDLES;
    si.hStdInput = in_read;
    si.hStdOutput = out_write;
    si.hStdError = out_write;
    PROCESS_INFORMATION pi;
    bmf_memset(&pi, 0, sizeof pi);
    b32 started = CreateProcessW(wide_exe, wide_command, 0, 0, 1, 0, 0, 0, &si, &pi) != 0;
    CloseHandle(in_read);
    CloseHandle(out_write);
    if (started) {
        u32 written = 0;
        for (i32 done = 0; done < stdin_length; done += (i32)written)
            if (!WriteFile(in_write, stdin_text + done, (u32)(stdin_length - done), &written, 0) || !written) break;
    }
    CloseHandle(in_write);   // end of stdin
    i32 used = 0;
    if (started) {
        u32 got = 0;
        while (used < GOLDEN_OUTPUT - 1 && ReadFile(out_read, golden_output + used, (u32)(GOLDEN_OUTPUT - 1 - used), &got, 0) && got) used += (i32)got;
    }
    golden_output[used] = 0;
    CloseHandle(out_read);
    if (!started) return 0xFFFFFFFFu;
    WaitForSingleObject(pi.hProcess, (u32)WIN32_INFINITE);
    u32 code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code;
}

// Byte order of two NUL-terminated names: < 0, 0, > 0.
static i32 golden_name_compare(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (i32)(u8)*a - (i32)(u8)*b;
}

// Names (not paths) in `dir` ending in `suffix` (directories when `dirs`), sorted. Returns the count.
static i32 golden_list(const char* dir, const char* suffix, b32 dirs, char (*names)[128], i32 capacity) {
    char pattern[1200];
    WCHAR wide[1200];
    bmf_fmt_buf(pattern, (i32)sizeof pattern, "%s/*", dir);
    i32 n = bmf_utf8_to_utf16(bmf_str_from_cstr(pattern), (u16*)wide, 1199);
    wide[n < 0 ? 0 : n] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(wide, &fd);
    if (find == WIN32_INVALID_HANDLE_VALUE) return 0;
    i32 count = 0;
    do {
        char name[128];
        i32 len = 0;
        while (fd.cFileName[len]) len++;
        i32 m = bmf_utf16_to_utf8((const u16*)fd.cFileName, len, (u8*)name, 127);
        if (m < 0) continue;
        name[m] = 0;
        b32 is_dir = (fd.dwFileAttributes & WIN32_FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (name[0] == '.' || is_dir != dirs || !bmf_str_ends_with(bmf_str_from_cstr(name), bmf_str_from_cstr(suffix)) || count >= capacity) continue;
        bmf_memcpy(names[count], name, (i64)m + 1);
        count++;
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    for (i32 i = 1; i < count; i++)   // insertion sort: listing order is not guaranteed
        for (i32 j = i; j > 0 && golden_name_compare(names[j - 1], names[j]) > 0; j--) {
            char tmp[128];
            bmf_memcpy(tmp, names[j], 128);
            bmf_memcpy(names[j], names[j - 1], 128);
            bmf_memcpy(names[j - 1], tmp, 128);
        }
    return count;
}

// ---------------------------------------------------------------- results
static i32 golden_run, golden_passed;

static void golden_result(const char* name, b32 ok, i64 ms, const char* message) {
    char line[8192];
    bmf_json_writer w;
    bmf_json_init(&w, line, (i32)sizeof line - 1);
    bmf_json_object_begin(&w);
    bmf_json_key(&w, "test"); bmf_json_string(&w, name);
    bmf_json_key(&w, "ok"); bmf_json_i64(&w, ok ? 1 : 0);
    bmf_json_key(&w, "ms"); bmf_json_i64(&w, ms);
    if (!ok) { bmf_json_key(&w, "msg"); bmf_json_string(&w, message); }
    bmf_json_object_end(&w);
    line[w.len] = '\n';
    line[w.len + 1] = 0;
    test_out(line);
    golden_run++;
    golden_passed += ok ? 1 : 0;
}

// The last `count` lines of golden_output (for failure messages).
static const char* golden_output_tail(i32 count) {
    i32 n = bmf_cstr_len(golden_output);
    while (n > 0 && (golden_output[n - 1] == '\n' || golden_output[n - 1] == '\r')) n--;
    i32 at = n;
    for (i32 lines = 0; at > 0; at--)
        if (golden_output[at - 1] == '\n' && ++lines == count) break;
    return golden_output + at;
}

// ---------------------------------------------------------------- scenarios
// The game's own tick rate (the `hello` response), or 0.
static i32 golden_game_rate(const char* exe) {
    const char* request = "hello\nquit\n";
    if (golden_spawn(exe, "--headless --control --no-gpu", request, bmf_cstr_len(request)) != 0) return 0;
    i32 at = bmf_str_find(bmf_str_from_cstr(golden_output), bmf_str_from_cstr("\"tick_rate\":"), 0);
    if (at < 0) return 0;
    i64 rate = 0;
    const char* p = golden_output + at + 12;
    while (*p >= '0' && *p <= '9') rate = rate * 10 + (*p++ - '0');
    return (i32)rate;
}

// Records scenario `stem` of `game` at `rate` (base = the game's own rate). Returns 1 on success;
// on failure writes the reason.
static b32 golden_record(const char* game, const char* exe, const char* tests_dir, const char* stem, bmf_str script,
                         i32 rate, i32 base, char* error, i32 error_capacity) {
    char replay_path[1200], golden_dir[1200];
    bmf_fmt_buf(replay_path, (i32)sizeof replay_path, "%s/%s.%ihz.bmfr", tests_dir, stem, rate);
    bmf_fmt_buf(golden_dir, (i32)sizeof golden_dir, "%s/golden", tests_dir);
    bmf_platform_dir_create(golden_dir);
    bmf_fmt_buf(golden_dir, (i32)sizeof golden_dir, "%s/golden/d3d11", tests_dir);
    bmf_platform_dir_create(golden_dir);
    char record_path[1200];
    bmf_fmt_buf(record_path, (i32)sizeof record_path, "%s/out/test/golden/%s.%s.%ihz.bmfr", golden_root_dir, game, stem, rate);
    char out_dir[1200];
    bmf_fmt_buf(out_dir, (i32)sizeof out_dir, "%s/out/test/golden", golden_root_dir);
    bmf_platform_dir_create(out_dir);

    // The protocol input: the script with scaled tick counts and golden shots into out/test/golden.
    u64 seed = 0;
    i32 width = 320, height = 180;
    char labels[64][64];
    i32 label_count = 0;
    i32 used = bmf_fmt_buf(golden_input, (i32)sizeof golden_input, "record start %s\n", record_path);
    for (i32 at = 0; at < script.len;) {
        i32 end = at;
        while (end < script.len && script.ptr[end] != '\n') end++;
        bmf_str line = bmf_str_slice(script, at, end);
        at = end + 1;
        while (line.len > 0 && (line.ptr[line.len - 1] == '\r' || line.ptr[line.len - 1] == ' ')) line.len--;
        bmf_str tokens[8];
        i32 count = 0;
        for (i32 i = 0; i < line.len && count < 8;) {
            while (i < line.len && line.ptr[i] == ' ') i++;
            i32 start = i;
            while (i < line.len && line.ptr[i] != ' ') i++;
            if (i > start) tokens[count++] = bmf_str_slice(line, start, i);
        }
        if (count == 0 || tokens[0].ptr[0] == '#') continue;
        i64 value = 0;
        if (bmf_str_eq_cstr(tokens[0], "seed") && count == 2 && bmf_parse_i64(tokens[1], &value) == BMF_OK) { seed = (u64)value; continue; }
        if (bmf_str_eq_cstr(tokens[0], "size") && count == 2) {
            i32 x = bmf_str_find(tokens[1], bmf_str_from_cstr("x"), 0);
            i64 w = 0, h = 0;
            if (x < 0 || bmf_parse_i64(bmf_str_slice(tokens[1], 0, x), &w) != BMF_OK || bmf_parse_i64(bmf_str_slice(tokens[1], x + 1, tokens[1].len), &h) != BMF_OK) {
                bmf_fmt_buf(error, error_capacity, "%s.script: bad line '%S'", stem, line);
                return 0;
            }
            width = (i32)w, height = (i32)h;
            continue;
        }
        if (bmf_str_eq_cstr(tokens[0], "golden") && count == 2) {
            if (label_count >= 64) { bmf_fmt_buf(error, error_capacity, "%s.script: more than 64 golden shots", stem); return 0; }
            bmf_fmt_buf(labels[label_count], 64, "%S", tokens[1]);
            used += bmf_fmt_buf(golden_input + used, (i32)sizeof golden_input - used, "shot %s/%s.%s.%ihz.%S.png\n", out_dir, game, stem, rate, tokens[1]);
            label_count++;
            continue;
        }
        // Tick counts: `step N`, `press A N`, `axis A X Y N` (the last token).
        i32 tick_token = bmf_str_eq_cstr(tokens[0], "step") && count == 2 ? 1
                       : bmf_str_eq_cstr(tokens[0], "press") && count == 3 ? 2
                       : bmf_str_eq_cstr(tokens[0], "axis") && count == 5 ? 4 : -1;
        if (tick_token > 0) {
            if (bmf_parse_i64(tokens[tick_token], &value) != BMF_OK || (value * rate) % base != 0) {
                bmf_fmt_buf(error, error_capacity, "%s.script: '%S': the tick count must scale from %i Hz to %i Hz exactly", stem, line, base, rate);
                return 0;
            }
            for (i32 k = 0; k < count; k++) {
                if (k == tick_token) used += bmf_fmt_buf(golden_input + used, (i32)sizeof golden_input - used, "%li", value * rate / base);
                else used += bmf_fmt_buf(golden_input + used, (i32)sizeof golden_input - used, "%S", tokens[k]);
                used += bmf_fmt_buf(golden_input + used, (i32)sizeof golden_input - used, k + 1 < count ? " " : "\n");
            }
            continue;
        }
        used += bmf_fmt_buf(golden_input + used, (i32)sizeof golden_input - used, "%S\n", line);
    }
    used += bmf_fmt_buf(golden_input + used, (i32)sizeof golden_input - used, "record stop\nquit\n");

    char args[256];
    bmf_fmt_buf(args, (i32)sizeof args, "--headless --control --tick-rate %i --seed %lu --size %ix%i", rate, seed, width, height);
    u32 code = golden_spawn(exe, args, golden_input, used);
    if (code != 0 || bmf_str_find(bmf_str_from_cstr(golden_output), bmf_str_from_cstr("{\"ok\":0"), 0) >= 0) {
        i32 at = bmf_str_find(bmf_str_from_cstr(golden_output), bmf_str_from_cstr("{\"ok\":0"), 0);
        bmf_fmt_buf(error, error_capacity, "recording %s at %i Hz failed (exit %u): %s", stem, rate, code, at >= 0 ? golden_output + at : golden_output_tail(3));
        return 0;
    }

    // The replay, then each shot with its sidecar (tick and size from the shot's response).
    i64 mark = bmf_arena_mark(&golden_arena);
    bmf_str file;
    if (!golden_read(record_path, &file)) { bmf_fmt_buf(error, error_capacity, "no recording at '%s'", record_path); return 0; }
    golden_write(replay_path, file.ptr, file.len, (i64)offsetof(bmf_replay_header, build_id), 8);
    bmf_arena_pop_to(&golden_arena, mark);
    i32 search = 0;
    for (i32 l = 0; l < label_count; l++) {
        i32 at = bmf_str_find(bmf_str_from_cstr(golden_output), bmf_str_from_cstr("\"width\":"), search);
        i32 tick_at = at < 0 ? -1 : bmf_str_find(bmf_str_from_cstr(golden_output), bmf_str_from_cstr("\"tick\":"), at);
        if (tick_at < 0) { bmf_fmt_buf(error, error_capacity, "shot %s: no response", labels[l]); return 0; }
        search = tick_at + 1;
        i64 tick = 0;
        for (const char* p = golden_output + tick_at + 7; *p >= '0' && *p <= '9'; p++) tick = tick * 10 + (*p - '0');
        char shot_path[1200], png_path[1200], sidecar[256];
        bmf_fmt_buf(shot_path, (i32)sizeof shot_path, "%s/%s.%s.%ihz.%s.png", out_dir, game, stem, rate, labels[l]);
        bmf_fmt_buf(png_path, (i32)sizeof png_path, "%s/golden/d3d11/%s.%ihz.%s.png", tests_dir, stem, rate, labels[l]);
        if (!golden_read(shot_path, &file)) { bmf_fmt_buf(error, error_capacity, "no shot at '%s'", shot_path); return 0; }
        golden_write(png_path, file.ptr, file.len, 0, 0);
        bmf_arena_pop_to(&golden_arena, mark);
        i32 length = bmf_fmt_buf(sidecar, (i32)sizeof sidecar, "replay %s.%ihz.bmfr\ntick %li\nsize %ix%i\n", stem, rate, tick, width, height);
        bmf_fmt_buf(png_path, (i32)sizeof png_path, "%s/golden/d3d11/%s.%ihz.%s.shot", tests_dir, stem, rate, labels[l]);
        golden_write(png_path, sidecar, length, 0, 0);
    }
    return 1;
}

// Checks one .shot golden: plays its replay to the tick, shoots and compares with the .png.
static void golden_check_shot(const char* game, const char* exe, const char* tests_dir, const char* shot_name) {
    i64 start = bmf_platform_time_ns();
    char name[256], path[1200], message[2048];
    i32 stem_length = bmf_cstr_len(shot_name) - 5;   // without ".shot"
    bmf_fmt_buf(name, (i32)sizeof name, "golden.%s.%S", game, bmf_str_make(shot_name, stem_length));
    bmf_fmt_buf(path, (i32)sizeof path, "%s/golden/d3d11/%s", tests_dir, shot_name);
    i64 mark = bmf_arena_mark(&golden_arena);
    bmf_str sidecar;
    char replay[256] = { 0 };
    i64 tick = -1, width = 0, height = 0;
    if (golden_read(path, &sidecar)) {
        for (i32 at = 0; at < sidecar.len;) {
            i32 end = at;
            while (end < sidecar.len && sidecar.ptr[end] != '\n') end++;
            bmf_str line = bmf_str_slice(sidecar, at, end);
            at = end + 1;
            if (bmf_str_starts_with(line, bmf_str_from_cstr("replay "))) bmf_fmt_buf(replay, (i32)sizeof replay, "%S", bmf_str_slice(line, 7, line.len));
            if (bmf_str_starts_with(line, bmf_str_from_cstr("tick "))) bmf_parse_i64(bmf_str_slice(line, 5, line.len), &tick);
            if (bmf_str_starts_with(line, bmf_str_from_cstr("size "))) {
                bmf_str v = bmf_str_slice(line, 5, line.len);
                i32 x = bmf_str_find(v, bmf_str_from_cstr("x"), 0);
                if (x > 0) { bmf_parse_i64(bmf_str_slice(v, 0, x), &width); bmf_parse_i64(bmf_str_slice(v, x + 1, v.len), &height); }
            }
        }
    }
    bmf_arena_pop_to(&golden_arena, mark);
    char replay_path[1200];
    bmf_fmt_buf(replay_path, (i32)sizeof replay_path, "%s/%s", tests_dir, replay);
    bmf_replay_header h;
    char error[512];
    if (!replay[0] || tick < 0 || width <= 0 || height <= 0) {
        bmf_fmt_buf(message, (i32)sizeof message, "%s: needs lines 'replay F', 'tick T', 'size WxH'", path);
        golden_result(name, 0, 0, message);
        return;
    }
    if (bmf_replay_peek(replay_path, &h, error, (i32)sizeof error) != BMF_OK || tick < h.start_tick || tick > h.end_tick) {
        bmf_fmt_buf(message, (i32)sizeof message, "%s: replay '%s': %s", path, replay_path, bmf_replay_peek(replay_path, &h, error, (i32)sizeof error) != BMF_OK ? error : "the tick is outside it");
        golden_result(name, 0, 0, message);
        return;
    }
    char shot_path[1200], args[1400], requests[1400];
    bmf_fmt_buf(shot_path, (i32)sizeof shot_path, "%s/out/test/golden/%s.check.png", golden_root_dir, name);
    bmf_fmt_buf(args, (i32)sizeof args, "--replay \"%s\" --control --headless --size %lix%li", replay_path, width, height);
    i32 length = bmf_fmt_buf(requests, (i32)sizeof requests, "step %li\nshot %s\nquit\n", tick - h.start_tick, shot_path);
    u32 code = golden_spawn(exe, args, requests, length);
    bmf_str png;
    i32 w = 0, hgt = 0;
    if (code != 0 || !golden_read(shot_path, &png)) {
        bmf_fmt_buf(message, (i32)sizeof message, "playing '%s' to tick %li failed (exit %u): %s", replay_path, tick, code, golden_output_tail(3));
        golden_result(name, 0, (bmf_platform_time_ns() - start) / 1000000, message);
        bmf_arena_pop_to(&golden_arena, mark);
        return;
    }
    i64 bytes = width * height * 4;
    u8* actual = (u8*)bmf_arena_push_aligned(&golden_arena, bytes, 64);
    b32 ok = bmf_png_read_stored(png.ptr, png.len, actual, bytes, &w, &hgt) == BMF_OK && w == width && hgt == height;
    char golden_png[1200], out_stem[1200];
    bmf_fmt_buf(golden_png, (i32)sizeof golden_png, "%s/golden/d3d11/%S.png", tests_dir, bmf_str_make(shot_name, stem_length));
    bmf_fmt_buf(out_stem, (i32)sizeof out_stem, "%s/out/test/golden/%s", golden_root_dir, name);
    if (ok) ok = golden_compare_image(golden_png, actual, w, hgt, out_stem, message, (i32)sizeof message);
    else bmf_fmt_buf(message, (i32)sizeof message, "the shot '%s' is not a %lix%li stored-deflate PNG", shot_path, width, height);
    bmf_platform_file_delete(shot_path);
    bmf_arena_pop_to(&golden_arena, mark);
    golden_result(name, ok, (bmf_platform_time_ns() - start) / 1000000, message);
}

// Updates (when selected) and checks the goldens of one game (`parent` "games") or bench scene
// (`parent` "tests/bench").
static void golden_game(const char* parent, const char* game, const char* filter) {
    char tests_dir[1200], exe[1200], name[256], message[4096];
    bmf_fmt_buf(tests_dir, (i32)sizeof tests_dir, "%s/%s/%s/tests", golden_root_dir, parent, game);
    bmf_fmt_buf(exe, (i32)sizeof exe, "%s/out/%s/win64-debug/game.exe", golden_root_dir, game);
    static char names[GOLDEN_MAX_NAMES][128];
    bmf_file_info info;
    if (bmf_platform_file_info(exe, &info) != BMF_OK) {
        bmf_fmt_buf(name, (i32)sizeof name, "golden.%s", game);
        bmf_fmt_buf(message, (i32)sizeof message, "'%s' is not built (build win64 debug %s/%s)", exe, parent, game);
        golden_result(name, 0, 0, message);
        return;
    }

    // Scripts: update the selected ones; a script without recordings fails.
    i32 script_count = golden_list(tests_dir, ".script", 0, names, GOLDEN_MAX_NAMES);
    i32 base = 0;
    for (i32 i = 0; i < script_count; i++) {
        char stem[128];
        bmf_fmt_buf(stem, (i32)sizeof stem, "%S", bmf_str_make(names[i], bmf_cstr_len(names[i]) - 7));
        bmf_fmt_buf(name, (i32)sizeof name, "%s.%s", game, stem);
        char script_path[1200];
        bmf_fmt_buf(script_path, (i32)sizeof script_path, "%s/%s", tests_dir, names[i]);
        if (golden_update_matches(name)) {
            i64 start = bmf_platform_time_ns();
            if (!base) base = golden_game_rate(exe);
            i64 mark = bmf_arena_mark(&golden_arena);
            bmf_str script;
            b32 ok = base > 0 && golden_read(script_path, &script);
            if (!ok) bmf_fmt_buf(message, (i32)sizeof message, base > 0 ? "cannot read '%s'" : "cannot get the tick rate of '%s'", base > 0 ? script_path : exe);
            i32 rates[2] = { base, 120 };
            for (i32 r = 0; r < (base == 120 ? 1 : 2) && ok; r++)
                ok = golden_record(game, exe, tests_dir, stem, script, rates[r], base, message, (i32)sizeof message);
            bmf_arena_pop_to(&golden_arena, mark);
            char update_name[300];
            bmf_fmt_buf(update_name, (i32)sizeof update_name, "golden.%s.update", name);
            golden_result(update_name, ok, (bmf_platform_time_ns() - start) / 1000000, message);
        }
        static char recorded[GOLDEN_MAX_NAMES][128];
        i32 recorded_count = golden_list(tests_dir, ".bmfr", 0, recorded, GOLDEN_MAX_NAMES);
        char prefix[140];
        bmf_fmt_buf(prefix, (i32)sizeof prefix, "%s.", stem);
        b32 has_recording = 0;
        for (i32 k = 0; k < recorded_count; k++) has_recording |= bmf_str_starts_with(bmf_str_from_cstr(recorded[k]), bmf_str_from_cstr(prefix));
        bmf_fmt_buf(name, (i32)sizeof name, "golden.%s.%s", game, stem);
        if (!has_recording && bmf_str_find(bmf_str_from_cstr(name), bmf_str_from_cstr(filter), 0) >= 0) {
            bmf_fmt_buf(message, (i32)sizeof message, "%s has no recordings (create them with build test --update-golden %s.%s)", script_path, game, stem);
            golden_result(name, 0, 0, message);
        }
    }

    // Replays.
    i32 replay_count = golden_list(tests_dir, ".bmfr", 0, names, GOLDEN_MAX_NAMES);
    for (i32 i = 0; i < replay_count; i++) {
        bmf_fmt_buf(name, (i32)sizeof name, "golden.%s.%S", game, bmf_str_make(names[i], bmf_cstr_len(names[i]) - 5));
        if (bmf_str_find(bmf_str_from_cstr(name), bmf_str_from_cstr(filter), 0) < 0) continue;
        i64 start = bmf_platform_time_ns();
        char args[1400];
        bmf_fmt_buf(args, (i32)sizeof args, "--replay \"%s/%s\" --verify", tests_dir, names[i]);
        u32 code = golden_spawn(exe, args, "", 0);
        i32 at = bmf_str_find(bmf_str_from_cstr(golden_output), bmf_str_from_cstr("\"verify\":\"mismatch\""), 0);
        while (at > 0 && golden_output[at - 1] != '\n') at--;
        bmf_fmt_buf(message, (i32)sizeof message, "--replay --verify exit %u: %s", code, at >= 0 ? golden_output + at : golden_output_tail(3));
        golden_result(name, code == 0, (bmf_platform_time_ns() - start) / 1000000, message);
    }

    // Images.
    char golden_dir[1200];
    bmf_fmt_buf(golden_dir, (i32)sizeof golden_dir, "%s/golden/d3d11", tests_dir);
    i32 shot_count = golden_list(golden_dir, ".shot", 0, names, GOLDEN_MAX_NAMES);
    for (i32 i = 0; i < shot_count; i++) {
        bmf_fmt_buf(name, (i32)sizeof name, "golden.%s.%S", game, bmf_str_make(names[i], bmf_cstr_len(names[i]) - 5));
        if (bmf_str_find(bmf_str_from_cstr(name), bmf_str_from_cstr(filter), 0) < 0) continue;
        golden_check_shot(game, exe, tests_dir, names[i]);
    }
}

// Lint over the simulation sources (see the top). One result per scanned file.
static void golden_lint(const char* filter) {
    char dir[1200], path[1200], name[256], report[4096];
    static char names[GOLDEN_MAX_NAMES][128], games[GOLDEN_MAX_NAMES][128];
    // Places: src/physics2d (every file), then games/* and tests/bench/* (marked files only).
    const char* parents[3] = { 0, "games", "tests/bench" };
    for (i32 p = 0; p < 3; p++) {
        i32 dir_count = 1;
        if (p > 0) {
            bmf_fmt_buf(dir, (i32)sizeof dir, "%s/%s", golden_root_dir, parents[p]);
            dir_count = golden_list(dir, "", 1, games, GOLDEN_MAX_NAMES);
        }
        for (i32 d = 0; d < dir_count; d++) {
            if (p == 0) bmf_fmt_buf(dir, (i32)sizeof dir, "%s/src/physics2d", golden_root_dir);
            else bmf_fmt_buf(dir, (i32)sizeof dir, "%s/%s/%s", golden_root_dir, parents[p], games[d]);
            i32 count = golden_list(dir, ".c", 0, names, GOLDEN_MAX_NAMES);
            for (i32 f = 0; f < count; f++) {
                i64 mark = bmf_arena_mark(&golden_arena);
                bmf_fmt_buf(path, (i32)sizeof path, "%s/%s", dir, names[f]);
                bmf_str text;
                if (!golden_read(path, &text)) continue;
                if (p > 0 && bmf_str_find(text, bmf_str_from_cstr("// bmf-lint: sim"), 0) < 0) { bmf_arena_pop_to(&golden_arena, mark); continue; }
                const char* label = path + bmf_cstr_len(golden_root_dir) + 1;
                bmf_fmt_buf(name, (i32)sizeof name, "lint.%s", label);
                if (bmf_str_find(bmf_str_from_cstr(name), bmf_str_from_cstr(filter), 0) >= 0) {
                    report[0] = 0;
                    i32 findings = golden_lint_source(label, (const char*)text.ptr, text.len, report, (i32)sizeof report);
                    golden_result(name, findings == 0, 0, report);
                }
                bmf_arena_pop_to(&golden_arena, mark);
            }
        }
    }
}

// `test.exe --golden [filter]`: lint, then every game and bench scene with a tests/ folder. Exit
// 0 if all passed.
static i32 golden_main(const char* filter) {
    golden_init();
    i64 start = bmf_platform_time_ns();
    golden_lint(filter);
    char dir[1200];
    static char games[GOLDEN_MAX_NAMES][128];
    const char* parents[2] = { "games", "tests/bench" };
    for (i32 p = 0; p < 2; p++) {
        bmf_fmt_buf(dir, (i32)sizeof dir, "%s/%s", golden_root_dir, parents[p]);
        i32 game_count = golden_list(dir, "", 1, games, GOLDEN_MAX_NAMES);
        for (i32 g = 0; g < game_count; g++) {
            char tests_dir[1200];
            bmf_fmt_buf(tests_dir, (i32)sizeof tests_dir, "%s/%s/%s/tests", golden_root_dir, parents[p], games[g]);
            bmf_file_info info;
            if (bmf_platform_file_info(tests_dir, &info) != BMF_OK) continue;
            golden_game(parents[p], games[g], filter);
        }
    }
    char summary[256];
    bmf_fmt_buf(summary, (i32)sizeof summary, "{\"goldens\":%i,\"passed\":%i,\"failed\":%i,\"ms\":%li}\n",
                golden_run, golden_passed, golden_run - golden_passed, (bmf_platform_time_ns() - start) / 1000000);
    test_out(summary);
    return golden_run == golden_passed ? 0 : 1;
}
