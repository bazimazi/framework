// draw2d.h: the batched instanced 2D renderer (SPEC §12.2, task 1A-20). The game side (cameras,
// shapes, text) is public in bmf.h; this header is the engine side and the instance format.
//
// Games call the bmf_draw_* functions during their draw callback; the engine brackets a frame
// with bmf_draw2d_begin_frame and renders with bmf_draw2d_flush inside a GPU pass. Every shape is
// one 48-byte instance of a single pipeline (draw2d.hlsl); the pixel shader picks an SDF by
// `kind` (task 1A-20b).
//
// Order: each submit names a layer (0..255). Instances draw in layer order. Within a layer they
// are grouped by pipeline and texture slot (shapes use the built-in white texture's slot),
// and within a group they keep submission order. Overlap order between a sprite and a shape, or
// between two textures, in one layer is therefore NOT submission order: use layers for that.
//
// Coordinates (D-19, task 1A-20c): submit calls take camera-space values of the layer's current
// camera: a WORLD camera (y up, meters; the default for every layer: 64 logical pixels per
// meter, world origin at the view center) or a SCREEN camera (y down, logical pixels, origin
// top-left). Each layer has a camera stack (bmf_draw_camera_push/pop), reset every frame.
// Submits are transformed to target pixels at once (the instances and the shader only know
// target pixels), with the view (target size and DPI scale) given to begin_frame.
// Angles are radians, counter-clockwise on screen positive, in every camera space.
// Colors are bmf_color (0xRRGGBBAA sRGB, straight alpha); the shader premultiplies.
//
// Thread rule for every function: main thread only. Not allowed in tick (rendering is
// presentation, SPEC §8.3).
#ifndef BMF_DRAW2D_H
#define BMF_DRAW2D_H

#include "../bmf.h"
#include "../base/base_memory.h"
#include "../gpu/gpu.h"

// ---------------------------------------------------------------- instance format
// Shape kinds (instance `kind_corner` bits 0..7). Field use per kind:
//   RECT     position = center, size = full size, rotation, corner radius (0 = sharp), stroke
//            (0 = filled, else outline width), softness
//   CIRCLE   position = center, size = (diameter, diameter), stroke (0 = disc, else ring width)
//   CAPSULE  position = endpoint a, extra.xy = endpoint b, size.y = thickness (round caps)
//   TRIANGLE position = vertex a, extra.xy = vertex b, extra.zw = vertex c
//   SPRITE   as RECT, plus extra = uv rect (u0, v0, u1, v1) into the instance's texture
//   GLYPH    as CAPSULE (stroke-font segments, task 1A-21)
typedef enum bmf_draw_kind {
    BMF_DRAW_KIND_RECT = 0,
    BMF_DRAW_KIND_CIRCLE = 1,
    BMF_DRAW_KIND_CAPSULE = 2,
    BMF_DRAW_KIND_TRIANGLE = 3,
    BMF_DRAW_KIND_SPRITE = 4,
    BMF_DRAW_KIND_GLYPH = 5,
    BMF_DRAW_KIND_COUNT = 6,
} bmf_draw_kind;

// One instance, 48 bytes, the per-instance vertex stream of draw2d.hlsl (locations in brackets).
// All lengths are target pixels. f16 fields are IEEE half floats (round to nearest even,
// saturated to 65504).
typedef struct bmf_draw_instance {
    bmf_v2 position;     // [0] F32X2   offset 0
    bmf_v2 size;         // [1] F32X2   offset 8
    f32 rotation;        // [2] F32     offset 16: radians, applied in y-down pixel space (= -screen angle)
    u32 color;           // [3] U8X4    offset 20: bmf_color; little-endian bytes A,B,G,R (shader swizzles .wzyx)
    bmf_v4 extra;        // [4] F32X4   offset 24: uv rect, or the other capsule/triangle points
    u32 kind_corner;     // [5] U32     offset 40: kind (bits 0..7) | flags (8..15, 0) | corner radius f16 (16..31)
    u32 stroke_softness; // [6] U32     offset 44: stroke f16 (0..15) | softness f16 (16..31)
} bmf_draw_instance;
BMF_STATIC_ASSERT(sizeof(bmf_draw_instance) == 48, "draw2d instance is 48 bytes (SPEC §12.2)");

// Sort key (SPEC §12.2): layer(8) | pipeline(4) | texture(12) | depth(24) | seq(16), most
// significant first. pipeline = blend preset (0 = premultiplied, the only one so far), texture =
// GPU texture slot index. depth and seq are 0: the radix sort is stable, so equal keys keep
// submission order without spending sort passes on a sequence number.
enum {
    BMF_DRAW_KEY_LAYER_SHIFT = 56,
    BMF_DRAW_KEY_PIPELINE_SHIFT = 52,
    BMF_DRAW_KEY_TEXTURE_SHIFT = 40,
    BMF_DRAW_KEY_DEPTH_SHIFT = 16,
};

// IEEE half float of `value` (round to nearest even; values above 65504 saturate, negative
// values and NaN become 0: every f16 field is a non-negative length). O(1).
u32 bmf_draw_f16(f32 value);

// ---------------------------------------------------------------- engine interface
enum {
    BMF_DRAW2D_DEFAULT_MAX_INSTANCES = 65536,   // per frame, all flushes together
    BMF_DRAW2D_MAX_FLUSHES_PER_FRAME = 4,       // one uniform block (256 bytes) each
    BMF_DRAW2D_MAX_LAYERS = 256,
};

typedef struct bmf_draw2d_desc {
    i32 max_instances;   // per frame; 0 = BMF_DRAW2D_DEFAULT_MAX_INSTANCES. Sets the GPU ring
                         // size (48 bytes each) and the frame-arena use (about 120 bytes each)
} bmf_draw2d_desc;

// Creates the GPU objects (instance ring, uniform buffer, 1x1 white texture, sampler); pipelines
// and bind sets are created on first use. The GPU layer must be running. O(1).
void bmf_draw2d_init(const bmf_draw2d_desc* desc);

// Destroys every GPU object draw2d made. Outside a GPU frame. O(objects).
void bmf_draw2d_shutdown(void);

// What submits are transformed for: the render target in physical pixels and the DPI scale
// (physical pixels per logical pixel, platform bmf_window_info.dpi_scale; 1 offscreen).
typedef struct bmf_draw2d_view {
    i32 width;          // physical pixels, > 0
    i32 height;
    f32 dpi_scale;      // > 0
} bmf_draw2d_view;

// Starts collecting a frame's instances into arrays pushed on `frame` (which must stay valid
// until the frame's last flush; the engine resets it every frame) and resets every layer's
// camera stack to the default world camera for `view`. Outside a GPU frame: it also destroys
// bind sets made stale by destroyed textures. O(layers) plus arena pushes.
void bmf_draw2d_begin_frame(bmf_arena* frame, bmf_draw2d_view view);

// Sorts the instances submitted since begin_frame or the previous flush, uploads them with one
// bmf_gpu_buffer_write, and records one draw per run of equal pipeline and texture into the
// current GPU pass, whose color target is `target_width` x `target_height` pixels of
// `color_format` (no depth target). Submits were placed for begin_frame's view, so the target
// normally has the view's size. Then the instance list is empty again. Inside a GPU pass;
// at most BMF_DRAW2D_MAX_FLUSHES_PER_FRAME flushes per frame. Temporaries come from the frame
// arena and are popped again. O(n) (radix sort: at most 8 passes, constant key bytes skipped).
void bmf_draw2d_flush(i32 target_width, i32 target_height, bmf_gpu_format color_format);

typedef struct bmf_draw2d_stats {
    // Last flush:
    i32 instances;      // drawn
    i32 batches;        // bmf_gpu_draw calls
    // This frame so far:
    i32 frame_instances;
    i32 dropped;        // submits past max_instances (not drawn)
} bmf_draw2d_stats;

bmf_draw2d_stats bmf_draw2d_get_stats(void);

// ---------------------------------------------------------------- sprites (engine and tests only until assets, M8)
// Textured quad: `uv` is the source rectangle in texture coordinates (0..1, y down; the
// texture's top row appears at the top of the quad in every camera space), `tint`
// multiplies the texture color. `texture` is a live SAMPLED GPU texture. Not in bmf.h: games get
// sprites with the public texture handle of the asset tasks (1B-32, 1B-33). The texture must stay
// live until the frame's flush.
void bmf_draw_sprite(i32 layer, bmf_gpu_texture texture, bmf_rect uv, bmf_v2 center, bmf_v2 size, f32 rotation, bmf_color tint);

#endif // BMF_DRAW2D_H
