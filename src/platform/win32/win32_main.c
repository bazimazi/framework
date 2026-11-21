// win32_main.c: runtime exe entry (CRT-free, D-03). Task 1A-23 turns this into the engine
// loop. Until then it proves the link modes and gives a manual event test:
//   game.exe             resolve bmf_game() from game.dll next to the exe (BMF_GAME_LINK_DLL) or
//                        the statically linked game (BMF_GAME_LINK_STATIC), print one JSON line,
//                        exit 0 when the symbol was found
//   game.exe --control   also open a window and print every platform event as a JSON line until
//                        the window is closed (F11 toggles borderless fullscreen) (task 1A-12)

typedef const void* (*bmf_game_fn)(void);

#if defined(BMF_GAME_LINK_STATIC)
BMF_EXPORT const void* bmf_game(void);   // defined by the game's game.c, included after bmf.c
#endif

static const char* const WIN32_EVENT_NAMES[BMF_EVENT_TYPE_COUNT] = {
    "none", "key_down", "key_up", "text", "mouse_move", "mouse_delta", "mouse_button_down",
    "mouse_button_up", "mouse_wheel", "pointer_down", "pointer_move", "pointer_up", "gamepad_connected",
    "gamepad_disconnected", "focus_gained", "focus_lost", "resized", "suspended", "resumed", "close_requested",
};

// Prints one event as {"event":NAME,...} on stdout.
static void win32_print_event(const bmf_event* e) {
    char line[512];
    bmf_json_writer w;
    bmf_json_init(&w, line, (i32)sizeof line - 1);
    bmf_json_object_begin(&w);
    bmf_json_key(&w, "event"); bmf_json_string(&w, WIN32_EVENT_NAMES[e->type]);
    switch (e->type) {
    case BMF_EVENT_KEY_DOWN: case BMF_EVENT_KEY_UP:
        bmf_json_key(&w, "key"); bmf_json_string(&w, bmf_key_name(e->key.key));
        if (e->type == BMF_EVENT_KEY_DOWN) { bmf_json_key(&w, "repeat"); bmf_json_bool(&w, e->key.repeat); }
        break;
    case BMF_EVENT_TEXT: {
        u8 utf8[5];
        i32 len = bmf_utf8_encode(e->text.codepoint, utf8);
        bmf_json_key(&w, "text"); bmf_json_str(&w, bmf_str_make(utf8, len));
        bmf_json_key(&w, "codepoint"); bmf_json_u64(&w, e->text.codepoint);
        break;
    }
    case BMF_EVENT_MOUSE_MOVE:
        bmf_json_key(&w, "x"); bmf_json_f32(&w, e->position.x);
        bmf_json_key(&w, "y"); bmf_json_f32(&w, e->position.y);
        break;
    case BMF_EVENT_MOUSE_DELTA: case BMF_EVENT_MOUSE_WHEEL:
        bmf_json_key(&w, "dx"); bmf_json_f32(&w, e->delta.x);
        bmf_json_key(&w, "dy"); bmf_json_f32(&w, e->delta.y);
        break;
    case BMF_EVENT_MOUSE_BUTTON_DOWN: case BMF_EVENT_MOUSE_BUTTON_UP:
        bmf_json_key(&w, "button"); bmf_json_i64(&w, e->mouse.button);
        break;
    case BMF_EVENT_POINTER_DOWN: case BMF_EVENT_POINTER_MOVE: case BMF_EVENT_POINTER_UP:
        bmf_json_key(&w, "slot"); bmf_json_i64(&w, e->pointer.slot);
        bmf_json_key(&w, "kind"); bmf_json_string(&w, e->pointer.kind == BMF_POINTER_PEN ? "pen" : "touch");
        bmf_json_key(&w, "x"); bmf_json_f32(&w, e->pointer.x);
        bmf_json_key(&w, "y"); bmf_json_f32(&w, e->pointer.y);
        bmf_json_key(&w, "pressure"); bmf_json_f32(&w, e->pointer.pressure);
        break;
    case BMF_EVENT_GAMEPAD_CONNECTED: case BMF_EVENT_GAMEPAD_DISCONNECTED:
        bmf_json_key(&w, "index"); bmf_json_i64(&w, e->gamepad.index);
        break;
    case BMF_EVENT_RESIZED:
        bmf_json_key(&w, "width"); bmf_json_i64(&w, e->size.width);
        bmf_json_key(&w, "height"); bmf_json_i64(&w, e->size.height);
        bmf_json_key(&w, "dpi_scale"); bmf_json_f32(&w, e->size.dpi_scale);
        break;
    default: break;
    }
    bmf_json_object_end(&w);
    line[w.len] = '\n';
    bmf_platform_write(BMF_STREAM_STDOUT, line, w.len + 1);
}

// Manual test for 1A-12: window plus event printing; gamepad state is printed when it changes.
static void win32_event_test_loop(void) {
    bmf_window_desc desc = { "bmf events", 960, 540, 0 };
    if (!bmf_platform_window_open(&desc)) {
        const char* failed = "{\"event\":\"error\",\"detail\":\"window_open_failed\"}\n";
        bmf_platform_write(BMF_STREAM_STDOUT, failed, bmf_cstr_len(failed));
        bmf_platform_exit(1);
    }
    bmf_window_size size = bmf_platform_window_size();
    char line[256];
    i32 len = bmf_fmt_buf(line, (i32)sizeof line, "{\"event\":\"window_open\",\"width\":%i,\"height\":%i,\"dpi_scale\":%f}\n",
                          size.width, size.height, size.dpi_scale);
    bmf_platform_write(BMF_STREAM_STDOUT, line, len);
    bmf_gamepad previous[BMF_GAMEPAD_MAX];
    bmf_memset(previous, 0, sizeof previous);
    for (;;) {
        bmf_platform_poll_events();
        bmf_event event;
        b32 close = 0;
        while (bmf_platform_event_pop(&event)) {
            win32_print_event(&event);
            if (event.type == BMF_EVENT_CLOSE_REQUESTED) close = 1;
            if (event.type == BMF_EVENT_KEY_DOWN && event.key.key == BMF_KEY_F11 && !event.key.repeat)
                bmf_platform_window_set_fullscreen(!bmf_platform_window_is_fullscreen());
        }
        for (i32 i = 0; i < BMF_GAMEPAD_MAX; i++) {
            bmf_gamepad pad;
            bmf_platform_gamepad_get(i, &pad);
            if (!pad.connected || bmf_memcmp(&pad, &previous[i], sizeof pad) == 0) continue;
            previous[i] = pad;
            len = bmf_fmt_buf(line, (i32)sizeof line,
                              "{\"event\":\"gamepad\",\"index\":%i,\"buttons\":%u,\"left\":[%f,%f],\"right\":[%f,%f],\"triggers\":[%f,%f]}\n",
                              i, pad.buttons, pad.left_x, pad.left_y, pad.right_x, pad.right_y, pad.left_trigger, pad.right_trigger);
            bmf_platform_write(BMF_STREAM_STDOUT, line, len);
        }
        if (close) break;
        bmf_platform_sleep_ns(2000000);   // ~500 polls per second is plenty for a manual test
    }
    bmf_platform_window_close();
    bmf_platform_exit(0);
}

void bmf_win32_entry(void);
void bmf_win32_entry(void) {
    bmf_platform_init();
    bmf_game_fn game = 0;
#if defined(BMF_GAME_LINK_DLL)
    char path[BMF_PLATFORM_PATH_MAX * 3];
    i32 n = bmf_platform_exe_dir(path, (i32)sizeof path - 16);
    if (n > 0) {
        bmf_memcpy(path + n, "game.dll", 9);
        bmf_library dll = bmf_platform_library_load(path);
        if (dll) game = (bmf_game_fn)bmf_platform_library_symbol(dll, "bmf_game");
    }
#else
    game = bmf_game;
#endif
    const char* line = game ? "{\"event\":\"start\",\"game\":\"found\"}\n" : "{\"event\":\"start\",\"game\":\"missing\"}\n";
    bmf_platform_write(BMF_STREAM_STDOUT, line, bmf_cstr_len(line));
    if (!game) bmf_platform_exit(1);
    bmf_platform_args args = bmf_platform_get_args();
    for (i32 i = 1; i < args.count; i++)
        if (bmf_cstr_eq(args.values[i], "--control")) win32_event_test_loop();
    bmf_platform_exit(0);
}
