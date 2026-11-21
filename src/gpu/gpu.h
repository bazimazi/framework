// gpu.h: the backend-agnostic GPU API (SPEC §12.1, D-25, D-38). Internal: games draw through
// draw2d and never call this directly; it is not part of bmf.h.
//
// Shape (D-25): WebGPU-like. Buffers, textures, samplers, pipelines (shader, vertex layout,
// blend, topology, cull, depth and target formats baked in) and bind sets are created up front
// and named by generational u32 handles. Per frame: begin_frame, then passes, each a list of
// draws, then end_frame (submit and present). There is no bound state: every bmf_gpu_draw names
// its pipeline, bind set and vertex buffers.
//
// Backends: NULL (every platform, D-38: validates and counts every call, renders nothing),
// D3D11 (Windows, task 1A-14), WEBGPU (web, task 1B-42). All validation lives in the frontend
// (gpu.c) and is identical for every backend, so a bug that only WebGPU would reject is caught on
// the null backend in a native test. Validation failures are BMF_ASSERTs (programmer errors,
// debug builds only); their message names the function and the broken rule.
//
// Thread rule for every function: main thread only.
// Not allowed in tick: rendering is presentation, never simulation (SPEC §8.3).
#ifndef BMF_GPU_H
#define BMF_GPU_H

#include "../base/base_types.h"
#include "../base/base_math.h"

// ---------------------------------------------------------------- handles
// Generational handles (index:20 | generation:12, base_memory.h). {0} is never valid and means
// "none" where a field allows none. A destroyed handle stays invalid (until its slot's 12-bit
// generation wraps after 4095 reuses).
typedef struct bmf_gpu_buffer   { u32 id; } bmf_gpu_buffer;
typedef struct bmf_gpu_texture  { u32 id; } bmf_gpu_texture;
typedef struct bmf_gpu_sampler  { u32 id; } bmf_gpu_sampler;
typedef struct bmf_gpu_pipeline { u32 id; } bmf_gpu_pipeline;
typedef struct bmf_gpu_bindset  { u32 id; } bmf_gpu_bindset;

// ---------------------------------------------------------------- limits
enum {
    BMF_GPU_MAX_BUFFERS = 256,
    BMF_GPU_MAX_TEXTURES = 256,      // including the surface texture
    BMF_GPU_MAX_SAMPLERS = 32,
    BMF_GPU_MAX_PIPELINES = 64,
    BMF_GPU_MAX_BINDSETS = 256,
    BMF_GPU_MAX_SHADERS = 64,        // shader ids are 0..BMF_GPU_MAX_SHADERS-1 (generated enum, task 1A-15)

    BMF_GPU_MAX_BUFFER_SIZE = 256 << 20,          // WebGPU default maxBufferSize
    BMF_GPU_MAX_UNIFORM_BUFFER_SIZE = 64 << 10,   // whole uniform buffer; also the max binding size
    BMF_GPU_UNIFORM_OFFSET_ALIGNMENT = 256,       // D3D11.1 constant buffer offsets, WebGPU minUniformBufferOffsetAlignment
    BMF_GPU_MAX_TEXTURE_SIZE = 8192,              // WebGPU default maxTextureDimension2D

    BMF_GPU_MAX_VERTEX_BUFFERS = 2,               // per pipeline: e.g. one per-vertex and one per-instance stream
    BMF_GPU_MAX_VERTEX_ATTRIBUTES = 8,            // per vertex buffer
    BMF_GPU_MAX_VERTEX_LOCATIONS = 16,            // shader locations 0..15 (WebGPU maxVertexAttributes)
    BMF_GPU_MAX_VERTEX_STRIDE = 2048,             // D3D11 and WebGPU limit

    BMF_GPU_MAX_BIND_UNIFORMS = 4,                // bind set slots, see bmf_gpu_bindset_desc
    BMF_GPU_MAX_BIND_TEXTURES = 8,
    BMF_GPU_MAX_BIND_SAMPLERS = 4,
};

// ---------------------------------------------------------------- enums
typedef enum bmf_gpu_backend {
    BMF_GPU_BACKEND_NULL = 0,    // D-38: accepts and validates every call, renders nothing
    BMF_GPU_BACKEND_D3D11 = 1,   // Windows (task 1A-14)
    BMF_GPU_BACKEND_WEBGPU = 2,  // web (task 1B-42)
} bmf_gpu_backend;

typedef enum bmf_gpu_format {
    BMF_GPU_FORMAT_NONE = 0,
    BMF_GPU_FORMAT_RGBA8_UNORM = 1,
    BMF_GPU_FORMAT_RGBA8_UNORM_SRGB = 2,   // sampling decodes sRGB to linear, rendering encodes
    BMF_GPU_FORMAT_BGRA8_UNORM = 3,
    BMF_GPU_FORMAT_R8_UNORM = 4,
    BMF_GPU_FORMAT_DEPTH24_STENCIL8 = 5,   // depth attachments only (not sampleable, not readable)
    BMF_GPU_FORMAT_COUNT = 6,
} bmf_gpu_format;

// Buffer usage bits. UNIFORM cannot be combined with other bits (D3D11 rule).
enum {
    BMF_GPU_BUFFER_VERTEX = 1u << 0,    // vertex or instance stream (the step mode is in the pipeline)
    BMF_GPU_BUFFER_UNIFORM = 1u << 1,   // HLSL cbuffer / WGSL var<uniform>; size a multiple of 16
};

// Texture usage bits. Depth formats allow RENDER_TARGET only.
enum {
    BMF_GPU_TEXTURE_SAMPLED = 1u << 0,        // bindable in a bind set
    BMF_GPU_TEXTURE_RENDER_TARGET = 1u << 1,  // color or depth attachment of a pass
    BMF_GPU_TEXTURE_COPY_SRC = 1u << 2,       // readable with bmf_gpu_texture_read_begin
};

typedef enum bmf_gpu_filter { BMF_GPU_FILTER_NEAREST = 0, BMF_GPU_FILTER_LINEAR = 1 } bmf_gpu_filter;
typedef enum bmf_gpu_address { BMF_GPU_ADDRESS_CLAMP = 0, BMF_GPU_ADDRESS_REPEAT = 1 } bmf_gpu_address;

typedef enum bmf_gpu_vertex_format {
    BMF_GPU_VERTEX_F32 = 1,
    BMF_GPU_VERTEX_F32X2 = 2,
    BMF_GPU_VERTEX_F32X3 = 3,
    BMF_GPU_VERTEX_F32X4 = 4,
    BMF_GPU_VERTEX_U32 = 5,
    BMF_GPU_VERTEX_U8X4_UNORM = 6,   // 4 bytes, shader sees float4 in [0, 1]
} bmf_gpu_vertex_format;

typedef enum bmf_gpu_step { BMF_GPU_STEP_VERTEX = 0, BMF_GPU_STEP_INSTANCE = 1 } bmf_gpu_step;

typedef enum bmf_gpu_topology {
    BMF_GPU_TOPOLOGY_TRIANGLE_LIST = 0,
    BMF_GPU_TOPOLOGY_TRIANGLE_STRIP = 1,
    BMF_GPU_TOPOLOGY_LINE_LIST = 2,
} bmf_gpu_topology;

// Blend presets. Colors are premultiplied by alpha everywhere in bmf (SPEC §12.2).
typedef enum bmf_gpu_blend {
    BMF_GPU_BLEND_OPAQUE = 0,          // dst = src
    BMF_GPU_BLEND_PREMULTIPLIED = 1,   // dst = src + dst * (1 - src.a), alpha the same
    BMF_GPU_BLEND_ADDITIVE = 2,        // dst = src + dst, alpha the same
} bmf_gpu_blend;

// Front faces are counter-clockwise in normalized device coordinates (x right, y up).
typedef enum bmf_gpu_cull { BMF_GPU_CULL_NONE = 0, BMF_GPU_CULL_BACK = 1, BMF_GPU_CULL_FRONT = 2 } bmf_gpu_cull;

typedef enum bmf_gpu_load {
    BMF_GPU_LOAD_CLEAR = 0,   // the attachment starts as the clear value
    BMF_GPU_LOAD_KEEP = 1,    // the attachment keeps its previous contents
} bmf_gpu_load;

// ---------------------------------------------------------------- init and surface
typedef struct bmf_gpu_desc {
    bmf_gpu_backend backend;
    void* window;              // native window to present to (HWND on Win32); 0 = no surface (headless)
    i32 surface_width;         // pixels; > 0 when window != 0
    i32 surface_height;
    b32 software;              // D3D11: force the WARP software device (headless goldens, SPEC §9.1)
    b32 vsync;
    b32 debug_layer;           // backend validation messages (D3D11 debug layer)
} bmf_gpu_desc;

// Starts the GPU layer with one backend. Must not be running already.
// Returns BMF_OK, or BMF_ERR_UNSUPPORTED when the backend is not available on this platform or
// device (then nothing is started; the caller may retry with BMF_GPU_BACKEND_NULL).
// The null backend always starts. Cost: backend device creation (D3D11 WARP: about 5 ms).
bmf_err bmf_gpu_init(const bmf_gpu_desc* desc);

// Destroys every live resource and stops the GPU layer. Outside a frame. Init may follow again.
// O(live resources).
void bmf_gpu_shutdown(void);

// The running backend. O(1).
bmf_gpu_backend bmf_gpu_backend_active(void);

// The texture that stands for the window's back buffer: a RENDER_TARGET of format
// bmf_gpu_surface_format() and the current surface size. {0} when started without a window.
// It cannot be destroyed, sampled or read. O(1).
bmf_gpu_texture bmf_gpu_surface(void);

// The color format that pipelines rendering to the surface must declare. Backend-chosen
// (BGRA8_UNORM on D3D11 and the null backend; the browser's preferred canvas format on WebGPU).
// Also valid without a window. O(1).
bmf_gpu_format bmf_gpu_surface_format(void);

// Resizes the surface (after a window resize event), in pixels, both > 0. Needs a surface;
// outside a frame. O(1) plus the backend's swapchain resize.
void bmf_gpu_surface_resize(i32 width, i32 height);

// ---------------------------------------------------------------- resources
// Every create panics when its pool is full (a fixed budget, BMF_GPU_MAX_*) or the device fails
// to create the object (out of GPU memory); invalid descriptors are asserts. `label` names the
// object in backend debug output; it must outlive the object (use string literals).
// Destroy: the handle must be live; outside a frame. Bind sets that refer to a destroyed
// resource stay alive but must not be drawn with.

typedef struct bmf_gpu_buffer_desc {
    u32 usage;           // BMF_GPU_BUFFER_* bits
    i32 size;            // bytes, a multiple of 4 (of 16 for UNIFORM), 1..BMF_GPU_MAX_BUFFER_SIZE
                         // (..BMF_GPU_MAX_UNIFORM_BUFFER_SIZE for UNIFORM)
    b32 dynamic;         // rewritten every frame (D3D11: a mappable buffer). Contents outside the
                         // ranges written in the current frame are undefined.
    const void* data;    // initial contents (`size` bytes), or 0 for zeros
    const char* label;
} bmf_gpu_buffer_desc;

// Creates a buffer. O(size) to upload `data`.
bmf_gpu_buffer bmf_gpu_buffer_create(const bmf_gpu_buffer_desc* desc);
void bmf_gpu_buffer_destroy(bmf_gpu_buffer buffer);

// Writes `size` bytes at `offset` (both multiples of 4, inside the buffer). Allowed anywhere,
// including inside a pass. Rule (WebGPU queue-write semantics): between two end_frame calls,
// the writes to one buffer go to increasing, non-overlapping ranges, so every draw of the frame
// sees the data written for it. Ring-style appending satisfies this. O(size).
void bmf_gpu_buffer_write(bmf_gpu_buffer buffer, i32 offset, const void* data, i32 size);

typedef struct bmf_gpu_texture_desc {
    bmf_gpu_format format;
    u32 usage;           // BMF_GPU_TEXTURE_* bits, at least one
    i32 width;           // pixels, 1..BMF_GPU_MAX_TEXTURE_SIZE
    i32 height;
    const void* data;    // initial pixels, rows top to bottom, tightly packed; 0 = undefined
                         // contents (render targets). Not allowed for depth formats.
    const char* label;
} bmf_gpu_texture_desc;

// Creates a 2D texture with one mip level. O(width * height) to upload `data`.
bmf_gpu_texture bmf_gpu_texture_create(const bmf_gpu_texture_desc* desc);
void bmf_gpu_texture_destroy(bmf_gpu_texture texture);

// Size in pixels of a live texture (the surface: its current size). O(1).
void bmf_gpu_texture_size(bmf_gpu_texture texture, i32* out_width, i32* out_height);

typedef struct bmf_gpu_sampler_desc {
    bmf_gpu_filter filter;    // minification and magnification (textures have no mips)
    bmf_gpu_address address;  // both axes
    const char* label;
} bmf_gpu_sampler_desc;

bmf_gpu_sampler bmf_gpu_sampler_create(const bmf_gpu_sampler_desc* desc);
void bmf_gpu_sampler_destroy(bmf_gpu_sampler sampler);

// Vertex input. Shader contract: the attribute with `location` L is HLSL semantic `ATTR<L>` and
// WGSL `@location(L)`. Offsets are multiples of 4.
typedef struct bmf_gpu_vertex_attribute {
    bmf_gpu_vertex_format format;
    i32 offset;      // bytes from the start of the element
    i32 location;    // 0..BMF_GPU_MAX_VERTEX_LOCATIONS-1, unique within the pipeline
} bmf_gpu_vertex_attribute;

typedef struct bmf_gpu_vertex_layout {
    i32 stride;      // bytes per element, a multiple of 4, 4..BMF_GPU_MAX_VERTEX_STRIDE
    bmf_gpu_step step;
    i32 attribute_count;   // 1..BMF_GPU_MAX_VERTEX_ATTRIBUTES
    bmf_gpu_vertex_attribute attributes[BMF_GPU_MAX_VERTEX_ATTRIBUTES];
} bmf_gpu_vertex_layout;

typedef struct bmf_gpu_pipeline_desc {
    i32 shader;                  // shader id (generated enum, task 1A-15)
    i32 vertex_buffer_count;     // 0..BMF_GPU_MAX_VERTEX_BUFFERS (0: the shader uses only the vertex index)
    bmf_gpu_vertex_layout vertex_buffers[BMF_GPU_MAX_VERTEX_BUFFERS];
    bmf_gpu_topology topology;
    bmf_gpu_blend blend;
    bmf_gpu_cull cull;
    bmf_gpu_format color_format; // the pass color target format this pipeline renders to (required)
    bmf_gpu_format depth_format; // NONE, or DEPTH24_STENCIL8 (then passes must have a depth target)
    b32 depth_test;              // compare less-or-equal; needs depth_format
    b32 depth_write;             // needs depth_format
    const char* label;
} bmf_gpu_pipeline_desc;

// Creates a pipeline: every state object is created here, none at draw time. O(1) plus
// backend shader object creation.
bmf_gpu_pipeline bmf_gpu_pipeline_create(const bmf_gpu_pipeline_desc* desc);
void bmf_gpu_pipeline_destroy(bmf_gpu_pipeline pipeline);

// Shader resources for draws. Slot i of each array is register b<i>/t<i>/s<i> in HLSL (visible
// to the vertex and pixel stage) and in WGSL @group(0) @binding(i) for uniforms,
// @binding(8 + i) for textures and @binding(16 + i) for samplers. Unused slots are {0}. A bind set is made for one shader and
// can be used with every pipeline of that shader; it must fill exactly the slots the shader
// declares (checked by backends against generated shader metadata).
typedef struct bmf_gpu_uniform_binding {
    bmf_gpu_buffer buffer;   // UNIFORM usage
    i32 offset;              // a multiple of BMF_GPU_UNIFORM_OFFSET_ALIGNMENT
    i32 size;                // a multiple of 16, > 0, offset + size within the buffer
} bmf_gpu_uniform_binding;

typedef struct bmf_gpu_bindset_desc {
    i32 shader;
    bmf_gpu_uniform_binding uniforms[BMF_GPU_MAX_BIND_UNIFORMS];
    bmf_gpu_texture textures[BMF_GPU_MAX_BIND_TEXTURES];   // SAMPLED usage; not the surface
    bmf_gpu_sampler samplers[BMF_GPU_MAX_BIND_SAMPLERS];
    const char* label;
} bmf_gpu_bindset_desc;

// Creates a bind set. Referenced resources must be live now and when drawing. O(1).
bmf_gpu_bindset bmf_gpu_bindset_create(const bmf_gpu_bindset_desc* desc);
void bmf_gpu_bindset_destroy(bmf_gpu_bindset bindset);

// ---------------------------------------------------------------- frame
// Starts a frame (D3D11: waits on the swapchain's frame-latency object). Not inside a frame.
// Returns 1 when the surface will be presented at end_frame, 0 when it cannot be this frame
// (window minimized or zero-sized): passes to the surface are then validated and dropped.
b32 bmf_gpu_begin_frame(void);

// Submits the frame's work and presents the surface if a pass rendered to it. Inside a frame,
// outside a pass. Ends the buffer-write ordering period (bmf_gpu_buffer_write).
void bmf_gpu_end_frame(void);

typedef struct bmf_gpu_pass_desc {
    bmf_gpu_texture color;       // RENDER_TARGET color texture, or bmf_gpu_surface(); required
    bmf_gpu_texture depth;       // RENDER_TARGET depth texture of the same size, or {0}
    bmf_gpu_load color_load;
    bmf_v4 clear_color;          // stored as given (premultiplied, in the target's encoding)
    bmf_gpu_load depth_load;
    f32 clear_depth;             // 0..1
    const char* label;
} bmf_gpu_pass_desc;

// Starts a render pass. Inside a frame, not inside a pass. The viewport is the whole target.
void bmf_gpu_begin_pass(const bmf_gpu_pass_desc* desc);
void bmf_gpu_end_pass(void);

typedef struct bmf_gpu_draw_desc {
    bmf_gpu_pipeline pipeline;   // color/depth formats must match the current pass
    bmf_gpu_bindset bindset;     // made for the pipeline's shader, or {0} if the shader binds nothing;
                                 // must not sample the pass's own targets
    bmf_gpu_buffer vertex_buffers[BMF_GPU_MAX_VERTEX_BUFFERS];   // VERTEX usage, one per pipeline layout
    i32 vertex_buffer_offsets[BMF_GPU_MAX_VERTEX_BUFFERS];       // bytes, multiples of 4
    i32 vertex_count;
    i32 instance_count;
    i32 first_vertex;
    i32 first_instance;
} bmf_gpu_draw_desc;

// Draws `instance_count` instances of `vertex_count` vertices. Inside a pass. Every element a
// vertex buffer is read for must lie inside it. A draw with a zero count is skipped.
// O(1); backends skip redundant state changes between consecutive draws.
void bmf_gpu_draw(const bmf_gpu_draw_desc* desc);

// ---------------------------------------------------------------- readback (screenshots)
// Starts copying a COPY_SRC texture of format RGBA8_UNORM, RGBA8_UNORM_SRGB or BGRA8_UNORM for
// reading. Outside a pass; at most one read in flight. Returns BMF_OK, or BMF_ERR_UNSUPPORTED on
// the null backend (D-38: then no read is in flight).
bmf_err bmf_gpu_texture_read_begin(bmf_gpu_texture texture);

// Finishes the read in flight: writes width * height * 4 bytes, RGBA, rows top to bottom, to
// `out` (`out_capacity` must be large enough). Returns BMF_OK (the read is over), or
// BMF_ERR_PENDING when the data is not back yet (WebGPU maps asynchronously: call again after
// the next end_frame). D3D11 returns BMF_OK at once (stalls until the GPU is done).
bmf_err bmf_gpu_texture_read_end(u8* out, i64 out_capacity);

// ---------------------------------------------------------------- stats
typedef struct bmf_gpu_stats {
    u64 frames;              // completed frames since init
    // Last completed frame:
    i32 passes;
    i32 draws;               // non-skipped bmf_gpu_draw calls
    i32 pipeline_changes;    // draws whose pipeline differs from the previous draw in the pass
    i32 bindset_changes;     // same for the bind set
    i64 instances;           // sum of instance_count
    i64 vertices;            // sum of vertex_count * instance_count
    i64 bytes_written;       // bmf_gpu_buffer_write bytes
    // Live objects now:
    i32 buffers, textures, samplers, pipelines, bindsets;
} bmf_gpu_stats;

// Counters for the `profile` command and draw-call budgets (SPEC §12.3). O(1).
bmf_gpu_stats bmf_gpu_get_stats(void);

#endif // BMF_GPU_H
