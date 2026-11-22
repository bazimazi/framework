// base_png.h: PNG encoding (stored deflate) and decoding of stored-deflate PNGs (task 1A-32).
//
// The writer makes 8-bit RGBA, non-interlaced PNGs whose zlib stream uses only stored (raw)
// deflate blocks: no compression, but valid for every PNG reader, exact, fast and simple. Each
// row has filter type 0 (none). CRC-32 per chunk, Adler-32 over the raw data (bmf.h hashing).
// The reader accepts exactly the PNGs whose IDAT data is stored deflate: 8-bit RGBA or RGB,
// non-interlaced, any row filter (0..4). Compressed PNGs are BMF_ERR_UNSUPPORTED until the full
// inflate of task 1B-30. Golden-image comparisons (1A-41) read what the writer wrote.
//
// Pixels are RGBA, 4 bytes each, rows top to bottom, no padding. Both directions are pure functions
// (no allocation, no platform calls): any thread.
#ifndef BMF_BASE_PNG_H
#define BMF_BASE_PNG_H

#include "../bmf.h"

enum { BMF_PNG_MAX_DIMENSION = 16384 };   // width and height limit of both directions

// Exact size in bytes of the PNG bmf_png_write makes for width x height (1..BMF_PNG_MAX_DIMENSION
// each). O(1).
i64 bmf_png_write_size(i32 width, i32 height);

// Encodes `rgba` (width * height * 4 bytes) into out[0..bmf_png_write_size). `out_capacity` must be
// at least that size (asserted). Returns the number of bytes written. O(pixels).
i64 bmf_png_write(const u8* rgba, i32 width, i32 height, u8* out, i64 out_capacity);

// Reads width and height from a PNG's IHDR (signature and IHDR CRC checked). BMF_ERR_INVALID when
// it is not a PNG. O(1).
bmf_err bmf_png_info(const u8* data, i64 size, i32* width, i32* height);

// Decodes a stored-deflate PNG into out_rgba (width * height * 4 bytes; RGB gets alpha 255).
// Checks every chunk CRC, the zlib header, every stored block's LEN/NLEN, the Adler-32 and the
// filter types. Errors: BMF_ERR_INVALID (corrupt or truncated), BMF_ERR_UNSUPPORTED (compressed
// deflate blocks, interlace, a bit depth or color type other than 8-bit RGBA/RGB), BMF_ERR_RANGE
// (out_capacity too small). O(pixels).
bmf_err bmf_png_read_stored(const u8* data, i64 size, u8* out_rgba, i64 out_capacity, i32* width, i32* height);

#endif // BMF_BASE_PNG_H
