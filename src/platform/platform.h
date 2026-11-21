// platform.h: the internal platform contract (SPEC §9). Every OS folder under src/platform/
// implements it; nothing here is in bmf.h. Task 1A-11 covers process start, stdio, time, files,
// threads, atomics, futex, semaphore, FPU state and DLL loading. Window and events (1A-12), the
// audio device (1B) and stdin for the control protocol (1A-3x) are added by their tasks.
// Virtual memory hooks (bmf_platform_reserve/commit/decommit) are declared in base/base_memory.h.
//
// Thread rules: functions marked "main thread only" must be called from the thread that called
// bmf_platform_init. Everything else is safe from any thread.
#ifndef BMF_PLATFORM_H
#define BMF_PLATFORM_H

#include "base/base_types.h"
#include "base/base_string.h"
#include "base/base_memory.h"

// ================================================================ process
// First call of every entry point (runtime exe, test runner). Sets this thread's FPU state
// (bmf_fpu_setup), reads the clock frequency, splits the command line into UTF-8 arguments and
// prepares stdout/stderr and the sleep timer. Main thread only, once. O(command line length).
void bmf_platform_init(void);

typedef struct bmf_platform_args {
    i32 count;                  // >= 1; values[0] is the program as typed on the command line
    const char* const* values;  // UTF-8, NUL-terminated, valid for the whole process
} bmf_platform_args;

// The process arguments, split with the Windows CRT rules (quotes, backslash escapes) on win32.
// Valid after bmf_platform_init. O(1).
bmf_platform_args bmf_platform_get_args(void);

// Flushes stdout and stderr, then ends the process with `code`. Never returns.
__attribute__((noreturn)) void bmf_platform_exit(i32 code);

// Writes the folder of the running executable as UTF-8 with a trailing separator, plus a NUL,
// into out[0..capacity). Returns the length without the NUL, or 0 if it does not fit. O(path).
i32 bmf_platform_exe_dir(char* out, i32 capacity);

// ================================================================ stdio
typedef enum bmf_stream {
    BMF_STREAM_STDOUT = 0,
    BMF_STREAM_STDERR = 1,
} bmf_stream;

// Appends bytes to the stream's 64 KiB buffer. Line-buffered: when the appended bytes contain
// a newline, everything up to and including the last newline is written to the OS; a full
// buffer is written too. Main thread only (bmf_platform_fatal writes stderr directly from any
// thread). O(len).
void bmf_platform_write(bmf_stream stream, const char* data, i32 len);

// Writes any buffered bytes of the stream to the OS. Main thread only. O(buffered bytes).
void bmf_platform_flush(bmf_stream stream);

// ================================================================ time
// Monotonic time in nanoseconds since an arbitrary start (QueryPerformanceCounter on win32).
// Never decreases. Not allowed in simulation code (D-37 rule 1). O(1), about 20 ns.
i64 bmf_platform_time_ns(void);

// Blocks the calling thread for at least `ns` nanoseconds (high-resolution waitable timer on
// win32, typically < 0.5 ms late). ns <= 0 returns at once. Main thread only (one shared timer).
void bmf_platform_sleep_ns(i64 ns);

// ================================================================ files
// Paths are UTF-8, relative to the current directory or absolute; '/' and '\' both work on win32.
// Maximum path length: BMF_PLATFORM_PATH_MAX UTF-16 units (BMF_ERR_RANGE when longer).
enum { BMF_PLATFORM_PATH_MAX = 2048 };

// Reads a whole file into `arena` (16-byte aligned, plus one NUL byte after the data that is
// not counted in out->len). On failure nothing stays allocated in the arena and *out is empty.
// Errors: BMF_ERR_NOT_FOUND, BMF_ERR_RANGE (path too long, or file >= 2 GiB), BMF_ERR_IO.
// O(file size).
bmf_err bmf_platform_file_read(const char* path, bmf_arena* arena, bmf_str* out);

typedef struct bmf_file_map {
    const u8* data;   // read-only; 0 for an empty file
    i64 size;
} bmf_file_map;

// Maps a whole file read-only into memory. The view stays valid until bmf_platform_file_unmap,
// even if the file is replaced on disk meanwhile. Errors as bmf_platform_file_read (no size
// limit). O(1) plus page faults on first touch.
bmf_err bmf_platform_file_map(const char* path, bmf_file_map* out);

// Releases a mapping from bmf_platform_file_map and clears *map. Safe on an empty map.
void bmf_platform_file_unmap(bmf_file_map* map);

// Writes `size` bytes to `path` so that readers see either the old file or the complete new
// one, never a partial file: the data goes to "<path>.<pid>-<thread>.tmp" in the same folder,
// which then replaces `path` in one rename. The temp file is deleted on failure. Atomic against
// a crash of this process, not against power loss (no flush to disk: it costs milliseconds).
// When another process holds `path` open without delete sharing (virus scanners do this for a
// moment after a write), the rename is retried for up to 63 ms. Errors: BMF_ERR_NOT_FOUND (folder
// missing), BMF_ERR_RANGE (path too long), BMF_ERR_IO (for example `path` stays locked). O(size).
bmf_err bmf_platform_file_write_atomic(const char* path, const void* data, i64 size);

// Deletes a file. Errors: BMF_ERR_NOT_FOUND, BMF_ERR_RANGE, BMF_ERR_IO. O(1).
bmf_err bmf_platform_file_delete(const char* path);

typedef struct bmf_file_info {
    i64 size;
    u64 modified;   // last write time; opaque units (100 ns on win32), only compare for equality/order
} bmf_file_info;

// Size and last write time of a file. Errors: BMF_ERR_NOT_FOUND, BMF_ERR_RANGE, BMF_ERR_IO.
// O(1), one metadata syscall (no file open).
bmf_err bmf_platform_file_info(const char* path, bmf_file_info* out);

// ---------------------------------------------------------------- file-change polling
enum { BMF_FILE_WATCH_INTERVAL_MS = 250 };

// Polls one file for changes by comparing size and last write time, at most once every
// BMF_FILE_WATCH_INTERVAL_MS. A missing file counts as size -1, modified 0, so appearing and
// disappearing are changes too. Used for hot reload of the game DLL and assets.
typedef struct bmf_file_watch {
    const char* path;      // caller-owned, must outlive the watch
    bmf_file_info last;    // state seen at the previous check
    i64 next_check_ns;
} bmf_file_watch;

// Records the file's current state; the first poll reports changes after this point. O(1).
void bmf_platform_file_watch_init(bmf_file_watch* watch, const char* path, i64 now_ns);

// Returns 1 once per observed change. Returns 0 without a syscall when called again within the
// interval. `now_ns` is bmf_platform_time_ns(). O(1).
b32 bmf_platform_file_watch_poll(bmf_file_watch* watch, i64 now_ns);

// ================================================================ dynamic libraries
typedef void* bmf_library;   // 0 = not loaded

// Loads a DLL by UTF-8 path (a bare name searches the system folders). Returns 0 on failure. O(load).
bmf_library bmf_platform_library_load(const char* path);
// Address of an exported symbol, or 0. O(exports) once per name: cache the result.
void* bmf_platform_library_symbol(bmf_library library, const char* name);
// Unloads a library from bmf_platform_library_load. Pointers into it become invalid.
void bmf_platform_library_unload(bmf_library library);

// ================================================================ FPU state (D-37 rule 10)
// Required on every thread that runs simulation or math: round-to-nearest-even, denormals kept
// (FTZ and DAZ off), all floating-point exceptions masked. x64: MXCSR = 0x1F80.
// bmf_platform_init and bmf_platform_thread_start apply it; call it yourself only on threads
// you did not start through the platform (none so far).
enum { BMF_MXCSR_REQUIRED = 0x1F80, BMF_MXCSR_STATUS_FLAGS = 0x3F };

static inline void bmf_fpu_setup(void) {
#if BMF_ARCH_X64
    __builtin_ia32_ldmxcsr(BMF_MXCSR_REQUIRED);
#endif
}

// True when this thread's FPU control state matches bmf_fpu_setup. Exception status flags (set
// by ordinary inexact results) are ignored. The engine asserts it in debug at every tick. O(1).
static inline b32 bmf_fpu_check(void) {
#if BMF_ARCH_X64
    return (__builtin_ia32_stmxcsr() & ~(u32)BMF_MXCSR_STATUS_FLAGS) == BMF_MXCSR_REQUIRED;
#else
    return 1;   // wasm32: IEEE default mode only; arm64 FPCR check arrives with that port
#endif
}

// ================================================================ atomics
// Thin wrappers over the clang __atomic builtins. Read-modify-write operations and bmf_atomic_fence
// are sequentially consistent; loads are acquire and stores are release. Addresses must be
// naturally aligned. All O(1).
static inline i32  bmf_atomic_load_i32(const i32* p)                { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static inline void bmf_atomic_store_i32(i32* p, i32 value)          { __atomic_store_n(p, value, __ATOMIC_RELEASE); }
// Adds and returns the previous value.
static inline i32  bmf_atomic_add_i32(i32* p, i32 value)            { return __atomic_fetch_add(p, value, __ATOMIC_SEQ_CST); }
// Stores and returns the previous value.
static inline i32  bmf_atomic_exchange_i32(i32* p, i32 value)       { return __atomic_exchange_n(p, value, __ATOMIC_SEQ_CST); }
// Stores `desired` if *p == expected. Returns 1 if it did.
static inline b32  bmf_atomic_cas_i32(i32* p, i32 expected, i32 desired) {
    return __atomic_compare_exchange_n(p, &expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
static inline i64  bmf_atomic_load_i64(const i64* p)                { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static inline void bmf_atomic_store_i64(i64* p, i64 value)          { __atomic_store_n(p, value, __ATOMIC_RELEASE); }
static inline i64  bmf_atomic_add_i64(i64* p, i64 value)            { return __atomic_fetch_add(p, value, __ATOMIC_SEQ_CST); }
static inline i64  bmf_atomic_exchange_i64(i64* p, i64 value)       { return __atomic_exchange_n(p, value, __ATOMIC_SEQ_CST); }
static inline b32  bmf_atomic_cas_i64(i64* p, i64 expected, i64 desired) {
    return __atomic_compare_exchange_n(p, &expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
// Full sequentially consistent fence.
static inline void bmf_atomic_fence(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
// Spin-wait hint (x64 `pause`). Use inside busy loops.
static inline void bmf_cpu_pause(void) {
#if BMF_ARCH_X64
    __builtin_ia32_pause();
#endif
}

// ================================================================ futex and semaphore
// Blocks while *address == expected. Returns when woken, or spuriously: always re-check the
// value in a loop. O(1) when the value already differs (no syscall). Not on web Phase 1.
void bmf_platform_futex_wait(i32* address, i32 expected);
// Wakes one / all threads blocked in bmf_platform_futex_wait on `address`. O(1) syscall.
void bmf_platform_futex_wake_one(i32* address);
void bmf_platform_futex_wake_all(i32* address);

// Counting semaphore on the futex. Zero-initialize (count 0) or set `count` before use; no
// destroy. Holds no OS handle, so it can live in any memory. signal and wait make a syscall
// only when a thread must block or be woken.
typedef struct bmf_semaphore {
    i32 count;     // available units, never negative
    i32 waiters;   // threads in (or about to enter) futex_wait
} bmf_semaphore;

// Adds `count` units (>= 1) and wakes waiting threads. O(1).
void bmf_semaphore_signal(bmf_semaphore* semaphore, i32 count);
// Takes one unit, blocking while none is available. O(1) without contention.
void bmf_semaphore_wait(bmf_semaphore* semaphore);

// ================================================================ threads
typedef void (*bmf_thread_fn)(void* user);
typedef struct bmf_thread {
    void* handle;   // 0 = not started
} bmf_thread;

enum { BMF_THREAD_MAX_STARTING = 64 };   // threads that may be starting (not yet running) at once

// Starts `fn(user)` on a new thread with `stack_size` bytes of reserved stack (>= 64 KiB) and a
// debugger-visible `name` (UTF-8). The new thread runs bmf_fpu_setup before `fn`. Panics if the
// OS refuses. Join every started thread. O(1) plus thread creation (~20 us).
bmf_thread bmf_platform_thread_start(bmf_thread_fn fn, void* user, i64 stack_size, const char* name);

// Waits until the thread's function returned, then releases the thread. O(1) plus waiting.
void bmf_platform_thread_join(bmf_thread* thread);

// Logical processors available to this process (all processor groups). O(1).
i32 bmf_platform_cpu_count(void);

// ================================================================ keys (task 1A-12)
// Physical key positions, named after the US layout (like the web `KeyboardEvent.code`): BMF_KEY_Q
// is the key left of W whatever the layout prints on it. Text input comes separately as
// BMF_EVENT_TEXT. The input module (1A-22) and bmf.h (1A-24) take this enum over unchanged.
typedef enum bmf_key {
    BMF_KEY_NONE = 0,
    BMF_KEY_A, BMF_KEY_B, BMF_KEY_C, BMF_KEY_D, BMF_KEY_E, BMF_KEY_F, BMF_KEY_G, BMF_KEY_H, BMF_KEY_I,
    BMF_KEY_J, BMF_KEY_K, BMF_KEY_L, BMF_KEY_M, BMF_KEY_N, BMF_KEY_O, BMF_KEY_P, BMF_KEY_Q, BMF_KEY_R,
    BMF_KEY_S, BMF_KEY_T, BMF_KEY_U, BMF_KEY_V, BMF_KEY_W, BMF_KEY_X, BMF_KEY_Y, BMF_KEY_Z,
    BMF_KEY_0, BMF_KEY_1, BMF_KEY_2, BMF_KEY_3, BMF_KEY_4, BMF_KEY_5, BMF_KEY_6, BMF_KEY_7, BMF_KEY_8, BMF_KEY_9,
    BMF_KEY_ESCAPE, BMF_KEY_ENTER, BMF_KEY_TAB, BMF_KEY_BACKSPACE, BMF_KEY_SPACE,
    BMF_KEY_MINUS, BMF_KEY_EQUAL, BMF_KEY_LEFT_BRACKET, BMF_KEY_RIGHT_BRACKET, BMF_KEY_BACKSLASH,
    BMF_KEY_SEMICOLON, BMF_KEY_APOSTROPHE, BMF_KEY_GRAVE, BMF_KEY_COMMA, BMF_KEY_PERIOD, BMF_KEY_SLASH,
    BMF_KEY_INTL_BACKSLASH,   // the extra key right of left Shift on ISO keyboards
    BMF_KEY_CAPS_LOCK, BMF_KEY_SCROLL_LOCK, BMF_KEY_NUM_LOCK, BMF_KEY_PRINT_SCREEN, BMF_KEY_PAUSE,
    BMF_KEY_F1, BMF_KEY_F2, BMF_KEY_F3, BMF_KEY_F4, BMF_KEY_F5, BMF_KEY_F6, BMF_KEY_F7, BMF_KEY_F8,
    BMF_KEY_F9, BMF_KEY_F10, BMF_KEY_F11, BMF_KEY_F12, BMF_KEY_F13, BMF_KEY_F14, BMF_KEY_F15, BMF_KEY_F16,
    BMF_KEY_F17, BMF_KEY_F18, BMF_KEY_F19, BMF_KEY_F20, BMF_KEY_F21, BMF_KEY_F22, BMF_KEY_F23, BMF_KEY_F24,
    BMF_KEY_INSERT, BMF_KEY_DELETE, BMF_KEY_HOME, BMF_KEY_END, BMF_KEY_PAGE_UP, BMF_KEY_PAGE_DOWN,
    BMF_KEY_LEFT, BMF_KEY_RIGHT, BMF_KEY_UP, BMF_KEY_DOWN,
    BMF_KEY_KP_0, BMF_KEY_KP_1, BMF_KEY_KP_2, BMF_KEY_KP_3, BMF_KEY_KP_4, BMF_KEY_KP_5, BMF_KEY_KP_6,
    BMF_KEY_KP_7, BMF_KEY_KP_8, BMF_KEY_KP_9, BMF_KEY_KP_DECIMAL, BMF_KEY_KP_DIVIDE, BMF_KEY_KP_MULTIPLY,
    BMF_KEY_KP_SUBTRACT, BMF_KEY_KP_ADD, BMF_KEY_KP_ENTER,
    BMF_KEY_LEFT_SHIFT, BMF_KEY_RIGHT_SHIFT, BMF_KEY_LEFT_CTRL, BMF_KEY_RIGHT_CTRL,
    BMF_KEY_LEFT_ALT, BMF_KEY_RIGHT_ALT, BMF_KEY_LEFT_SUPER, BMF_KEY_RIGHT_SUPER, BMF_KEY_MENU,
    BMF_KEY_COUNT
} bmf_key;
BMF_STATIC_ASSERT(BMF_KEY_COUNT <= 128, "key state is kept in two u64 bitsets");

// Lowercase name of a key ("a", "1", "space", "left_shift", "kp_enter", "f12"), the spelling the
// control protocol uses (1A-31). BMF_KEY_NONE and out-of-range values give "none". O(1).
const char* bmf_key_name(bmf_key key);
// Inverse of bmf_key_name; BMF_KEY_NONE for an unknown name. O(BMF_KEY_COUNT).
bmf_key bmf_key_from_name(bmf_str name);

typedef enum bmf_mouse_button {
    BMF_MOUSE_LEFT = 0, BMF_MOUSE_RIGHT = 1, BMF_MOUSE_MIDDLE = 2, BMF_MOUSE_X1 = 3, BMF_MOUSE_X2 = 4,
    BMF_MOUSE_BUTTON_COUNT
} bmf_mouse_button;

// ================================================================ gamepads (task 1A-12)
enum { BMF_GAMEPAD_MAX = 4 };

typedef enum bmf_gamepad_button {   // bits of bmf_gamepad.buttons (Xbox names and positions)
    BMF_GAMEPAD_A = 1 << 0, BMF_GAMEPAD_B = 1 << 1, BMF_GAMEPAD_X = 1 << 2, BMF_GAMEPAD_Y = 1 << 3,
    BMF_GAMEPAD_LEFT_SHOULDER = 1 << 4, BMF_GAMEPAD_RIGHT_SHOULDER = 1 << 5,
    BMF_GAMEPAD_BACK = 1 << 6, BMF_GAMEPAD_START = 1 << 7,
    BMF_GAMEPAD_LEFT_STICK = 1 << 8, BMF_GAMEPAD_RIGHT_STICK = 1 << 9,
    BMF_GAMEPAD_DPAD_UP = 1 << 10, BMF_GAMEPAD_DPAD_DOWN = 1 << 11,
    BMF_GAMEPAD_DPAD_LEFT = 1 << 12, BMF_GAMEPAD_DPAD_RIGHT = 1 << 13,
} bmf_gamepad_button;

typedef struct bmf_gamepad {
    b32 connected;
    u32 buttons;                    // bmf_gamepad_button bits
    f32 left_x, left_y;             // sticks after the radial dead zone, each axis in [-1, 1], +y = up
    f32 right_x, right_y;
    f32 left_trigger, right_trigger;   // [0, 1] after the trigger threshold
} bmf_gamepad;

// State of gamepad `index` (0..BMF_GAMEPAD_MAX-1) as of the last bmf_platform_poll_events.
// Returns `connected`. Gamepads are only read while a window is open (never in headless runs,
// so agent runs cannot pick up a real controller). Main thread only. O(1).
b32 bmf_platform_gamepad_get(i32 index, bmf_gamepad* out);

// ================================================================ window and events (task 1A-12)
typedef struct bmf_window_desc {
    const char* title;   // UTF-8
    i32 width, height;   // client size in logical pixels (scaled by the monitor's DPI)
    b32 hidden;          // create without showing (tests)
} bmf_window_desc;

typedef struct bmf_window_size {
    i32 width, height;   // client size in physical pixels
    f32 dpi_scale;       // physical pixels per logical pixel (1.0 at 96 DPI)
} bmf_window_size;

// Creates the one window (per-monitor DPI aware v2), registers the raw mouse and loads XInput if
// present. Returns 0 on failure. Main thread only; at most one window at a time.
b32 bmf_platform_window_open(const bmf_window_desc* desc);
// Destroys the window. Pending events stay in the queue. Main thread only.
void bmf_platform_window_close(void);
// Current client size and DPI scale; zeros without a window. O(1).
bmf_window_size bmf_platform_window_size(void);
// Borderless fullscreen on the window's monitor, or back to the previous placement. Main thread only.
void bmf_platform_window_set_fullscreen(b32 fullscreen);
b32 bmf_platform_window_is_fullscreen(void);
// The native handle for the GPU backend (HWND on win32), 0 without a window.
void* bmf_platform_window_native(void);

typedef enum bmf_event_type {
    BMF_EVENT_NONE = 0,
    BMF_EVENT_KEY_DOWN,              // key; repeat = 1 for auto-repeat
    BMF_EVENT_KEY_UP,                // key
    BMF_EVENT_TEXT,                  // codepoint (no control characters)
    BMF_EVENT_MOUSE_MOVE,            // position: client pixels
    BMF_EVENT_MOUSE_DELTA,           // delta: raw mouse counts since the previous poll (summed)
    BMF_EVENT_MOUSE_BUTTON_DOWN,     // button
    BMF_EVENT_MOUSE_BUTTON_UP,       // button
    BMF_EVENT_MOUSE_WHEEL,           // delta: notches (+y = away from the user, +x = right)
    BMF_EVENT_POINTER_DOWN,          // pointer: touch or pen contact starts
    BMF_EVENT_POINTER_MOVE,          // pointer
    BMF_EVENT_POINTER_UP,            // pointer: contact ends (or is canceled)
    BMF_EVENT_GAMEPAD_CONNECTED,     // gamepad
    BMF_EVENT_GAMEPAD_DISCONNECTED,  // gamepad
    BMF_EVENT_FOCUS_GAINED,
    BMF_EVENT_FOCUS_LOST,            // preceded by KEY_UP / MOUSE_BUTTON_UP / POINTER_UP for everything held
    BMF_EVENT_RESIZED,               // size: new client size or DPI scale
    BMF_EVENT_SUSPENDED,             // minimized: stop rendering
    BMF_EVENT_RESUMED,               // restored from minimized
    BMF_EVENT_CLOSE_REQUESTED,       // the user closed the window; the engine decides what happens
    BMF_EVENT_TYPE_COUNT
} bmf_event_type;

typedef enum bmf_pointer_kind { BMF_POINTER_TOUCH = 0, BMF_POINTER_PEN = 1 } bmf_pointer_kind;
enum { BMF_POINTER_MAX = 10 };   // simultaneous touch/pen contacts; more are ignored

typedef struct bmf_event {
    bmf_event_type type;
    union {
        struct { bmf_key key; b32 repeat; } key;
        struct { u32 codepoint; } text;
        struct { f32 x, y; } position;
        struct { f32 x, y; } delta;
        struct { bmf_mouse_button button; } mouse;
        struct { i32 slot; bmf_pointer_kind kind; f32 x, y, pressure; } pointer;   // slot 0..9, x/y client pixels, pressure 0..1
        struct { i32 index; } gamepad;
        bmf_window_size size;
    };
} bmf_event;

enum { BMF_EVENT_QUEUE_CAPACITY = 1024 };

// Reads OS messages and gamepads and appends the resulting events to the queue. Raw mouse motion
// is summed into at most one MOUSE_DELTA per poll. Main thread only. O(pending messages).
void bmf_platform_poll_events(void);
// Takes the oldest queued event. Returns 0 when the queue is empty. O(1).
b32 bmf_platform_event_pop(bmf_event* out);
// Events dropped because the queue was full (the newest are dropped), since the process started.
i32 bmf_platform_events_dropped(void);

#endif
