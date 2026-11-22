// draw2d.c: instance collection, sorting, batching and upload (SPEC §12.2, task 1A-20a).
// Included by bmf.c after gpu.c. See draw2d.h for the contract and README.md for the layout.
//
// Per frame: begin_frame pushes the instance and key arrays on the frame arena. Every submit
// appends one instance and its u64 sort key. A flush sorts the keys (stable LSD radix sort,
// payload = submission index), gathers the instances in sorted order into one contiguous array,
// writes it to the instance ring with one bmf_gpu_buffer_write (appending after the frame's
// earlier flushes), then issues one bmf_gpu_draw per run of equal pipeline and texture.

// A layer's current camera as the transform from camera space to target pixels: a similarity
// (uniform scale, rotation, a y flip for WORLD) applied at submit time.
typedef struct draw2d_transform {
    bmf_m3x2 to_pixels;   // camera space -> target pixels (y down)
    f32 scale;            // target pixels per camera unit (lengths, radii, thickness)
    f32 rotation;         // the camera's rotation: stored instance rotation = rotation - submitted angle
} draw2d_transform;

typedef struct draw2d_state {
    b32 running;
    i32 max_instances;
    bmf_gpu_buffer instance_ring;       // dynamic VERTEX, max_instances * 48 bytes
    bmf_gpu_buffer uniforms;            // dynamic UNIFORM, one 256-byte block per flush of a frame
    bmf_gpu_texture white;              // 1x1 opaque white: the texture of every shape
    bmf_gpu_sampler sampler;            // linear, clamp
    bmf_gpu_pipeline pipelines[BMF_GPU_FORMAT_COUNT];   // premultiplied blend, by color format; made on first use

    // Bind sets by texture slot and flush index (uniform block offset), made on first use.
    // bindset_texture_ids[slot] is the texture handle they were made for; when another handle
    // takes the slot they are retired and destroyed at the next begin_frame (not allowed inside
    // a GPU frame).
    bmf_gpu_bindset bindsets[BMF_GPU_MAX_TEXTURES][BMF_DRAW2D_MAX_FLUSHES_PER_FRAME];
    u32 bindset_texture_ids[BMF_GPU_MAX_TEXTURES];
    bmf_gpu_bindset retired[BMF_GPU_MAX_BINDSETS];
    i32 retired_count;
    u32 texture_ids[BMF_GPU_MAX_TEXTURES];   // the handle submitted for each slot this frame

    // Cameras (reset every begin_frame).
    bmf_draw2d_view view;
    draw2d_transform layer_transforms[BMF_DRAW2D_MAX_LAYERS];
    bmf_camera camera_stacks[BMF_DRAW2D_MAX_LAYERS][BMF_DRAW_CAMERA_STACK_DEPTH];
    i32 camera_depths[BMF_DRAW2D_MAX_LAYERS];
    draw2d_transform default_transform;   // the default world camera for the frame's view

    // Frame.
    bmf_arena* frame;                   // 0 outside begin_frame .. next begin_frame
    bmf_draw_instance* instances;       // [max_instances], submission order since the last flush
    u64* keys;                          // [max_instances]
    i32 count;                          // instances since the last flush
    i32 flushed;                        // instances already flushed this frame (ring offset in instances)
    i32 flush_count;
    bmf_draw2d_stats stats;
} draw2d_state;

static draw2d_state draw2d;

// ---------------------------------------------------------------- f16
// v >> shift rounded to nearest, ties to even. shift 1..31.
static inline u32 draw2d_shift_round_even(u32 v, i32 shift) {
    u32 q = v >> shift;
    u32 rem = v & ((1u << shift) - 1u);
    u32 half = 1u << (shift - 1);
    if (rem > half || (rem == half && (q & 1u))) q++;
    return q;
}

u32 bmf_draw_f16(f32 value) {
    if (!(value > 0.0f)) return 0;                 // zero, negative, NaN
    if (value >= 65504.0f) return 0x7BFFu;         // largest finite half
    u32 bits = bmf_f32_bits(value);
    i32 exponent = (i32)(bits >> 23) - 127;
    u32 mantissa = (bits & 0x7FFFFFu) | 0x800000u; // value = mantissa * 2^(exponent - 23)
    if (exponent < -14) {                          // half subnormal: k * 2^-24, k = mantissa * 2^(exponent + 1)
        i32 shift = -(exponent + 1);
        if (shift > 24) return 0;                  // below half the smallest subnormal
        return draw2d_shift_round_even(mantissa, shift);   // may round up to the smallest normal (0x400): still correct
    }
    // Normal: rounding may carry into the exponent, which is the correct next value.
    return ((u32)(exponent + 15) << 10) + draw2d_shift_round_even(mantissa, 13) - 0x400u;
}

// ---------------------------------------------------------------- init / shutdown
void bmf_draw2d_init(const bmf_draw2d_desc* desc) {
    BMF_ASSERT(!draw2d.running && "bmf_draw2d_init: already running");
    BMF_ASSERT(desc->max_instances >= 0 && desc->max_instances <= BMF_GPU_MAX_BUFFER_SIZE / (i32)sizeof(bmf_draw_instance) &&
               "bmf_draw2d_init: max_instances out of range");
    bmf_memset(&draw2d, 0, sizeof draw2d);
    draw2d.running = 1;
    draw2d.max_instances = desc->max_instances ? desc->max_instances : BMF_DRAW2D_DEFAULT_MAX_INSTANCES;

    bmf_gpu_buffer_desc ring_desc = { BMF_GPU_BUFFER_VERTEX, draw2d.max_instances * (i32)sizeof(bmf_draw_instance), 1, 0, "draw2d instances" };
    draw2d.instance_ring = bmf_gpu_buffer_create(&ring_desc);
    bmf_gpu_buffer_desc uniform_desc = { BMF_GPU_BUFFER_UNIFORM, BMF_DRAW2D_MAX_FLUSHES_PER_FRAME * BMF_GPU_UNIFORM_OFFSET_ALIGNMENT, 1, 0,
                                         "draw2d uniforms" };
    draw2d.uniforms = bmf_gpu_buffer_create(&uniform_desc);
    static const u8 white_pixel[4] = { 255, 255, 255, 255 };
    bmf_gpu_texture_desc white_desc = { BMF_GPU_FORMAT_RGBA8_UNORM, BMF_GPU_TEXTURE_SAMPLED, 1, 1, white_pixel, "draw2d white" };
    draw2d.white = bmf_gpu_texture_create(&white_desc);
    bmf_gpu_sampler_desc sampler_desc = { BMF_GPU_FILTER_LINEAR, BMF_GPU_ADDRESS_CLAMP, "draw2d linear clamp" };
    draw2d.sampler = bmf_gpu_sampler_create(&sampler_desc);
}

static void draw2d_destroy_retired(void) {
    for (i32 i = 0; i < draw2d.retired_count; i++) bmf_gpu_bindset_destroy(draw2d.retired[i]);
    draw2d.retired_count = 0;
}

void bmf_draw2d_shutdown(void) {
    BMF_ASSERT(draw2d.running && "bmf_draw2d_shutdown: not running");
    draw2d_destroy_retired();
    for (i32 slot = 0; slot < BMF_GPU_MAX_TEXTURES; slot++)
        for (i32 f = 0; f < BMF_DRAW2D_MAX_FLUSHES_PER_FRAME; f++)
            if (draw2d.bindsets[slot][f].id) bmf_gpu_bindset_destroy(draw2d.bindsets[slot][f]);
    for (i32 format = 0; format < BMF_GPU_FORMAT_COUNT; format++)
        if (draw2d.pipelines[format].id) bmf_gpu_pipeline_destroy(draw2d.pipelines[format]);
    bmf_gpu_sampler_destroy(draw2d.sampler);
    bmf_gpu_texture_destroy(draw2d.white);
    bmf_gpu_buffer_destroy(draw2d.uniforms);
    bmf_gpu_buffer_destroy(draw2d.instance_ring);
    bmf_memset(&draw2d, 0, sizeof draw2d);
}

// ---------------------------------------------------------------- cameras
bmf_camera bmf_camera_world_default(void) {
    bmf_camera camera = { BMF_CAMERA_WORLD, { 0.0f, 0.0f }, BMF_DRAW_WORLD_PIXELS_PER_METER, 0.0f };
    return camera;
}

bmf_camera bmf_camera_screen_default(void) {
    bmf_camera camera = { BMF_CAMERA_SCREEN, { 0.0f, 0.0f }, 1.0f, 0.0f };
    return camera;
}

// Camera space -> screen logical pixels for a view of `view_size` logical pixels.
//   WORLD:  logical = view_size / 2 + k * flip_y(R(-rotation) * (p - position))
//   SCREEN: logical = k * R_y_down(rotation) * (p - position)
// k = pixels_per_unit. Both turn the content clockwise on screen by `rotation`. The columns are
// the images of the unit axes; the origin follows from mapping `position` to the anchor.
static bmf_m3x2 draw2d_camera_to_logical(const bmf_camera* camera, bmf_v2 view_size) {
    BMF_ASSERT(camera->pixels_per_unit > 0.0f && "bmf_camera: pixels_per_unit must be > 0");
    f32 s, c;
    bmf_sincosf(camera->rotation, &s, &c);
    f32 k = camera->pixels_per_unit;
    bmf_m3x2 m;
    bmf_v2 anchor;
    if (camera->space == BMF_CAMERA_WORLD) {
        m.x_axis = bmf_v2_make(k * c, k * s);
        m.y_axis = bmf_v2_make(k * s, -k * c);
        anchor = bmf_v2_scale(view_size, 0.5f);
    } else {
        m.x_axis = bmf_v2_make(k * c, k * s);
        m.y_axis = bmf_v2_make(-k * s, k * c);
        anchor = bmf_v2_make(0.0f, 0.0f);
    }
    m.origin = bmf_v2_sub(anchor, bmf_m3x2_apply_vector(m, camera->position));
    return m;
}

bmf_v2 bmf_camera_world_to_screen(const bmf_camera* camera, bmf_v2 view_size, bmf_v2 point) {
    return bmf_m3x2_apply_point(draw2d_camera_to_logical(camera, view_size), point);
}

bmf_v2 bmf_camera_screen_to_world(const bmf_camera* camera, bmf_v2 view_size, bmf_v2 screen) {
    return bmf_m3x2_apply_point(bmf_m3x2_inverse(draw2d_camera_to_logical(camera, view_size)), screen);
}

// The camera's transform to target pixels for the frame's view (logical times dpi_scale).
static draw2d_transform draw2d_camera_transform(const bmf_camera* camera) {
    f32 dpi = draw2d.view.dpi_scale;
    bmf_v2 view_size = bmf_v2_make((f32)draw2d.view.width / dpi, (f32)draw2d.view.height / dpi);
    bmf_m3x2 logical = draw2d_camera_to_logical(camera, view_size);
    draw2d_transform t;
    t.to_pixels.x_axis = bmf_v2_scale(logical.x_axis, dpi);
    t.to_pixels.y_axis = bmf_v2_scale(logical.y_axis, dpi);
    t.to_pixels.origin = bmf_v2_scale(logical.origin, dpi);
    t.scale = camera->pixels_per_unit * dpi;
    t.rotation = camera->rotation;
    return t;
}

void bmf_draw_camera_push(i32 layer, const bmf_camera* camera) {
    BMF_ASSERT(draw2d.frame && "bmf_draw_camera_push: outside a frame (bmf_draw2d_begin_frame)");
    BMF_ASSERT(layer >= 0 && layer < BMF_DRAW2D_MAX_LAYERS && "bmf_draw_camera_push: layer must be 0..255");
    BMF_ASSERT(draw2d.camera_depths[layer] < BMF_DRAW_CAMERA_STACK_DEPTH && "bmf_draw_camera_push: stack full (BMF_DRAW_CAMERA_STACK_DEPTH)");
    BMF_ASSERT((camera->space == BMF_CAMERA_WORLD || camera->space == BMF_CAMERA_SCREEN) && "bmf_draw_camera_push: unknown camera space");
    draw2d.camera_stacks[layer][draw2d.camera_depths[layer]++] = *camera;
    draw2d.layer_transforms[layer] = draw2d_camera_transform(camera);
}

void bmf_draw_camera_pop(i32 layer) {
    BMF_ASSERT(draw2d.frame && "bmf_draw_camera_pop: outside a frame (bmf_draw2d_begin_frame)");
    BMF_ASSERT(layer >= 0 && layer < BMF_DRAW2D_MAX_LAYERS && "bmf_draw_camera_pop: layer must be 0..255");
    BMF_ASSERT(draw2d.camera_depths[layer] > 0 && "bmf_draw_camera_pop: the layer's camera stack is empty");
    i32 depth = --draw2d.camera_depths[layer];
    draw2d.layer_transforms[layer] = depth > 0 ? draw2d_camera_transform(&draw2d.camera_stacks[layer][depth - 1]) : draw2d.default_transform;
}

// ---------------------------------------------------------------- frame
void bmf_draw2d_begin_frame(bmf_arena* frame, bmf_draw2d_view view) {
    BMF_ASSERT(draw2d.running && "bmf_draw2d_begin_frame: not running");
    BMF_ASSERT(view.width > 0 && view.height > 0 && view.dpi_scale > 0.0f && "bmf_draw2d_begin_frame: view size and dpi_scale must be > 0");
    draw2d_destroy_retired();
    draw2d.view = view;
    bmf_camera world = bmf_camera_world_default();
    draw2d.default_transform = draw2d_camera_transform(&world);
    for (i32 layer = 0; layer < BMF_DRAW2D_MAX_LAYERS; layer++) {
        draw2d.layer_transforms[layer] = draw2d.default_transform;
        draw2d.camera_depths[layer] = 0;
    }
    draw2d.frame = frame;
    draw2d.instances = (bmf_draw_instance*)bmf_arena_push_aligned(frame, (i64)draw2d.max_instances * (i64)sizeof(bmf_draw_instance), 64);
    draw2d.keys = (u64*)bmf_arena_push_aligned(frame, (i64)draw2d.max_instances * 8, 64);
    draw2d.count = 0;
    draw2d.flushed = 0;
    draw2d.flush_count = 0;
    draw2d.stats.frame_instances = 0;
    draw2d.stats.dropped = 0;
    bmf_memset(draw2d.texture_ids, 0, sizeof draw2d.texture_ids);
}

bmf_draw2d_stats bmf_draw2d_get_stats(void) { return draw2d.stats; }

// The pipeline for a color target format: the draw2d shader, one per-instance stream of
// bmf_draw_instance, a 4-vertex triangle strip, premultiplied blending, no depth.
static bmf_gpu_pipeline draw2d_pipeline(bmf_gpu_format color_format) {
    if (draw2d.pipelines[color_format].id) return draw2d.pipelines[color_format];
    bmf_gpu_pipeline_desc desc;
    bmf_memset(&desc, 0, sizeof desc);
    desc.shader = BMF_SHADER_DRAW2D;
    desc.vertex_buffer_count = 1;
    bmf_gpu_vertex_layout* layout = &desc.vertex_buffers[0];
    layout->stride = (i32)sizeof(bmf_draw_instance);
    layout->step = BMF_GPU_STEP_INSTANCE;
    layout->attribute_count = 7;
    layout->attributes[0] = (bmf_gpu_vertex_attribute){ BMF_GPU_VERTEX_F32X2, 0, 0 };        // position
    layout->attributes[1] = (bmf_gpu_vertex_attribute){ BMF_GPU_VERTEX_F32X2, 8, 1 };        // size
    layout->attributes[2] = (bmf_gpu_vertex_attribute){ BMF_GPU_VERTEX_F32, 16, 2 };         // rotation
    layout->attributes[3] = (bmf_gpu_vertex_attribute){ BMF_GPU_VERTEX_U8X4_UNORM, 20, 3 };  // color (A,B,G,R bytes)
    layout->attributes[4] = (bmf_gpu_vertex_attribute){ BMF_GPU_VERTEX_F32X4, 24, 4 };       // extra
    layout->attributes[5] = (bmf_gpu_vertex_attribute){ BMF_GPU_VERTEX_U32, 40, 5 };         // kind_corner
    layout->attributes[6] = (bmf_gpu_vertex_attribute){ BMF_GPU_VERTEX_U32, 44, 6 };         // stroke_softness
    desc.topology = BMF_GPU_TOPOLOGY_TRIANGLE_STRIP;
    desc.blend = BMF_GPU_BLEND_PREMULTIPLIED;
    desc.cull = BMF_GPU_CULL_NONE;
    desc.color_format = color_format;
    desc.depth_format = BMF_GPU_FORMAT_NONE;
    desc.label = "draw2d";
    draw2d.pipelines[color_format] = bmf_gpu_pipeline_create(&desc);
    return draw2d.pipelines[color_format];
}

// The bind set for a texture slot (with the handle submitted this frame) and flush index:
// b0 = the flush's uniform block, t0 = the texture, s0 = the sampler.
static bmf_gpu_bindset draw2d_bindset(i32 slot, i32 flush) {
    u32 texture_id = draw2d.texture_ids[slot];
    if (draw2d.bindset_texture_ids[slot] != texture_id) {
        for (i32 f = 0; f < BMF_DRAW2D_MAX_FLUSHES_PER_FRAME; f++) {
            if (!draw2d.bindsets[slot][f].id) continue;
            if (draw2d.retired_count == BMF_GPU_MAX_BINDSETS) bmf_panic("bmf_draw2d_flush: too many retired bind sets");
            draw2d.retired[draw2d.retired_count++] = draw2d.bindsets[slot][f];
            draw2d.bindsets[slot][f].id = 0;
        }
        draw2d.bindset_texture_ids[slot] = texture_id;
    }
    if (draw2d.bindsets[slot][flush].id) return draw2d.bindsets[slot][flush];
    bmf_gpu_bindset_desc desc;
    bmf_memset(&desc, 0, sizeof desc);
    desc.shader = BMF_SHADER_DRAW2D;
    desc.uniforms[0] = (bmf_gpu_uniform_binding){ draw2d.uniforms, flush * BMF_GPU_UNIFORM_OFFSET_ALIGNMENT, 16 };
    desc.textures[0].id = texture_id;
    desc.samplers[0] = draw2d.sampler;
    desc.label = "draw2d";
    draw2d.bindsets[slot][flush] = bmf_gpu_bindset_create(&desc);
    return draw2d.bindsets[slot][flush];
}

void bmf_draw2d_flush(i32 target_width, i32 target_height, bmf_gpu_format color_format) {
    BMF_ASSERT(draw2d.frame && "bmf_draw2d_flush: outside a frame (call bmf_draw2d_begin_frame first)");
    BMF_ASSERT(target_width > 0 && target_height > 0 && "bmf_draw2d_flush: target size must be > 0");
    BMF_ASSERT(color_format > BMF_GPU_FORMAT_NONE && color_format < BMF_GPU_FORMAT_DEPTH24_STENCIL8 &&
               "bmf_draw2d_flush: color_format must be a color format");
    i32 n = draw2d.count;
    draw2d.stats.instances = 0;
    draw2d.stats.batches = 0;
    if (n == 0) return;
    BMF_ASSERT(draw2d.flush_count < BMF_DRAW2D_MAX_FLUSHES_PER_FRAME && "bmf_draw2d_flush: more than BMF_DRAW2D_MAX_FLUSHES_PER_FRAME flushes in a frame");
    i32 flush = draw2d.flush_count++;

    // Uniform block: target size and its reciprocal (pixels to clip space in the vertex shader).
    f32 block[4] = { (f32)target_width, (f32)target_height, 1.0f / (f32)target_width, 1.0f / (f32)target_height };
    bmf_gpu_buffer_write(draw2d.uniforms, flush * BMF_GPU_UNIFORM_OFFSET_ALIGNMENT, block, (i32)sizeof block);

    // Sort (stable: equal keys keep submission order), then gather into one contiguous upload.
    bmf_arena* frame = draw2d.frame;
    i64 mark = bmf_arena_mark(frame);
    u64* keys = draw2d.keys;
    u64* keys_tmp = (u64*)bmf_arena_push_aligned(frame, (i64)n * 8, 64);
    u32* order = (u32*)bmf_arena_push_aligned(frame, (i64)n * 4, 64);
    u32* order_tmp = (u32*)bmf_arena_push_aligned(frame, (i64)n * 4, 64);
    bmf_draw_instance* sorted = (bmf_draw_instance*)bmf_arena_push_aligned(frame, (i64)n * (i64)sizeof(bmf_draw_instance), 64);
    for (i32 i = 0; i < n; i++) order[i] = (u32)i;
    bmf_sort_u64(keys, order, n, keys_tmp, order_tmp);
    const bmf_draw_instance* instances = draw2d.instances;
    for (i32 i = 0; i < n; i++) sorted[i] = instances[order[i]];
    i32 ring_offset = draw2d.flushed * (i32)sizeof(bmf_draw_instance);
    bmf_gpu_buffer_write(draw2d.instance_ring, ring_offset, sorted, n * (i32)sizeof(bmf_draw_instance));

    // One draw per run of equal pipeline and texture (key bits 40..55); layer changes alone do
    // not split a run.
    bmf_gpu_draw_desc draw;
    bmf_memset(&draw, 0, sizeof draw);
    draw.pipeline = draw2d_pipeline(color_format);
    draw.vertex_buffers[0] = draw2d.instance_ring;
    draw.vertex_buffer_offsets[0] = ring_offset;
    draw.vertex_count = 4;
    i32 start = 0;
    for (i32 i = 1; i <= n; i++) {
        u32 group = (u32)(keys[start] >> BMF_DRAW_KEY_TEXTURE_SHIFT) & 0xFFFFu;
        if (i < n && ((u32)(keys[i] >> BMF_DRAW_KEY_TEXTURE_SHIFT) & 0xFFFFu) == group) continue;
        draw.bindset = draw2d_bindset((i32)(group & 0xFFFu), flush);
        draw.first_instance = start;
        draw.instance_count = i - start;
        bmf_gpu_draw(&draw);
        draw2d.stats.batches++;
        start = i;
    }
    bmf_arena_pop_to(frame, mark);

    draw2d.stats.instances = n;
    draw2d.flushed += n;
    draw2d.count = 0;
}

// ---------------------------------------------------------------- submit
// Appends one instance with its sort key (pipeline 0) and returns it, or 0 when the frame's
// budget is full (counted in stats.dropped). The caller fills every field.
static inline bmf_draw_instance* draw2d_push(i32 layer, u32 texture_id) {
    BMF_ASSERT(draw2d.frame && "bmf_draw_*: outside a frame (bmf_draw2d_begin_frame)");
    BMF_ASSERT(layer >= 0 && layer < BMF_DRAW2D_MAX_LAYERS && "bmf_draw_*: layer must be 0..255");
    if (draw2d.flushed + draw2d.count >= draw2d.max_instances) {
        draw2d.stats.dropped++;
        return 0;
    }
    i32 index = draw2d.count++;
    draw2d.stats.frame_instances++;
    u32 slot = texture_id >> BMF_HANDLE_GENERATION_BITS;
    draw2d.texture_ids[slot] = texture_id;
    draw2d.keys[index] = (u64)(u32)layer << BMF_DRAW_KEY_LAYER_SHIFT | (u64)slot << BMF_DRAW_KEY_TEXTURE_SHIFT;
    return &draw2d.instances[index];
}

static inline u32 draw2d_kind_corner(bmf_draw_kind kind, f32 corner_radius) { return (u32)kind | bmf_draw_f16(corner_radius) << 16; }

// Every submit maps camera space to target pixels with the layer's current transform: points
// through the matrix, lengths times the scale, angles as (camera rotation - angle), the
// instance's y-down pixel-space angle.
void bmf_draw_rect(i32 layer, bmf_v2 center, bmf_v2 size, f32 rotation, bmf_color color) {
    bmf_draw_instance* d = draw2d_push(layer, draw2d.white.id);
    if (!d) return;
    const draw2d_transform* t = &draw2d.layer_transforms[layer];
    d->position = bmf_m3x2_apply_point(t->to_pixels, center);
    d->size = bmf_v2_scale(size, t->scale);
    d->rotation = t->rotation - rotation;
    d->color = color;
    d->extra = (bmf_v4){ 0.0f, 0.0f, 0.0f, 0.0f };
    d->kind_corner = BMF_DRAW_KIND_RECT;
    d->stroke_softness = 0;
}

void bmf_draw_rect_rounded(i32 layer, bmf_v2 center, bmf_v2 size, f32 rotation, f32 corner_radius, bmf_color color) {
    bmf_draw_instance* d = draw2d_push(layer, draw2d.white.id);
    if (!d) return;
    const draw2d_transform* t = &draw2d.layer_transforms[layer];
    d->position = bmf_m3x2_apply_point(t->to_pixels, center);
    d->size = bmf_v2_scale(size, t->scale);
    d->rotation = t->rotation - rotation;
    d->color = color;
    d->extra = (bmf_v4){ 0.0f, 0.0f, 0.0f, 0.0f };
    d->kind_corner = draw2d_kind_corner(BMF_DRAW_KIND_RECT, corner_radius * t->scale);
    d->stroke_softness = 0;
}

void bmf_draw_circle(i32 layer, bmf_v2 center, f32 radius, bmf_color color) {
    bmf_draw_instance* d = draw2d_push(layer, draw2d.white.id);
    if (!d) return;
    const draw2d_transform* t = &draw2d.layer_transforms[layer];
    f32 diameter = radius * 2.0f * t->scale;
    d->position = bmf_m3x2_apply_point(t->to_pixels, center);
    d->size = (bmf_v2){ diameter, diameter };
    d->rotation = 0.0f;
    d->color = color;
    d->extra = (bmf_v4){ 0.0f, 0.0f, 0.0f, 0.0f };
    d->kind_corner = BMF_DRAW_KIND_CIRCLE;
    d->stroke_softness = 0;
}

void bmf_draw_ring(i32 layer, bmf_v2 center, f32 radius, f32 thickness, bmf_color color) {
    bmf_draw_instance* d = draw2d_push(layer, draw2d.white.id);
    if (!d) return;
    const draw2d_transform* t = &draw2d.layer_transforms[layer];
    f32 diameter = radius * 2.0f * t->scale;
    d->position = bmf_m3x2_apply_point(t->to_pixels, center);
    d->size = (bmf_v2){ diameter, diameter };
    d->rotation = 0.0f;
    d->color = color;
    d->extra = (bmf_v4){ 0.0f, 0.0f, 0.0f, 0.0f };
    d->kind_corner = BMF_DRAW_KIND_CIRCLE;
    d->stroke_softness = bmf_draw_f16(thickness * t->scale);
}

void bmf_draw_line(i32 layer, bmf_v2 a, bmf_v2 b, f32 thickness, bmf_color color) {
    bmf_draw_instance* d = draw2d_push(layer, draw2d.white.id);
    if (!d) return;
    const draw2d_transform* t = &draw2d.layer_transforms[layer];
    bmf_v2 pixel_b = bmf_m3x2_apply_point(t->to_pixels, b);
    d->position = bmf_m3x2_apply_point(t->to_pixels, a);
    d->size = (bmf_v2){ 0.0f, thickness * t->scale };
    d->rotation = 0.0f;
    d->color = color;
    d->extra = (bmf_v4){ pixel_b.x, pixel_b.y, 0.0f, 0.0f };
    d->kind_corner = BMF_DRAW_KIND_CAPSULE;
    d->stroke_softness = 0;
}

void bmf_draw_triangle(i32 layer, bmf_v2 a, bmf_v2 b, bmf_v2 c, bmf_color color) {
    bmf_draw_instance* d = draw2d_push(layer, draw2d.white.id);
    if (!d) return;
    const draw2d_transform* t = &draw2d.layer_transforms[layer];
    bmf_v2 pixel_b = bmf_m3x2_apply_point(t->to_pixels, b);
    bmf_v2 pixel_c = bmf_m3x2_apply_point(t->to_pixels, c);
    d->position = bmf_m3x2_apply_point(t->to_pixels, a);
    d->size = (bmf_v2){ 0.0f, 0.0f };
    d->rotation = 0.0f;
    d->color = color;
    d->extra = (bmf_v4){ pixel_b.x, pixel_b.y, pixel_c.x, pixel_c.y };
    d->kind_corner = BMF_DRAW_KIND_TRIANGLE;
    d->stroke_softness = 0;
}

void bmf_draw_sprite(i32 layer, bmf_gpu_texture texture, bmf_rect uv, bmf_v2 center, bmf_v2 size, f32 rotation, bmf_color tint) {
    BMF_ASSERT(texture.id != 0 && "bmf_draw_sprite: texture is {0}");
    bmf_draw_instance* d = draw2d_push(layer, texture.id);
    if (!d) return;
    const draw2d_transform* t = &draw2d.layer_transforms[layer];
    d->position = bmf_m3x2_apply_point(t->to_pixels, center);
    d->size = bmf_v2_scale(size, t->scale);
    d->rotation = t->rotation - rotation;
    d->color = tint;
    d->extra = (bmf_v4){ uv.x, uv.y, uv.x + uv.w, uv.y + uv.h };
    d->kind_corner = BMF_DRAW_KIND_SPRITE;
    d->stroke_softness = 0;
}
