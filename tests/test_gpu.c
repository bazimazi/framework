// Tests for src/gpu/gpu.{h,c} on the null backend (task 1A-13, D-38): lifecycle, a full API
// usage pass with exact stats, handle staleness, and validation asserts (expected crashes).

// A fake native window: the null backend never touches it, but it makes the surface exist.
static void* const GPU_TEST_FAKE_WINDOW = (void*)(u64)0x1000;

static void gpu_test_init_null(i32 width, i32 height) {
    bmf_gpu_desc desc;
    bmf_memset(&desc, 0, sizeof desc);
    desc.backend = BMF_GPU_BACKEND_NULL;
    desc.window = width > 0 ? GPU_TEST_FAKE_WINDOW : 0;
    desc.surface_width = width;
    desc.surface_height = height;
    if (bmf_gpu_init(&desc) != BMF_OK) bmf_panic("gpu test: null backend init failed");
}

// One per-vertex stream (f32x2 position) and one per-instance stream (f32x4 rect + u8x4 color).
static bmf_gpu_pipeline_desc gpu_test_pipeline_desc(bmf_gpu_format color_format, bmf_gpu_format depth_format) {
    bmf_gpu_pipeline_desc d;
    bmf_memset(&d, 0, sizeof d);
    d.shader = 3;
    d.vertex_buffer_count = 2;
    d.vertex_buffers[0].stride = 8;
    d.vertex_buffers[0].step = BMF_GPU_STEP_VERTEX;
    d.vertex_buffers[0].attribute_count = 1;
    d.vertex_buffers[0].attributes[0] = (bmf_gpu_vertex_attribute){ BMF_GPU_VERTEX_F32X2, 0, 0 };
    d.vertex_buffers[1].stride = 20;
    d.vertex_buffers[1].step = BMF_GPU_STEP_INSTANCE;
    d.vertex_buffers[1].attribute_count = 2;
    d.vertex_buffers[1].attributes[0] = (bmf_gpu_vertex_attribute){ BMF_GPU_VERTEX_F32X4, 0, 1 };
    d.vertex_buffers[1].attributes[1] = (bmf_gpu_vertex_attribute){ BMF_GPU_VERTEX_U8X4_UNORM, 16, 2 };
    d.topology = BMF_GPU_TOPOLOGY_TRIANGLE_STRIP;
    d.blend = BMF_GPU_BLEND_PREMULTIPLIED;
    d.color_format = color_format;
    d.depth_format = depth_format;
    d.depth_test = depth_format != BMF_GPU_FORMAT_NONE;
    d.depth_write = depth_format != BMF_GPU_FORMAT_NONE;
    d.label = "test pipeline";
    return d;
}

static void test_gpu_null_lifecycle(bmf_test* t) {
    gpu_test_init_null(0, 0);
    bmf_check(t, bmf_gpu_backend_active() == BMF_GPU_BACKEND_NULL, __FILE__, __LINE__, "backend is null");
    bmf_check_eq_u64(t, bmf_gpu_surface().id, 0, __FILE__, __LINE__, "no window, no surface");
    bmf_check(t, bmf_gpu_surface_format() == BMF_GPU_FORMAT_BGRA8_UNORM, __FILE__, __LINE__, "surface format without a window");
    bmf_check(t, bmf_gpu_begin_frame() == 0, __FILE__, __LINE__, "no surface to present");
    bmf_gpu_end_frame();
    bmf_check_eq_u64(t, bmf_gpu_get_stats().frames, 1, __FILE__, __LINE__, "one frame counted");
    bmf_gpu_shutdown();

    // Restart with a window: the surface is a live RENDER_TARGET texture that tracks resizes.
    gpu_test_init_null(640, 360);
    bmf_gpu_texture surface = bmf_gpu_surface();
    bmf_check(t, surface.id != 0, __FILE__, __LINE__, "surface exists with a window");
    i32 w = 0, h = 0;
    bmf_gpu_texture_size(surface, &w, &h);
    bmf_check(t, w == 640 && h == 360, __FILE__, __LINE__, "surface size from init");
    bmf_gpu_surface_resize(1280, 720);
    bmf_gpu_texture_size(surface, &w, &h);
    bmf_check(t, w == 1280 && h == 720, __FILE__, __LINE__, "surface size after resize");
    bmf_check_eq_i64(t, bmf_gpu_get_stats().textures, 1, __FILE__, __LINE__, "the surface counts as a live texture");
    bmf_check_eq_u64(t, bmf_gpu_get_stats().frames, 0, __FILE__, __LINE__, "frame count restarts after shutdown");
    bmf_check(t, bmf_gpu_begin_frame() == 1, __FILE__, __LINE__, "surface will be presented");
    bmf_gpu_end_frame();
    bmf_gpu_shutdown();
}

static void test_gpu_null_api_usage(bmf_test* t) {
    gpu_test_init_null(320, 200);
    bmf_gpu_format surface_format = bmf_gpu_surface_format();

    // Resources of every kind.
    static const f32 quad[8] = { 0, 0, 1, 0, 0, 1, 1, 1 };
    bmf_gpu_buffer_desc vb = { BMF_GPU_BUFFER_VERTEX, (i32)sizeof quad, 0, quad, "quad" };
    bmf_gpu_buffer quad_buffer = bmf_gpu_buffer_create(&vb);
    bmf_gpu_buffer_desc ib = { BMF_GPU_BUFFER_VERTEX, 20 * 1000, 1, 0, "instances" };
    bmf_gpu_buffer instance_buffer = bmf_gpu_buffer_create(&ib);
    bmf_gpu_buffer_desc ub = { BMF_GPU_BUFFER_UNIFORM, 512, 1, 0, "camera" };
    bmf_gpu_buffer uniform_buffer = bmf_gpu_buffer_create(&ub);

    static u8 pixels[16 * 16 * 4];
    bmf_gpu_texture_desc atlas_desc = { BMF_GPU_FORMAT_RGBA8_UNORM_SRGB, BMF_GPU_TEXTURE_SAMPLED, 16, 16, pixels, "atlas" };
    bmf_gpu_texture atlas = bmf_gpu_texture_create(&atlas_desc);
    bmf_gpu_texture_desc target_desc = { surface_format, BMF_GPU_TEXTURE_RENDER_TARGET | BMF_GPU_TEXTURE_COPY_SRC | BMF_GPU_TEXTURE_SAMPLED, 64, 32, 0, "offscreen" };
    bmf_gpu_texture target = bmf_gpu_texture_create(&target_desc);
    bmf_gpu_texture_desc depth_desc = { BMF_GPU_FORMAT_DEPTH24_STENCIL8, BMF_GPU_TEXTURE_RENDER_TARGET, 64, 32, 0, "depth" };
    bmf_gpu_texture depth = bmf_gpu_texture_create(&depth_desc);
    bmf_gpu_sampler_desc sampler_desc = { BMF_GPU_FILTER_LINEAR, BMF_GPU_ADDRESS_CLAMP, "linear clamp" };
    bmf_gpu_sampler sampler = bmf_gpu_sampler_create(&sampler_desc);

    bmf_gpu_pipeline_desc pd = gpu_test_pipeline_desc(surface_format, BMF_GPU_FORMAT_NONE);
    bmf_gpu_pipeline pipeline = bmf_gpu_pipeline_create(&pd);
    bmf_gpu_pipeline_desc pd_depth = gpu_test_pipeline_desc(surface_format, BMF_GPU_FORMAT_DEPTH24_STENCIL8);
    bmf_gpu_pipeline pipeline_depth = bmf_gpu_pipeline_create(&pd_depth);
    pd.blend = BMF_GPU_BLEND_ADDITIVE;
    bmf_gpu_pipeline pipeline_additive = bmf_gpu_pipeline_create(&pd);

    bmf_gpu_bindset_desc bd;
    bmf_memset(&bd, 0, sizeof bd);
    bd.shader = 3;
    bd.uniforms[0] = (bmf_gpu_uniform_binding){ uniform_buffer, 0, 64 };
    bd.textures[0] = atlas;
    bd.samplers[0] = sampler;
    bmf_gpu_bindset bindset_a = bmf_gpu_bindset_create(&bd);
    bd.uniforms[0].offset = 256;   // second camera slot
    bmf_gpu_bindset bindset_b = bmf_gpu_bindset_create(&bd);
    bd.textures[0] = target;       // sampling the offscreen target in a later pass
    bmf_gpu_bindset bindset_target = bmf_gpu_bindset_create(&bd);

    bmf_gpu_stats s = bmf_gpu_get_stats();
    bmf_check(t, s.buffers == 3 && s.textures == 4 && s.samplers == 1 && s.pipelines == 3 && s.bindsets == 3,
              __FILE__, __LINE__, "live counts after creation (textures include the surface)");

    // Frame 1: an offscreen pass with depth, then the surface pass sampling the offscreen target.
    for (i32 frame = 0; frame < 2; frame++) {
        b32 presented = bmf_gpu_begin_frame();
        bmf_check(t, presented, __FILE__, __LINE__, "surface presentable");
        static u8 camera[512];
        bmf_gpu_buffer_write(uniform_buffer, 0, camera, 64);
        bmf_gpu_buffer_write(uniform_buffer, 256, camera, 64);   // increasing ranges: allowed
        static u8 instances[20 * 1000];
        bmf_gpu_buffer_write(instance_buffer, 0, instances, 20 * 600);

        bmf_gpu_pass_desc offscreen = { target, depth, BMF_GPU_LOAD_CLEAR, { 0, 0, 0, 1 }, BMF_GPU_LOAD_CLEAR, 1.0f, "offscreen" };
        bmf_gpu_begin_pass(&offscreen);
        bmf_gpu_draw_desc draw = { pipeline_depth, bindset_a, { quad_buffer, instance_buffer }, { 0, 0 }, 4, 100, 0, 0 };
        bmf_gpu_draw(&draw);
        bmf_gpu_end_pass();

        // Writes may continue inside the frame, after earlier ranges (ring-style append).
        bmf_gpu_buffer_write(instance_buffer, 20 * 600, instances, 20 * 400);

        bmf_gpu_pass_desc to_surface = { bmf_gpu_surface(), { 0 }, BMF_GPU_LOAD_CLEAR, { 0.1f, 0.2f, 0.3f, 1 }, BMF_GPU_LOAD_CLEAR, 0, "surface" };
        bmf_gpu_begin_pass(&to_surface);
        bmf_gpu_draw_desc d1 = { pipeline, bindset_a, { quad_buffer, instance_buffer }, { 0, 0 }, 4, 300, 0, 0 };
        bmf_gpu_draw(&d1);
        bmf_gpu_draw_desc d2 = { pipeline, bindset_b, { quad_buffer, instance_buffer }, { 0, 0 }, 4, 200, 0, 300 };   // bindset change only
        bmf_gpu_draw(&d2);
        bmf_gpu_draw_desc d3 = { pipeline_additive, bindset_b, { quad_buffer, instance_buffer }, { 0, 0 }, 4, 99, 0, 500 };   // pipeline change only
        bmf_gpu_draw(&d3);
        bmf_gpu_draw_desc d4 = { pipeline_additive, bindset_target, { quad_buffer, instance_buffer }, { 0, 20 * 999 }, 4, 1, 0, 0 };   // last element exactly fits
        bmf_gpu_draw(&d4);
        bmf_gpu_draw_desc skipped = { pipeline, bindset_a, { quad_buffer, instance_buffer }, { 0, 0 }, 4, 0, 0, 0 };
        bmf_gpu_draw(&skipped);
        bmf_gpu_end_pass();
        bmf_gpu_end_frame();

        s = bmf_gpu_get_stats();
        bmf_check_eq_u64(t, s.frames, (u64)frame + 1, __FILE__, __LINE__, "frames");
        bmf_check_eq_i64(t, s.passes, 2, __FILE__, __LINE__, "passes");
        bmf_check_eq_i64(t, s.draws, 5, __FILE__, __LINE__, "draws (the zero-instance draw is skipped)");
        bmf_check_eq_i64(t, s.pipeline_changes, 3, __FILE__, __LINE__, "pipeline changes (first draw of each pass counts)");
        bmf_check_eq_i64(t, s.bindset_changes, 4, __FILE__, __LINE__, "bind set changes");
        bmf_check_eq_i64(t, s.instances, 100 + 300 + 200 + 99 + 1, __FILE__, __LINE__, "instances");
        bmf_check_eq_i64(t, s.vertices, 4 * (100 + 300 + 200 + 99 + 1), __FILE__, __LINE__, "vertices");
        bmf_check_eq_i64(t, s.bytes_written, 64 + 64 + 20 * 1000, __FILE__, __LINE__, "bytes written");
    }

    // An empty frame resets the per-frame counters.
    bmf_gpu_begin_frame();
    bmf_gpu_end_frame();
    s = bmf_gpu_get_stats();
    bmf_check(t, s.frames == 3 && s.passes == 0 && s.draws == 0 && s.bytes_written == 0, __FILE__, __LINE__, "empty frame stats");

    // The next frame may write the same ranges again (a new write period).
    bmf_gpu_begin_frame();
    static u8 again[64];
    bmf_gpu_buffer_write(uniform_buffer, 0, again, 64);
    bmf_gpu_end_frame();

    // Readback: validated, then unsupported on the null backend (D-38: shot reports no_gpu).
    bmf_check(t, bmf_gpu_texture_read_begin(target) == BMF_ERR_UNSUPPORTED, __FILE__, __LINE__, "null backend cannot read back");

    // Destroy: handles go stale, slots are reused with a new generation, counts drop to the surface.
    bmf_gpu_bindset_destroy(bindset_a);
    bmf_gpu_bindset_destroy(bindset_b);
    bmf_gpu_bindset_destroy(bindset_target);
    bmf_gpu_pipeline_destroy(pipeline);
    bmf_gpu_pipeline_destroy(pipeline_depth);
    bmf_gpu_pipeline_destroy(pipeline_additive);
    bmf_gpu_sampler_destroy(sampler);
    bmf_gpu_texture_destroy(atlas);
    bmf_gpu_texture_destroy(target);
    bmf_gpu_texture_destroy(depth);
    bmf_gpu_buffer_destroy(quad_buffer);
    bmf_gpu_buffer_destroy(instance_buffer);
    bmf_gpu_buffer_destroy(uniform_buffer);
    s = bmf_gpu_get_stats();
    bmf_check(t, s.buffers == 0 && s.textures == 1 && s.samplers == 0 && s.pipelines == 0 && s.bindsets == 0,
              __FILE__, __LINE__, "only the surface is left");
    bmf_check(t, !gpu_buffer_live(quad_buffer) && !gpu_texture_live(atlas) && !gpu_pipeline_live(pipeline) &&
              !gpu_sampler_live(sampler) && !gpu_bindset_live(bindset_a), __FILE__, __LINE__, "destroyed handles are stale");
    bmf_gpu_buffer reused = bmf_gpu_buffer_create(&vb);
    bmf_check(t, reused.id != quad_buffer.id && gpu_handle_slot(reused.id) == gpu_handle_slot(uniform_buffer.id),
              __FILE__, __LINE__, "slot reused (LIFO) with a new generation");
    bmf_check(t, !gpu_buffer_live(uniform_buffer), __FILE__, __LINE__, "old handle to the reused slot stays stale");
    bmf_gpu_buffer_destroy(reused);
    bmf_gpu_shutdown();
}

// Expected crashes: each breaks one validation rule; the assert must stop the process.
static void test_gpu_validation_draw_outside_pass(bmf_test* t) {
    gpu_test_init_null(0, 0);
    bmf_gpu_pipeline_desc pd = gpu_test_pipeline_desc(BMF_GPU_FORMAT_BGRA8_UNORM, BMF_GPU_FORMAT_NONE);
    pd.vertex_buffer_count = 0;
    bmf_gpu_draw_desc draw = { bmf_gpu_pipeline_create(&pd), { 0 }, { { 0 }, { 0 } }, { 0, 0 }, 3, 1, 0, 0 };
    bmf_gpu_begin_frame();
    bmf_gpu_draw(&draw);
}

static void test_gpu_validation_format_mismatch(bmf_test* t) {
    gpu_test_init_null(0, 0);
    bmf_gpu_texture_desc td = { BMF_GPU_FORMAT_RGBA8_UNORM, BMF_GPU_TEXTURE_RENDER_TARGET, 8, 8, 0, "rgba target" };
    bmf_gpu_pass_desc pass = { bmf_gpu_texture_create(&td), { 0 }, BMF_GPU_LOAD_CLEAR, { 0, 0, 0, 0 }, BMF_GPU_LOAD_CLEAR, 0, "pass" };
    bmf_gpu_pipeline_desc pd = gpu_test_pipeline_desc(BMF_GPU_FORMAT_BGRA8_UNORM, BMF_GPU_FORMAT_NONE);   // not RGBA8
    pd.vertex_buffer_count = 0;
    bmf_gpu_draw_desc draw = { bmf_gpu_pipeline_create(&pd), { 0 }, { { 0 }, { 0 } }, { 0, 0 }, 3, 1, 0, 0 };
    bmf_gpu_begin_frame();
    bmf_gpu_begin_pass(&pass);
    bmf_gpu_draw(&draw);
}

static void test_gpu_validation_write_overlap(bmf_test* t) {
    gpu_test_init_null(0, 0);
    bmf_gpu_buffer_desc bd = { BMF_GPU_BUFFER_VERTEX, 256, 1, 0, "ring" };
    bmf_gpu_buffer b = bmf_gpu_buffer_create(&bd);
    static u8 data[64];
    bmf_gpu_buffer_write(b, 64, data, 64);
    bmf_gpu_buffer_write(b, 0, data, 64);   // before the previous range in the same period
}

static void test_gpu_validation_static_uniform_write(bmf_test* t) {
    gpu_test_init_null(0, 0);
    bmf_gpu_buffer_desc ud = { BMF_GPU_BUFFER_UNIFORM, 256, 0, 0, "constants" };
    bmf_gpu_buffer u = bmf_gpu_buffer_create(&ud);
    static u8 data[64];
    bmf_gpu_buffer_write(u, 0, data, 64);   // UNIFORM buffers must be dynamic to be written
}

static void test_gpu_validation_vertex_read_past_end(bmf_test* t) {
    gpu_test_init_null(0, 0);
    bmf_gpu_buffer_desc vb = { BMF_GPU_BUFFER_VERTEX, 32, 0, 0, "quad" };
    bmf_gpu_buffer_desc ib = { BMF_GPU_BUFFER_VERTEX, 20 * 10, 1, 0, "instances" };
    bmf_gpu_buffer quad = bmf_gpu_buffer_create(&vb);
    bmf_gpu_buffer instances = bmf_gpu_buffer_create(&ib);
    bmf_gpu_pipeline_desc pd = gpu_test_pipeline_desc(BMF_GPU_FORMAT_BGRA8_UNORM, BMF_GPU_FORMAT_NONE);
    bmf_gpu_texture_desc td = { BMF_GPU_FORMAT_BGRA8_UNORM, BMF_GPU_TEXTURE_RENDER_TARGET, 8, 8, 0, "target" };
    bmf_gpu_pass_desc pass = { bmf_gpu_texture_create(&td), { 0 }, BMF_GPU_LOAD_CLEAR, { 0, 0, 0, 0 }, BMF_GPU_LOAD_CLEAR, 0, "pass" };
    bmf_gpu_draw_desc draw = { bmf_gpu_pipeline_create(&pd), { 0 }, { quad, instances }, { 0, 0 }, 4, 11, 0, 0 };   // 11 > 10 instances
    bmf_gpu_begin_frame();
    bmf_gpu_begin_pass(&pass);
    bmf_gpu_draw(&draw);
}
