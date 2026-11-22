// Tests for src/debug/shot.c and the `shot` command (task 1A-32): the image diff, `--size`, and
// real shots of the protocol test game (tests/test_protocol.c: a red 1 m circle at
// (values[0], 0)) rendered headless on WARP, written as PNG, read back and checked.

// ---------------------------------------------------------------- shot.diff
static void test_shot_diff(bmf_test* t) {
    static u8 a[16 * 8 * 4], b[16 * 8 * 4], diff[16 * 8 * 4];
    png_test_fill(a, 16, 8, 11);
    bmf_memcpy(b, a, sizeof a);
    bmf_image_diff_result r = bmf_image_diff(a, b, 16, 8, 0, diff);
    bmf_check(t, r.pixels == 128 && r.differing == 0 && r.max_channel_diff == 0 && r.first_x == -1 && r.first_y == -1 && r.differing_fraction == 0.0,
              __FILE__, __LINE__, "identical images");
    bmf_check(t, diff[3] == 255 && diff[0] == diff[1] && diff[1] == diff[2] && diff[0] <= 85, __FILE__, __LINE__, "unchanged pixels: dim gray");

    // One pixel, one channel, off by one.
    b[(3 * 16 + 5) * 4 + 2] ^= 1;
    r = bmf_image_diff(a, b, 16, 8, 0, diff);
    bmf_check(t, r.differing == 1 && r.first_x == 5 && r.first_y == 3 && r.max_channel_diff == 1, __FILE__, __LINE__, "one-pixel change found");
    bmf_check(t, r.differing_fraction == 1.0 / 128.0, __FILE__, __LINE__, "fraction 1/128");
    const u8* marked = &diff[(3 * 16 + 5) * 4];
    bmf_check(t, marked[0] == 255 && marked[1] == 0 && marked[2] == 0 && marked[3] == 255, __FILE__, __LINE__, "the differing pixel is red in the diff image");
    r = bmf_image_diff(a, b, 16, 8, 1, 0);
    bmf_check(t, r.differing == 0 && r.max_channel_diff == 1, __FILE__, __LINE__, "within tolerance 1; no diff image needed");

    // Alpha counts, and the largest difference is reported.
    b[0 * 4 + 3] = (u8)(a[3] ^ 0x80);
    r = bmf_image_diff(a, b, 16, 8, 1, 0);
    bmf_check(t, r.differing == 1 && r.first_x == 0 && r.first_y == 0 && r.max_channel_diff == 128, __FILE__, __LINE__, "alpha difference");

    // The diff image survives a PNG round trip (it is what golden tests write).
    i64 size = bmf_png_write(diff, 16, 8, png_test_file, (i64)sizeof png_test_file);
    i32 w, h;
    bmf_check_eq_i64(t, bmf_png_read_stored(png_test_file, size, png_test_decoded, (i64)sizeof png_test_decoded, &w, &h), BMF_OK, __FILE__, __LINE__, "diff PNG");
    bmf_check_eq_mem(t, png_test_decoded, diff, (i64)sizeof diff, __FILE__, __LINE__, "diff PNG round trip");
}

// ---------------------------------------------------------------- shot.size_flag
static void test_shot_size_flag(bmf_test* t) {
    bmf_core_options options;
    char error[BMF_CORE_ERROR_MAX];
    const char* good[] = { "game.exe", "--size", "320x180" };
    bmf_check_eq_i64(t, bmf_core_parse_args(3, good, &options, error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, error);
    bmf_check(t, options.width == 320 && options.height == 180, __FILE__, __LINE__, "--size 320x180");
    const char* defaults[] = { "game.exe" };
    bmf_check_eq_i64(t, bmf_core_parse_args(1, defaults, &options, error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, error);
    bmf_check(t, options.width == 0 && options.height == 0, __FILE__, __LINE__, "no --size: 0 (default 1280x720)");
    const char* bad[] = { "10x10", "320", "axb", "320x", "x180", "8193x100", "320x180x2", "-320x180" };
    for (i32 i = 0; i < (i32)(sizeof bad / sizeof bad[0]); i++) {
        const char* args[] = { "game.exe", "--size", bad[i] };
        bmf_err err = bmf_core_parse_args(3, args, &options, error, (i32)sizeof error);
        bmf_check(t, err == BMF_ERR_INVALID && core_test_contains(error, "expected WxH, each 16..8192"), __FILE__, __LINE__, bad[i]);
    }
}

// ---------------------------------------------------------------- shot.capture
static void test_shot_capture(bmf_test* t) {
    bmf_game_desc game = protocol_test_game();
    char error[BMF_CORE_ERROR_MAX];
    bmf_core_options options;
    bmf_memset(&options, 0, sizeof options);
    options.time_scale = 1.0f;
    options.headless = 1;
    options.control = 1;
    options.width = 320;
    options.height = 180;
    bmf_check_eq_i64(t, bmf_core_start(&game, &options, error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, error);
    bmf_check_eq_i64(t, bmf_core_display_open(error, (i32)sizeof error), BMF_OK, __FILE__, __LINE__, error);
    bmf_check(t, bmf_core_view().width == 320 && bmf_core_view().height == 180, __FILE__, __LINE__, "--size sets the headless target");
    bmf_protocol_begin(protocol_test_output, 0);

    char dir[1024];
    bmf_check(t, bmf_platform_exe_dir(dir, (i32)sizeof dir) > 0, __FILE__, __LINE__, "exe dir");
    char request[1400], expected[1400], path[1200];
    bmf_fmt_buf(path, (i32)sizeof path, "%sshot_test.png", dir);
    i64 png_size = bmf_png_write_size(320, 180);

    // Full view: the circle (values[0] = 0.5: 32 px right of the center, radius 64 px).
    bmf_fmt_buf(request, (i32)sizeof request, "shot %s", path);
    bmf_json_writer w;
    bmf_json_init(&w, expected, (i32)sizeof expected);
    bmf_json_object_begin(&w);
    bmf_json_key(&w, "ok"); bmf_json_i64(&w, 1);
    bmf_json_key(&w, "path"); bmf_json_string(&w, path);
    bmf_json_key(&w, "width"); bmf_json_i64(&w, 320);
    bmf_json_key(&w, "height"); bmf_json_i64(&w, 180);
    bmf_json_key(&w, "bytes"); bmf_json_i64(&w, png_size);
    bmf_json_key(&w, "tick"); bmf_json_i64(&w, 0);
    bmf_json_object_end(&w);
    expected[w.len] = '\n';
    expected[w.len + 1] = 0;
    protocol_expect(t, request, expected, __LINE__);

    bmf_arena* arena = &bmf_core_memory()->permanent;
    i64 mark = bmf_arena_mark(arena);
    bmf_str file;
    bmf_check_eq_i64(t, bmf_platform_file_read(path, arena, &file), BMF_OK, __FILE__, __LINE__, "read the shot back");
    i32 width = 0, height = 0;
    bmf_check_eq_i64(t, bmf_png_read_stored(file.ptr, file.len, png_test_decoded, (i64)sizeof png_test_decoded, &width, &height), BMF_OK, __FILE__, __LINE__, "decode the shot");
    bmf_check(t, width == 320 && height == 180, __FILE__, __LINE__, "320x180");
    const u8* circle = &png_test_decoded[(90 * 320 + 192) * 4];
    const u8* corner = &png_test_decoded[(5 * 320 + 5) * 4];
    bmf_check(t, circle[0] == 255 && circle[1] == 0 && circle[2] == 0 && circle[3] == 255, __FILE__, __LINE__, "circle center red");
    bmf_check(t, corner[0] == 15 && corner[1] == 18 && corner[2] == 23 && corner[3] == 255, __FILE__, __LINE__, "corner: clear color");
    // A second shot of the same state is byte-identical.
    static u8 first[320 * 180 * 4];
    bmf_memcpy(first, png_test_decoded, sizeof first);
    protocol_expect(t, request, expected, __LINE__);
    bmf_check_eq_i64(t, bmf_platform_file_read(path, arena, &file), BMF_OK, __FILE__, __LINE__, "read again");
    bmf_check_eq_i64(t, bmf_png_read_stored(file.ptr, file.len, png_test_decoded, (i64)sizeof png_test_decoded, &width, &height), BMF_OK, __FILE__, __LINE__, "decode again");
    bmf_image_diff_result r = bmf_image_diff(first, png_test_decoded, 320, 180, 0, 0);
    bmf_check(t, r.differing == 0, __FILE__, __LINE__, "repeat shot identical");
    bmf_arena_pop_to(arena, mark);

    // An explicit size at DPI scale 1, after moving the circle: 64x32 lies inside it, all red.
    protocol_expect(t, "set values[0] 0", "{\"ok\":1,\"path\":\"values[0]\",\"value\":0}\n", __LINE__);
    bmf_fmt_buf(request, (i32)sizeof request, "shot %s 64 32", path);
    protocol_capture.length = 0;
    bmf_protocol_execute(bmf_str_from_cstr(request));
    bmf_check(t, core_test_contains(protocol_capture.text, "\"width\":64,\"height\":32"), __FILE__, __LINE__, protocol_capture.text);
    bmf_check_eq_i64(t, arena->used, mark, __FILE__, __LINE__, "the shot gave its memory back");
    bmf_check_eq_i64(t, bmf_platform_file_read(path, arena, &file), BMF_OK, __FILE__, __LINE__, "read the small shot");
    bmf_check_eq_i64(t, bmf_png_read_stored(file.ptr, file.len, png_test_decoded, (i64)sizeof png_test_decoded, &width, &height), BMF_OK, __FILE__, __LINE__, "decode");
    b32 all_red = width == 64 && height == 32;
    for (i32 p = 0; p < 64 * 32 && all_red; p++) all_red = png_test_decoded[p * 4] == 255 && png_test_decoded[p * 4 + 1] == 0;
    bmf_check(t, all_red, __FILE__, __LINE__, "64x32 inside the circle: all red");
    bmf_arena_pop_to(arena, mark);
    bmf_check(t, bmf_core_view().width == 320 && bmf_core_ctx()->view_size.x == 320.0f && bmf_core_ctx()->view_size.y == 180.0f,
              __FILE__, __LINE__, "the view is restored after a shot of another size");

    // Errors.
    protocol_expect(t, "shot a.jpg", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"'a.jpg': the file name must end in .png\"}\n", __LINE__);
    protocol_expect(t, "shot a.png 9000 10", "{\"ok\":0,\"error\":\"bad_args\",\"detail\":\"size 9000x10 out of range 1..8192\"}\n", __LINE__);
    bmf_fmt_buf(request, (i32)sizeof request, "shot %sno_such_folder/x.png", dir);
    protocol_capture.length = 0;
    bmf_protocol_execute(bmf_str_from_cstr(request));
    bmf_check(t, core_test_contains(protocol_capture.text, "{\"ok\":0,\"error\":\"io_failed\",\"detail\":\"cannot write"), __FILE__, __LINE__, protocol_capture.text);
    bmf_platform_file_delete(path);
    bmf_protocol_end();
    bmf_core_display_close();
    bmf_core_stop();
}

// ---------------------------------------------------------------- shot.no_gpu
static void test_shot_no_gpu(bmf_test* t) {
#if BMF_OS_WINDOWS
    static char out[4096];
    u32 code = protocol_test_spawn("--headless --control --no-gpu", "shot x.png\nquit\n", 16, out, (i32)sizeof out);
    bmf_check(t, code == 0 && core_test_contains(out, "{\"ok\":0,\"error\":\"not_supported\",\"detail\":\"the null GPU backend renders nothing (started with --no-gpu?)\"}"),
              __FILE__, __LINE__, out);
#endif
}
