// Tests for the D3D11 backend (task 1A-14). Every test runs on WARP with the debug layer on (when
// installed), so results do not depend on the machine's GPU or driver, and debug-layer errors
// stop the test (gpu_d3d11_drain_messages panics on them). 1A-14a: device, swapchain, frames,
// resize, device-lost message.

static void gpu_d3d11_test_init(bmf_test* t, void* window, i32 width, i32 height, b32 software) {
    bmf_gpu_desc desc;
    bmf_memset(&desc, 0, sizeof desc);
    desc.backend = BMF_GPU_BACKEND_D3D11;
    desc.window = window;
    desc.surface_width = width;
    desc.surface_height = height;
    desc.software = software;
    desc.vsync = 0;
    desc.debug_layer = 1;
    bmf_err err = bmf_gpu_init(&desc);
    bmf_check_eq_i64(t, err, BMF_OK, __FILE__, __LINE__, "D3D11 init");
    if (err != BMF_OK) bmf_panic("gpu d3d11 test: D3D11 init failed");
}

// Opens a hidden test window and returns its HWND with the client size in pixels.
static void* gpu_d3d11_test_window(bmf_test* t, i32* out_width, i32* out_height) {
    bmf_window_desc window_desc = { "bmf gpu test", 320, 200, 1 };
    bmf_check(t, bmf_platform_window_open(&window_desc), __FILE__, __LINE__, "open hidden window");
    bmf_window_size size = bmf_platform_window_size();
    *out_width = size.width;
    *out_height = size.height;
    return bmf_platform_window_native();
}

// One frame with a cleared surface pass. Returns begin_frame's result.
static b32 gpu_d3d11_test_frame(f32 red) {
    b32 usable = bmf_gpu_begin_frame();
    bmf_gpu_pass_desc pass;
    bmf_memset(&pass, 0, sizeof pass);
    pass.color = bmf_gpu_surface();
    pass.color_load = BMF_GPU_LOAD_CLEAR;
    pass.clear_color = (bmf_v4){ red, 0.25f, 0.5f, 1.0f };
    pass.label = "test clear";
    bmf_gpu_begin_pass(&pass);
    bmf_gpu_end_pass();
    bmf_gpu_end_frame();
    return usable;
}

static void test_gpu_d3d11_headless(bmf_test* t) {
    // Hardware requested: a machine without a usable GPU falls back to WARP; either must start.
    gpu_d3d11_test_init(t, 0, 0, 0, 0);
    bmf_check(t, bmf_gpu_backend_active() == BMF_GPU_BACKEND_D3D11, __FILE__, __LINE__, "backend is D3D11");
    bmf_check(t, gpu_d3d11.feature_level >= WIN32_D3D_FEATURE_LEVEL_11_0, __FILE__, __LINE__, "feature level 11_0 or better");
    bmf_check(t, gpu_d3d11.adapter_name[0] != 0, __FILE__, __LINE__, "adapter name read");
    char hardware_note[300];
    bmf_fmt_buf(hardware_note, (i32)sizeof hardware_note, "hardware request got '%s'%s, feature level 0x%x", gpu_d3d11.adapter_name,
                gpu_d3d11.warp ? " (WARP fallback)" : "", gpu_d3d11.feature_level);
    bmf_check_eq_u64(t, bmf_gpu_surface().id, 0, __FILE__, __LINE__, "no window, no surface");
    bmf_check(t, bmf_gpu_begin_frame() == 0, __FILE__, __LINE__, "nothing to present headless");
    bmf_gpu_end_frame();
    bmf_gpu_shutdown();

    // Forced WARP (headless goldens, SPEC §9.1).
    gpu_d3d11_test_init(t, 0, 0, 0, 1);
    bmf_check(t, gpu_d3d11.warp, __FILE__, __LINE__, "software forces WARP");
    char note[600];
    bmf_fmt_buf(note, (i32)sizeof note, "%s; forced: '%s', feature level 0x%x, debug layer %s", hardware_note, gpu_d3d11.adapter_name,
                gpu_d3d11.feature_level, gpu_d3d11.debug ? "on" : "not installed");
    bmf_test_note(t, note);
    bmf_check_eq_u64(t, bmf_gpu_get_stats().frames, 0, __FILE__, __LINE__, "no frames yet");
    for (i32 i = 0; i < 3; i++) {
        bmf_gpu_begin_frame();
        bmf_gpu_end_frame();
    }
    bmf_check_eq_u64(t, bmf_gpu_get_stats().frames, 3, __FILE__, __LINE__, "three frames");
    bmf_gpu_shutdown();
}

static void test_gpu_d3d11_surface_present(bmf_test* t) {
    i32 width = 0, height = 0;
    void* window = gpu_d3d11_test_window(t, &width, &height);
    gpu_d3d11_test_init(t, window, width, height, 1);
    bmf_check(t, gpu_d3d11.swapchain != 0 && gpu_d3d11.frame_latency_waitable != 0, __FILE__, __LINE__, "swapchain and latency object");
    bmf_check(t, bmf_gpu_surface_format() == BMF_GPU_FORMAT_BGRA8_UNORM, __FILE__, __LINE__, "surface format");
    i32 w = 0, h = 0;
    bmf_gpu_texture_size(bmf_gpu_surface(), &w, &h);
    bmf_check(t, w == width && h == height, __FILE__, __LINE__, "surface size is the client size");

    // A hidden window may count as occluded: then frames are dropped, never blocked.
    i32 usable_frames = 0;
    for (i32 i = 0; i < 10; i++) usable_frames += gpu_d3d11_test_frame((f32)i / 10.0f);
    bmf_check(t, (i32)gpu_d3d11.presents <= usable_frames, __FILE__, __LINE__, "presents only in usable frames");
    bmf_check_eq_i64(t, bmf_gpu_get_stats().passes, 1, __FILE__, __LINE__, "one pass in the last frame");

    // Visible window: frames present.
    ShowWindow((HWND)window, WIN32_SW_SHOWNOACTIVATE);
    bmf_platform_poll_events();
    u64 presents_before = gpu_d3d11.presents;
    for (i32 i = 0; i < 10; i++) gpu_d3d11_test_frame(0.5f);
    char note[160];
    bmf_fmt_buf(note, (i32)sizeof note, "hidden: %i of 10 frames usable; shown: %lu presents of 10", usable_frames,
                gpu_d3d11.presents - presents_before);
    bmf_test_note(t, note);
    bmf_check(t, gpu_d3d11.presents - presents_before >= 9, __FILE__, __LINE__, "a shown window presents its frames");

    // Minimized: frames are dropped without waiting until restore.
    ShowWindow((HWND)window, WIN32_SW_MINIMIZE);
    bmf_platform_poll_events();
    i32 minimized_usable = 0;
    i64 minimized_start = bmf_platform_time_ns();
    for (i32 i = 0; i < 10; i++) minimized_usable += gpu_d3d11_test_frame(0.5f);
    i64 minimized_ms = (bmf_platform_time_ns() - minimized_start) / 1000000;
    ShowWindow((HWND)window, WIN32_SW_RESTORE);
    bmf_platform_poll_events();
    u64 presents_restored = gpu_d3d11.presents;
    for (i32 i = 0; i < 10; i++) gpu_d3d11_test_frame(0.5f);
    bmf_fmt_buf(note, (i32)sizeof note, "hidden: %i/10 usable; shown: %lu/10 presents; minimized: %i/10 usable in %li ms; restored: %lu/10 presents",
                usable_frames, presents_restored - presents_before - (u64)minimized_usable, minimized_usable, minimized_ms,
                gpu_d3d11.presents - presents_restored);
    bmf_test_note(t, note);
    bmf_check_eq_i64(t, minimized_usable, 0, __FILE__, __LINE__, "minimized: no frame is usable");
    bmf_check(t, minimized_ms < 500, __FILE__, __LINE__, "minimized frames do not block");
    bmf_check(t, gpu_d3d11.presents - presents_restored >= 9, __FILE__, __LINE__, "restored window presents again");

    bmf_gpu_shutdown();
    bmf_platform_window_close();
    bmf_platform_poll_events();
    bmf_event event;
    while (bmf_platform_event_pop(&event)) {}
}

// Plan acceptance (1A-14): 1000 swapchain resizes leak nothing (live device objects stable).
static void test_gpu_d3d11_resize_stress(bmf_test* t) {
    i32 width = 0, height = 0;
    void* window = gpu_d3d11_test_window(t, &width, &height);
    ShowWindow((HWND)window, WIN32_SW_SHOWNOACTIVATE);
    bmf_platform_poll_events();
    gpu_d3d11_test_init(t, window, width, height, 1);
    gpu_d3d11_test_frame(0.0f);
    i32 live_before = gpu_d3d11_live_object_count();
    i64 start = bmf_platform_time_ns();
    for (i32 i = 0; i < 1000; i++) {
        i32 w = 64 + (i * 37) % 900;
        i32 h = 48 + (i * 53) % 700;
        bmf_gpu_surface_resize(w, h);
        gpu_d3d11_test_frame((f32)(i % 10) / 10.0f);
    }
    bmf_gpu_surface_resize(width, height);
    gpu_d3d11_test_frame(0.0f);
    i64 elapsed_ms = (bmf_platform_time_ns() - start) / 1000000;
    i32 live_after = gpu_d3d11_live_object_count();
    char note[160];
    bmf_fmt_buf(note, (i32)sizeof note, "1000 resizes + frames in %li ms; live objects before %i, after %i", elapsed_ms, live_before, live_after);
    bmf_test_note(t, note);
    if (gpu_d3d11.debug) {
        bmf_check(t, live_before > 0, __FILE__, __LINE__, "live objects are counted");
        bmf_check_eq_i64(t, live_after, live_before, __FILE__, __LINE__, "live object count stable after 1000 resizes");
    }
    bmf_check_eq_i64(t, gpu_d3d11.surface_width, width, __FILE__, __LINE__, "back to the window size");
    bmf_gpu_shutdown();
    bmf_platform_window_close();
    bmf_platform_poll_events();
    bmf_event event;
    while (bmf_platform_event_pop(&event)) {}
}

static void test_gpu_d3d11_hresult_text(bmf_test* t) {
    char text[64];
    gpu_d3d11_hresult_text(WIN32_DXGI_ERROR_DEVICE_HUNG, text, (i32)sizeof text);
    bmf_check(t, bmf_cstr_eq(text, "DXGI_ERROR_DEVICE_HUNG (0x887a0006)"), __FILE__, __LINE__, "named HRESULT");
    gpu_d3d11_hresult_text((HRESULT)0x80001234u, text, (i32)sizeof text);
    bmf_check(t, bmf_cstr_eq(text, "0x80001234"), __FILE__, __LINE__, "unknown HRESULT as hex");
}

// Expected crash: a device-lost HRESULT stops the process with the clean message (the crash line
// reads "bmf_gpu: D3D11 device lost in test: DXGI_ERROR_DEVICE_REMOVED ...").
static void test_gpu_d3d11_device_lost_panics(bmf_test* t) {
    gpu_d3d11_test_init(t, 0, 0, 0, 1);
    gpu_d3d11_check_device_lost(WIN32_DXGI_ERROR_DEVICE_REMOVED, "test");
    bmf_gpu_shutdown();
}

// 1A-14b: buffer contents through every write path (checked with a staging readback), and every
// buffer kind released again (live device objects back to the baseline).
static void test_gpu_d3d11_buffers(bmf_test* t) {
    gpu_d3d11_test_init(t, 0, 0, 0, 1);
    i32 live_baseline = gpu_d3d11_live_object_count();
    static u8 readback[200000];
    static u8 pattern[1024];
    for (i32 i = 0; i < (i32)sizeof pattern; i++) pattern[i] = (u8)(i * 7 + 3);

    // Default buffer with initial data.
    bmf_gpu_buffer_desc with_data = { BMF_GPU_BUFFER_VERTEX, 64, 0, pattern, "with data" };
    bmf_gpu_buffer a = bmf_gpu_buffer_create(&with_data);
    gpu_d3d11_test_read_buffer(gpu_handle_slot(a.id), readback, 64);
    bmf_check_eq_mem(t, readback, pattern, 64, __FILE__, __LINE__, "initial data");

    // Default buffer without data: zeros, also past the first 64 KiB fill chunk; then a partial write.
    bmf_gpu_buffer_desc no_data = { BMF_GPU_BUFFER_VERTEX, 200000, 0, 0, "zeros" };
    bmf_gpu_buffer b = bmf_gpu_buffer_create(&no_data);
    bmf_gpu_buffer_write(b, 140000, pattern, 16);
    gpu_d3d11_test_read_buffer(gpu_handle_slot(b.id), readback, 200000);
    i32 nonzero = 0;
    for (i32 i = 0; i < 200000; i++) nonzero += (i < 140000 || i >= 140016) && readback[i] != 0;
    bmf_check_eq_i64(t, nonzero, 0, __FILE__, __LINE__, "zero-filled outside the written range");
    bmf_check_eq_mem(t, readback + 140000, pattern, 16, __FILE__, __LINE__, "partial write to a default buffer");

    // Dynamic ring: two appends in one frame keep each other (NO_OVERWRITE after the DISCARD).
    bmf_gpu_buffer_desc ring_desc = { BMF_GPU_BUFFER_VERTEX, 4096, 1, 0, "ring" };
    bmf_gpu_buffer ring = bmf_gpu_buffer_create(&ring_desc);
    bmf_gpu_begin_frame();
    bmf_gpu_buffer_write(ring, 0, pattern, 64);
    gpu_d3d11_test_queue_buffer_use(gpu_handle_slot(ring.id), 64);   // as if a draw read the first append
    bmf_gpu_buffer_write(ring, 64, pattern + 100, 64);
    bmf_gpu_end_frame();
    gpu_d3d11_test_read_buffer(gpu_handle_slot(ring.id), readback, 128);
    bmf_check_eq_mem(t, readback, pattern, 64, __FILE__, __LINE__, "first append survives the second");
    bmf_check_eq_mem(t, readback + 64, pattern + 100, 64, __FILE__, __LINE__, "second append");
    bmf_gpu_begin_frame();
    bmf_gpu_buffer_write(ring, 0, pattern + 200, 32);   // the next frame starts over at 0 (DISCARD)
    bmf_gpu_end_frame();
    gpu_d3d11_test_read_buffer(gpu_handle_slot(ring.id), readback, 32);
    bmf_check_eq_mem(t, readback, pattern + 200, 32, __FILE__, __LINE__, "first write of the next frame");

    // Uniform buffers: dynamic appends at binding offsets; default ones hold their create data.
    bmf_gpu_buffer_desc dynamic_uniform_desc = { BMF_GPU_BUFFER_UNIFORM, 1024, 1, 0, "dynamic uniforms" };
    bmf_gpu_buffer dynamic_uniform = bmf_gpu_buffer_create(&dynamic_uniform_desc);
    bmf_gpu_buffer_write(dynamic_uniform, 0, pattern, 64);
    bmf_gpu_buffer_write(dynamic_uniform, 256, pattern + 300, 64);
    gpu_d3d11_test_read_buffer(gpu_handle_slot(dynamic_uniform.id), readback, 320);
    bmf_check_eq_mem(t, readback, pattern, 64, __FILE__, __LINE__, "dynamic uniform at 0");
    bmf_check_eq_mem(t, readback + 256, pattern + 300, 64, __FILE__, __LINE__, "dynamic uniform at 256");
    bmf_gpu_buffer_desc uniform_desc = { BMF_GPU_BUFFER_UNIFORM, 512, 0, pattern, "uniforms" };
    bmf_gpu_buffer uniform = bmf_gpu_buffer_create(&uniform_desc);
    gpu_d3d11_test_read_buffer(gpu_handle_slot(uniform.id), readback, 512);
    bmf_check_eq_mem(t, readback, pattern, 512, __FILE__, __LINE__, "default uniform create data");
    bmf_gpu_buffer_desc zero_uniform_desc = { BMF_GPU_BUFFER_UNIFORM, 1024, 0, 0, "zero uniforms" };
    bmf_gpu_buffer zero_uniform = bmf_gpu_buffer_create(&zero_uniform_desc);
    gpu_d3d11_test_read_buffer(gpu_handle_slot(zero_uniform.id), readback, 1024);
    nonzero = 0;
    for (i32 i = 0; i < 1024; i++) nonzero += readback[i] != 0;
    bmf_check_eq_i64(t, nonzero, 0, __FILE__, __LINE__, "default uniform without data is zeros");

    bmf_gpu_buffer_destroy(a);
    bmf_gpu_buffer_destroy(b);
    bmf_gpu_buffer_destroy(ring);
    bmf_gpu_buffer_destroy(dynamic_uniform);
    bmf_gpu_buffer_destroy(uniform);
    bmf_gpu_buffer_destroy(zero_uniform);
    bmf_check_eq_i64(t, gpu_d3d11_live_object_count(), live_baseline, __FILE__, __LINE__, "every buffer released");
    bmf_gpu_shutdown();
}

// 1A-14b: every texture format and usage, every sampler, passes to offscreen targets with depth;
// everything released again. Debug-layer errors would panic the test.
static void test_gpu_d3d11_textures_samplers(bmf_test* t) {
    gpu_d3d11_test_init(t, 0, 0, 0, 1);
    i32 live_baseline = gpu_d3d11_live_object_count();
    static u8 pixels[16 * 16 * 4];
    for (i32 i = 0; i < (i32)sizeof pixels; i++) pixels[i] = (u8)i;

    bmf_gpu_texture_desc descs[] = {
        { BMF_GPU_FORMAT_RGBA8_UNORM, BMF_GPU_TEXTURE_SAMPLED, 16, 16, pixels, "rgba8" },
        { BMF_GPU_FORMAT_RGBA8_UNORM_SRGB, BMF_GPU_TEXTURE_SAMPLED | BMF_GPU_TEXTURE_COPY_SRC, 16, 16, pixels, "rgba8 srgb" },
        { BMF_GPU_FORMAT_R8_UNORM, BMF_GPU_TEXTURE_SAMPLED, 13, 7, pixels, "r8 odd width" },
        { BMF_GPU_FORMAT_BGRA8_UNORM, BMF_GPU_TEXTURE_RENDER_TARGET | BMF_GPU_TEXTURE_SAMPLED | BMF_GPU_TEXTURE_COPY_SRC, 64, 32, 0, "bgra8 target" },
        { BMF_GPU_FORMAT_RGBA8_UNORM_SRGB, BMF_GPU_TEXTURE_RENDER_TARGET, 64, 32, 0, "srgb target" },
        { BMF_GPU_FORMAT_DEPTH24_STENCIL8, BMF_GPU_TEXTURE_RENDER_TARGET, 64, 32, 0, "depth" },
        { BMF_GPU_FORMAT_RGBA8_UNORM, BMF_GPU_TEXTURE_RENDER_TARGET | BMF_GPU_TEXTURE_SAMPLED, 8, 8, pixels, "target with data" },
    };
    enum { TEXTURE_COUNT = (i32)(sizeof descs / sizeof descs[0]) };
    bmf_gpu_texture textures[TEXTURE_COUNT];
    for (i32 i = 0; i < TEXTURE_COUNT; i++) textures[i] = bmf_gpu_texture_create(&descs[i]);
    bmf_check(t, gpu_d3d11.shader_resource_views[gpu_handle_slot(textures[0].id)] != 0, __FILE__, __LINE__, "sampled texture has a view");
    bmf_check(t, gpu_d3d11.render_target_views[gpu_handle_slot(textures[3].id)] != 0, __FILE__, __LINE__, "color target has a view");
    bmf_check(t, gpu_d3d11.depth_stencil_views[gpu_handle_slot(textures[5].id)] != 0, __FILE__, __LINE__, "depth target has a view");
    bmf_check(t, gpu_d3d11.render_target_views[gpu_handle_slot(textures[0].id)] == 0, __FILE__, __LINE__, "sampled-only texture has no target view");

    bmf_gpu_sampler samplers[4];
    for (i32 i = 0; i < 4; i++) {
        bmf_gpu_sampler_desc sampler_desc = { (bmf_gpu_filter)(i & 1), (bmf_gpu_address)(i >> 1), "test sampler" };
        samplers[i] = bmf_gpu_sampler_create(&sampler_desc);
    }

    for (i32 frame = 0; frame < 2; frame++) {
        bmf_gpu_begin_frame();
        bmf_gpu_pass_desc pass;
        bmf_memset(&pass, 0, sizeof pass);
        pass.color = textures[3];
        pass.depth = textures[5];
        pass.color_load = BMF_GPU_LOAD_CLEAR;
        pass.clear_color = (bmf_v4){ 0.0f, 0.5f, 1.0f, 1.0f };
        pass.depth_load = BMF_GPU_LOAD_CLEAR;
        pass.clear_depth = 1.0f;
        bmf_gpu_begin_pass(&pass);
        bmf_gpu_end_pass();
        pass.color = textures[4];
        pass.color_load = BMF_GPU_LOAD_KEEP;
        pass.depth_load = BMF_GPU_LOAD_KEEP;
        bmf_gpu_begin_pass(&pass);
        bmf_gpu_end_pass();
        bmf_gpu_end_frame();
    }
    bmf_check_eq_i64(t, bmf_gpu_get_stats().passes, 2, __FILE__, __LINE__, "two passes per frame");
    bmf_check_eq_i64(t, bmf_gpu_get_stats().textures, TEXTURE_COUNT, __FILE__, __LINE__, "live textures");

    for (i32 i = 0; i < TEXTURE_COUNT; i++) bmf_gpu_texture_destroy(textures[i]);
    for (i32 i = 0; i < 4; i++) bmf_gpu_sampler_destroy(samplers[i]);
    bmf_check_eq_i64(t, gpu_d3d11_live_object_count(), live_baseline, __FILE__, __LINE__, "every texture, view and sampler released");

    // Shutdown with live resources releases them (the backend walks every slot).
    bmf_gpu_texture leftover = bmf_gpu_texture_create(&descs[3]);
    bmf_gpu_buffer_desc buffer_desc = { BMF_GPU_BUFFER_VERTEX, 256, 1, 0, "leftover" };
    bmf_gpu_buffer_create(&buffer_desc);
    bmf_check(t, gpu_d3d11.textures[gpu_handle_slot(leftover.id)] != 0, __FILE__, __LINE__, "leftover texture exists");
    bmf_gpu_shutdown();
    bmf_check(t, gpu_d3d11.textures[gpu_handle_slot(leftover.id)] == 0 && gpu_d3d11.device == 0, __FILE__, __LINE__, "shutdown released everything");
}

// ---------------------------------------------------------------- 1A-14c: pipelines, draws, readback
// Test shaders live in tests/shaders/d3d11/*.hlsl. `build test` compiles them like the runtime's
// (task 1A-15) into out/gen/test_shaders_d3d11.h: ids GPU_TEST_SHADER_COLOR, ..._TEXTURE from
// GPU_TEST_SHADER_FIRST up, above every runtime shader id. They are copied into the backend's
// table before use.
#include "out/gen/test_shaders_d3d11.h"

static void gpu_d3d11_test_shaders(bmf_test* t) {
    for (i32 id = GPU_TEST_SHADER_FIRST; id < GPU_TEST_SHADER_END; id++) gpu_d3d11_shader_codes[id] = gpu_d3d11_test_shader_codes[id];
    bmf_check(t, gpu_d3d11_shader_codes[GPU_TEST_SHADER_COLOR].vertex_code != 0 && gpu_d3d11_shader_codes[GPU_TEST_SHADER_TEXTURE].pixel_code != 0,
              __FILE__, __LINE__, "test shaders present");
}

// Reads a COPY_SRC texture through the public two-phase readback into a static buffer.
static const u8* gpu_d3d11_test_read(bmf_test* t, bmf_gpu_texture texture) {
    static u8 pixels[64 * 64 * 4];
    bmf_check_eq_i64(t, bmf_gpu_texture_read_begin(texture), BMF_OK, __FILE__, __LINE__, "read_begin");
    bmf_check_eq_i64(t, bmf_gpu_texture_read_end(pixels, (i64)sizeof pixels), BMF_OK, __FILE__, __LINE__, "read_end finishes at once on D3D11");
    return pixels;
}

// Compares `actual` with an expected RGBA image; on mismatch the note names the first bad pixel.
static void gpu_d3d11_test_compare(bmf_test* t, const u8* actual, const u8* expected, i32 width, i32 height, i32 tolerance,
                                   const char* what, i32 line) {
    i32 bad = 0;
    i32 first = -1;
    for (i32 i = 0; i < width * height * 4; i++) {
        i32 difference = (i32)actual[i] - (i32)expected[i];
        if (difference > tolerance || difference < -tolerance) {
            if (first < 0) first = i;
            bad++;
        }
    }
    if (bad) {
        char note[300];
        i32 pixel = first / 4;
        bmf_fmt_buf(note, (i32)sizeof note, "%s: %i bad channels; first at (%i,%i): got %i,%i,%i,%i expected %i,%i,%i,%i", what, bad, pixel % width,
                    pixel / width, actual[pixel * 4], actual[pixel * 4 + 1], actual[pixel * 4 + 2], actual[pixel * 4 + 3], expected[pixel * 4],
                    expected[pixel * 4 + 1], expected[pixel * 4 + 2], expected[pixel * 4 + 3]);
        bmf_test_note(t, note);
    }
    bmf_check_eq_i64(t, bad, 0, __FILE__, line, what);
}

static void gpu_d3d11_test_fill(u8* image, i32 width, i32 height, u8 r, u8 g, u8 b, u8 a) {
    for (i32 i = 0; i < width * height; i++) {
        image[i * 4 + 0] = r;
        image[i * 4 + 1] = g;
        image[i * 4 + 2] = b;
        image[i * 4 + 3] = a;
    }
}

// Plan acceptance (1A-14): a clear reads back exactly, for every readable color format.
static void test_gpu_d3d11_clear_pixels(bmf_test* t) {
    gpu_d3d11_test_init(t, 0, 0, 0, 1);
    static u8 expected[64 * 32 * 4];
    bmf_gpu_format formats[2] = { BMF_GPU_FORMAT_RGBA8_UNORM, BMF_GPU_FORMAT_BGRA8_UNORM };
    for (i32 f = 0; f < 2; f++) {
        bmf_gpu_texture_desc target_desc = { formats[f], BMF_GPU_TEXTURE_RENDER_TARGET | BMF_GPU_TEXTURE_COPY_SRC, 64, 32, 0, "clear target" };
        bmf_gpu_texture target = bmf_gpu_texture_create(&target_desc);
        bmf_gpu_begin_frame();
        bmf_gpu_pass_desc pass;
        bmf_memset(&pass, 0, sizeof pass);
        pass.color = target;
        pass.color_load = BMF_GPU_LOAD_CLEAR;
        pass.clear_color = (bmf_v4){ 51.0f / 255.0f, 102.0f / 255.0f, 153.0f / 255.0f, 204.0f / 255.0f };
        bmf_gpu_begin_pass(&pass);
        bmf_gpu_end_pass();
        bmf_gpu_end_frame();
        gpu_d3d11_test_fill(expected, 64, 32, 51, 102, 153, 204);
        gpu_d3d11_test_compare(t, gpu_d3d11_test_read(t, target), expected, 64, 32, 0, f == 0 ? "rgba8 clear" : "bgra8 clear (read as rgba)", __LINE__);
        bmf_gpu_texture_destroy(target);
    }

    // sRGB target: the clear color is linear and gets encoded like rendered output.
    bmf_gpu_texture_desc srgb_desc = { BMF_GPU_FORMAT_RGBA8_UNORM_SRGB, BMF_GPU_TEXTURE_RENDER_TARGET | BMF_GPU_TEXTURE_COPY_SRC, 8, 8, 0, "srgb" };
    bmf_gpu_texture srgb = bmf_gpu_texture_create(&srgb_desc);
    bmf_gpu_begin_frame();
    bmf_gpu_pass_desc pass;
    bmf_memset(&pass, 0, sizeof pass);
    pass.color = srgb;
    pass.color_load = BMF_GPU_LOAD_CLEAR;
    pass.clear_color = (bmf_v4){ 0.5f, 0.0f, 1.0f, 0.5f };
    bmf_gpu_begin_pass(&pass);
    bmf_gpu_end_pass();
    bmf_gpu_end_frame();
    gpu_d3d11_test_fill(expected, 8, 8, 188, 0, 255, 128);   // linear 0.5 encodes to 188; alpha stays linear
    gpu_d3d11_test_compare(t, gpu_d3d11_test_read(t, srgb), expected, 8, 8, 0, "srgb clear encodes", __LINE__);
    bmf_gpu_texture_destroy(srgb);
    bmf_gpu_shutdown();
}

// Plan acceptance (1A-14): one triangle matches the expected pixels exactly. The triangle's
// corners lie on pixel corners and no pixel center lies on an edge, so the expected coverage is
// exact: pixel (x, y) is inside when x >= 8, y >= 8 and 2x + 3y < 133.5. It also covers vertex and
// instance streams (first_instance 1), a uniform binding at offset 256, culling, both color formats.
static void test_gpu_d3d11_triangle_pixels(bmf_test* t) {
    gpu_d3d11_test_init(t, 0, 0, 0, 1);
    gpu_d3d11_test_shaders(t);
    i32 live_baseline = gpu_d3d11_live_object_count();

    static const f32 corners[6] = { 8.0f, 8.0f, 56.0f, 8.0f, 8.0f, 40.0f };   // clockwise on screen = back face
    static const u8 colors[8] = { 1, 2, 3, 4, 255, 128, 0, 255 };            // instance 1 is the one drawn
    bmf_gpu_buffer_desc vertex_desc = { BMF_GPU_BUFFER_VERTEX, (i32)sizeof corners, 0, corners, "corners" };
    bmf_gpu_buffer_desc instance_desc = { BMF_GPU_BUFFER_VERTEX, (i32)sizeof colors, 0, colors, "colors" };
    bmf_gpu_buffer_desc uniform_desc = { BMF_GPU_BUFFER_UNIFORM, 272, 1, 0, "params" };   // the binding is its last 16 bytes
    bmf_gpu_buffer vertices = bmf_gpu_buffer_create(&vertex_desc);
    bmf_gpu_buffer instances = bmf_gpu_buffer_create(&instance_desc);
    bmf_gpu_buffer uniforms = bmf_gpu_buffer_create(&uniform_desc);
    static const f32 garbage[64] = { 1e9f, 1e9f, 1e9f, 1e9f };
    static const f32 params[4] = { 64.0f, 64.0f, 0.0f, 0.0f };
    bmf_gpu_buffer_write(uniforms, 0, garbage, (i32)sizeof garbage);   // a wrong offset would read this
    bmf_gpu_buffer_write(uniforms, 256, params, (i32)sizeof params);

    bmf_gpu_bindset_desc bindset_desc;
    bmf_memset(&bindset_desc, 0, sizeof bindset_desc);
    bindset_desc.shader = GPU_TEST_SHADER_COLOR;
    bindset_desc.uniforms[0] = (bmf_gpu_uniform_binding){ uniforms, 256, 16 };
    bindset_desc.label = "params at 256";
    bmf_gpu_bindset bindset = bmf_gpu_bindset_create(&bindset_desc);

    static u8 expected[64 * 64 * 4];
    gpu_d3d11_test_fill(expected, 64, 64, 0, 0, 0, 255);
    for (i32 y = 0; y < 64; y++)
        for (i32 x = 0; x < 64; x++)
            if (x >= 8 && y >= 8 && 2 * x + 3 * y < 134) bmf_memcpy(expected + (y * 64 + x) * 4, colors + 4, 4);
    static u8 empty[64 * 64 * 4];
    gpu_d3d11_test_fill(empty, 64, 64, 0, 0, 0, 255);

    bmf_gpu_format formats[2] = { BMF_GPU_FORMAT_RGBA8_UNORM, BMF_GPU_FORMAT_BGRA8_UNORM };
    for (i32 f = 0; f < 2; f++) {
        bmf_gpu_pipeline_desc pipeline_desc;
        bmf_memset(&pipeline_desc, 0, sizeof pipeline_desc);
        pipeline_desc.shader = GPU_TEST_SHADER_COLOR;
        pipeline_desc.vertex_buffer_count = 2;
        pipeline_desc.vertex_buffers[0] = (bmf_gpu_vertex_layout){ 8, BMF_GPU_STEP_VERTEX, 1, { { BMF_GPU_VERTEX_F32X2, 0, 0 } } };
        pipeline_desc.vertex_buffers[1] = (bmf_gpu_vertex_layout){ 4, BMF_GPU_STEP_INSTANCE, 1, { { BMF_GPU_VERTEX_U8X4_UNORM, 0, 1 } } };
        pipeline_desc.topology = BMF_GPU_TOPOLOGY_TRIANGLE_LIST;
        pipeline_desc.blend = BMF_GPU_BLEND_OPAQUE;
        pipeline_desc.cull = BMF_GPU_CULL_FRONT;
        pipeline_desc.color_format = formats[f];
        pipeline_desc.label = "triangle";
        bmf_gpu_pipeline draws_back_faces = bmf_gpu_pipeline_create(&pipeline_desc);
        pipeline_desc.cull = BMF_GPU_CULL_BACK;
        bmf_gpu_pipeline culls_back_faces = bmf_gpu_pipeline_create(&pipeline_desc);

        bmf_gpu_texture_desc target_desc = { formats[f], BMF_GPU_TEXTURE_RENDER_TARGET | BMF_GPU_TEXTURE_COPY_SRC, 64, 64, 0, "triangle target" };
        bmf_gpu_texture drawn = bmf_gpu_texture_create(&target_desc);
        bmf_gpu_texture culled = bmf_gpu_texture_create(&target_desc);
        bmf_gpu_begin_frame();
        for (i32 pass_index = 0; pass_index < 2; pass_index++) {
            bmf_gpu_pass_desc pass;
            bmf_memset(&pass, 0, sizeof pass);
            pass.color = pass_index == 0 ? drawn : culled;
            pass.color_load = BMF_GPU_LOAD_CLEAR;
            pass.clear_color = (bmf_v4){ 0.0f, 0.0f, 0.0f, 1.0f };
            bmf_gpu_begin_pass(&pass);
            bmf_gpu_draw_desc draw;
            bmf_memset(&draw, 0, sizeof draw);
            draw.pipeline = pass_index == 0 ? draws_back_faces : culls_back_faces;
            draw.bindset = bindset;
            draw.vertex_buffers[0] = vertices;
            draw.vertex_buffers[1] = instances;
            draw.vertex_count = 3;
            draw.instance_count = 1;
            draw.first_instance = 1;
            bmf_gpu_draw(&draw);
            bmf_gpu_end_pass();
        }
        bmf_gpu_end_frame();
        gpu_d3d11_test_compare(t, gpu_d3d11_test_read(t, drawn), expected, 64, 64, 0, f == 0 ? "rgba8 triangle" : "bgra8 triangle", __LINE__);
        gpu_d3d11_test_compare(t, gpu_d3d11_test_read(t, culled), empty, 64, 64, 0, "back face culled", __LINE__);
        bmf_gpu_texture_destroy(drawn);
        bmf_gpu_texture_destroy(culled);
        bmf_gpu_pipeline_destroy(draws_back_faces);
        bmf_gpu_pipeline_destroy(culls_back_faces);
    }
    bmf_check_eq_i64(t, bmf_gpu_get_stats().draws, 2, __FILE__, __LINE__, "two draws in the last frame");

    // Submit cost (CPU side of bmf_gpu_draw, frontend + backend), 10 000 draws per case: identical
    // state (cache hits), and pipelines alternating every draw (state set each time).
    {
        bmf_gpu_pipeline_desc pipeline_desc;
        bmf_memset(&pipeline_desc, 0, sizeof pipeline_desc);
        pipeline_desc.shader = GPU_TEST_SHADER_COLOR;
        pipeline_desc.vertex_buffer_count = 2;
        pipeline_desc.vertex_buffers[0] = (bmf_gpu_vertex_layout){ 8, BMF_GPU_STEP_VERTEX, 1, { { BMF_GPU_VERTEX_F32X2, 0, 0 } } };
        pipeline_desc.vertex_buffers[1] = (bmf_gpu_vertex_layout){ 4, BMF_GPU_STEP_INSTANCE, 1, { { BMF_GPU_VERTEX_U8X4_UNORM, 0, 1 } } };
        pipeline_desc.color_format = BMF_GPU_FORMAT_RGBA8_UNORM;
        bmf_gpu_pipeline pipelines[2];
        pipelines[0] = bmf_gpu_pipeline_create(&pipeline_desc);
        pipeline_desc.blend = BMF_GPU_BLEND_ADDITIVE;
        pipelines[1] = bmf_gpu_pipeline_create(&pipeline_desc);
        bmf_gpu_texture_desc target_desc = { BMF_GPU_FORMAT_RGBA8_UNORM, BMF_GPU_TEXTURE_RENDER_TARGET, 64, 64, 0, "bench target" };
        bmf_gpu_texture target = bmf_gpu_texture_create(&target_desc);
        i64 nanoseconds[2];
        for (i32 bench = 0; bench < 2; bench++) {
            bmf_gpu_begin_frame();
            bmf_gpu_pass_desc pass;
            bmf_memset(&pass, 0, sizeof pass);
            pass.color = target;
            bmf_gpu_begin_pass(&pass);
            bmf_gpu_draw_desc draw;
            bmf_memset(&draw, 0, sizeof draw);
            draw.bindset = bindset;
            draw.vertex_buffers[0] = vertices;
            draw.vertex_buffers[1] = instances;
            draw.vertex_count = 3;
            draw.instance_count = 1;
            i64 start = bmf_platform_time_ns();
            for (i32 i = 0; i < 10000; i++) {
                draw.pipeline = pipelines[bench == 0 ? 0 : (i & 1)];
                bmf_gpu_draw(&draw);
            }
            nanoseconds[bench] = bmf_platform_time_ns() - start;
            bmf_gpu_end_pass();
            bmf_gpu_end_frame();
        }
        char note[200];
        bmf_fmt_buf(note, (i32)sizeof note, "draw submit: same state %li ns/draw, alternating pipelines %li ns/draw (10000 draws each)",
                    nanoseconds[0] / 10000, nanoseconds[1] / 10000);
        bmf_test_note(t, note);
        bmf_gpu_texture_destroy(target);
        bmf_gpu_pipeline_destroy(pipelines[0]);
        bmf_gpu_pipeline_destroy(pipelines[1]);
    }

    bmf_gpu_bindset_destroy(bindset);
    bmf_gpu_buffer_destroy(vertices);
    bmf_gpu_buffer_destroy(instances);
    bmf_gpu_buffer_destroy(uniforms);
    bmf_check_eq_i64(t, gpu_d3d11_live_object_count(), live_baseline, __FILE__, __LINE__, "pipelines, bind set and buffers released");
    bmf_gpu_shutdown();
}

// Texture upload, views and samplers through real sampling: point-sampling a texture into a
// target of the same size copies it exactly (pixel centers hit texel centers).
static void test_gpu_d3d11_texture_sampling(bmf_test* t) {
    gpu_d3d11_test_init(t, 0, 0, 0, 1);
    gpu_d3d11_test_shaders(t);
    static u8 rgba[16 * 16 * 4];
    for (i32 i = 0; i < (i32)sizeof rgba; i++) rgba[i] = (u8)(i * 37 + 11);
    static u8 r8[13 * 7];
    for (i32 i = 0; i < (i32)sizeof r8; i++) r8[i] = (u8)(i * 29 + 5);
    static u8 r8_expected[13 * 7 * 4];
    for (i32 i = 0; i < 13 * 7; i++) {
        r8_expected[i * 4 + 0] = r8[i];
        r8_expected[i * 4 + 1] = 0;
        r8_expected[i * 4 + 2] = 0;
        r8_expected[i * 4 + 3] = 255;
    }
    bmf_gpu_sampler_desc sampler_desc = { BMF_GPU_FILTER_NEAREST, BMF_GPU_ADDRESS_CLAMP, "point" };
    bmf_gpu_sampler sampler = bmf_gpu_sampler_create(&sampler_desc);
    bmf_gpu_pipeline_desc pipeline_desc;
    bmf_memset(&pipeline_desc, 0, sizeof pipeline_desc);
    pipeline_desc.shader = GPU_TEST_SHADER_TEXTURE;
    pipeline_desc.topology = BMF_GPU_TOPOLOGY_TRIANGLE_STRIP;
    pipeline_desc.color_format = BMF_GPU_FORMAT_RGBA8_UNORM;
    pipeline_desc.label = "copy";
    bmf_gpu_pipeline pipeline = bmf_gpu_pipeline_create(&pipeline_desc);

    struct { u8* data; i32 width, height; bmf_gpu_format format; const u8* expected; const char* what; } cases[2] = {
        { rgba, 16, 16, BMF_GPU_FORMAT_RGBA8_UNORM, rgba, "rgba8 texture sampled" },
        { r8, 13, 7, BMF_GPU_FORMAT_R8_UNORM, r8_expected, "r8 texture sampled (odd width)" },
    };
    for (i32 c = 0; c < 2; c++) {
        bmf_gpu_texture_desc source_desc = { cases[c].format, BMF_GPU_TEXTURE_SAMPLED, cases[c].width, cases[c].height, cases[c].data, "source" };
        bmf_gpu_texture source = bmf_gpu_texture_create(&source_desc);
        bmf_gpu_texture_desc target_desc = { BMF_GPU_FORMAT_RGBA8_UNORM, BMF_GPU_TEXTURE_RENDER_TARGET | BMF_GPU_TEXTURE_COPY_SRC, cases[c].width,
                                             cases[c].height, 0, "copy target" };
        bmf_gpu_texture target = bmf_gpu_texture_create(&target_desc);
        bmf_gpu_bindset_desc bindset_desc;
        bmf_memset(&bindset_desc, 0, sizeof bindset_desc);
        bindset_desc.shader = GPU_TEST_SHADER_TEXTURE;
        bindset_desc.textures[0] = source;
        bindset_desc.samplers[0] = sampler;
        bmf_gpu_bindset bindset = bmf_gpu_bindset_create(&bindset_desc);
        bmf_gpu_begin_frame();
        bmf_gpu_pass_desc pass;
        bmf_memset(&pass, 0, sizeof pass);
        pass.color = target;
        pass.color_load = BMF_GPU_LOAD_CLEAR;
        bmf_gpu_begin_pass(&pass);
        bmf_gpu_draw_desc draw;
        bmf_memset(&draw, 0, sizeof draw);
        draw.pipeline = pipeline;
        draw.bindset = bindset;
        draw.vertex_count = 4;
        draw.instance_count = 1;
        bmf_gpu_draw(&draw);
        bmf_gpu_end_pass();
        bmf_gpu_end_frame();
        gpu_d3d11_test_compare(t, gpu_d3d11_test_read(t, target), cases[c].expected, cases[c].width, cases[c].height, 0, cases[c].what, __LINE__);
        // The bind set keeps the texture's view alive: destroying the texture first is safe.
        bmf_gpu_texture_destroy(source);
        bmf_gpu_bindset_destroy(bindset);
        bmf_gpu_texture_destroy(target);
    }
    bmf_gpu_pipeline_destroy(pipeline);
    bmf_gpu_sampler_destroy(sampler);
    bmf_gpu_shutdown();
}

// Blend presets and depth test through pixels. Left half premultiplied, right half additive, over
// a blue clear; then depth: a near quad hides a farther one drawn later, a nearer one drawn last shows.
static void test_gpu_d3d11_blend_depth(bmf_test* t) {
    gpu_d3d11_test_init(t, 0, 0, 0, 1);
    gpu_d3d11_test_shaders(t);
    i32 live_baseline = gpu_d3d11_live_object_count();

    // Rectangles as 4-vertex strips: left half, right half, whole target (pixel corners, 64x64).
    static const f32 rects[24] = {
        0.0f, 0.0f, 32.0f, 0.0f, 0.0f, 64.0f, 32.0f, 64.0f,
        32.0f, 0.0f, 64.0f, 0.0f, 32.0f, 64.0f, 64.0f, 64.0f,
        0.0f, 0.0f, 64.0f, 0.0f, 0.0f, 64.0f, 64.0f, 64.0f,
    };
    static const u8 colors[20] = {
        128, 0, 0, 128,     // 0: half-transparent red, premultiplied
        0, 64, 0, 0,        // 1: additive green
        255, 0, 0, 255,     // 2: red
        0, 255, 0, 255,     // 3: green
        0, 0, 255, 255,     // 4: blue
    };
    bmf_gpu_buffer_desc vertex_desc = { BMF_GPU_BUFFER_VERTEX, (i32)sizeof rects, 0, rects, "rects" };
    bmf_gpu_buffer_desc instance_desc = { BMF_GPU_BUFFER_VERTEX, (i32)sizeof colors, 0, colors, "colors" };
    bmf_gpu_buffer vertices = bmf_gpu_buffer_create(&vertex_desc);
    bmf_gpu_buffer instances = bmf_gpu_buffer_create(&instance_desc);
    bmf_gpu_buffer_desc uniform_desc = { BMF_GPU_BUFFER_UNIFORM, 1024, 1, 0, "params" };
    bmf_gpu_buffer uniforms = bmf_gpu_buffer_create(&uniform_desc);
    f32 depths[4] = { 0.0f, 0.5f, 0.75f, 0.25f };
    bmf_gpu_bindset bindsets[4];
    for (i32 i = 0; i < 4; i++) {
        f32 params[4] = { 64.0f, 64.0f, depths[i], 0.0f };
        bmf_gpu_buffer_write(uniforms, i * 256, params, (i32)sizeof params);
        bmf_gpu_bindset_desc bindset_desc;
        bmf_memset(&bindset_desc, 0, sizeof bindset_desc);
        bindset_desc.shader = GPU_TEST_SHADER_COLOR;
        bindset_desc.uniforms[0] = (bmf_gpu_uniform_binding){ uniforms, i * 256, 16 };
        bindsets[i] = bmf_gpu_bindset_create(&bindset_desc);
    }

    bmf_gpu_pipeline_desc pipeline_desc;
    bmf_memset(&pipeline_desc, 0, sizeof pipeline_desc);
    pipeline_desc.shader = GPU_TEST_SHADER_COLOR;
    pipeline_desc.vertex_buffer_count = 2;
    pipeline_desc.vertex_buffers[0] = (bmf_gpu_vertex_layout){ 8, BMF_GPU_STEP_VERTEX, 1, { { BMF_GPU_VERTEX_F32X2, 0, 0 } } };
    pipeline_desc.vertex_buffers[1] = (bmf_gpu_vertex_layout){ 4, BMF_GPU_STEP_INSTANCE, 1, { { BMF_GPU_VERTEX_U8X4_UNORM, 0, 1 } } };
    pipeline_desc.topology = BMF_GPU_TOPOLOGY_TRIANGLE_STRIP;
    pipeline_desc.color_format = BMF_GPU_FORMAT_RGBA8_UNORM;
    pipeline_desc.blend = BMF_GPU_BLEND_PREMULTIPLIED;
    bmf_gpu_pipeline premultiplied = bmf_gpu_pipeline_create(&pipeline_desc);
    pipeline_desc.blend = BMF_GPU_BLEND_ADDITIVE;
    bmf_gpu_pipeline additive = bmf_gpu_pipeline_create(&pipeline_desc);
    pipeline_desc.blend = BMF_GPU_BLEND_OPAQUE;
    pipeline_desc.depth_format = BMF_GPU_FORMAT_DEPTH24_STENCIL8;
    pipeline_desc.depth_test = 1;
    pipeline_desc.depth_write = 1;
    bmf_gpu_pipeline depth_tested = bmf_gpu_pipeline_create(&pipeline_desc);

    bmf_gpu_texture_desc target_desc = { BMF_GPU_FORMAT_RGBA8_UNORM, BMF_GPU_TEXTURE_RENDER_TARGET | BMF_GPU_TEXTURE_COPY_SRC, 64, 64, 0, "target" };
    bmf_gpu_texture blend_target = bmf_gpu_texture_create(&target_desc);
    bmf_gpu_texture depth_target = bmf_gpu_texture_create(&target_desc);
    bmf_gpu_texture_desc depth_desc = { BMF_GPU_FORMAT_DEPTH24_STENCIL8, BMF_GPU_TEXTURE_RENDER_TARGET, 64, 64, 0, "depth" };
    bmf_gpu_texture depth = bmf_gpu_texture_create(&depth_desc);

    bmf_gpu_begin_frame();
    bmf_gpu_pass_desc pass;
    bmf_memset(&pass, 0, sizeof pass);
    pass.color = blend_target;
    pass.color_load = BMF_GPU_LOAD_CLEAR;
    pass.clear_color = (bmf_v4){ 0.0f, 0.0f, 1.0f, 1.0f };
    bmf_gpu_begin_pass(&pass);
    bmf_gpu_draw_desc draw;
    bmf_memset(&draw, 0, sizeof draw);
    draw.vertex_buffers[0] = vertices;
    draw.vertex_buffers[1] = instances;
    draw.vertex_count = 4;
    draw.instance_count = 1;
    draw.bindset = bindsets[0];
    draw.pipeline = premultiplied;
    draw.first_vertex = 0;
    draw.first_instance = 0;
    bmf_gpu_draw(&draw);
    draw.pipeline = additive;
    draw.first_vertex = 4;
    draw.first_instance = 1;
    bmf_gpu_draw(&draw);
    bmf_gpu_end_pass();

    pass.color = depth_target;
    pass.depth = depth;
    pass.clear_color = (bmf_v4){ 0.0f, 0.0f, 0.0f, 1.0f };
    pass.depth_load = BMF_GPU_LOAD_CLEAR;
    pass.clear_depth = 1.0f;
    bmf_gpu_begin_pass(&pass);
    draw.pipeline = depth_tested;
    draw.first_vertex = 8;      // whole target, red at depth 0.5
    draw.first_instance = 2;
    draw.bindset = bindsets[1];
    bmf_gpu_draw(&draw);
    draw.first_instance = 3;    // whole target, green at 0.75: behind, hidden
    draw.bindset = bindsets[2];
    bmf_gpu_draw(&draw);
    draw.first_vertex = 0;      // left half, blue at 0.25: in front
    draw.first_instance = 4;
    draw.bindset = bindsets[3];
    bmf_gpu_draw(&draw);
    bmf_gpu_end_pass();
    bmf_gpu_end_frame();

    // Premultiplied: rgb = src + dst * (1 - 128/255); additive: dst + src.
    static u8 expected[64 * 64 * 4];
    for (i32 y = 0; y < 64; y++) {
        for (i32 x = 0; x < 64; x++) {
            u8* p = expected + (y * 64 + x) * 4;
            if (x < 32) { p[0] = 128; p[1] = 0; p[2] = 127; p[3] = 255; }
            else        { p[0] = 0; p[1] = 64; p[2] = 255; p[3] = 255; }
        }
    }
    gpu_d3d11_test_compare(t, gpu_d3d11_test_read(t, blend_target), expected, 64, 64, 0, "premultiplied and additive blend", __LINE__);
    for (i32 y = 0; y < 64; y++) {
        for (i32 x = 0; x < 64; x++) {
            u8* p = expected + (y * 64 + x) * 4;
            if (x < 32) { p[0] = 0; p[1] = 0; p[2] = 255; p[3] = 255; }
            else        { p[0] = 255; p[1] = 0; p[2] = 0; p[3] = 255; }
        }
    }
    gpu_d3d11_test_compare(t, gpu_d3d11_test_read(t, depth_target), expected, 64, 64, 0, "depth test", __LINE__);
    bmf_check_eq_i64(t, bmf_gpu_get_stats().draws, 5, __FILE__, __LINE__, "five draws");
    bmf_check_eq_i64(t, bmf_gpu_get_stats().pipeline_changes, 3, __FILE__, __LINE__, "pipeline changes counted per pass");

    for (i32 i = 0; i < 4; i++) bmf_gpu_bindset_destroy(bindsets[i]);
    bmf_gpu_pipeline_destroy(premultiplied);
    bmf_gpu_pipeline_destroy(additive);
    bmf_gpu_pipeline_destroy(depth_tested);
    bmf_gpu_texture_destroy(blend_target);
    bmf_gpu_texture_destroy(depth_target);
    bmf_gpu_texture_destroy(depth);
    bmf_gpu_buffer_destroy(vertices);
    bmf_gpu_buffer_destroy(instances);
    bmf_gpu_buffer_destroy(uniforms);
    bmf_check_eq_i64(t, gpu_d3d11_live_object_count(), live_baseline, __FILE__, __LINE__, "everything released");
    bmf_gpu_shutdown();
}

// ---------------------------------------------------------------- 1A-15: generated shader tables
// The generated tables (out/gen/shaders_d3d11.h, out/gen/test_shaders_d3d11.h) hold DXBC bytecode
// for every id and the b/t/s registers each shader declares, read from the bytecode by build.c.
static void test_gpu_d3d11_shader_tables(bmf_test* t) {
    for (i32 id = 0; id < BMF_SHADER_COUNT; id++) {
        const gpu_d3d11_shader_code* code = &gpu_d3d11_shader_codes[id];
        bmf_check(t, code->name != 0 && code->vertex_code_size > 32 && code->pixel_code_size > 32, __FILE__, __LINE__, "runtime shader has bytecode");
        if (code->vertex_code) bmf_check_eq_mem(t, code->vertex_code, "DXBC", 4, __FILE__, __LINE__, "runtime VS is DXBC");
        if (code->pixel_code) bmf_check_eq_mem(t, code->pixel_code, "DXBC", 4, __FILE__, __LINE__, "runtime PS is DXBC");
    }
    for (i32 id = BMF_SHADER_COUNT; id < GPU_TEST_SHADER_FIRST; id++)
        bmf_check(t, gpu_d3d11_shader_codes[id].vertex_code == 0, __FILE__, __LINE__, "no runtime shader past BMF_SHADER_COUNT");
    for (i32 id = GPU_TEST_SHADER_FIRST; id < GPU_TEST_SHADER_END; id++) {
        const gpu_d3d11_shader_code* code = &gpu_d3d11_test_shader_codes[id];
        bmf_check_eq_mem(t, code->vertex_code, "DXBC", 4, __FILE__, __LINE__, "test VS is DXBC");
        bmf_check_eq_mem(t, code->pixel_code, "DXBC", 4, __FILE__, __LINE__, "test PS is DXBC");
    }
    // color.hlsl: cbuffer b0 only. texture.hlsl: t0 and s0 only.
    const gpu_d3d11_shader_code* color = &gpu_d3d11_test_shader_codes[GPU_TEST_SHADER_COLOR];
    const gpu_d3d11_shader_code* texture = &gpu_d3d11_test_shader_codes[GPU_TEST_SHADER_TEXTURE];
    bmf_check(t, color->name && color->name[0] == 'c', __FILE__, __LINE__, "color shader name");
    bmf_check_eq_u64(t, color->uniform_mask, 0x1, __FILE__, __LINE__, "color: b0");
    bmf_check_eq_u64(t, color->texture_mask, 0x0, __FILE__, __LINE__, "color: no textures");
    bmf_check_eq_u64(t, color->sampler_mask, 0x0, __FILE__, __LINE__, "color: no samplers");
    bmf_check_eq_u64(t, texture->uniform_mask, 0x0, __FILE__, __LINE__, "texture: no cbuffers");
    bmf_check_eq_u64(t, texture->texture_mask, 0x1, __FILE__, __LINE__, "texture: t0");
    bmf_check_eq_u64(t, texture->sampler_mask, 0x1, __FILE__, __LINE__, "texture: s0");
}

// Expected crash: a bind set for texture.hlsl (t0 + s0) without its sampler stops at the
// bmf_gpu_bindset_create slot check.
static void test_gpu_d3d11_validation_bindset_slots(bmf_test* t) {
    gpu_d3d11_test_init(t, 0, 0, 0, 1);
    gpu_d3d11_test_shaders(t);
    static const u8 pixel[4] = { 255, 255, 255, 255 };
    bmf_gpu_texture_desc texture_desc;
    bmf_memset(&texture_desc, 0, sizeof texture_desc);
    texture_desc.width = 1;
    texture_desc.height = 1;
    texture_desc.format = BMF_GPU_FORMAT_RGBA8_UNORM;
    texture_desc.usage = BMF_GPU_TEXTURE_SAMPLED;
    texture_desc.data = pixel;
    bmf_gpu_texture texture = bmf_gpu_texture_create(&texture_desc);
    bmf_gpu_bindset_desc bindset_desc;
    bmf_memset(&bindset_desc, 0, sizeof bindset_desc);
    bindset_desc.shader = GPU_TEST_SHADER_TEXTURE;
    bindset_desc.textures[0] = texture;
    bmf_gpu_bindset_create(&bindset_desc);
    bmf_gpu_shutdown();
}
