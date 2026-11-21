# Spike helper (throwaway). Source from Git Bash.
export MSYS_NO_PATHCONV=1
CLANG="/c/Program Files/LLVM/bin/clang"
VC="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231"
K="C:/Program Files (x86)/Windows Kits/10"
SDKLIB="$K/Lib/10.0.26100.0/um/x64"
HOST=(-O1 -Wall -D_CRT_SECURE_NO_WARNINGS -Xmicrosoft-visualc-tools-root "$VC" -Xmicrosoft-windows-sdk-root "$K" -Xmicrosoft-windows-sdk-version 10.0.26100.0)
WIN=(--target=x86_64-pc-windows-msvc -std=c11 -march=x86-64-v2 -ffreestanding -nostdlib -fno-builtin-printf -ffp-contract=off -fno-fast-math -fno-asynchronous-unwind-tables -fuse-ld=lld -Wall -Wextra -Wno-unused-parameter)
WASM=(--target=wasm32 -std=c11 -nostdlib -ffreestanding -fno-builtin-printf -ffp-contract=off -fno-fast-math -msimd128 -mbulk-memory -mnontrapping-fptoint -Wall -Wextra -Wno-unused-parameter -Wl,--no-entry -Wl,--export-dynamic -Wl,--import-memory)
