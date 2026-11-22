// input.h: actions, bindings and per-tick input sampling (SPEC §10, D-24, task 1A-22). The game
// side (bmf_action, bmf_input, the binding values and the run-time action calls) is public in
// bmf.h; this header is the engine side (feeding events, sampling ticks, injection).
//
// Simulation never sees devices, only actions. A game declares a table of named actions with
// default bindings (bmf_action). Once per frame the engine feeds every platform event and the
// gamepad states into this module (bmf_input_event, bmf_input_set_gamepads). It then calls
// bmf_input_sample_tick once per simulation tick. The result, a fixed-size POD bmf_input, is the
// only input `tick` gets. Replays store bmf_input per tick, so they do not depend on bindings
// or devices.
//
// Edges are per tick (SPEC §10): `pressed`/`released` describe the change since the previous
// sampled tick, so no press is lost when a frame runs 0 ticks or several. A press released again
// before the next tick is still down (and pressed) on exactly one tick, and released on the next
// one. Every press counts: two taps within one frame give pressed, released, pressed, released
// on four ticks (up to BMF_INPUT_MAX_PENDING_PRESSES presses wait per button or pointer).
//
// The module never reads the platform itself. The engine pumps bmf_platform_event_pop and
// bmf_platform_gamepad_get into it, and the control protocol (1A-31) feeds the same calls (raw
// keys) plus the bmf_input_inject_* overrides (actions, pointers). Tests and agents
// therefore drive the same code that players do.
//
// Thread rule for every function: main thread only. Not allowed in tick unless the contract says
// so: tick reads only the bmf_input it is given (D-37 rule 8).
#ifndef BMF_INPUT_H
#define BMF_INPUT_H

#include "../bmf.h"
#include "../platform/platform.h"
#include "../draw2d/draw2d.h"

// ---------------------------------------------------------------- engine interface
// Validates the action table and makes it current, with its default bindings; resets all device
// state, injections, the text queue, the view ({0, 0, 1}) and the pointer camera (default world).
// Rules: actions != 0 when action_count > 0; 0 <= action_count <= BMF_INPUT_MAX_ACTIONS; names
// valid and unique; at most 64 BUTTON, 8 AXIS2 and 1 POINTER actions; every binding fits its
// action's type (see the binding enum). On failure returns BMF_ERR_INVALID, writes a one-line
// message naming the action into error[0..error_capacity) (NUL-terminated) and leaves an empty
// table (every tick samples zeros). `actions` must stay valid until the next init (the game's
// static table). O(action_count^2) name comparisons.
bmf_err bmf_input_init(const bmf_action* actions, i32 action_count, char* error, i32 error_capacity);

// Checks an action table against the rules of bmf_input_init without changing any input state
// (the engine checks a reloaded game before swapping it in). Same results and messages as
// bmf_input_init. O(action_count^2).
bmf_err bmf_input_check_actions(const bmf_action* actions, i32 action_count, char* error, i32 error_capacity);

// The view pointer positions refer to (the window client area or the offscreen target): physical
// size and DPI scale. Event positions (physical pixels) are divided by view.dpi_scale; pointer
// world positions are mapped with the pointer camera for the logical view size. Set it before
// the events of a frame whenever it changes. view.dpi_scale > 0. O(1).
void bmf_input_set_view(bmf_draw2d_view view);

// Applies one platform event to the device state: keys (auto-repeat ignored), mouse position and
// buttons, touch/pen contacts, text. Other event types are ignored. A button action goes down
// when any of its bindings (or its injection) is down, and every such change counts as a press.
// O(button actions x bindings) for key and mouse button events, O(1) otherwise.
void bmf_input_event(const bmf_event* event);

// The current state of gamepads 0..count-1 (count <= BMF_GAMEPAD_MAX; missing pads count as
// disconnected), once per frame after the events. O(count + button actions x bindings).
void bmf_input_set_gamepads(const bmf_gamepad* pads, i32 count);

// Fills *out for one simulation tick from the device state, the injections and the presses
// waiting since the previous tick (see the edge rules at the top). Text: up to
// BMF_INPUT_MAX_TEXT queued codepoints; the rest wait for the next tick. Pointers stay 0 without
// a POINTER action. O(actions x bindings + pointers + text queue).
void bmf_input_sample_tick(bmf_input* out);

// Codepoints dropped because the text queue was full, since the last init. O(1).
i32 bmf_input_text_dropped(void);

// ---------------------------------------------------------------- injection (protocol, tests)
// Holds a BUTTON action down (down = 1) or lets it go (0), on top of its bindings, until changed.
// The protocol's `press ACTION N` calls this before and after N ticks. O(button actions x bindings).
void bmf_input_inject_button(i32 action, b32 down);

// Replaces an AXIS2 action's value with `value` (normalized to length <= 1 like device input)
// while `active`; active = 0 returns it to its bindings. Finite values. O(1).
void bmf_input_inject_axis(i32 action, b32 active, bmf_v2 value);

typedef enum bmf_input_pointer_change {
    BMF_INPUT_POINTER_MOVE = 0,
    BMF_INPUT_POINTER_DOWN = 1,
    BMF_INPUT_POINTER_UP = 2,
} bmf_input_pointer_change;

// Moves, presses or releases pointer `slot` (0..3) as an injected contact, which owns the slot
// (mouse and touch are ignored there) until an UP. `position` is in the pointer camera's space
// when `camera_space`, else logical pixels; it is converted to logical pixels at once with the
// current view and camera. A press counts like a device press. O(1).
void bmf_input_inject_pointer(i32 slot, bmf_input_pointer_change change, bmf_v2 position, b32 camera_space);

#endif
