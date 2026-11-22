// Tests for src/base/base_png_write.c and base_png_read_stored.c (task 1A-32): round trips at
// sizes that split stored blocks inside rows and pixels, a pinned encoding, files made by an
// independent encoder, and every rejection path.
//
// The PNG_REF_* arrays were made once with Python's standard zlib module (an independent encoder):
// a 5x5 RGBA image whose rows use filters 0, 1, 2, 3, 4 with zlib level 0 (stored blocks), the
// same image at level 6 (compressed: must be refused), and a 3x2 RGB image with filters 4 and 1.
// Pixel bytes: RGBA ((x*37 + y*11 + c*53)*7 + x*y) & 255, RGB (x*90 + y*40 + c*70) & 255.

static const u8 PNG_REF_RGBA_PIXELS[100] = {
    0x00, 0x73, 0xe6, 0x59, 0x03, 0x76, 0xe9, 0x5c, 0x06, 0x79, 0xec, 0x5f, 0x09, 0x7c, 0xef, 0x62, 0x0c, 0x7f, 0xf2, 0x65,
    0x4d, 0xc0, 0x33, 0xa6, 0x51, 0xc4, 0x37, 0xaa, 0x55, 0xc8, 0x3b, 0xae, 0x59, 0xcc, 0x3f, 0xb2, 0x5d, 0xd0, 0x43, 0xb6,
    0x9a, 0x0d, 0x80, 0xf3, 0x9f, 0x12, 0x85, 0xf8, 0xa4, 0x17, 0x8a, 0xfd, 0xa9, 0x1c, 0x8f, 0x02, 0xae, 0x21, 0x94, 0x07,
    0xe7, 0x5a, 0xcd, 0x40, 0xed, 0x60, 0xd3, 0x46, 0xf3, 0x66, 0xd9, 0x4c, 0xf9, 0x6c, 0xdf, 0x52, 0xff, 0x72, 0xe5, 0x58,
    0x34, 0xa7, 0x1a, 0x8d, 0x3b, 0xae, 0x21, 0x94, 0x42, 0xb5, 0x28, 0x9b, 0x49, 0xbc, 0x2f, 0xa2, 0x50, 0xc3, 0x36, 0xa9,
};
static const u8 PNG_REF_RGBA_STORED[173] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x05,
    0x00, 0x00, 0x00, 0x05, 0x08, 0x06, 0x00, 0x00, 0x00, 0x8d, 0x6f, 0x26, 0xe5, 0x00, 0x00, 0x00, 0x74, 0x49, 0x44, 0x41,
    0x54, 0x78, 0x01, 0x01, 0x69, 0x00, 0x96, 0xff, 0x00, 0x00, 0x73, 0xe6, 0x59, 0x03, 0x76, 0xe9, 0x5c, 0x06, 0x79, 0xec,
    0x5f, 0x09, 0x7c, 0xef, 0x62, 0x0c, 0x7f, 0xf2, 0x65, 0x01, 0x4d, 0xc0, 0x33, 0xa6, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x02, 0x4d, 0x4d, 0x4d, 0x4d, 0x4e, 0x4e, 0x4e, 0x4e, 0x4f,
    0x4f, 0x4f, 0x4f, 0x50, 0x50, 0x50, 0x50, 0x51, 0x51, 0x51, 0x51, 0x03, 0x9a, 0x54, 0x8d, 0xc7, 0x2a, 0x2a, 0x2a, 0xaa,
    0x2b, 0x2b, 0x2b, 0xab, 0x2b, 0x2b, 0x2b, 0x2b, 0x2c, 0x2c, 0x2c, 0x2c, 0x04, 0x4d, 0x4d, 0x4d, 0x4d, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x1b, 0xc2, 0x18, 0xe5, 0x69, 0x0f, 0x09,
    0x13, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};
static const u8 PNG_REF_RGBA_COMPRESSED[142] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x05,
    0x00, 0x00, 0x00, 0x05, 0x08, 0x06, 0x00, 0x00, 0x00, 0x8d, 0x6f, 0x26, 0xe5, 0x00, 0x00, 0x00, 0x55, 0x49, 0x44, 0x41,
    0x54, 0x78, 0x9c, 0x63, 0x60, 0x28, 0x7e, 0x16, 0xc9, 0x5c, 0xf6, 0x32, 0x86, 0xad, 0xf2, 0x4d, 0x3c, 0x67, 0xcd, 0xfb,
    0x24, 0x9e, 0xfa, 0x4f, 0xa9, 0x8c, 0xbe, 0x07, 0x8c, 0x97, 0xb1, 0xa0, 0x01, 0x26, 0x5f, 0x5f, 0x5f, 0x5f, 0x3f, 0x3f,
    0x3f, 0x3f, 0x7f, 0x7f, 0x7f, 0xff, 0x80, 0x80, 0x80, 0x80, 0xc0, 0xc0, 0xc0, 0x40, 0xe6, 0x59, 0x21, 0xbd, 0xc7, 0xb5,
    0xb4, 0xb4, 0x56, 0x69, 0x6b, 0x6b, 0xaf, 0xd6, 0xd6, 0xd6, 0xd6, 0xd6, 0xd1, 0xd1, 0xd1, 0x61, 0x01, 0xa9, 0x64, 0x47,
    0x03, 0x00, 0x1b, 0xc2, 0x18, 0xe5, 0x97, 0x8f, 0x7d, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42,
    0x60, 0x82,
};
static const u8 PNG_REF_RGB_PIXELS[18] = {
    0x00, 0x46, 0x8c, 0x5a, 0xa0, 0xe6, 0xb4, 0xfa, 0x40, 0x28, 0x6e, 0xb4, 0x82, 0xc8, 0x0e, 0xdc, 0x22, 0x68,
};
static const u8 PNG_REF_RGB_STORED[88] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0x12, 0x16, 0xf1, 0x4d, 0x00, 0x00, 0x00, 0x1f, 0x49, 0x44, 0x41,
    0x54, 0x78, 0x01, 0x01, 0x14, 0x00, 0xeb, 0xff, 0x04, 0x00, 0x46, 0x8c, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x01, 0x28,
    0x6e, 0xb4, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x3c, 0x46, 0x06, 0x5a, 0xa1, 0x60, 0x21, 0x65, 0x00, 0x00, 0x00, 0x00,
    0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

static u8 png_test_pixels[1 << 20];
static u8 png_test_decoded[1 << 20];
static u8 png_test_file[(1 << 20) + 4096];

// Deterministic test image: every byte from a PCG32 stream.
static void png_test_fill(u8* rgba, i32 width, i32 height, u64 seed) {
    bmf_rng rng;
    bmf_rng_seed(&rng, seed, 1);
    for (i64 i = 0; i < (i64)width * height * 4; i++) rgba[i] = (u8)(bmf_rng_next_u32(&rng) >> 24);
}

// Writes the chunk CRC of the IDAT chunk again after a test changed its data (our files have one
// IDAT at offset 33).
static void png_test_fix_idat_crc(u8* png) {
    u32 length = png_get_u32_be(png + 33);
    u32 crc = bmf_crc32(0, png + 37, 4 + (i64)length);
    u8* at = png + 41 + length;
    at[0] = (u8)(crc >> 24); at[1] = (u8)(crc >> 16); at[2] = (u8)(crc >> 8); at[3] = (u8)crc;
}

// ---------------------------------------------------------------- png.roundtrip
static void test_png_roundtrip(bmf_test* t) {
    // Sizes that put stored-block boundaries in the middle of rows and pixels.
    const i32 sizes[][2] = { { 1, 1 }, { 3, 2 }, { 17, 5 }, { 200, 400 }, { 16384, 1 }, { 1, 1000 }, { 127, 129 } };
    for (i32 c = 0; c < (i32)(sizeof sizes / sizeof sizes[0]); c++) {
        i32 w = sizes[c][0], h = sizes[c][1];
        png_test_fill(png_test_pixels, w, h, (u64)c + 1);
        i64 expected_size = bmf_png_write_size(w, h);
        i64 size = bmf_png_write(png_test_pixels, w, h, png_test_file, (i64)sizeof png_test_file);
        char message[128];
        bmf_fmt_buf(message, (i32)sizeof message, "%ix%i image", w, h);
        bmf_check_eq_i64(t, size, expected_size, __FILE__, __LINE__, message);
        i32 iw = 0, ih = 0;
        bmf_check_eq_i64(t, bmf_png_info(png_test_file, size, &iw, &ih), BMF_OK, __FILE__, __LINE__, message);
        bmf_check(t, iw == w && ih == h, __FILE__, __LINE__, message);
        bmf_memset(png_test_decoded, 0xAB, (i64)w * h * 4);
        i32 dw = 0, dh = 0;
        bmf_check_eq_i64(t, bmf_png_read_stored(png_test_file, size, png_test_decoded, (i64)sizeof png_test_decoded, &dw, &dh), BMF_OK, __FILE__, __LINE__, message);
        bmf_check(t, dw == w && dh == h, __FILE__, __LINE__, message);
        bmf_check_eq_mem(t, png_test_decoded, png_test_pixels, (i64)w * h * 4, __FILE__, __LINE__, message);
    }
    // The encoding itself is pinned: same pixels, same bytes, on every target.
    png_test_fill(png_test_pixels, 3, 2, 7);
    i64 size = bmf_png_write(png_test_pixels, 3, 2, png_test_file, (i64)sizeof png_test_file);
    u64 hash = bmf_hash(png_test_file, size, 0);
    char message[128];
    bmf_fmt_buf(message, (i32)sizeof message, "3x2 encoding (%li bytes) pinned, got 0x%016lx", size, hash);
    bmf_check(t, size == 94 && hash == 0x4a6286d8f9e3dfdfull, __FILE__, __LINE__, message);
}

// ---------------------------------------------------------------- png.reference
// PNGs made by Python's zlib (see the top of this file): an independent encoder,
// row filters 0..4, RGB, and a compressed file.
static void test_png_reference(bmf_test* t) {
    i32 w = 0, h = 0;
    bmf_check_eq_i64(t, bmf_png_read_stored(PNG_REF_RGBA_STORED, (i64)sizeof PNG_REF_RGBA_STORED, png_test_decoded, (i64)sizeof png_test_decoded, &w, &h),
                     BMF_OK, __FILE__, __LINE__, "RGBA, filters 0..4");
    bmf_check(t, w == 5 && h == 5, __FILE__, __LINE__, "5x5");
    bmf_check_eq_mem(t, png_test_decoded, PNG_REF_RGBA_PIXELS, (i64)sizeof PNG_REF_RGBA_PIXELS, __FILE__, __LINE__, "RGBA pixels (every filter type)");
    bmf_check_eq_i64(t, bmf_png_read_stored(PNG_REF_RGB_STORED, (i64)sizeof PNG_REF_RGB_STORED, png_test_decoded, (i64)sizeof png_test_decoded, &w, &h),
                     BMF_OK, __FILE__, __LINE__, "RGB");
    b32 rgb_ok = w == 3 && h == 2;
    for (i32 p = 0; p < 6; p++)
        for (i32 c = 0; c < 4; c++) rgb_ok &= png_test_decoded[p * 4 + c] == (c < 3 ? PNG_REF_RGB_PIXELS[p * 3 + c] : 255);
    bmf_check(t, rgb_ok, __FILE__, __LINE__, "RGB widened to RGBA with alpha 255");
    bmf_check_eq_i64(t, bmf_png_read_stored(PNG_REF_RGBA_COMPRESSED, (i64)sizeof PNG_REF_RGBA_COMPRESSED, png_test_decoded, (i64)sizeof png_test_decoded, &w, &h),
                     BMF_ERR_UNSUPPORTED, __FILE__, __LINE__, "compressed deflate needs the full inflate (1B-30)");
    bmf_check_eq_i64(t, bmf_png_info(PNG_REF_RGBA_COMPRESSED, (i64)sizeof PNG_REF_RGBA_COMPRESSED, &w, &h), BMF_OK, __FILE__, __LINE__, "info works on any PNG");
}

// ---------------------------------------------------------------- png.errors
static void test_png_errors(bmf_test* t) {
    png_test_fill(png_test_pixels, 4, 3, 3);
    i64 size = bmf_png_write(png_test_pixels, 4, 3, png_test_file, (i64)sizeof png_test_file);
    static u8 broken[256];
    i32 w, h;
    bmf_check(t, size <= (i64)sizeof broken, __FILE__, __LINE__, "small file");

    bmf_check_eq_i64(t, bmf_png_read_stored(png_test_file, size, png_test_decoded, 4 * 3 * 4 - 1, &w, &h), BMF_ERR_RANGE, __FILE__, __LINE__, "output too small");
    bmf_check_eq_i64(t, bmf_png_read_stored(png_test_file, size - 1, png_test_decoded, (i64)sizeof png_test_decoded, &w, &h), BMF_ERR_INVALID, __FILE__, __LINE__, "truncated");
    bmf_check_eq_i64(t, bmf_png_read_stored(png_test_file, 20, png_test_decoded, (i64)sizeof png_test_decoded, &w, &h), BMF_ERR_INVALID, __FILE__, __LINE__, "only the signature");
    bmf_check_eq_i64(t, bmf_png_info((const u8*)"GIF89a not a png at all.........", 32, &w, &h), BMF_ERR_INVALID, __FILE__, __LINE__, "not a PNG");

    struct { const char* what; i32 offset; u8 xor_value; b32 fix_crc; bmf_err expected; } cases[] = {
        { "pixel byte changed (chunk CRC)", 60, 0x01, 0, BMF_ERR_INVALID },
        { "pixel byte changed (Adler-32)", 60, 0x01, 1, BMF_ERR_INVALID },
        { "IHDR byte changed (CRC)", 18, 0x01, 0, BMF_ERR_INVALID },
        { "zlib method", 41, 0x01, 1, BMF_ERR_INVALID },
        { "NLEN not ~LEN", 46, 0x01, 1, BMF_ERR_INVALID },
        { "block type 01 (fixed Huffman)", 43, 0x02, 1, BMF_ERR_UNSUPPORTED },
        { "filter type 5", 48, 0x05, 1, BMF_ERR_INVALID },
    };
    for (i32 c = 0; c < (i32)(sizeof cases / sizeof cases[0]); c++) {
        bmf_memcpy(broken, png_test_file, size);
        broken[cases[c].offset] ^= cases[c].xor_value;
        if (cases[c].fix_crc) png_test_fix_idat_crc(broken);
        bmf_check_eq_i64(t, bmf_png_read_stored(broken, size, png_test_decoded, (i64)sizeof png_test_decoded, &w, &h), cases[c].expected, __FILE__, __LINE__, cases[c].what);
    }
    // Interlaced and 16-bit images are not read (IHDR bytes 12 and 8 of the data, CRC fixed).
    for (i32 c = 0; c < 2; c++) {
        bmf_memcpy(broken, png_test_file, size);
        broken[c == 0 ? 28 : 24] = c == 0 ? 1 : 16;
        u32 crc = bmf_crc32(0, broken + 12, 17);
        broken[29] = (u8)(crc >> 24); broken[30] = (u8)(crc >> 16); broken[31] = (u8)(crc >> 8); broken[32] = (u8)crc;
        bmf_check_eq_i64(t, bmf_png_read_stored(broken, size, png_test_decoded, (i64)sizeof png_test_decoded, &w, &h), BMF_ERR_UNSUPPORTED, __FILE__, __LINE__,
                         c == 0 ? "interlaced" : "16-bit");
    }
}
