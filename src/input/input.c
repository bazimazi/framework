// input.c: action table, bindings, device state and per-tick sampling (task 1A-22). See input.h
// for the contract and README.md for the data layout.

enum {
    INPUT_OWNER_NONE = 0,       // pointer slot free (it may still report a release)
    INPUT_OWNER_MOUSE = 1,
    INPUT_OWNER_TOUCH = 2,      // a platform touch/pen contact (touch_source)
    INPUT_OWNER_INJECTED = 3,   // bmf_input_inject_pointer
};
enum { INPUT_MOUSE_POINTER_ID = 1, INPUT_FIRST_TOUCH_ID = 2 };
enum { INPUT_PAD_LEFT_TRIGGER_BIT = 14, INPUT_PAD_RIGHT_TRIGGER_BIT = 15 };   // after the 14 bmf_gamepad_button bits

// A level with counted presses, as used by buttons and pointers: `level` is the device (or
// injected) state now, `press_count` the rising edges not yet given to a tick, `previous_down`
// what the last sampled tick reported.
typedef struct input_pointer_slot {
    i32 owner;            // INPUT_OWNER_*
    i32 touch_source;     // platform pointer slot while owner == INPUT_OWNER_TOUCH
    u32 id;               // reported id; 0 = none
    bmf_v2 screen;        // logical pixels
    b32 level;
    i32 press_count;
    b32 previous_down;
} input_pointer_slot;

static struct {
    // The action table (input.h bmf_input_init).
    const bmf_action* actions;
    i32 action_count;
    i32 button_count, axis_count;
    b32 has_pointer;
    i32 slot_of[BMF_INPUT_MAX_ACTIONS];        // bit, axis index or 0, per table index
    i32 button_action[BMF_INPUT_MAX_BUTTONS];  // table index per bit
    i32 axis_action[BMF_INPUT_MAX_AXES];       // table index per axis
    u16 bindings[BMF_INPUT_MAX_ACTIONS][BMF_ACTION_MAX_BINDINGS];

    bmf_draw2d_view view;
    bmf_camera pointer_camera;

    // Device state.
    u64 keys[2];                  // bit per bmf_key
    u32 mouse_buttons;            // bit per bmf_mouse_button
    u32 pad_buttons;              // bmf_gamepad_button bits of every connected pad, plus the trigger bits
    bmf_v2 pad_left_stick, pad_right_stick;
    b32 mouse_seen;
    bmf_v2 mouse_screen;          // logical pixels

    // Button actions (bit per action).
    u64 button_level;             // bindings or injection down, as of the last update
    u64 injected_buttons;
    u64 previous_down;
    u8 press_count[BMF_INPUT_MAX_BUTTONS];

    // Axis injection.
    u8 axis_injected[BMF_INPUT_MAX_AXES];
    bmf_v2 axis_injected_value[BMF_INPUT_MAX_AXES];

    input_pointer_slot pointers[BMF_INPUT_MAX_POINTERS];
    u32 next_touch_id;

    u32 text[BMF_INPUT_TEXT_QUEUE];
    i32 text_count;
    i32 text_dropped;
} input;

// ---------------------------------------------------------------- bindings
static b32 input_source_is_digital(u32 source) {
    return (source >= 1 && source < (u32)BMF_KEY_COUNT) ||
           (source >= BMF_MOUSE_LEFT && source <= BMF_MOUSE_X2) ||
           (source >= BMF_PAD_A && source <= BMF_PAD_RIGHT_TRIGGER);
}

static b32 input_binding_valid(bmf_action_type type, u16 binding) {
    if (binding == 0) return 1;
    u32 source = binding & BMF_BINDING_SOURCE_MASK;
    u32 direction = (binding & BMF_BINDING_DIRECTION_MASK) >> 12;
    if (binding & 0x8000u) return 0;
    b32 two_dimensional = source >= BMF_KEYS_WASD && source <= BMF_PAD_DPAD;
    if (type == BMF_ACTION_BUTTON) return input_source_is_digital(source) && direction == 0;
    if (type == BMF_ACTION_AXIS2) return (input_source_is_digital(source) && direction >= 1 && direction <= 4) ||
                                         (two_dimensional && direction == 0);
    return 0;   // POINTER actions take no bindings
}

static b32 input_key_down(u32 key) { return (b32)((input.keys[key >> 6] >> (key & 63)) & 1u); }

static b32 input_source_down(u32 source) {
    if (source >= 1 && source < (u32)BMF_KEY_COUNT) return input_key_down(source);
    if (source >= BMF_MOUSE_LEFT && source <= BMF_MOUSE_X2) return (b32)((input.mouse_buttons >> (source - BMF_MOUSE_LEFT)) & 1u);
    if (source >= BMF_PAD_A && source <= BMF_PAD_RIGHT_TRIGGER) return (b32)((input.pad_buttons >> (source - BMF_PAD_A)) & 1u);
    return 0;
}

// Recomputes every button action's level from its bindings and injection; each rise counts as
// a press for the next ticks.
static void input_update_buttons(void) {
    for (i32 bit = 0; bit < input.button_count; bit++) {
        i32 action = input.button_action[bit];
        u64 mask = (u64)1 << bit;
        b32 level = (input.injected_buttons & mask) != 0;
        for (i32 b = 0; b < BMF_ACTION_MAX_BINDINGS && !level; b++) {
            u16 binding = input.bindings[action][b];
            if (binding) level = input_source_down(binding & BMF_BINDING_SOURCE_MASK);
        }
        if (level && !(input.button_level & mask)) {
            if (input.press_count[bit] < BMF_INPUT_MAX_PENDING_PRESSES) input.press_count[bit]++;
            input.button_level |= mask;
        } else if (!level) {
            input.button_level &= ~mask;
        }
    }
}

// ---------------------------------------------------------------- init
static void input_reset_devices(void) {
    input.view.width = 0;
    input.view.height = 0;
    input.view.dpi_scale = 1.0f;
    input.pointer_camera = bmf_camera_world_default();
    bmf_memset(input.keys, 0, sizeof input.keys);
    input.mouse_buttons = 0;
    input.pad_buttons = 0;
    input.pad_left_stick = bmf_v2_make(0.0f, 0.0f);
    input.pad_right_stick = bmf_v2_make(0.0f, 0.0f);
    input.mouse_seen = 0;
    input.mouse_screen = bmf_v2_make(0.0f, 0.0f);
    input.button_level = 0;
    input.injected_buttons = 0;
    input.previous_down = 0;
    bmf_memset(input.press_count, 0, sizeof input.press_count);
    bmf_memset(input.axis_injected, 0, sizeof input.axis_injected);
    bmf_memset(input.axis_injected_value, 0, sizeof input.axis_injected_value);
    bmf_memset(input.pointers, 0, sizeof input.pointers);
    input.next_touch_id = INPUT_FIRST_TOUCH_ID;
    input.text_count = 0;
    input.text_dropped = 0;
}

static b32 input_name_valid(const char* name) {
    if (!name) return 0;
    i32 length = 0;
    for (const char* c = name; *c; c++, length++) {
        b32 ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ||
                 *c == '_' || *c == '.' || *c == '-';
        if (!ok || length >= BMF_ACTION_NAME_MAX) return 0;
    }
    return length > 0;
}

static const char* const INPUT_TYPE_NAMES[BMF_ACTION_TYPE_COUNT] = { "BUTTON", "AXIS2", "POINTER" };

bmf_err bmf_input_check_actions(const bmf_action* actions, i32 action_count, char* error, i32 error_capacity) {
    BMF_ASSERT(error && error_capacity > 0);
    error[0] = 0;

    if (action_count < 0 || action_count > BMF_INPUT_MAX_ACTIONS || (action_count > 0 && !actions)) {
        bmf_fmt_buf(error, error_capacity, "input: action_count %i out of range 0..%i (or no table)", action_count, BMF_INPUT_MAX_ACTIONS);
        return BMF_ERR_INVALID;
    }
    i32 buttons = 0, axes = 0, pointers = 0;
    for (i32 a = 0; a < action_count; a++) {
        const bmf_action* action = &actions[a];
        const char* name = action->name ? action->name : "(null)";
        if (!input_name_valid(action->name)) {
            bmf_fmt_buf(error, error_capacity, "input: action %i \"%s\": name must be 1..%i bytes of a-z A-Z 0-9 _ . -", a, name, BMF_ACTION_NAME_MAX);
            return BMF_ERR_INVALID;
        }
        for (i32 other = 0; other < a; other++) {
            if (bmf_cstr_eq(actions[other].name, action->name)) {
                bmf_fmt_buf(error, error_capacity, "input: action %i \"%s\": duplicate name (also action %i)", a, name, other);
                return BMF_ERR_INVALID;
            }
        }
        if ((u32)action->type >= BMF_ACTION_TYPE_COUNT) {
            bmf_fmt_buf(error, error_capacity, "input: action %i \"%s\": unknown type %i", a, name, (i32)action->type);
            return BMF_ERR_INVALID;
        }
        if (action->type == BMF_ACTION_BUTTON && ++buttons > BMF_INPUT_MAX_BUTTONS) {
            bmf_fmt_buf(error, error_capacity, "input: action %i \"%s\": more than %i BUTTON actions", a, name, BMF_INPUT_MAX_BUTTONS);
            return BMF_ERR_INVALID;
        }
        if (action->type == BMF_ACTION_AXIS2 && ++axes > BMF_INPUT_MAX_AXES) {
            bmf_fmt_buf(error, error_capacity, "input: action %i \"%s\": more than %i AXIS2 actions", a, name, BMF_INPUT_MAX_AXES);
            return BMF_ERR_INVALID;
        }
        if (action->type == BMF_ACTION_POINTER && ++pointers > BMF_INPUT_MAX_POINTER_ACTIONS) {
            bmf_fmt_buf(error, error_capacity, "input: action %i \"%s\": more than %i POINTER action", a, name, BMF_INPUT_MAX_POINTER_ACTIONS);
            return BMF_ERR_INVALID;
        }
        for (i32 b = 0; b < BMF_ACTION_MAX_BINDINGS; b++) {
            if (!input_binding_valid(action->type, action->bindings[b])) {
                bmf_fmt_buf(error, error_capacity, "input: action %i \"%s\": binding %i (0x%x) is not valid for a %s action",
                            a, name, b, (u32)action->bindings[b], INPUT_TYPE_NAMES[action->type]);
                return BMF_ERR_INVALID;
            }
        }
    }
    return BMF_OK;
}

bmf_err bmf_input_init(const bmf_action* actions, i32 action_count, char* error, i32 error_capacity) {
    input.actions = 0;
    input.action_count = 0;
    input.button_count = 0;
    input.axis_count = 0;
    input.has_pointer = 0;
    input_reset_devices();
    bmf_err err = bmf_input_check_actions(actions, action_count, error, error_capacity);
    if (err != BMF_OK) return err;

    input.actions = actions;
    input.action_count = action_count;
    for (i32 a = 0; a < action_count; a++) {
        switch (actions[a].type) {
        case BMF_ACTION_BUTTON:
            input.slot_of[a] = input.button_count;
            input.button_action[input.button_count++] = a;
            break;
        case BMF_ACTION_AXIS2:
            input.slot_of[a] = input.axis_count;
            input.axis_action[input.axis_count++] = a;
            break;
        default:
            input.slot_of[a] = 0;
            input.has_pointer = 1;
            break;
        }
    }
    bmf_input_reset_bindings();
    return BMF_OK;
}

void bmf_input_set_view(bmf_draw2d_view view) {
    BMF_ASSERT(view.dpi_scale > 0.0f && view.width >= 0 && view.height >= 0);
    input.view = view;
}

i32 bmf_input_text_dropped(void) { return input.text_dropped; }

// ---------------------------------------------------------------- pointers
static void input_pointer_set_level(input_pointer_slot* slot, b32 level) {
    if (level && !slot->level && slot->press_count < BMF_INPUT_MAX_PENDING_PRESSES) slot->press_count++;
    slot->level = level;
}

// The mouse owns slot 0 unless a touch contact or an injection holds it, or a finished one still
// has a press or release to report (its id must stay until then).
static input_pointer_slot* input_mouse_slot(void) {
    input_pointer_slot* slot = &input.pointers[0];
    if (slot->owner == INPUT_OWNER_TOUCH || slot->owner == INPUT_OWNER_INJECTED) return 0;
    if (slot->owner == INPUT_OWNER_NONE && slot->id != 0 && (slot->previous_down || slot->press_count > 0)) return 0;
    slot->owner = INPUT_OWNER_MOUSE;
    slot->id = INPUT_MOUSE_POINTER_ID;
    slot->screen = input.mouse_screen;
    return slot;
}

// A slot for a new touch contact: the lowest one that is free (or the mouse's while its button is
// up), preferring slots whose previous contact has been fully reported to ticks. -1 if none.
static i32 input_pointer_free_slot(void) {
    i32 fallback = -1;
    for (i32 s = 0; s < BMF_INPUT_MAX_POINTERS; s++) {
        input_pointer_slot* slot = &input.pointers[s];
        b32 free = slot->owner == INPUT_OWNER_NONE || (slot->owner == INPUT_OWNER_MOUSE && !slot->level);
        if (!free) continue;
        if (slot->press_count == 0 && !slot->previous_down) return s;
        if (fallback < 0) fallback = s;
    }
    return fallback;
}

static input_pointer_slot* input_touch_slot(i32 touch_source) {
    for (i32 s = 0; s < BMF_INPUT_MAX_POINTERS; s++)
        if (input.pointers[s].owner == INPUT_OWNER_TOUCH && input.pointers[s].touch_source == touch_source) return &input.pointers[s];
    return 0;
}

// ---------------------------------------------------------------- events
void bmf_input_event(const bmf_event* event) {
    f32 dpi = input.view.dpi_scale;
    switch (event->type) {
    case BMF_EVENT_KEY_DOWN:
    case BMF_EVENT_KEY_UP: {
        u32 key = (u32)event->key.key;
        if (key == 0 || key >= (u32)BMF_KEY_COUNT) return;
        if (event->type == BMF_EVENT_KEY_DOWN && event->key.repeat) return;
        u64 mask = (u64)1 << (key & 63);
        if (event->type == BMF_EVENT_KEY_DOWN) input.keys[key >> 6] |= mask;
        else input.keys[key >> 6] &= ~mask;
        input_update_buttons();
        return;
    }
    case BMF_EVENT_TEXT:
        if (input.text_count < BMF_INPUT_TEXT_QUEUE) input.text[input.text_count++] = event->text.codepoint;
        else input.text_dropped++;
        return;
    case BMF_EVENT_MOUSE_MOVE: {
        input.mouse_seen = 1;
        input.mouse_screen = bmf_v2_make(event->position.x / dpi, event->position.y / dpi);
        input_mouse_slot();
        return;
    }
    case BMF_EVENT_MOUSE_BUTTON_DOWN:
    case BMF_EVENT_MOUSE_BUTTON_UP: {
        u32 button = (u32)event->mouse.button;
        if (button >= (u32)BMF_MOUSE_BUTTON_COUNT) return;
        b32 down = event->type == BMF_EVENT_MOUSE_BUTTON_DOWN;
        if (down) input.mouse_buttons |= 1u << button;
        else input.mouse_buttons &= ~(1u << button);
        input_update_buttons();
        if (button == BMF_MOUSE_BUTTON_LEFT) {
            input_pointer_slot* slot = input_mouse_slot();
            if (slot) input_pointer_set_level(slot, down);
        }
        return;
    }
    case BMF_EVENT_POINTER_DOWN: {
        if (input_touch_slot(event->pointer.slot)) return;   // already tracked (should not happen)
        i32 s = input_pointer_free_slot();
        if (s < 0) return;   // more contacts than slots: ignored, like its later moves and up
        input_pointer_slot* slot = &input.pointers[s];
        slot->owner = INPUT_OWNER_TOUCH;
        slot->touch_source = event->pointer.slot;
        slot->id = input.next_touch_id++;
        slot->screen = bmf_v2_make(event->pointer.x / dpi, event->pointer.y / dpi);
        slot->level = 0;
        input_pointer_set_level(slot, 1);
        return;
    }
    case BMF_EVENT_POINTER_MOVE: {
        input_pointer_slot* slot = input_touch_slot(event->pointer.slot);
        if (slot) slot->screen = bmf_v2_make(event->pointer.x / dpi, event->pointer.y / dpi);
        return;
    }
    case BMF_EVENT_POINTER_UP: {
        // The position is not taken: a release forced by focus loss carries (0, 0).
        input_pointer_slot* slot = input_touch_slot(event->pointer.slot);
        if (!slot) return;
        input_pointer_set_level(slot, 0);
        slot->owner = INPUT_OWNER_NONE;
        return;
    }
    default:
        return;
    }
}

void bmf_input_set_gamepads(const bmf_gamepad* pads, i32 count) {
    BMF_ASSERT(count >= 0 && count <= BMF_GAMEPAD_MAX && (pads || count == 0));
    u32 buttons = 0;
    bmf_v2 left = bmf_v2_make(0.0f, 0.0f), right = left;
    f32 left_length = 0.0f, right_length = 0.0f;
    for (i32 p = 0; p < count; p++) {
        const bmf_gamepad* pad = &pads[p];
        if (!pad->connected) continue;
        buttons |= pad->buttons & 0x3FFFu;
        if (pad->left_trigger >= BMF_INPUT_TRIGGER_DOWN) buttons |= 1u << INPUT_PAD_LEFT_TRIGGER_BIT;
        if (pad->right_trigger >= BMF_INPUT_TRIGGER_DOWN) buttons |= 1u << INPUT_PAD_RIGHT_TRIGGER_BIT;
        bmf_v2 l = bmf_v2_make(pad->left_x, pad->left_y), r = bmf_v2_make(pad->right_x, pad->right_y);
        if (bmf_v2_length_sq(l) > left_length) { left = l; left_length = bmf_v2_length_sq(l); }
        if (bmf_v2_length_sq(r) > right_length) { right = r; right_length = bmf_v2_length_sq(r); }
    }
    input.pad_buttons = buttons;
    input.pad_left_stick = left;
    input.pad_right_stick = right;
    input_update_buttons();
}

// ---------------------------------------------------------------- sampling
// One tick of a counted level (see input_pointer_slot): returns down for this tick. A pending
// press makes a tick down; a press that followed a release (while the last tick was down) first
// shows that release.
static b32 input_edge_step(b32 level, i32* press_count, b32 previous_down) {
    if (previous_down) return *press_count > 0 ? 0 : level;
    if (*press_count > 0) { (*press_count)--; return 1; }
    return level;
}

// Unit-length clamp: vectors longer than 1 are scaled to length 1 (real division, D-37).
static bmf_v2 input_axis_clamp(bmf_v2 v) {
    f32 length_sq = v.x * v.x + v.y * v.y;
    if (length_sq <= 1.0f) return v;
    f32 length = bmf_sqrtf(length_sq);
    return bmf_v2_make(v.x / length, v.y / length);
}

static bmf_v2 input_keys_vector(u32 up, u32 down, u32 left, u32 right, b32 (*is_down)(u32)) {
    return bmf_v2_make((f32)is_down(right) - (f32)is_down(left), (f32)is_down(up) - (f32)is_down(down));
}

static bmf_v2 input_axis_value(i32 axis) {
    if (input.axis_injected[axis]) return input_axis_clamp(input.axis_injected_value[axis]);
    i32 action = input.axis_action[axis];
    bmf_v2 sum = bmf_v2_make(0.0f, 0.0f);
    for (i32 b = 0; b < BMF_ACTION_MAX_BINDINGS; b++) {
        u16 binding = input.bindings[action][b];
        if (!binding) continue;
        u32 source = binding & BMF_BINDING_SOURCE_MASK;
        u32 direction = (binding & BMF_BINDING_DIRECTION_MASK) >> 12;
        bmf_v2 v = bmf_v2_make(0.0f, 0.0f);
        if (direction) {
            if (input_source_down(source)) {
                static const bmf_v2 DIRECTIONS[5] = { { 0, 0 }, { 0, 1 }, { 0, -1 }, { -1, 0 }, { 1, 0 } };
                v = DIRECTIONS[direction];
            }
        } else if (source == BMF_KEYS_WASD) {
            v = input_keys_vector(BMF_KEY_W, BMF_KEY_S, BMF_KEY_A, BMF_KEY_D, input_source_down);
        } else if (source == BMF_KEYS_ARROWS) {
            v = input_keys_vector(BMF_KEY_UP, BMF_KEY_DOWN, BMF_KEY_LEFT, BMF_KEY_RIGHT, input_source_down);
        } else if (source == BMF_PAD_DPAD) {
            v = input_keys_vector(BMF_PAD_DPAD_UP, BMF_PAD_DPAD_DOWN, BMF_PAD_DPAD_LEFT, BMF_PAD_DPAD_RIGHT, input_source_down);
        } else if (source == BMF_PAD_LSTICK) {
            v = input.pad_left_stick;
        } else if (source == BMF_PAD_RSTICK) {
            v = input.pad_right_stick;
        }
        sum = bmf_v2_add(sum, v);
    }
    return input_axis_clamp(sum);
}

void bmf_input_sample_tick(bmf_input* out) {
    bmf_memset(out, 0, sizeof *out);

    for (i32 bit = 0; bit < input.button_count; bit++) {
        u64 mask = (u64)1 << bit;
        i32 press_count = input.press_count[bit];
        b32 previous = (input.previous_down & mask) != 0;
        b32 down = input_edge_step((input.button_level & mask) != 0, &press_count, previous);
        input.press_count[bit] = (u8)press_count;
        if (down) out->down |= mask;
        if (down && !previous) out->pressed |= mask;
        if (!down && previous) out->released |= mask;
    }
    input.previous_down = out->down;

    for (i32 axis = 0; axis < input.axis_count; axis++) out->axis[axis] = input_axis_value(axis);

    if (input.has_pointer) {
        bmf_v2 view_size = bmf_v2_make((f32)input.view.width / input.view.dpi_scale, (f32)input.view.height / input.view.dpi_scale);
        for (i32 s = 0; s < BMF_INPUT_MAX_POINTERS; s++) {
            input_pointer_slot* slot = &input.pointers[s];
            b32 previous = slot->previous_down;
            b32 down = input_edge_step(slot->level, &slot->press_count, previous);
            slot->previous_down = down;
            bmf_pointer* pointer = &out->pointer[s];
            pointer->id = slot->id;
            pointer->down = (u8)down;
            pointer->pressed = (u8)(down && !previous);
            pointer->released = (u8)(!down && previous);
            if (slot->id) {
                pointer->screen = slot->screen;
                pointer->world = bmf_camera_screen_to_world(&input.pointer_camera, view_size, slot->screen);
            }
            // A finished contact disappears once its last press and release have been reported.
            if (slot->owner == INPUT_OWNER_NONE && !down && slot->press_count == 0) slot->id = 0;
        }
    }

    i32 take = input.text_count < BMF_INPUT_MAX_TEXT ? input.text_count : BMF_INPUT_MAX_TEXT;
    for (i32 i = 0; i < take; i++) out->text[i] = input.text[i];
    out->text_len = take;
    for (i32 i = take; i < input.text_count; i++) input.text[i - take] = input.text[i];
    input.text_count -= take;
}

// ---------------------------------------------------------------- game interface
void bmf_input_set_pointer_camera(const bmf_camera* camera) {
    BMF_ASSERT(camera && camera->pixels_per_unit > 0.0f);
    input.pointer_camera = *camera;
}

i32 bmf_input_action_find(bmf_str name) {
    for (i32 a = 0; a < input.action_count; a++)
        if (bmf_str_eq_cstr(name, input.actions[a].name)) return a;
    return -1;
}

bmf_action_type bmf_input_action_type(i32 action) {
    BMF_ASSERT(action >= 0 && action < input.action_count);
    return input.actions[action].type;
}

i32 bmf_input_action_slot(i32 action) {
    BMF_ASSERT(action >= 0 && action < input.action_count);
    return input.slot_of[action];
}

bmf_err bmf_input_bind(i32 action, i32 slot, u16 binding) {
    if (action < 0 || action >= input.action_count || slot < 0 || slot >= BMF_ACTION_MAX_BINDINGS) return BMF_ERR_RANGE;
    if (!input_binding_valid(input.actions[action].type, binding)) return BMF_ERR_INVALID;
    input.bindings[action][slot] = binding;
    return BMF_OK;
}

u16 bmf_input_binding(i32 action, i32 slot) {
    BMF_ASSERT(action >= 0 && action < input.action_count && slot >= 0 && slot < BMF_ACTION_MAX_BINDINGS);
    return input.bindings[action][slot];
}

void bmf_input_reset_bindings(void) {
    for (i32 a = 0; a < input.action_count; a++)
        for (i32 b = 0; b < BMF_ACTION_MAX_BINDINGS; b++) input.bindings[a][b] = input.actions[a].bindings[b];
}

// ---------------------------------------------------------------- injection
void bmf_input_inject_button(i32 action, b32 down) {
    BMF_ASSERT(action >= 0 && action < input.action_count && input.actions[action].type == BMF_ACTION_BUTTON);
    u64 mask = (u64)1 << input.slot_of[action];
    if (down) input.injected_buttons |= mask;
    else input.injected_buttons &= ~mask;
    input_update_buttons();
}

void bmf_input_inject_axis(i32 action, b32 active, bmf_v2 value) {
    BMF_ASSERT(action >= 0 && action < input.action_count && input.actions[action].type == BMF_ACTION_AXIS2);
    BMF_ASSERT(value.x - value.x == 0.0f && value.y - value.y == 0.0f);   // finite
    i32 axis = input.slot_of[action];
    input.axis_injected[axis] = (u8)(active != 0);
    input.axis_injected_value[axis] = active ? value : bmf_v2_make(0.0f, 0.0f);
}

void bmf_input_inject_pointer(i32 slot_index, bmf_input_pointer_change change, bmf_v2 position, b32 camera_space) {
    BMF_ASSERT(slot_index >= 0 && slot_index < BMF_INPUT_MAX_POINTERS);
    BMF_ASSERT(change == BMF_INPUT_POINTER_MOVE || change == BMF_INPUT_POINTER_DOWN || change == BMF_INPUT_POINTER_UP);
    input_pointer_slot* slot = &input.pointers[slot_index];
    if (camera_space) {
        bmf_v2 view_size = bmf_v2_make((f32)input.view.width / input.view.dpi_scale, (f32)input.view.height / input.view.dpi_scale);
        position = bmf_camera_world_to_screen(&input.pointer_camera, view_size, position);
    }
    if (slot->owner != INPUT_OWNER_INJECTED) {
        // Take the slot over: a touch contact there is dropped (released), the mouse waits.
        slot->owner = INPUT_OWNER_INJECTED;
        slot->id = input.next_touch_id++;
        input_pointer_set_level(slot, 0);
    }
    slot->screen = position;
    if (change == BMF_INPUT_POINTER_DOWN) input_pointer_set_level(slot, 1);
    if (change == BMF_INPUT_POINTER_UP) {
        input_pointer_set_level(slot, 0);
        slot->owner = INPUT_OWNER_NONE;
    }
}
