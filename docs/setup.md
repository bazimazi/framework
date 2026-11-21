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
build test               (unit tests, from task 1A-08 on)
```

`build win64 debug games/_empty` checks the whole pipeline (clang, SDK, linker) in about 0.3 s.
