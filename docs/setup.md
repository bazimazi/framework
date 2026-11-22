# Machine setup

bmf needs only the clang/LLVM toolchain and the Windows SDK import libraries (D-02, D-05). Nothing else is installed or downloaded by the build.

## 1. LLVM (exact version pinned in `toolchain.txt`)

```
winget install --id LLVM.LLVM -e --version 23.1.6
```

Default install path: `C:\Program Files\LLVM\bin`. The build program finds clang through the `CLANG` env var, then `PATH`, then that default path. Adding it to `PATH` is optional.

A different LLVM version is a determinism event (D-37 rule 14): do not upgrade without the owner, and update `toolchain.txt` only together with a full golden re-verification on every target.

## 2. Windows SDK

Needed for import libraries only (`kernel32.lib`, `user32.lib`, `gdi32.lib`, `ole32.lib`, `d3d11.lib`, `dxgi.lib`, `avrt.lib`, `ntdll.lib`). No SDK headers are used by the runtime.

Install through the Visual Studio Installer ("Windows 11 SDK") or `winget install Microsoft.WindowsSDK.10.0.26100`. Expected path:

```
C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64
```

The build program picks the newest folder under `Windows Kits\10\Lib`. Override with the `BMF_WINSDK` env var (path to the `um\x64` folder).

## 3. System components (ship with Windows)

- `C:\Windows\System32\D3DCompiler_47.dll`: build-time HLSL compiler (D-14).
- `C:\Windows\System32\d3d10warp.dll`: WARP software rasterizer for headless rendering and golden images.
- `C:\Windows\System32\XInput1_4.dll`: gamepads.
- Microsoft Edge (or Chrome): headless web tests (D-04).

## 4. Verify

Run from a Git Bash or PowerShell prompt:

```
"C:\Program Files\LLVM\bin\clang.exe" --version          # first line must match toolchain.txt
dir "C:\Program Files\LLVM\bin\lld-link.exe"              # must exist
dir "C:\Program Files\LLVM\bin\wasm-ld.exe"               # must exist
"C:\Program Files\LLVM\bin\clang.exe" --print-targets     # must list wasm32 and x86-64
dir "C:\Program Files\LLVM\lib\clang\23\lib\windows\clang_rt.builtins-x86_64.lib"
```

Note: LLVM's Windows installer ships compiler-rt builtins for x86_64 Windows only. There are no wasm32 builtins; the runtime provides what it needs itself (see `docs/spikes/wasm32.md`).

## 5. Bootstrap the build program

```
bootstrap.bat            (PowerShell: .\bootstrap.bat; builds build.exe, CRT-free, no Visual Studio needed)
```

Re-run it after every change to `build.c`. `build win64 debug games/_empty` checks the whole pipeline (clang, SDK, linker) in about 0.3 s.

## 6. Optional: Visual Studio C++ headers

`build test` runs a host check (`vtable_slots`) that compares the hand-written COM declarations in `src/platform/win32/win32_api.h` with the Windows SDK headers. It needs the MSVC C headers of a Visual Studio install (the newest `VC\Tools\MSVC\<ver>`, or the `BMF_MSVC` env var pointing at that folder). Without them the check is skipped and says so; nothing else needs Visual Studio.

## 7. Everyday commands

Every command prints one JSON line per result and exits nonzero on any failure.

```
build win64 debug games/hello            # out/hello/win64-debug: game.exe + game.dll (hot reload)
build win64 release games/hello          # out/hello/win64-release: one static exe
build win64 release games/hello --agent  # out/hello/win64-release-agent: release with BMF_AGENT=1 (protocol, profiler)
build test [filter]                      # host checks, unit tests, goldens, perf budgets (tools/test/README.md)
build test --update-golden NAME          # rewrite the named goldens (always name them in the change summary)
build api                                # regenerate API.md from src/bmf.h (build test fails while it is stale)
build shaders                            # HLSL to bytecode headers (also run by the other commands)
build clean                              # delete out/
```

Run a game headless under agent control: `out/hello/win64-debug/game.exe --headless --control` (protocol in SPEC §11.2, `hello` lists the commands).

Test environment notes:
- Golden images render on WARP (`d3d10warp.dll`), so they need no GPU.
- `gpu.d3d11_surface_present` shows a real window and needs an unlocked interactive session; it fails while the workstation is locked (LogonUI running).
- Perf budgets run release `BMF_AGENT=1` builds on the null GPU backend; heavy load from other processes can still push a run over budget, which is why a failing run is repeated up to 3 times.
