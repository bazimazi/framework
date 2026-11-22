// shot.h: screenshots and image comparison (SPEC §11.2 `shot`, task 1A-32).
//
// `shot PATH.png [W H]` renders the current state once (the game's draw, current alpha) into an
// offscreen target, reads it back and writes it as a stored-deflate PNG (base_png.h). Without
// W H the target is the current view (window client area or the --size headless target, at its
// DPI scale); with W H it is a W x H target at DPI scale 1. Rendering a shot is a GPU frame of
// its own between two engine frames; it changes nothing in the state.
//
// bmf_image_diff compares two RGBA images (golden tests, task 1A-41): a pixel differs when any
// channel differs by more than `tolerance`; the result counts them and can paint a diff image.
//
// Main thread only. Memory for pixels and the PNG is taken from the permanent arena and given
// back (mark/pop) before returning.
#ifndef BMF_SHOT_H
#define BMF_SHOT_H

#include "../bmf.h"
#include "../base/base_png.h"

enum { BMF_SHOT_MAX_DIMENSION = 8192 };

// Renders and writes `path` (it must end in ".png"). width/height 0 = the current view, else
// both 1..BMF_SHOT_MAX_DIMENSION. Errors (message in error[0..error_capacity)):
// BMF_ERR_UNSUPPORTED (null GPU backend: --no-gpu), BMF_ERR_RANGE (too big for the free part of
// the permanent arena), BMF_ERR_IO (the file could not be written), BMF_ERR_INVALID (bad path or
// size). On success writes the image size and file size. Needs a running display
// (bmf_core_display_open). O(pixels).
bmf_err bmf_shot_save(const char* path, i32 width, i32 height, i32* out_width, i32* out_height, i64* out_bytes,
                      char* error, i32 error_capacity);

typedef struct bmf_image_diff_result {
    i64 pixels;              // width * height
    i64 differing;           // pixels with some channel difference > tolerance
    f64 differing_fraction;  // differing / pixels
    i32 max_channel_diff;    // largest channel difference over the whole image (0..255)
    i32 first_x, first_y;    // first differing pixel in row order, or -1, -1
} bmf_image_diff_result;

// Compares two width x height RGBA images. `tolerance` 0..255 per channel. When `diff_rgba` is
// not 0 it receives width * height * 4 bytes: differing pixels opaque red, the others the image
// `a` in gray at a third of its brightness (context). Deterministic, any thread. O(pixels).
bmf_image_diff_result bmf_image_diff(const u8* a, const u8* b, i32 width, i32 height, i32 tolerance, u8* diff_rgba);

#endif // BMF_SHOT_H
