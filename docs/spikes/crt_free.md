# Spike P0-03: CRT-free win32 exe

Date: 2026-11-18. Toolchain: llvm 23.1.6. Spike code: `spikes/crt_free/` (throwaway).

## Result: works

A console exe with entry `bmf_win32_entry` and a DLL with entry `bmf_dll_entry` build and run with no CRT, no `windows.h`, and only `kernel32.lib` + `user32.lib` from the SDK. The exe loads the DLL with `LoadLibraryA`, resolves an export with `GetProcAddress` and calls it (hot reload path, D-13, is viable without the CRT).

## Exact flags

Compile (all configs):
```
--target=x86_64-pc-windows-msvc -std=c11 -march=x86-64-v2
-ffreestanding -nostdlib -fno-builtin-printf -ffp-contract=off -fno-fast-math
-fno-asynchronous-unwind-tables -fuse-ld=lld
```
Link, exe:
```
-Wl,/entry:bmf_win32_entry -Wl,/subsystem:windows   (or :console for tools/tests)
-L"<WindowsKits>\10\Lib\<ver>\um\x64" -lkernel32 -luser32 ...
```
Link, DLL: `-shared -Wl,/entry:bmf_dll_entry -Wl,/noimplib`. The DLL entry has the `DllMain` signature `i32 __stdcall (void* inst, u32 reason, void* reserved)` and returns 1.

Release with `-flto`: add `-Wl,/include:_fltused`. Without it LTO internalizes and drops `_fltused` (the backend references it after IR optimization), and the link fails with `undefined symbol: _fltused`. This happens even in a single-TU unity build. `memset`/`memcpy`/`__chkstk` survive LTO without help.

Note for `build.c` (Git Bash only): MSYS path conversion rewrites `/noimplib` into a path. `build.c` calls `CreateProcessW` directly, so it is not affected.

## Symbols we must provide

| Symbol | Emitted by | Decision |
|---|---|---|
| `memset` | zero-init of large locals (`body local[32] = {0}`), at -O0 and -O2 | Own, in `base_builtins.c`, `__attribute__((no_builtin))` |
| `memcpy` | struct copies and copy loops | Own (same file) |
| `memmove`, `memcmp` | not emitted in this spike, but LLVM may emit them | Own (same file) |
| `_fltused` | any float use (MSVC ABI marker) | Own: `int _fltused = 0x9875;` |
| `__chkstk` | any frame > 4 KiB | Own, 20 lines of top-level asm (see below) |

`clang_rt.builtins-x86_64.lib` (shipped with LLVM) contains **none** of these: no `__chkstk` for the MSVC target, no mem functions. It has `__udivti3` etc. (128-bit helpers) only. We do not need to link it for x64; 64-bit division is native.

## Stack probe (`__chkstk`)

Chosen: **own `__chkstk`** in top-level asm (`spikes/crt_free/rt.c`). Semantics (MSVC x64 ABI): input RAX = frame size; probe every 4 KiB page from the caller's RSP down to RSP-RAX; preserve all registers except R10, R11 and flags; do not move RSP. Verified with a 64 KiB stack array at -O0 and -O2.

Alternative (also verified): `-mno-stack-arg-probe` plus `-Xlinker /stack:0x100000,0x100000` (commit the whole stack up front). It works, but a frame larger than the committed stack would corrupt silently instead of faulting on the guard page, and every worker thread would need the same full commit. Rejected.

Recommendation: keep the own probe, plus a test with a 256 KiB stack array in a spawned thread (1A-11).

## Sizes (`-O2`, unstripped, no debug info)

- exe: 5 632 bytes (with own mem functions, chkstk and two imports DLLs)
- DLL: 3 584 bytes
- exe, `-O0 -g` (PDB separate): 4 608 bytes

The 300 KB release-exe budget (SPEC §12.3) has plenty of room.

## Windows Defender (K-08)

Not testable on this machine: Defender real-time protection is disabled (`Get-MpComputerStatus.AntivirusEnabled = False`; `MpCmdRun -Scan` returns `Product/Feature disabled`). The binaries ran without any SmartScreen prompt because they were built locally (no Mark-of-the-Web). **Open:** re-check on a machine with Defender on, and with a downloaded (MotW-tagged) copy, before the first public release (O-5).

## Inputs for later tasks

- 1A-02: `base_builtins.c` = `memset`, `memcpy`, `memmove`, `memcmp` (no_builtin), `_fltused`, `__chkstk` (asm). x64 only for `_fltused` and `__chkstk`.
- 1A-01: release link adds `/include:_fltused`; DLL link uses `/noimplib`.
