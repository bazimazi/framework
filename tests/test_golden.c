// Tests for tools/test/golden.c (task 1A-41): the simulation lint, the image comparison with the
// SPEC §15 tolerances, and how --update-golden names select goldens. The game goldens themselves
// run in `test.exe --golden` (build test's golden phase).

// ---------------------------------------------------------------- golden.lint
static void test_golden_lint(bmf_test* t) {
    golden_init();
    const char* source =
        "// (i32)1.5f in a comment, bmf_fast_sqrtf too\n"                 // 1
        "/* rdtsc\n __builtin_fma */\n"                                    // 2, 3
        "static const char* s = \"(i32)1.5f bmf_fast_\";\n"               // 4
        "i32 a = (i32)(v * 0.5f);\n"                                       // 5: flagged
        "i32 b = (i32)bmf_floorf(x);\n"                                    // 6: flagged
        "u32 c = (u32) 1.5;\n"                                             // 7: flagged
        "f32 d = bmf_fast_sqrtf(x);\n"                                     // 8: flagged
        "u64 e = __rdtsc();\n"                                             // 9: flagged
        "f32 f = __builtin_fmaf(x, y, z);\n"                               // 10: flagged
        "i32 g = (i32)bmf_rng_range_f32(&r, 0.0f, 1.0f);\n"                // 11: flagged
        "i32 h = (i32)sizeof(x) + (i32)(sizeof a / sizeof b) + (u32)0x10 + (i32)count;\n"   // 12: fine
        "i32 k = bmf_f32_to_i32(1.5f) + (i32)offsetof(s, f) + (i32)(n + 1);\n"            // 13: fine
        "f32 m = (f32)i * 0.5f; char q = '\"';\n";                         // 14: fine
    char report[2048];
    i32 findings = golden_lint_source("x.c", source, bmf_cstr_len(source), report, (i32)sizeof report);
    bmf_check_eq_i64(t, findings, 7, __FILE__, __LINE__, report);
    const char* expected[] = {
        "x.c:5: raw float-to-int cast", "x.c:6: raw float-to-int cast", "x.c:7: raw float-to-int cast",
        "x.c:8: bmf_fast_* math", "x.c:9: rdtsc", "x.c:10: FMA", "x.c:11: raw float-to-int cast",
    };
    for (i32 i = 0; i < (i32)(sizeof expected / sizeof expected[0]); i++)
        bmf_check(t, bmf_str_find(bmf_str_from_cstr(report), bmf_str_from_cstr(expected[i]), 0) >= 0, __FILE__, __LINE__, expected[i]);
    // A clean file.
    const char* clean = "i32 n = (i32)(sizeof x / sizeof x[0]);\nf32 y = bmf_floorf(x);\n";
    bmf_check_eq_i64(t, golden_lint_source("y.c", clean, bmf_cstr_len(clean), report, (i32)sizeof report), 0, __FILE__, __LINE__, report);
}

// ---------------------------------------------------------------- golden.compare
static void test_golden_compare(bmf_test* t) {
    golden_init();
    enum { W = 16, H = 16 };   // 256 pixels: one differing pixel is 0.39%, over the 0.1% limit
    static u8 image[W * H * 4], other[W * H * 4], png[8192];
    for (i32 i = 0; i < W * H * 4; i++) image[i] = (u8)(i * 7);
    char dir[1200], path[1200], stem[1200], message[1024];
    bmf_fmt_buf(dir, (i32)sizeof dir, "%s/out/test/golden", golden_root_dir);
    bmf_platform_dir_create(dir);
    bmf_fmt_buf(path, (i32)sizeof path, "%s/unit_compare.png", dir);
    bmf_fmt_buf(stem, (i32)sizeof stem, "%s/unit_compare.actual", dir);
    i64 size = bmf_png_write(image, W, H, png, (i64)sizeof png);
    bmf_check_eq_i64(t, bmf_platform_file_write_atomic(path, png, size), BMF_OK, __FILE__, __LINE__, "write the golden");

    bmf_memcpy(other, image, sizeof other);
    bmf_check(t, golden_compare_image(path, other, W, H, stem, message, (i32)sizeof message), __FILE__, __LINE__, "identical");
    other[100] = (u8)(other[100] + 2);   // within the tolerance of 2
    other[200] = (u8)(other[200] - 2);
    bmf_check(t, golden_compare_image(path, other, W, H, stem, message, (i32)sizeof message), __FILE__, __LINE__, "within +-2");
    other[100] = (u8)(other[100] + 1);   // 3 off: one pixel of 256 differs
    b32 ok = golden_compare_image(path, other, W, H, stem, message, (i32)sizeof message);
    bmf_check(t, !ok && bmf_str_starts_with(bmf_str_from_cstr(message), bmf_str_from_cstr("1 of 256 pixels differ by more than 2 (0.390625%, at most 0.1%), largest channel difference 3, first at (9, 1)")),
              __FILE__, __LINE__, message);
    char diff_path[1300];
    bmf_fmt_buf(diff_path, (i32)sizeof diff_path, "%s.diff.png", stem);
    bmf_file_info info;
    bmf_check(t, bmf_platform_file_info(diff_path, &info) == BMF_OK && info.size == bmf_png_write_size(W, H), __FILE__, __LINE__, "diff image written");
    // Wrong size and a missing golden.
    bmf_check(t, !golden_compare_image(path, other, 8, 8, stem, message, (i32)sizeof message) &&
                 bmf_str_find(bmf_str_from_cstr(message), bmf_str_from_cstr("is 16x16, the image is 8x8"), 0) >= 0, __FILE__, __LINE__, message);
    bmf_fmt_buf(path, (i32)sizeof path, "%s/no_such_golden.png", dir);
    bmf_check(t, !golden_compare_image(path, other, W, H, stem, message, (i32)sizeof message) &&
                 bmf_str_starts_with(bmf_str_from_cstr(message), bmf_str_from_cstr("no golden")), __FILE__, __LINE__, message);
    bmf_fmt_buf(path, (i32)sizeof path, "%s/unit_compare.png", dir);
    bmf_platform_file_delete(path);
    bmf_platform_file_delete(diff_path);
    bmf_fmt_buf(path, (i32)sizeof path, "%s.png", stem);
    bmf_platform_file_delete(path);
}

// ---------------------------------------------------------------- golden.update_names
static void test_golden_update_names(bmf_test* t) {
    const char* saved = golden_update_name;
    golden_update_name = "hello";
    bmf_check(t, golden_update_matches("hello") && golden_update_matches("hello.bounce") && !golden_update_matches("hellos.bounce") && !golden_update_matches("draw2d.font"),
              __FILE__, __LINE__, "a game name selects its scenarios");
    golden_update_name = "draw2d.font";
    bmf_check(t, golden_update_matches("draw2d.font") && !golden_update_matches("draw2d.shapes") && !golden_update_matches("draw2d"), __FILE__, __LINE__, "one golden");
    golden_update_name = 0;
    bmf_check(t, !golden_update_matches("hello"), __FILE__, __LINE__, "no update without --update-golden");
    golden_update_name = saved;
}
