# Spike P0-05: sanitizers without the CRT

Date: 2026-11-18. Toolchain: llvm 23.1.6, MSVC 14.51.36231 CRT libs (VS 18 Community). Spike code: `spikes/sanitize/` (throwaway).

## Result: both outcomes are available; recommend using both

### (b) UBSan in trap mode: works with no CRT and no runtime

Flags: `-fsanitize=undefined -fsanitize-trap=undefined`, added to the normal CRT-free debug build. Nothing extra to link.

| Case | -O0 -g | -O1 |
|---|---|---|
| signed `int` add overflow | trap (`ud2`, exit 132 / `0xC000001D`) | trap |
| shift by 40 | trap | trap |
| misaligned `i32` load | trap | trap |
| clean run | exit 0 | exit 0 |

The trap is an illegal-instruction exception, so the crash handler (1A-35) catches it like any other crash and writes the replay. There is no message text: the crash JSON reports `reason:"illegal_instruction"` and the address; the agent maps the address to a line with the PDB (`llvm-symbolizer` ships with LLVM). Recommendation: the crash handler prints `"reason":"ubsan_trap"` when the faulting instruction bytes are `0F 0B`.

UBSan does **not** catch out-of-bounds reads (stack array read at index 16 of 16 ran clean).

### (a) ASan: works only as a separate configuration that links the CRT

What works: `-fsanitize=address -O0 -g` with the CRT startup (`main`, dynamic CRT `/MD`), the ASan DLL `clang_rt.asan_dynamic-x86_64.dll` copied next to the exe.

| Case | Result |
|---|---|
| stack array read one past the end | `ERROR: AddressSanitizer: stack-buffer-overflow`, exit 1 |
| arena memory (from `VirtualAlloc`) poisoned with `__asan_poison_memory_region`, then a 64-byte "push" unpoisoned, read at byte 64 | `ERROR: AddressSanitizer: use-after-poison`, exit 1 |
| clean run | exit 0 |

What does **not** work:
- Our own `memcpy`/`memset`/`memmove`/`memcmp` (or `__chkstk`) linked into the ASan build: segfault at startup, before `main`. ASan intercepts the CRT mem functions; ours collide.
- Our own entry point (`/entry:bmf_win32_entry`) with ASan, even with the CRT libs linked: segfault at startup. ASan initializes through CRT startup.
- ASan with `-nostdlib`: the ASan runtime needs `abort`, `atoi`, `frexp`, `longjmp` and more from the CRT.

Arena integration: ASan does not know about `VirtualAlloc` memory, so arenas must call `__asan_poison_memory_region` on reserve/reset/pop and `__asan_unpoison_memory_region` on push. Pools poison freed slots. This is cheap to add in 1A-03 behind `#if BMF_SANITIZE`.

## Recommended build configuration (amendment A-03)

- `debug` (default, CRT-free): `-O0 -g -DBMF_DEBUG=1 -fsanitize=undefined -fsanitize-trap=undefined`, plus arena guard pages and debug fill patterns (1A-03).
- `sanitize` (new, win64 only, never shipped): `-O0 -g -DBMF_DEBUG=1 -DBMF_SANITIZE=1 -fsanitize=address,undefined -fsanitize-trap=undefined`, CRT entry `main` calling the bmf entry, dynamic CRT, ASan DLL copied to the output folder. Under `BMF_SANITIZE`, `base_builtins.c` does not define the mem functions, `_fltused` or `__chkstk` (the CRT provides them), and arenas/pools poison and unpoison.
- `build test` runs the unit tests in `debug`. The `sanitize` config runs on request (`build test --sanitize`) and before each milestone gate.

This replaces the D-09 sentence "`-fsanitize=address,undefined` on native targets where clang supports it" (owner approval needed, R8).

## Host-toolchain finding (affects 1A-01 and A-01)

clang 23 did **not** find the VS 18 MSVC install or the SDK headers on its own (include search list only had clang's own dir). Host builds that use the CRT need explicit roots:
```
-Xmicrosoft-visualc-tools-root "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/<ver>"
-Xmicrosoft-windows-sdk-root   "C:/Program Files (x86)/Windows Kits/10"
-Xmicrosoft-windows-sdk-version <ver>
```
`build.c` needs MSVC discovery (newest folder under `VC/Tools/MSVC` of any VS install, overridable via `BMF_MSVC`) for the `sanitize` config and for any CRT-using host tool. The runtime `debug`/`release` builds do not need it.

Detail: `clang -v` shows the driver searching only legacy `Microsoft Visual Studio 8/9/10.0` paths, so plain `clang build.c -o build.exe` (SPEC D-12) fails on this machine with `'stdio.h' file not found`. Options for 1A-01, owner to choose:
1. `build.c` is CRT-free like the runtime (D-04 already says tooling follows the same rule). The bootstrap still needs `-L<SDK um\x64>` for `kernel32.lib`, so a one-line `bootstrap.bat` that finds the newest SDK and calls clang is needed.
2. Bootstrap from a "Developer PowerShell for VS 18" prompt (clang honors the `INCLUDE`/`LIB` env vars it sets).
Recommendation: option 1 (no VS dependency at all; the SDK alone suffices).
