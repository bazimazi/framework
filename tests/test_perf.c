// Tests for tools/test/perf.c (task 1A-42): the perf.budget parser and the check of a profile
// report against the limits. The budgets themselves run in `test.exe --perf` (build test's perf
// phase).

// ---------------------------------------------------------------- perf.budget_parse
static void test_perf_budget_parse(bmf_test* t) {
    perf_budget b;
    char error[1024];
    const char* text =
        "# a comment\r\n"
        "\n"
        "replay=tests/a.60hz.bmfr\n"
        "  replay=tests/a.120hz.bmfr  \n"
        "size=320x180\n"
        "zone=tick p99_us<=1500\n"
        "zone=draw2d_flush\tp50_us<=12.5\n"
        "zone=game.update mean_us<=3\n"
        "zone=draw max_us<=1e3\n";
    b32 ok = perf_parse_budget(bmf_str_from_cstr(text), &b, error, (i32)sizeof error);
    bmf_check(t, ok, __FILE__, __LINE__, error);
    bmf_check(t, b.replay_count == 2 && bmf_cstr_eq(b.replays[0], "tests/a.60hz.bmfr") && bmf_cstr_eq(b.replays[1], "tests/a.120hz.bmfr"), __FILE__, __LINE__, "replays");
    bmf_check(t, b.width == 320 && b.height == 180, __FILE__, __LINE__, "size");
    bmf_check_eq_i64(t, b.limit_count, 4, __FILE__, __LINE__, "limits");
    bmf_check(t, bmf_cstr_eq(b.limits[0].zone, "tick") && b.limits[0].stat == PERF_STAT_P99 && b.limits[0].limit_us == 1500.0f && b.limits[0].line == 6,
              __FILE__, __LINE__, "zone=tick p99_us<=1500 on line 6");
    bmf_check(t, bmf_cstr_eq(b.limits[1].zone, "draw2d_flush") && b.limits[1].stat == PERF_STAT_P50 && b.limits[1].limit_us == 12.5f, __FILE__, __LINE__, "tab, p50");
    bmf_check(t, bmf_cstr_eq(b.limits[2].zone, "game.update") && b.limits[2].stat == PERF_STAT_MEAN, __FILE__, __LINE__, "game zone, mean");
    bmf_check(t, b.limits[3].stat == PERF_STAT_MAX && b.limits[3].limit_us == 1000.0f, __FILE__, __LINE__, "max, exponent");
    const char* defaults = "replay=r.bmfr\nzone=tick p99_us<=5";
    bmf_check(t, perf_parse_budget(bmf_str_from_cstr(defaults), &b, error, (i32)sizeof error) && b.width == 1280 && b.height == 720,
              __FILE__, __LINE__, "default size, no final newline");

    struct { const char* text; const char* expected; } bad[] = {
        { "replay=r.bmfr\nzone=tick p98_us<=5\n", "line 2: 'zone=tick p98_us<=5': expected zone=<name> <stat><=<microseconds>" },
        { "replay=r.bmfr\nzone=tick p99_us<5\n", "line 2:" },
        { "replay=r.bmfr\nzone=tick p99_us<=0\n", "line 2:" },
        { "replay=r.bmfr\nzone=tick p99_us<=-1\n", "line 2:" },
        { "replay=r.bmfr\nzone=tick p99_us<=ten\n", "line 2:" },
        { "replay=r.bmfr\nzone= p99_us<=5\n", "line 2:" },
        { "replay=r.bmfr\nzone=tick\n", "line 2:" },
        { "replay=\nzone=tick p99_us<=5\n", "line 1: replay=<path>" },
        { "size=10x10\nreplay=r.bmfr\nzone=tick p99_us<=5\n", "line 1: size=<W>x<H>, each 16..8192" },
        { "replay=r.bmfr\nzone=tick p99_us<=5\nbudget tick 5\n", "line 3: 'budget tick 5': expected replay=, size= or zone=" },
        { "zone=tick p99_us<=5\n", "needs at least one replay= line and one zone= line" },
        { "replay=r.bmfr\n# zone=tick p99_us<=5\n", "needs at least one replay= line and one zone= line" },
    };
    for (i32 c = 0; c < (i32)(sizeof bad / sizeof bad[0]); c++) {
        error[0] = 0;
        b32 parsed = perf_parse_budget(bmf_str_from_cstr(bad[c].text), &b, error, (i32)sizeof error);
        char message[1400];
        bmf_fmt_buf(message, (i32)sizeof message, "case %i: expected \"%s\" in \"%s\"", c, bad[c].expected, error);
        bmf_check(t, !parsed && bmf_str_starts_with(bmf_str_from_cstr(error), bmf_str_from_cstr(bad[c].expected)), __FILE__, __LINE__, message);
    }
}

// ---------------------------------------------------------------- perf.check
static void test_perf_check(bmf_test* t) {
    // A report as the runtime prints it with --profile (without the "event" key).
    const char* report =
        "{\"ticks\":600,\"zones\":{\"tick\":{\"calls\":600,\"mean_us\":0.5,\"p50_us\":0.4,\"p99_us\":1.25,\"max_us\":9},"
        "\"draw\":{\"calls\":600,\"mean_us\":110,\"p50_us\":100,\"p99_us\":812.5,\"max_us\":1.5e3},"
        "\"draw2d_flush\":{\"calls\":600,\"mean_us\":50,\"p50_us\":48,\"p99_us\":60,\"max_us\":70}},"
        "\"frames\":600,\"frame_ms\":{\"mean\":0.2,\"p50\":0.2,\"p99\":0.9,\"max\":1.6},\"draw_calls\":32,\"instances\":10000,\"nesting_errors\":0,\"counter_hz\":3000000000}";
    f32 value = 0.0f;
    bmf_check(t, perf_zone_value(bmf_str_from_cstr(report), "draw", PERF_STAT_P99, &value) && value == 812.5f, __FILE__, __LINE__, "draw p99");
    bmf_check(t, perf_zone_value(bmf_str_from_cstr(report), "draw", PERF_STAT_MAX, &value) && value == 1500.0f, __FILE__, __LINE__, "draw max (exponent)");
    bmf_check(t, perf_zone_value(bmf_str_from_cstr(report), "tick", PERF_STAT_MAX, &value) && value == 9.0f, __FILE__, __LINE__, "last key of a zone");
    bmf_check(t, !perf_zone_value(bmf_str_from_cstr(report), "draw2d", PERF_STAT_P99, &value), __FILE__, __LINE__, "a name prefix is not a zone");
    bmf_check(t, !perf_zone_value(bmf_str_from_cstr(report), "frame_ms", PERF_STAT_P99, &value), __FILE__, __LINE__, "only zones count");
    bmf_check(t, !perf_zone_value(bmf_str_from_cstr("{\"ticks\":0,\"zones\":{},\"frames\":0}"), "tick", PERF_STAT_P99, &value), __FILE__, __LINE__, "no zones");

    perf_budget b;
    char error[512], message[1024], array[2048];
    const char* text = "replay=r.bmfr\nzone=tick p99_us<=5\nzone=draw p99_us<=500\nzone=draw2d_flush p99_us<=250\nzone=physics p99_us<=100\nzone=draw p50_us<=100\n";
    bmf_check(t, perf_parse_budget(bmf_str_from_cstr(text), &b, error, (i32)sizeof error), __FILE__, __LINE__, error);
    bmf_json_writer w;
    bmf_json_init(&w, array, (i32)sizeof array - 1);
    i32 failed = perf_check(&b, bmf_str_from_cstr(report), &w, message, (i32)sizeof message);
    array[w.len] = 0;
    bmf_check_eq_i64(t, failed, 2, __FILE__, __LINE__, message);
    bmf_check(t, bmf_cstr_eq(message, "draw p99_us 812.5 > 500 (perf.budget line 3); zone 'physics' never ran (perf.budget line 5)"), __FILE__, __LINE__, message);
    bmf_check(t, bmf_str_starts_with(bmf_str_from_cstr(array), bmf_str_from_cstr("[{\"zone\":\"tick\",\"stat\":\"p99_us\",\"limit_us\":5,\"value_us\":1.25,\"ok\":true},{\"zone\":\"draw\",\"stat\":\"p99_us\",\"limit_us\":500,\"value_us\":812.5,\"ok\":false}")) &&
                 bmf_str_find(bmf_str_from_cstr(array), bmf_str_from_cstr("{\"zone\":\"physics\",\"stat\":\"p99_us\",\"limit_us\":100,\"value_us\":null,\"ok\":false}"), 0) > 0 &&
                 bmf_str_ends_with(bmf_str_from_cstr(array), bmf_str_from_cstr("{\"zone\":\"draw\",\"stat\":\"p50_us\",\"limit_us\":100,\"value_us\":100,\"ok\":true}]")),
              __FILE__, __LINE__, array);
    // The value equal to the limit passes (the limit is inclusive: <=).
    const char* exact = "replay=r.bmfr\nzone=draw p99_us<=812.5\n";
    perf_parse_budget(bmf_str_from_cstr(exact), &b, error, (i32)sizeof error);
    bmf_json_init(&w, array, (i32)sizeof array - 1);
    bmf_check_eq_i64(t, perf_check(&b, bmf_str_from_cstr(report), &w, message, (i32)sizeof message), 0, __FILE__, __LINE__, "inclusive limit");
    bmf_check(t, message[0] == 0, __FILE__, __LINE__, "no message when all pass");
}
