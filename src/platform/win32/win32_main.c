// win32_main.c: runtime exe entry (CRT-free, D-03). Parses the command line (SPEC §11.1),
// resolves the game and hands it to the engine loop (src/core, task 1A-23):
//   BMF_GAME_LINK_DLL     debug builds: game.dll next to the exe, loaded from a copy and hot
//                         reloaded when it changes (bmf_core_dll_open, SPEC §8.5)
//   BMF_GAME_LINK_STATIC  release builds: the game's game.c is compiled into this binary
// Exit code: 0 after a normal quit, 1 after a startup error (one {"event":"error",...} line on stdout).

void bmf_win32_entry(void);
void bmf_win32_entry(void) {
    bmf_platform_init();
    bmf_platform_args args = bmf_platform_get_args();
    char error[BMF_CORE_ERROR_MAX];
    bmf_core_options options;
    if (bmf_core_parse_args(args.count, args.values, &options, error, (i32)sizeof error) != BMF_OK) {
        bmf_core_print_error("bad_args", error);
        bmf_platform_exit(1);
    }
#if defined(BMF_GAME_LINK_DLL)
    const bmf_game_desc* game = bmf_core_dll_open(error, (i32)sizeof error);
    if (!game) {
        bmf_core_print_error("game_missing", error);
        bmf_platform_exit(1);
    }
    // Hot reload may grow the state block: reserve address space for it (never committed unless used).
    options.state_reserve = 256ll << 20;
#else
    const bmf_game_desc* game = bmf_game();
    if (!game) {
        bmf_core_print_error("game_missing", "bmf_game() returned 0");
        bmf_platform_exit(1);
    }
#endif
    bmf_platform_exit(bmf_core_run(game, &options));
}
