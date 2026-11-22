// shot.c: screenshots and image comparison (task 1A-32). Contracts in shot.h.

bmf_err bmf_shot_save(const char* path, i32 width, i32 height, i32* out_width, i32* out_height, i64* out_bytes,
                      char* error, i32 error_capacity) {
    BMF_ASSERT(path && error && error_capacity > 0);
    error[0] = 0;
    bmf_str name = bmf_str_from_cstr(path);
    if (name.len < 5 || !bmf_str_ends_with(name, bmf_str_from_cstr(".png"))) {
        bmf_fmt_buf(error, error_capacity, "'%s': the file name must end in .png", path);
        return BMF_ERR_INVALID;
    }
    if (!bmf_core_display_is_open()) {
        bmf_fmt_buf(error, error_capacity, "no display is open (shots need the runtime's renderer)");
        return BMF_ERR_UNSUPPORTED;
    }
    if (width == 0 && height == 0) {
        bmf_draw2d_view view = bmf_core_view();
        width = view.width;
        height = view.height;
    }
    if (width < 1 || height < 1 || width > BMF_SHOT_MAX_DIMENSION || height > BMF_SHOT_MAX_DIMENSION) {
        bmf_fmt_buf(error, error_capacity, "size %ix%i out of range 1..%i", width, height, (i32)BMF_SHOT_MAX_DIMENSION);
        return BMF_ERR_INVALID;
    }
    i64 pixel_bytes = (i64)width * height * 4;
    i64 png_bytes = bmf_png_write_size(width, height);
    bmf_arena* arena = &bmf_core_memory()->permanent;
    i64 needed = pixel_bytes + png_bytes + 64;
    if (arena->reserved - arena->used < needed) {
        bmf_fmt_buf(error, error_capacity, "a %ix%i shot needs %li MiB, the permanent arena has %li MiB free (raise memory.permanent)",
                    width, height, (needed + (1 << 20) - 1) >> 20, (arena->reserved - arena->used) >> 20);
        return BMF_ERR_RANGE;
    }
    i64 mark = bmf_arena_mark(arena);
    u8* pixels = (u8*)bmf_arena_push_aligned(arena, pixel_bytes, 16);
    bmf_err err = bmf_core_capture(width, height, pixels, pixel_bytes);
    if (err != BMF_OK) {
        bmf_arena_pop_to(arena, mark);
        bmf_fmt_buf(error, error_capacity, err == BMF_ERR_UNSUPPORTED ? "the null GPU backend renders nothing (started with --no-gpu?)"
                                                                       : "the GPU readback failed");
        return err;
    }
    u8* png = (u8*)bmf_arena_push_aligned(arena, png_bytes, 16);
    i64 written = bmf_png_write(pixels, width, height, png, png_bytes);
    err = bmf_platform_file_write_atomic(path, png, written);
    bmf_arena_pop_to(arena, mark);
    if (err != BMF_OK) {
        bmf_fmt_buf(error, error_capacity, "cannot write '%s' (folder missing, or the file is locked)", path);
        return BMF_ERR_IO;
    }
    *out_width = width;
    *out_height = height;
    *out_bytes = written;
    return BMF_OK;
}

bmf_image_diff_result bmf_image_diff(const u8* a, const u8* b, i32 width, i32 height, i32 tolerance, u8* diff_rgba) {
    BMF_ASSERT(a && b && width >= 0 && height >= 0 && tolerance >= 0 && tolerance <= 255);
    bmf_image_diff_result r;
    bmf_memset(&r, 0, sizeof r);
    r.pixels = (i64)width * height;
    r.first_x = -1;
    r.first_y = -1;
    for (i64 p = 0; p < r.pixels; p++) {
        i32 worst = 0;
        for (i32 c = 0; c < 4; c++) {
            i32 d = (i32)a[p * 4 + c] - (i32)b[p * 4 + c];
            if (d < 0) d = -d;
            if (d > worst) worst = d;
        }
        if (worst > r.max_channel_diff) r.max_channel_diff = worst;
        b32 differs = worst > tolerance;
        if (differs) {
            if (r.differing == 0) {
                r.first_x = (i32)(p % width);
                r.first_y = (i32)(p / width);
            }
            r.differing++;
        }
        if (!diff_rgba) continue;
        u8* d = diff_rgba + p * 4;
        if (differs) {
            d[0] = 255; d[1] = 0; d[2] = 0; d[3] = 255;
        } else {
            // Integer luma (BT.601 weights in 1/256), then a third of it.
            u32 luma = (77u * a[p * 4] + 150u * a[p * 4 + 1] + 29u * a[p * 4 + 2]) >> 8;
            u8 gray = (u8)(luma / 3u);
            d[0] = gray; d[1] = gray; d[2] = gray; d[3] = 255;
        }
    }
    r.differing_fraction = r.pixels ? (f64)r.differing / (f64)r.pixels : 0.0;
    return r;
}
