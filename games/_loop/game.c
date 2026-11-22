// games/_loop: engine-loop and hot-reload test game (task 1A-23). A circle moves at a constant
// velocity and bounces off the edges of a 20 x 11.25 m field (the default world camera at
// 1280 x 720); `reverse` (space, gamepad A) reverses it. Draw interpolates between the previous
// and current position with alpha. To try hot reload: run the debug build, change LOOP_COLOR and
// rebuild (state kept), or add a field to loop_state and loop_fields: the reload is rejected
// with the new layout hash; set state_layout_hash to it and rebuild (state reset).
// Not a template; new games start from games/hello.

#include "bmf.h"

#define LOOP_COLOR 0x3DA5F4FF

typedef struct loop_state {
    bmf_v2 position;
    bmf_v2 previous_position;
    bmf_v2 velocity;          // meters per second
    i32 bounces;
    i32 reversals;
} loop_state;

static const f32 LOOP_RADIUS = 0.5f;
static const f32 LOOP_HALF_WIDTH = 10.0f;
static const f32 LOOP_HALF_HEIGHT = 5.625f;

static const bmf_field loop_fields[] = {
    { "position", BMF_T_V2, offsetof(loop_state, position), 1, 0 },
    { "velocity", BMF_T_V2, offsetof(loop_state, velocity), 1, 0 },
    { "bounces", BMF_T_I32, offsetof(loop_state, bounces), 1, 0 },
    { "reversals", BMF_T_I32, offsetof(loop_state, reversals), 1, 0 },
};

static const bmf_action loop_actions[] = {
    { "reverse", BMF_ACTION_BUTTON, { BMF_KEY_SPACE, BMF_PAD_A } },
};

static void loop_init(bmf_ctx* cx, void* state, u64 seed) {
    loop_state* s = (loop_state*)state;
    s->velocity = bmf_v2_make(3.0f, 2.0f);
}

static void loop_tick(bmf_ctx* cx, void* state, const bmf_input* in) {
    loop_state* s = (loop_state*)state;
    if (bmf_input_pressed(in, 0)) {
        s->velocity = bmf_v2_make(-s->velocity.x, -s->velocity.y);
        s->reversals++;
    }
    s->previous_position = s->position;
    s->position.x += s->velocity.x * cx->dt;
    s->position.y += s->velocity.y * cx->dt;
    f32 limit_x = LOOP_HALF_WIDTH - LOOP_RADIUS, limit_y = LOOP_HALF_HEIGHT - LOOP_RADIUS;
    if ((s->position.x > limit_x && s->velocity.x > 0.0f) || (s->position.x < -limit_x && s->velocity.x < 0.0f)) {
        s->velocity.x = -s->velocity.x;
        s->bounces++;
    }
    if ((s->position.y > limit_y && s->velocity.y > 0.0f) || (s->position.y < -limit_y && s->velocity.y < 0.0f)) {
        s->velocity.y = -s->velocity.y;
        s->bounces++;
    }
}

static void loop_draw(bmf_ctx* cx, const void* state, f32 alpha) {
    const loop_state* s = (const loop_state*)state;
    bmf_v2 at = bmf_v2_make(s->previous_position.x + (s->position.x - s->previous_position.x) * alpha,
                            s->previous_position.y + (s->position.y - s->previous_position.y) * alpha);
    bmf_draw_circle(0, at, LOOP_RADIUS, LOOP_COLOR);
    bmf_draw_text(1, bmf_v2_make(-9.5f, 5.3f), 0.5f, 0.0f, 0xFFFFFFFF, BMF_TEXT_ALIGN_LEFT, bmf_str_from_cstr("bmf _loop"));
}

static const bmf_game_desc loop_game = {
    .api_version = BMF_API_VERSION,
    .name = "_loop",
    .state_size = (i32)sizeof(loop_state),
    .state_layout_hash = 0x8f33ca16b24c85fcull,   // bmf_fields_hash(loop_fields); the debug runtime checks it
    .tick_rate = 60,
    .fields = loop_fields,
    .field_count = (i32)(sizeof loop_fields / sizeof loop_fields[0]),
    .actions = loop_actions,
    .action_count = (i32)(sizeof loop_actions / sizeof loop_actions[0]),
    .init = loop_init,
    .tick = loop_tick,
    .draw = loop_draw,
};

BMF_EXPORT const bmf_game_desc* bmf_game(void) { return &loop_game; }
