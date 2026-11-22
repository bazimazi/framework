// win32_window.c: the window, OS events and gamepads on Win32 (SPEC §9.1, task 1A-12).
// Implements the window/event/gamepad part of platform.h. Keyboard: WM_KEYDOWN/UP scan codes
// through a positional table. Text: WM_CHAR (UTF-16, surrogate pairs joined). Mouse: window
// messages for position/buttons/wheel, Raw Input for relative motion. Touch and pen: WM_POINTER*.
// Gamepads: XInput (xinput1_4.dll, loaded at runtime), polled in bmf_platform_poll_events.

// ---------------------------------------------------------------- scan code table
// Index: the 8-bit set-1 scan code from lParam bits 16..23, plus 0x100 when the extended-key bit
// (lParam bit 24, the E0 prefix) is set. Keys absent from the table map to BMF_KEY_NONE: this
// includes 0x12A, the fake Shift that Windows sends around navigation keys when NumLock is on.
static const u8 WIN32_SCANCODE_TO_KEY[512] = {
    [0x01] = BMF_KEY_ESCAPE,
    [0x02] = BMF_KEY_1, [0x03] = BMF_KEY_2, [0x04] = BMF_KEY_3, [0x05] = BMF_KEY_4, [0x06] = BMF_KEY_5,
    [0x07] = BMF_KEY_6, [0x08] = BMF_KEY_7, [0x09] = BMF_KEY_8, [0x0A] = BMF_KEY_9, [0x0B] = BMF_KEY_0,
    [0x0C] = BMF_KEY_MINUS, [0x0D] = BMF_KEY_EQUAL, [0x0E] = BMF_KEY_BACKSPACE, [0x0F] = BMF_KEY_TAB,
    [0x10] = BMF_KEY_Q, [0x11] = BMF_KEY_W, [0x12] = BMF_KEY_E, [0x13] = BMF_KEY_R, [0x14] = BMF_KEY_T,
    [0x15] = BMF_KEY_Y, [0x16] = BMF_KEY_U, [0x17] = BMF_KEY_I, [0x18] = BMF_KEY_O, [0x19] = BMF_KEY_P,
    [0x1A] = BMF_KEY_LEFT_BRACKET, [0x1B] = BMF_KEY_RIGHT_BRACKET, [0x1C] = BMF_KEY_ENTER,
    [0x1D] = BMF_KEY_LEFT_CTRL,
    [0x1E] = BMF_KEY_A, [0x1F] = BMF_KEY_S, [0x20] = BMF_KEY_D, [0x21] = BMF_KEY_F, [0x22] = BMF_KEY_G,
    [0x23] = BMF_KEY_H, [0x24] = BMF_KEY_J, [0x25] = BMF_KEY_K, [0x26] = BMF_KEY_L,
    [0x27] = BMF_KEY_SEMICOLON, [0x28] = BMF_KEY_APOSTROPHE, [0x29] = BMF_KEY_GRAVE,
    [0x2A] = BMF_KEY_LEFT_SHIFT, [0x2B] = BMF_KEY_BACKSLASH,
    [0x2C] = BMF_KEY_Z, [0x2D] = BMF_KEY_X, [0x2E] = BMF_KEY_C, [0x2F] = BMF_KEY_V, [0x30] = BMF_KEY_B,
    [0x31] = BMF_KEY_N, [0x32] = BMF_KEY_M, [0x33] = BMF_KEY_COMMA, [0x34] = BMF_KEY_PERIOD,
    [0x35] = BMF_KEY_SLASH, [0x36] = BMF_KEY_RIGHT_SHIFT, [0x37] = BMF_KEY_KP_MULTIPLY,
    [0x38] = BMF_KEY_LEFT_ALT, [0x39] = BMF_KEY_SPACE, [0x3A] = BMF_KEY_CAPS_LOCK,
    [0x3B] = BMF_KEY_F1, [0x3C] = BMF_KEY_F2, [0x3D] = BMF_KEY_F3, [0x3E] = BMF_KEY_F4, [0x3F] = BMF_KEY_F5,
    [0x40] = BMF_KEY_F6, [0x41] = BMF_KEY_F7, [0x42] = BMF_KEY_F8, [0x43] = BMF_KEY_F9, [0x44] = BMF_KEY_F10,
    [0x45] = BMF_KEY_PAUSE,          // WM_KEYDOWN reports Pause as 0x45 and NumLock as 0x45 + extended
    [0x46] = BMF_KEY_SCROLL_LOCK,
    [0x47] = BMF_KEY_KP_7, [0x48] = BMF_KEY_KP_8, [0x49] = BMF_KEY_KP_9, [0x4A] = BMF_KEY_KP_SUBTRACT,
    [0x4B] = BMF_KEY_KP_4, [0x4C] = BMF_KEY_KP_5, [0x4D] = BMF_KEY_KP_6, [0x4E] = BMF_KEY_KP_ADD,
    [0x4F] = BMF_KEY_KP_1, [0x50] = BMF_KEY_KP_2, [0x51] = BMF_KEY_KP_3, [0x52] = BMF_KEY_KP_0,
    [0x53] = BMF_KEY_KP_DECIMAL,
    [0x54] = BMF_KEY_PRINT_SCREEN,   // Alt+PrintScreen (SysRq)
    [0x56] = BMF_KEY_INTL_BACKSLASH, [0x57] = BMF_KEY_F11, [0x58] = BMF_KEY_F12,
    [0x64] = BMF_KEY_F13, [0x65] = BMF_KEY_F14, [0x66] = BMF_KEY_F15, [0x67] = BMF_KEY_F16,
    [0x68] = BMF_KEY_F17, [0x69] = BMF_KEY_F18, [0x6A] = BMF_KEY_F19, [0x6B] = BMF_KEY_F20,
    [0x6C] = BMF_KEY_F21, [0x6D] = BMF_KEY_F22, [0x6E] = BMF_KEY_F23, [0x76] = BMF_KEY_F24,
    [0x11C] = BMF_KEY_KP_ENTER, [0x11D] = BMF_KEY_RIGHT_CTRL, [0x135] = BMF_KEY_KP_DIVIDE,
    [0x136] = BMF_KEY_RIGHT_SHIFT,   // some IMEs set the extended bit on right Shift
    [0x137] = BMF_KEY_PRINT_SCREEN, [0x138] = BMF_KEY_RIGHT_ALT,
    [0x145] = BMF_KEY_NUM_LOCK,
    [0x146] = BMF_KEY_PAUSE,         // Ctrl+Pause (Break)
    [0x147] = BMF_KEY_HOME, [0x148] = BMF_KEY_UP, [0x149] = BMF_KEY_PAGE_UP, [0x14B] = BMF_KEY_LEFT,
    [0x14D] = BMF_KEY_RIGHT, [0x14F] = BMF_KEY_END, [0x150] = BMF_KEY_DOWN, [0x151] = BMF_KEY_PAGE_DOWN,
    [0x152] = BMF_KEY_INSERT, [0x153] = BMF_KEY_DELETE,
    [0x15B] = BMF_KEY_LEFT_SUPER, [0x15C] = BMF_KEY_RIGHT_SUPER, [0x15D] = BMF_KEY_MENU,
};

// Maps a WM_KEYDOWN/UP scan code (8 bits | 0x100 when extended) and virtual key to a bmf_key.
// The virtual key decides only Pause, NumLock and PrintScreen, whose scan codes overlap across
// modifier states. Exposed for tests. O(1).
bmf_key bmf_win32_key_from_scancode(u32 scancode, u32 virtual_key) {
    if (virtual_key == WIN32_VK_PAUSE) return BMF_KEY_PAUSE;
    if (virtual_key == WIN32_VK_NUMLOCK) return BMF_KEY_NUM_LOCK;
    if (virtual_key == WIN32_VK_SNAPSHOT) return BMF_KEY_PRINT_SCREEN;
    if (scancode >= 512) return BMF_KEY_NONE;
    return (bmf_key)WIN32_SCANCODE_TO_KEY[scancode];
}

// ---------------------------------------------------------------- UTF-16 text decoding
// WM_CHAR delivers one UTF-16 unit per message; a code point above U+FFFF arrives as a high
// surrogate message followed by a low surrogate message.
typedef struct bmf_win32_utf16_decoder {
    u16 pending_high;   // 0 = none
} bmf_win32_utf16_decoder;

// Feeds one unit and writes 0..2 complete code points to out. A high surrogate waits for its
// low surrogate; an unpaired surrogate becomes U+FFFD. Exposed for tests. O(1).
i32 bmf_win32_utf16_feed(bmf_win32_utf16_decoder* decoder, u16 unit, u32 out[2]) {
    i32 count = 0;
    b32 is_high = unit >= 0xD800 && unit <= 0xDBFF;
    b32 is_low = unit >= 0xDC00 && unit <= 0xDFFF;
    if (decoder->pending_high) {
        if (is_low) {
            out[0] = 0x10000 + (((u32)decoder->pending_high - 0xD800) << 10) + ((u32)unit - 0xDC00);
            decoder->pending_high = 0;
            return 1;
        }
        out[count++] = 0xFFFD;   // the previous high surrogate was not followed by a low one
        decoder->pending_high = 0;
    }
    if (is_high) { decoder->pending_high = unit; return count; }
    out[count++] = is_low ? 0xFFFD : unit;
    return count;
}

// ---------------------------------------------------------------- gamepad conversion
// Radial dead zone: inside `dead_zone` (raw units) the stick reads 0; outside, the magnitude is
// rescaled from [dead_zone, 32767] to [0, 1] keeping the direction. Exposed for tests. O(1).
void bmf_win32_stick(i16 raw_x, i16 raw_y, i32 dead_zone, f32* out_x, f32* out_y) {
    f32 x = (f32)raw_x, y = (f32)raw_y;
    f32 magnitude = bmf_sqrtf(x * x + y * y);
    *out_x = 0.0f;
    *out_y = 0.0f;
    if (magnitude <= (f32)dead_zone) return;
    f32 clamped = magnitude > 32767.0f ? 32767.0f : magnitude;
    f32 scale = (clamped - (f32)dead_zone) / (32767.0f - (f32)dead_zone) / magnitude;
    *out_x = bmf_clampf(x * scale, -1.0f, 1.0f);
    *out_y = bmf_clampf(y * scale, -1.0f, 1.0f);
}

// Trigger threshold: at or below it the trigger reads 0; above it, rescaled to (0, 1].
static f32 win32_trigger(u8 raw) {
    if (raw <= WIN32_XINPUT_GAMEPAD_TRIGGER_THRESHOLD) return 0.0f;
    return (f32)(raw - WIN32_XINPUT_GAMEPAD_TRIGGER_THRESHOLD) / (f32)(255 - WIN32_XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
}

static const struct { u16 xinput; u32 bmf; } WIN32_GAMEPAD_BUTTONS[14] = {
    { WIN32_XINPUT_GAMEPAD_A, BMF_GAMEPAD_A }, { WIN32_XINPUT_GAMEPAD_B, BMF_GAMEPAD_B },
    { WIN32_XINPUT_GAMEPAD_X, BMF_GAMEPAD_X }, { WIN32_XINPUT_GAMEPAD_Y, BMF_GAMEPAD_Y },
    { WIN32_XINPUT_GAMEPAD_LEFT_SHOULDER, BMF_GAMEPAD_LEFT_SHOULDER },
    { WIN32_XINPUT_GAMEPAD_RIGHT_SHOULDER, BMF_GAMEPAD_RIGHT_SHOULDER },
    { WIN32_XINPUT_GAMEPAD_BACK, BMF_GAMEPAD_BACK }, { WIN32_XINPUT_GAMEPAD_START, BMF_GAMEPAD_START },
    { WIN32_XINPUT_GAMEPAD_LEFT_THUMB, BMF_GAMEPAD_LEFT_STICK },
    { WIN32_XINPUT_GAMEPAD_RIGHT_THUMB, BMF_GAMEPAD_RIGHT_STICK },
    { WIN32_XINPUT_GAMEPAD_DPAD_UP, BMF_GAMEPAD_DPAD_UP }, { WIN32_XINPUT_GAMEPAD_DPAD_DOWN, BMF_GAMEPAD_DPAD_DOWN },
    { WIN32_XINPUT_GAMEPAD_DPAD_LEFT, BMF_GAMEPAD_DPAD_LEFT }, { WIN32_XINPUT_GAMEPAD_DPAD_RIGHT, BMF_GAMEPAD_DPAD_RIGHT },
};

// ---------------------------------------------------------------- window state
enum { WIN32_GAMEPAD_PROBE_INTERVAL_NS = 1000000000 };   // XInputGetState on an empty slot is slow

static struct {
    HWND hwnd;
    b32 class_registered;
    b32 fullscreen;
    WINDOWPLACEMENT saved_placement;   // windowed placement while fullscreen
    bmf_window_size size;
    b32 minimized;
    u64 keys_down[2];                  // bit per bmf_key
    u32 mouse_buttons_down;            // bit per bmf_mouse_button
    i32 raw_dx, raw_dy;                // raw mouse motion summed during one poll
    struct { u32 id; b32 active; bmf_pointer_kind kind; } pointers[BMF_POINTER_MAX];
    bmf_win32_utf16_decoder text;
    HMODULE xinput;
    XInputGetState_fn xinput_get_state;
    bmf_gamepad gamepads[BMF_GAMEPAD_MAX];
    i64 gamepad_next_probe_ns[BMF_GAMEPAD_MAX];
} win32_window;

static void win32_push_simple(bmf_event_type type) {
    bmf_event event;
    bmf_memset(&event, 0, sizeof event);
    event.type = type;
    platform_event_push(&event);
}

static void win32_push_key(bmf_event_type type, bmf_key key, b32 repeat) {
    bmf_event event;
    bmf_memset(&event, 0, sizeof event);
    event.type = type;
    event.key.key = key;
    event.key.repeat = repeat;
    platform_event_push(&event);
}

static void win32_push_mouse_button(bmf_event_type type, bmf_mouse_button button) {
    bmf_event event;
    bmf_memset(&event, 0, sizeof event);
    event.type = type;
    event.mouse.button = button;
    platform_event_push(&event);
}

static void win32_push_pointer(bmf_event_type type, i32 slot, f32 x, f32 y, f32 pressure) {
    bmf_event event;
    bmf_memset(&event, 0, sizeof event);
    event.type = type;
    event.pointer.slot = slot;
    event.pointer.kind = win32_window.pointers[slot].kind;
    event.pointer.x = x;
    event.pointer.y = y;
    event.pointer.pressure = pressure;
    platform_event_push(&event);
}

static void win32_push_size(void) {
    bmf_event event;
    bmf_memset(&event, 0, sizeof event);
    event.type = BMF_EVENT_RESIZED;
    event.size = win32_window.size;
    platform_event_push(&event);
}

// Focus loss: release everything that is held, so no key stays stuck down while another window
// has the keyboard.
static void win32_release_all(HWND hwnd) {
    for (i32 k = 1; k < BMF_KEY_COUNT; k++) {
        u64 bit = 1ull << (k & 63);
        if (!(win32_window.keys_down[k >> 6] & bit)) continue;
        win32_window.keys_down[k >> 6] &= ~bit;
        win32_push_key(BMF_EVENT_KEY_UP, (bmf_key)k, 0);
    }
    for (i32 b = 0; b < BMF_MOUSE_BUTTON_COUNT; b++)
        if (win32_window.mouse_buttons_down & (1u << b)) win32_push_mouse_button(BMF_EVENT_MOUSE_BUTTON_UP, (bmf_mouse_button)b);
    if (win32_window.mouse_buttons_down) ReleaseCapture();
    win32_window.mouse_buttons_down = 0;
    for (i32 s = 0; s < BMF_POINTER_MAX; s++) {
        if (!win32_window.pointers[s].active) continue;
        win32_push_pointer(BMF_EVENT_POINTER_UP, s, 0.0f, 0.0f, 0.0f);
        win32_window.pointers[s].active = 0;
    }
    win32_window.text.pending_high = 0;
}

static void win32_mouse_button(HWND hwnd, bmf_mouse_button button, b32 down) {
    u32 bit = 1u << button;
    if (down) {
        if (!win32_window.mouse_buttons_down) SetCapture(hwnd);   // keep receiving the release outside the window
        win32_window.mouse_buttons_down |= bit;
        win32_push_mouse_button(BMF_EVENT_MOUSE_BUTTON_DOWN, button);
    } else {
        if (!(win32_window.mouse_buttons_down & bit)) return;
        win32_window.mouse_buttons_down &= ~bit;
        if (!win32_window.mouse_buttons_down) ReleaseCapture();
        win32_push_mouse_button(BMF_EVENT_MOUSE_BUTTON_UP, button);
    }
}

// WM_POINTERDOWN/UPDATE/UP/CAPTURECHANGED for touch and pen. Returns 0 for mouse pointers (the
// caller passes those to DefWindowProc; with EnableMouseInPointer(FALSE) they do not occur).
static b32 win32_pointer_message(HWND hwnd, u32 message, WPARAM wparam) {
    u32 id = (u32)(wparam & 0xFFFF);
    u32 type = 0;
    if (!GetPointerType(id, &type)) return 0;
    if (type != WIN32_PT_TOUCH && type != WIN32_PT_PEN) return 0;
    POINTER_INFO info;
    f32 pressure = 1.0f;
    if (type == WIN32_PT_PEN) {
        POINTER_PEN_INFO pen;
        if (!GetPointerPenInfo(id, &pen)) return 1;
        info = pen.pointerInfo;
        if (pen.pressure > 0) pressure = (f32)pen.pressure / 1024.0f;
    } else {
        if (!GetPointerInfo(id, &info)) return 1;
    }
    POINT point = info.ptPixelLocation;
    ScreenToClient(hwnd, &point);
    f32 x = (f32)point.x, y = (f32)point.y;
    i32 slot = -1;
    for (i32 s = 0; s < BMF_POINTER_MAX; s++)
        if (win32_window.pointers[s].active && win32_window.pointers[s].id == id) slot = s;
    b32 ended = message == WIN32_WM_POINTERUP || message == WIN32_WM_POINTERCAPTURECHANGED ||
                (info.pointerFlags & WIN32_POINTER_FLAG_CANCELED);
    if (message == WIN32_WM_POINTERDOWN && slot < 0) {
        for (i32 s = 0; s < BMF_POINTER_MAX && slot < 0; s++) if (!win32_window.pointers[s].active) slot = s;
        if (slot < 0) return 1;   // more than BMF_POINTER_MAX contacts: ignored
        win32_window.pointers[slot].id = id;
        win32_window.pointers[slot].active = 1;
        win32_window.pointers[slot].kind = type == WIN32_PT_PEN ? BMF_POINTER_PEN : BMF_POINTER_TOUCH;
        win32_push_pointer(BMF_EVENT_POINTER_DOWN, slot, x, y, pressure);
        return 1;
    }
    if (slot < 0) return 1;   // hover or a contact we do not track
    if (ended) {
        win32_push_pointer(BMF_EVENT_POINTER_UP, slot, x, y, 0.0f);
        win32_window.pointers[slot].active = 0;
    } else if (info.pointerFlags & WIN32_POINTER_FLAG_INCONTACT) {
        win32_push_pointer(BMF_EVENT_POINTER_MOVE, slot, x, y, pressure);
    }
    return 1;
}

static LRESULT win32_window_proc(HWND hwnd, u32 message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WIN32_WM_KEYDOWN: case WIN32_WM_SYSKEYDOWN: case WIN32_WM_KEYUP: case WIN32_WM_SYSKEYUP: {
        u32 scancode = (u32)(lparam >> 16) & 0x1FF;   // bits 16..23 scan code, bit 24 extended
        if ((scancode & 0xFF) == 0) {                  // synthesized input without a scan code
            u32 mapped = MapVirtualKeyW((u32)wparam, WIN32_MAPVK_VK_TO_VSC_EX);
            scancode = (mapped & 0xFF) | ((mapped >> 8) == 0xE0 ? 0x100u : 0u);
        }
        bmf_key key = bmf_win32_key_from_scancode(scancode, (u32)wparam);
        b32 down = message == WIN32_WM_KEYDOWN || message == WIN32_WM_SYSKEYDOWN;
        if (key != BMF_KEY_NONE) {
            u64 bit = 1ull << (key & 63);
            if (down) {
                b32 repeat = (win32_window.keys_down[key >> 6] & bit) != 0;
                win32_window.keys_down[key >> 6] |= bit;
                win32_push_key(BMF_EVENT_KEY_DOWN, key, repeat);
            } else if (win32_window.keys_down[key >> 6] & bit) {
                win32_window.keys_down[key >> 6] &= ~bit;
                win32_push_key(BMF_EVENT_KEY_UP, key, 0);
            }
        }
        // System keys still go to DefWindowProc so Alt+F4 closes; SC_KEYMENU is blocked below.
        if (message == WIN32_WM_SYSKEYDOWN || message == WIN32_WM_SYSKEYUP) break;
        return 0;
    }
    case WIN32_WM_CHAR: {
        u32 codepoints[2];
        i32 count = bmf_win32_utf16_feed(&win32_window.text, (u16)wparam, codepoints);
        for (i32 i = 0; i < count; i++) {
            if (codepoints[i] < 0x20 || codepoints[i] == 0x7F) continue;   // Enter, Tab, Backspace, Ctrl+letters: keys, not text
            bmf_event event;
            bmf_memset(&event, 0, sizeof event);
            event.type = BMF_EVENT_TEXT;
            event.text.codepoint = codepoints[i];
            platform_event_push(&event);
        }
        return 0;
    }
    case WIN32_WM_SYSCHAR: return 0;
    case WIN32_WM_SYSCOMMAND:
        if ((wparam & 0xFFF0) == WIN32_SC_KEYMENU) return 0;   // Alt/F10 would enter the (absent) menu and freeze the loop
        break;
    case WIN32_WM_MENUCHAR: return (LRESULT)WIN32_MNC_CLOSE << 16;   // no beep on Alt+letter

    case WIN32_WM_MOUSEMOVE: {
        bmf_event event;
        bmf_memset(&event, 0, sizeof event);
        event.type = BMF_EVENT_MOUSE_MOVE;
        event.position.x = (f32)(i16)(lparam & 0xFFFF);
        event.position.y = (f32)(i16)((lparam >> 16) & 0xFFFF);
        platform_event_push(&event);
        return 0;
    }
    case WIN32_WM_LBUTTONDOWN: win32_mouse_button(hwnd, BMF_MOUSE_BUTTON_LEFT, 1); return 0;
    case WIN32_WM_LBUTTONUP:   win32_mouse_button(hwnd, BMF_MOUSE_BUTTON_LEFT, 0); return 0;
    case WIN32_WM_RBUTTONDOWN: win32_mouse_button(hwnd, BMF_MOUSE_BUTTON_RIGHT, 1); return 0;
    case WIN32_WM_RBUTTONUP:   win32_mouse_button(hwnd, BMF_MOUSE_BUTTON_RIGHT, 0); return 0;
    case WIN32_WM_MBUTTONDOWN: win32_mouse_button(hwnd, BMF_MOUSE_BUTTON_MIDDLE, 1); return 0;
    case WIN32_WM_MBUTTONUP:   win32_mouse_button(hwnd, BMF_MOUSE_BUTTON_MIDDLE, 0); return 0;
    case WIN32_WM_XBUTTONDOWN: case WIN32_WM_XBUTTONUP: {
        bmf_mouse_button button = ((wparam >> 16) & 0xFFFF) == WIN32_XBUTTON2 ? BMF_MOUSE_BUTTON_X2 : BMF_MOUSE_BUTTON_X1;
        win32_mouse_button(hwnd, button, message == WIN32_WM_XBUTTONDOWN);
        return 1;   // X buttons: TRUE means handled
    }
    case WIN32_WM_MOUSEWHEEL: case WIN32_WM_MOUSEHWHEEL: {
        f32 notches = (f32)(i16)((wparam >> 16) & 0xFFFF) / (f32)WIN32_WHEEL_DELTA;
        bmf_event event;
        bmf_memset(&event, 0, sizeof event);
        event.type = BMF_EVENT_MOUSE_WHEEL;
        if (message == WIN32_WM_MOUSEWHEEL) event.delta.y = notches;
        else event.delta.x = notches;
        platform_event_push(&event);
        return 0;
    }
    case WIN32_WM_INPUT: {
        RAWINPUT raw;
        u32 size = sizeof raw;
        if (GetRawInputData((HRAWINPUT)lparam, WIN32_RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != (u32)-1 &&
            raw.header.dwType == WIN32_RIM_TYPEMOUSE && !(raw.data.mouse.usFlags & WIN32_MOUSE_MOVE_ABSOLUTE)) {
            win32_window.raw_dx += raw.data.mouse.lLastX;
            win32_window.raw_dy += raw.data.mouse.lLastY;
        }
        break;   // DefWindowProc must see WM_INPUT to release the input
    }
    case WIN32_WM_POINTERDOWN: case WIN32_WM_POINTERUPDATE: case WIN32_WM_POINTERUP: case WIN32_WM_POINTERCAPTURECHANGED:
        if (win32_pointer_message(hwnd, message, wparam)) return 0;   // handled: no mouse emulation
        break;

    case WIN32_WM_SETFOCUS: win32_push_simple(BMF_EVENT_FOCUS_GAINED); return 0;
    case WIN32_WM_KILLFOCUS:
        win32_release_all(hwnd);
        win32_push_simple(BMF_EVENT_FOCUS_LOST);
        return 0;
    case WIN32_WM_SIZE: {
        if (wparam == WIN32_SIZE_MINIMIZED) {
            if (!win32_window.minimized) { win32_window.minimized = 1; win32_push_simple(BMF_EVENT_SUSPENDED); }
            return 0;
        }
        if (win32_window.minimized) { win32_window.minimized = 0; win32_push_simple(BMF_EVENT_RESUMED); }
        i32 width = (i32)(lparam & 0xFFFF), height = (i32)((lparam >> 16) & 0xFFFF);
        if (width != win32_window.size.width || height != win32_window.size.height) {
            win32_window.size.width = width;
            win32_window.size.height = height;
            win32_push_size();
        }
        return 0;
    }
    case WIN32_WM_DPICHANGED: {
        win32_window.size.dpi_scale = (f32)(wparam & 0xFFFF) / (f32)WIN32_USER_DEFAULT_SCREEN_DPI;
        const RECT* suggested = (const RECT*)lparam;
        SetWindowPos(hwnd, 0, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, WIN32_SWP_NOZORDER | WIN32_SWP_NOACTIVATE);
        win32_push_size();   // the scale changed even if the pixel size did not
        return 0;
    }
    case WIN32_WM_CLOSE: win32_push_simple(BMF_EVENT_CLOSE_REQUESTED); return 0;
    case WIN32_WM_ERASEBKGND: return 1;   // the GPU covers the client area
    default: break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

// ---------------------------------------------------------------- window
static const WCHAR WIN32_WINDOW_CLASS[] = u"bmf_window";

b32 bmf_platform_window_open(const bmf_window_desc* desc) {
    BMF_ASSERT(win32_window.hwnd == 0 && desc->width > 0 && desc->height > 0);
    // Both fail harmlessly when already set for this process.
    SetProcessDpiAwarenessContext(WIN32_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    EnableMouseInPointer(0);   // mouse stays on WM_MOUSE*/Raw Input; WM_POINTER* is touch and pen only
    HINSTANCE instance = (HINSTANCE)GetModuleHandleW(0);
    if (!win32_window.class_registered) {
        WNDCLASSEXW window_class;
        bmf_memset(&window_class, 0, sizeof window_class);
        window_class.cbSize = sizeof window_class;
        window_class.style = WIN32_CS_HREDRAW | WIN32_CS_VREDRAW;
        window_class.lpfnWndProc = win32_window_proc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorW(0, WIN32_IDC_ARROW);
        window_class.lpszClassName = WIN32_WINDOW_CLASS;
        if (!RegisterClassExW(&window_class)) return 0;
        win32_window.class_registered = 1;
    }
    WCHAR title[256];
    i32 title_len = bmf_utf8_to_utf16(bmf_str_from_cstr(desc->title ? desc->title : "bmf"), title, 255);
    title[title_len < 255 ? title_len : 255] = 0;
    u32 style = WIN32_WS_OVERLAPPEDWINDOW;
    u32 ex_style = WIN32_WS_EX_APPWINDOW | WIN32_WS_EX_NOREDIRECTIONBITMAP;
    win32_window.size.width = 0;
    win32_window.size.height = 0;
    win32_window.minimized = 0;
    win32_window.fullscreen = 0;
    HWND hwnd = CreateWindowExW(ex_style, WIN32_WINDOW_CLASS, title, style, WIN32_CW_USEDEFAULT, WIN32_CW_USEDEFAULT,
                                WIN32_CW_USEDEFAULT, WIN32_CW_USEDEFAULT, 0, 0, instance, 0);
    if (!hwnd) return 0;
    win32_window.hwnd = hwnd;
    // Size the client area in logical pixels for the monitor the window landed on.
    u32 dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) dpi = WIN32_USER_DEFAULT_SCREEN_DPI;
    win32_window.size.dpi_scale = (f32)dpi / (f32)WIN32_USER_DEFAULT_SCREEN_DPI;
    RECT rect = { 0, 0, (i32)((i64)desc->width * dpi / WIN32_USER_DEFAULT_SCREEN_DPI),
                  (i32)((i64)desc->height * dpi / WIN32_USER_DEFAULT_SCREEN_DPI) };
    AdjustWindowRectExForDpi(&rect, style, 0, ex_style, dpi);
    SetWindowPos(hwnd, 0, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 WIN32_SWP_NOMOVE | WIN32_SWP_NOZORDER | WIN32_SWP_NOACTIVATE);
    RECT client;
    GetClientRect(hwnd, &client);
    win32_window.size.width = client.right - client.left;
    win32_window.size.height = client.bottom - client.top;

    RAWINPUTDEVICE mouse = { WIN32_HID_USAGE_PAGE_GENERIC, WIN32_HID_USAGE_GENERIC_MOUSE, 0, hwnd };
    RegisterRawInputDevices(&mouse, 1, sizeof mouse);   // without it there is no relative motion, nothing else breaks

    if (!win32_window.xinput) {
        win32_window.xinput = LoadLibraryW(u"xinput1_4.dll");
        if (win32_window.xinput) win32_window.xinput_get_state = (XInputGetState_fn)GetProcAddress(win32_window.xinput, "XInputGetState");
    }
    if (!desc->hidden) ShowWindow(hwnd, WIN32_SW_SHOW);
    return 1;
}

void bmf_platform_window_close(void) {
    if (!win32_window.hwnd) return;
    DestroyWindow(win32_window.hwnd);
    win32_window.hwnd = 0;
    win32_window.size.width = 0;
    win32_window.size.height = 0;
    win32_window.size.dpi_scale = 0.0f;
    win32_window.fullscreen = 0;
    win32_window.keys_down[0] = win32_window.keys_down[1] = 0;
    win32_window.mouse_buttons_down = 0;
    bmf_memset(win32_window.pointers, 0, sizeof win32_window.pointers);
    bmf_memset(win32_window.gamepads, 0, sizeof win32_window.gamepads);
}

bmf_window_size bmf_platform_window_size(void) {
    return win32_window.size;
}

void* bmf_platform_window_native(void) {
    return win32_window.hwnd;
}

b32 bmf_platform_window_is_fullscreen(void) {
    return win32_window.fullscreen;
}

void bmf_platform_window_set_fullscreen(b32 fullscreen) {
    HWND hwnd = win32_window.hwnd;
    fullscreen = fullscreen != 0;
    if (!hwnd || fullscreen == win32_window.fullscreen) return;
    u32 visible = (u32)GetWindowLongPtrW(hwnd, WIN32_GWL_STYLE) & WIN32_WS_VISIBLE;
    if (fullscreen) {
        win32_window.saved_placement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd, &win32_window.saved_placement);
        MONITORINFO monitor;
        bmf_memset(&monitor, 0, sizeof monitor);
        monitor.cbSize = sizeof monitor;
        GetMonitorInfoW(MonitorFromWindow(hwnd, WIN32_MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowLongPtrW(hwnd, WIN32_GWL_STYLE, (i64)((u32)WIN32_WS_POPUP | visible));
        SetWindowPos(hwnd, WIN32_HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                     monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                     WIN32_SWP_FRAMECHANGED | WIN32_SWP_NOOWNERZORDER);
    } else {
        SetWindowLongPtrW(hwnd, WIN32_GWL_STYLE, (i64)((u32)WIN32_WS_OVERLAPPEDWINDOW | visible));
        WINDOWPLACEMENT placement = win32_window.saved_placement;
        if (!visible) placement.showCmd = WIN32_SW_HIDE;   // restoring must not show a hidden window
        SetWindowPlacement(hwnd, &placement);
        SetWindowPos(hwnd, 0, 0, 0, 0, 0, WIN32_SWP_NOMOVE | WIN32_SWP_NOSIZE | WIN32_SWP_NOZORDER |
                     WIN32_SWP_NOOWNERZORDER | WIN32_SWP_FRAMECHANGED);
    }
    win32_window.fullscreen = fullscreen;
}

// ---------------------------------------------------------------- polling
static void win32_poll_gamepads(void) {
    if (!win32_window.xinput_get_state) return;
    i64 now = bmf_platform_time_ns();
    for (i32 i = 0; i < BMF_GAMEPAD_MAX; i++) {
        bmf_gamepad* pad = &win32_window.gamepads[i];
        if (!pad->connected && now < win32_window.gamepad_next_probe_ns[i]) continue;
        XINPUT_STATE state;
        bmf_memset(&state, 0, sizeof state);
        if (win32_window.xinput_get_state((u32)i, &state) != WIN32_ERROR_SUCCESS) {
            win32_window.gamepad_next_probe_ns[i] = now + WIN32_GAMEPAD_PROBE_INTERVAL_NS;
            if (pad->connected) {
                bmf_memset(pad, 0, sizeof *pad);
                bmf_event event;
                bmf_memset(&event, 0, sizeof event);
                event.type = BMF_EVENT_GAMEPAD_DISCONNECTED;
                event.gamepad.index = i;
                platform_event_push(&event);
            }
            continue;
        }
        if (!pad->connected) {
            pad->connected = 1;
            bmf_event event;
            bmf_memset(&event, 0, sizeof event);
            event.type = BMF_EVENT_GAMEPAD_CONNECTED;
            event.gamepad.index = i;
            platform_event_push(&event);
        }
        const XINPUT_GAMEPAD* raw = &state.Gamepad;
        pad->buttons = 0;
        for (i32 b = 0; b < 14; b++) if (raw->wButtons & WIN32_GAMEPAD_BUTTONS[b].xinput) pad->buttons |= WIN32_GAMEPAD_BUTTONS[b].bmf;
        bmf_win32_stick(raw->sThumbLX, raw->sThumbLY, WIN32_XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, &pad->left_x, &pad->left_y);
        bmf_win32_stick(raw->sThumbRX, raw->sThumbRY, WIN32_XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE, &pad->right_x, &pad->right_y);
        pad->left_trigger = win32_trigger(raw->bLeftTrigger);
        pad->right_trigger = win32_trigger(raw->bRightTrigger);
    }
}

void bmf_platform_poll_events(void) {
    MSG msg;
    while (PeekMessageW(&msg, 0, 0, 0, WIN32_PM_REMOVE)) {
        TranslateMessage(&msg);   // WM_KEYDOWN -> WM_CHAR
        DispatchMessageW(&msg);
    }
    if (win32_window.raw_dx || win32_window.raw_dy) {
        bmf_event event;
        bmf_memset(&event, 0, sizeof event);
        event.type = BMF_EVENT_MOUSE_DELTA;
        event.delta.x = (f32)win32_window.raw_dx;
        event.delta.y = (f32)win32_window.raw_dy;
        platform_event_push(&event);
        win32_window.raw_dx = 0;
        win32_window.raw_dy = 0;
    }
    if (win32_window.hwnd) win32_poll_gamepads();
}

b32 bmf_platform_gamepad_get(i32 index, bmf_gamepad* out) {
    BMF_ASSERT(index >= 0 && index < BMF_GAMEPAD_MAX);
    *out = win32_window.gamepads[index];
    return out->connected;
}
