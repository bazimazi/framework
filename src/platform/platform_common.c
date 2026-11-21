// platform_common.c: the OS-independent parts of platform.h (task 1A-12): key names and the
// platform event queue. Included by bmf.c before the OS folder's files.

// ---------------------------------------------------------------- key names
static const char* const PLATFORM_KEY_NAMES[BMF_KEY_COUNT] = {
    [BMF_KEY_NONE] = "none",
    [BMF_KEY_A] = "a", [BMF_KEY_B] = "b", [BMF_KEY_C] = "c", [BMF_KEY_D] = "d", [BMF_KEY_E] = "e",
    [BMF_KEY_F] = "f", [BMF_KEY_G] = "g", [BMF_KEY_H] = "h", [BMF_KEY_I] = "i", [BMF_KEY_J] = "j",
    [BMF_KEY_K] = "k", [BMF_KEY_L] = "l", [BMF_KEY_M] = "m", [BMF_KEY_N] = "n", [BMF_KEY_O] = "o",
    [BMF_KEY_P] = "p", [BMF_KEY_Q] = "q", [BMF_KEY_R] = "r", [BMF_KEY_S] = "s", [BMF_KEY_T] = "t",
    [BMF_KEY_U] = "u", [BMF_KEY_V] = "v", [BMF_KEY_W] = "w", [BMF_KEY_X] = "x", [BMF_KEY_Y] = "y",
    [BMF_KEY_Z] = "z",
    [BMF_KEY_0] = "0", [BMF_KEY_1] = "1", [BMF_KEY_2] = "2", [BMF_KEY_3] = "3", [BMF_KEY_4] = "4",
    [BMF_KEY_5] = "5", [BMF_KEY_6] = "6", [BMF_KEY_7] = "7", [BMF_KEY_8] = "8", [BMF_KEY_9] = "9",
    [BMF_KEY_ESCAPE] = "escape", [BMF_KEY_ENTER] = "enter", [BMF_KEY_TAB] = "tab",
    [BMF_KEY_BACKSPACE] = "backspace", [BMF_KEY_SPACE] = "space",
    [BMF_KEY_MINUS] = "minus", [BMF_KEY_EQUAL] = "equal", [BMF_KEY_LEFT_BRACKET] = "left_bracket",
    [BMF_KEY_RIGHT_BRACKET] = "right_bracket", [BMF_KEY_BACKSLASH] = "backslash",
    [BMF_KEY_SEMICOLON] = "semicolon", [BMF_KEY_APOSTROPHE] = "apostrophe", [BMF_KEY_GRAVE] = "grave",
    [BMF_KEY_COMMA] = "comma", [BMF_KEY_PERIOD] = "period", [BMF_KEY_SLASH] = "slash",
    [BMF_KEY_INTL_BACKSLASH] = "intl_backslash",
    [BMF_KEY_CAPS_LOCK] = "caps_lock", [BMF_KEY_SCROLL_LOCK] = "scroll_lock", [BMF_KEY_NUM_LOCK] = "num_lock",
    [BMF_KEY_PRINT_SCREEN] = "print_screen", [BMF_KEY_PAUSE] = "pause",
    [BMF_KEY_F1] = "f1", [BMF_KEY_F2] = "f2", [BMF_KEY_F3] = "f3", [BMF_KEY_F4] = "f4",
    [BMF_KEY_F5] = "f5", [BMF_KEY_F6] = "f6", [BMF_KEY_F7] = "f7", [BMF_KEY_F8] = "f8",
    [BMF_KEY_F9] = "f9", [BMF_KEY_F10] = "f10", [BMF_KEY_F11] = "f11", [BMF_KEY_F12] = "f12",
    [BMF_KEY_F13] = "f13", [BMF_KEY_F14] = "f14", [BMF_KEY_F15] = "f15", [BMF_KEY_F16] = "f16",
    [BMF_KEY_F17] = "f17", [BMF_KEY_F18] = "f18", [BMF_KEY_F19] = "f19", [BMF_KEY_F20] = "f20",
    [BMF_KEY_F21] = "f21", [BMF_KEY_F22] = "f22", [BMF_KEY_F23] = "f23", [BMF_KEY_F24] = "f24",
    [BMF_KEY_INSERT] = "insert", [BMF_KEY_DELETE] = "delete", [BMF_KEY_HOME] = "home", [BMF_KEY_END] = "end",
    [BMF_KEY_PAGE_UP] = "page_up", [BMF_KEY_PAGE_DOWN] = "page_down",
    [BMF_KEY_LEFT] = "left", [BMF_KEY_RIGHT] = "right", [BMF_KEY_UP] = "up", [BMF_KEY_DOWN] = "down",
    [BMF_KEY_KP_0] = "kp_0", [BMF_KEY_KP_1] = "kp_1", [BMF_KEY_KP_2] = "kp_2", [BMF_KEY_KP_3] = "kp_3",
    [BMF_KEY_KP_4] = "kp_4", [BMF_KEY_KP_5] = "kp_5", [BMF_KEY_KP_6] = "kp_6", [BMF_KEY_KP_7] = "kp_7",
    [BMF_KEY_KP_8] = "kp_8", [BMF_KEY_KP_9] = "kp_9", [BMF_KEY_KP_DECIMAL] = "kp_decimal",
    [BMF_KEY_KP_DIVIDE] = "kp_divide", [BMF_KEY_KP_MULTIPLY] = "kp_multiply",
    [BMF_KEY_KP_SUBTRACT] = "kp_subtract", [BMF_KEY_KP_ADD] = "kp_add", [BMF_KEY_KP_ENTER] = "kp_enter",
    [BMF_KEY_LEFT_SHIFT] = "left_shift", [BMF_KEY_RIGHT_SHIFT] = "right_shift",
    [BMF_KEY_LEFT_CTRL] = "left_ctrl", [BMF_KEY_RIGHT_CTRL] = "right_ctrl",
    [BMF_KEY_LEFT_ALT] = "left_alt", [BMF_KEY_RIGHT_ALT] = "right_alt",
    [BMF_KEY_LEFT_SUPER] = "left_super", [BMF_KEY_RIGHT_SUPER] = "right_super", [BMF_KEY_MENU] = "menu",
};

const char* bmf_key_name(bmf_key key) {
    if ((i32)key <= 0 || (i32)key >= BMF_KEY_COUNT) return "none";
    return PLATFORM_KEY_NAMES[key];
}

bmf_key bmf_key_from_name(bmf_str name) {
    for (i32 k = 1; k < BMF_KEY_COUNT; k++)
        if (bmf_str_eq_cstr(name, PLATFORM_KEY_NAMES[k])) return (bmf_key)k;
    return BMF_KEY_NONE;
}

// ---------------------------------------------------------------- event queue
// Ring buffer written by the OS layer during bmf_platform_poll_events and read by the engine.
// Both happen on the main thread, so no atomics are needed.
static struct {
    bmf_event events[BMF_EVENT_QUEUE_CAPACITY];
    i32 head;      // index of the oldest event
    i32 count;
    i32 dropped;
} platform_events;

// For the OS layer: appends one event; when the queue is full the event is dropped and counted.
static void platform_event_push(const bmf_event* event) {
    if (platform_events.count == BMF_EVENT_QUEUE_CAPACITY) { platform_events.dropped++; return; }
    i32 index = (platform_events.head + platform_events.count) % BMF_EVENT_QUEUE_CAPACITY;
    platform_events.events[index] = *event;
    platform_events.count++;
}

b32 bmf_platform_event_pop(bmf_event* out) {
    if (platform_events.count == 0) return 0;
    *out = platform_events.events[platform_events.head];
    platform_events.head = (platform_events.head + 1) % BMF_EVENT_QUEUE_CAPACITY;
    platform_events.count--;
    return 1;
}

i32 bmf_platform_events_dropped(void) {
    return platform_events.dropped;
}
