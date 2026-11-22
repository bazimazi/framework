// games/_empty: the smallest valid game, used only to test the build pipeline (1A-01) and the
// engine's startup path. It is not a template; new games start from games/hello.

#include "bmf.h"

static void empty_init(bmf_ctx* cx, void* state, u64 seed) {}
static void empty_tick(bmf_ctx* cx, void* state, const bmf_input* in) {}
static void empty_draw(bmf_ctx* cx, const void* state, f32 alpha) {}

static const bmf_game_desc empty_game = {
    .api_version = BMF_API_VERSION,
    .name = "_empty",
    .state_size = 4,
    .init = empty_init,
    .tick = empty_tick,
    .draw = empty_draw,
};

BMF_EXPORT const bmf_game_desc* bmf_game(void) { return &empty_game; }
