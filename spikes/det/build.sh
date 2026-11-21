source ../env.sh
set -e
W2=("${WIN[@]}" -O2 -fno-math-errno)
L=(-Wl,/entry:bmf_win32_entry -Wl,/subsystem:console "-L$SDKLIB" -lkernel32)
RT=../crt_free/rt.c
"$CLANG" "${W2[@]}" det.c $RT -o out/det_scalar.exe "${L[@]}"
"$CLANG" "${W2[@]}" -DUSE_SIMD det.c $RT -o out/det_simd.exe "${L[@]}"
"$CLANG" "${WIN[@]}" -O0 -fno-math-errno det.c $RT -o out/det_o0.exe "${L[@]}"
"$CLANG" "${W2[@]}" -flto det.c $RT -o out/det_lto.exe "${L[@]}" -Wl,/include:_fltused
"$CLANG" "${W2[@]}" -DBREAK_RSQRT det.c $RT -o out/det_rsqrt.exe "${L[@]}"
"$CLANG" "${W2[@]}" -DBREAK_FTZ det.c $RT -o out/det_ftz.exe "${L[@]}"
"$CLANG" "${W2[@]}" -DBREAK_CAST det.c $RT -o out/det_cast.exe "${L[@]}"
"$CLANG" "${W2[@]/-ffp-contract=off/-ffp-contract=fast}" -march=x86-64-v3 det.c $RT -o out/det_fma.exe "${L[@]}"
"$CLANG" "${W2[@]}" -march=x86-64-v3 det.c $RT -o out/det_v3.exe "${L[@]}"
WS=("${WASM[@]}" -O2 -fno-math-errno)
"$CLANG" "${WS[@]}" det.c -o out/det.wasm
"$CLANG" "${WS[@]}" -DUSE_SIMD det.c -o out/det_simd.wasm
"$CLANG" "${WS[@]/-O2/-O0}" det.c -o out/det_o0.wasm
"$CLANG" "${WS[@]}" -DBREAK_CAST det.c -o out/det_cast.wasm
"$CLANG" "${WS[@]/-ffp-contract=off/-ffp-contract=fast}" -mrelaxed-simd -DUSE_SIMD det.c -o out/det_relaxed.wasm
echo built
