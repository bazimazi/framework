// base_png_write.c: PNG encoder with stored deflate blocks (task 1A-32). Contract in base_png.h.

enum { PNG_STORED_BLOCK_MAX = 65535 };   // deflate stored block payload limit

static const u8 PNG_SIGNATURE[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };

static void png_put_u32_be(u8* p, u32 v) {
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

// Bytes of the filtered image data (one filter byte per row) and of its zlib stream.
static i64 png_raw_size(i32 width, i32 height) { return (i64)height * (1 + (i64)width * 4); }

static i64 png_zlib_size(i64 raw) {
    i64 blocks = raw == 0 ? 1 : (raw + PNG_STORED_BLOCK_MAX - 1) / PNG_STORED_BLOCK_MAX;
    return 2 + raw + 5 * blocks + 4;   // header, data, per-block headers, Adler-32
}

i64 bmf_png_write_size(i32 width, i32 height) {
    BMF_ASSERT(width >= 1 && height >= 1 && width <= BMF_PNG_MAX_DIMENSION && height <= BMF_PNG_MAX_DIMENSION);
    return 8 + (12 + 13) + (12 + png_zlib_size(png_raw_size(width, height))) + 12;
}

i64 bmf_png_write(const u8* rgba, i32 width, i32 height, u8* out, i64 out_capacity) {
    i64 total = bmf_png_write_size(width, height);
    BMF_ASSERT(rgba && out && out_capacity >= total);
    u8* p = out;
    bmf_memcpy(p, PNG_SIGNATURE, 8);
    p += 8;

    // IHDR: width, height, bit depth 8, color type 6 (RGBA), compression 0, filter 0, no interlace.
    png_put_u32_be(p, 13);
    bmf_memcpy(p + 4, "IHDR", 4);
    png_put_u32_be(p + 8, (u32)width);
    png_put_u32_be(p + 12, (u32)height);
    p[16] = 8; p[17] = 6; p[18] = 0; p[19] = 0; p[20] = 0;
    png_put_u32_be(p + 21, bmf_crc32(0, p + 4, 17));
    p += 25;

    // IDAT: one chunk holding the whole zlib stream.
    i64 raw = png_raw_size(width, height);
    i64 zlib = png_zlib_size(raw);
    BMF_ASSERT(zlib <= 0x7FFFFFFF);   // PNG chunk length limit (2^31 - 1)
    png_put_u32_be(p, (u32)zlib);
    bmf_memcpy(p + 4, "IDAT", 4);
    u8* chunk_type = p + 4;
    u8* z = p + 8;
    z[0] = 0x78;   // CMF: deflate, 32 KiB window
    z[1] = 0x01;   // FLG: no dictionary, level 0; (0x78 * 256 + 0x01) % 31 == 0
    z += 2;
    // Stored blocks over the filtered rows, written row by row as the block boundaries fall.
    i64 row_bytes = 1 + (i64)width * 4;
    i64 block_left = 0;   // payload bytes still to come in the current block
    i64 remaining = raw;
    u32 adler = 1;
    for (i32 y = 0; y < height; y++) {
        const u8* row = rgba + (i64)y * width * 4;
        for (i64 i = 0; i < row_bytes;) {
            if (block_left == 0) {
                i64 length = remaining < PNG_STORED_BLOCK_MAX ? remaining : PNG_STORED_BLOCK_MAX;
                z[0] = remaining <= PNG_STORED_BLOCK_MAX ? 1 : 0;   // BFINAL, BTYPE 00 (stored)
                z[1] = (u8)length; z[2] = (u8)(length >> 8);
                z[3] = (u8)~length; z[4] = (u8)(~length >> 8);
                z += 5;
                block_left = length;
            }
            i64 take = row_bytes - i < block_left ? row_bytes - i : block_left;
            if (i == 0) {   // the filter byte (0 = none) leads the row
                z[0] = 0;
                adler = bmf_adler32(adler, z, 1);
                z++; i++; take--; block_left--; remaining--;
                if (take == 0) continue;
            }
            const u8* src = row + (i - 1);
            bmf_memcpy(z, src, take);
            adler = bmf_adler32(adler, z, take);
            z += take; i += take; block_left -= take; remaining -= take;
        }
    }
    png_put_u32_be(z, adler);
    z += 4;
    BMF_ASSERT(z - (p + 8) == zlib);
    png_put_u32_be(z, bmf_crc32(0, chunk_type, 4 + zlib));
    p = z + 4;

    png_put_u32_be(p, 0);
    bmf_memcpy(p + 4, "IEND", 4);
    png_put_u32_be(p + 8, bmf_crc32(0, p + 4, 4));
    p += 12;
    BMF_ASSERT(p - out == total);
    return total;
}
