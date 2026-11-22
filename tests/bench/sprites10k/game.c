// tests/bench/sprites10k: the 10 000-sprite benchmark scene (SPEC §15 point 6, §12.3: 10 000
// sprites, CPU submit plus sort < 0.5 ms on desktop; task 1A-42).
//
// Every frame draws `sprite_count` (10 000) textured sprites on 8 layers with 4 textures (32
// sort groups), each bouncing across the view on its own straight path. The paths are a closed
// form of the simulated time (a triangle wave per axis), so the state is a few scalars and the
// recorded replay stays small, while the draw callback does what a real game's does: compute a
// position, submit a sprite. The work measured is the `draw` zone (computing and submitting the
// sprites) and the `draw2d_flush` zone (sort, gather, ring upload, batching); perf.budget holds
// their limits and tests/bench.script the replay (`build test --update-golden sprites10k.bench`).
//
// A bench scene is framework code: unlike a game it may use the engine's internal headers (here
// the sprite call and GPU textures, which games get with the asset tasks 1B-32/1B-33). It builds
// like a game: `build win64 release tests/bench/sprites10k --agent`, then
// out/sprites10k/win64-release-agent/game.exe --headless --replay tests/bench/sprites10k/tests/bench.60hz.bmfr --profile.
//
// bmf-lint: sim

#include "bmf.h"
#include "gpu/gpu.h"
#include "draw2d/draw2d.h"

enum {
    SPRITES_DEFAULT_COUNT = 10000,
    SPRITES_MAX_COUNT = 60000,        // under draw2d's 65 536 instances per frame
    SPRITES_LAYERS = 8,
    SPRITES_TEXTURES = 4,
    SPRITES_TEXTURE_SIZE = 8,         // pixels
};

// ---------------------------------------------------------------- state
typedef struct sprites_state {
    i32 sprite_count;    // sprites drawn per frame, 0..SPRITES_MAX_COUNT (`set sprite_count N`)
    f32 time;            // simulated seconds
    f32 previous_time;   // time at the end of the previous tick (draw interpolates)
} sprites_state;

static const bmf_field sprites_fields[] = {
    { "sprite_count", BMF_T_I32, offsetof(sprites_state, sprite_count), 1, 0 },
    { "time", BMF_T_F32, offsetof(sprites_state, time), 1, 0 },
    { "previous_time", BMF_T_F32, offsetof(sprites_state, previous_time), 1, 0 },
};

static void sprites_init(bmf_ctx* cx, void* state, u64 seed) {
    sprites_state* s = (sprites_state*)state;
    s->sprite_count = SPRITES_DEFAULT_COUNT;
}

static void sprites_tick(bmf_ctx* cx, void* state, const bmf_input* in) {
    sprites_state* s = (sprites_state*)state;
    s->previous_time = s->time;
    s->time += cx->dt;
    if (s->sprite_count < 0) s->sprite_count = 0;
    if (s->sprite_count > SPRITES_MAX_COUNT) s->sprite_count = SPRITES_MAX_COUNT;
}

// ---------------------------------------------------------------- presentation
// Textures (presentation resources, not state): 4 small patterns, made on the first draw.
static bmf_gpu_texture sprites_textures[SPRITES_TEXTURES];

static void sprites_make_textures(void) {
    static const u32 colors[SPRITES_TEXTURES] = { 0xFF3DA5F4u, 0xFFF4A53Du, 0xFF68D96Bu, 0xFF6A50E8u };   // RGBA bytes, little endian ABGR
    u32 pixels[SPRITES_TEXTURE_SIZE * SPRITES_TEXTURE_SIZE];
    for (i32 t = 0; t < SPRITES_TEXTURES; t++) {
        for (i32 y = 0; y < SPRITES_TEXTURE_SIZE; y++)
            for (i32 x = 0; x < SPRITES_TEXTURE_SIZE; x++)
                pixels[y * SPRITES_TEXTURE_SIZE + x] = ((x ^ y) & (t + 1)) ? colors[t] : 0xFFFFFFFFu;
        bmf_gpu_texture_desc desc = { 0 };
        desc.format = BMF_GPU_FORMAT_RGBA8_UNORM;
        desc.usage = BMF_GPU_TEXTURE_SAMPLED;
        desc.width = SPRITES_TEXTURE_SIZE;
        desc.height = SPRITES_TEXTURE_SIZE;
        desc.data = pixels;
        desc.label = "sprites10k";
        sprites_textures[t] = bmf_gpu_texture_create(&desc);
    }
}

// A value in [0, 1) from a sprite index and a salt (integer hash, then the top 24 bits).
static f32 sprites_unit(u32 index, u32 salt) {
    u32 h = index * 0x9E3779B1u ^ salt;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return (f32)(h >> 8) * (1.0f / 16777216.0f);
}

// Folds a coordinate travelling at constant speed into [0, 1] (bouncing between the ends).
static f32 sprites_bounce(f32 x) {
    f32 cycle = x * 0.5f;
    f32 phase = (cycle - bmf_floorf(cycle)) * 2.0f;   // 0..2
    return phase <= 1.0f ? phase : 2.0f - phase;
}

static void sprites_draw(bmf_ctx* cx, const void* state, f32 alpha) {
    const sprites_state* s = (const sprites_state*)state;
    if (!sprites_textures[0].id) sprites_make_textures();
    bmf_camera camera = bmf_camera_screen_default();
    for (i32 layer = 0; layer < SPRITES_LAYERS; layer++) bmf_draw_camera_push(layer, &camera);
    bmf_v2 view = cx->view_size;
    f32 size = view.y / 24.0f;
    f32 time = s->previous_time + (s->time - s->previous_time) * alpha;
    bmf_rect uv = { 0.0f, 0.0f, 1.0f, 1.0f };
    for (i32 i = 0; i < s->sprite_count; i++) {
        u32 index = (u32)i;
        // Start in [0, 1), speed 0.05..0.3 view sizes per second, per axis.
        f32 x = sprites_bounce(sprites_unit(index, 0x1111u) + time * (0.05f + 0.25f * sprites_unit(index, 0x2222u)));
        f32 y = sprites_bounce(sprites_unit(index, 0x3333u) + time * (0.05f + 0.25f * sprites_unit(index, 0x4444u)));
        f32 rotation = time * (sprites_unit(index, 0x5555u) - 0.5f) * 4.0f;
        bmf_draw_sprite(i % SPRITES_LAYERS, sprites_textures[(i / SPRITES_LAYERS) % SPRITES_TEXTURES], uv,
                        bmf_v2_make(x * view.x, y * view.y), bmf_v2_make(size, size), rotation, 0xFFFFFFFFu);
    }
}

// ---------------------------------------------------------------- the scene
static const bmf_game_desc sprites_game = {
    .api_version = BMF_API_VERSION,
    .name = "sprites10k",
    .state_size = (i32)sizeof(sprites_state),
    .state_layout_hash = 0x5e1e863c90982602ull,   // bmf_fields_hash(sprites_fields)
    .tick_rate = 60,
    .fields = sprites_fields,
    .field_count = (i32)(sizeof sprites_fields / sizeof sprites_fields[0]),
    .init = sprites_init,
    .tick = sprites_tick,
    .draw = sprites_draw,
};

BMF_EXPORT const bmf_game_desc* bmf_game(void) { return &sprites_game; }
