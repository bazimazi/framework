// base_png_read_stored.c: decoder for PNGs whose zlib stream has only stored deflate blocks
// (task 1A-32; the full inflate comes with 1B-30). Contract in base_png.h.

static u32 png_get_u32_be(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3]; }

// Walks the chunks after the signature; checks lengths and CRCs. Reports IHDR and the IDAT run.
typedef struct png_layout {
    const u8* ihdr;        // IHDR data (13 bytes)
    i64 idat_first;        // offset of the first IDAT chunk header
    i64 idat_bytes;        // sum of IDAT data lengths
} png_layout;

static bmf_err png_scan(const u8* data, i64 size, png_layout* out, b32 header_only) {
    static const u8 signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    if (!data || size < 8 + 25 || bmf_memcmp(data, signature, 8) != 0) return BMF_ERR_INVALID;
    bmf_memset(out, 0, sizeof *out);
    out->idat_first = -1;
    b32 idat_done = 0;   // an IDAT run ended (a later IDAT is invalid)
    for (i64 at = 8;;) {
        if (size - at < 12) return BMF_ERR_INVALID;   // truncated (no IEND)
        u32 length = png_get_u32_be(data + at);
        if (length > 0x7FFFFFFFu || (i64)length > size - at - 12) return BMF_ERR_INVALID;
        const u8* type = data + at + 4;
        if (png_get_u32_be(data + at + 8 + length) != bmf_crc32(0, type, 4 + (i64)length)) return BMF_ERR_INVALID;
        b32 is_ihdr = bmf_memcmp(type, "IHDR", 4) == 0;
        if ((at == 8) != is_ihdr) return BMF_ERR_INVALID;   // IHDR first, exactly once
        if (is_ihdr) {
            if (length != 13) return BMF_ERR_INVALID;
            out->ihdr = data + at + 8;
            if (header_only) return BMF_OK;
        } else if (bmf_memcmp(type, "IDAT", 4) == 0) {
            if (idat_done) return BMF_ERR_INVALID;
            if (out->idat_first < 0) out->idat_first = at;
            out->idat_bytes += length;
        } else {
            if (out->idat_first >= 0) idat_done = 1;
            if (bmf_memcmp(type, "IEND", 4) == 0) return out->idat_first >= 0 ? BMF_OK : BMF_ERR_INVALID;
            if (type[0] >= 'A' && type[0] <= 'Z' && bmf_memcmp(type, "PLTE", 4) != 0) return BMF_ERR_UNSUPPORTED;   // unknown critical chunk
        }
        at += 12 + (i64)length;
    }
}

bmf_err bmf_png_info(const u8* data, i64 size, i32* width, i32* height) {
    png_layout layout;
    bmf_err err = png_scan(data, size, &layout, 1);
    if (err != BMF_OK) return err;
    u32 w = png_get_u32_be(layout.ihdr);
    u32 h = png_get_u32_be(layout.ihdr + 4);
    if (w < 1 || h < 1 || w > BMF_PNG_MAX_DIMENSION || h > BMF_PNG_MAX_DIMENSION) return BMF_ERR_INVALID;
    *width = (i32)w;
    *height = (i32)h;
    return BMF_OK;
}

// Byte stream over the data of consecutive IDAT chunks.
typedef struct png_stream {
    const u8* data;
    i64 at;      // next byte
    i64 left;    // bytes left in the current chunk
} png_stream;

static b32 png_next(png_stream* s, u8* out) {
    while (s->left == 0) {   // step over the CRC and the next chunk header; png_scan checked the run
        s->at += 4;
        if (bmf_memcmp(s->data + s->at + 4, "IDAT", 4) != 0) return 0;
        s->left = png_get_u32_be(s->data + s->at);
        s->at += 8;
    }
    *out = s->data[s->at++];
    s->left--;
    return 1;
}

static u8 png_paeth(u8 a, u8 b, u8 c) {
    i32 p = (i32)a + (i32)b - (i32)c;
    i32 pa = p > a ? p - a : a - p;
    i32 pb = p > b ? p - b : b - p;
    i32 pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

bmf_err bmf_png_read_stored(const u8* data, i64 size, u8* out_rgba, i64 out_capacity, i32* width, i32* height) {
    png_layout layout;
    bmf_err err = png_scan(data, size, &layout, 0);
    if (err != BMF_OK) return err;
    const u8* ihdr = layout.ihdr;
    u32 w = png_get_u32_be(ihdr);
    u32 h = png_get_u32_be(ihdr + 4);
    if (w < 1 || h < 1 || w > BMF_PNG_MAX_DIMENSION || h > BMF_PNG_MAX_DIMENSION) return BMF_ERR_INVALID;
    if (ihdr[10] != 0 || ihdr[11] != 0) return BMF_ERR_INVALID;               // compression and filter method
    if (ihdr[8] != 8 || (ihdr[9] != 6 && ihdr[9] != 2) || ihdr[12] != 0) return BMF_ERR_UNSUPPORTED;
    i32 bpp = ihdr[9] == 6 ? 4 : 3;
    i64 stride = (i64)w * bpp;
    if (out_capacity < (i64)w * h * 4) return BMF_ERR_RANGE;

    png_stream s = { data, layout.idat_first + 8, png_get_u32_be(data + layout.idat_first) };
    u8 cmf, flg;
    if (!png_next(&s, &cmf) || !png_next(&s, &flg)) return BMF_ERR_INVALID;
    if ((cmf & 15) != 8 || (cmf >> 4) > 7 || ((u32)cmf * 256u + flg) % 31u != 0 || (flg & 0x20)) return BMF_ERR_INVALID;

    // Rows are reconstructed packed (stride = w * bpp) at the start of out_rgba; RGB is widened
    // to RGBA afterwards, from the end backwards.
    i64 raw_total = (i64)h * (1 + stride);
    i64 raw_at = 0;   // filtered bytes consumed
    u8 filter = 0;
    u32 adler = 1;
    b32 final = 0;
    while (!final) {
        u8 header;
        if (!png_next(&s, &header)) return BMF_ERR_INVALID;
        final = header & 1;
        if (((header >> 1) & 3) != 0) return BMF_ERR_UNSUPPORTED;   // compressed block: needs inflate (1B-30)
        u8 b[4];
        for (i32 i = 0; i < 4; i++) if (!png_next(&s, &b[i])) return BMF_ERR_INVALID;
        u32 length = (u32)b[0] | ((u32)b[1] << 8);
        u32 nlength = (u32)b[2] | ((u32)b[3] << 8);
        if ((length ^ 0xFFFFu) != nlength) return BMF_ERR_INVALID;
        for (u32 k = 0; k < length; k++) {
            u8 x;
            if (!png_next(&s, &x)) return BMF_ERR_INVALID;
            adler = bmf_adler32(adler, &x, 1);
            if (raw_at >= raw_total) return BMF_ERR_INVALID;   // more data than the image holds
            i64 row = raw_at / (1 + stride);
            i64 column = raw_at % (1 + stride);
            raw_at++;
            if (column == 0) {
                if (x > 4) return BMF_ERR_INVALID;
                filter = x;
                continue;
            }
            i64 i = column - 1;
            u8* cur = out_rgba + row * stride;
            u8 left = i >= bpp ? cur[i - bpp] : 0;
            u8 up = row > 0 ? cur[i - stride] : 0;
            u8 up_left = row > 0 && i >= bpp ? cur[i - stride - bpp] : 0;
            switch (filter) {
            case 0: cur[i] = x; break;
            case 1: cur[i] = (u8)(x + left); break;
            case 2: cur[i] = (u8)(x + up); break;
            case 3: cur[i] = (u8)(x + (u8)(((u32)left + up) >> 1)); break;
            default: cur[i] = (u8)(x + png_paeth(left, up, up_left)); break;
            }
        }
    }
    if (raw_at != raw_total) return BMF_ERR_INVALID;
    u8 a[4];
    for (i32 i = 0; i < 4; i++) if (!png_next(&s, &a[i])) return BMF_ERR_INVALID;
    if (png_get_u32_be(a) != adler) return BMF_ERR_INVALID;
    if (bpp == 3) {
        for (i64 p = (i64)w * h - 1; p >= 0; p--) {
            u8 r = out_rgba[p * 3], g = out_rgba[p * 3 + 1], bl = out_rgba[p * 3 + 2];
            out_rgba[p * 4] = r; out_rgba[p * 4 + 1] = g; out_rgba[p * 4 + 2] = bl; out_rgba[p * 4 + 3] = 255;
        }
    }
    *width = (i32)w;
    *height = (i32)h;
    return BMF_OK;
}
