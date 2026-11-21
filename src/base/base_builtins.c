// base_builtins.c: symbols clang codegen expects from a C runtime (D-03, spikes P0-03/P0-04).
// - memcpy, memset, memmove, memcmp: clang emits calls for struct copies, zero-init and loops.
//   They are marked no_builtin so the optimizer cannot turn their bodies back into calls to
//   themselves (infinite recursion).
// - _fltused (x64): MSVC ABI marker referenced by any float use.
// - __chkstk (x64): stack probe for frames larger than 4 KiB.
// Under BMF_SANITIZE the CRT provides all of these (ASan intercepts the mem functions), so none
// is defined here (D-09).
#include "base_types.h"

#if !BMF_SANITIZE

#if BMF_ARCH_X64
// x64: `rep movsb` / `rep stosb` are fast on every x86-64-v2 CPU with ERMSB/FSRM, and short.

__attribute__((no_builtin)) void* memcpy(void* dst, const void* src, size_t n) {
    void* d = dst;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(src), "+c"(n) : : "memory");
    return dst;
}

__attribute__((no_builtin)) void* memset(void* dst, int c, size_t n) {
    void* d = dst;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return dst;
}

__attribute__((no_builtin)) void* memmove(void* dst, const void* src, size_t n) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    if (d <= s || d >= s + n) {   // no harmful overlap: forward copy
        __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    } else {                      // dst overlaps the tail of src: backward copy
        d += n - 1;
        s += n - 1;
        __asm__ volatile("std\n\trep movsb\n\tcld" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    }
    return dst;
}

int _fltused = 0x9875;

// MSVC x64 stack probe. In: RAX = frame size. Touches every 4 KiB page from the caller's RSP
// down to RSP-RAX in order, so the guard page moves one page at a time. Preserves every
// register except R10, R11 and flags. Does not move RSP (the caller does `sub rsp, rax`).
__asm__(
    ".globl __chkstk\n"
    "__chkstk:\n"
    "    push %rcx\n"
    "    push %rax\n"
    "    cmp $0x1000, %rax\n"
    "    lea 24(%rsp), %rcx\n"        // rcx = caller's rsp (above return address and 2 pushes)
    "    jb 2f\n"
    "1:\n"
    "    sub $0x1000, %rcx\n"
    "    test %rcx, (%rcx)\n"
    "    sub $0x1000, %rax\n"
    "    cmp $0x1000, %rax\n"
    "    ja 1b\n"
    "2:\n"
    "    sub %rax, %rcx\n"
    "    test %rcx, (%rcx)\n"
    "    pop %rax\n"
    "    pop %rcx\n"
    "    ret\n"
);

#else
// Portable versions (wasm32, arm64): byte loops. wasm normally inlines memory.copy/fill for
// compiler-generated copies, so these run only for explicit calls.

__attribute__((no_builtin)) void* memcpy(void* dst, const void* src, size_t n) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

__attribute__((no_builtin)) void* memset(void* dst, int c, size_t n) {
    u8* d = (u8*)dst;
    for (size_t i = 0; i < n; i++) d[i] = (u8)c;
    return dst;
}

__attribute__((no_builtin)) void* memmove(void* dst, const void* src, size_t n) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    if (d <= s || d >= s + n) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}
#endif

__attribute__((no_builtin)) int memcmp(const void* a, const void* b, size_t n) {
    const u8* x = (const u8*)a;
    const u8* y = (const u8*)b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

#endif // !BMF_SANITIZE

// Short names used inside bmf. They compile to the instructions above (or to inline code for
// small constant sizes).
static inline void bmf_memcpy(void* dst, const void* src, i64 n) { __builtin_memcpy(dst, src, (size_t)n); }
static inline void bmf_memset(void* dst, u8 value, i64 n) { __builtin_memset(dst, value, (size_t)n); }
static inline void bmf_memmove(void* dst, const void* src, i64 n) { __builtin_memmove(dst, src, (size_t)n); }
static inline i32 bmf_memcmp(const void* a, const void* b, i64 n) { return __builtin_memcmp(a, b, (size_t)n); }
