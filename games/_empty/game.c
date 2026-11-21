// games/_empty: the smallest possible game, used only to test the build pipeline (1A-01).
// It is not a template; new games start from games/hello.
BMF_EXPORT const void* bmf_game(void);
BMF_EXPORT const void* bmf_game(void) { return 0; }
