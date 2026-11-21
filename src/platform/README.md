# src/platform

The platform layer: the internal contract in SPEC §9, one folder per OS. Nothing here is in `bmf.h`.

## Files
- `platform.h`: the contract every OS implements (task 1A-11). Process start and arguments, buffered stdio, time and sleep, files (read whole, map read-only, atomic write, delete, info), file-change polling, DLL loading, futex and semaphore, threads, CPU count (1A-11); keys (`bmf_key`, positional, with names), gamepads, the window and the platform event queue (1A-12). Inline: FPU state (`bmf_fpu_setup`, `bmf_fpu_check`), atomics (`bmf_atomic_*` over `__atomic_*`), `bmf_cpu_pause`. Virtual-memory hooks are declared in `base/base_memory.h`. The audio device (1B) and stdin for the control protocol (1A-3x) are added by their tasks.
- `platform_common.c`: OS-independent parts: key names (`bmf_key_name`, `bmf_key_from_name`) and the event queue (`platform_event_push` for OS code, `bmf_platform_event_pop`).
- `win32/win32_api.h`: hand-written Win32 declarations (no `windows.h`), task 1A-10. kernel32 (files, memory, time, waitable timers, threads, semaphores/events, DLLs, crash handling), synchronization (`WaitOnAddress`), user32 (window, messages, DPI, Raw Input, pointer), ole32/avrt, XInput, and COM vtables for D3D11, DXGI, `ID3DBlob` and WASAPI. Runtime-loaded entry points (`D3D11CreateDevice`, `CreateDXGIFactory2`, `XInputGetState/SetState`, `D3DCompile`) are `*_fn` typedefs. Each declaration carries its MSDN name; struct sizes are pinned with `BMF_STATIC_ASSERT`. Add only what is used.
- `win32/win32_core.c`: implements `platform.h` on Win32, plus `bmf_platform_fatal` (one JSON crash line to stderr, stack buffer only) and `bmf_win32_split_command_line` (UCRT argv rules, exposed for tests).
- `win32/win32_window.c`: window, OS messages, Raw Input mouse, `WM_POINTER` touch/pen, XInput gamepads (task 1A-12); `bmf_win32_key_from_scancode`, `bmf_win32_utf16_feed` and `bmf_win32_stick` are exposed for tests.
- `win32/win32_main.c`: runtime exe entry `bmf_win32_entry` (task 1A-23). So far a stub that calls `bmf_platform_init`, resolves `bmf_game` from `game.dll` (debug) or the static link (release) and exits. With `--control` it opens a window and prints every platform event as a JSON line until the window is closed (F11 toggles fullscreen): the manual event test of 1A-12.

## Invariants
- Every entry point calls `bmf_platform_init` first (runtime exe, test runner). It sets the main thread's FPU state, the clock frequency, arguments, stdio handles and the sleep timer.
- **FPU state (D-37 rule 10):** MXCSR = 0x1F80 (round-to-nearest-even, FTZ and DAZ off, all exceptions masked) on the main thread (`bmf_platform_init`) and on every thread started by `bmf_platform_thread_start`, before any user code. `bmf_fpu_check()` ignores the exception status flags.
- Arguments are split with the Universal CRT rules (verified against the UCRT's own `argv` on the test cases) and converted to UTF-8 once; they live for the whole process. Limits: 1024 arguments (more is a panic).
- Stdio is line-buffered in two static 64 KiB buffers (not arena memory: it must work before the arenas exist and during crashes). A write that contains a newline flushes through its last newline; larger-than-buffer writes go straight through. `bmf_platform_exit` flushes both. `bmf_platform_fatal` bypasses the buffers.
- Files: paths are UTF-8 (converted to UTF-16, at most 2048 units). Readers open with read/write/delete sharing, so a file being read or mapped can still be replaced. Atomic write = temp file `<path>.<pid>-<tid>.tmp` in the same folder, then `MoveFileExW(REPLACE_EXISTING)`; a transient `ACCESS_DENIED`/`SHARING_VIOLATION` on the rename (virus scanners briefly open new files without delete sharing; seen in about 1 of 15 test runs) is retried with backoff for up to 63 ms; the temp file is deleted on any failure. Not flushed to disk (atomic against process crashes, not power loss). A mapping stays valid and unchanged after the file is replaced.
- File watch compares size and last write time, at most every 250 ms, with time passed in by the caller (testable, and no syscall between checks).
- Semaphore: `{count, waiters}` on `WaitOnAddress`; no OS handle, so it can live in any memory. The lost-wakeup argument is in `win32_core.c` above `bmf_semaphore_signal`.
- Threads get their function through one of 64 start slots that the new thread frees before running user code; stacks are explicit reservations (`STACK_SIZE_PARAM_IS_A_RESERVATION`, >= 64 KiB).
- Keys are positional (`BMF_KEY_Q` is the key left of W on every layout). Win32 maps the WM_KEYDOWN scan code (8 bits, plus 0x100 for the extended bit) through a 512-entry table; only Pause, NumLock and PrintScreen also look at the virtual key. Aliases: Pause (0x45, 0x146), PrintScreen (0x137, 0x54), right Shift (0x36, 0x136); the fake Shift 0x12A maps to nothing. A test checks that every key is reachable and that these are the only aliases.
- `KEY_DOWN` carries `repeat = 1` when the key was already down. A `KEY_UP` is only sent for a key that is down. Focus loss sends `KEY_UP`/`MOUSE_BUTTON_UP`/`POINTER_UP` for everything held, then `FOCUS_LOST`.
- Text comes only from `WM_CHAR`, surrogate pairs joined, unpaired surrogates as U+FFFD, control characters (< 0x20, 0x7F) dropped.
- Raw mouse motion is summed into at most one `MOUSE_DELTA` per poll; window messages give position, buttons (with mouse capture while any is held) and wheel (in notches).
- Touch and pen use `WM_POINTER*` (`EnableMouseInPointer(FALSE)`, so the mouse never arrives there); handled pointer messages return 0, so Windows generates no emulated mouse input. Up to 10 contacts get slots 0..9.
- Alt and F10 do not enter the (absent) window menu: `SC_KEYMENU` is swallowed, `WM_MENUCHAR` answers `MNC_CLOSE`. Alt+F4 still closes: `WM_CLOSE` becomes `CLOSE_REQUESTED` and the engine decides.
- Gamepads: XInput is loaded at runtime (`xinput1_4.dll`) and read only while a window is open. An empty slot is probed at most once per second (`XInputGetState` on an empty slot is slow). Sticks use a radial dead zone rescaled to [0, 1]; triggers a threshold.
- Window: per-monitor DPI aware v2; the client size is the logical size times the monitor's DPI scale; `WM_DPICHANGED` applies the suggested rectangle and sends `RESIZED` with the new scale. Borderless fullscreen = `WS_POPUP` over the monitor rectangle; leaving it restores the saved placement. The window has `WS_EX_NOREDIRECTIONBITMAP` (only a flip-model swapchain draws it), so before 1A-14 it shows no content.
- No `windows.h` (or any SDK header) in `src/`. Types and fields keep SDK names; constants are `WIN32_`-prefixed (the game is compiled into the same unity build).
- COM vtables list every slot up to the last one used, in SDK order; unused slots are `void* _<slot>_<Name>`. Verified against SDK 10.0.26100 when added; `tests/test_vtable_slots.c` (1A-14) keeps checking.
- Libraries: `kernel32`, `synchronization` (`WaitOnAddress`, an API set present since Windows 8) and `user32` are linked. `ole32` and `avrt` are added to the link by the task that first calls them; `d3d11`, `dxgi`, `xinput1_4` and `d3dcompiler_47` are always loaded at runtime (`--no-gpu` must start without them).
- x64 only on Windows: one calling convention, so declarations carry no `WINAPI`.
- Every code path that can run at crash time uses only stack or pre-allocated memory.

## Data layout
- `win32_core` (one static struct, about 235 KiB of zero-initialized `.bss`): main thread id, counter frequency, sleep timer, std handles, two 64 KiB stdio buffers, 1024 argument pointers and 99 KiB of argument bytes.
- `win32_thread_slots[64]`: `{in_use, fn, user}` per starting thread.
- `platform_events`: ring of 1024 `bmf_event` (24 bytes each), head, count, dropped counter.
- `win32_window`: HWND, saved placement, size, key-down bitset (2 x u64), mouse-button bits, summed raw delta, 10 pointer slots, the UTF-16 decoder, XInput entry point, 4 gamepad states and their probe times.

## Hot paths
- `bmf_platform_time_ns`: one `QueryPerformanceCounter` plus a divide; 22 ns/call (debug build, 2026-11-20).
- `bmf_semaphore_signal/wait`: atomics only unless a thread must block or be woken. Atomics are inline builtins.
- `bmf_platform_sleep_ns`: high-resolution waitable timer; a 2 ms sleep measured 0.6 ms late at worst when idle, up to 2.7 ms under a full test run.

## Limits
- `bmf_platform_write`, `bmf_platform_flush` and `bmf_platform_sleep_ns` are main-thread only (debug asserts). Workers that need to log or sleep wait for the job system design (1B-12).
- `bmf_platform_file_read` handles files < 2 GiB (`bmf_str` length is `i32`); use `bmf_platform_file_map` for larger ones.
- No stdin reading yet (control protocol, 1A-3x), no audio device (1B).
- The game freezes while the user drags or resizes the window (Windows runs a modal loop inside `DefWindowProc`); 1A-23 decides whether to render from `WM_SIZE` during it.
- With both Shift keys held, releasing one sends no `WM_KEYUP` (a Windows quirk); that Shift stays down until the other is released or focus is lost.
- One window per process. Window, event and gamepad functions are main-thread only.
- XInput controllers only (no DirectInput/HID gamepads).
- `bmf_fpu_check` on wasm32 always returns 1 (only IEEE default mode exists there); arm64 FPCR arrives with that port.
- Tests: `tests/test_platform.c` (UCRT argv cases incl. UTF-16 surrogates, file round trip incl. empty/missing/Unicode names, atomic write incl. replace-while-mapped, locked destination and 200 replaces, file watch, 8-thread atomic counters and 3 waves of 64 threads, semaphore ping-pong and 4 consumers, MXCSR in a spawned thread after breaking the parent's, time monotonicity and sleep, stdio line buffering through a pipe, DLL load) and `tests/test_platform_window.c` (scan code table: reachability, aliases, layout independence, overlapping codes; key names; UTF-16 decoding incl. lone and doubled surrogates; dead zones and triggers; event queue overflow and wrap-around; a hidden window driven by posted messages: keys, repeat, text, mouse, wheel, focus-loss release, close, DPI change, fullscreen and restore, suspend/resume, reopen).
