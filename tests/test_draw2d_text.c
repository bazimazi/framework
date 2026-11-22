// Tests for src/draw2d/font_stroke.c (task 1A-21): glyph data, measuring, the submitted
// instances (layout, alignment, lines, cameras, budget) and a WARP character sheet checked
// against a CPU reference and against the measured boxes.
// Included after test_draw2d.c (uses its helpers).

// Sum of glyph segment counts of a UTF-8 text, as bmf_draw_text submits them.
static i32 draw2d_text_test_segments(bmf_str text) {
    i32 total = 0;
    for (i32 at = 0; at < text.len;) {
        u32 codepoint;
        at += bmf_utf8_decode(text, at, &codepoint);
        if (codepoint == '\n' || codepoint == '\r') continue;
        total += font_stroke.count[font_stroke_glyph(codepoint)];
    }
    return total;
}

static void test_draw2d_text_glyph_data(bmf_test* t) {
    if (!font_stroke.built) font_stroke_build();
    i32 sum = 0;
    for (i32 g = 0; g < FONT_STROKE_GLYPH_COUNT; g++) sum += font_stroke.count[g];
    bmf_check_eq_i64(t, sum, font_stroke.segment_count, __FILE__, __LINE__, "glyph counts cover the segment table");
    char note[96];
    bmf_fmt_buf(note, (i32)sizeof note, "%i segments for 96 glyphs", font_stroke.segment_count);
    bmf_test_note(t, note);
    bmf_check_eq_i64(t, font_stroke.count[0], 0, __FILE__, __LINE__, "space has no ink");
    i32 empty = 0;
    for (i32 g = 1; g < FONT_STROKE_GLYPH_COUNT; g++) if (font_stroke.count[g] == 0) empty++;
    bmf_check_eq_i64(t, empty, 0, __FILE__, __LINE__, "every printable character and the replacement glyph has ink");

    // Every point inside the glyph's cell with a 3-unit margin, so strokes up to
    // BMF_TEXT_MAX_CONTAINED_STROKE stay inside the measured box.
    bmf_check(t, BMF_TEXT_MAX_CONTAINED_STROKE == 6.0f / (f32)FONT_STROKE_EM, __FILE__, __LINE__, "contained stroke = 2 x 3-unit margin");
    bmf_check(t, BMF_TEXT_DEFAULT_STROKE <= BMF_TEXT_MAX_CONTAINED_STROKE, __FILE__, __LINE__, "the default stroke is contained");
    i32 outside = 0;
    for (i32 g = 0; g < FONT_STROKE_GLYPH_COUNT; g++) {
        i32 advance = FONT_STROKE_GLYPHS[g].advance;
        for (i32 s = font_stroke.first[g]; s < font_stroke.first[g] + font_stroke.count[g]; s++) {
            const font_stroke_segment* seg = &font_stroke.segments[s];
            i32 xs[2] = { seg->x0, seg->x1 }, ys[2] = { seg->y0, seg->y1 };
            for (i32 e = 0; e < 2; e++)
                if (xs[e] < 3 || xs[e] > advance - 3 || ys[e] < FONT_STROKE_BASELINE - FONT_STROKE_EM + 4 || ys[e] > FONT_STROKE_BASELINE - 3) {
                    if (!outside) {
                        bmf_fmt_buf(note, (i32)sizeof note, "glyph %i point (%i,%i) outside its cell (advance %i)", g, xs[e], ys[e], advance);
                        bmf_test_note(t, note);
                    }
                    outside++;
                }
        }
    }
    bmf_check_eq_i64(t, outside, 0, __FILE__, __LINE__, "every glyph point lies in x 3..advance-3, y -12..45");
    for (i32 digit = '1'; digit <= '9'; digit++)
        bmf_check_eq_i64(t, FONT_STROKE_GLYPHS[digit - 32].advance, FONT_STROKE_GLYPHS['0' - 32].advance, __FILE__, __LINE__, "digits share one advance");

    // No two glyphs are drawn the same (catches copy-paste slips in the table).
    i32 duplicates = 0;
    for (i32 a = 1; a < FONT_STROKE_GLYPH_COUNT; a++)
        for (i32 b = a + 1; b < FONT_STROKE_GLYPH_COUNT; b++) {
            if (font_stroke.count[a] != font_stroke.count[b]) continue;
            if (bmf_memcmp(&font_stroke.segments[font_stroke.first[a]], &font_stroke.segments[font_stroke.first[b]],
                           (i64)font_stroke.count[a] * (i64)sizeof(font_stroke_segment)) == 0) {
                bmf_fmt_buf(note, (i32)sizeof note, "glyphs %i and %i are identical", a, b);
                bmf_test_note(t, note);
                duplicates++;
            }
        }
    bmf_check_eq_i64(t, duplicates, 0, __FILE__, __LINE__, "glyphs are distinct");
}

static b32 draw2d_text_test_measure_is(bmf_str text, f32 size, f32 width, f32 height) {
    bmf_v2 box = bmf_text_measure(size, text);
    return box.x == width && box.y == height;
}

static void test_draw2d_text_measure(bmf_test* t) {
    i32 h = FONT_STROKE_GLYPHS['H' - 32].advance;
    i32 replacement = FONT_STROKE_GLYPHS[FONT_STROKE_REPLACEMENT].advance;
    bmf_check_eq_i64(t, h, 42, __FILE__, __LINE__, "'H' advance (grid units)");
    bmf_check(t, draw2d_text_test_measure_is(bmf_str_from_cstr(""), 64.0f, 0.0f, 64.0f), __FILE__, __LINE__, "empty text: one empty line");
    bmf_check(t, draw2d_text_test_measure_is(bmf_str_from_cstr("H"), 64.0f, 42.0f, 64.0f), __FILE__, __LINE__, "size 64: one grid unit = 1");
    bmf_check(t, draw2d_text_test_measure_is(bmf_str_from_cstr("H"), 32.0f, 21.0f, 32.0f), __FILE__, __LINE__, "size 32 halves it");
    bmf_check(t, draw2d_text_test_measure_is(bmf_str_from_cstr("HH\nH"), 64.0f, 84.0f, 128.0f), __FILE__, __LINE__, "widest line, two lines");
    bmf_check(t, draw2d_text_test_measure_is(bmf_str_from_cstr("H\nHH\n"), 64.0f, 84.0f, 192.0f), __FILE__, __LINE__, "a trailing newline starts an empty line");
    bmf_check(t, draw2d_text_test_measure_is(bmf_str_from_cstr("H\r\nH\r"), 64.0f, 42.0f, 128.0f), __FILE__, __LINE__, "\\r takes no space");
    bmf_check(t, draw2d_text_test_measure_is(bmf_str_from_cstr("\xC3\xA9"), 64.0f, (f32)replacement, 64.0f), __FILE__, __LINE__,
              "one code point outside ASCII = one replacement glyph");
    bmf_check(t, draw2d_text_test_measure_is(bmf_str_from_cstr("\xFF\xFE"), 64.0f, (f32)(2 * replacement), 64.0f), __FILE__, __LINE__,
              "each invalid UTF-8 byte = one replacement glyph");
    bmf_check(t, draw2d_text_test_measure_is(bmf_str_from_cstr("\t"), 64.0f, (f32)replacement, 64.0f), __FILE__, __LINE__, "tab = replacement glyph");
    bmf_check(t, draw2d_text_test_measure_is(bmf_str_from_cstr("0123456789"), 64.0f, 400.0f, 64.0f), __FILE__, __LINE__, "digits: 10 x 40");
    bmf_check(t, draw2d_text_test_measure_is(bmf_str_make("H\0H", 3), 64.0f, (f32)(2 * h + replacement), 64.0f), __FILE__, __LINE__,
              "length-delimited: NUL is a replacement glyph, not the end");
    bmf_v2 small = bmf_text_measure(0.3f, bmf_str_from_cstr("Hello"));
    bmf_v2 large = bmf_text_measure(0.6f, bmf_str_from_cstr("Hello"));
    bmf_check(t, large.x == small.x * 2.0f && large.y == small.y * 2.0f, __FILE__, __LINE__, "measure scales exactly with size");
}

static void test_draw2d_text_submit(bmf_test* t) {
    draw2d_test_init_null(0);
    bmf_draw2d_view view = { 256, 256, 1.0f };
    bmf_draw2d_begin_frame(draw2d_test_frame_arena(), view);
    bmf_camera screen = bmf_camera_screen_default();
    bmf_draw_camera_push(0, &screen);   // layer 1 keeps the default world camera
    const bmf_draw_instance* d = draw2d.instances;
    bmf_v2 at = bmf_v2_make(10.0f, 20.0f);

    // '-' is one segment (6,18)-(26,18); at size 64 a grid unit is one pixel and the baseline is
    // 48 pixels below the top: a = (10 + 6, 20 + 48 - 18).
    bmf_draw_text(0, at, 64.0f, 0.0f, 0x11223344u, BMF_TEXT_ALIGN_LEFT, bmf_str_from_cstr("-"));
    bmf_check_eq_i64(t, draw2d.count, 1, __FILE__, __LINE__, "'-' is one instance");
    bmf_check(t, d[0].position.x == 16.0f && d[0].position.y == 50.0f && d[0].extra.x == 36.0f && d[0].extra.y == 50.0f, __FILE__, __LINE__,
              "segment end points in target pixels");
    bmf_check(t, d[0].size.y == 64.0f * BMF_TEXT_DEFAULT_STROKE && d[0].kind_corner == BMF_DRAW_KIND_GLYPH && d[0].color == 0x11223344u &&
                 d[0].rotation == 0.0f && d[0].stroke_softness == 0,
              __FILE__, __LINE__, "GLYPH instance, default stroke 0.08 x size, color");
    bmf_check_eq_u64(t, draw2d.keys[0], (u64)(draw2d.white.id >> BMF_HANDLE_GENERATION_BITS) << BMF_DRAW_KEY_TEXTURE_SHIFT, __FILE__, __LINE__,
                     "text batches with the shapes (white texture)");

    // Alignment per line, line advance, '\r', explicit stroke.
    bmf_draw_text(0, at, 64.0f, 2.0f, 0xFFFFFFFFu, BMF_TEXT_ALIGN_CENTER, bmf_str_from_cstr("--\n-"));
    bmf_check_eq_i64(t, draw2d.count, 4, __FILE__, __LINE__, "three more instances");
    bmf_check(t, d[1].position.x == 10.0f - 32.0f + 6.0f && d[2].position.x == 10.0f + 6.0f && d[1].position.y == 50.0f, __FILE__, __LINE__,
              "centered first line: 64 wide, starts 32 left of the anchor");
    bmf_check(t, d[3].position.x == 10.0f - 16.0f + 6.0f && d[3].position.y == 50.0f + 64.0f, __FILE__, __LINE__,
              "second line centered on its own, one size further down");
    bmf_check(t, d[1].size.y == 2.0f, __FILE__, __LINE__, "explicit stroke width");
    bmf_draw_text(0, at, 64.0f, 0.0f, 0xFFFFFFFFu, BMF_TEXT_ALIGN_RIGHT, bmf_str_from_cstr("-\r-"));
    bmf_check(t, draw2d.count == 6 && d[4].position.x == 10.0f - 64.0f + 6.0f && d[5].position.x == 10.0f - 32.0f + 6.0f, __FILE__, __LINE__,
              "right aligned; \\r takes no space");

    // A dot is a zero-length segment; spaces and newlines submit nothing; unknown code points
    // draw the replacement box (4 segments).
    bmf_draw_text(0, at, 64.0f, 0.0f, 0xFFFFFFFFu, BMF_TEXT_ALIGN_LEFT, bmf_str_from_cstr(" .\n \xC3\xA9"));
    bmf_check_eq_i64(t, draw2d.count, 6 + 1 + 4, __FILE__, __LINE__, "dot + replacement box");
    bmf_check(t, d[6].position.x == d[6].extra.x && d[6].position.y == d[6].extra.y && d[6].position.x == 10.0f + 20.0f + 6.0f,
              __FILE__, __LINE__, "the dot after a space (advance 20)");

    // The world camera places the same text at the same pixels, upright: (-1.84375, 1.6875) m
    // maps to pixel (10, 20) in a 256 x 256 view, and 1 m = 64 px.
    i32 before = draw2d.count;
    bmf_str sample = bmf_str_from_cstr("Ag1\nW");
    bmf_draw_text(0, at, 64.0f, 0.0f, 0xFFFFFFFFu, BMF_TEXT_ALIGN_LEFT, sample);
    i32 middle = draw2d.count;
    bmf_draw_text(1, bmf_v2_make(-1.84375f, 1.6875f), 1.0f, 0.0f, 0xFFFFFFFFu, BMF_TEXT_ALIGN_LEFT, sample);
    bmf_check_eq_i64(t, middle - before, draw2d_text_test_segments(sample), __FILE__, __LINE__, "one instance per glyph segment");
    bmf_check_eq_i64(t, draw2d.count - middle, middle - before, __FILE__, __LINE__, "same instance count in world space");
    i32 different = 0;
    for (i32 i = 0; i < middle - before; i++)
        if (bmf_memcmp(&d[before + i], &d[middle + i], (i64)sizeof(bmf_draw_instance)) != 0) different++;
    bmf_check_eq_i64(t, different, 0, __FILE__, __LINE__, "world-space text is upright and identical in pixels");

    // A rotated SCREEN camera turns the text with it (one point checked).
    bmf_camera turned = bmf_camera_screen_default();
    turned.rotation = BMF_HALF_PI;
    bmf_draw_camera_push(2, &turned);
    i32 rotated = draw2d.count;
    bmf_draw_text(2, bmf_v2_make(0.0f, 0.0f), 64.0f, 0.0f, 0xFFFFFFFFu, BMF_TEXT_ALIGN_LEFT, bmf_str_from_cstr("-"));
    bmf_check(t, bmf_fabsf(d[rotated].position.x + 30.0f) < 1e-4f && bmf_fabsf(d[rotated].position.y - 6.0f) < 1e-4f, __FILE__, __LINE__,
              "rotated camera: text point (6, 30) maps to pixel (-30, 6)");
    draw2d_test_begin_frame_pixels();   // drop the frame
    draw2d_test_shutdown();

    // Budget: past max_instances, segments are dropped and counted.
    draw2d_test_init_null(8);
    draw2d_test_begin_frame_pixels();
    bmf_draw_text(0, at, 16.0f, 0.0f, 0xFFFFFFFFu, BMF_TEXT_ALIGN_LEFT, bmf_str_from_cstr("HHH"));   // 3 segments each
    bmf_check(t, draw2d.count == 8 && bmf_draw2d_get_stats().dropped == 1, __FILE__, __LINE__, "8 drawn, 1 dropped");
    draw2d_test_begin_frame_pixels();
    draw2d_test_shutdown();
}

// ---------------------------------------------------------------- WARP character sheet
enum { DRAW2D_TEXT_SHEET_W = 672, DRAW2D_TEXT_SHEET_H = 272 };

static void test_draw2d_d3d11_text_sheet(bmf_test* t) {
    enum { W = DRAW2D_TEXT_SHEET_W, H = DRAW2D_TEXT_SHEET_H, MAX_BOXES = 16 };
    gpu_d3d11_test_init(t, 0, 0, 0, 1);
    bmf_draw2d_desc desc = { 0 };
    bmf_draw2d_init(&desc);
    bmf_gpu_texture_desc target_desc = { BMF_GPU_FORMAT_RGBA8_UNORM, BMF_GPU_TEXTURE_RENDER_TARGET | BMF_GPU_TEXTURE_COPY_SRC, W, H, 0, "text sheet" };
    bmf_gpu_texture target = bmf_gpu_texture_create(&target_desc);

    bmf_draw2d_view view = { W, H, 1.0f };
    bmf_draw2d_begin_frame(draw2d_test_frame_arena(), view);
    bmf_camera screen = bmf_camera_screen_default();
    bmf_draw_camera_push(0, &screen);

    // Every printable character (and one replacement glyph) at three sizes, then the three
    // alignments. Each text's measured box is kept for the containment check.
    static char rows[3][40];
    for (i32 c = 32; c < 127; c++) rows[(c - 32) / 32][(c - 32) % 32] = (char)c;
    rows[2][31] = (char)0xC3;   // row 3 ends with U+00E9 (replacement glyph)
    rows[2][32] = (char)0xA9;
    bmf_str row_text[3] = { bmf_str_make(rows[0], 32), bmf_str_make(rows[1], 32), bmf_str_make(rows[2], 33) };
    struct { f32 x0, y0, x1, y1; } boxes[MAX_BOXES];
    i32 box_count = 0;
    static const f32 sizes[3] = { 12.0f, 20.0f, 32.0f };
    f32 y = 4.0f;
    for (i32 s = 0; s < 3; s++) {
        for (i32 r = 0; r < 3; r++) {
            bmf_v2 box = bmf_text_measure(sizes[s], row_text[r]);
            bmf_draw_text(0, bmf_v2_make(4.0f, y), sizes[s], 0.0f, 0xFFFFFFFFu, BMF_TEXT_ALIGN_LEFT, row_text[r]);
            boxes[box_count].x0 = 4.0f, boxes[box_count].y0 = y, boxes[box_count].x1 = 4.0f + box.x, boxes[box_count].y1 = y + box.y;
            box_count++;
            y += sizes[s];
        }
        y += 4.0f;
    }
    struct { bmf_text_align align; f32 x; } aligned[3] = { { BMF_TEXT_ALIGN_LEFT, 8.0f }, { BMF_TEXT_ALIGN_CENTER, 336.0f }, { BMF_TEXT_ALIGN_RIGHT, 664.0f } };
    bmf_str aligned_text = bmf_str_from_cstr("HxH\nHH");
    for (i32 a = 0; a < 3; a++) {
        bmf_draw_text(0, bmf_v2_make(aligned[a].x, y), 24.0f, 0.0f, 0xFFFFFFFFu, aligned[a].align, aligned_text);
        for (i32 line = 0; line < 2; line++) {
            bmf_str line_text = line == 0 ? bmf_str_from_cstr("HxH") : bmf_str_from_cstr("HH");
            f32 width = bmf_text_measure(24.0f, line_text).x;
            f32 x0 = aligned[a].align == BMF_TEXT_ALIGN_LEFT ? aligned[a].x : aligned[a].align == BMF_TEXT_ALIGN_CENTER ? aligned[a].x - width * 0.5f : aligned[a].x - width;
            boxes[box_count].x0 = x0, boxes[box_count].y0 = y + 24.0f * (f32)line, boxes[box_count].x1 = x0 + width, boxes[box_count].y1 = y + 24.0f * (f32)(line + 1);
            box_count++;
        }
    }
    bmf_check(t, y + 48.0f <= (f32)H, __FILE__, __LINE__, "sheet fits");

    // Keep the submitted instances (submission order = draw order: one layer, one texture).
    i32 n = draw2d.count;
    bmf_draw_instance* instances = (bmf_draw_instance*)bmf_arena_push_aligned(draw2d.frame, (i64)n * (i64)sizeof(bmf_draw_instance), 64);
    bmf_memcpy(instances, draw2d.instances, (i64)n * (i64)sizeof(bmf_draw_instance));
    bmf_gpu_begin_frame();
    draw2d_test_begin_pass(target, (bmf_v4){ 0.0f, 0.0f, 0.0f, 1.0f });
    bmf_draw2d_flush(W, H, BMF_GPU_FORMAT_RGBA8_UNORM);
    bmf_gpu_end_pass();
    bmf_gpu_end_frame();
    bmf_check_eq_i64(t, bmf_draw2d_get_stats().batches, 1, __FILE__, __LINE__, "the whole sheet is one draw");
    static u8 actual[W * H * 4];
    bmf_check_eq_i64(t, bmf_gpu_texture_read_begin(target), BMF_OK, __FILE__, __LINE__, "read_begin");
    bmf_check_eq_i64(t, bmf_gpu_texture_read_end(actual, (i64)sizeof actual), BMF_OK, __FILE__, __LINE__, "read_end");
    test_golden_image(t, "draw2d.font", actual, W, H);   // tests/golden/d3d11 (task 1A-41)

    // CPU reference: each capsule in draw order over its padded bounding box, coverage
    // saturate(0.5 - d), blended premultiplied over the 8-bit target. WARP converts the shader
    // output to the target format before blending and stores the rounded result, so the model
    // rounds both: s = round(255 c), dst = round(s + dst (1 - s / 255)). (Blending in float
    // instead differs by up to 3 levels where four faint capsule ends overlap.)
    static u8 expected[W * H * 4];
    gpu_d3d11_test_fill(expected, W, H, 0, 0, 0, 255);
    for (i32 i = 0; i < n; i++) {
        const bmf_draw_instance* d = &instances[i];
        bmf_v2 a = d->position, b = bmf_v2_make(d->extra.x, d->extra.y);
        f32 radius = d->size.y * 0.5f;
        i32 x0 = bmf_f32_to_i32(bmf_fminf(a.x, b.x) - radius - 2.0f), x1 = bmf_f32_to_i32(bmf_fmaxf(a.x, b.x) + radius + 2.0f);
        i32 y0 = bmf_f32_to_i32(bmf_fminf(a.y, b.y) - radius - 2.0f), y1 = bmf_f32_to_i32(bmf_fmaxf(a.y, b.y) + radius + 2.0f);
        for (i32 py = y0 < 0 ? 0 : y0; py <= y1 && py < H; py++)
            for (i32 px = x0 < 0 ? 0 : x0; px <= x1 && px < W; px++) {
                f32 distance = draw2d_test_distance_segment(bmf_v2_make((f32)px + 0.5f, (f32)py + 0.5f), a, b) - radius;
                f32 coverage = bmf_saturatef(0.5f - distance);
                if (coverage <= 0.0f) continue;
                u8* p = &expected[(py * W + px) * 4];
                f32 source = (f32)bmf_f32_to_i32(coverage * 255.0f + 0.5f);
                for (i32 ch = 0; ch < 3; ch++) {
                    f32 value = source + (f32)p[ch] * (1.0f - source / 255.0f);
                    p[ch] = (u8)bmf_f32_to_i32(value + 0.5f);
                }
            }
    }
    gpu_d3d11_test_compare(t, actual, expected, W, H, 1, "text sheet vs CPU reference", __LINE__);

    // Measuring is consistent with drawing: no ink outside the measured boxes (plus the 1-pixel
    // antialiased fringe), and the aligned "H" lines have ink within 3 pixels of both box edges.
    i32 stray = 0, lit = 0;
    for (i32 py = 0; py < H; py++)
        for (i32 px = 0; px < W; px++) {
            if (actual[(py * W + px) * 4] == 0) continue;
            lit++;
            b32 inside = 0;
            for (i32 bi = 0; bi < box_count && !inside; bi++)
                inside = (f32)px + 1.0f >= boxes[bi].x0 - 1.0f && (f32)px <= boxes[bi].x1 + 1.0f && (f32)py + 1.0f >= boxes[bi].y0 - 1.0f &&
                         (f32)py <= boxes[bi].y1 + 1.0f;
            if (!inside) stray++;
        }
    bmf_check(t, lit > 10000, __FILE__, __LINE__, "the sheet has ink");
    bmf_check_eq_i64(t, stray, 0, __FILE__, __LINE__, "no ink outside the measured boxes");
    i32 tight = 0;
    for (i32 bi = 9; bi < box_count; bi++) {
        i32 left = W, right = -1;
        for (i32 py = (i32)boxes[bi].y0; py < (i32)boxes[bi].y1; py++)
            for (i32 px = 0; px < W; px++)
                if (actual[(py * W + px) * 4] != 0 && (f32)px + 1.0f >= boxes[bi].x0 - 1.0f && (f32)px <= boxes[bi].x1 + 1.0f) {
                    if (px < left) left = px;
                    if (px > right) right = px;
                }
        if ((f32)left <= boxes[bi].x0 + 3.0f && (f32)right + 1.0f >= boxes[bi].x1 - 3.0f) tight++;
    }
    bmf_check_eq_i64(t, tight, 6, __FILE__, __LINE__, "aligned lines: ink reaches within 3 px of both measured edges");
    char note[160];
    bmf_fmt_buf(note, (i32)sizeof note, "%i instances, %i lit pixels; %ix%i, golden tests/golden/d3d11/draw2d.font.png", n, lit, W, H);
    bmf_test_note(t, note);

    bmf_gpu_texture_destroy(target);
    bmf_draw2d_shutdown();
    bmf_gpu_shutdown();
}

// Submit cost of text: 10 lines of 100 characters (about 9 000 instances), best of 5.
static void test_draw2d_bench_text(bmf_test* t) {
    draw2d_test_init_null(0);
    bmf_gpu_texture target = draw2d_test_target(64, 64);
    static char line[101];
    for (i32 i = 0; i < 100; i++) line[i] = (char)(33 + (i * 7) % 94);
    line[100] = '\n';
    static char text[101 * 10];
    for (i32 l = 0; l < 10; l++) bmf_memcpy(&text[l * 101], line, 101);
    bmf_str all = bmf_str_make(text, (i32)sizeof text - 1);
    i64 best_ns = 1ll << 62;
    i32 instances = 0;
    for (i32 run = 0; run < 5; run++) {
        draw2d_test_begin_frame_pixels();
        bmf_gpu_begin_frame();
        draw2d_test_begin_pass(target, (bmf_v4){ 0.0f, 0.0f, 0.0f, 1.0f });
        i64 start = bmf_platform_time_ns();
        bmf_draw_text(0, bmf_v2_make(4.0f, 4.0f), 16.0f, 0.0f, 0xFFFFFFFFu, BMF_TEXT_ALIGN_CENTER, all);
        instances = draw2d.count;
        bmf_draw2d_flush(64, 64, BMF_GPU_FORMAT_RGBA8_UNORM);
        i64 elapsed = bmf_platform_time_ns() - start;
        if (elapsed < best_ns) best_ns = elapsed;
        bmf_gpu_end_pass();
        bmf_gpu_end_frame();
    }
    bmf_check_eq_i64(t, instances, draw2d_text_test_segments(all), __FILE__, __LINE__, "one instance per segment");
    char note[128];
    bmf_fmt_buf(note, (i32)sizeof note, "1000 characters (%i instances) submit+sort+upload+batch: best of 5 %i us (debug -O0, null backend)", instances,
                (i32)(best_ns / 1000));
    bmf_test_note(t, note);
    bmf_gpu_texture_destroy(target);
    draw2d_test_shutdown();
}
