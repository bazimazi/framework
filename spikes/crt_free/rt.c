// Spike P0-03: symbols clang emits that we must provide without the CRT.
typedef unsigned long long u64; typedef unsigned char u8;
int _fltused = 0x9875;   // MSVC ABI marker: any float use references it

__attribute__((no_builtin)) void* memset(void* dst, int c, u64 n) {
    u8* d = (u8*)dst; for (u64 i = 0; i < n; i++) d[i] = (u8)c; return dst;
}
__attribute__((no_builtin)) void* memcpy(void* dst, const void* src, u64 n) {
    u8* d = (u8*)dst; const u8* s = (const u8*)src; for (u64 i = 0; i < n; i++) d[i] = s[i]; return dst;
}
__attribute__((no_builtin)) void* memmove(void* dst, const void* src, u64 n) {
    u8* d = (u8*)dst; const u8* s = (const u8*)src;
    if (d < s) { for (u64 i = 0; i < n; i++) d[i] = s[i]; } else { for (u64 i = n; i > 0; i--) d[i - 1] = s[i - 1]; }
    return dst;
}
__attribute__((no_builtin)) int memcmp(const void* a, const void* b, u64 n) {
    const u8* x = (const u8*)a; const u8* y = (const u8*)b;
    for (u64 i = 0; i < n; i++) if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    return 0;
}
// x64 MSVC stack probe. In: RAX = frame size. Touches each 4 KiB page from RSP down
// to RSP-RAX so the guard page moves in order. Must preserve every register except
// R10, R11 and flags. Does not move RSP (the caller does `sub rsp, rax`).
__asm__(
    ".globl __chkstk\n"
    "__chkstk:\n"
    "    push %rcx\n"
    "    push %rax\n"
    "    cmp $0x1000, %rax\n"
    "    lea 24(%rsp), %rcx\n"          // rcx = caller rsp (above return addr + 2 pushes)
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
