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
    bmf_check_eq_u64(t, bmf_gpu_surface().id, 0, __FILE__, __LINE__, "no window, no surface");
    bmf_check(t, bmf_gpu_begin_frame() == 0, __FILE__, __LINE__, "nothing to present headless");
    bmf_gpu_end_frame();
    bmf_gpu_shutdown();

    // Forced WARP (headless goldens, SPEC §9.1).
    gpu_d3d11_test_init(t, 0, 0, 0, 1);
    bmf_check(t, gpu_d3d11.warp, __FILE__, __LINE__, "software forces WARP");
    char note[200];
    bmf_fmt_buf(note, (i32)sizeof note, "warp adapter '%s', feature level 0x%x, debug layer %s", gpu_d3d11.adapter_name,
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
