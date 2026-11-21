// Tests for the window/event/gamepad part of platform.h on win32 (task 1A-12): scan code table,
// key names, UTF-16 text decoding, gamepad dead zones, the event queue, and a real (hidden)
// window driven by posted messages.

static void test_platform_scancode_table(bmf_test* t) {
    // Every key is reachable, and only the documented aliases map two scan codes to one key.
    i32 sources[BMF_KEY_COUNT];
    bmf_memset(sources, 0, sizeof sources);
    for (u32 scancode = 0; scancode < 512; scancode++) sources[bmf_win32_key_from_scancode(scancode, 0)]++;
    i32 unreachable = 0, aliased = 0;
    for (i32 k = 1; k < BMF_KEY_COUNT; k++) {
        if (sources[k] == 0) unreachable++;
        if (sources[k] > 1) aliased++;
    }
    bmf_check_eq_i64(t, unreachable, 0, __FILE__, __LINE__, "keys without a scan code");
    // PAUSE (0x45, 0x146), PRINT_SCREEN (0x137, 0x54), RIGHT_SHIFT (0x36, 0x136).
    bmf_check_eq_i64(t, aliased, 3, __FILE__, __LINE__, "keys with two scan codes");
    bmf_check_eq_i64(t, sources[BMF_KEY_PAUSE] + sources[BMF_KEY_PRINT_SCREEN] + sources[BMF_KEY_RIGHT_SHIFT], 6, __FILE__, __LINE__, "the aliases");

    static const struct { u32 scancode; u32 virtual_key; bmf_key key; } cases[] = {
        { 0x1E, 'A', BMF_KEY_A },
        { 0x10, 'A', BMF_KEY_Q },             // AZERTY: the key left of W prints A but is still Q
        { 0x11, 'Z', BMF_KEY_W },
        { 0x02, '1', BMF_KEY_1 }, { 0x0B, '0', BMF_KEY_0 },
        { 0x1C, 0x0D, BMF_KEY_ENTER }, { 0x11C, 0x0D, BMF_KEY_KP_ENTER },
        { 0x1D, 0x11, BMF_KEY_LEFT_CTRL }, { 0x11D, 0x11, BMF_KEY_RIGHT_CTRL },
        { 0x38, 0x12, BMF_KEY_LEFT_ALT }, { 0x138, 0x12, BMF_KEY_RIGHT_ALT },
        { 0x48, 0x68, BMF_KEY_KP_8 }, { 0x148, 0x26, BMF_KEY_UP },
        { 0x53, 0x6E, BMF_KEY_KP_DECIMAL }, { 0x153, 0x2E, BMF_KEY_DELETE },
        { 0x35, 0xBF, BMF_KEY_SLASH }, { 0x135, 0x6F, BMF_KEY_KP_DIVIDE },
        { 0x45, WIN32_VK_PAUSE, BMF_KEY_PAUSE }, { 0x145, WIN32_VK_NUMLOCK, BMF_KEY_NUM_LOCK },
        { 0x45, WIN32_VK_NUMLOCK, BMF_KEY_NUM_LOCK },   // the virtual key decides the overlapping codes
        { 0x146, 0x03, BMF_KEY_PAUSE },                 // Ctrl+Pause sends VK_CANCEL
        { 0x137, WIN32_VK_SNAPSHOT, BMF_KEY_PRINT_SCREEN }, { 0x54, WIN32_VK_SNAPSHOT, BMF_KEY_PRINT_SCREEN },
        { 0x56, 0xE2, BMF_KEY_INTL_BACKSLASH },
        { 0x57, 0x7A, BMF_KEY_F11 }, { 0x76, 0x87, BMF_KEY_F24 },
        { 0x15B, 0x5B, BMF_KEY_LEFT_SUPER }, { 0x15D, 0x5D, BMF_KEY_MENU },
        { 0x12A, 0x10, BMF_KEY_NONE },                 // fake Shift around navigation keys
        { 0x00, 0, BMF_KEY_NONE }, { 0x1FF, 0, BMF_KEY_NONE }, { 600, 0, BMF_KEY_NONE },
    };
    for (i32 c = 0; c < (i32)(sizeof cases / sizeof cases[0]); c++) {
        char message[128];
        bmf_fmt_buf(message, (i32)sizeof message, "scan code 0x%x vk 0x%x", cases[c].scancode, cases[c].virtual_key);
        bmf_check_eq_i64(t, bmf_win32_key_from_scancode(cases[c].scancode, cases[c].virtual_key), cases[c].key, __FILE__, __LINE__, message);
    }

    // Names: present, unique, lowercase, and round-trip through bmf_key_from_name.
    b32 names_ok = 1;
    for (i32 k = 1; k < BMF_KEY_COUNT; k++) {
        const char* name = bmf_key_name((bmf_key)k);
        if (!name || bmf_cstr_eq(name, "none") || bmf_key_from_name(bmf_str_from_cstr(name)) != (bmf_key)k) names_ok = 0;
        for (const char* c = name; c && *c; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) names_ok = 0;
    }
    bmf_check(t, names_ok, __FILE__, __LINE__, "every key has a unique lowercase name");
    bmf_check(t, bmf_cstr_eq(bmf_key_name(BMF_KEY_NONE), "none") && bmf_cstr_eq(bmf_key_name((bmf_key)999), "none"), __FILE__, __LINE__, "none name");
    bmf_check_eq_i64(t, bmf_key_from_name(bmf_str_from_cstr("no_such_key")), BMF_KEY_NONE, __FILE__, __LINE__, "unknown name");
    bmf_check_eq_i64(t, bmf_key_from_name(bmf_str_from_cstr("kp_enter")), BMF_KEY_KP_ENTER, __FILE__, __LINE__, "kp_enter by name");
}

static void test_platform_utf16_text(bmf_test* t) {
    static const struct { u16 units[4]; i32 unit_count; u32 expected[4]; i32 expected_count; } cases[] = {
        { { 'a' }, 1, { 'a' }, 1 },
        { { 0x00E9 }, 1, { 0x00E9 }, 1 },
        { { 0xD83D, 0xDE00 }, 2, { 0x1F600 }, 1 },           // surrogate pair
        { { 0xDBFF, 0xDFFF }, 2, { 0x10FFFF }, 1 },          // highest code point
        { { 0xD800, 0xDC00 }, 2, { 0x10000 }, 1 },           // lowest supplementary code point
        { { 0xD83D, 'b' }, 2, { 0xFFFD, 'b' }, 2 },          // high surrogate then a BMP unit
        { { 0xDE00 }, 1, { 0xFFFD }, 1 },                    // lone low surrogate
        { { 0xD83D, 0xD83D, 0xDE00 }, 3, { 0xFFFD, 0x1F600 }, 2 },   // two highs, then a low
        { { 0xD83D }, 1, { 0 }, 0 },                         // waiting for the low surrogate
    };
    for (i32 c = 0; c < (i32)(sizeof cases / sizeof cases[0]); c++) {
        bmf_win32_utf16_decoder decoder = { 0 };
        u32 got[8];
        i32 got_count = 0;
        for (i32 u = 0; u < cases[c].unit_count; u++) got_count += bmf_win32_utf16_feed(&decoder, cases[c].units[u], got + got_count);
        char message[64];
        bmf_fmt_buf(message, (i32)sizeof message, "case %i count", c);
        bmf_check_eq_i64(t, got_count, cases[c].expected_count, __FILE__, __LINE__, message);
        bmf_fmt_buf(message, (i32)sizeof message, "case %i code points", c);
        bmf_check_eq_mem(t, got, cases[c].expected, (i64)cases[c].expected_count * 4, __FILE__, __LINE__, message);
    }
}

static void test_platform_gamepad_dead_zone(bmf_test* t) {
    f32 x = 0.0f, y = 0.0f;
    i32 zone = WIN32_XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    bmf_win32_stick(0, 0, zone, &x, &y);
    bmf_check(t, x == 0.0f && y == 0.0f, __FILE__, __LINE__, "center");
    bmf_win32_stick((i16)(zone - 1), 0, zone, &x, &y);
    bmf_check(t, x == 0.0f && y == 0.0f, __FILE__, __LINE__, "inside the dead zone");
    bmf_win32_stick(5000, 5000, zone, &x, &y);   // |v| = 7071 < 7849: radial, not per axis
    bmf_check(t, x == 0.0f && y == 0.0f, __FILE__, __LINE__, "diagonal inside the radial zone");
    bmf_win32_stick((i16)(zone + 100), 0, zone, &x, &y);
    bmf_check(t, x > 0.0f && x < 0.01f && y == 0.0f, __FILE__, __LINE__, "just outside starts near 0");
    bmf_win32_stick(32767, 0, zone, &x, &y);
    bmf_check(t, x == 1.0f && y == 0.0f, __FILE__, __LINE__, "full right");
    bmf_win32_stick(0, -32768, zone, &x, &y);
    bmf_check(t, x == 0.0f && y == -1.0f, __FILE__, __LINE__, "full down clamps to -1");
    bmf_win32_stick(32767, 32767, zone, &x, &y);
    f32 magnitude = bmf_sqrtf(x * x + y * y);
    bmf_check(t, x == y && magnitude > 0.999f && magnitude < 1.001f, __FILE__, __LINE__, "full diagonal keeps direction, magnitude 1");
    bmf_win32_stick(20000, -10000, zone, &x, &y);
    bmf_check(t, x > 0.0f && y < 0.0f && bmf_fabsf(x + 2.0f * y) < 1e-6f, __FILE__, __LINE__, "direction preserved");
    bmf_check(t, win32_trigger(0) == 0.0f && win32_trigger(WIN32_XINPUT_GAMEPAD_TRIGGER_THRESHOLD) == 0.0f, __FILE__, __LINE__, "trigger threshold");
    bmf_check(t, win32_trigger(255) == 1.0f && win32_trigger(WIN32_XINPUT_GAMEPAD_TRIGGER_THRESHOLD + 1) > 0.0f, __FILE__, __LINE__, "trigger range");
    bmf_gamepad pad;
    bmf_check(t, !bmf_platform_gamepad_get(0, &pad), __FILE__, __LINE__, "no gamepad without a window");
}

static void platform_drain_events(void) {
    bmf_event event;
    while (bmf_platform_event_pop(&event)) {}
}

static void test_platform_event_queue(bmf_test* t) {
    platform_drain_events();
    i32 dropped_before = bmf_platform_events_dropped();
    for (i32 i = 0; i < BMF_EVENT_QUEUE_CAPACITY + 6; i++) {
        bmf_event event;
        bmf_memset(&event, 0, sizeof event);
        event.type = BMF_EVENT_TEXT;
        event.text.codepoint = (u32)i;
        platform_event_push(&event);
    }
    bmf_check_eq_i64(t, bmf_platform_events_dropped() - dropped_before, 6, __FILE__, __LINE__, "overflow drops the newest");
    i32 popped = 0;
    b32 in_order = 1;
    bmf_event event;
    while (bmf_platform_event_pop(&event)) {
        if (event.type != BMF_EVENT_TEXT || event.text.codepoint != (u32)popped) in_order = 0;
        popped++;
    }
    bmf_check_eq_i64(t, popped, BMF_EVENT_QUEUE_CAPACITY, __FILE__, __LINE__, "queue capacity");
    bmf_check(t, in_order, __FILE__, __LINE__, "FIFO order");
    // Wrap-around: interleaved push/pop keeps order across the ring boundary.
    b32 wrap_ok = 1;
    for (i32 i = 0; i < 3000; i++) {
        bmf_memset(&event, 0, sizeof event);
        event.type = BMF_EVENT_TEXT;
        event.text.codepoint = (u32)i;
        platform_event_push(&event);
        if (!bmf_platform_event_pop(&event) || event.text.codepoint != (u32)i) wrap_ok = 0;
    }
    bmf_check(t, wrap_ok, __FILE__, __LINE__, "ring wrap-around");
}

// Collects queued events except window bookkeeping (RESIZED, FOCUS_GAINED) into `out`.
static i32 platform_collect_events(bmf_event* out, i32 capacity) {
    i32 count = 0;
    bmf_event event;
    while (bmf_platform_event_pop(&event)) {
        if (event.type == BMF_EVENT_RESIZED || event.type == BMF_EVENT_FOCUS_GAINED) continue;
        if (count < capacity) out[count++] = event;
    }
    return count;
}

static void test_platform_window_messages(bmf_test* t) {
    platform_drain_events();
    bmf_window_desc desc = { "bmf test window", 320, 200, 1 };
    bmf_check(t, bmf_platform_window_open(&desc), __FILE__, __LINE__, "open hidden window");
    HWND hwnd = (HWND)bmf_platform_window_native();
    bmf_window_size size = bmf_platform_window_size();
    bmf_check(t, hwnd != 0 && size.dpi_scale > 0.0f, __FILE__, __LINE__, "native handle and DPI scale");
    bmf_check_eq_i64(t, size.width, (i64)(320 * size.dpi_scale + 0.5f), __FILE__, __LINE__, "client width is logical size times DPI scale");
    bmf_check_eq_i64(t, size.height, (i64)(200 * size.dpi_scale + 0.5f), __FILE__, __LINE__, "client height");
    bmf_platform_poll_events();
    platform_drain_events();

    // Keyboard lParam: repeat count 1 | scan code << 16 | extended << 24 | (key up: bits 30, 31).
    LPARAM f5_down = 1 | (0x3F << 16);
    LPARAM up_down = 1 | (0x48 << 16) | (1 << 24);
    LPARAM f5_up = (LPARAM)(1 | (0x3F << 16) | (1u << 30) | (1u << 31));
    PostMessageW(hwnd, WIN32_WM_KEYDOWN, 0x74, f5_down);
    PostMessageW(hwnd, WIN32_WM_KEYDOWN, 0x74, f5_down | (1 << 30));   // auto-repeat
    PostMessageW(hwnd, WIN32_WM_KEYDOWN, 0x26, up_down);
    PostMessageW(hwnd, WIN32_WM_CHAR, 0xD83D, 1);
    PostMessageW(hwnd, WIN32_WM_CHAR, 0xDE00, 1);
    PostMessageW(hwnd, WIN32_WM_CHAR, 0x0D, 1);                       // Enter: a key, not text
    PostMessageW(hwnd, WIN32_WM_KEYUP, 0x74, f5_up);
    PostMessageW(hwnd, WIN32_WM_KEYUP, 0x74, f5_up);                  // second release: ignored
    PostMessageW(hwnd, WIN32_WM_LBUTTONDOWN, 1, 10 | (20 << 16));
    PostMessageW(hwnd, WIN32_WM_MOUSEMOVE, 1, 30 | (40 << 16));
    PostMessageW(hwnd, WIN32_WM_MOUSEWHEEL, (WPARAM)(u32)(-240 << 16), 0);
    PostMessageW(hwnd, WIN32_WM_KILLFOCUS, 0, 0);                     // releases UP and the left button
    PostMessageW(hwnd, WIN32_WM_CLOSE, 0, 0);
    bmf_platform_poll_events();

    bmf_event events[32];
    i32 count = platform_collect_events(events, 32);
    static const struct { bmf_event_type type; i32 value; } expected[] = {
        { BMF_EVENT_KEY_DOWN, BMF_KEY_F5 }, { BMF_EVENT_KEY_DOWN, BMF_KEY_F5 }, { BMF_EVENT_KEY_DOWN, BMF_KEY_UP },
        { BMF_EVENT_TEXT, 0x1F600 }, { BMF_EVENT_KEY_UP, BMF_KEY_F5 },
        { BMF_EVENT_MOUSE_BUTTON_DOWN, BMF_MOUSE_LEFT }, { BMF_EVENT_MOUSE_MOVE, 30 }, { BMF_EVENT_MOUSE_WHEEL, -2 },
        { BMF_EVENT_KEY_UP, BMF_KEY_UP }, { BMF_EVENT_MOUSE_BUTTON_UP, BMF_MOUSE_LEFT }, { BMF_EVENT_FOCUS_LOST, 0 },
        { BMF_EVENT_CLOSE_REQUESTED, 0 },
    };
    i32 expected_count = (i32)(sizeof expected / sizeof expected[0]);
    bmf_check_eq_i64(t, count, expected_count, __FILE__, __LINE__, "event count");
    for (i32 i = 0; i < count && i < expected_count; i++) {
        const bmf_event* e = &events[i];
        i32 value = 0;
        switch (e->type) {
        case BMF_EVENT_KEY_DOWN: case BMF_EVENT_KEY_UP: value = (i32)e->key.key; break;
        case BMF_EVENT_TEXT: value = (i32)e->text.codepoint; break;
        case BMF_EVENT_MOUSE_BUTTON_DOWN: case BMF_EVENT_MOUSE_BUTTON_UP: value = (i32)e->mouse.button; break;
        case BMF_EVENT_MOUSE_MOVE: value = (i32)e->position.x; break;
        case BMF_EVENT_MOUSE_WHEEL: value = (i32)e->delta.y; break;
        default: break;
        }
        char message[96];
        bmf_fmt_buf(message, (i32)sizeof message, "event %i: type %i value %i", i, (i32)e->type, value);
        bmf_check(t, e->type == expected[i].type && value == expected[i].value, __FILE__, __LINE__, message);
    }
    bmf_check(t, count > 1 && !events[0].key.repeat && events[1].key.repeat, __FILE__, __LINE__, "repeat flag");
    bmf_check(t, count > 6 && events[6].position.y == 40.0f, __FILE__, __LINE__, "mouse y");

    // DPI change: the scale follows wParam, the window takes the suggested rectangle.
    RECT window_rect;
    GetWindowRect(hwnd, &window_rect);
    RECT suggested = { window_rect.left, window_rect.top, window_rect.left + 900, window_rect.top + 700 };
    // Sent, not posted: Windows refuses to post system messages whose lParam is a pointer.
    SendMessageW(hwnd, WIN32_WM_DPICHANGED, 144 | (144 << 16), (LPARAM)&suggested);
    bmf_event event;
    b32 saw_resize = 0;
    while (bmf_platform_event_pop(&event)) if (event.type == BMF_EVENT_RESIZED && event.size.dpi_scale == 1.5f) saw_resize = 1;
    bmf_check(t, saw_resize, __FILE__, __LINE__, "RESIZED with dpi_scale 1.5");
    GetWindowRect(hwnd, &window_rect);
    bmf_check_eq_i64(t, window_rect.right - window_rect.left, 900, __FILE__, __LINE__, "suggested width applied");

    // Borderless fullscreen covers the monitor and restores the window; a hidden window stays hidden.
    bmf_window_size before = bmf_platform_window_size();
    bmf_platform_window_set_fullscreen(1);
    MONITORINFO monitor;
    bmf_memset(&monitor, 0, sizeof monitor);
    monitor.cbSize = sizeof monitor;
    GetMonitorInfoW(MonitorFromWindow(hwnd, WIN32_MONITOR_DEFAULTTONEAREST), &monitor);
    bmf_window_size full = bmf_platform_window_size();
    bmf_check(t, bmf_platform_window_is_fullscreen(), __FILE__, __LINE__, "is fullscreen");
    bmf_check_eq_i64(t, full.width, monitor.rcMonitor.right - monitor.rcMonitor.left, __FILE__, __LINE__, "fullscreen width = monitor");
    bmf_check_eq_i64(t, full.height, monitor.rcMonitor.bottom - monitor.rcMonitor.top, __FILE__, __LINE__, "fullscreen height = monitor");
    bmf_platform_window_set_fullscreen(0);
    bmf_window_size after = bmf_platform_window_size();
    bmf_check(t, !bmf_platform_window_is_fullscreen(), __FILE__, __LINE__, "back to windowed");
    bmf_check(t, after.width == before.width && after.height == before.height, __FILE__, __LINE__, "windowed size restored");
    bmf_check(t, (GetWindowLongPtrW(hwnd, WIN32_GWL_STYLE) & WIN32_WS_VISIBLE) == 0, __FILE__, __LINE__, "still hidden");

    // Minimize and restore: SUSPENDED then RESUMED. Sent synchronously, so no window has to be shown.
    SendMessageW(hwnd, WIN32_WM_SIZE, WIN32_SIZE_MINIMIZED, 0);
    SendMessageW(hwnd, WIN32_WM_SIZE, WIN32_SIZE_MINIMIZED, 0);
    SendMessageW(hwnd, WIN32_WM_SIZE, WIN32_SIZE_RESTORED, (LPARAM)(after.width | (after.height << 16)));
    count = platform_collect_events(events, 32);
    bmf_check(t, count == 2 && events[0].type == BMF_EVENT_SUSPENDED && events[1].type == BMF_EVENT_RESUMED, __FILE__, __LINE__, "suspend/resume once each");

    bmf_platform_window_close();
    bmf_check(t, bmf_platform_window_native() == 0 && bmf_platform_window_size().width == 0, __FILE__, __LINE__, "closed");
    platform_drain_events();
    // A second window after closing the first (the class is registered once).
    bmf_check(t, bmf_platform_window_open(&desc), __FILE__, __LINE__, "reopen");
    bmf_platform_window_close();
    platform_drain_events();
}
