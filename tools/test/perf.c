// perf.c: perf budgets and the benchmark history (SPEC §15 points 5 and 6, task 1A-42), part of
// the test runner (test_main.c, after golden.c, whose process and file helpers it uses).
//
// A game or bench scene opts in with a perf.budget file (games/<g>/perf.budget,
// tests/bench/<scene>/perf.budget). Lines ('#' starts a comment, blank lines are skipped):
//   replay=<path>              a benchmark replay, relative to the game's folder (1..8 of them)
//   size=<W>x<H>               the headless target for the runs (default 1280x720)
//   zone=<name> <stat><=<N>    a limit: zone <name>'s <stat> must be at most N microseconds; <stat>
//                              is p99_us (SPEC's form), p50_us, mean_us or max_us (1..32 limits)
// `test.exe --perf [filter]` (run by `build test` after building each such folder in release with
// BMF_AGENT=1, out/<g>/win64-release-agent) plays every replay with
//   game.exe --headless --no-gpu --replay F --profile --size WxH
// (one tick and one draw per frame on the null GPU backend, as fast as possible; the stats cover the whole run: per
// call, p50/p99 over the last 1024 calls, src/debug/profile.c) and checks every limit against the
// report. A run over budget is repeated, up to 3 runs; the replay passes when one run meets every
// limit (transient load from outside does not fail it, a regression fails all three). One JSON
// line per replay, for its last run:
//   {"test":"perf.<g>.<replay stem>","ok":B,"ms":T,"runs":R,"budgets":[{"zone":Z,"stat":S,"limit_us":L,
//    "value_us":V,"ok":B},...],"profile":{zones, frames, frame_ms, draw_calls, ...},"msg":M}
// "profile" is the breakdown (every zone's calls, mean, p50, p99, max); "msg" (failures only)
// names each limit that was exceeded, e.g. "draw p99_us 812.5 > 500 (perf.budget line 4)". A zone
// that never ran fails its limits. Then {"perf":N,"passed":P,"failed":F,"ms":T}.
// Every run (each repeat too) is appended to out/bench/history.jsonl:
//   {"time":"2026-11-22T10:11:12Z","rev":R,"scene":G,"replay":F,"size":"WxH","ok":B,"profile":{...}}
// with "rev" the git commit of HEAD when .git can be read (no git process), else no "rev" key.
//
// Windows only so far (process spawning and directory listing come from golden.c).

enum {
    PERF_MAX_REPLAYS = 8,
    PERF_MAX_LIMITS = 32,
    PERF_PATH_MAX = 256,
    PERF_RUNS = 3,                // runs of a replay at most; it passes when one run meets every limit
};

typedef enum perf_stat { PERF_STAT_P99 = 0, PERF_STAT_P50, PERF_STAT_MEAN, PERF_STAT_MAX, PERF_STAT_COUNT } perf_stat;
static const char* const PERF_STAT_KEYS[PERF_STAT_COUNT] = { "p99_us", "p50_us", "mean_us", "max_us" };

typedef struct perf_limit {
    char zone[64];
    perf_stat stat;
    f32 limit_us;
    i32 line;            // in perf.budget, for messages
} perf_limit;

typedef struct perf_budget {
    char replays[PERF_MAX_REPLAYS][PERF_PATH_MAX];
    i32 replay_count;
    i32 width, height;
    perf_limit limits[PERF_MAX_LIMITS];
    i32 limit_count;
} perf_budget;

// ---------------------------------------------------------------- the budget file
// Parses a perf.budget text into *out. Returns 1, or 0 with "line N: ..." in `error`.
static b32 perf_parse_budget(bmf_str text, perf_budget* out, char* error, i32 error_capacity) {
    bmf_memset(out, 0, sizeof *out);
    out->width = 1280;
    out->height = 720;
    i32 line_number = 0;
    for (i32 at = 0; at < text.len;) {
        i32 end = at;
        while (end < text.len && text.ptr[end] != '\n') end++;
        bmf_str line = bmf_str_slice(text, at, end);
        at = end + 1;
        line_number++;
        while (line.len > 0 && (line.ptr[0] == ' ' || line.ptr[0] == '\t')) line = bmf_str_slice(line, 1, line.len);
        while (line.len > 0 && (line.ptr[line.len - 1] == '\r' || line.ptr[line.len - 1] == ' ' || line.ptr[line.len - 1] == '\t')) line.len--;
        if (line.len == 0 || line.ptr[0] == '#') continue;
        if (bmf_str_starts_with(line, bmf_str_from_cstr("replay="))) {
            bmf_str path = bmf_str_slice(line, 7, line.len);
            if (path.len == 0 || path.len >= PERF_PATH_MAX || out->replay_count >= PERF_MAX_REPLAYS) {
                bmf_fmt_buf(error, error_capacity, "line %i: replay=<path> (1..%i bytes, at most %i replays)", line_number, PERF_PATH_MAX - 1, PERF_MAX_REPLAYS);
                return 0;
            }
            bmf_fmt_buf(out->replays[out->replay_count++], PERF_PATH_MAX, "%S", path);
            continue;
        }
        if (bmf_str_starts_with(line, bmf_str_from_cstr("size="))) {
            bmf_str v = bmf_str_slice(line, 5, line.len);
            i32 x = bmf_str_find_byte(v, 'x', 0);
            i32 w = 0, h = 0;
            if (x < 0 || bmf_parse_i32(bmf_str_slice(v, 0, x), &w) != BMF_OK || bmf_parse_i32(bmf_str_slice(v, x + 1, v.len), &h) != BMF_OK ||
                w < 16 || h < 16 || w > 8192 || h > 8192) {
                bmf_fmt_buf(error, error_capacity, "line %i: size=<W>x<H>, each 16..8192", line_number);
                return 0;
            }
            out->width = w;
            out->height = h;
            continue;
        }
        if (bmf_str_starts_with(line, bmf_str_from_cstr("zone="))) {
            // zone=<name> <stat><=<N>: two tokens separated by spaces or tabs.
            i32 space = 5;
            while (space < line.len && line.ptr[space] != ' ' && line.ptr[space] != '\t') space++;
            bmf_str name = bmf_str_slice(line, 5, space);
            i32 rest_at = space;
            while (rest_at < line.len && (line.ptr[rest_at] == ' ' || line.ptr[rest_at] == '\t')) rest_at++;
            bmf_str rest = bmf_str_slice(line, rest_at, line.len);
            i32 op = bmf_str_find(rest, bmf_str_from_cstr("<="), 0);
            i32 stat = -1;
            if (op > 0)
                for (i32 s = 0; s < PERF_STAT_COUNT; s++)
                    if (bmf_str_eq(bmf_str_slice(rest, 0, op), bmf_str_from_cstr(PERF_STAT_KEYS[s]))) stat = s;
            f32 limit = 0.0f;
            b32 ok = name.len >= 1 && name.len <= 63 && stat >= 0 &&
                     bmf_parse_f32(bmf_str_slice(rest, op + 2, rest.len), &limit) == BMF_OK && limit > 0.0f && limit < 1e9f;
            if (!ok || out->limit_count >= PERF_MAX_LIMITS) {
                bmf_fmt_buf(error, error_capacity, "line %i: '%S': expected zone=<name> <stat><=<microseconds> with stat p99_us, p50_us, mean_us or max_us (at most %i limits)",
                            line_number, line, PERF_MAX_LIMITS);
                return 0;
            }
            perf_limit* l = &out->limits[out->limit_count++];
            bmf_fmt_buf(l->zone, (i32)sizeof l->zone, "%S", name);
            l->stat = (perf_stat)stat;
            l->limit_us = limit;
            l->line = line_number;
            continue;
        }
        bmf_fmt_buf(error, error_capacity, "line %i: '%S': expected replay=, size= or zone= (or # for a comment)", line_number, line);
        return 0;
    }
    if (out->replay_count == 0 || out->limit_count == 0) {
        bmf_fmt_buf(error, error_capacity, "needs at least one replay= line and one zone= line");
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------- the report
// The value of `stat` of zone `zone` in a profile report (the {"event":"profile",...} line).
// Returns 0 when the zone is not in the report (it never ran) or the value cannot be read.
static b32 perf_zone_value(bmf_str report, const char* zone, perf_stat stat, f32* out) {
    i32 zones = bmf_str_find(report, bmf_str_from_cstr("\"zones\":{"), 0);
    if (zones < 0) return 0;
    i32 zones_end = bmf_str_find(report, bmf_str_from_cstr("\"frames\":"), zones);   // the key after "zones"
    if (zones_end < 0) zones_end = report.len;
    char pattern[80];
    bmf_fmt_buf(pattern, (i32)sizeof pattern, "\"%s\":{", zone);
    i32 at = bmf_str_find(report, bmf_str_from_cstr(pattern), zones + 9);
    if (at < 0 || at >= zones_end) return 0;
    i32 object_end = bmf_str_find_byte(report, '}', at);
    bmf_fmt_buf(pattern, (i32)sizeof pattern, "\"%s\":", PERF_STAT_KEYS[stat]);
    i32 key = bmf_str_find(report, bmf_str_from_cstr(pattern), at);
    if (key < 0 || key > object_end) return 0;
    i32 start = key + bmf_cstr_len(pattern), end = start;
    while (end < report.len && report.ptr[end] != ',' && report.ptr[end] != '}') end++;
    return bmf_parse_f32(bmf_str_slice(report, start, end), out) == BMF_OK;
}

// Checks every limit of `budget` against `report`: writes the results as a JSON array to `w` and
// the exceeded limits to `message` ("" when none). Returns the number of exceeded limits.
static i32 perf_check(const perf_budget* budget, bmf_str report, bmf_json_writer* w, char* message, i32 message_capacity) {
    i32 failed = 0, used = 0;
    message[0] = 0;
    bmf_json_array_begin(w);
    for (i32 i = 0; i < budget->limit_count; i++) {
        const perf_limit* l = &budget->limits[i];
        f32 value = 0.0f;
        b32 ran = perf_zone_value(report, l->zone, l->stat, &value);
        b32 ok = ran && value <= l->limit_us;
        bmf_json_object_begin(w);
        bmf_json_key(w, "zone"); bmf_json_string(w, l->zone);
        bmf_json_key(w, "stat"); bmf_json_string(w, PERF_STAT_KEYS[l->stat]);
        bmf_json_key(w, "limit_us"); bmf_json_f32(w, l->limit_us);
        bmf_json_key(w, "value_us");
        if (ran) bmf_json_f32(w, value);
        else bmf_json_null(w);
        bmf_json_key(w, "ok"); bmf_json_bool(w, ok);
        bmf_json_object_end(w);
        if (ok) continue;
        failed++;
        char value_text[32], limit_text[32];
        bmf_f32_to_text(value, value_text, (i32)sizeof value_text);
        bmf_f32_to_text(l->limit_us, limit_text, (i32)sizeof limit_text);
        if (ran) used += bmf_fmt_buf(message + used, message_capacity - used, "%s%s %s %s > %s (perf.budget line %i)", used ? "; " : "",
                                     l->zone, PERF_STAT_KEYS[l->stat], value_text, limit_text, l->line);
        else used += bmf_fmt_buf(message + used, message_capacity - used, "%szone '%s' never ran (perf.budget line %i)", used ? "; " : "", l->zone, l->line);
    }
    bmf_json_array_end(w);
    return failed;
}

// ---------------------------------------------------------------- history
// The commit of HEAD from <root>/.git (HEAD, then the ref file or packed-refs): 40 hex digits in
// `out`, or 0 when it cannot be read.
static b32 perf_git_revision(char* out, i32 capacity) {
    char path[1200];
    bmf_fmt_buf(path, (i32)sizeof path, "%s/.git/HEAD", golden_root_dir);
    i64 mark = bmf_arena_mark(&golden_arena);
    bmf_str head, text;
    b32 found = 0;
    bmf_str hash = { 0, 0 };
    if (golden_read(path, &head)) {
        while (head.len > 0 && (head.ptr[head.len - 1] == '\n' || head.ptr[head.len - 1] == '\r' || head.ptr[head.len - 1] == ' ')) head.len--;
        if (bmf_str_starts_with(head, bmf_str_from_cstr("ref: "))) {
            bmf_str ref = bmf_str_slice(head, 5, head.len);
            bmf_fmt_buf(path, (i32)sizeof path, "%s/.git/%S", golden_root_dir, ref);
            if (golden_read(path, &text) && text.len >= 40) hash = bmf_str_slice(text, 0, 40);
            else {
                bmf_fmt_buf(path, (i32)sizeof path, "%s/.git/packed-refs", golden_root_dir);
                if (golden_read(path, &text)) {
                    char suffix[300];
                    bmf_fmt_buf(suffix, (i32)sizeof suffix, " %S\n", ref);
                    i32 at = bmf_str_find(text, bmf_str_from_cstr(suffix), 0);
                    if (at >= 40) hash = bmf_str_slice(text, at - 40, at);
                }
            }
        } else if (head.len >= 40) {
            hash = bmf_str_slice(head, 0, 40);   // detached HEAD
        }
    }
    if (hash.len == 40 && capacity > 40) {
        found = 1;
        for (i32 i = 0; i < 40; i++) {
            u8 c = hash.ptr[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) found = 0;
        }
        if (found) bmf_fmt_buf(out, capacity, "%S", hash);
    }
    bmf_arena_pop_to(&golden_arena, mark);
    return found;
}

// Appends one line to out/bench/history.jsonl.
static void perf_history_append(const char* scene, const char* replay, const perf_budget* budget, b32 ok, const char* report) {
    static char line[48 * 1024];
    i64 now_ms = bmf_platform_utc_ns() / 1000000;
    i64 seconds = now_ms / 1000;
    i32 year, month, day;
    crash_civil_date(seconds / 86400, &year, &month, &day);
    i32 second_of_day = (i32)(seconds % 86400);
    char time[40], size[32], revision[48];
    bmf_fmt_buf(time, (i32)sizeof time, "%u-%02u-%02uT%02u:%02u:%02uZ", (u32)year, (u32)month, (u32)day,
                (u32)(second_of_day / 3600), (u32)(second_of_day / 60 % 60), (u32)(second_of_day % 60));
    bmf_fmt_buf(size, (i32)sizeof size, "%ix%i", budget->width, budget->height);
    bmf_json_writer w;
    bmf_json_init(&w, line, (i32)sizeof line - 1);
    bmf_json_object_begin(&w);
    bmf_json_key(&w, "time"); bmf_json_string(&w, time);
    if (perf_git_revision(revision, (i32)sizeof revision)) { bmf_json_key(&w, "rev"); bmf_json_string(&w, revision); }
    bmf_json_key(&w, "scene"); bmf_json_string(&w, scene);
    bmf_json_key(&w, "replay"); bmf_json_string(&w, replay);
    bmf_json_key(&w, "size"); bmf_json_string(&w, size);
    bmf_json_key(&w, "ok"); bmf_json_bool(&w, ok);
    bmf_json_key(&w, "profile"); bmf_json_raw(&w, report);
    bmf_json_object_end(&w);
    line[w.len] = '\n';
    char dir[1200], path[1200];
    bmf_fmt_buf(dir, (i32)sizeof dir, "%s/out/bench", golden_root_dir);
    bmf_platform_dir_create(dir);
    bmf_fmt_buf(path, (i32)sizeof path, "%s/history.jsonl", dir);
    i64 mark = bmf_arena_mark(&golden_arena);
    bmf_str old = { 0, 0 };
    if (!golden_read(path, &old)) old.len = 0;
    const void* parts[2] = { old.ptr, line };
    i64 sizes[2] = { old.len, (i64)w.len + 1 };
    bmf_platform_file_write_atomic_parts(path, parts, sizes, 2);
    bmf_arena_pop_to(&golden_arena, mark);
}

// ---------------------------------------------------------------- running
static i32 perf_run, perf_passed;

// Runs the budgets of one game (`parent` "games") or bench scene (`parent` "tests/bench").
static void perf_scene(const char* parent, const char* scene, const char* filter) {
    static char line[64 * 1024], message[4096], report[40 * 1024];
    char dir[1200], path[1200], exe[1200], name[300], error[1024];
    bmf_fmt_buf(dir, (i32)sizeof dir, "%s/%s/%s", golden_root_dir, parent, scene);
    bmf_fmt_buf(path, (i32)sizeof path, "%s/perf.budget", dir);
    bmf_fmt_buf(exe, (i32)sizeof exe, "%s/out/%s/win64-release-agent/game.exe", golden_root_dir, scene);
    static perf_budget budget;
    i64 mark = bmf_arena_mark(&golden_arena);
    bmf_str text;
    bmf_fmt_buf(error, (i32)sizeof error, "cannot be read");
    b32 parsed = golden_read(path, &text) && perf_parse_budget(text, &budget, error, (i32)sizeof error);
    bmf_arena_pop_to(&golden_arena, mark);
    bmf_file_info info;
    if (!parsed || bmf_platform_file_info(exe, &info) != BMF_OK) {
        bmf_fmt_buf(name, (i32)sizeof name, "perf.%s", scene);
        if (bmf_str_find(bmf_str_from_cstr(name), bmf_str_from_cstr(filter), 0) < 0) return;
        if (parsed) bmf_fmt_buf(message, (i32)sizeof message, "'%s' is not built (build win64 release %s/%s --agent)", exe, parent, scene);
        else bmf_fmt_buf(message, (i32)sizeof message, "%s/%s/perf.budget: %s", parent, scene, error);
        golden_result(name, 0, 0, message);   // counted by golden_result, see perf_main
        return;
    }
    for (i32 r = 0; r < budget.replay_count; r++) {
        // Name: perf.<scene>.<replay file name without folders and .bmfr>.
        bmf_str replay = bmf_str_from_cstr(budget.replays[r]);
        i32 slash = replay.len;
        while (slash > 0 && replay.ptr[slash - 1] != '/' && replay.ptr[slash - 1] != '\\') slash--;
        bmf_str stem = bmf_str_slice(replay, slash, replay.len);
        if (bmf_str_ends_with(stem, bmf_str_from_cstr(".bmfr"))) stem.len -= 5;
        bmf_fmt_buf(name, (i32)sizeof name, "perf.%s.%S", scene, stem);
        if (bmf_str_find(bmf_str_from_cstr(name), bmf_str_from_cstr(filter), 0) < 0) continue;
        i64 start = bmf_platform_time_ns();
        char args[1600];
        bmf_fmt_buf(args, (i32)sizeof args, "--headless --no-gpu --replay \"%s/%s\" --profile --size %ix%i", dir, budget.replays[r], budget.width, budget.height);
        // Up to PERF_RUNS runs: a run over budget is repeated, so a burst of load from outside
        // (a virus scan of a fresh exe, another process) does not fail the test; a real
        // regression is over budget in every run. The last run is reported; each is in the history.
        static char budgets_text[8192];
        u32 code = 0;
        i32 failed = 1, runs = 0;
        while (failed && runs < PERF_RUNS) {
            runs++;
            code = golden_spawn(exe, args, "", 0);
            // The report line, without its "event" key: {"ticks":...,"zones":{...},...}.
            bmf_str output = bmf_str_from_cstr(golden_output);
            i32 at = bmf_str_find(output, bmf_str_from_cstr("{\"event\":\"profile\","), 0);
            report[0] = 0;
            if (at >= 0) {
                i32 end = bmf_str_find_byte(output, '\n', at);
                if (end < 0) end = output.len;
                bmf_fmt_buf(report, (i32)sizeof report, "{%S", bmf_str_slice(output, at + 19, end));
            }
            if (code != 0 || !report[0]) {
                bmf_fmt_buf(message, (i32)sizeof message, "'%s %s' exit %u without a profile report: %s", exe, args, code, golden_output_tail(3));
                break;   // not a timing problem: no repeat
            }
            bmf_json_writer budgets;
            bmf_json_init(&budgets, budgets_text, (i32)sizeof budgets_text - 1);
            failed = perf_check(&budget, bmf_str_from_cstr(report), &budgets, message, (i32)sizeof message);
            budgets_text[budgets.len] = 0;
            perf_history_append(scene, budget.replays[r], &budget, failed == 0, report);
        }
        bmf_json_writer w;
        bmf_json_init(&w, line, (i32)sizeof line - 2);
        bmf_json_object_begin(&w);
        bmf_json_key(&w, "test"); bmf_json_string(&w, name);
        bmf_json_key(&w, "ok"); bmf_json_i64(&w, failed == 0);
        bmf_json_key(&w, "ms"); bmf_json_i64(&w, (bmf_platform_time_ns() - start) / 1000000);
        bmf_json_key(&w, "runs"); bmf_json_i64(&w, runs);
        if (code == 0 && report[0]) {
            bmf_json_key(&w, "budgets"); bmf_json_raw(&w, budgets_text);
            bmf_json_key(&w, "profile"); bmf_json_raw(&w, report);
        }
        if (failed) { bmf_json_key(&w, "msg"); bmf_json_string(&w, message); }
        bmf_json_object_end(&w);
        line[w.len] = '\n';
        line[w.len + 1] = 0;
        test_out(line);
        perf_run++;
        perf_passed += failed == 0;
    }
}

// `test.exe --perf [filter]`: every game and bench scene with a perf.budget. Exit 0 if all passed.
static i32 perf_main(const char* filter) {
    golden_init();
    i64 start = bmf_platform_time_ns();
    char dir[1200];
    static char scenes[GOLDEN_MAX_NAMES][128];
    const char* parents[2] = { "games", "tests/bench" };
    for (i32 p = 0; p < 2; p++) {
        bmf_fmt_buf(dir, (i32)sizeof dir, "%s/%s", golden_root_dir, parents[p]);
        i32 count = golden_list(dir, "", 1, scenes, GOLDEN_MAX_NAMES);
        for (i32 s = 0; s < count; s++) {
            char budget_path[1200];
            bmf_fmt_buf(budget_path, (i32)sizeof budget_path, "%s/%s/perf.budget", dir, scenes[s]);
            bmf_file_info info;
            if (bmf_platform_file_info(budget_path, &info) != BMF_OK) continue;
            perf_scene(parents[p], scenes[s], filter);
        }
    }
    // Budget files that could not be used are reported through golden_result (counted there).
    i32 run = perf_run + golden_run, passed = perf_passed + golden_passed;
    char summary[256];
    bmf_fmt_buf(summary, (i32)sizeof summary, "{\"perf\":%i,\"passed\":%i,\"failed\":%i,\"ms\":%li}\n",
                run, passed, run - passed, (bmf_platform_time_ns() - start) / 1000000);
    test_out(summary);
    return run == passed ? 0 : 1;
}
