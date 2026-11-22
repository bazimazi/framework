// games/hello: the smallest complete bmf game, and the template for new ones (task 1A-40).
//
// Circles fall under gravity and bounce off the walls of a 20 x 11.25 m field, which the camera
// fits into any window size. A click or tap spawns a circle at the pointer,
// `jump` (space, gamepad A) kicks every circle upwards, and every circle that reaches the
// ceiling scores a point.
//
// How a bmf game is put together (copy this file to games/<name>/game.c to start one):
//   1. One state struct holds the whole simulation: fixed-capacity arrays plus a live count, no
//      pointers (D-21, D-23). The engine allocates it, zeroes it, snapshots and hashes it.
//   2. A field table describes the state for the agent tools (get/set/dump/watch, replays'
//      field diffs). state_layout_hash must equal bmf_fields_hash(fields): the debug runtime
//      says which value to put there when they differ.
//   3. An action table names the inputs; tick reads them through bmf_input, never from devices.
//   4. init and tick are deterministic (D-37): only the state, the seed and the input, the RNG
//      kept in the state, constants per second scaled by cx->dt (rule 15), no float-to-int casts.
//   5. draw only reads the state, interpolating between the previous and the current tick with
//      alpha. It picks the camera (here: the whole field, as large as the view allows) and gives
//      the same camera to the input, so pointer positions arrive in world meters.
//   6. bmf_game() returns the description. Build: `build win64 debug games/hello`; run:
//      out/hello/win64-debug/game.exe (add --headless --control to drive it with the protocol).
//   7. tests/ holds the goldens: tests/bounce.script plays a scenario; `build test --update-golden
//      hello.bounce` records it at 60 and 120 Hz with WARP shots, and `build test` then checks
//      that the game still replays to the same hashes and pixels (tools/test/README.md).
//   8. perf.budget limits the time of the tick and draw zones (`zone=tick p99_us<=10`), measured
//      by `build test` on a release BMF_AGENT=1 build playing the golden replays.

//
// bmf-lint marks the whole file as simulation code for the golden lint run by `build test`: no
// bmf_fast_*, clocks, FMA or raw float-to-int casts (use bmf_f32_to_i32; int-to-float casts are
// fine). draw lives here too, so it follows the same rules.
// bmf-lint: sim

#include "bmf.h"

// ---------------------------------------------------------------- constants (per second, D-37 rule 15)
enum { HELLO_MAX_CIRCLES = 64, HELLO_START_CIRCLES = 8 };

static const f32 HELLO_HALF_WIDTH = 10.0f;      // m: the field is 20 x 11.25 m
static const f32 HELLO_HALF_HEIGHT = 5.625f;
static const f32 HELLO_RADIUS = 0.35f;          // m
static const f32 HELLO_GRAVITY = -9.81f;        // m/s^2
static const f32 HELLO_BOUNCE = 0.85f;          // share of the speed kept by a wall bounce
static const f32 HELLO_REST_SPEED = 0.6f;       // m/s: slower floor bounces come to rest
static const f32 HELLO_JUMP_SPEED = 9.0f;       // m/s added upwards by `jump`
static const f32 HELLO_SPAWN_SPEED = 4.0f;      // m/s: the largest horizontal spawn speed

// ---------------------------------------------------------------- state
typedef struct hello_state {
    bmf_rng rng;                                  // all randomness comes from here (seeded in init)
    i32 circle_count;                             // live circles: [0, circle_count)
    bmf_v2 position[HELLO_MAX_CIRCLES];           // m
    bmf_v2 previous_position[HELLO_MAX_CIRCLES];  // position at the end of the previous tick (draw interpolates)
    bmf_v2 velocity[HELLO_MAX_CIRCLES];           // m/s
    i32 score;                                    // ceiling hits
    i32 jumps;
    i32 spawns;
} hello_state;

static const bmf_field hello_fields[] = {
    { "rng", BMF_T_RNG, offsetof(hello_state, rng), 1, 0 },
    { "circle_count", BMF_T_I32, offsetof(hello_state, circle_count), 1, 0 },
    { "position", BMF_T_V2, offsetof(hello_state, position), HELLO_MAX_CIRCLES, "circle_count" },
    { "previous_position", BMF_T_V2, offsetof(hello_state, previous_position), HELLO_MAX_CIRCLES, "circle_count" },
    { "velocity", BMF_T_V2, offsetof(hello_state, velocity), HELLO_MAX_CIRCLES, "circle_count" },
    { "score", BMF_T_I32, offsetof(hello_state, score), 1, 0 },
    { "jumps", BMF_T_I32, offsetof(hello_state, jumps), 1, 0 },
    { "spawns", BMF_T_I32, offsetof(hello_state, spawns), 1, 0 },
};

// ---------------------------------------------------------------- input
// Button bits of bmf_input.down/pressed/released: BUTTON actions numbered in table order, counting
// BUTTON actions only (not the index in hello_actions; a POINTER or AXIS2 action takes no bit).
enum { HELLO_BUTTON_JUMP = 0 };

static const bmf_action hello_actions[] = {
    { "jump", BMF_ACTION_BUTTON, { BMF_KEY_SPACE, BMF_PAD_A } },
    { "spawn", BMF_ACTION_POINTER, { 0 } },       // makes the engine fill bmf_input.pointer[]
};

// ---------------------------------------------------------------- simulation
static void hello_spawn(hello_state* s, bmf_v2 at) {
    if (s->circle_count >= HELLO_MAX_CIRCLES) return;   // full: the click does nothing
    i32 i = s->circle_count++;
    f32 limit_x = HELLO_HALF_WIDTH - HELLO_RADIUS, limit_y = HELLO_HALF_HEIGHT - HELLO_RADIUS;
    at.x = bmf_clampf(at.x, -limit_x, limit_x);
    at.y = bmf_clampf(at.y, -limit_y, limit_y);
    s->position[i] = at;
    s->previous_position[i] = at;
    // Two separate statements: the order of RNG draws must not depend on argument evaluation order.
    f32 vx = bmf_rng_range_f32(&s->rng, -HELLO_SPAWN_SPEED, HELLO_SPAWN_SPEED);
    f32 vy = bmf_rng_range_f32(&s->rng, 0.0f, HELLO_SPAWN_SPEED);
    s->velocity[i] = bmf_v2_make(vx, vy);
    s->spawns++;
}

static void hello_init(bmf_ctx* cx, void* state, u64 seed) {
    hello_state* s = (hello_state*)state;
    bmf_rng_seed(&s->rng, seed, 1);
    for (i32 i = 0; i < HELLO_START_CIRCLES; i++) {
        f32 x = bmf_rng_range_f32(&s->rng, -HELLO_HALF_WIDTH + 1.0f, HELLO_HALF_WIDTH - 1.0f);
        f32 y = bmf_rng_range_f32(&s->rng, 0.0f, HELLO_HALF_HEIGHT - 1.0f);
        hello_spawn(s, bmf_v2_make(x, y));
    }
    s->spawns = 0;   // the starting circles are not player spawns
}

static void hello_tick(bmf_ctx* cx, void* state, const bmf_input* in) {
    hello_state* s = (hello_state*)state;
    f32 dt = cx->dt;

    // Input first: spawns at every pointer pressed this tick, then the jump.
    for (i32 p = 0; p < BMF_INPUT_MAX_POINTERS; p++)
        if (in->pointer[p].pressed) hello_spawn(s, in->pointer[p].world);
    if (bmf_input_pressed(in, HELLO_BUTTON_JUMP)) {
        for (i32 i = 0; i < s->circle_count; i++) s->velocity[i].y += HELLO_JUMP_SPEED;
        s->jumps++;
    }

    // Semi-implicit Euler: velocity, then position with the new velocity. Then the walls.
    f32 limit_x = HELLO_HALF_WIDTH - HELLO_RADIUS, limit_y = HELLO_HALF_HEIGHT - HELLO_RADIUS;
    for (i32 i = 0; i < s->circle_count; i++) {
        bmf_v2 p = s->position[i], v = s->velocity[i];
        s->previous_position[i] = p;
        v.y += HELLO_GRAVITY * dt;
        p.x += v.x * dt;
        p.y += v.y * dt;
        if (p.x < -limit_x || p.x > limit_x) {
            p.x = p.x < 0.0f ? -limit_x : limit_x;
            v.x = -v.x * HELLO_BOUNCE;
        }
        if (p.y > limit_y) {
            p.y = limit_y;
            if (v.y > 0.0f) {
                v.y = -v.y * HELLO_BOUNCE;
                s->score++;
            }
        }
        if (p.y < -limit_y) {
            p.y = -limit_y;
            v.y = -v.y * HELLO_BOUNCE;
            if (v.y < HELLO_REST_SPEED) v.y = 0.0f;   // resting on the floor
        }
        s->position[i] = p;
        s->velocity[i] = v;
    }
}

// ---------------------------------------------------------------- presentation
static const bmf_color HELLO_COLORS[] = { 0x3DA5F4FF, 0xF4A53DFF, 0x6BD968FF, 0xE8506AFF, 0xB58CF2FF, 0xF2E35AFF };

static void hello_draw(bmf_ctx* cx, const void* state, f32 alpha) {
    const hello_state* s = (const hello_state*)state;
    // The camera: the field's center at the view's center, scaled so the whole field fits.
    // Presentation only (view_size is never read in tick).
    bmf_camera camera = bmf_camera_world_default();
    camera.pixels_per_unit = bmf_fminf(cx->view_size.x / (2.0f * HELLO_HALF_WIDTH), cx->view_size.y / (2.0f * HELLO_HALF_HEIGHT));
    if (camera.pixels_per_unit <= 0.0f) camera.pixels_per_unit = BMF_DRAW_WORLD_PIXELS_PER_METER;
    for (i32 layer = 0; layer < 3; layer++) bmf_draw_camera_push(layer, &camera);
    bmf_input_set_pointer_camera(&camera);
    // The walls.
    bmf_color wall = 0x5A6475FF;
    bmf_draw_line(0, bmf_v2_make(-HELLO_HALF_WIDTH, -HELLO_HALF_HEIGHT), bmf_v2_make(HELLO_HALF_WIDTH, -HELLO_HALF_HEIGHT), 0.08f, wall);
    bmf_draw_line(0, bmf_v2_make(-HELLO_HALF_WIDTH, HELLO_HALF_HEIGHT), bmf_v2_make(HELLO_HALF_WIDTH, HELLO_HALF_HEIGHT), 0.08f, wall);
    bmf_draw_line(0, bmf_v2_make(-HELLO_HALF_WIDTH, -HELLO_HALF_HEIGHT), bmf_v2_make(-HELLO_HALF_WIDTH, HELLO_HALF_HEIGHT), 0.08f, wall);
    bmf_draw_line(0, bmf_v2_make(HELLO_HALF_WIDTH, -HELLO_HALF_HEIGHT), bmf_v2_make(HELLO_HALF_WIDTH, HELLO_HALF_HEIGHT), 0.08f, wall);
    // The circles, between their previous and current position.
    for (i32 i = 0; i < s->circle_count; i++) {
        bmf_v2 a = s->previous_position[i], b = s->position[i];
        bmf_v2 at = bmf_v2_make(a.x + (b.x - a.x) * alpha, a.y + (b.y - a.y) * alpha);
        bmf_draw_circle(1, at, HELLO_RADIUS, HELLO_COLORS[i % (i32)(sizeof HELLO_COLORS / sizeof HELLO_COLORS[0])]);
    }
    // The score, top left, with the built-in stroke font.
    char text[96];
    i32 length = bmf_fmt_buf(text, (i32)sizeof text, "hello  score %i  circles %i", s->score, s->circle_count);
    bmf_draw_text(2, bmf_v2_make(-HELLO_HALF_WIDTH + 0.4f, HELLO_HALF_HEIGHT - 0.3f), 0.5f, 0.0f, 0xFFFFFFFF, BMF_TEXT_ALIGN_LEFT, bmf_str_make(text, length));
    bmf_draw_text(2, bmf_v2_make(HELLO_HALF_WIDTH - 0.4f, HELLO_HALF_HEIGHT - 0.3f), 0.35f, 0.0f, 0xA0A8B8FF, BMF_TEXT_ALIGN_RIGHT,
                  bmf_str_from_cstr("click: spawn   space: jump"));
}

// ---------------------------------------------------------------- the game
static const bmf_game_desc hello_game = {
    .api_version = BMF_API_VERSION,
    .name = "hello",
    .state_size = (i32)sizeof(hello_state),
    .state_layout_hash = 0x9ff849e59a4d873aull,   // bmf_fields_hash(hello_fields); a debug run with a stale value prints the right one
    .tick_rate = 60,
    .fields = hello_fields,
    .field_count = (i32)(sizeof hello_fields / sizeof hello_fields[0]),
    .actions = hello_actions,
    .action_count = (i32)(sizeof hello_actions / sizeof hello_actions[0]),
    .init = hello_init,
    .tick = hello_tick,
    .draw = hello_draw,
};

BMF_EXPORT const bmf_game_desc* bmf_game(void) { return &hello_game; }
