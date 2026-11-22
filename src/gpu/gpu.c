// gpu.c: the GPU frontend (SPEC §12.1, D-25, D-38): handle pools, validation, frame/pass
// state, stats. With BMF_GPU_BACKEND_NULL this file is the whole backend: it validates and
// counts every call and renders nothing.
//
// Backend contract (tasks 1A-14 D3D11, 1B-42 WebGPU): each backend keeps its native objects in
// arrays indexed by the pool slot index of the frontend handle. Every public function below
// validates first, then updates frontend state, then (where marked `backend:`) calls the active
// backend with slot indices and already-validated descriptors. A backend never re-validates and
// never sees an invalid handle. Backend files are included before this one (bmf.c) and define
// static gpu_<backend>_* functions; each call site names the backend explicitly.
//
// Validation runs in debug builds only (BMF_ASSERT); each assert's expression carries a message
// naming the function and the rule. Release builds keep only the bookkeeping.

typedef struct gpu_buffer_info {
    u32 usage;
    i32 size;
    b32 dynamic;
    u64 write_period;   // gpu_frontend.write_period of the last write
    i32 write_end;      // end of the last write's range in that period
    const char* label;
} gpu_buffer_info;

typedef struct gpu_texture_info {
    bmf_gpu_format format;
    u32 usage;
    i32 width;
    i32 height;
    b32 is_surface;
    const char* label;
} gpu_texture_info;

static struct {
    b32 running;
    bmf_gpu_desc desc;
    bmf_gpu_texture surface;
    bmf_gpu_format surface_format;

    b32 in_frame;
    b32 in_pass;
    b32 frame_surface_usable;      // begin_frame result: passes to the surface reach the backend
    b32 frame_surface_rendered;    // a pass rendered to the surface this frame: present at end_frame
    u64 write_period;              // counts end_frame calls; orders bmf_gpu_buffer_write ranges

    // Current pass.
    bmf_gpu_texture pass_color;
    bmf_gpu_texture pass_depth;
    bmf_gpu_format pass_color_format;
    bmf_gpu_format pass_depth_format;
    bmf_gpu_pipeline pass_last_pipeline;   // {0} at pass start
    bmf_gpu_bindset pass_last_bindset;
    b32 pass_first_draw;
    b32 pass_dropped;              // a surface pass while the surface is unusable: validate only

    b32 read_in_flight;
    bmf_gpu_texture read_texture;

    bmf_gpu_stats frame;           // counters of the frame in progress
    bmf_gpu_stats last;            // counters of the last completed frame

    bmf_pool buffer_pool;
    bmf_pool texture_pool;
    bmf_pool sampler_pool;
    bmf_pool pipeline_pool;
    bmf_pool bindset_pool;
    bmf_pool_slot buffer_slots[BMF_GPU_MAX_BUFFERS];
    bmf_pool_slot texture_slots[BMF_GPU_MAX_TEXTURES];
    bmf_pool_slot sampler_slots[BMF_GPU_MAX_SAMPLERS];
    bmf_pool_slot pipeline_slots[BMF_GPU_MAX_PIPELINES];
    bmf_pool_slot bindset_slots[BMF_GPU_MAX_BINDSETS];
    gpu_buffer_info buffers[BMF_GPU_MAX_BUFFERS];
    gpu_texture_info textures[BMF_GPU_MAX_TEXTURES];
    bmf_gpu_sampler_desc samplers[BMF_GPU_MAX_SAMPLERS];
    bmf_gpu_pipeline_desc pipelines[BMF_GPU_MAX_PIPELINES];
    bmf_gpu_bindset_desc bindsets[BMF_GPU_MAX_BINDSETS];
} gpu_frontend;

// ---------------------------------------------------------------- handle lookup
static inline b32 gpu_buffer_live(bmf_gpu_buffer h)     { return bmf_pool_valid(&gpu_frontend.buffer_pool, gpu_frontend.buffer_slots, h.id); }
static inline b32 gpu_texture_live(bmf_gpu_texture h)   { return bmf_pool_valid(&gpu_frontend.texture_pool, gpu_frontend.texture_slots, h.id); }
static inline b32 gpu_sampler_live(bmf_gpu_sampler h)   { return bmf_pool_valid(&gpu_frontend.sampler_pool, gpu_frontend.sampler_slots, h.id); }
static inline b32 gpu_pipeline_live(bmf_gpu_pipeline h) { return bmf_pool_valid(&gpu_frontend.pipeline_pool, gpu_frontend.pipeline_slots, h.id); }
static inline b32 gpu_bindset_live(bmf_gpu_bindset h)   { return bmf_pool_valid(&gpu_frontend.bindset_pool, gpu_frontend.bindset_slots, h.id); }

// Slot index of a live handle (callers validate liveness first, in debug).
static inline i32 gpu_handle_slot(u32 id) { return (i32)(id >> BMF_HANDLE_GENERATION_BITS); }

static inline i32 gpu_format_bytes(bmf_gpu_format f) {
    switch (f) {
    case BMF_GPU_FORMAT_RGBA8_UNORM:
    case BMF_GPU_FORMAT_RGBA8_UNORM_SRGB:
    case BMF_GPU_FORMAT_BGRA8_UNORM:
    case BMF_GPU_FORMAT_DEPTH24_STENCIL8: return 4;
    case BMF_GPU_FORMAT_R8_UNORM: return 1;
    case BMF_GPU_FORMAT_NONE:
    case BMF_GPU_FORMAT_COUNT: break;
    }
    return 0;
}

static inline b32 gpu_format_is_color(bmf_gpu_format f) {
    return f == BMF_GPU_FORMAT_RGBA8_UNORM || f == BMF_GPU_FORMAT_RGBA8_UNORM_SRGB ||
           f == BMF_GPU_FORMAT_BGRA8_UNORM || f == BMF_GPU_FORMAT_R8_UNORM;
}

static inline i32 gpu_vertex_format_bytes(bmf_gpu_vertex_format f) {
    switch (f) {
    case BMF_GPU_VERTEX_F32: return 4;
    case BMF_GPU_VERTEX_F32X2: return 8;
    case BMF_GPU_VERTEX_F32X3: return 12;
    case BMF_GPU_VERTEX_F32X4: return 16;
    case BMF_GPU_VERTEX_U32: return 4;
    case BMF_GPU_VERTEX_U8X4_UNORM: return 4;
    }
    return 0;
}

// ---------------------------------------------------------------- init and surface
bmf_err bmf_gpu_init(const bmf_gpu_desc* desc) {
    BMF_ASSERT(!gpu_frontend.running && "bmf_gpu_init: already running (call bmf_gpu_shutdown first)");
    BMF_ASSERT((u32)desc->backend <= BMF_GPU_BACKEND_WEBGPU && "bmf_gpu_init: unknown backend");
    BMF_ASSERT((!desc->window || (desc->surface_width > 0 && desc->surface_height > 0)) &&
               "bmf_gpu_init: a window needs surface_width and surface_height > 0");
#if !BMF_OS_WINDOWS
    if (desc->backend == BMF_GPU_BACKEND_D3D11) return BMF_ERR_UNSUPPORTED;
#endif
    // backend: WebGPU lands in 1B-42. Until then it is unavailable.
    if (desc->backend == BMF_GPU_BACKEND_WEBGPU) return BMF_ERR_UNSUPPORTED;

    bmf_memset(&gpu_frontend, 0, sizeof gpu_frontend);
    gpu_frontend.running = 1;
    gpu_frontend.desc = *desc;
    gpu_frontend.surface_format = BMF_GPU_FORMAT_BGRA8_UNORM;
    bmf_pool_init(&gpu_frontend.buffer_pool, gpu_frontend.buffer_slots, BMF_GPU_MAX_BUFFERS);
    bmf_pool_init(&gpu_frontend.texture_pool, gpu_frontend.texture_slots, BMF_GPU_MAX_TEXTURES);
    bmf_pool_init(&gpu_frontend.sampler_pool, gpu_frontend.sampler_slots, BMF_GPU_MAX_SAMPLERS);
    bmf_pool_init(&gpu_frontend.pipeline_pool, gpu_frontend.pipeline_slots, BMF_GPU_MAX_PIPELINES);
    bmf_pool_init(&gpu_frontend.bindset_pool, gpu_frontend.bindset_slots, BMF_GPU_MAX_BINDSETS);

    if (desc->window) {
        // The surface is an ordinary texture slot, so passes treat it like any render target.
        gpu_frontend.surface.id = bmf_pool_alloc(&gpu_frontend.texture_pool, gpu_frontend.texture_slots);
        gpu_texture_info* info = &gpu_frontend.textures[gpu_handle_slot(gpu_frontend.surface.id)];
        info->format = gpu_frontend.surface_format;
        info->usage = BMF_GPU_TEXTURE_RENDER_TARGET;
        info->width = desc->surface_width;
        info->height = desc->surface_height;
        info->is_surface = 1;
        info->label = "surface";
    }
#if BMF_OS_WINDOWS
    if (desc->backend == BMF_GPU_BACKEND_D3D11) {
        i32 surface_slot = desc->window ? gpu_handle_slot(gpu_frontend.surface.id) : -1;
        bmf_err err = gpu_d3d11_init(desc, surface_slot);
        if (err != BMF_OK) {
            bmf_memset(&gpu_frontend, 0, sizeof gpu_frontend);
            return err;
        }
    }
#endif
    return BMF_OK;
}

void bmf_gpu_shutdown(void) {
    BMF_ASSERT(gpu_frontend.running && "bmf_gpu_shutdown: not running");
    BMF_ASSERT(!gpu_frontend.in_frame && "bmf_gpu_shutdown: inside a frame (call bmf_gpu_end_frame first)");
    // backend: release every live object, then the device.
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11) gpu_d3d11_shutdown();
#endif
    bmf_memset(&gpu_frontend, 0, sizeof gpu_frontend);
}

bmf_gpu_backend bmf_gpu_backend_active(void) {
    BMF_ASSERT(gpu_frontend.running && "bmf_gpu_backend_active: not running");
    return gpu_frontend.desc.backend;
}

bmf_gpu_texture bmf_gpu_surface(void) {
    BMF_ASSERT(gpu_frontend.running && "bmf_gpu_surface: not running");
    return gpu_frontend.surface;
}

bmf_gpu_format bmf_gpu_surface_format(void) {
    BMF_ASSERT(gpu_frontend.running && "bmf_gpu_surface_format: not running");
    return gpu_frontend.surface_format;
}

void bmf_gpu_surface_resize(i32 width, i32 height) {
    BMF_ASSERT(gpu_frontend.running && gpu_frontend.surface.id != 0 && "bmf_gpu_surface_resize: no surface (started without a window)");
    BMF_ASSERT(!gpu_frontend.in_frame && "bmf_gpu_surface_resize: inside a frame");
    BMF_ASSERT(width > 0 && height > 0 && width <= BMF_GPU_MAX_TEXTURE_SIZE && height <= BMF_GPU_MAX_TEXTURE_SIZE &&
               "bmf_gpu_surface_resize: size must be 1..BMF_GPU_MAX_TEXTURE_SIZE");
    gpu_texture_info* info = &gpu_frontend.textures[gpu_handle_slot(gpu_frontend.surface.id)];
    info->width = width;
    info->height = height;
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11) gpu_d3d11_surface_resize(width, height);
#endif
}

// ---------------------------------------------------------------- buffers
bmf_gpu_buffer bmf_gpu_buffer_create(const bmf_gpu_buffer_desc* desc) {
    BMF_ASSERT(gpu_frontend.running && "bmf_gpu_buffer_create: not running");
    BMF_ASSERT(desc->usage != 0 && (desc->usage & ~(u32)(BMF_GPU_BUFFER_VERTEX | BMF_GPU_BUFFER_UNIFORM)) == 0 &&
               "bmf_gpu_buffer_create: usage must be a non-empty set of BMF_GPU_BUFFER_* bits");
    BMF_ASSERT((!(desc->usage & BMF_GPU_BUFFER_UNIFORM) || desc->usage == BMF_GPU_BUFFER_UNIFORM) &&
               "bmf_gpu_buffer_create: UNIFORM cannot be combined with other usage bits");
    BMF_ASSERT(desc->size > 0 && desc->size <= BMF_GPU_MAX_BUFFER_SIZE && desc->size % 4 == 0 &&
               "bmf_gpu_buffer_create: size must be a multiple of 4 in 4..BMF_GPU_MAX_BUFFER_SIZE");
    BMF_ASSERT((desc->usage != BMF_GPU_BUFFER_UNIFORM || (desc->size % 16 == 0 && desc->size <= BMF_GPU_MAX_UNIFORM_BUFFER_SIZE)) &&
               "bmf_gpu_buffer_create: a UNIFORM buffer's size must be a multiple of 16 and <= BMF_GPU_MAX_UNIFORM_BUFFER_SIZE");

    bmf_gpu_buffer h = { bmf_pool_alloc(&gpu_frontend.buffer_pool, gpu_frontend.buffer_slots) };
    if (h.id == 0) bmf_panic("bmf_gpu_buffer_create: buffer pool full (BMF_GPU_MAX_BUFFERS)");
    gpu_buffer_info* info = &gpu_frontend.buffers[gpu_handle_slot(h.id)];
    info->usage = desc->usage;
    info->size = desc->size;
    info->dynamic = desc->dynamic;
    info->write_period = ~0ull;   // never written: the next write is its period's first
    info->write_end = 0;
    info->label = desc->label;
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11) gpu_d3d11_buffer_create(gpu_handle_slot(h.id), desc);
#endif
    return h;
}

void bmf_gpu_buffer_destroy(bmf_gpu_buffer buffer) {
    BMF_ASSERT(gpu_frontend.running && gpu_buffer_live(buffer) && "bmf_gpu_buffer_destroy: handle is not a live buffer");
    BMF_ASSERT(!gpu_frontend.in_frame && "bmf_gpu_buffer_destroy: inside a frame");
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11) gpu_d3d11_buffer_destroy(gpu_handle_slot(buffer.id));
#endif
    bmf_pool_free(&gpu_frontend.buffer_pool, gpu_frontend.buffer_slots, buffer.id);
}

void bmf_gpu_buffer_write(bmf_gpu_buffer buffer, i32 offset, const void* data, i32 size) {
    BMF_ASSERT(gpu_frontend.running && gpu_buffer_live(buffer) && "bmf_gpu_buffer_write: handle is not a live buffer");
    gpu_buffer_info* info = &gpu_frontend.buffers[gpu_handle_slot(buffer.id)];
    BMF_ASSERT(data != 0 && size > 0 && "bmf_gpu_buffer_write: needs data and size > 0");
    BMF_ASSERT(offset >= 0 && offset % 4 == 0 && size % 4 == 0 && "bmf_gpu_buffer_write: offset and size must be multiples of 4");
    BMF_ASSERT((i64)offset + size <= info->size && "bmf_gpu_buffer_write: range goes past the end of the buffer");
    BMF_ASSERT((info->usage != BMF_GPU_BUFFER_UNIFORM || info->dynamic) &&
               "bmf_gpu_buffer_write: a UNIFORM buffer must be dynamic to be written (pass constant data at create)");
    BMF_ASSERT((info->write_period != gpu_frontend.write_period || offset >= info->write_end) &&
               "bmf_gpu_buffer_write: range overlaps or precedes an earlier write to this buffer in the same frame");
    b32 first_in_period = info->write_period != gpu_frontend.write_period;
    info->write_period = gpu_frontend.write_period;
    info->write_end = offset + size;
    gpu_frontend.frame.bytes_written += size;
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11) gpu_d3d11_buffer_write(gpu_handle_slot(buffer.id), offset, data, size, first_in_period);
#endif
    (void)first_in_period;
}

// ---------------------------------------------------------------- textures
bmf_gpu_texture bmf_gpu_texture_create(const bmf_gpu_texture_desc* desc) {
    BMF_ASSERT(gpu_frontend.running && "bmf_gpu_texture_create: not running");
    BMF_ASSERT(desc->format > BMF_GPU_FORMAT_NONE && desc->format < BMF_GPU_FORMAT_COUNT && "bmf_gpu_texture_create: invalid format");
    BMF_ASSERT(desc->usage != 0 &&
               (desc->usage & ~(u32)(BMF_GPU_TEXTURE_SAMPLED | BMF_GPU_TEXTURE_RENDER_TARGET | BMF_GPU_TEXTURE_COPY_SRC)) == 0 &&
               "bmf_gpu_texture_create: usage must be a non-empty set of BMF_GPU_TEXTURE_* bits");
    BMF_ASSERT(desc->width > 0 && desc->height > 0 && desc->width <= BMF_GPU_MAX_TEXTURE_SIZE && desc->height <= BMF_GPU_MAX_TEXTURE_SIZE &&
               "bmf_gpu_texture_create: width and height must be 1..BMF_GPU_MAX_TEXTURE_SIZE");
    BMF_ASSERT((desc->format != BMF_GPU_FORMAT_DEPTH24_STENCIL8 || (desc->usage == BMF_GPU_TEXTURE_RENDER_TARGET && desc->data == 0)) &&
               "bmf_gpu_texture_create: depth textures allow only RENDER_TARGET usage and no initial data");

    bmf_gpu_texture h = { bmf_pool_alloc(&gpu_frontend.texture_pool, gpu_frontend.texture_slots) };
    if (h.id == 0) bmf_panic("bmf_gpu_texture_create: texture pool full (BMF_GPU_MAX_TEXTURES)");
    gpu_texture_info* info = &gpu_frontend.textures[gpu_handle_slot(h.id)];
    info->format = desc->format;
    info->usage = desc->usage;
    info->width = desc->width;
    info->height = desc->height;
    info->is_surface = 0;
    info->label = desc->label;
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11) gpu_d3d11_texture_create(gpu_handle_slot(h.id), desc);
#endif
    return h;
}

void bmf_gpu_texture_destroy(bmf_gpu_texture texture) {
    BMF_ASSERT(gpu_frontend.running && gpu_texture_live(texture) && "bmf_gpu_texture_destroy: handle is not a live texture");
    BMF_ASSERT(!gpu_frontend.textures[gpu_handle_slot(texture.id)].is_surface && "bmf_gpu_texture_destroy: the surface cannot be destroyed");
    BMF_ASSERT(!gpu_frontend.in_frame && "bmf_gpu_texture_destroy: inside a frame");
    BMF_ASSERT(!(gpu_frontend.read_in_flight && gpu_frontend.read_texture.id == texture.id) &&
               "bmf_gpu_texture_destroy: a read of this texture is in flight (finish it with bmf_gpu_texture_read_end)");
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11) gpu_d3d11_texture_destroy(gpu_handle_slot(texture.id));
#endif
    bmf_pool_free(&gpu_frontend.texture_pool, gpu_frontend.texture_slots, texture.id);
}

void bmf_gpu_texture_size(bmf_gpu_texture texture, i32* out_width, i32* out_height) {
    BMF_ASSERT(gpu_frontend.running && gpu_texture_live(texture) && "bmf_gpu_texture_size: handle is not a live texture");
    const gpu_texture_info* info = &gpu_frontend.textures[gpu_handle_slot(texture.id)];
    *out_width = info->width;
    *out_height = info->height;
}

// ---------------------------------------------------------------- samplers
bmf_gpu_sampler bmf_gpu_sampler_create(const bmf_gpu_sampler_desc* desc) {
    BMF_ASSERT(gpu_frontend.running && "bmf_gpu_sampler_create: not running");
    BMF_ASSERT((u32)desc->filter <= BMF_GPU_FILTER_LINEAR && "bmf_gpu_sampler_create: invalid filter");
    BMF_ASSERT((u32)desc->address <= BMF_GPU_ADDRESS_REPEAT && "bmf_gpu_sampler_create: invalid address mode");
    bmf_gpu_sampler h = { bmf_pool_alloc(&gpu_frontend.sampler_pool, gpu_frontend.sampler_slots) };
    if (h.id == 0) bmf_panic("bmf_gpu_sampler_create: sampler pool full (BMF_GPU_MAX_SAMPLERS)");
    gpu_frontend.samplers[gpu_handle_slot(h.id)] = *desc;
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11) gpu_d3d11_sampler_create(gpu_handle_slot(h.id), desc);
#endif
    return h;
}

void bmf_gpu_sampler_destroy(bmf_gpu_sampler sampler) {
    BMF_ASSERT(gpu_frontend.running && gpu_sampler_live(sampler) && "bmf_gpu_sampler_destroy: handle is not a live sampler");
    BMF_ASSERT(!gpu_frontend.in_frame && "bmf_gpu_sampler_destroy: inside a frame");
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11) gpu_d3d11_sampler_destroy(gpu_handle_slot(sampler.id));
#endif
    bmf_pool_free(&gpu_frontend.sampler_pool, gpu_frontend.sampler_slots, sampler.id);
}

// ---------------------------------------------------------------- pipelines
bmf_gpu_pipeline bmf_gpu_pipeline_create(const bmf_gpu_pipeline_desc* desc) {
    BMF_ASSERT(gpu_frontend.running && "bmf_gpu_pipeline_create: not running");
#if BMF_DEBUG
    BMF_ASSERT(desc->shader >= 0 && desc->shader < BMF_GPU_MAX_SHADERS && "bmf_gpu_pipeline_create: shader id out of range");
    BMF_ASSERT(desc->vertex_buffer_count >= 0 && desc->vertex_buffer_count <= BMF_GPU_MAX_VERTEX_BUFFERS &&
               "bmf_gpu_pipeline_create: vertex_buffer_count must be 0..BMF_GPU_MAX_VERTEX_BUFFERS");
    u32 locations_used = 0;
    for (i32 b = 0; b < desc->vertex_buffer_count; b++) {
        const bmf_gpu_vertex_layout* layout = &desc->vertex_buffers[b];
        BMF_ASSERT(layout->stride >= 4 && layout->stride <= BMF_GPU_MAX_VERTEX_STRIDE && layout->stride % 4 == 0 &&
                   "bmf_gpu_pipeline_create: vertex stride must be a multiple of 4 in 4..BMF_GPU_MAX_VERTEX_STRIDE");
        BMF_ASSERT((u32)layout->step <= BMF_GPU_STEP_INSTANCE && "bmf_gpu_pipeline_create: invalid vertex step");
        BMF_ASSERT(layout->attribute_count >= 1 && layout->attribute_count <= BMF_GPU_MAX_VERTEX_ATTRIBUTES &&
                   "bmf_gpu_pipeline_create: attribute_count must be 1..BMF_GPU_MAX_VERTEX_ATTRIBUTES");
        for (i32 a = 0; a < layout->attribute_count; a++) {
            const bmf_gpu_vertex_attribute* attribute = &layout->attributes[a];
            i32 bytes = gpu_vertex_format_bytes(attribute->format);
            BMF_ASSERT(bytes > 0 && "bmf_gpu_pipeline_create: invalid vertex attribute format");
            BMF_ASSERT(attribute->offset >= 0 && attribute->offset % 4 == 0 && attribute->offset + bytes <= layout->stride &&
                       "bmf_gpu_pipeline_create: attribute offset must be a multiple of 4 and the attribute must fit in the stride");
            BMF_ASSERT(attribute->location >= 0 && attribute->location < BMF_GPU_MAX_VERTEX_LOCATIONS &&
                       "bmf_gpu_pipeline_create: attribute location must be 0..BMF_GPU_MAX_VERTEX_LOCATIONS-1");
            BMF_ASSERT(!(locations_used & (1u << attribute->location)) && "bmf_gpu_pipeline_create: attribute location used twice");
            locations_used |= 1u << attribute->location;
        }
    }
    BMF_ASSERT((u32)desc->topology <= BMF_GPU_TOPOLOGY_LINE_LIST && "bmf_gpu_pipeline_create: invalid topology");
    BMF_ASSERT((u32)desc->blend <= BMF_GPU_BLEND_ADDITIVE && "bmf_gpu_pipeline_create: invalid blend");
    BMF_ASSERT((u32)desc->cull <= BMF_GPU_CULL_FRONT && "bmf_gpu_pipeline_create: invalid cull");
    BMF_ASSERT(gpu_format_is_color(desc->color_format) && "bmf_gpu_pipeline_create: color_format must be a color format");
    BMF_ASSERT((desc->depth_format == BMF_GPU_FORMAT_NONE || desc->depth_format == BMF_GPU_FORMAT_DEPTH24_STENCIL8) &&
               "bmf_gpu_pipeline_create: depth_format must be NONE or DEPTH24_STENCIL8");
    BMF_ASSERT((desc->depth_format != BMF_GPU_FORMAT_NONE || (!desc->depth_test && !desc->depth_write)) &&
               "bmf_gpu_pipeline_create: depth_test/depth_write need a depth_format");
#endif
    bmf_gpu_pipeline h = { bmf_pool_alloc(&gpu_frontend.pipeline_pool, gpu_frontend.pipeline_slots) };
    if (h.id == 0) bmf_panic("bmf_gpu_pipeline_create: pipeline pool full (BMF_GPU_MAX_PIPELINES)");
    gpu_frontend.pipelines[gpu_handle_slot(h.id)] = *desc;
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11) gpu_d3d11_pipeline_create(gpu_handle_slot(h.id), desc);
#endif
    return h;
}

void bmf_gpu_pipeline_destroy(bmf_gpu_pipeline pipeline) {
    BMF_ASSERT(gpu_frontend.running && gpu_pipeline_live(pipeline) && "bmf_gpu_pipeline_destroy: handle is not a live pipeline");
    BMF_ASSERT(!gpu_frontend.in_frame && "bmf_gpu_pipeline_destroy: inside a frame");
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11) gpu_d3d11_pipeline_destroy(gpu_handle_slot(pipeline.id));
#endif
    bmf_pool_free(&gpu_frontend.pipeline_pool, gpu_frontend.pipeline_slots, pipeline.id);
}

// ---------------------------------------------------------------- bind sets
// Debug check shared by bindset_create and draw: every referenced resource is live and usable.
static inline b32 gpu_bindset_resources_live(const bmf_gpu_bindset_desc* desc) {
    for (i32 i = 0; i < BMF_GPU_MAX_BIND_UNIFORMS; i++)
        if (desc->uniforms[i].buffer.id != 0 && !gpu_buffer_live(desc->uniforms[i].buffer)) return 0;
    for (i32 i = 0; i < BMF_GPU_MAX_BIND_TEXTURES; i++)
        if (desc->textures[i].id != 0 && !gpu_texture_live(desc->textures[i])) return 0;
    for (i32 i = 0; i < BMF_GPU_MAX_BIND_SAMPLERS; i++)
        if (desc->samplers[i].id != 0 && !gpu_sampler_live(desc->samplers[i])) return 0;
    return 1;
}

bmf_gpu_bindset bmf_gpu_bindset_create(const bmf_gpu_bindset_desc* desc) {
    BMF_ASSERT(gpu_frontend.running && "bmf_gpu_bindset_create: not running");
#if BMF_DEBUG
    BMF_ASSERT(desc->shader >= 0 && desc->shader < BMF_GPU_MAX_SHADERS && "bmf_gpu_bindset_create: shader id out of range");
    BMF_ASSERT(gpu_bindset_resources_live(desc) && "bmf_gpu_bindset_create: a referenced buffer, texture or sampler is not live");
    for (i32 i = 0; i < BMF_GPU_MAX_BIND_UNIFORMS; i++) {
        const bmf_gpu_uniform_binding* u = &desc->uniforms[i];
        if (u->buffer.id == 0) {
            BMF_ASSERT(u->offset == 0 && u->size == 0 && "bmf_gpu_bindset_create: an empty uniform slot must have offset and size 0");
            continue;
        }
        const gpu_buffer_info* info = &gpu_frontend.buffers[gpu_handle_slot(u->buffer.id)];
        BMF_ASSERT(info->usage == BMF_GPU_BUFFER_UNIFORM && "bmf_gpu_bindset_create: uniform slot buffer lacks UNIFORM usage");
        BMF_ASSERT(u->offset >= 0 && u->offset % BMF_GPU_UNIFORM_OFFSET_ALIGNMENT == 0 &&
                   "bmf_gpu_bindset_create: uniform offset must be a multiple of BMF_GPU_UNIFORM_OFFSET_ALIGNMENT");
        BMF_ASSERT(u->size > 0 && u->size % 16 == 0 && (i64)u->offset + u->size <= info->size &&
                   "bmf_gpu_bindset_create: uniform size must be a multiple of 16, > 0, and inside the buffer");
    }
    for (i32 i = 0; i < BMF_GPU_MAX_BIND_TEXTURES; i++) {
        if (desc->textures[i].id == 0) continue;
        const gpu_texture_info* info = &gpu_frontend.textures[gpu_handle_slot(desc->textures[i].id)];
        BMF_ASSERT((info->usage & BMF_GPU_TEXTURE_SAMPLED) && "bmf_gpu_bindset_create: texture lacks SAMPLED usage");
    }
#if BMF_OS_WINDOWS
    // Only a backend with shader bytecode knows the declared registers (the null backend has none).
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11)
        BMF_ASSERT(gpu_d3d11_bindset_fits_shader(desc) &&
                   "bmf_gpu_bindset_create: the shader has no bytecode, or the bind set does not fill exactly the b/t/s slots it declares");
#endif
#endif
    bmf_gpu_bindset h = { bmf_pool_alloc(&gpu_frontend.bindset_pool, gpu_frontend.bindset_slots) };
    if (h.id == 0) bmf_panic("bmf_gpu_bindset_create: bind set pool full (BMF_GPU_MAX_BINDSETS)");
    gpu_frontend.bindsets[gpu_handle_slot(h.id)] = *desc;
    // backend: WebGPU creates the bind group here (1B-42).
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11) gpu_d3d11_bindset_create(gpu_handle_slot(h.id), desc);
#endif
    return h;
}

void bmf_gpu_bindset_destroy(bmf_gpu_bindset bindset) {
    BMF_ASSERT(gpu_frontend.running && gpu_bindset_live(bindset) && "bmf_gpu_bindset_destroy: handle is not a live bind set");
    BMF_ASSERT(!gpu_frontend.in_frame && "bmf_gpu_bindset_destroy: inside a frame");
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11) gpu_d3d11_bindset_destroy(gpu_handle_slot(bindset.id));
#endif
    bmf_pool_free(&gpu_frontend.bindset_pool, gpu_frontend.bindset_slots, bindset.id);
}

// ---------------------------------------------------------------- frame and passes
b32 bmf_gpu_begin_frame(void) {
    BMF_ASSERT(gpu_frontend.running && "bmf_gpu_begin_frame: not running");
    BMF_ASSERT(!gpu_frontend.in_frame && "bmf_gpu_begin_frame: already inside a frame (missing bmf_gpu_end_frame)");
    gpu_frontend.in_frame = 1;
    gpu_frontend.frame_surface_rendered = 0;
    b32 usable = gpu_frontend.surface.id != 0;
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11) usable = gpu_d3d11_begin_frame();
#endif
    gpu_frontend.frame_surface_usable = usable;
    return usable;
}

void bmf_gpu_end_frame(void) {
    BMF_ASSERT(gpu_frontend.in_frame && "bmf_gpu_end_frame: not inside a frame");
    BMF_ASSERT(!gpu_frontend.in_pass && "bmf_gpu_end_frame: inside a pass (missing bmf_gpu_end_pass)");
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11) gpu_d3d11_end_frame(gpu_frontend.frame_surface_rendered);
#endif
    gpu_frontend.in_frame = 0;
    gpu_frontend.write_period++;
    gpu_frontend.frame.frames = gpu_frontend.last.frames + 1;
    gpu_frontend.last = gpu_frontend.frame;
    bmf_memset(&gpu_frontend.frame, 0, sizeof gpu_frontend.frame);
}

void bmf_gpu_begin_pass(const bmf_gpu_pass_desc* desc) {
    BMF_ASSERT(gpu_frontend.in_frame && "bmf_gpu_begin_pass: not inside a frame (call bmf_gpu_begin_frame first)");
    BMF_ASSERT(!gpu_frontend.in_pass && "bmf_gpu_begin_pass: already inside a pass (missing bmf_gpu_end_pass)");
    BMF_ASSERT(gpu_texture_live(desc->color) && "bmf_gpu_begin_pass: color is not a live texture (bmf_gpu_surface() is {0} without a window)");
    const gpu_texture_info* color = &gpu_frontend.textures[gpu_handle_slot(desc->color.id)];
    BMF_ASSERT((color->usage & BMF_GPU_TEXTURE_RENDER_TARGET) && gpu_format_is_color(color->format) &&
               "bmf_gpu_begin_pass: color must be a RENDER_TARGET texture with a color format");
    BMF_ASSERT((u32)desc->color_load <= BMF_GPU_LOAD_KEEP && (u32)desc->depth_load <= BMF_GPU_LOAD_KEEP &&
               "bmf_gpu_begin_pass: invalid load action");
    bmf_gpu_format depth_format = BMF_GPU_FORMAT_NONE;
    if (desc->depth.id != 0) {
        BMF_ASSERT(gpu_texture_live(desc->depth) && "bmf_gpu_begin_pass: depth is not a live texture");
        const gpu_texture_info* depth = &gpu_frontend.textures[gpu_handle_slot(desc->depth.id)];
        BMF_ASSERT(depth->format == BMF_GPU_FORMAT_DEPTH24_STENCIL8 && "bmf_gpu_begin_pass: depth must have a depth format");
        BMF_ASSERT(depth->width == color->width && depth->height == color->height && "bmf_gpu_begin_pass: depth and color sizes differ");
        BMF_ASSERT((desc->depth_load != BMF_GPU_LOAD_CLEAR || (desc->clear_depth >= 0.0f && desc->clear_depth <= 1.0f)) &&
                   "bmf_gpu_begin_pass: clear_depth must be in 0..1");
        depth_format = depth->format;
    }
    gpu_frontend.in_pass = 1;
    gpu_frontend.pass_color = desc->color;
    gpu_frontend.pass_depth = desc->depth;
    gpu_frontend.pass_color_format = color->format;
    gpu_frontend.pass_depth_format = depth_format;
    gpu_frontend.pass_last_pipeline.id = 0;
    gpu_frontend.pass_last_bindset.id = 0;
    gpu_frontend.pass_first_draw = 1;
    gpu_frontend.frame.passes++;
    b32 to_surface = desc->color.id == gpu_frontend.surface.id;
    gpu_frontend.pass_dropped = to_surface && !gpu_frontend.frame_surface_usable;
    if (gpu_frontend.pass_dropped) return;
    if (to_surface) gpu_frontend.frame_surface_rendered = 1;
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11)
        gpu_d3d11_begin_pass(gpu_handle_slot(desc->color.id), desc->depth.id ? gpu_handle_slot(desc->depth.id) : -1, color->width,
                             color->height, desc);
#endif
}

void bmf_gpu_end_pass(void) {
    BMF_ASSERT(gpu_frontend.in_pass && "bmf_gpu_end_pass: not inside a pass");
    gpu_frontend.in_pass = 0;
    gpu_frontend.pass_dropped = 0;
    // backend: nothing yet (D3D11 rebinds targets at the next begin_pass).
}

void bmf_gpu_draw(const bmf_gpu_draw_desc* desc) {
    BMF_ASSERT(gpu_frontend.in_pass && "bmf_gpu_draw: not inside a pass (call bmf_gpu_begin_pass first)");
    BMF_ASSERT(gpu_pipeline_live(desc->pipeline) && "bmf_gpu_draw: pipeline is not live");
    BMF_ASSERT(desc->vertex_count >= 0 && desc->instance_count >= 0 && desc->first_vertex >= 0 && desc->first_instance >= 0 &&
               "bmf_gpu_draw: counts and first indices must be >= 0");
    const bmf_gpu_pipeline_desc* pipeline = &gpu_frontend.pipelines[gpu_handle_slot(desc->pipeline.id)];
#if BMF_DEBUG
    BMF_ASSERT(pipeline->color_format == gpu_frontend.pass_color_format && "bmf_gpu_draw: pipeline color_format differs from the pass color target");
    BMF_ASSERT(pipeline->depth_format == gpu_frontend.pass_depth_format &&
               "bmf_gpu_draw: pipeline depth_format differs from the pass depth target (NONE needs a pass without depth)");
    if (desc->bindset.id != 0) {
        BMF_ASSERT(gpu_bindset_live(desc->bindset) && "bmf_gpu_draw: bindset is not live");
        const bmf_gpu_bindset_desc* bindset = &gpu_frontend.bindsets[gpu_handle_slot(desc->bindset.id)];
        BMF_ASSERT(bindset->shader == pipeline->shader && "bmf_gpu_draw: bindset was made for a different shader than the pipeline's");
        BMF_ASSERT(gpu_bindset_resources_live(bindset) && "bmf_gpu_draw: bindset refers to a destroyed resource");
        for (i32 i = 0; i < BMF_GPU_MAX_BIND_TEXTURES; i++) {
            BMF_ASSERT((bindset->textures[i].id == 0 ||
                        (bindset->textures[i].id != gpu_frontend.pass_color.id && bindset->textures[i].id != gpu_frontend.pass_depth.id)) &&
                       "bmf_gpu_draw: bindset samples a texture that is a target of the current pass");
        }
    }
    for (i32 b = 0; b < BMF_GPU_MAX_VERTEX_BUFFERS; b++) {
        if (b >= pipeline->vertex_buffer_count) {
            BMF_ASSERT(desc->vertex_buffers[b].id == 0 && "bmf_gpu_draw: more vertex buffers than the pipeline has layouts");
            continue;
        }
        BMF_ASSERT(gpu_buffer_live(desc->vertex_buffers[b]) && "bmf_gpu_draw: a vertex buffer the pipeline needs is not live");
        const gpu_buffer_info* info = &gpu_frontend.buffers[gpu_handle_slot(desc->vertex_buffers[b].id)];
        BMF_ASSERT((info->usage & BMF_GPU_BUFFER_VERTEX) && "bmf_gpu_draw: vertex buffer lacks VERTEX usage");
        i32 offset = desc->vertex_buffer_offsets[b];
        BMF_ASSERT(offset >= 0 && offset % 4 == 0 && "bmf_gpu_draw: vertex buffer offset must be a multiple of 4");
        // Bytes read: up to the end of the last attribute of the last element (WebGPU's rule).
        const bmf_gpu_vertex_layout* layout = &pipeline->vertex_buffers[b];
        i64 count = layout->step == BMF_GPU_STEP_VERTEX ? desc->vertex_count : desc->instance_count;
        i64 first = layout->step == BMF_GPU_STEP_VERTEX ? desc->first_vertex : desc->first_instance;
        i64 extent = 0;
        for (i32 a = 0; a < layout->attribute_count; a++) {
            i64 end = layout->attributes[a].offset + gpu_vertex_format_bytes(layout->attributes[a].format);
            if (end > extent) extent = end;
        }
        BMF_ASSERT((desc->vertex_count == 0 || desc->instance_count == 0 || offset + (first + count - 1) * layout->stride + extent <= info->size) &&
                   "bmf_gpu_draw: the draw reads past the end of a vertex buffer");
    }
#endif
    if (desc->vertex_count == 0 || desc->instance_count == 0) return;
    if (gpu_frontend.pass_first_draw || desc->pipeline.id != gpu_frontend.pass_last_pipeline.id) gpu_frontend.frame.pipeline_changes++;
    if (gpu_frontend.pass_first_draw || desc->bindset.id != gpu_frontend.pass_last_bindset.id) gpu_frontend.frame.bindset_changes++;
    gpu_frontend.pass_first_draw = 0;
    gpu_frontend.pass_last_pipeline = desc->pipeline;
    gpu_frontend.pass_last_bindset = desc->bindset;
    gpu_frontend.frame.draws++;
    gpu_frontend.frame.instances += desc->instance_count;
    gpu_frontend.frame.vertices += (i64)desc->vertex_count * desc->instance_count;
    (void)pipeline;
    if (gpu_frontend.pass_dropped) return;
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11) {
        i32 vertex_buffer_slots[BMF_GPU_MAX_VERTEX_BUFFERS] = { -1, -1 };
        for (i32 b = 0; b < pipeline->vertex_buffer_count; b++) vertex_buffer_slots[b] = gpu_handle_slot(desc->vertex_buffers[b].id);
        gpu_d3d11_draw(gpu_handle_slot(desc->pipeline.id), desc->bindset.id ? gpu_handle_slot(desc->bindset.id) : -1, vertex_buffer_slots, desc);
    }
#endif
}

// ---------------------------------------------------------------- readback
bmf_err bmf_gpu_texture_read_begin(bmf_gpu_texture texture) {
    BMF_ASSERT(gpu_frontend.running && gpu_texture_live(texture) && "bmf_gpu_texture_read_begin: handle is not a live texture");
    BMF_ASSERT(!gpu_frontend.in_pass && "bmf_gpu_texture_read_begin: inside a pass");
    BMF_ASSERT(!gpu_frontend.read_in_flight && "bmf_gpu_texture_read_begin: a read is already in flight");
    const gpu_texture_info* info = &gpu_frontend.textures[gpu_handle_slot(texture.id)];
    BMF_ASSERT((info->usage & BMF_GPU_TEXTURE_COPY_SRC) && "bmf_gpu_texture_read_begin: texture lacks COPY_SRC usage");
    BMF_ASSERT(gpu_format_bytes(info->format) == 4 && info->format != BMF_GPU_FORMAT_DEPTH24_STENCIL8 &&
               "bmf_gpu_texture_read_begin: format must be RGBA8_UNORM, RGBA8_UNORM_SRGB or BGRA8_UNORM");
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_NULL) return BMF_ERR_UNSUPPORTED;
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11) gpu_d3d11_read_begin(gpu_handle_slot(texture.id), info->width, info->height, info->format);
#endif
    gpu_frontend.read_in_flight = 1;
    gpu_frontend.read_texture = texture;
    return BMF_OK;
}

bmf_err bmf_gpu_texture_read_end(u8* out, i64 out_capacity) {
    BMF_ASSERT(gpu_frontend.running && gpu_frontend.read_in_flight && "bmf_gpu_texture_read_end: no read in flight");
    BMF_ASSERT(gpu_texture_live(gpu_frontend.read_texture) && "bmf_gpu_texture_read_end: texture was destroyed");
    const gpu_texture_info* info = &gpu_frontend.textures[gpu_handle_slot(gpu_frontend.read_texture.id)];
    BMF_ASSERT(out != 0 && out_capacity >= (i64)info->width * info->height * 4 && "bmf_gpu_texture_read_end: out is smaller than width * height * 4");
    (void)info;
    // backend: map; BMF_ERR_PENDING until the data is back (WebGPU); convert to RGBA rows top to bottom.
#if BMF_OS_WINDOWS
    if (gpu_frontend.desc.backend == BMF_GPU_BACKEND_D3D11) gpu_d3d11_read_end(out);
#endif
    gpu_frontend.read_in_flight = 0;
    return BMF_OK;
}

// ---------------------------------------------------------------- stats
bmf_gpu_stats bmf_gpu_get_stats(void) {
    BMF_ASSERT(gpu_frontend.running && "bmf_gpu_get_stats: not running");
    bmf_gpu_stats s = gpu_frontend.last;
    s.buffers = gpu_frontend.buffer_pool.live_count;
    s.textures = gpu_frontend.texture_pool.live_count;
    s.samplers = gpu_frontend.sampler_pool.live_count;
    s.pipelines = gpu_frontend.pipeline_pool.live_count;
    s.bindsets = gpu_frontend.bindset_pool.live_count;
    return s;
}
