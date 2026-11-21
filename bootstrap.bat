@echo off
rem bootstrap.bat: builds build.exe from build.c (SPEC D-12). Run once, and again after build.c changes.
rem Finds clang (CLANG env var, then PATH, then %ProgramFiles%\LLVM\bin) and the newest Windows SDK
rem import-library folder (BMF_WINSDK env var overrides). build.c is CRT-free (D-04): no Visual Studio needed.
setlocal
set "BMF_CLANG=%CLANG%"
if not defined BMF_CLANG for /f "delims=" %%i in ('where clang.exe 2^>nul') do if not defined BMF_CLANG set "BMF_CLANG=%%i"
if not defined BMF_CLANG if exist "%ProgramFiles%\LLVM\bin\clang.exe" set "BMF_CLANG=%ProgramFiles%\LLVM\bin\clang.exe"
if not defined BMF_CLANG (
  echo {"bootstrap":"fail","error":"clang_not_found","detail":"install LLVM, see docs/setup.md"}
  exit /b 1
)
set "BMF_SDK=%BMF_WINSDK%"
if not defined BMF_SDK for /d %%d in ("%ProgramFiles(x86)%\Windows Kits\10\Lib\10.*") do if exist "%%d\um\x64\kernel32.lib" set "BMF_SDK=%%d\um\x64"
if not defined BMF_SDK (
  echo {"bootstrap":"fail","error":"windows_sdk_not_found","detail":"install the Windows SDK, see docs/setup.md"}
  exit /b 1
)
"%BMF_CLANG%" "%~dp0build.c" -o "%~dp0build.exe" ^
  -std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wno-unused-parameter ^
  -ffp-contract=off -fno-fast-math -fno-math-errno -ffreestanding -nostdlib -fno-builtin-printf ^
  -fno-exceptions -fno-asynchronous-unwind-tables -fno-color-diagnostics -fdiagnostics-absolute-paths ^
  --target=x86_64-pc-windows-msvc -march=x86-64-v2 -O1 -fuse-ld=lld ^
  -Wl,/entry:build_entry -Wl,/subsystem:console "-L%BMF_SDK%" -lkernel32
if errorlevel 1 (
  echo {"bootstrap":"fail","error":"compile_failed"}
  exit /b 1
)
echo {"bootstrap":"ok","out":"build.exe"}
