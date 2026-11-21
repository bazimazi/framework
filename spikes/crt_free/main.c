// Spike P0-03: CRT-free win32 exe. Throwaway.
typedef unsigned long long u64; typedef unsigned u32; typedef int i32; typedef unsigned short u16;
typedef void* HANDLE;
#define WINAPI __attribute__((stdcall))
__declspec(dllimport) void WINAPI ExitProcess(u32 code);
__declspec(dllimport) HANDLE WINAPI GetStdHandle(u32 n);
__declspec(dllimport) i32 WINAPI WriteFile(HANDLE h, const void* buf, u32 n, u32* written, void* ov);
__declspec(dllimport) HANDLE WINAPI LoadLibraryA(const char* name);
__declspec(dllimport) void* WINAPI GetProcAddress(HANDLE mod, const char* name);
__declspec(dllimport) i32 WINAPI MessageBoxA(HANDLE wnd, const char* text, const char* caption, u32 type);

static void out(const char* s) { u32 n = 0; while (s[n]) n++; u32 w; WriteFile(GetStdHandle((u32)-11), s, n, &w, 0); }
static void out_u64(u64 v) { char b[24]; i32 i = 23; b[i] = 0; do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v); out(b + i); }

typedef struct { float x, y, vx, vy; } body;
static body bodies[64];

__attribute__((noinline)) static u64 big_stack(i32 n) {
    volatile char arr[64 * 1024];            // > 4 KiB page: triggers __chkstk
    for (i32 i = 0; i < n; i++) arr[i * 97 % (64 * 1024)] = (char)i;
    u64 s = 0; for (i32 i = 0; i < 64 * 1024; i += 4096) s += (u64)arr[i];
    return s;
}

__attribute__((noinline)) static void copy_struct(body* dst, const body* src) { *dst = *src; }  // may lower to memcpy

void bmf_win32_entry(void) {
    out("crt_free: hello\n");
    // float use -> _fltused
    float f = 1.5f; for (i32 i = 0; i < 10; i++) f = f * 1.1f + 0.25f;
    out("float*1000: "); out_u64((u64)(f * 1000.0f)); out("\n");
    // zero-init large struct array -> memset
    body local[32] = {0};
    for (i32 i = 0; i < 32; i++) local[i].x = (float)i;
    for (i32 i = 0; i < 32; i++) bodies[i] = local[i];   // struct copy loop -> memcpy idiom
    copy_struct(&bodies[40], &bodies[3]);
    out("bodies[40].x: "); out_u64((u64)bodies[40].x); out("\n");
    out("big_stack: "); out_u64(big_stack(70000)); out("\n");
    // u64 divide: fine on x64 (native), included for parity with wasm spike
    volatile u64 a = 1234567890123ull, b = 97;
    out("div: "); out_u64(a / b); out("\n");
    // DLL load
    HANDLE dll = LoadLibraryA("game.dll");
    if (!dll) { out("dll load failed\n"); ExitProcess(2); }
    typedef i32 (*add_fn)(i32, i32);
    add_fn add = (add_fn)GetProcAddress(dll, "game_add");
    if (!add) { out("GetProcAddress failed\n"); ExitProcess(3); }
    out("game_add(40,2): "); out_u64((u64)add(40, 2)); out("\n");
    if (0) MessageBoxA(0, "x", "y", 0);
    ExitProcess(0);
}
