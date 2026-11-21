// Spike P0-05: sanitizers. Throwaway.
typedef unsigned long long u64; typedef unsigned u32; typedef int i32; typedef unsigned char u8; typedef void* HANDLE;
#define WINAPI __attribute__((stdcall))
__declspec(dllimport) void WINAPI ExitProcess(u32 code);
__declspec(dllimport) HANDLE WINAPI GetStdHandle(u32 n);
__declspec(dllimport) i32 WINAPI WriteFile(HANDLE h, const void* buf, u32 n, u32* written, void* ov);
__declspec(dllimport) void* WINAPI VirtualAlloc(void* a, u64 size, u32 type, u32 protect);
__declspec(dllimport) u32 WINAPI GetCommandLineA_dummy(void);
__declspec(dllimport) char* WINAPI GetCommandLineA(void);
static void out(const char* s) { u32 n = 0; while (s[n]) n++; u32 w; WriteFile(GetStdHandle((u32)-11), s, n, &w, 0); }

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HAS_ASAN 1
void __asan_poison_memory_region(void const volatile* addr, u64 size);
void __asan_unpoison_memory_region(void const volatile* addr, u64 size);
#endif
#endif

__attribute__((noinline)) i32 add_overflow(i32 a, i32 b) { return a + b; }
__attribute__((noinline)) i32 shift(i32 a, i32 s) { return a << s; }
__attribute__((noinline)) u8 read_at(volatile u8* p, i32 i) { return p[i]; }
__attribute__((noinline)) i32 misaligned(u8* p) { return *(i32*)(p + 1); }

static int has(const char* cmd, const char* w) {
    for (const char* c = cmd; *c; c++) { const char* a = c; const char* b = w; while (*b && *a == *b) { a++; b++; } if (!*b) return 1; }
    return 0;
}

void san_main(void) {
    const char* cmd = GetCommandLineA();
    volatile i32 big = 0x7fffffff;
    if (has(cmd, " ub_add")) { out("ub_add\n"); out(add_overflow(big, 1) ? "no trap\n" : "no trap\n"); }
    if (has(cmd, " ub_shift")) { out("ub_shift\n"); shift(1, 40); out("no trap\n"); }
    if (has(cmd, " ub_align")) { u8 buf[16] = {0}; out("ub_align\n"); misaligned(buf); out("no trap\n"); }
    if (has(cmd, " stack_oob")) { volatile u8 arr[16] = {0}; out("stack_oob\n"); read_at(arr, 16); out("no report\n"); }
    if (has(cmd, " arena_oob")) {
        u8* arena = (u8*)VirtualAlloc(0, 1 << 20, 0x3000, 0x04);
#ifdef HAS_ASAN
        __asan_poison_memory_region(arena, 1 << 20);      // whole arena poisoned
        __asan_unpoison_memory_region(arena, 64);          // "push" 64 bytes
#endif
        out("arena_oob\n"); read_at(arena, 64); out("no report\n");
    }
    out("clean exit\n");
}
#ifdef USE_CRT
int main(void) { san_main(); return 0; }
#else
void bmf_win32_entry(void) { san_main(); ExitProcess(0); }
#endif
