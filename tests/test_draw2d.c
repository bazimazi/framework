// Tests for src/draw2d (task 1A-20a): instance layout, f16 packing, submit fields, budgets,
// sort and batch counts and bind set reuse on the null backend, and exact pixels on WARP for
// layer order, submission order within a layer, rotation, sprites and alpha.
// Included after test_gpu.c and test_gpu_d3d11.c (uses their init and readback helpers).

// One frame arena for every draw2d test (reset per frame, like the engine's).
static bmf_arena* draw2d_test_frame_arena(void) {
    static bmf_arena arena;
    static b32 ready;
    if (!ready) {
        i64 size = 64ll << 20;
        u8* base = (u8*)bmf_platform_reserve(size + BMF_PAGE_GRANULE);
        if (!base) bmf_panic("draw2d test: reserve failed");
        bmf_arena_init(&arena, "draw2d test frame", base, size);
        ready = 1;
    }
    bmf_arena_reset(&arena);
    return &arena;
}

// Begins a frame (view 128x128, DPI 1) whose every layer uses the identity screen camera, so
// submits are in target pixels (the 1A-20a/b tests).
static void draw2d_test_begin_frame_pixels(void) {
    bmf_draw2d_view view = { 128, 128, 1.0f };
    bmf_draw2d_begin_frame(draw2d_test_frame_arena(), view);
    bmf_camera screen = bmf_camera_screen_default();
    for (i32 layer = 0; layer < BMF_DRAW2D_MAX_LAYERS; layer++) bmf_draw_camera_push(layer, &screen);
}

static void draw2d_test_init_null(i32 max_instances) {
    gpu_test_init_null(0, 0);
    bmf_draw2d_desc desc = { max_instances };
    bmf_draw2d_init(&desc);
}

static void draw2d_test_shutdown(void) {
    bmf_draw2d_shutdown();
    bmf_gpu_shutdown();
}

// An offscreen RGBA8 target for one flush, in one pass that clears to `clear`.
static bmf_gpu_texture draw2d_test_target(i32 width, i32 height) {
    bmf_gpu_texture_desc desc = { BMF_GPU_FORMAT_RGBA8_UNORM, BMF_GPU_TEXTURE_RENDER_TARGET | BMF_GPU_TEXTURE_COPY_SRC, width, height, 0, "draw2d test target" };
    return bmf_gpu_texture_create(&desc);
}

static void draw2d_test_begin_pass(bmf_gpu_texture target, bmf_v4 clear) {
    bmf_gpu_pass_desc pass;
    bmf_memset(&pass, 0, sizeof pass);
    pass.color = target;
    pass.color_load = BMF_GPU_LOAD_CLEAR;
    pass.clear_color = clear;
    pass.label = "draw2d test";
    bmf_gpu_begin_pass(&pass);
}

static void test_draw2d_instance_layout(bmf_test* t) {
    bmf_check_eq_i64(t, (i64)sizeof(bmf_draw_instance), 48, __FILE__, __LINE__, "48 bytes");
    bmf_check_eq_i64(t, (i64)__builtin_offsetof(bmf_draw_instance, size), 8, __FILE__, __LINE__, "size offset");
    bmf_check_eq_i64(t, (i64)__builtin_offsetof(bmf_draw_instance, rotation), 16, __FILE__, __LINE__, "rotation offset");
    bmf_check_eq_i64(t, (i64)__builtin_offsetof(bmf_draw_instance, color), 20, __FILE__, __LINE__, "color offset");
    bmf_check_eq_i64(t, (i64)__builtin_offsetof(bmf_draw_instance, extra), 24, __FILE__, __LINE__, "extra offset");
    bmf_check_eq_i64(t, (i64)__builtin_offsetof(bmf_draw_instance, kind_corner), 40, __FILE__, __LINE__, "kind_corner offset");
    bmf_check_eq_i64(t, (i64)__builtin_offsetof(bmf_draw_instance, stroke_softness), 44, __FILE__, __LINE__, "stroke_softness offset");
    // Little-endian: the color's bytes in memory are A, B, G, R (the shader swizzles .wzyx).
    bmf_draw_instance instance;
    instance.color = 0x11223344u;
    const u8* bytes = (const u8*)&instance.color;
    bmf_check(t, bytes[0] == 0x44 && bytes[1] == 0x33 && bytes[2] == 0x22 && bytes[3] == 0x11, __FILE__, __LINE__, "color byte order");
}

// Reference decode of a half float (finite, non-negative).
static f64 draw2d_test_half_value(u32 h) {
    u32 exponent = (h >> 10) & 31u, mantissa = h & 1023u;
    f64 scale = 1.0;
    if (exponent == 0) {
        for (i32 i = 0; i < 24; i++) scale *= 0.5;
        return (f64)mantissa * scale;
    }
    for (i32 i = 0; i < 25; i++) scale *= 0.5;   // 2^-25 * 2^exponent * (1024 + mantissa) = 2^(exponent - 15) * (1 + m/1024)
    for (u32 i = 0; i < exponent; i++) scale *= 2.0;
    return (f64)(1024u + mantissa) * scale;
}

// Every finite non-negative half round-trips, and every midpoint between neighbors rounds to the
// even one (all exactly representable as f32), plus the special inputs.
static void test_draw2d_f16(bmf_test* t) {
    i32 bad_exact = 0, bad_midpoint = 0;
    for (u32 h = 0; h <= 0x7BFFu; h++) {
        if (bmf_draw_f16((f32)draw2d_test_half_value(h)) != h) bad_exact++;
        if (h == 0x7BFFu) break;
        f64 midpoint = (draw2d_test_half_value(h) + draw2d_test_half_value(h + 1)) * 0.5;
        u32 expected = (h & 1u) ? h + 1 : h;
        if (bmf_draw_f16((f32)midpoint) != expected) bad_midpoint++;
    }
    bmf_check_eq_i64(t, bad_exact, 0, __FILE__, __LINE__, "every half round-trips");
    bmf_check_eq_i64(t, bad_midpoint, 0, __FILE__, __LINE__, "midpoints round to even");
    bmf_check_eq_u64(t, bmf_draw_f16(1.0f), 0x3C00, __FILE__, __LINE__, "1.0");
    bmf_check_eq_u64(t, bmf_draw_f16(0.333333343f), 0x3555, __FILE__, __LINE__, "1/3");
    bmf_check_eq_u64(t, bmf_draw_f16(1.0e9f), 0x7BFF, __FILE__, __LINE__, "saturates");
    bmf_check_eq_u64(t, bmf_draw_f16(65519.0f), 0x7BFF, __FILE__, __LINE__, "just below the overflow midpoint");
    bmf_check_eq_u64(t, bmf_draw_f16(-3.0f), 0, __FILE__, __LINE__, "negative -> 0");
    bmf_check_eq_u64(t, bmf_draw_f16(bmf_f32_from_bits(0x7FC00000u)), 0, __FILE__, __LINE__, "NaN -> 0");
    bmf_check_eq_u64(t, bmf_draw_f16(1.0e-9f), 0, __FILE__, __LINE__, "underflow -> 0");
    bmf_check_eq_u64(t, bmf_draw_f16(0.0000000447034836f), 1, __FILE__, __LINE__, "0.75 * 2^-24 rounds up to the smallest subnormal");
}

static void test_draw2d_submit_fields(bmf_test* t) {
    draw2d_test_init_null(0);
    bmf_gpu_texture_desc texture_desc = { BMF_GPU_FORMAT_RGBA8_UNORM, BMF_GPU_TEXTURE_SAMPLED, 2, 2, 0, "sprite" };
    bmf_gpu_texture texture = bmf_gpu_texture_create(&texture_desc);
    draw2d_test_begin_frame_pixels();
    bmf_draw_rect(3, bmf_v2_make(10.0f, 20.0f), bmf_v2_make(4.0f, 6.0f), 0.5f, 0x11223344u);
    bmf_draw_rect_rounded(4, bmf_v2_make(1.0f, 2.0f), bmf_v2_make(30.0f, 40.0f), 0.0f, 5.0f, 0xFFFFFFFFu);
    bmf_draw_circle(5, bmf_v2_make(7.0f, 8.0f), 3.0f, 0x000000FFu);
    bmf_draw_ring(6, bmf_v2_make(7.0f, 8.0f), 10.0f, 2.0f, 0x000000FFu);
    bmf_draw_line(7, bmf_v2_make(1.0f, 2.0f), bmf_v2_make(3.0f, 4.0f), 1.5f, 0x000000FFu);
    bmf_draw_triangle(8, bmf_v2_make(1.0f, 2.0f), bmf_v2_make(3.0f, 4.0f), bmf_v2_make(5.0f, 6.0f), 0x000000FFu);
    bmf_draw_sprite(255, texture, bmf_rect_make(0.25f, 0.5f, 0.5f, 0.25f), bmf_v2_make(9.0f, 9.0f), bmf_v2_make(16.0f, 8.0f), 1.0f, 0x808080FFu);
    const bmf_draw_instance* d = draw2d.instances;
    const u64* k = draw2d.keys;
    u64 white_slot = draw2d.white.id >> BMF_HANDLE_GENERATION_BITS;
    u64 sprite_slot = texture.id >> BMF_HANDLE_GENERATION_BITS;
    bmf_check_eq_i64(t, draw2d.count, 7, __FILE__, __LINE__, "seven instances");

    bmf_check(t, d[0].position.x == 10.0f && d[0].position.y == 20.0f && d[0].size.x == 4.0f && d[0].size.y == 6.0f, __FILE__, __LINE__, "rect geometry");
    bmf_check(t, d[0].rotation == -0.5f, __FILE__, __LINE__, "rect rotation is negated (CCW on screen = negative in y-down pixels)");
    bmf_check_eq_u64(t, d[0].color, 0x11223344u, __FILE__, __LINE__, "rect color");
    bmf_check_eq_u64(t, d[0].kind_corner, BMF_DRAW_KIND_RECT, __FILE__, __LINE__, "rect kind, no corner");
    bmf_check_eq_u64(t, k[0], 3ull << 56 | white_slot << 40, __FILE__, __LINE__, "rect key: layer 3, white texture");

    bmf_check_eq_u64(t, d[1].kind_corner, BMF_DRAW_KIND_RECT | 0x4500u << 16, __FILE__, __LINE__, "rounded rect: corner 5.0 as f16 0x4500");
    bmf_check(t, d[2].size.x == 6.0f && d[2].size.y == 6.0f && d[2].kind_corner == BMF_DRAW_KIND_CIRCLE && d[2].stroke_softness == 0,
              __FILE__, __LINE__, "circle: diameter, no stroke");
    bmf_check(t, d[3].size.x == 20.0f && d[3].kind_corner == BMF_DRAW_KIND_CIRCLE && d[3].stroke_softness == 0x4000u,
              __FILE__, __LINE__, "ring: circle with stroke 2.0 (f16 0x4000)");
    bmf_check(t, d[4].kind_corner == BMF_DRAW_KIND_CAPSULE && d[4].position.x == 1.0f && d[4].extra.x == 3.0f && d[4].extra.y == 4.0f &&
              d[4].size.y == 1.5f, __FILE__, __LINE__, "line: capsule a, b, thickness");
    bmf_check(t, d[5].kind_corner == BMF_DRAW_KIND_TRIANGLE && d[5].position.y == 2.0f && d[5].extra.x == 3.0f && d[5].extra.w == 6.0f,
              __FILE__, __LINE__, "triangle: a, then b and c in extra");
    bmf_check(t, d[6].kind_corner == BMF_DRAW_KIND_SPRITE && d[6].extra.x == 0.25f && d[6].extra.y == 0.5f && d[6].extra.z == 0.75f &&
              d[6].extra.w == 0.75f && d[6].rotation == -1.0f, __FILE__, __LINE__, "sprite: uv rect as u0 v0 u1 v1");
    bmf_check_eq_u64(t, k[6], 255ull << 56 | sprite_slot << 40, __FILE__, __LINE__, "sprite key: layer 255, its texture slot");
    draw2d_test_begin_frame_pixels();   // drop the frame without flushing
    bmf_gpu_texture_destroy(texture);
    draw2d_test_shutdown();
}

// Interleaved layers and textures on the null backend: one sort, one ring write, one draw per
// run of equal texture, exact GPU counters.
static void test_draw2d_batches_null(bmf_test* t) {
    draw2d_test_init_null(0);
    bmf_gpu_texture_desc texture_desc = { BMF_GPU_FORMAT_RGBA8_UNORM, BMF_GPU_TEXTURE_SAMPLED, 2, 2, 0, "sprite" };
    bmf_gpu_texture a = bmf_gpu_texture_create(&texture_desc);
    bmf_gpu_texture b = bmf_gpu_texture_create(&texture_desc);
    bmf_rect full = bmf_rect_make(0.0f, 0.0f, 1.0f, 1.0f);
    bmf_v2 p = bmf_v2_make(10.0f, 10.0f), s = bmf_v2_make(4.0f, 4.0f);

    draw2d_test_begin_frame_pixels();
    bmf_draw_sprite(1, a, full, p, s, 0.0f, 0xFFFFFFFFu);
    bmf_draw_rect(2, p, s, 0.0f, 0xFFFFFFFFu);
    bmf_draw_sprite(1, b, full, p, s, 0.0f, 0xFFFFFFFFu);
    bmf_draw_circle(1, p, 3.0f, 0xFFFFFFFFu);
    bmf_draw_sprite(1, a, full, p, s, 0.0f, 0xFFFFFFFFu);
    bmf_draw_line(0, p, s, 1.0f, 0xFFFFFFFFu);
    bmf_draw_rect(2, p, s, 0.0f, 0xFFFFFFFFu);
    bmf_gpu_texture target = draw2d_test_target(64, 64);
    bmf_gpu_begin_frame();
    draw2d_test_begin_pass(target, (bmf_v4){ 0.0f, 0.0f, 0.0f, 1.0f });
    bmf_draw2d_flush(64, 64, BMF_GPU_FORMAT_RGBA8_UNORM);
    bmf_gpu_end_pass();
    bmf_gpu_end_frame();
    // Sorted: layer 0 line (white) | layer 1: white circle, a, a, b (slot order) | layer 2: two
    // white rects. Runs: white (line + circle), a, b, white (rects) = 4 draws; the slots of
    // white < a < b because they were created in that order (fresh pools hand out ascending slots).
    bmf_check(t, (draw2d.white.id >> BMF_HANDLE_GENERATION_BITS) < (a.id >> BMF_HANDLE_GENERATION_BITS) &&
              (a.id >> BMF_HANDLE_GENERATION_BITS) < (b.id >> BMF_HANDLE_GENERATION_BITS), __FILE__, __LINE__, "slot order white < a < b");
    bmf_draw2d_stats stats = bmf_draw2d_get_stats();
    bmf_gpu_stats gpu = bmf_gpu_get_stats();
    bmf_check_eq_i64(t, stats.instances, 7, __FILE__, __LINE__, "7 instances flushed");
    bmf_check_eq_i64(t, stats.batches, 4, __FILE__, __LINE__, "4 batches");
    bmf_check_eq_i64(t, gpu.draws, 4, __FILE__, __LINE__, "4 GPU draws");
    bmf_check_eq_i64(t, gpu.instances, 7, __FILE__, __LINE__, "7 GPU instances");
    bmf_check_eq_i64(t, gpu.vertices, 28, __FILE__, __LINE__, "4 vertices per instance");
    bmf_check_eq_i64(t, gpu.bytes_written, 7 * 48 + 16, __FILE__, __LINE__, "one ring write of 7 instances plus one uniform block");
    bmf_check_eq_i64(t, gpu.pipeline_changes, 1, __FILE__, __LINE__, "one pipeline for everything");
    bmf_check_eq_i64(t, gpu.bindset_changes, 4, __FILE__, __LINE__, "bind set per run");
    bmf_check_eq_i64(t, gpu.bindsets, 3, __FILE__, __LINE__, "three bind sets (white, a, b) cached");

    // Second frame, two flushes: the second appends after the first in the ring and uses its own
    // uniform block (new bind set for flush index 1); the first flush reuses the cached bind sets.
    draw2d_test_begin_frame_pixels();
    bmf_gpu_begin_frame();
    draw2d_test_begin_pass(target, (bmf_v4){ 0.0f, 0.0f, 0.0f, 1.0f });
    bmf_draw_rect(0, p, s, 0.0f, 0xFFFFFFFFu);
    bmf_draw_rect(0, p, s, 0.0f, 0xFFFFFFFFu);
    bmf_draw2d_flush(64, 64, BMF_GPU_FORMAT_RGBA8_UNORM);
    bmf_draw_rect(0, p, s, 0.0f, 0xFFFFFFFFu);
    bmf_draw2d_flush(64, 64, BMF_GPU_FORMAT_RGBA8_UNORM);
    bmf_draw2d_flush(64, 64, BMF_GPU_FORMAT_RGBA8_UNORM);   // empty: no draw, no flush slot used
    bmf_gpu_end_pass();
    bmf_gpu_end_frame();
    gpu = bmf_gpu_get_stats();
    bmf_check_eq_i64(t, gpu.draws, 2, __FILE__, __LINE__, "one draw per flush");
    bmf_check_eq_i64(t, gpu.bytes_written, 3 * 48 + 2 * 16, __FILE__, __LINE__, "two ring writes, two uniform blocks");
    bmf_check_eq_i64(t, gpu.bindsets, 4, __FILE__, __LINE__, "one more bind set (white, flush 1)");
    bmf_check_eq_i64(t, draw2d.flush_count, 2, __FILE__, __LINE__, "the empty flush took no slot");

    bmf_gpu_texture_destroy(target);
    bmf_gpu_texture_destroy(a);
    bmf_gpu_texture_destroy(b);
    bmf_draw2d_shutdown();
    bmf_gpu_stats after = bmf_gpu_get_stats();
    bmf_check(t, after.bindsets == 0 && after.pipelines == 0 && after.buffers == 0 && after.samplers == 0 && after.textures == 0,
              __FILE__, __LINE__, "draw2d shutdown destroyed every GPU object it made");
    bmf_gpu_shutdown();
}

// A new texture in a destroyed texture's slot gets a new bind set; the old one is destroyed at
// the next begin_frame.
static void test_draw2d_bindset_retire(bmf_test* t) {
    draw2d_test_init_null(0);
    bmf_gpu_texture target = draw2d_test_target(16, 16);
    bmf_gpu_texture_desc texture_desc = { BMF_GPU_FORMAT_RGBA8_UNORM, BMF_GPU_TEXTURE_SAMPLED, 2, 2, 0, "sprite" };
    bmf_rect full = bmf_rect_make(0.0f, 0.0f, 1.0f, 1.0f);
    bmf_gpu_texture first = bmf_gpu_texture_create(&texture_desc);
    bmf_gpu_texture second = { 0 };
    for (i32 frame = 0; frame < 3; frame++) {
        draw2d_test_begin_frame_pixels();
        if (frame == 1) {
            bmf_check_eq_i64(t, bmf_gpu_get_stats().bindsets, 1, __FILE__, __LINE__, "frame 1 start: the first texture's bind set");
            bmf_gpu_texture_destroy(first);
            second = bmf_gpu_texture_create(&texture_desc);
            bmf_check(t, (second.id >> BMF_HANDLE_GENERATION_BITS) == (first.id >> BMF_HANDLE_GENERATION_BITS) && second.id != first.id,
                      __FILE__, __LINE__, "the new texture reuses the slot with a new generation");
        }
        if (frame == 2) bmf_check_eq_i64(t, bmf_gpu_get_stats().bindsets, 1, __FILE__, __LINE__, "frame 2 start: the retired bind set is gone");
        if (frame < 2) bmf_draw_sprite(0, frame == 0 ? first : second, full, bmf_v2_make(8.0f, 8.0f), bmf_v2_make(4.0f, 4.0f), 0.0f, 0xFFFFFFFFu);
        bmf_gpu_begin_frame();
        draw2d_test_begin_pass(target, (bmf_v4){ 0.0f, 0.0f, 0.0f, 1.0f });
        bmf_draw2d_flush(16, 16, BMF_GPU_FORMAT_RGBA8_UNORM);
        bmf_gpu_end_pass();
        bmf_gpu_end_frame();
        if (frame == 1) bmf_check_eq_i64(t, bmf_gpu_get_stats().bindsets, 2, __FILE__, __LINE__, "frame 1 end: retired + new bind set");
    }
    bmf_gpu_texture_destroy(second);
    bmf_gpu_texture_destroy(target);
    draw2d_test_shutdown();
}

// Submits past max_instances are dropped and counted; a later flush still draws the rest.
static void test_draw2d_budget(bmf_test* t) {
    draw2d_test_init_null(4);
    bmf_gpu_texture target = draw2d_test_target(16, 16);
    draw2d_test_begin_frame_pixels();
    bmf_gpu_begin_frame();
    draw2d_test_begin_pass(target, (bmf_v4){ 0.0f, 0.0f, 0.0f, 1.0f });
    for (i32 i = 0; i < 3; i++) bmf_draw_circle(0, bmf_v2_make(8.0f, 8.0f), 2.0f, 0xFFFFFFFFu);
    bmf_draw2d_flush(16, 16, BMF_GPU_FORMAT_RGBA8_UNORM);
    for (i32 i = 0; i < 3; i++) bmf_draw_circle(0, bmf_v2_make(8.0f, 8.0f), 2.0f, 0xFFFFFFFFu);
    bmf_draw2d_flush(16, 16, BMF_GPU_FORMAT_RGBA8_UNORM);
    bmf_gpu_end_pass();
    bmf_gpu_end_frame();
    bmf_draw2d_stats stats = bmf_draw2d_get_stats();
    bmf_check_eq_i64(t, stats.frame_instances, 4, __FILE__, __LINE__, "4 accepted in the frame");
    bmf_check_eq_i64(t, stats.dropped, 2, __FILE__, __LINE__, "2 dropped");
    bmf_check_eq_i64(t, stats.instances, 1, __FILE__, __LINE__, "the second flush drew the one that fit");
    bmf_check_eq_i64(t, bmf_gpu_get_stats().instances, 4, __FILE__, __LINE__, "4 GPU instances");
    bmf_gpu_texture_destroy(target);
    draw2d_test_shutdown();
}

// Exact pixels on WARP (64x64 RGBA8, cleared black). Pixel centers are at .5, so edges on
// integer coordinates are exact.
//   A: layer 1 red over layer 0 green (green submitted later): red wins (layer order).
//   B: two layer-3 rects, blue then yellow: the later one wins (submission order in a layer).
//   C: a 8x24 rect rotated by pi/2 covers 24x8 (rotation).
//   D: a sprite showing only texel (1, 0) of a 2x2 texture (uv rect of zero size at its center).
//   E: half-transparent white over black: 128 (premultiplied output).
static void test_draw2d_d3d11_pixels(bmf_test* t) {
    gpu_d3d11_test_init(t, 0, 0, 0, 1);
    i32 live_baseline = gpu_d3d11_live_object_count();
    bmf_draw2d_desc desc = { 0 };
    bmf_draw2d_init(&desc);
    static const u8 texels[16] = { 10, 20, 30, 255,   40, 50, 60, 255,   70, 80, 90, 255,   100, 110, 120, 255 };
    bmf_gpu_texture_desc texture_desc = { BMF_GPU_FORMAT_RGBA8_UNORM, BMF_GPU_TEXTURE_SAMPLED, 2, 2, texels, "texels" };
    bmf_gpu_texture texture = bmf_gpu_texture_create(&texture_desc);
    bmf_gpu_texture target = draw2d_test_target(64, 64);

    draw2d_test_begin_frame_pixels();
    bmf_draw_rect(1, bmf_v2_make(8.0f, 8.0f), bmf_v2_make(16.0f, 16.0f), 0.0f, 0xFF0000FFu);                // A red   [0,16)^2
    bmf_draw_rect(0, bmf_v2_make(8.0f, 8.0f), bmf_v2_make(16.0f, 16.0f), 0.0f, 0x00FF00FFu);                // A green (below)
    bmf_draw_rect(3, bmf_v2_make(40.0f, 8.0f), bmf_v2_make(16.0f, 16.0f), 0.0f, 0x0000FFFFu);               // B blue  [32,48)x[0,16)
    bmf_draw_rect(3, bmf_v2_make(44.0f, 8.0f), bmf_v2_make(8.0f, 16.0f), 0.0f, 0xFFFF00FFu);                // B yellow [40,48)x[0,16)
    bmf_draw_rect(2, bmf_v2_make(16.0f, 32.0f), bmf_v2_make(8.0f, 24.0f), BMF_HALF_PI, 0xFFFFFFFFu);       // C [4,28)x[28,36)
    bmf_draw_sprite(2, texture, bmf_rect_make(0.75f, 0.25f, 0.0f, 0.0f), bmf_v2_make(48.0f, 32.0f), bmf_v2_make(8.0f, 8.0f), 0.0f,
                    0xFFFFFFFFu);                                                                                // D [44,52)x[28,36)
    bmf_draw_rect(2, bmf_v2_make(32.0f, 56.0f), bmf_v2_make(16.0f, 8.0f), 0.0f, 0xFFFFFF80u);               // E [24,40)x[52,60)
    bmf_gpu_begin_frame();
    draw2d_test_begin_pass(target, (bmf_v4){ 0.0f, 0.0f, 0.0f, 1.0f });
    bmf_draw2d_flush(64, 64, BMF_GPU_FORMAT_RGBA8_UNORM);
    bmf_gpu_end_pass();
    bmf_gpu_end_frame();
    // Sorted runs: layer 0-2 white (green, red, C, E), layer 2 texture (D), layer 3 white = 3 draws.
    bmf_check_eq_i64(t, bmf_draw2d_get_stats().batches, 3, __FILE__, __LINE__, "3 draws for 7 instances");

    static u8 expected[64 * 64 * 4];
    for (i32 i = 0; i < 64 * 64; i++) { expected[i * 4 + 0] = 0; expected[i * 4 + 1] = 0; expected[i * 4 + 2] = 0; expected[i * 4 + 3] = 255; }
    struct { i32 x0, y0, x1, y1; u8 r, g, b, a; } boxes[6] = {
        { 0, 0, 16, 16, 255, 0, 0, 255 },
        { 32, 0, 40, 16, 0, 0, 255, 255 },
        { 40, 0, 48, 16, 255, 255, 0, 255 },
        { 4, 28, 28, 36, 255, 255, 255, 255 },
        { 44, 28, 52, 36, 40, 50, 60, 255 },
        { 24, 52, 40, 60, 128, 128, 128, 255 },   // alpha: src 128 + dst 255 * (1 - 128/255) = 255
    };
    for (i32 b = 0; b < 6; b++)
        for (i32 y = boxes[b].y0; y < boxes[b].y1; y++)
            for (i32 x = boxes[b].x0; x < boxes[b].x1; x++) {
                u8* px = &expected[(y * 64 + x) * 4];
                px[0] = boxes[b].r; px[1] = boxes[b].g; px[2] = boxes[b].b; px[3] = boxes[b].a;
            }
    gpu_d3d11_test_compare(t, gpu_d3d11_test_read(t, target), expected, 64, 64, 1, "draw2d sheet", __LINE__);

    bmf_gpu_texture_destroy(target);
    bmf_gpu_texture_destroy(texture);
    bmf_draw2d_shutdown();
    bmf_check_eq_i64(t, gpu_d3d11_live_object_count(), live_baseline, __FILE__, __LINE__, "draw2d shutdown released every device object it made");
    bmf_gpu_shutdown();
}

// CPU cost of 10 000 sprites: submit, sort, gather, upload and batch (null backend, debug build).
static void test_draw2d_bench_sprites_10k(bmf_test* t) {
    draw2d_test_init_null(0);
    bmf_gpu_texture_desc texture_desc = { BMF_GPU_FORMAT_RGBA8_UNORM, BMF_GPU_TEXTURE_SAMPLED, 2, 2, 0, "sprite" };
    bmf_gpu_texture textures[4];
    for (i32 i = 0; i < 4; i++) textures[i] = bmf_gpu_texture_create(&texture_desc);
    bmf_gpu_texture target = draw2d_test_target(64, 64);
    bmf_rect full = bmf_rect_make(0.0f, 0.0f, 1.0f, 1.0f);
    bmf_rng rng;
    bmf_rng_seed(&rng, 42, 7);
    i64 best_ns = 1ll << 62;
    for (i32 run = 0; run < 5; run++) {
        draw2d_test_begin_frame_pixels();
        bmf_gpu_begin_frame();
        draw2d_test_begin_pass(target, (bmf_v4){ 0.0f, 0.0f, 0.0f, 1.0f });
        i64 start = bmf_platform_time_ns();
        for (i32 i = 0; i < 10000; i++) {
            u32 r = bmf_rng_next_u32(&rng);
            bmf_draw_sprite((i32)(r & 7u), textures[(r >> 3) & 3u], full, bmf_v2_make((f32)(r >> 16 & 1023u), (f32)(r >> 5 & 511u)),
                            bmf_v2_make(16.0f, 16.0f), 0.0f, 0xFFFFFFFFu);
        }
        bmf_draw2d_flush(64, 64, BMF_GPU_FORMAT_RGBA8_UNORM);
        i64 elapsed = bmf_platform_time_ns() - start;
        if (elapsed < best_ns) best_ns = elapsed;
        bmf_gpu_end_pass();
        bmf_gpu_end_frame();
    }
    bmf_check_eq_i64(t, bmf_draw2d_get_stats().batches, 32, __FILE__, __LINE__, "8 layers x 4 textures = 32 draws");
    char note[128];
    bmf_fmt_buf(note, (i32)sizeof note, "10k sprites submit+sort+upload+batch: best of 5 %i us (debug -O0, null backend)", (i32)(best_ns / 1000));
    bmf_test_note(t, note);
    bmf_gpu_texture_destroy(target);
    for (i32 i = 0; i < 4; i++) bmf_gpu_texture_destroy(textures[i]);
    draw2d_test_shutdown();
}

// ---------------------------------------------------------------- 1A-20b: SDF coverage
// CPU twins of the draw2d.hlsl distance functions (pixels, negative inside).
static f32 draw2d_test_distance_round_box(bmf_v2 p, bmf_v2 half_size, f32 r) {
    r = bmf_fminf(r, bmf_fminf(half_size.x, half_size.y));
    f32 qx = bmf_fabsf(p.x) - half_size.x + r, qy = bmf_fabsf(p.y) - half_size.y + r;
    f32 ox = bmf_fmaxf(qx, 0.0f), oy = bmf_fmaxf(qy, 0.0f);
    return bmf_sqrtf(ox * ox + oy * oy) + bmf_fminf(bmf_fmaxf(qx, qy), 0.0f) - r;
}

static f32 draw2d_test_distance_segment(bmf_v2 p, bmf_v2 a, bmf_v2 b) {
    bmf_v2 pa = bmf_v2_sub(p, a), ba = bmf_v2_sub(b, a);
    f32 length_sq = bmf_v2_dot(ba, ba);
    f32 h = length_sq > 0.0f ? bmf_saturatef(bmf_v2_dot(pa, ba) / length_sq) : 0.0f;
    return bmf_v2_length(bmf_v2_sub(pa, bmf_v2_scale(ba, h)));
}

static f32 draw2d_test_distance_triangle(bmf_v2 p, bmf_v2 p0, bmf_v2 p1, bmf_v2 p2) {
    bmf_v2 e[3] = { bmf_v2_sub(p1, p0), bmf_v2_sub(p2, p1), bmf_v2_sub(p0, p2) };
    bmf_v2 v[3] = { bmf_v2_sub(p, p0), bmf_v2_sub(p, p1), bmf_v2_sub(p, p2) };
    f32 s = e[0].x * e[2].y - e[0].y * e[2].x >= 0.0f ? 1.0f : -1.0f;
    f32 best_sq = BMF_F32_MAX, best_side = BMF_F32_MAX;
    for (i32 i = 0; i < 3; i++) {
        f32 h = bmf_saturatef(bmf_v2_dot(v[i], e[i]) / bmf_fmaxf(bmf_v2_dot(e[i], e[i]), 1e-12f));
        bmf_v2 q = bmf_v2_sub(v[i], bmf_v2_scale(e[i], h));
        best_sq = bmf_fminf(best_sq, bmf_v2_dot(q, q));
        best_side = bmf_fminf(best_side, s * (v[i].x * e[i].y - v[i].y * e[i].x));
    }
    return best_side > 0.0f ? -bmf_sqrtf(best_sq) : bmf_sqrtf(best_sq);
}

// A test shape: what is submitted and what the reference evaluates.
typedef struct draw2d_test_shape {
    bmf_draw_kind kind;
    bmf_v2 a, b, c;         // RECT/SPRITE/CIRCLE: a = center; CAPSULE: a..b; TRIANGLE: a, b, c
    bmf_v2 size;            // RECT/SPRITE: full size; CIRCLE: size.x = radius; CAPSULE: size.x = thickness
    f32 rotation, corner, stroke;
} draw2d_test_shape;

// Shapes in white over black, plus one rotated sprite showing a single texel (200, 100, 50).
// Every kind, both triangle windings, a ring, sub-pixel and thin shapes, and rotated boxes,
// compared with a CPU evaluation of the same distance functions.
static void test_draw2d_d3d11_sdf_sheet(bmf_test* t) {
    enum { W = 128, H = 128 };
    static const draw2d_test_shape shapes[] = {
        { BMF_DRAW_KIND_RECT, { 20.0f, 20.0f }, { 0 }, { 0 }, { 26.0f, 12.0f }, 0.3f, 0.0f, 0.0f },
        { BMF_DRAW_KIND_RECT, { 62.0f, 20.0f }, { 0 }, { 0 }, { 30.0f, 20.0f }, -0.2f, 6.0f, 0.0f },
        { BMF_DRAW_KIND_CIRCLE, { 102.0f, 20.0f }, { 0 }, { 0 }, { 12.5f, 0.0f }, 0.0f, 0.0f, 0.0f },
        { BMF_DRAW_KIND_CIRCLE, { 20.0f, 62.0f }, { 0 }, { 0 }, { 14.0f, 0.0f }, 0.0f, 0.0f, 3.0f },
        { BMF_DRAW_KIND_CAPSULE, { 42.0f, 50.0f }, { 82.0f, 72.0f }, { 0 }, { 5.0f, 0.0f }, 0.0f, 0.0f, 0.0f },
        { BMF_DRAW_KIND_TRIANGLE, { 96.0f, 46.0f }, { 124.0f, 74.0f }, { 90.0f, 80.0f }, { 0 }, 0.0f, 0.0f, 0.0f },
        { BMF_DRAW_KIND_TRIANGLE, { 40.0f, 76.0f }, { 38.0f, 88.0f }, { 52.0f, 84.0f }, { 0 }, 0.0f, 0.0f, 0.0f },
        { BMF_DRAW_KIND_CAPSULE, { 58.0f, 96.0f }, { 122.0f, 110.0f }, { 0 }, { 1.5f, 0.0f }, 0.0f, 0.0f, 0.0f },
        { BMF_DRAW_KIND_CIRCLE, { 70.3f, 120.6f }, { 0 }, { 0 }, { 1.2f, 0.0f }, 0.0f, 0.0f, 0.0f },
        { BMF_DRAW_KIND_SPRITE, { 26.0f, 108.0f }, { 0 }, { 0 }, { 32.0f, 20.0f }, 0.5f, 0.0f, 0.0f },
    };
    i32 shape_count = (i32)(sizeof shapes / sizeof shapes[0]);
    gpu_d3d11_test_init(t, 0, 0, 0, 1);
    bmf_draw2d_desc desc = { 0 };
    bmf_draw2d_init(&desc);
    static const u8 texels[16] = { 0, 0, 0, 255,   200, 100, 50, 255,   0, 0, 0, 255,   0, 0, 0, 255 };
    bmf_gpu_texture_desc texture_desc = { BMF_GPU_FORMAT_RGBA8_UNORM, BMF_GPU_TEXTURE_SAMPLED, 2, 2, texels, "texels" };
    bmf_gpu_texture texture = bmf_gpu_texture_create(&texture_desc);
    bmf_gpu_texture target = draw2d_test_target(W, H);

    draw2d_test_begin_frame_pixels();
    for (i32 i = 0; i < shape_count; i++) {
        const draw2d_test_shape* s = &shapes[i];
        switch (s->kind) {
        case BMF_DRAW_KIND_RECT: bmf_draw_rect_rounded(0, s->a, s->size, s->rotation, s->corner, 0xFFFFFFFFu); break;
        case BMF_DRAW_KIND_CIRCLE:
            if (s->stroke > 0.0f) bmf_draw_ring(0, s->a, s->size.x, s->stroke, 0xFFFFFFFFu);
            else bmf_draw_circle(0, s->a, s->size.x, 0xFFFFFFFFu);
            break;
        case BMF_DRAW_KIND_CAPSULE: bmf_draw_line(0, s->a, s->b, s->size.x, 0xFFFFFFFFu); break;
        case BMF_DRAW_KIND_TRIANGLE: bmf_draw_triangle(0, s->a, s->b, s->c, 0xFFFFFFFFu); break;
        case BMF_DRAW_KIND_SPRITE: bmf_draw_sprite(0, texture, bmf_rect_make(0.75f, 0.25f, 0.0f, 0.0f), s->a, s->size, s->rotation, 0xFFFFFFFFu); break;
        default: break;
        }
    }
    bmf_gpu_begin_frame();
    draw2d_test_begin_pass(target, (bmf_v4){ 0.0f, 0.0f, 0.0f, 1.0f });
    bmf_draw2d_flush(W, H, BMF_GPU_FORMAT_RGBA8_UNORM);
    bmf_gpu_end_pass();
    bmf_gpu_end_frame();
    bmf_check(t, bmf_draw2d_get_stats().batches <= 3, __FILE__, __LINE__, "test sheet in at most 3 draws (1A-20 acceptance)");
    bmf_check_eq_i64(t, bmf_draw2d_get_stats().batches, 2, __FILE__, __LINE__, "shapes in one draw, the sprite in another");

    static u8 actual[W * H * 4];
    bmf_check_eq_i64(t, bmf_gpu_texture_read_begin(target), BMF_OK, __FILE__, __LINE__, "read_begin");
    bmf_check_eq_i64(t, bmf_gpu_texture_read_end(actual, (i64)sizeof actual), BMF_OK, __FILE__, __LINE__, "read_end");
    test_golden_image(t, "draw2d.shapes", actual, W, H);   // tests/golden/d3d11 (task 1A-41)

    // Reference: per pixel center, coverage = saturate(0.5 - d), composited premultiplied over
    // black in draw order (the white shapes in submission order, then the sprite).
    static u8 expected[W * H * 4];
    for (i32 y = 0; y < H; y++)
        for (i32 x = 0; x < W; x++) {
            bmf_v2 p = bmf_v2_make((f32)x + 0.5f, (f32)y + 0.5f);
            f32 rgb[3] = { 0.0f, 0.0f, 0.0f };
            for (i32 pass = 0; pass < 2; pass++)
                for (i32 i = 0; i < shape_count; i++) {
                    const draw2d_test_shape* s = &shapes[i];
                    if ((s->kind == BMF_DRAW_KIND_SPRITE) != (pass == 1)) continue;
                    f32 d;
                    if (s->kind == BMF_DRAW_KIND_CIRCLE) d = bmf_v2_distance(p, s->a) - s->size.x;
                    else if (s->kind == BMF_DRAW_KIND_CAPSULE) d = draw2d_test_distance_segment(p, s->a, s->b) - s->size.x * 0.5f;
                    else if (s->kind == BMF_DRAW_KIND_TRIANGLE) d = draw2d_test_distance_triangle(p, s->a, s->b, s->c);
                    else {
                        // The instance stores -rotation; the shader's inverse rotation uses its cos/sin.
                        f32 sn, cs;
                        bmf_sincosf(-s->rotation, &sn, &cs);
                        bmf_v2 o = bmf_v2_sub(p, s->a);
                        bmf_v2 local = bmf_v2_make(cs * o.x + sn * o.y, cs * o.y - sn * o.x);
                        d = draw2d_test_distance_round_box(local, bmf_v2_scale(s->size, 0.5f), s->corner);
                    }
                    if (s->stroke > 0.0f) d = bmf_fabsf(d + s->stroke * 0.5f) - s->stroke * 0.5f;
                    f32 coverage = bmf_saturatef(0.5f - d);
                    f32 color[3] = { 1.0f, 1.0f, 1.0f };
                    if (s->kind == BMF_DRAW_KIND_SPRITE) { color[0] = 200.0f / 255.0f; color[1] = 100.0f / 255.0f; color[2] = 50.0f / 255.0f; }
                    for (i32 ch = 0; ch < 3; ch++) rgb[ch] = color[ch] * coverage + rgb[ch] * (1.0f - coverage);
                }
            u8* px = &expected[(y * W + x) * 4];
            for (i32 ch = 0; ch < 3; ch++) px[ch] = (u8)bmf_f32_to_i32(rgb[ch] * 255.0f + 0.5f);
            px[3] = 255;
        }
    i32 max_difference = 0, edge_pixels = 0;
    for (i32 i = 0; i < W * H * 4; i++) {
        i32 difference = (i32)actual[i] - (i32)expected[i];
        if (difference < 0) difference = -difference;
        if (difference > max_difference) max_difference = difference;
        if (i % 4 == 0 && expected[i] != 0 && expected[i] != 255 && expected[i] != 200) edge_pixels++;
    }
    char note[160];
    bmf_fmt_buf(note, (i32)sizeof note, "max channel difference vs CPU reference %i; %i partially covered pixels; image: out/test/draw2d_sdf_sheet.rgba",
                max_difference, edge_pixels);
    bmf_test_note(t, note);
    bmf_check(t, edge_pixels > 300, __FILE__, __LINE__, "antialiased edges exist");
    gpu_d3d11_test_compare(t, actual, expected, W, H, 1, "sdf sheet vs CPU reference", __LINE__);
    bmf_platform_file_write_atomic("out/test/draw2d_sdf_sheet.rgba", actual, (i64)sizeof actual);   // for viewing; not compared

    bmf_gpu_texture_destroy(target);
    bmf_gpu_texture_destroy(texture);
    bmf_draw2d_shutdown();
    bmf_gpu_shutdown();
}

// ---------------------------------------------------------------- 1A-20c: cameras
static b32 draw2d_test_near(bmf_v2 a, bmf_v2 b, f32 tolerance) { return bmf_fabsf(a.x - b.x) <= tolerance && bmf_fabsf(a.y - b.y) <= tolerance; }

// Camera space <-> screen logical pixels for both spaces, position, scale and rotation.
static void test_draw2d_camera_math(bmf_test* t) {
    bmf_v2 view = bmf_v2_make(64.0f, 64.0f);
    bmf_camera world = bmf_camera_world_default();
    bmf_check(t, draw2d_test_near(bmf_camera_world_to_screen(&world, view, bmf_v2_make(0.0f, 0.0f)), bmf_v2_make(32.0f, 32.0f), 0.0f),
              __FILE__, __LINE__, "world origin at the view center");
    bmf_check(t, draw2d_test_near(bmf_camera_world_to_screen(&world, view, bmf_v2_make(1.0f, 0.0f)), bmf_v2_make(96.0f, 32.0f), 0.0f),
              __FILE__, __LINE__, "+x meter = 64 px right");
    bmf_check(t, draw2d_test_near(bmf_camera_world_to_screen(&world, view, bmf_v2_make(0.0f, 1.0f)), bmf_v2_make(32.0f, -32.0f), 0.0f),
              __FILE__, __LINE__, "+y meter = 64 px up (y up)");
    world.position = bmf_v2_make(2.0f, 1.0f);
    world.pixels_per_unit = 32.0f;
    bmf_check(t, draw2d_test_near(bmf_camera_world_to_screen(&world, view, bmf_v2_make(2.0f, 1.0f)), bmf_v2_make(32.0f, 32.0f), 0.0f),
              __FILE__, __LINE__, "camera position at the view center");
    bmf_check(t, draw2d_test_near(bmf_camera_world_to_screen(&world, view, bmf_v2_make(3.0f, 1.0f)), bmf_v2_make(64.0f, 32.0f), 0.0f),
              __FILE__, __LINE__, "32 px per meter");
    world = bmf_camera_world_default();
    world.rotation = BMF_HALF_PI;
    bmf_check(t, draw2d_test_near(bmf_camera_world_to_screen(&world, view, bmf_v2_make(1.0f, 0.0f)), bmf_v2_make(32.0f, 96.0f), 1e-4f),
              __FILE__, __LINE__, "view turned counter-clockwise: world +x appears pointing down");

    bmf_camera screen = bmf_camera_screen_default();
    bmf_check(t, draw2d_test_near(bmf_camera_world_to_screen(&screen, view, bmf_v2_make(10.0f, 20.0f)), bmf_v2_make(10.0f, 20.0f), 0.0f),
              __FILE__, __LINE__, "screen default is the identity");
    screen.position = bmf_v2_make(10.0f, 20.0f);
    screen.pixels_per_unit = 2.0f;
    bmf_check(t, draw2d_test_near(bmf_camera_world_to_screen(&screen, view, bmf_v2_make(11.0f, 21.0f)), bmf_v2_make(2.0f, 2.0f), 0.0f),
              __FILE__, __LINE__, "screen scroll and scale");
    screen.rotation = BMF_HALF_PI;
    bmf_check(t, draw2d_test_near(bmf_camera_world_to_screen(&screen, view, bmf_v2_make(11.0f, 20.0f)), bmf_v2_make(0.0f, 2.0f), 1e-4f),
              __FILE__, __LINE__, "screen view turned counter-clockwise: +x appears pointing down");

    // Round trips at many points for several cameras.
    bmf_camera cameras[3] = { bmf_camera_world_default(), bmf_camera_screen_default(), { BMF_CAMERA_WORLD, { -3.5f, 7.25f }, 20.0f, 0.7f } };
    cameras[1].rotation = -1.1f;
    cameras[1].pixels_per_unit = 1.5f;
    f32 worst = 0.0f;
    for (i32 c = 0; c < 3; c++)
        for (i32 i = 0; i < 100; i++) {
            bmf_v2 p = bmf_v2_make((f32)(i % 10) * 3.1f - 12.0f, (f32)(i / 10) * 2.7f - 9.0f);
            bmf_v2 back = bmf_camera_screen_to_world(&cameras[c], view, bmf_camera_world_to_screen(&cameras[c], view, p));
            worst = bmf_fmaxf(worst, bmf_fmaxf(bmf_fabsf(back.x - p.x), bmf_fabsf(back.y - p.y)));
        }
    bmf_check(t, worst < 1e-4f, __FILE__, __LINE__, "screen_to_world inverts world_to_screen");
}

// Submits through cameras on the null backend: every field is mapped to target pixels (view
// 200x100 physical at DPI 2 = 100x50 logical), and the per-layer stacks push and pop.
static void test_draw2d_camera_submit(bmf_test* t) {
    draw2d_test_init_null(0);
    bmf_draw2d_view view = { 200, 100, 2.0f };
    bmf_draw2d_begin_frame(draw2d_test_frame_arena(), view);
    const bmf_draw_instance* d = draw2d.instances;
    // Default world camera: logical center (50, 25), 64 px/m, physical = 2 x logical.
    bmf_draw_rect(0, bmf_v2_make(1.0f, 0.5f), bmf_v2_make(0.5f, 0.25f), 0.3f, 0xFFFFFFFFu);
    bmf_check(t, d[0].position.x == 228.0f && d[0].position.y == -14.0f, __FILE__, __LINE__, "rect center: (50 + 64, 25 - 32) * 2");
    bmf_check(t, d[0].size.x == 64.0f && d[0].size.y == 32.0f && d[0].rotation == -0.3f, __FILE__, __LINE__, "rect size * 128, rotation negated");
    bmf_draw_rect_rounded(0, bmf_v2_make(0.0f, 0.0f), bmf_v2_make(1.0f, 1.0f), 0.0f, 0.125f, 0xFFFFFFFFu);
    bmf_check_eq_u64(t, d[1].kind_corner >> 16, bmf_draw_f16(16.0f), __FILE__, __LINE__, "corner radius 0.125 m = 16 px");
    bmf_draw_ring(0, bmf_v2_make(0.0f, 0.0f), 0.25f, 0.0625f, 0xFFFFFFFFu);
    bmf_check(t, d[2].position.x == 100.0f && d[2].position.y == 50.0f && d[2].size.x == 64.0f && d[2].stroke_softness == bmf_draw_f16(8.0f),
              __FILE__, __LINE__, "ring: center, diameter 64 px, thickness 8 px");
    bmf_draw_line(0, bmf_v2_make(0.0f, 0.0f), bmf_v2_make(0.5f, 0.25f), 0.125f, 0xFFFFFFFFu);
    bmf_check(t, d[3].extra.x == 164.0f && d[3].extra.y == 18.0f && d[3].size.y == 16.0f, __FILE__, __LINE__, "line end b and thickness mapped");
    bmf_draw_triangle(0, bmf_v2_make(0.0f, 0.0f), bmf_v2_make(0.5f, 0.0f), bmf_v2_make(0.0f, 0.5f), 0xFFFFFFFFu);
    bmf_check(t, d[4].extra.x == 164.0f && d[4].extra.y == 50.0f && d[4].extra.z == 100.0f && d[4].extra.w == -14.0f, __FILE__, __LINE__,
              "triangle b and c mapped (y up)");

    // Layer 1: a screen camera, then a rotated world camera on top, then pops.
    bmf_camera screen = bmf_camera_screen_default();
    bmf_draw_camera_push(1, &screen);
    bmf_draw_circle(1, bmf_v2_make(10.0f, 20.0f), 5.0f, 0xFFFFFFFFu);
    bmf_check(t, d[5].position.x == 20.0f && d[5].position.y == 40.0f && d[5].size.x == 20.0f, __FILE__, __LINE__, "screen camera: logical px * DPI");
    bmf_draw_circle(2, bmf_v2_make(0.0f, 0.0f), 0.25f, 0xFFFFFFFFu);
    bmf_check(t, d[6].position.x == 100.0f && d[6].size.x == 64.0f, __FILE__, __LINE__, "layer 2 keeps the default world camera");
    bmf_camera turned = bmf_camera_world_default();
    turned.rotation = 0.5f;
    bmf_draw_camera_push(1, &turned);
    bmf_draw_rect(1, bmf_v2_make(0.0f, 0.0f), bmf_v2_make(1.0f, 1.0f), 0.3f, 0xFFFFFFFFu);
    bmf_check(t, d[7].rotation == 0.5f - 0.3f, __FILE__, __LINE__, "instance rotation = camera rotation - shape rotation");
    bmf_draw_camera_pop(1);
    bmf_draw_circle(1, bmf_v2_make(10.0f, 20.0f), 5.0f, 0xFFFFFFFFu);
    bmf_check(t, d[8].position.x == 20.0f && d[8].position.y == 40.0f, __FILE__, __LINE__, "pop returns to the screen camera");
    bmf_draw_camera_pop(1);
    bmf_draw_circle(1, bmf_v2_make(0.0f, 0.0f), 0.25f, 0xFFFFFFFFu);
    bmf_check(t, d[9].position.x == 100.0f && d[9].position.y == 50.0f, __FILE__, __LINE__, "empty stack: default world camera");

    // The next frame starts with every stack empty.
    bmf_draw_camera_push(3, &screen);
    bmf_draw2d_begin_frame(draw2d_test_frame_arena(), view);
    bmf_draw_circle(3, bmf_v2_make(0.0f, 0.0f), 0.25f, 0xFFFFFFFFu);
    bmf_check(t, draw2d.instances[0].position.x == 100.0f && draw2d.camera_depths[3] == 0, __FILE__, __LINE__, "stacks reset at begin_frame");
    draw2d_test_shutdown();
}

// Expected crash: popping an empty camera stack.
static void test_draw2d_camera_pop_empty(bmf_test* t) {
    draw2d_test_init_null(0);
    bmf_draw2d_view view = { 64, 64, 1.0f };
    bmf_draw2d_begin_frame(draw2d_test_frame_arena(), view);
    bmf_draw_camera_pop(0);
    draw2d_test_shutdown();
}

// Exact pixels on WARP through cameras (64x64 target, DPI 1, default world camera: center
// (32, 32), 64 px/m, y up):
//   A: world rect (-0.25, 0.25) 0.25 x 0.25 m       -> [8,24) x [8,24)    (y up: above center)
//   B: world rect (-0.25, -0.375) 0.25 x 0.125 m    -> [8,24) x [52,60)
//   C: layer 1, world camera turned by pi/2: rect (0.25, 0) 0.25 x 0.125 m -> [28,36) x [40,56)
//   D: world sprite (0.25, 0.25) 0.25 x 0.25 m, 2x2 texture -> [40,56) x [8,24), texture upright
// Then a second frame at DPI 2 (32x32 logical): world rect (0, 0) 0.25 m -> 16 logical = [16,48)^2.
static void test_draw2d_d3d11_camera_pixels(bmf_test* t) {
    gpu_d3d11_test_init(t, 0, 0, 0, 1);
    bmf_draw2d_desc desc = { 0 };
    bmf_draw2d_init(&desc);
    static const u8 texels[16] = { 255, 0, 0, 255,   0, 255, 0, 255,   0, 0, 255, 255,   255, 255, 0, 255 };   // TL red, TR green, BL blue, BR yellow
    bmf_gpu_texture_desc texture_desc = { BMF_GPU_FORMAT_RGBA8_UNORM, BMF_GPU_TEXTURE_SAMPLED, 2, 2, texels, "texels" };
    bmf_gpu_texture texture = bmf_gpu_texture_create(&texture_desc);
    bmf_gpu_texture target = draw2d_test_target(64, 64);

    bmf_draw2d_view view = { 64, 64, 1.0f };
    bmf_draw2d_begin_frame(draw2d_test_frame_arena(), view);
    bmf_draw_rect(0, bmf_v2_make(-0.25f, 0.25f), bmf_v2_make(0.25f, 0.25f), 0.0f, 0xFFFFFFFFu);
    bmf_draw_rect(0, bmf_v2_make(-0.25f, -0.375f), bmf_v2_make(0.25f, 0.125f), 0.0f, 0xFFFFFFFFu);
    bmf_camera turned = bmf_camera_world_default();
    turned.rotation = BMF_HALF_PI;
    bmf_draw_camera_push(1, &turned);
    bmf_draw_rect(1, bmf_v2_make(0.25f, 0.0f), bmf_v2_make(0.25f, 0.125f), 0.0f, 0xFFFFFFFFu);
    bmf_draw_sprite(0, texture, bmf_rect_make(0.0f, 0.0f, 1.0f, 1.0f), bmf_v2_make(0.25f, 0.25f), bmf_v2_make(0.25f, 0.25f), 0.0f, 0xFFFFFFFFu);
    bmf_gpu_begin_frame();
    draw2d_test_begin_pass(target, (bmf_v4){ 0.0f, 0.0f, 0.0f, 1.0f });
    bmf_draw2d_flush(64, 64, BMF_GPU_FORMAT_RGBA8_UNORM);
    bmf_gpu_end_pass();
    bmf_gpu_end_frame();
    const u8* actual = gpu_d3d11_test_read(t, target);

    // Sprite corners: pixels whose uv lies within a quarter texel of the edge sample one texel only.
    struct { i32 x, y; u8 r, g, b; const char* what; } corners[4] = {
        { 41, 9, 255, 0, 0, "sprite top-left = texel (0,0) red: the texture is upright in world space" },
        { 54, 9, 0, 255, 0, "sprite top-right = green" },
        { 41, 22, 0, 0, 255, "sprite bottom-left = blue" },
        { 54, 22, 255, 255, 0, "sprite bottom-right = yellow" },
    };
    for (i32 i = 0; i < 4; i++) {
        const u8* px = &actual[(corners[i].y * 64 + corners[i].x) * 4];
        bmf_check(t, px[0] == corners[i].r && px[1] == corners[i].g && px[2] == corners[i].b, __FILE__, __LINE__, corners[i].what);
    }
    static u8 expected[64 * 64 * 4];
    gpu_d3d11_test_fill(expected, 64, 64, 0, 0, 0, 255);
    struct { i32 x0, y0, x1, y1; } boxes[3] = { { 8, 8, 24, 24 }, { 8, 52, 24, 60 }, { 28, 40, 36, 56 } };
    for (i32 b = 0; b < 3; b++)
        for (i32 y = boxes[b].y0; y < boxes[b].y1; y++)
            for (i32 x = boxes[b].x0; x < boxes[b].x1; x++) { u8* px = &expected[(y * 64 + x) * 4]; px[0] = px[1] = px[2] = 255; }
    for (i32 y = 8; y < 24; y++)   // the sprite area is checked above (filtered texels), not here
        for (i32 x = 40; x < 56; x++) bmf_memcpy(&expected[(y * 64 + x) * 4], &actual[(y * 64 + x) * 4], 4);
    gpu_d3d11_test_compare(t, actual, expected, 64, 64, 1, "world camera sheet", __LINE__);
    test_golden_image(t, "draw2d.cameras", actual, 64, 64);   // tests/golden/d3d11 (task 1A-41)

    bmf_draw2d_view hidpi = { 64, 64, 2.0f };
    bmf_draw2d_begin_frame(draw2d_test_frame_arena(), hidpi);
    bmf_draw_rect(0, bmf_v2_make(0.0f, 0.0f), bmf_v2_make(0.25f, 0.25f), 0.0f, 0xFFFFFFFFu);
    bmf_gpu_begin_frame();
    draw2d_test_begin_pass(target, (bmf_v4){ 0.0f, 0.0f, 0.0f, 1.0f });
    bmf_draw2d_flush(64, 64, BMF_GPU_FORMAT_RGBA8_UNORM);
    bmf_gpu_end_pass();
    bmf_gpu_end_frame();
    gpu_d3d11_test_fill(expected, 64, 64, 0, 0, 0, 255);
    for (i32 y = 16; y < 48; y++)
        for (i32 x = 16; x < 48; x++) { u8* px = &expected[(y * 64 + x) * 4]; px[0] = px[1] = px[2] = 255; }
    gpu_d3d11_test_compare(t, gpu_d3d11_test_read(t, target), expected, 64, 64, 0, "DPI 2: 0.25 m = 16 logical = 32 physical px", __LINE__);

    bmf_gpu_texture_destroy(target);
    bmf_gpu_texture_destroy(texture);
    bmf_draw2d_shutdown();
    bmf_gpu_shutdown();
}
