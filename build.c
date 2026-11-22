// build.c: the bmf build program (SPEC §4, D-12). CRT-free (D-04); built by bootstrap.bat.
//
//   build win64 debug|release <game_dir> [--agent]
//                                          runtime exe (+ game.dll in debug) in out/<game>/win64-<config>/;
//                                          --agent: release with BMF_AGENT=1 in win64-release-agent/
//   build shaders                          shaders/d3d11/*.hlsl to out/gen/shaders_d3d11.h (also run
//                                          first by win64 and test when shader files changed)
//   build test [filter] [--update-golden NAME]
//                                          host checks (vtable slots vs SDK headers; the public header:
//                                          contract comments, games compile with bmf.h alone), then
//                                          build and run the unit-test runner (out/test/win64-debug/),
//                                          then build every game that has a tests/ folder (win64 debug)
//                                          and run the golden tests and the lint (test.exe --golden,
//                                          tools/test/golden.c) unless a filter excludes them (a filter
//                                          runs them only when it starts with "golden" or "lint").
//                                          --update-golden NAME rewrites the selected goldens. Bench
//                                          scenes (tests/bench/*) are built and checked like games. Then
//                                          the perf budgets (test.exe --perf, tools/test/perf.c) of every
//                                          folder with a perf.budget, built with --agent (a filter runs
//                                          them only when it starts with "perf").
//   build api                              API.md from src/bmf.h (contract-comment check, token target)
//   build clean                            delete out/
//
// Every binary is one translation unit (D-08): build.c writes a tiny generated root to
// out/gen/<...>/<binary>.c that sets the build mode and #includes src/bmf.c (plus game.c or the
// test runner), then runs one clang command that compiles and links it. Exception: the debug
// runtime exe (BMF_GAME_LINK_DLL) is compiled to an object first; every external function whose
// name starts with bmf_ is read from the object's COFF symbol table into a .def file, and the link
// exports them and writes the import library game.lib. The game DLL (built after the exe) holds
// only the game and the engine headers and links against game.lib: one engine, in the exe.
// Every generated root defines BMF_BUILD_ID: a hash of the clang version, the config, the binary
// kind and every input file (contents and path relative to the framework or game folder); the
// debug game DLL also exports it as bmf_game_build_id() (replay headers, task 1A-33).
// A binary is skipped when the hash of its flags, clang version, generated root and every input
// file is unchanged since its last successful build (stamp file next to the output).
// Output: clang diagnostics pass through unchanged (`file:line:col: error: ...`); the last line
// is one JSON summary. Exit code 0 on success, 1 on failure.

#include "src/bmf.h"
#include "src/base/base_builtins.c"
#include "src/platform/win32/win32_api.h"

// ================================================================ memory: one linear arena
// The build program is short-lived, so it never frees. 256 MiB is committed lazily by the OS.
static u8* arena_base;
static i64 arena_used;
static const i64 ARENA_SIZE = 256ll << 20;

static void* push(i64 size) {
    i64 aligned = (arena_used + 15) & ~15ll;
    if (aligned + size > ARENA_SIZE) bmf_panic("build: arena exhausted");
    arena_used = aligned + size;
    return arena_base + aligned;   // fresh VirtualAlloc memory is zero and never reused
}

// ================================================================ strings (NUL-terminated UTF-8)
static i32 cstr_len(const char* s) { i32 n = 0; while (s[n]) n++; return n; }

static b32 cstr_eq(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static b32 cstr_ends_with(const char* s, const char* suffix) {
    i32 n = cstr_len(s), m = cstr_len(suffix);
    return n >= m && cstr_eq(s + n - m, suffix);
}

static i32 cstr_cmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (i32)(u8)*a - (i32)(u8)*b;
}

static char* cstr_copy(const char* s) {
    i32 n = cstr_len(s);
    char* out = (char*)push(n + 1);
    bmf_memcpy(out, s, n);
    return out;
}

// String builder over a fixed buffer taken from the arena.
typedef struct { char* buf; i32 len; i32 cap; } sbuf;

static sbuf sb_make(i32 cap) { sbuf b = { (char*)push(cap), 0, cap }; return b; }

static void sb_put(sbuf* b, const char* s) {
    i32 n = cstr_len(s);
    if (b->len + n + 1 > b->cap) bmf_panic("build: string buffer overflow");
    bmf_memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = 0;
}

static void sb_put_char(sbuf* b, char c) { char s[2] = { c, 0 }; sb_put(b, s); }

static void sb_put_i64(sbuf* b, i64 v) {
    char tmp[24]; i32 i = 23; tmp[i] = 0;
    b32 negative = v < 0;
    u64 u = negative ? (u64)0 - (u64)v : (u64)v;
    do { tmp[--i] = "0123456789"[u % 10]; u /= 10; } while (u);
    if (negative) tmp[--i] = '-';
    sb_put(b, tmp + i);
}

static void sb_put_hex64(sbuf* b, u64 v) {
    char tmp[17];
    for (i32 i = 0; i < 16; i++) tmp[i] = "0123456789abcdef"[(v >> ((15 - i) * 4)) & 15];
    tmp[16] = 0;
    sb_put(b, tmp);
}

// Appends s as a JSON string literal (quotes included).
static void sb_put_json(sbuf* b, const char* s) {
    sb_put_char(b, '"');
    for (; *s; s++) {
        u8 c = (u8)*s;
        if (c == '"' || c == '\\') { sb_put_char(b, '\\'); sb_put_char(b, (char)c); }
        else if (c < 0x20) { sb_put(b, "\\u00"); sb_put_char(b, "0123456789abcdef"[c >> 4]); sb_put_char(b, "0123456789abcdef"[c & 15]); }
        else sb_put_char(b, (char)c);
    }
    sb_put_char(b, '"');
}

static char* cat2(const char* a, const char* b) { sbuf s = sb_make(cstr_len(a) + cstr_len(b) + 1); sb_put(&s, a); sb_put(&s, b); return s.buf; }
static char* cat3(const char* a, const char* b, const char* c) { return cat2(cat2(a, b), c); }

// Path with forward slashes (for #include lines and JSON output).
static char* slashes_forward(const char* path) {
    char* out = cstr_copy(path);
    for (char* p = out; *p; p++) if (*p == '\\') *p = '/';
    return out;
}

// ================================================================ UTF-8 <-> UTF-16 for Win32 W APIs
static WCHAR* wide(const char* s) {
    i32 n = MultiByteToWideChar(WIN32_CP_UTF8, 0, s, -1, 0, 0);
    WCHAR* out = (WCHAR*)push((i64)n * 2);
    MultiByteToWideChar(WIN32_CP_UTF8, 0, s, -1, out, n);
    return out;
}

static char* narrow(const WCHAR* s) {
    i32 n = WideCharToMultiByte(WIN32_CP_UTF8, 0, s, -1, 0, 0, 0, 0);
    char* out = (char*)push(n);
    WideCharToMultiByte(WIN32_CP_UTF8, 0, s, -1, out, n, 0, 0);
    return out;
}

// ================================================================ stdout / stderr
static void write_handle(i32 std_handle, const char* s) {
    u32 written = 0;
    WriteFile(GetStdHandle((u32)std_handle), s, (u32)cstr_len(s), &written, 0);
}
static void out_line(const char* s) { write_handle(WIN32_STD_OUTPUT_HANDLE, s); write_handle(WIN32_STD_OUTPUT_HANDLE, "\n"); }

void bmf_platform_fatal(const char* file, i32 line, const char* kind, const char* what) {
    sbuf b = { 0 };
    char storage[1024];
    b.buf = storage; b.cap = (i32)sizeof storage; storage[0] = 0;
    sb_put(&b, "{\"build\":\"fail\",\"error\":"); sb_put_json(&b, kind);
    sb_put(&b, ",\"detail\":"); sb_put_json(&b, what);
    sb_put(&b, ",\"file\":"); sb_put_json(&b, file);
    sb_put(&b, ",\"line\":"); sb_put_i64(&b, line); sb_put(&b, "}\n");
    write_handle(WIN32_STD_OUTPUT_HANDLE, b.buf);
    ExitProcess(1);
}

// ================================================================ hashing (FNV-1a 64; replaced by bmf_hash in 1A-07)
static u64 hash_bytes(u64 h, const void* data, i64 len) {
    const u8* p = (const u8*)data;
    for (i64 i = 0; i < len; i++) { h ^= p[i]; h *= 0x100000001B3ull; }
    return h;
}
static b32 starts_with(const char* s, const char* prefix) { while (*prefix) { if (*s++ != *prefix++) return 0; } return 1; }
static u64 hash_cstr(u64 h, const char* s) { return hash_bytes(h, s, cstr_len(s) + 1); }   // includes NUL as separator
static const u64 HASH_SEED = 0xCBF29CE484222325ull;

// ================================================================ files and directories
static b32 file_exists(const char* path) {
    u32 a = GetFileAttributesW(wide(path));
    return a != (u32)WIN32_INVALID_FILE_ATTRIBUTES && !(a & WIN32_FILE_ATTRIBUTE_DIRECTORY);
}

static b32 dir_exists(const char* path) {
    u32 a = GetFileAttributesW(wide(path));
    return a != (u32)WIN32_INVALID_FILE_ATTRIBUTES && (a & WIN32_FILE_ATTRIBUTE_DIRECTORY);
}

// Reads a whole file into the arena, NUL-terminated. Returns 0 if it cannot be opened.
static u8* read_file(const char* path, i64* out_len) {
    HANDLE f = CreateFileW(wide(path), (u32)WIN32_GENERIC_READ, WIN32_FILE_SHARE_READ | WIN32_FILE_SHARE_WRITE | WIN32_FILE_SHARE_DELETE,
                           0, WIN32_OPEN_EXISTING, WIN32_FILE_ATTRIBUTE_NORMAL, 0);
    if (f == WIN32_INVALID_HANDLE_VALUE) return 0;
    i64 size = 0;
    GetFileSizeEx(f, &size);
    u8* data = (u8*)push(size + 1);
    i64 done = 0;
    while (done < size) {
        u32 chunk = (u32)((size - done) > (1 << 30) ? (1 << 30) : (size - done));
        u32 got = 0;
        if (!ReadFile(f, data + done, chunk, &got, 0) || got == 0) break;
        done += got;
    }
    CloseHandle(f);
    if (done != size) return 0;
    *out_len = size;
    return data;
}

// Writes a file atomically: write `path.tmp`, then rename over `path`.
static b32 write_file(const char* path, const void* data, i64 len) {
    char* tmp = cat2(path, ".tmp");
    HANDLE f = CreateFileW(wide(tmp), (u32)WIN32_GENERIC_WRITE, 0, 0, WIN32_CREATE_ALWAYS, WIN32_FILE_ATTRIBUTE_NORMAL, 0);
    if (f == WIN32_INVALID_HANDLE_VALUE) return 0;
    u32 written = 0;
    b32 ok = WriteFile(f, data, (u32)len, &written, 0) && (i64)written == len;
    CloseHandle(f);
    if (!ok) return 0;
    return MoveFileExW(wide(tmp), wide(path), WIN32_MOVEFILE_REPLACE_EXISTING | WIN32_MOVEFILE_WRITE_THROUGH) != 0;
}

// Creates `path` and every missing parent. Path uses backslashes.
static void make_dirs(const char* path) {
    char* p = cstr_copy(path);
    for (char* c = p + 3; *c; c++) {   // skip "C:\"
        if (*c == '\\') { *c = 0; CreateDirectoryW(wide(p), 0); *c = '\\'; }
    }
    CreateDirectoryW(wide(p), 0);
}

// Collects every file under `dir` (recursive) whose name ends with one of `exts`, sorted by
// path so the input hash does not depend on directory enumeration order.
typedef struct { char** items; i32 count; i32 cap; } path_list;

static void list_files(path_list* list, const char* dir, const char* const* exts) {   // exts is 0-terminated
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(wide(cat2(dir, "\\*")), &fd);
    if (find == WIN32_INVALID_HANDLE_VALUE) return;
    do {
        char* name = narrow(fd.cFileName);
        if (cstr_eq(name, ".") || cstr_eq(name, "..")) continue;
        char* full = cat3(dir, "\\", name);
        if (fd.dwFileAttributes & WIN32_FILE_ATTRIBUTE_DIRECTORY) {
            list_files(list, full, exts);
            continue;
        }
        b32 match = 0;
        for (i32 i = 0; exts[i]; i++) if (cstr_ends_with(name, exts[i])) match = 1;
        if (!match) continue;
        if (list->count == list->cap) bmf_panic("build: too many input files");
        list->items[list->count++] = full;
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

static void sort_paths(path_list* list) {   // insertion sort; input lists are small
    for (i32 i = 1; i < list->count; i++) {
        char* key = list->items[i];
        i32 j = i - 1;
        while (j >= 0 && cstr_cmp(list->items[j], key) > 0) { list->items[j + 1] = list->items[j]; j--; }
        list->items[j + 1] = key;
    }
}

// Deletes a directory tree. Refuses any path that does not end in "\out" (safety).
static b32 delete_tree(const char* dir) {
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(wide(cat2(dir, "\\*")), &fd);
    if (find != WIN32_INVALID_HANDLE_VALUE) {
        do {
            char* name = narrow(fd.cFileName);
            if (cstr_eq(name, ".") || cstr_eq(name, "..")) continue;
            char* full = cat3(dir, "\\", name);
            if (fd.dwFileAttributes & WIN32_FILE_ATTRIBUTE_DIRECTORY) delete_tree(full);
            else {
                SetFileAttributesW(wide(full), WIN32_FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(wide(full));
            }
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    return RemoveDirectoryW(wide(dir)) != 0;
}

static char* full_path(const char* path) {
    WCHAR buf[2048];
    u32 n = GetFullPathNameW(wide(path), 2048, buf, 0);
    if (n == 0 || n >= 2048) return 0;
    char* out = narrow(buf);
    i32 len = cstr_len(out);
    while (len > 3 && (out[len - 1] == '\\' || out[len - 1] == '/')) out[--len] = 0;
    for (char* p = out; *p; p++) if (*p == '/') *p = '\\';
    return out;
}

static char* path_dir(const char* path) {   // "a\b\c" -> "a\b"
    char* out = cstr_copy(path);
    i32 len = cstr_len(out);
    while (len > 0 && out[len - 1] != '\\') len--;
    if (len > 0) out[len - 1] = 0;
    return out;
}

static const char* path_name(const char* path) {   // "a\b\c" -> "c"
    const char* name = path;
    for (const char* p = path; *p; p++) if (*p == '\\' || *p == '/') name = p + 1;
    return name;
}

static char* env_var(const char* name) {
    WCHAR buf[4096];
    u32 n = GetEnvironmentVariableW(wide(name), buf, 4096);
    if (n == 0 || n >= 4096) return 0;
    return narrow(buf);
}

// ================================================================ processes
// Appends one argument with Windows command-line quoting (CommandLineToArgvW rules).
static void sb_put_arg(sbuf* b, const char* arg) {
    b32 needs_quotes = arg[0] == 0;
    for (const char* p = arg; *p; p++) if (*p == ' ' || *p == '\t' || *p == '"') needs_quotes = 1;
    if (b->len) sb_put_char(b, ' ');
    if (!needs_quotes) { sb_put(b, arg); return; }
    sb_put_char(b, '"');
    i32 backslashes = 0;
    for (const char* p = arg; *p; p++) {
        if (*p == '\\') { backslashes++; continue; }
        if (*p == '"') { for (i32 i = 0; i < backslashes * 2 + 1; i++) sb_put_char(b, '\\'); }
        else { for (i32 i = 0; i < backslashes; i++) sb_put_char(b, '\\'); }
        backslashes = 0;
        sb_put_char(b, *p);
    }
    for (i32 i = 0; i < backslashes * 2; i++) sb_put_char(b, '\\');
    sb_put_char(b, '"');
}

// Runs `args[0]` with the given arguments and waits. stdout/stderr are inherited (diagnostics
// pass through unchanged) unless `capture` is non-null, in which case stdout is captured into it
// (NUL-terminated, at most capture_cap-1 bytes). Returns the exit code, or -1 if it cannot start.
static i32 run_process(const char* const* args, i32 arg_count, char* capture, i32 capture_cap) {
    sbuf cmd = sb_make(64 * 1024);
    for (i32 i = 0; i < arg_count; i++) sb_put_arg(&cmd, args[i]);

    STARTUPINFOW si;
    bmf_memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = WIN32_STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle((u32)WIN32_STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle((u32)WIN32_STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle((u32)WIN32_STD_ERROR_HANDLE);
    HANDLE pipe_read = 0, pipe_write = 0;
    if (capture) {
        SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), 0, 1 };
        if (!CreatePipe(&pipe_read, &pipe_write, &sa, 0)) return -1;
        SetHandleInformation(pipe_read, WIN32_HANDLE_FLAG_INHERIT, 0);
        si.hStdOutput = pipe_write;
    }
    PROCESS_INFORMATION pi;
    bmf_memset(&pi, 0, sizeof pi);
    b32 started = CreateProcessW(wide(args[0]), wide(cmd.buf), 0, 0, 1, 0, 0, 0, &si, &pi) != 0;
    if (capture) CloseHandle(pipe_write);
    if (!started) { if (capture) CloseHandle(pipe_read); return -1; }
    if (capture) {
        i32 len = 0;
        for (;;) {
            u32 got = 0;
            if (len >= capture_cap - 1) break;
            if (!ReadFile(pipe_read, capture + len, (u32)(capture_cap - 1 - len), &got, 0) || got == 0) break;
            len += (i32)got;
        }
        capture[len] = 0;
        CloseHandle(pipe_read);
    }
    WaitForSingleObject(pi.hProcess, (u32)WIN32_INFINITE);
    u32 code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (i32)code;
}

// ================================================================ command line
static i32 parse_args(char** argv, i32 cap) {   // CommandLineToArgvW rules for the common cases
    char* s = narrow(GetCommandLineW());
    i32 argc = 0;
    while (*s && argc < cap) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        sbuf arg = sb_make(cstr_len(s) + 1);
        b32 quoted = 0;
        while (*s && (quoted || (*s != ' ' && *s != '\t'))) {
            if (*s == '"') { quoted = !quoted; s++; continue; }
            if (*s == '\\') {
                i32 n = 0; while (s[n] == '\\') n++;
                if (s[n] == '"') { for (i32 i = 0; i < n / 2; i++) sb_put_char(&arg, '\\'); s += n; if (n % 2) { sb_put_char(&arg, '"'); s++; } continue; }
                for (i32 i = 0; i < n; i++) sb_put_char(&arg, '\\');
                s += n; continue;
            }
            sb_put_char(&arg, *s++);
        }
        argv[argc++] = arg.buf;
    }
    return argc;
}

// ================================================================ toolchain and SDK discovery
typedef struct {
    char* root;          // framework root (folder of build.exe)
    char* clang;         // full path of clang.exe
    char* clang_version; // e.g. "23.1.6"
    char* sdk_lib;       // ...\Windows Kits\10\Lib\<ver>\um\x64
} toolchain;

static i64 build_start_ticks, ticks_per_second;

static i64 elapsed_ms(void) {
    i64 now = 0;
    QueryPerformanceCounter(&now);
    return (now - build_start_ticks) * 1000 / ticks_per_second;
}

static __attribute__((noreturn)) void fail_json(const char* error, const char* detail) {
    sbuf b = sb_make(8192);
    sb_put(&b, "{\"build\":\"fail\",\"error\":"); sb_put_json(&b, error);
    if (detail) { sb_put(&b, ",\"detail\":"); sb_put_json(&b, detail); }
    sb_put(&b, ",\"ms\":"); sb_put_i64(&b, elapsed_ms()); sb_put(&b, "}");
    out_line(b.buf);
    ExitProcess(1);
}

// Splits "10.0.26100.0" into a comparable u64 (16 bits per component).
static u64 version_key(const char* v) {
    u64 key = 0;
    for (i32 part = 0; part < 4; part++) {
        u64 n = 0;
        while (*v >= '0' && *v <= '9') { n = n * 10 + (u64)(*v - '0'); v++; }
        key = (key << 16) | (n & 0xFFFF);
        if (*v == '.') v++;
    }
    return key;
}

static toolchain find_toolchain(void) {
    toolchain tc = { 0 };
    WCHAR exe[2048];
    GetModuleFileNameW(0, exe, 2048);
    tc.root = path_dir(narrow(exe));

    // clang: CLANG env var, then PATH, then the default LLVM install folder.
    char* clang = env_var("CLANG");
    if (clang && !file_exists(clang)) fail_json("clang_not_found", cat2("CLANG env var points to a missing file: ", clang));
    if (!clang) {
        WCHAR found[2048];
        u32 n = SearchPathW(0, wide("clang.exe"), 0, 2048, found, 0);
        if (n > 0 && n < 2048) clang = narrow(found);
    }
    if (!clang) {
        char* program_files = env_var("ProgramFiles");
        char* candidate = cat2(program_files ? program_files : "C:\\Program Files", "\\LLVM\\bin\\clang.exe");
        if (file_exists(candidate)) clang = candidate;
    }
    if (!clang) fail_json("clang_not_found", "install LLVM (see docs/setup.md) or set the CLANG env var");
    tc.clang = clang;

    // Version check against toolchain.txt ("llvm X.Y.Z").
    char version_out[4096];
    const char* version_args[2] = { tc.clang, "--version" };
    if (run_process(version_args, 2, version_out, (i32)sizeof version_out) != 0) fail_json("clang_failed", "clang --version");
    const char* marker = "clang version ";
    char* found_version = 0;
    for (char* p = version_out; *p; p++) {
        const char* m = marker; char* q = p;
        while (*m && *q == *m) { m++; q++; }
        if (!*m) {
            sbuf v = sb_make(64);
            while (*q && *q != ' ' && *q != '\r' && *q != '\n' && v.len < 60) sb_put_char(&v, *q++);
            found_version = v.buf;
            break;
        }
    }
    if (!found_version) fail_json("clang_version_unknown", version_out);
    i64 pin_len = 0;
    char* pin = (char*)read_file(cat2(tc.root, "\\toolchain.txt"), &pin_len);
    if (!pin) fail_json("toolchain_txt_missing", cat2(tc.root, "\\toolchain.txt"));
    char* expected = pin;
    if (pin_len >= 5 && pin[0] == 'l' && pin[1] == 'l' && pin[2] == 'v' && pin[3] == 'm' && pin[4] == ' ') expected = pin + 5;
    for (char* p = expected; *p; p++) if (*p == '\r' || *p == '\n' || *p == ' ') { *p = 0; break; }
    if (!cstr_eq(expected, found_version)) {
        sbuf d = sb_make(1024);
        sb_put(&d, "toolchain.txt pins llvm "); sb_put(&d, expected); sb_put(&d, " but "); sb_put(&d, tc.clang);
        sb_put(&d, " is "); sb_put(&d, found_version);
        fail_json("toolchain_mismatch", d.buf);
    }
    tc.clang_version = found_version;

    // Windows SDK import libraries: BMF_WINSDK env var, else the newest Windows Kits\10\Lib\<ver>.
    char* sdk = env_var("BMF_WINSDK");
    if (!sdk) {
        char* program_files_x86 = env_var("ProgramFiles(x86)");
        char* lib_root = cat2(program_files_x86 ? program_files_x86 : "C:\\Program Files (x86)", "\\Windows Kits\\10\\Lib");
        WIN32_FIND_DATAW fd;
        HANDLE find = FindFirstFileW(wide(cat2(lib_root, "\\*")), &fd);
        u64 best_key = 0;
        if (find != WIN32_INVALID_HANDLE_VALUE) {
            do {
                char* name = narrow(fd.cFileName);
                if (!(fd.dwFileAttributes & WIN32_FILE_ATTRIBUTE_DIRECTORY) || name[0] < '0' || name[0] > '9') continue;
                char* candidate = cat3(lib_root, "\\", cat2(name, "\\um\\x64"));
                u64 key = version_key(name);
                if (key > best_key && file_exists(cat2(candidate, "\\kernel32.lib"))) { best_key = key; sdk = candidate; }
            } while (FindNextFileW(find, &fd));
            FindClose(find);
        }
    }
    if (!sdk || !file_exists(cat2(sdk, "\\kernel32.lib"))) fail_json("windows_sdk_not_found", "install the Windows SDK (see docs/setup.md) or set BMF_WINSDK to its um\\x64 folder");
    tc.sdk_lib = sdk;
    return tc;
}

// ================================================================ flag sets (D-09, as amended)
// One table per (target, config). Changing any flag changes every input hash.
static const char* const FLAGS_COMMON[] = {
    "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wconversion", "-Wshadow", "-Wstrict-prototypes",
    "-Wno-unused-parameter", "-ffp-contract=off", "-fno-fast-math", "-fno-math-errno",
    "-ffreestanding", "-nostdlib", "-fno-builtin-printf",
    "-fvisibility=hidden", "-fno-exceptions", "-fno-asynchronous-unwind-tables",
    "-fno-color-diagnostics", "-fdiagnostics-absolute-paths", 0,
};
static const char* const FLAGS_WIN64[] = { "--target=x86_64-pc-windows-msvc", "-march=x86-64-v2", "-fuse-ld=lld", 0 };
static const char* const FLAGS_WIN64_DEBUG[] = { "-O0", "-g", "-DBMF_DEBUG=1", "-fsanitize=undefined", "-fsanitize-trap=undefined", 0 };
static const char* const FLAGS_WIN64_RELEASE[] = { "-O2", "-DBMF_DEBUG=0", "-flto", "-Wl,/include:_fltused", 0 };
static const char* const LINK_WIN64_EXE[] = { "-Wl,/entry:bmf_win32_entry", "-Wl,/subsystem:console", "-Wl,/noimplib", 0 };
static const char* const LINK_WIN64_EXE_EXPORTS[] = { "-Wl,/entry:bmf_win32_entry", "-Wl,/subsystem:console", 0 };   // debug exe: /def + /implib
static const char* const LINK_WIN64_DLL[] = { "-shared", "-Wl,/entry:bmf_dll_entry", "-Wl,/noimplib", 0 };
static const char* const LIBS_WIN64[] = { "-lkernel32", "-lsynchronization", "-luser32", 0 };

// Flags that are never allowed (D-09): checked against every table at startup.
static const char* const FLAGS_BANNED[] = {
    "-mrelaxed-simd", "-ffast-math", "-ffinite-math-only", "-fno-honor-nans", "-fno-honor-infinities",
    "-fassociative-math", "-freciprocal-math", "-fno-signed-zeros", "-fapprox-func",
    "-funsafe-math-optimizations", "-ffp-contract=fast", "-ffp-contract=on", "-ffp-contract=fast-honor-pragmas", 0,
};


typedef struct { const char** items; i32 count; } arg_list;

static void args_add(arg_list* l, const char* a) { l->items[l->count++] = a; }
static void args_add_all(arg_list* l, const char* const* a) { for (i32 i = 0; a[i]; i++) args_add(l, a[i]); }   // a is 0-terminated

static void check_banned(const arg_list* l) {
    for (i32 i = 0; i < l->count; i++)
        for (i32 j = 0; FLAGS_BANNED[j]; j++)
            if (cstr_eq(l->items[i], FLAGS_BANNED[j])) fail_json("banned_flag", l->items[i]);
}

// ================================================================ building one binary
typedef enum { BIN_RUNTIME_EXE, BIN_GAME_DLL, BIN_TESTS } binary_kind;
typedef enum { BUILD_FAILED, BUILD_BUILT, BUILD_UP_TO_DATE } build_status;

typedef struct {
    binary_kind kind;
    const char* name;       // "runtime", "game_dll", "tests" (for JSON and generated file names)
    b32 release;
    const char* game_dir;   // full path, or 0
    const char* gen_dir;    // folder for the generated root
    const char* out_file;   // full path of the .exe/.dll
    b32 agent;              // release with BMF_AGENT=1 (agent tools kept: perf budgets, SPEC §15.5)
} binary_desc;

static const char* const SOURCE_EXTS[] = { ".c", ".h", ".hlsl", ".wgsl", ".inc", 0 };

// ================================================================ exports of the debug runtime exe
static u32 read_u16_le(const u8* p) { return (u32)p[0] | (u32)p[1] << 8; }
static u32 read_u32_le(const u8* p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

// Writes a .def file exporting every external function defined in the COFF object `obj_path`
// (regular or /bigobj format) whose name starts with "bmf_". Returns the export count, or -1 if
// the object cannot be read or is malformed.
static i32 write_exports_def(const char* obj_path, const char* def_path) {
    i64 len = 0;
    const u8* d = read_file(obj_path, &len);
    if (!d || len < 20) return -1;
    b32 big = len >= 56 && read_u16_le(d) == 0 && read_u16_le(d + 2) == 0xFFFF;
    i64 table = big ? read_u32_le(d + 48) : read_u32_le(d + 8);
    i64 symbol_count = big ? read_u32_le(d + 52) : read_u32_le(d + 12);
    i64 record = big ? 20 : 18;
    i64 strings = table + symbol_count * record;
    if (table <= 0 || strings + 4 > len) return -1;
    sbuf def = sb_make(256 * 1024);
    sb_put(&def, "EXPORTS\r\n");
    i32 exports = 0;
    for (i64 i = 0; i < symbol_count; i++) {
        const u8* sym = d + table + i * record;
        i32 section = big ? (i32)read_u32_le(sym + 12) : (i32)(i16)read_u16_le(sym + 12);
        u32 type = read_u16_le(sym + (big ? 16 : 14));
        u32 storage = sym[big ? 18 : 16];
        u32 aux = sym[big ? 19 : 17];
        if (storage == 2 && section > 0 && type == 0x20) {   // IMAGE_SYM_CLASS_EXTERNAL, defined, function
            char name[256];
            i32 n = 0;
            if (read_u32_le(sym) == 0) {   // long name: offset into the string table
                i64 at = strings + read_u32_le(sym + 4);
                while (at + n < len && d[at + n] && n < 255) { name[n] = (char)d[at + n]; n++; }
            } else {
                while (n < 8 && sym[n]) { name[n] = (char)sym[n]; n++; }
            }
            name[n] = 0;
            if (name[0] == 'b' && name[1] == 'm' && name[2] == 'f' && name[3] == '_') {
                sb_put(&def, "  "); sb_put(&def, name); sb_put(&def, "\r\n");
                exports++;
            }
        }
        i += aux;
    }
    if (!write_file(def_path, def.buf, def.len)) return -1;
    return exports;
}

static build_status build_binary(const toolchain* tc, const binary_desc* d) {
    b32 includes_game = d->kind == BIN_GAME_DLL || (d->kind == BIN_RUNTIME_EXE && d->release);
    // 0. Input files: every source the binary is compiled from.
    char* list_items[8192];
    path_list inputs = { list_items, 0, 8192 };
    list_files(&inputs, cat2(tc->root, "\\src"), SOURCE_EXTS);
    list_files(&inputs, cat2(tc->root, "\\shaders"), SOURCE_EXTS);
    char* generated_shaders = cat2(tc->root, "\\out\\gen\\shaders_d3d11.h");   // written by generate_shaders, which runs first
    if (file_exists(generated_shaders)) inputs.items[inputs.count++] = generated_shaders;
    char* generated_test_shaders = cat2(tc->root, "\\out\\gen\\test_shaders_d3d11.h");
    if (d->kind == BIN_TESTS && file_exists(generated_test_shaders)) inputs.items[inputs.count++] = generated_test_shaders;
    if (includes_game) list_files(&inputs, d->game_dir, SOURCE_EXTS);
    if (d->kind == BIN_TESTS) {
        list_files(&inputs, cat2(tc->root, "\\tools\\test"), SOURCE_EXTS);
        list_files(&inputs, cat2(tc->root, "\\tests"), SOURCE_EXTS);
    }
    sort_paths(&inputs);
    // Build id (replay headers, SPEC §11.4): clang version, config, binary kind and every input
    // file's contents and path relative to the framework or game folder, so it does not depend on
    // where the repository is checked out. The debug exe's id covers the runtime only (a game
    // edit must not rebuild the running exe); the game DLL exports the id of its own build and
    // the runtime combines both.
    u64 source_id = hash_cstr(HASH_SEED, tc->clang_version);
    source_id = hash_cstr(source_id, d->release ? "release" : "debug");
    if (d->agent) source_id = hash_cstr(source_id, "agent");
    source_id = hash_bytes(source_id, &d->kind, sizeof d->kind);
    i32 root_len = cstr_len(tc->root);
    i32 game_len = d->game_dir ? cstr_len(d->game_dir) : 0;
    for (i32 i = 0; i < inputs.count; i++) {
        i64 len = 0;
        u8* data = read_file(inputs.items[i], &len);
        if (!data) fail_json("read_failed", inputs.items[i]);
        const char* name = inputs.items[i];
        if (game_len && starts_with(name, d->game_dir)) name += game_len;
        else if (starts_with(name, tc->root)) name += root_len;
        source_id = hash_cstr(source_id, slashes_forward(name));
        source_id = hash_bytes(source_id, &len, sizeof len);
        source_id = hash_bytes(source_id, data, len);
    }

    // 1. Generated unity root (D-08).
    char* src = slashes_forward(cat2(tc->root, "\\src"));
    sbuf root = sb_make(8192);
    sb_put(&root, "// Generated by build.c. Do not edit.\n");
    sb_put(&root, "#define BMF_BUILD_ID 0x"); sb_put_hex64(&root, source_id); sb_put(&root, "ull\n");
    if (d->agent) sb_put(&root, "#define BMF_AGENT 1\n");
    if (d->kind == BIN_RUNTIME_EXE) {
        sb_put(&root, "#define BMF_BUILD_RUNTIME_EXE 1\n");
        sb_put(&root, d->release ? "#define BMF_GAME_LINK_STATIC 1\n" : "#define BMF_GAME_LINK_DLL 1\n");
    }
    if (d->kind == BIN_GAME_DLL) sb_put(&root, "#define BMF_BUILD_GAME_DLL 1\n");
    if (d->kind == BIN_TESTS) sb_put(&root, "#define BMF_BUILD_TESTS 1\n");
    sb_put(&root, "#include \""); sb_put(&root, src); sb_put(&root, "/bmf.c\"\n");
    if (includes_game) { sb_put(&root, "#include \""); sb_put(&root, slashes_forward(d->game_dir)); sb_put(&root, "/game.c\"\n"); }
    if (d->kind == BIN_TESTS) { sb_put(&root, "#include \""); sb_put(&root, slashes_forward(tc->root)); sb_put(&root, "/tools/test/test_main.c\"\n"); }
    if (d->kind == BIN_GAME_DLL) sb_put(&root, "BMF_EXPORT u64 bmf_game_build_id(void);\nBMF_EXPORT u64 bmf_game_build_id(void) { return BMF_BUILD_ID; }\n");
    char* root_path = cat3(d->gen_dir, "\\", cat2(d->name, ".c"));

    // 2. Command line.
    const char* items[256];
    arg_list args = { items, 0 };
    args_add(&args, tc->clang);
    args_add_all(&args, FLAGS_COMMON);
    args_add_all(&args, FLAGS_WIN64);
    if (d->release) args_add_all(&args, FLAGS_WIN64_RELEASE);
    else args_add_all(&args, FLAGS_WIN64_DEBUG);
    args_add(&args, cat2("-I", slashes_forward(tc->root)));
    args_add(&args, cat2("-I", slashes_forward(cat2(tc->root, "\\src"))));   // forward slashes: clean __FILE__ paths
    args_add(&args, root_path);
    args_add(&args, "-o");
    args_add(&args, d->out_file);
    if (!d->release) args_add(&args, cat3("-Wl,/pdb:", d->out_file, ".pdb"));   // game.exe.pdb / game.dll.pdb: no collision
    b32 exports = d->kind == BIN_RUNTIME_EXE && !d->release;
    char* implib_path = cat2(path_dir(d->out_file), "\\game.lib");
    if (d->kind == BIN_GAME_DLL) { args_add_all(&args, LINK_WIN64_DLL); args_add(&args, implib_path); }
    else if (exports) args_add_all(&args, LINK_WIN64_EXE_EXPORTS);
    else args_add_all(&args, LINK_WIN64_EXE);
    args_add(&args, cat2("-L", tc->sdk_lib));
    args_add_all(&args, LIBS_WIN64);
    check_banned(&args);

    // 3. Input hash: clang version, command line (minus the clang path), generated root, inputs.
    u64 h = hash_cstr(HASH_SEED, tc->clang_version);
    for (i32 i = 1; i < args.count; i++) h = hash_cstr(h, args.items[i]);
    h = hash_cstr(h, root.buf);
    for (i32 i = 0; i < inputs.count; i++) {
        i64 len = 0;
        u8* data = read_file(inputs.items[i], &len);
        if (!data) fail_json("read_failed", inputs.items[i]);
        h = hash_cstr(h, inputs.items[i]);
        h = hash_bytes(h, &len, sizeof len);
        h = hash_bytes(h, data, len);
    }
    sbuf stamp = sb_make(32);
    sb_put_hex64(&stamp, h);
    char* stamp_path = cat2(d->out_file, ".stamp");
    i64 old_len = 0;
    char* old_stamp = (char*)read_file(stamp_path, &old_len);
    if (old_stamp && cstr_eq(old_stamp, stamp.buf) && file_exists(d->out_file) && (!exports || file_exists(implib_path))) return BUILD_UP_TO_DATE;

    // 4. Compile and link.
    make_dirs(d->gen_dir);
    make_dirs(path_dir(d->out_file));
    if (!write_file(root_path, root.buf, root.len)) fail_json("write_failed", root_path);
    DeleteFileW(wide(stamp_path));
    if (exports) {
        // Compile (the one-step flags without the link inputs, plus -c), list the exports, link.
        char* obj_path = cat3(d->gen_dir, "\\", cat2(d->name, ".obj"));
        char* def_path = cat3(d->gen_dir, "\\", cat2(d->name, ".def"));
        const char* compile_items[256];
        arg_list compile = { compile_items, 0 };
        args_add(&compile, tc->clang);
        args_add_all(&compile, FLAGS_COMMON);
        args_add_all(&compile, FLAGS_WIN64);
        args_add_all(&compile, FLAGS_WIN64_DEBUG);
        args_add(&compile, "-Wno-unused-command-line-argument");   // -fuse-ld with -c
        args_add(&compile, cat2("-I", slashes_forward(tc->root)));
        args_add(&compile, cat2("-I", slashes_forward(cat2(tc->root, "\\src"))));
        args_add(&compile, "-c");
        args_add(&compile, root_path);
        args_add(&compile, "-o");
        args_add(&compile, obj_path);
        check_banned(&compile);
        if (run_process(compile.items, compile.count, 0, 0) != 0) return BUILD_FAILED;
        if (write_exports_def(obj_path, def_path) <= 0) fail_json("exports_failed", obj_path);
        const char* link_items[64];
        arg_list link = { link_items, 0 };
        args_add(&link, tc->clang);
        args_add_all(&link, FLAGS_WIN64);
        args_add_all(&link, FLAGS_WIN64_DEBUG);
        args_add(&link, "-nostdlib");
        args_add(&link, "-Wno-unused-command-line-argument");   // -D and -O at link time
        args_add(&link, obj_path);
        args_add(&link, "-o");
        args_add(&link, d->out_file);
        args_add(&link, cat3("-Wl,/pdb:", d->out_file, ".pdb"));
        args_add_all(&link, LINK_WIN64_EXE_EXPORTS);
        args_add(&link, cat2("-Wl,/def:", def_path));
        args_add(&link, cat2("-Wl,/implib:", implib_path));
        args_add(&link, cat2("-L", tc->sdk_lib));
        args_add_all(&link, LIBS_WIN64);
        check_banned(&link);
        if (run_process(link.items, link.count, 0, 0) != 0) return BUILD_FAILED;
    } else {
        i32 code = run_process(args.items, args.count, 0, 0);
        if (code != 0) return BUILD_FAILED;
    }
    if (!write_file(stamp_path, stamp.buf, stamp.len)) fail_json("write_failed", stamp_path);
    return BUILD_BUILT;
}

static const char* status_name(build_status s) {
    return s == BUILD_BUILT ? "built" : s == BUILD_UP_TO_DATE ? "up_to_date" : "failed";
}

// ================================================================ host check: vtable slots (1A-14, amendment A-01)
// tests/test_vtable_slots.c compares the COM vtables, struct sizes and GUIDs of win32_api.h with
// the Windows SDK headers. Its SDK half needs the MSVC C headers (vcruntime.h, excpt.h, ...) of a
// Visual Studio install: the newest VC\Tools\MSVC\<ver>, or the BMF_MSVC env var. Headers only:
// nothing links the CRT. Without them the check is skipped (and says so); the runtime and the
// unit tests never need Visual Studio.

// Appends the names of the subfolders of `dir` to `out` (at most `cap`); returns the count.
static i32 list_subdirs(const char* dir, char** out, i32 cap) {
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(wide(cat2(dir, "\\*")), &fd);
    if (find == WIN32_INVALID_HANDLE_VALUE) return 0;
    i32 count = 0;
    do {
        char* name = narrow(fd.cFileName);
        if (!(fd.dwFileAttributes & WIN32_FILE_ATTRIBUTE_DIRECTORY) || name[0] == '.') continue;
        if (count < cap) out[count++] = name;
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    return count;
}

// <ProgramFiles or ProgramFiles(x86)>\Microsoft Visual Studio\<year>\<edition>\VC\Tools\MSVC\<ver>,
// the newest <ver> that has include\vcruntime.h. 0 when there is none.
static char* find_msvc_tools(void) {
    char* msvc = env_var("BMF_MSVC");
    if (msvc) return file_exists(cat2(msvc, "\\include\\vcruntime.h")) ? msvc : 0;
    const char* program_files_vars[2] = { "ProgramFiles", "ProgramFiles(x86)" };
    char* best = 0;
    u64 best_key = 0;
    for (i32 r = 0; r < 2; r++) {
        char* program_files = env_var(program_files_vars[r]);
        if (!program_files) continue;
        char* vs_root = cat2(program_files, "\\Microsoft Visual Studio");
        char* years[16];
        i32 year_count = list_subdirs(vs_root, years, 16);
        for (i32 y = 0; y < year_count; y++) {
            char* editions[16];
            char* year_dir = cat3(vs_root, "\\", years[y]);
            i32 edition_count = list_subdirs(year_dir, editions, 16);
            for (i32 e = 0; e < edition_count; e++) {
                char* tools_dir = cat3(year_dir, "\\", cat2(editions[e], "\\VC\\Tools\\MSVC"));
                char* versions[16];
                i32 version_count = list_subdirs(tools_dir, versions, 16);
                for (i32 v = 0; v < version_count; v++) {
                    char* candidate = cat3(tools_dir, "\\", versions[v]);
                    u64 key = version_key(versions[v]);
                    if (key > best_key && file_exists(cat2(candidate, "\\include\\vcruntime.h"))) { best_key = key; best = candidate; }
                }
            }
        }
    }
    return best;
}

static void host_check_line(const char* check, const char* status_json) {
    sbuf b = sb_make(2048);
    sb_put(&b, "{\"host_check\":"); sb_put_json(&b, check); sb_put(&b, ","); sb_put(&b, status_json); sb_put(&b, "}");
    out_line(b.buf);
}

// ================================================================ host check: public API (1A-24, 1A-43)
// src/bmf.h is the whole public API (SPEC §16). Three checks:
// 1. Contract comments (also run by `build api`): every function declared or defined at file scope in bmf.h has a comment
//    line directly above it or above its group (declarations and comments with no blank line in
//    between). A function name is the first `bmf_<identifier>(` on a line that starts at column 0
//    at brace depth 0 (not a comment, preprocessor, typedef or BMF_STATIC_ASSERT line).
// 2. Self-contained: bmf.h is copied alone to out/gen/api/ and every games/<name>/game.c is
//    compiled (-fsyntax-only, debug flags) with only that folder on the include path, so each game
//    compiles including only bmf.h and no internal header is reachable.
// 3. API.md is current: equal to what `build api` generates from bmf.h now (line ends ignored).

static b32 is_ident_char(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

// Returns the number of public functions found; writes one failure line per function without a
// contract comment (`line_prefix` starts the JSON object, e.g. {"host_check":"api",) and counts it
// in *failures.
static i32 api_check_contracts(const char* line_prefix, const char* path, const char* text, i64 len, i32* failures) {
    enum { MAX_LINES = 16384 };
    static i32 line_start[MAX_LINES];
    static i32 line_depth[MAX_LINES];   // brace depth at the start of the line
    static b32 line_comment[MAX_LINES]; // the line is a // comment (after indentation)
    static b32 line_blank[MAX_LINES];
    i32 line_count = 0;
    i32 depth = 0;
    for (i64 i = 0; i < len && line_count < MAX_LINES;) {
        i64 end = i;
        while (end < len && text[end] != '\n') end++;
        i64 first = i;
        while (first < end && (text[first] == ' ' || text[first] == '\t' || text[first] == '\r')) first++;
        line_start[line_count] = (i32)i;
        line_depth[line_count] = depth;
        line_blank[line_count] = first == end;
        line_comment[line_count] = first + 1 < end && text[first] == '/' && text[first + 1] == '/';
        // Braces in code, not in comments or literals.
        b32 in_string = 0, in_char = 0;
        for (i64 k = i; k < end; k++) {
            char c = text[k];
            if (in_string) { if (c == '\\') k++; else if (c == '"') in_string = 0; continue; }
            if (in_char) { if (c == '\\') k++; else if (c == '\'') in_char = 0; continue; }
            if (c == '/' && k + 1 < end && text[k + 1] == '/') break;
            if (c == '"') in_string = 1;
            else if (c == '\'') in_char = 1;
            else if (c == '{') depth++;
            else if (c == '}') depth--;
        }
        line_count++;
        i = end + 1;
    }
    i32 functions = 0;
    for (i32 l = 0; l < line_count; l++) {
        const char* line = text + line_start[l];
        if (line_depth[l] != 0 || line_blank[l] || line_comment[l]) continue;
        if (line[0] == ' ' || line[0] == '\t' || line[0] == '#' || line[0] == '}') continue;
        if (starts_with(line, "typedef") || starts_with(line, "BMF_STATIC_ASSERT")) continue;
        // The first bmf_ identifier directly followed by '(' before the end of the line or a comment.
        char name[128];
        name[0] = 0;
        for (const char* c = line; *c && *c != '\n' && !(c[0] == '/' && c[1] == '/'); c++) {
            if (!starts_with(c, "bmf_") || (c > line && is_ident_char(c[-1]))) continue;
            const char* e = c;
            while (is_ident_char(*e)) e++;
            if (*e != '(') { c = e - 1; continue; }
            i32 n = (i32)(e - c) < 127 ? (i32)(e - c) : 127;
            for (i32 k = 0; k < n; k++) name[k] = c[k];
            name[n] = 0;
            break;
        }
        if (!name[0]) continue;
        functions++;
        b32 documented = 0;
        for (i32 up = l - 1; up >= 0 && !line_blank[up]; up--) {
            if (line_comment[up] && line_depth[up] == 0) { documented = 1; break; }
        }
        if (!documented) {
            sbuf b = sb_make(512);
            sb_put(&b, line_prefix);
            sb_put(&b, "\"ok\":0,\"error\":\"no_contract_comment\",\"file\":"); sb_put_json(&b, slashes_forward(path));
            sb_put(&b, ",\"line\":"); sb_put_i64(&b, l + 1);
            sb_put(&b, ",\"function\":"); sb_put_json(&b, name);
            sb_put(&b, "}");
            out_line(b.buf);
            (*failures)++;
        }
    }
    return functions;
}

// ================================================================ build api (1A-43)
// API.md, the API reference agents read instead of the sources (SPEC §17), is generated from
// src/bmf.h by a line parser that relies on the header's layout (SPEC §16 and bmf.h's top comment):
//   - the comment block before `#ifndef BMF_H` becomes the introduction;
//   - a section is a `// ====` banner, a `// Title` line and another banner: `## Title`;
//   - a subsection is a `// ------ name` line: `### name`;
//   - a comment block right after a heading that ends at a blank line, a heading or the end is
//     prose (the "// " stripped); everything else (contract comments, function declarations,
//     types, constants, macros) is kept in ```c blocks, with these exceptions: a static inline
//     function keeps only its signature (`... ;`, the body is dropped); preprocessor conditional
//     blocks (#if/#ifdef/#ifndef ... #endif) and BMF_STATIC_ASSERT lines are left out (the
//     comments in bmf.h say what they define); a declaration group whose comment starts with
//     "// Engine side" (functions behind static inline wrappers, games call the wrappers) is left
//     out up to the next blank line; the include guard, the #include and the final #endif are
//     left out; runs of spaces inside a line (column alignment) become one space; parenthesized
//     references to the spec, decisions and tasks ("(SPEC §5.4, D-37 rule 3)", "(1A-04b)") are
//     dropped (API.md readers do not have SPEC.md).
// The top line gives the API version, the function count and a token estimate: bytes / 2.07,
// rounded up. The ratio is calibrated on this file: 66 461 bytes in 1 402 lines read as 32 133
// tokens by a Claude file reader (line numbers included), 2026-11-22; C declarations and comments
// tokenize far denser than prose. SPEC §17 targets under 30 000 tokens; above that `build api`
// and the `api` host check fail.
enum { API_TOKEN_TARGET = 30000 };

static void sb_put_n(sbuf* b, const char* s, i32 n) {
    if (b->len + n + 1 > b->cap) bmf_panic("build: string buffer overflow");
    bmf_memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = 0;
}

// Brace depth change of one line of C (braces in // comments, strings and chars do not count).
static i32 api_brace_delta(const char* s, i32 n) {
    i32 delta = 0;
    b32 in_string = 0, in_char = 0;
    for (i32 k = 0; k < n; k++) {
        char c = s[k];
        if (in_string) { if (c == '\\') k++; else if (c == '"') in_string = 0; continue; }
        if (in_char) { if (c == '\\') k++; else if (c == '\'') in_char = 0; continue; }
        if (c == '/' && k + 1 < n && s[k + 1] == '/') break;
        if (c == '"') in_string = 1;
        else if (c == '\'') in_char = 1;
        else if (c == '{') delta++;
        else if (c == '}') delta--;
    }
    return delta;
}

static b32 api_is_comment(const char* s, i32 n) { return n >= 2 && s[0] == '/' && s[1] == '/'; }
static b32 api_is_banner(const char* s, i32 n) { return n >= 8 && starts_with(s, "// ====="); }
static b32 api_is_subheading(const char* s, i32 n) { return n >= 8 && starts_with(s, "// -----"); }

// Length of a parenthesized pure reference list starting at s[0] == '(' (" (SPEC §5.4, D-37
// rule 3)", "(1A-04b)", "(R7)"): the whole "(...)" when it holds only reference words, numbers
// and separators and at least one reference word, else 0.
static i32 api_reference_length(const char* s, i32 n) {
    static const char* const words[] = { "SPEC \xC2\xA7", "D-", "task ", "1A-", "1B-", "A-", "rules ", "rule ", "point ", "and ", "R", 0 };
    i32 k = 1;
    b32 any_word = 0;
    while (k < n && s[k] != ')') {
        b32 matched = 0;
        for (i32 w = 0; words[w] && !matched; w++) {
            if (!starts_with(s + k, words[w])) continue;
            if (words[w][0] == 'R' && !(k + 1 < n && s[k + 1] >= '0' && s[k + 1] <= '9')) continue;
            k += cstr_len(words[w]);
            matched = any_word = 1;
        }
        if (matched) continue;
        char c = s[k];
        if ((c >= '0' && c <= '9') || c == '.' || c == ',' || c == ';' || c == ' ') { k++; continue; }
        if (c >= 'a' && c <= 'c' && k > 1 && s[k - 1] >= '0' && s[k - 1] <= '9') { k++; continue; }   // 1A-04b
        return 0;
    }
    return k < n && any_word ? k + 1 : 0;
}

// Appends s[0..n) to out without parenthesized reference lists (and the space before them).
static void api_put_without_references(sbuf* out, const char* s, i32 n) {
    for (i32 k = 0; k < n; k++) {
        if (s[k] == ' ' && k + 1 < n && s[k + 1] == '(') {
            i32 skip = api_reference_length(s + k + 1, n - k - 1);
            if (skip) { k += skip; continue; }
        }
        if (s[k] == '(' && k == 0) {
            i32 skip = api_reference_length(s, n);
            if (skip) { k += skip - 1; continue; }
        }
        sb_put_char(out, s[k]);
    }
}

// Writes the text after "//" (and one space) of a comment line as a prose line.
static void api_put_prose(sbuf* out, const char* s, i32 n) {
    i32 at = 2;
    if (at < n && s[at] == ' ') at++;
    api_put_without_references(out, s + at, n - at);
    sb_put_char(out, '\n');
}

// Generates API.md from the text of bmf.h into *out. `functions` is the count from
// api_check_contracts. Returns the token estimate.
static i32 api_generate(const char* text, i64 len, i32 functions, sbuf* out) {
    enum { MAX_LINES = 16384 };
    static i32 starts[MAX_LINES], lengths[MAX_LINES];
    i32 line_count = 0;
    for (i64 i = 0; i < len && line_count < MAX_LINES;) {
        i64 end = i;
        while (end < len && text[end] != '\n') end++;
        i64 trimmed = end;
        while (trimmed > i && (text[trimmed - 1] == '\r' || text[trimmed - 1] == ' ' || text[trimmed - 1] == '\t')) trimmed--;
        starts[line_count] = (i32)i;
        lengths[line_count] = (i32)(trimmed - i);
        line_count++;
        i = end + 1;
    }
    sbuf intro = sb_make(16384);
    sbuf body = sb_make((i32)len + 65536);
    sbuf contents = sb_make(8192);
    char version[32] = "?";
    i32 l = 0;
    // The introduction: the file comment before the include guard.
    for (; l < line_count; l++) {
        const char* s = text + starts[l];
        i32 n = lengths[l];
        if (starts_with(s, "#ifndef BMF_H")) { l++; break; }
        if (api_is_comment(s, n)) api_put_prose(&intro, s, n);
        else if (n == 0) sb_put_char(&intro, '\n');
    }
    b32 in_code = 0, pending_blank = 0, after_heading = 0, skipping_body = 0, inline_pending = 0;
    i32 depth = 0, conditional_depth = 0;
    for (; l < line_count; l++) {
        const char* s = text + starts[l];
        i32 n = lengths[l];
        if (skipping_body) {
            depth += api_brace_delta(s, n);
            if (depth <= 0) { depth = 0; skipping_body = 0; }
            continue;
        }
        if (depth == 0 && (starts_with(s, "#define BMF_H") || starts_with(s, "#include") || starts_with(s, "#endif // BMF_H"))) continue;
        // Preprocessor conditional blocks and static asserts are left out (see above).
        if (depth == 0 && (starts_with(s, "#if") || conditional_depth > 0)) {
            if (starts_with(s, "#if")) conditional_depth++;
            else if (starts_with(s, "#endif")) conditional_depth--;
            continue;
        }
        if (depth == 0 && starts_with(s, "BMF_STATIC_ASSERT(")) continue;
        if (depth == 0 && starts_with(s, "// Engine side")) {
            while (l + 1 < line_count && lengths[l + 1] != 0) l++;   // the group, up to the blank line
            continue;
        }
        if (depth == 0 && starts_with(s, "#define BMF_API_VERSION ")) {
            i32 k = 0;
            for (const char* v = s + 24; *v >= '0' && *v <= '9' && k < 30; v++) version[k++] = *v;
            if (k) version[k] = 0;
        }
        // Headings.
        b32 section = depth == 0 && api_is_banner(s, n) && l + 2 < line_count && api_is_comment(text + starts[l + 1], lengths[l + 1]) &&
                      api_is_banner(text + starts[l + 2], lengths[l + 2]);
        b32 subsection = depth == 0 && api_is_subheading(s, n);
        if (section || subsection) {
            if (in_code) { sb_put(&body, "```\n"); in_code = 0; }
            pending_blank = 0;
            while (body.len >= 2 && body.buf[body.len - 1] == '\n' && body.buf[body.len - 2] == '\n') body.buf[--body.len] = 0;   // one blank line before a heading
            if (section) {
                const char* title = text + starts[l + 1] + 2;
                i32 title_length = lengths[l + 1] - 2;
                while (title_length > 0 && *title == ' ') { title++; title_length--; }
                sb_put(&body, "\n## "); api_put_without_references(&body, title, title_length); sb_put(&body, "\n\n");
                i32 short_length = 0;   // the contents list leaves out a trailing "(SPEC ...)" / "(D-..)"
                while (short_length < title_length && !(title[short_length] == ' ' && short_length + 1 < title_length && title[short_length + 1] == '(')) short_length++;
                sb_put(&contents, "- "); sb_put_n(&contents, title, short_length); sb_put_char(&contents, '\n');
                l += 2;
            } else {
                i32 at = 3;
                while (at < n && (s[at] == '-' || s[at] == ' ')) at++;
                sb_put(&body, "\n### "); api_put_without_references(&body, s + at, n - at); sb_put(&body, "\n\n");
            }
            after_heading = 1;
            continue;
        }
        if (n == 0) {
            if (in_code) pending_blank = 1;
            continue;
        }
        // Prose: a comment block right after a heading that ends at a blank line, a heading or the end.
        if (after_heading && depth == 0 && api_is_comment(s, n)) {
            i32 end = l;
            while (end < line_count && api_is_comment(text + starts[end], lengths[end]) &&
                   !api_is_banner(text + starts[end], lengths[end]) && !api_is_subheading(text + starts[end], lengths[end])) end++;
            b32 prose = end >= line_count || lengths[end] == 0 || api_is_banner(text + starts[end], lengths[end]) ||
                        api_is_subheading(text + starts[end], lengths[end]);
            if (prose) {
                for (i32 k = l; k < end; k++) api_put_prose(&body, text + starts[k], lengths[k]);
                sb_put_char(&body, '\n');
                l = end - 1;
                after_heading = 0;
                continue;
            }
        }
        after_heading = 0;
        // Code.
        if (!in_code) { sb_put(&body, "```c\n"); in_code = 1; pending_blank = 0; }
        if (pending_blank) { sb_put_char(&body, '\n'); pending_blank = 0; }
        i32 delta = api_brace_delta(s, n);
        if (depth == 0 && (starts_with(s, "static inline") || inline_pending)) {
            // A static inline function: the signature only.
            i32 brace = -1;
            for (i32 k = 0; k < n && brace < 0; k++) if (s[k] == '{') brace = k;
            if (brace < 0) {
                if (s[n - 1] == ';') continue;   // a forward declaration: the definition's signature follows
                sb_put_n(&body, s, n); sb_put_char(&body, '\n');
                inline_pending = 1;
                continue;
            }
            inline_pending = 0;
            i32 cut = brace;
            while (cut > 0 && s[cut - 1] == ' ') cut--;
            sb_put_n(&body, s, cut); sb_put(&body, ";\n");
            if (delta > 0) { depth = delta; skipping_body = 1; }
            continue;
        }
        // The line, with runs of spaces after its indentation collapsed to one and references dropped.
        static char collapsed[4096];
        i32 k = 0, used = 0;
        while (k < n && s[k] == ' ' && used < 4095) collapsed[used++] = s[k++];
        for (; k < n && used < 4095; k++) {
            if (s[k] == ' ' && k + 1 < n && s[k + 1] == ' ') continue;
            collapsed[used++] = s[k];
        }
        api_put_without_references(&body, collapsed, used);
        sb_put_char(&body, '\n');
        depth += delta;
        if (depth < 0) depth = 0;
    }
    if (in_code) sb_put(&body, "```\n");

    // The head: title, the numbers, contents. The estimate covers the whole file.
    sbuf head = sb_make(16384);
    sb_put(&head, "# bmf API\n\nGenerated by `build api` from `src/bmf.h`; do not edit (regenerate after changing bmf.h).\n");
    sb_put(&head, "API version "); sb_put(&head, version);
    sb_put(&head, ", "); sb_put_i64(&head, functions); sb_put(&head, " functions, about ");
    i64 total = head.len + 64 + intro.len + contents.len + 32 + body.len;
    i32 tokens = (i32)((total * 100 + 206) / 207);   // bytes / 2.07, rounded up
    sb_put_i64(&head, tokens); sb_put(&head, " tokens (estimate: bytes / 2.07).\n\n");
    while (intro.len > 0 && intro.buf[intro.len - 1] == '\n') intro.buf[--intro.len] = 0;
    sb_put(&head, intro.buf);
    sb_put(&head, "\n\n## Contents\n\n"); sb_put(&head, contents.buf); sb_put_char(&head, '\n');
    *out = sb_make(head.len + body.len + 16);
    sb_put(out, head.buf);
    sb_put(out, body.buf);
    return tokens;
}

// Generates API.md from bmf.h into *out after the contract check. Returns the number of failures
// (one JSON line each, starting with `line_prefix`): functions without a contract comment, and
// an estimate over API_TOKEN_TARGET.
static i32 api_build(const toolchain* tc, const char* line_prefix, sbuf* out, i32* out_functions, i32* out_tokens) {
    char* header = cat2(tc->root, "\\src\\bmf.h");
    i64 len = 0;
    char* text = (char*)read_file(header, &len);
    if (!text) fail_json("read_failed", header);
    i32 failures = 0;
    i32 functions = api_check_contracts(line_prefix, header, text, len, &failures);
    i32 tokens = api_generate(text, len, functions, out);
    if (tokens > API_TOKEN_TARGET) {
        sbuf b = sb_make(512);
        sb_put(&b, line_prefix); sb_put(&b, "\"ok\":0,\"error\":\"api_over_token_target\",\"tokens\":"); sb_put_i64(&b, tokens);
        sb_put(&b, ",\"target\":"); sb_put_i64(&b, API_TOKEN_TARGET); sb_put(&b, "}");
        out_line(b.buf);
        failures++;
    }
    *out_functions = functions;
    *out_tokens = tokens;
    return failures;
}

// `build api`: writes API.md at the framework root unless a check fails.
static i32 command_api(const toolchain* tc) {
    sbuf md;
    i32 functions = 0, tokens = 0;
    const char* prefix = "{\"build\":\"fail\",\"target\":\"api\",";
    i32 failures = api_build(tc, prefix, &md, &functions, &tokens);
    char* path = cat2(tc->root, "\\API.md");
    if (!failures && !write_file(path, md.buf, md.len)) fail_json("write_failed", path);
    sbuf b = sb_make(1024);
    sb_put(&b, "{\"build\":"); sb_put(&b, failures ? "\"fail\"" : "\"ok\"");
    sb_put(&b, ",\"target\":\"api\",\"functions\":"); sb_put_i64(&b, functions);
    sb_put(&b, ",\"bytes\":"); sb_put_i64(&b, md.len);
    sb_put(&b, ",\"tokens\":"); sb_put_i64(&b, tokens);
    if (failures) { sb_put(&b, ",\"failures\":"); sb_put_i64(&b, failures); }
    else { sb_put(&b, ",\"out\":"); sb_put_json(&b, slashes_forward(path)); }
    sb_put(&b, ",\"ms\":"); sb_put_i64(&b, elapsed_ms()); sb_put(&b, "}");
    out_line(b.buf);
    return failures ? 1 : 0;
}

// Returns 0 when all checks pass, 1 otherwise (each problem is one JSON line or clang diagnostic).
static i32 host_check_api(const toolchain* tc) {
    char* header = cat2(tc->root, "\\src\\bmf.h");
    i64 len = 0;
    char* text = (char*)read_file(header, &len);
    if (!text) fail_json("read_failed", header);
    sbuf md;
    i32 functions = 0, tokens = 0;
    i32 failures = api_build(tc, "{\"host_check\":\"api\",", &md, &functions, &tokens);
    // API.md must be what build api writes now (compared without '\r').
    i64 md_len = 0;
    char* current = (char*)read_file(cat2(tc->root, "\\API.md"), &md_len);
    b32 same = current != 0;
    i64 a = 0, b_at = 0;
    while (same && (a < md_len || b_at < md.len)) {
        if (a < md_len && current[a] == '\r') { a++; continue; }
        if (a >= md_len || b_at >= md.len || current[a] != md.buf[b_at]) same = 0;
        a++;
        b_at++;
    }
    if (!same) {
        host_check_line("api", current ? "\"ok\":0,\"error\":\"api_md_stale\",\"detail\":\"API.md differs from what src/bmf.h generates: run build api\""
                                       : "\"ok\":0,\"error\":\"api_md_missing\",\"detail\":\"no API.md: run build api\"");
        failures++;
    }

    char* api_dir = cat2(tc->root, "\\out\\gen\\api");
    make_dirs(api_dir);
    if (!write_file(cat2(api_dir, "\\bmf.h"), text, len)) fail_json("write_failed", cat2(api_dir, "\\bmf.h"));
    char* games_dir = cat2(tc->root, "\\games");
    char* games[64];
    i32 game_count = list_subdirs(games_dir, games, 64);
    sbuf compiled = sb_make(1024);
    i32 compiled_count = 0;
    for (i32 g = 0; g < game_count; g++) {
        char* game_c = cat3(games_dir, "\\", cat2(games[g], "\\game.c"));
        if (!file_exists(game_c)) continue;
        const char* items[64];
        arg_list args = { items, 0 };
        args_add(&args, tc->clang);
        args_add_all(&args, FLAGS_COMMON);
        args_add_all(&args, FLAGS_WIN64);
        args_add_all(&args, FLAGS_WIN64_DEBUG);
        args_add(&args, "-fsyntax-only");
        args_add(&args, cat2("-I", slashes_forward(api_dir)));
        args_add(&args, slashes_forward(game_c));
        check_banned(&args);
        if (run_process(args.items, args.count, 0, 0) != 0) {
            sbuf b = sb_make(512);
            sb_put(&b, "\"ok\":0,\"error\":\"game_needs_more_than_bmf_h\",\"game\":"); sb_put_json(&b, games[g]);
            host_check_line("api", b.buf);
            failures++;
        }
        sb_put(&compiled, compiled_count ? "," : ""); sb_put_json(&compiled, games[g]);
        compiled_count++;
    }
    sbuf b = sb_make(1024);
    sb_put(&b, "\"ok\":"); sb_put(&b, failures ? "0" : "1");
    sb_put(&b, ",\"functions\":"); sb_put_i64(&b, functions);
    sb_put(&b, ",\"api_md_tokens\":"); sb_put_i64(&b, tokens);
    sb_put(&b, ",\"games\":["); sb_put(&b, compiled.buf); sb_put(&b, "]");
    host_check_line("api", b.buf);
    return failures ? 1 : 0;
}


// Builds (when its inputs changed) and runs out/test/win64-debug/vtable_slots.exe. Returns 0 when
// it passed or was skipped, 1 on a mismatch (the exe prints each one), 2 when it does not compile.
static i32 host_check_vtable_slots(const toolchain* tc) {
    char* msvc = find_msvc_tools();
    char* sdk_version_dir = path_dir(path_dir(tc->sdk_lib));            // ...\Lib\<ver>
    const char* sdk_version = path_name(sdk_version_dir);
    char* sdk_root = path_dir(path_dir(sdk_version_dir));                // ...\Windows Kits\10
    if (!msvc || !file_exists(cat3(sdk_root, "\\Include\\", cat2(sdk_version, "\\um\\d3d11.h")))) {
        host_check_line("vtable_slots", "\"ok\":1,\"skipped\":\"no MSVC C headers or SDK headers (install Visual Studio C++ tools or set BMF_MSVC)\"");
        return 0;
    }
    char* source = cat2(tc->root, "\\tests\\test_vtable_slots.c");
    char* out_dir = cat2(tc->root, "\\out\\test\\win64-debug");
    char* object = cat2(out_dir, "\\vtable_slots_sdk.obj");
    char* exe = cat2(out_dir, "\\vtable_slots.exe");

    const char* sdk_items[32];
    arg_list sdk_args = { sdk_items, 0 };
    args_add(&sdk_args, tc->clang);
    args_add_all(&sdk_args, FLAGS_WIN64);
    args_add(&sdk_args, "-std=c11");
    args_add(&sdk_args, "-ffreestanding");
    args_add(&sdk_args, "-fno-color-diagnostics");
    args_add(&sdk_args, "-fdiagnostics-absolute-paths");
    args_add(&sdk_args, "-DVTABLE_SIDE_SDK=1");
    args_add(&sdk_args, "-Xmicrosoft-visualc-tools-root"); args_add(&sdk_args, msvc);
    args_add(&sdk_args, "-Xmicrosoft-windows-sdk-root"); args_add(&sdk_args, sdk_root);
    args_add(&sdk_args, "-Xmicrosoft-windows-sdk-version"); args_add(&sdk_args, sdk_version);
    args_add(&sdk_args, "-c");
    args_add(&sdk_args, source);
    args_add(&sdk_args, "-o");
    args_add(&sdk_args, object);

    const char* bmf_items[64];
    arg_list bmf_args = { bmf_items, 0 };
    args_add(&bmf_args, tc->clang);
    args_add_all(&bmf_args, FLAGS_COMMON);
    args_add_all(&bmf_args, FLAGS_WIN64);
    args_add(&bmf_args, "-O0");
    args_add(&bmf_args, "-DBMF_DEBUG=0");
    args_add(&bmf_args, source);
    args_add(&bmf_args, object);
    args_add(&bmf_args, "-o");
    args_add(&bmf_args, exe);
    args_add(&bmf_args, "-Wl,/entry:vtable_check_entry");
    args_add(&bmf_args, "-Wl,/subsystem:console");
    args_add(&bmf_args, "-Wl,/noimplib");
    args_add(&bmf_args, cat2("-L", tc->sdk_lib));
    args_add(&bmf_args, "-lkernel32");
    check_banned(&sdk_args);
    check_banned(&bmf_args);

    // Rebuild only when an input changed: clang version, both command lines, the source files.
    u64 h = hash_cstr(HASH_SEED, tc->clang_version);
    for (i32 i = 1; i < sdk_args.count; i++) h = hash_cstr(h, sdk_args.items[i]);
    for (i32 i = 1; i < bmf_args.count; i++) h = hash_cstr(h, bmf_args.items[i]);
    const char* inputs[4] = { "\\tests\\test_vtable_slots.c", "\\src\\platform\\win32\\win32_api.h", "\\src\\bmf.h",
                              "\\src\\base\\base_builtins.c" };
    for (i32 i = 0; i < 4; i++) {
        i64 len = 0;
        u8* data = read_file(cat2(tc->root, inputs[i]), &len);
        if (!data) fail_json("read_failed", cat2(tc->root, inputs[i]));
        h = hash_bytes(h, &len, sizeof len);
        h = hash_bytes(h, data, len);
    }
    sbuf stamp = sb_make(32);
    sb_put_hex64(&stamp, h);
    char* stamp_path = cat2(exe, ".stamp");
    i64 old_len = 0;
    char* old_stamp = (char*)read_file(stamp_path, &old_len);
    if (!old_stamp || !cstr_eq(old_stamp, stamp.buf) || !file_exists(exe)) {
        make_dirs(out_dir);
        DeleteFileW(wide(stamp_path));
        if (run_process(sdk_args.items, sdk_args.count, 0, 0) != 0) return 2;
        if (run_process(bmf_args.items, bmf_args.count, 0, 0) != 0) return 2;
        if (!write_file(stamp_path, stamp.buf, stamp.len)) fail_json("write_failed", stamp_path);
    }
    const char* run_args[1] = { exe };
    i32 code = run_process(run_args, 1, 0, 0);   // prints its own JSON lines
    return code == 0 ? 0 : 1;
}

// ================================================================ shaders (1A-15, D-14)
// Every shaders/d3d11/<name>.hlsl (top level; subfolders and other files are #include material)
// has entry points vs_main and ps_main. Each is compiled twice with D3DCompile from System32's
// d3dcompiler_47.dll: debug (DEBUG | SKIP_OPTIMIZATION) and release (OPTIMIZATION_LEVEL3), both
// with ENABLE_STRICTNESS | WARNINGS_ARE_ERRORS, targets vs_5_0/ps_5_0. The output,
// out/gen/shaders_d3d11.h, holds the bmf_shader_id enum (ids in name order), both bytecode sets
// (the binary picks one with BMF_DEBUG) and the gpu_d3d11_shader_codes[] table, including the
// b/t/s register slots each shader declares (read from the bytecode's RDEF chunk, VS and PS
// together). The debug and release slot sets must be equal, so a bind set check cannot depend on
// the configuration. It is regenerated when the hash of the compiler file's size and time and
// of every file under shaders/d3d11 changes; `build win64` and `build test` run this first.
// Compiler messages are reprinted as `file:line:col: error: <code>: <text>`.

static const char* const SHADER_GENERATOR_VERSION = "shaders_d3d11 v1";

// Limits from src/gpu/gpu.h (build.c does not include it): BMF_GPU_MAX_SHADERS and the
// BMF_GPU_MAX_BIND_* slot counts. The generated header re-checks them with static asserts.
enum { SHADER_MAX_COUNT = 64, SHADER_MAX_UNIFORMS = 4, SHADER_MAX_TEXTURES = 8, SHADER_MAX_SAMPLERS = 4 };

typedef struct {
    u32 uniform_mask;   // bit i: register b<i>
    u32 texture_mask;   // bit i: register t<i>
    u32 sampler_mask;   // bit i: register s<i>
} shader_slots;

// Prints one diagnostic line `<path>:<line>:<col>: error: <text>` to stdout.
static void shader_error(const char* path, i64 line, i64 column, const char* text) {
    sbuf b = sb_make(cstr_len(path) + cstr_len(text) + 64);
    sb_put(&b, path); sb_put_char(&b, ':'); sb_put_i64(&b, line); sb_put_char(&b, ':'); sb_put_i64(&b, column);
    sb_put(&b, ": error: "); sb_put(&b, text);
    out_line(b.buf);
}

// Reprints D3DCompile messages. Their lines look like `<source name>(<line>,<col>[-<col>]): error
// X3000: <text>` (or `warning X....`, which WARNINGS_ARE_ERRORS makes fatal); lines without a
// location are attached to `fallback_path` line 1. Returns the number of lines printed.
static i32 shader_print_messages(const char* messages, const char* fallback_path) {
    i32 printed = 0;
    const char* line_start = messages;
    while (*line_start) {
        const char* line_end = line_start;
        while (*line_end && *line_end != '\n') line_end++;
        i32 length = (i32)(line_end - line_start);
        while (length > 0 && (line_start[length - 1] == '\r' || line_start[length - 1] == ' ')) length--;
        char* line = (char*)push(length + 1);
        bmf_memcpy(line, line_start, length);
        line[length] = 0;
        line_start = *line_end ? line_end + 1 : line_end;
        if (length == 0) continue;

        // Find "(<digits>,<digits>[-<digits>]): " and split around it.
        i32 open = -1, after = -1;
        i64 line_number = 1, column = 1;
        for (i32 i = 0; i < length && open < 0; i++) {
            if (line[i] != '(') continue;
            i32 j = i + 1;
            i64 l = 0, c = 0;
            i32 digits = 0;
            while (line[j] >= '0' && line[j] <= '9') { l = l * 10 + (line[j] - '0'); j++; digits++; }
            if (!digits || line[j] != ',') continue;
            j++; digits = 0;
            while (line[j] >= '0' && line[j] <= '9') { c = c * 10 + (line[j] - '0'); j++; digits++; }
            if (!digits) continue;
            if (line[j] == '-') { j++; while (line[j] >= '0' && line[j] <= '9') j++; }
            if (line[j] != ')' || line[j + 1] != ':' || line[j + 2] != ' ') continue;
            open = i; after = j + 3; line_number = l; column = c;
        }
        const char* path = fallback_path;
        const char* text = line;
        if (open >= 0) {
            line[open] = 0;
            path = line;
            text = line + after;
            // An #include'd file is named as written; make it a full path next to the shader.
            b32 absolute = (path[0] && path[1] == ':') || path[0] == '\\' || path[0] == '/';
            if (!absolute) {
                char* candidate = cat3(path_dir(fallback_path), "\\", path);
                for (char* p = candidate; *p; p++) if (*p == '/') *p = '\\';
                if (file_exists(candidate)) path = candidate;
            }
        }
        // "error X3000: text" -> "X3000: text"; "warning X3206: text" -> "X3206: text (warning; warnings are errors)".
        b32 warning = 0;
        if (text[0] == 'e' && text[1] == 'r' && text[2] == 'r' && text[3] == 'o' && text[4] == 'r' && text[5] == ' ') text += 6;
        else if (text[0] == 'w' && text[1] == 'a' && text[2] == 'r' && text[3] == 'n' && text[4] == 'i' && text[5] == 'n' && text[6] == 'g' && text[7] == ' ') {
            text += 8;
            warning = 1;
        }
        shader_error(path, line_number, column, warning ? cat2(text, " (warning; warnings are errors)") : text);
        printed++;
    }
    return printed;
}

// Reads the register slots a DXBC shader declares from its RDEF chunk (D3D11 shader reflection
// format: a resource binding is 32 bytes {name offset, D3D_SHADER_INPUT_TYPE, return type,
// dimension, samples, bind point, bind count, flags}). Returns 0, or an error text.
static const char* shader_reflect_slots(const u8* code, i64 size, shader_slots* slots) {
    if (size < 32 || code[0] != 'D' || code[1] != 'X' || code[2] != 'B' || code[3] != 'C') return "bytecode is not a DXBC container";
    u32 chunk_count = read_u32_le(code + 28);
    if ((i64)32 + (i64)chunk_count * 4 > size) return "DXBC chunk table out of range";
    for (u32 c = 0; c < chunk_count; c++) {
        u32 chunk_offset = read_u32_le(code + 32 + c * 4);
        if ((i64)chunk_offset + 8 > size) return "DXBC chunk out of range";
        const u8* chunk = code + chunk_offset;
        if (chunk[0] != 'R' || chunk[1] != 'D' || chunk[2] != 'E' || chunk[3] != 'F') continue;
        u32 chunk_size = read_u32_le(chunk + 4);
        const u8* rdef = chunk + 8;
        if ((i64)chunk_offset + 8 + chunk_size > size || chunk_size < 28) return "RDEF chunk out of range";
        u32 binding_count = read_u32_le(rdef + 8);
        u32 binding_offset = read_u32_le(rdef + 12);
        if ((u64)binding_offset + (u64)binding_count * 32 > chunk_size) return "RDEF bindings out of range";
        for (u32 i = 0; i < binding_count; i++) {
            const u8* binding = rdef + binding_offset + i * 32;
            u32 type = read_u32_le(binding + 4);
            u32 first = read_u32_le(binding + 20);
            u32 count = read_u32_le(binding + 24);
            u32* mask;
            u32 limit;
            if (type == 0) { mask = &slots->uniform_mask; limit = SHADER_MAX_UNIFORMS; }        // D3D_SIT_CBUFFER
            else if (type == 2) { mask = &slots->texture_mask; limit = SHADER_MAX_TEXTURES; }   // D3D_SIT_TEXTURE
            else if (type == 3) { mask = &slots->sampler_mask; limit = SHADER_MAX_SAMPLERS; }   // D3D_SIT_SAMPLER
            else return "declares a resource kind the GPU API has no binding for (only cbuffers, textures and samplers)";
            if (count == 0 || first + count > limit)
                return type == 0 ? "uses a cbuffer register above b3 (BMF_GPU_MAX_BIND_UNIFORMS)"
                     : type == 2 ? "uses a texture register above t7 (BMF_GPU_MAX_BIND_TEXTURES)"
                                 : "uses a sampler register above s3 (BMF_GPU_MAX_BIND_SAMPLERS)";
            for (u32 s = first; s < first + count; s++) *mask |= 1u << s;
        }
        return 0;
    }
    return "bytecode has no RDEF (reflection) chunk";
}

static void sb_put_hex32(sbuf* b, u32 v) {
    sb_put(b, "0x");
    for (i32 i = 0; i < 8; i++) sb_put_char(b, "0123456789abcdef"[(v >> ((7 - i) * 4)) & 15]);
}

static void sb_put_bytes_array(sbuf* b, const char* name, const u8* data, i64 size) {
    sb_put(b, "static const u8 "); sb_put(b, name); sb_put(b, "["); sb_put_i64(b, size); sb_put(b, "] = {");
    for (i64 i = 0; i < size; i++) {
        if (i % 24 == 0) sb_put(b, "\n   ");
        sb_put(b, " 0x");
        sb_put_char(b, "0123456789abcdef"[data[i] >> 4]);
        sb_put_char(b, "0123456789abcdef"[data[i] & 15]);
        sb_put_char(b, ',');
    }
    sb_put(b, "\n};\n");
}

typedef enum { SHADERS_FAILED, SHADERS_BUILT, SHADERS_UP_TO_DATE } shaders_status;

// Two shader sets go through the same generator: the runtime's (ids from 0, enum bmf_shader_id,
// mutable table gpu_d3d11_shader_codes) and the unit tests' tests/shaders/d3d11 (ids from 56,
// GPU_TEST_SHADER_* values, a const table gpu_d3d11_test_shader_codes that tests/test_gpu_d3d11.c
// copies in), which only `build test` generates.
typedef struct {
    const char* source_dir;      // under the root
    const char* out_name;        // under out\gen
    const char* enum_prefix;
    const char* symbol_prefix;   // of the bytecode arrays
    i32 first_id;
    i32 max_count;
} shader_set;
static const shader_set SHADERS_RUNTIME = { "\\shaders\\d3d11", "shaders_d3d11.h", "BMF_SHADER_", "gpu_d3d11_", 0, SHADER_MAX_COUNT };
static const shader_set SHADERS_TESTS = { "\\tests\\shaders\\d3d11", "test_shaders_d3d11.h", "GPU_TEST_SHADER_", "gpu_d3d11_test_", 56, 8 };

// Appends `prefix` and the upper-cased file name without ".hlsl".
static void sb_put_shader_enum(sbuf* b, const char* prefix, const char* file_name) {
    sb_put(b, prefix);
    for (i32 i = 0; i < cstr_len(file_name) - 5; i++) sb_put_char(b, file_name[i] >= 'a' && file_name[i] <= 'z' ? (char)(file_name[i] - 32) : file_name[i]);
}

// Brings out\gen\<set->out_name> up to date. `shader_count` receives the number of shaders.
static shaders_status generate_shaders(const toolchain* tc, const shader_set* set, i32* shader_count) {
    b32 tests = set->first_id != 0;
    char* shader_dir = cat2(tc->root, set->source_dir);
    char* gen_dir = cat2(tc->root, "\\out\\gen");
    char* out_path = cat3(gen_dir, "\\", set->out_name);
    char* stamp_path = cat2(out_path, ".stamp");
    char* system_root = env_var("SystemRoot");
    char* compiler_path = cat2(system_root ? system_root : "C:\\Windows", "\\System32\\d3dcompiler_47.dll");

    // Inputs: every file under shaders/d3d11 (includes too); compiled: the top-level *.hlsl.
    char* all_items[1024];
    path_list all_files = { all_items, 0, 1024 };
    const char* const any_ext[] = { "", 0 };
    list_files(&all_files, shader_dir, any_ext);
    sort_paths(&all_files);
    char* shader_items[SHADER_MAX_COUNT + 1];
    i32 count = 0;
    for (i32 i = 0; i < all_files.count; i++) {
        if (!cstr_ends_with(all_files.items[i], ".hlsl") || !cstr_eq(path_dir(all_files.items[i]), shader_dir)) continue;
        if (count == set->max_count) {
            shader_error(all_files.items[i], 1, 1, tests ? "more than 8 test shaders (ids 56..63)" : "more than 64 shaders (BMF_GPU_MAX_SHADERS)");
            return SHADERS_FAILED;
        }
        shader_items[count++] = all_files.items[i];
    }
    *shader_count = count;

    WIN32_FILE_ATTRIBUTE_DATA compiler_info;
    bmf_memset(&compiler_info, 0, sizeof compiler_info);
    if (!GetFileAttributesExW(wide(compiler_path), WIN32_GetFileExInfoStandard, &compiler_info)) fail_json("d3dcompiler_not_found", compiler_path);
    u64 h = hash_cstr(HASH_SEED, SHADER_GENERATOR_VERSION);
    h = hash_cstr(h, set->out_name);
    h = hash_bytes(h, &compiler_info.ftLastWriteTime, sizeof compiler_info.ftLastWriteTime);
    h = hash_bytes(h, &compiler_info.nFileSizeLow, sizeof compiler_info.nFileSizeLow);
    i64 sizes[1024];
    u8* contents[1024];
    for (i32 i = 0; i < all_files.count; i++) {
        contents[i] = read_file(all_files.items[i], &sizes[i]);
        if (!contents[i]) fail_json("read_failed", all_files.items[i]);
        h = hash_cstr(h, all_files.items[i]);
        h = hash_bytes(h, &sizes[i], sizeof sizes[i]);
        h = hash_bytes(h, contents[i], sizes[i]);
    }
    sbuf stamp = sb_make(32);
    sb_put_hex64(&stamp, h);
    i64 old_len = 0;
    char* old_stamp = (char*)read_file(stamp_path, &old_len);
    if (old_stamp && cstr_eq(old_stamp, stamp.buf) && file_exists(out_path)) return SHADERS_UP_TO_DATE;

    // Names become enum values: [a-z][a-z0-9_]*.
    for (i32 s = 0; s < count; s++) {
        const char* name = path_name(shader_items[s]);
        i32 length = cstr_len(name) - 5;   // without ".hlsl"
        b32 valid = length > 0 && name[0] >= 'a' && name[0] <= 'z';
        for (i32 i = 0; i < length; i++) {
            char c = name[i];
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) valid = 0;
        }
        if (!valid) {
            shader_error(shader_items[s], 1, 1, cat3("shader file names must be [a-z][a-z0-9_]*.hlsl (they become ", set->enum_prefix, "<NAME>)"));
            return SHADERS_FAILED;
        }
    }

    D3DCompile_fn compile = 0;
    if (count > 0) {
        HMODULE compiler = LoadLibraryExW(wide("d3dcompiler_47.dll"), 0, WIN32_LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (compiler) compile = (D3DCompile_fn)GetProcAddress(compiler, "D3DCompile");
        if (!compile) fail_json("d3dcompiler_not_found", "LoadLibraryExW(d3dcompiler_47.dll) or GetProcAddress(D3DCompile) failed");
    }

    sbuf out = sb_make(64 << 20);
    char* source_dir_forward = slashes_forward(set->source_dir + 1);
    sb_put(&out, "// Generated by build.c from "); sb_put(&out, source_dir_forward); sb_put(&out, "/*.hlsl (task 1A-15, D-14). Do not edit.\n");
    if (tests) {
        sb_put(&out, "// Included by tests/test_gpu_d3d11.c.\n#ifndef BMF_TEST_SHADERS_D3D11_H\n#define BMF_TEST_SHADERS_D3D11_H\n\n");
        sb_put(&out, "// Test shader ids, in file name order, above the runtime's.\nenum {\n    GPU_TEST_SHADER_FIRST = ");
        sb_put_i64(&out, set->first_id); sb_put(&out, ",\n");
    } else {
        sb_put(&out, "// Included by src/gpu/d3d11/gpu_d3d11.c after its gpu_d3d11_shader_code type.\n#ifndef BMF_SHADERS_D3D11_H\n#define BMF_SHADERS_D3D11_H\n\n");
        sb_put(&out, "// Shader ids, in file name order. Every backend's generated header defines the same enum.\ntypedef enum bmf_shader_id {\n");
    }
    for (i32 s = 0; s < count; s++) {
        const char* name = path_name(shader_items[s]);
        sb_put(&out, "    "); sb_put_shader_enum(&out, set->enum_prefix, name);
        sb_put(&out, " = "); sb_put_i64(&out, set->first_id + s); sb_put(&out, ",   // "); sb_put(&out, source_dir_forward);
        sb_put_char(&out, '/'); sb_put(&out, name); sb_put(&out, "\n");
    }
    if (tests) {
        sb_put(&out, "    GPU_TEST_SHADER_END = "); sb_put_i64(&out, set->first_id + count); sb_put(&out, ",\n};\n\n");
        sb_put(&out, "BMF_STATIC_ASSERT(BMF_SHADER_COUNT <= GPU_TEST_SHADER_FIRST, \"runtime shader ids reach the test shader ids\");\n\n");
    } else {
        sb_put(&out, "    BMF_SHADER_COUNT = "); sb_put_i64(&out, count); sb_put(&out, ",\n} bmf_shader_id;\n\n");
        sb_put(&out, "BMF_STATIC_ASSERT(BMF_SHADER_COUNT <= BMF_GPU_MAX_SHADERS, \"too many shaders\");\n");
        sb_put(&out, "BMF_STATIC_ASSERT(BMF_GPU_MAX_SHADERS == 64 && BMF_GPU_MAX_BIND_UNIFORMS == 4 && BMF_GPU_MAX_BIND_TEXTURES == 8 &&\n");
        sb_put(&out, "                  BMF_GPU_MAX_BIND_SAMPLERS == 4, \"build.c SHADER_MAX_* limits are out of date\");\n\n");
    }

    sbuf table = sb_make(64 * 1024);
    static const u32 config_flags[2] = {
        WIN32_D3DCOMPILE_DEBUG | WIN32_D3DCOMPILE_SKIP_OPTIMIZATION | WIN32_D3DCOMPILE_ENABLE_STRICTNESS | WIN32_D3DCOMPILE_WARNINGS_ARE_ERRORS,
        WIN32_D3DCOMPILE_OPTIMIZATION_LEVEL3 | WIN32_D3DCOMPILE_ENABLE_STRICTNESS | WIN32_D3DCOMPILE_WARNINGS_ARE_ERRORS,
    };
    static const char* const stage_entries[2] = { "vs_main", "ps_main" };
    static const char* const stage_targets[2] = { "vs_5_0", "ps_5_0" };
    static const char* const stage_suffixes[2] = { "_vs", "_ps" };
    b32 failed = 0;
    shader_slots slots[SHADER_MAX_COUNT];
    bmf_memset(slots, 0, sizeof slots);
    for (i32 config = 0; config < 2 && !failed; config++) {   // release only when every debug compile passed
        sb_put(&out, config == 0 ? "#if BMF_DEBUG\n" : "#else\n");
        for (i32 s = 0; s < count; s++) {
            const char* path = shader_items[s];
            i32 file_index = 0;
            while (all_files.items[file_index] != path) file_index++;
            const char* name = path_name(path);
            char* base_name = cstr_copy(name);
            base_name[cstr_len(base_name) - 5] = 0;
            shader_slots config_slots = { 0, 0, 0 };
            for (i32 stage = 0; stage < 2; stage++) {
                ID3DBlob* code = 0;
                ID3DBlob* errors = 0;
                // The full path as the source name: messages name it, and #include "x" resolves next to it.
                HRESULT hr = compile(contents[file_index], (size_t)sizes[file_index], path, 0, WIN32_D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                     stage_entries[stage], stage_targets[stage], config_flags[config], 0, &code, &errors);
                if (hr < 0) {
                    i32 printed = errors ? shader_print_messages((const char*)errors->lpVtbl->GetBufferPointer(errors), path) : 0;
                    if (printed == 0) {
                        sbuf text = sb_make(256);
                        sb_put(&text, "D3DCompile failed for "); sb_put(&text, stage_entries[stage]); sb_put(&text, " with HRESULT ");
                        sb_put_hex32(&text, (u32)hr);
                        shader_error(path, 1, 1, text.buf);
                    }
                    if (errors) errors->lpVtbl->Release(errors);
                    failed = 1;
                    break;   // the other stage would mostly repeat the same messages
                }
                if (errors) errors->lpVtbl->Release(errors);
                const u8* bytes = (const u8*)code->lpVtbl->GetBufferPointer(code);
                i64 size = (i64)code->lpVtbl->GetBufferSize(code);
                const char* reflect_error = shader_reflect_slots(bytes, size, &config_slots);
                if (reflect_error) {
                    shader_error(path, 1, 1, cat3(stage_entries[stage], ": ", reflect_error));
                    failed = 1;
                }
                sb_put_bytes_array(&out, cat3(set->symbol_prefix, base_name, stage_suffixes[stage]), bytes, size);
                code->lpVtbl->Release(code);
            }
            if (failed) continue;
            if (config == 0) {
                slots[s] = config_slots;
            } else if (config_slots.uniform_mask != slots[s].uniform_mask || config_slots.texture_mask != slots[s].texture_mask ||
                       config_slots.sampler_mask != slots[s].sampler_mask) {
                shader_error(path, 1, 1, "the debug and release builds declare different b/t/s registers (an unused resource "
                                         "is kept only without optimization; remove it)");
                failed = 1;
            }
            if (config == 1) {
                const char* symbol = cat2(set->symbol_prefix, base_name);
                sb_put(&table, "    ["); sb_put_shader_enum(&table, set->enum_prefix, name);
                sb_put(&table, "] = { \""); sb_put(&table, base_name); sb_put(&table, "\", ");
                sb_put(&table, symbol); sb_put(&table, "_vs, (i32)sizeof "); sb_put(&table, symbol);
                sb_put(&table, "_vs, "); sb_put(&table, symbol); sb_put(&table, "_ps, (i32)sizeof "); sb_put(&table, symbol);
                sb_put(&table, "_ps, "); sb_put_hex32(&table, slots[s].uniform_mask); sb_put(&table, ", ");
                sb_put_hex32(&table, slots[s].texture_mask); sb_put(&table, ", "); sb_put_hex32(&table, slots[s].sampler_mask); sb_put(&table, " },\n");
            }
        }
    }
    if (failed) {
        DeleteFileW(wide(stamp_path));
        return SHADERS_FAILED;
    }
    sb_put(&out, "#endif\n\n");
    if (tests) sb_put(&out, "static const gpu_d3d11_shader_code gpu_d3d11_test_shader_codes[BMF_GPU_MAX_SHADERS] = {\n");
    else sb_put(&out, "// Mutable: tests copy their test shaders in at ids GPU_TEST_SHADER_FIRST and up.\nstatic gpu_d3d11_shader_code gpu_d3d11_shader_codes[BMF_GPU_MAX_SHADERS] = {\n");
    if (count == 0) sb_put(&out, "    { 0 },\n");
    sb_put(&out, table.buf);
    sb_put(&out, "};\n\n#endif\n");

    make_dirs(gen_dir);
    DeleteFileW(wide(stamp_path));
    if (!write_file(out_path, out.buf, out.len)) fail_json("write_failed", out_path);
    if (!write_file(stamp_path, stamp.buf, stamp.len)) fail_json("write_failed", stamp_path);
    return SHADERS_BUILT;
}

// ================================================================ commands
static void usage(void) {
    out_line("usage: build win64 debug|release <game_dir> [--agent] | build test [filter] [--update-golden NAME] | build api | build shaders | build clean");
    fail_json("usage", 0);
}

// Runs generate_shaders. On failure prints the command's summary line (after the compiler
// diagnostics) and returns 0; otherwise returns "built" or "up_to_date".
static const char* shaders_step(const toolchain* tc, const char* target) {
    i32 shader_count = 0;
    shaders_status s = generate_shaders(tc, &SHADERS_RUNTIME, &shader_count);
    if (s != SHADERS_FAILED && cstr_eq(target, "test")) {
        shaders_status test_status = generate_shaders(tc, &SHADERS_TESTS, &shader_count);
        if (test_status != SHADERS_UP_TO_DATE) s = test_status;
    }
    if (s != SHADERS_FAILED) return s == SHADERS_BUILT ? "built" : "up_to_date";
    sbuf b = sb_make(1024);
    sb_put(&b, "{\"build\":\"fail\",\"target\":"); sb_put_json(&b, target);
    sb_put(&b, ",\"error\":\"shader_compile_failed\",\"ms\":"); sb_put_i64(&b, elapsed_ms()); sb_put(&b, "}");
    out_line(b.buf);
    return 0;
}

static i32 command_shaders(const toolchain* tc) {
    i32 shader_count = 0;
    shaders_status s = generate_shaders(tc, &SHADERS_RUNTIME, &shader_count);
    sbuf b = sb_make(1024);
    sb_put(&b, "{\"build\":"); sb_put_json(&b, s == SHADERS_FAILED ? "fail" : s == SHADERS_BUILT ? "ok" : "up_to_date");
    sb_put(&b, ",\"target\":\"shaders\",\"backend\":\"d3d11\",\"shaders\":"); sb_put_i64(&b, shader_count);
    if (s == SHADERS_FAILED) sb_put(&b, ",\"error\":\"shader_compile_failed\"");
    sb_put(&b, ",\"out\":"); sb_put_json(&b, slashes_forward(cat2(tc->root, "\\out\\gen\\shaders_d3d11.h")));
    sb_put(&b, ",\"ms\":"); sb_put_i64(&b, elapsed_ms()); sb_put(&b, "}");
    out_line(b.buf);
    return s == SHADERS_FAILED ? 1 : 0;
}

// `agent`: a release build with BMF_AGENT=1 (--agent; out/<game>/win64-release-agent).
static i32 command_win64(const toolchain* tc, const char* config, const char* game_arg, b32 agent) {
    b32 release;
    if (cstr_eq(config, "debug")) release = 0;
    else if (cstr_eq(config, "release")) release = 1;
    else { usage(); return 1; }
    if (agent && !release) usage();   // debug builds always have the agent tools
    char* game_dir = full_path(game_arg);
    if (!game_dir || !dir_exists(game_dir)) fail_json("game_dir_not_found", game_arg);
    if (!file_exists(cat2(game_dir, "\\game.c"))) fail_json("game_c_not_found", cat2(game_dir, "\\game.c"));
    const char* game = path_name(game_dir);
    char* target_config = cat3("win64-", config, agent ? "-agent" : "");
    char* out_dir = cat3(tc->root, "\\out\\", cat3(game, "\\", target_config));
    char* gen_dir = cat3(tc->root, "\\out\\gen\\", cat3(game, "\\", target_config));
    const char* shaders = shaders_step(tc, "win64");
    if (!shaders) return 1;

    // Debug builds the exe first: the game DLL links against its import library (game.lib). The
    // debug exe does not include the game, so editing only the game leaves it up to date and the
    // DLL still rebuilds while a running game.exe keeps the exe locked (hot reload).
    binary_desc bins[2];
    i32 bin_count = 0;
    binary_desc exe = { BIN_RUNTIME_EXE, "runtime", release, game_dir, gen_dir, cat2(out_dir, "\\game.exe"), agent };
    bins[bin_count++] = exe;
    if (!release) {
        binary_desc dll = { BIN_GAME_DLL, "game_dll", 0, game_dir, gen_dir, cat2(out_dir, "\\game.dll"), 0 };
        bins[bin_count++] = dll;
    }
    const char* status[2] = { "skipped", "skipped" };
    b32 failed = 0, any_built = 0;
    for (i32 i = 0; i < bin_count && !failed; i++) {
        build_status s = build_binary(tc, &bins[i]);
        status[i] = status_name(s);
        failed = s == BUILD_FAILED;
        any_built |= s == BUILD_BUILT;
    }
    sbuf b = sb_make(8192);
    sb_put(&b, "{\"build\":"); sb_put(&b, failed ? "\"fail\"" : any_built ? "\"ok\"" : "\"up_to_date\"");
    sb_put(&b, ",\"target\":\"win64\",\"config\":"); sb_put_json(&b, config);
    if (agent) sb_put(&b, ",\"agent\":true");
    sb_put(&b, ",\"game\":"); sb_put_json(&b, game);
    sb_put(&b, ",\"ms\":"); sb_put_i64(&b, elapsed_ms());
    sb_put(&b, ",\"out\":"); sb_put_json(&b, slashes_forward(out_dir));
    sb_put(&b, ",\"shaders\":"); sb_put_json(&b, shaders);
    sb_put(&b, ",\"binaries\":{");
    for (i32 i = 0; i < bin_count; i++) {
        if (i) sb_put_char(&b, ',');
        sb_put_json(&b, bins[i].name); sb_put_char(&b, ':'); sb_put_json(&b, status[i]);
    }
    sb_put(&b, "}}");
    out_line(b.buf);
    return failed ? 1 : 0;
}

static i32 command_win64(const toolchain* tc, const char* config, const char* game_arg, b32 agent);

// Builds (debug, or release with BMF_AGENT=1) every folder under <root>\games and <root>\tests\bench
// that has a game.c and `marker` (the tests folder or perf.budget). Returns 0 when all succeeded.
static i32 build_scene_dirs(const toolchain* tc, const char* marker, b32 release_agent) {
    const char* parents[2] = { "\\games", "\\tests\\bench" };
    i32 code = 0;
    for (i32 p = 0; p < 2; p++) {
        char* parent = cat2(tc->root, parents[p]);
        char* names[64];
        i32 count = list_subdirs(parent, names, 64);
        for (i32 i = 0; i < count; i++) {
            char* dir = cat3(parent, "\\", names[i]);
            char* marked = cat3(dir, "\\", marker);
            if (!dir_exists(marked) && !file_exists(marked)) continue;
            if (!file_exists(cat2(dir, "\\game.c"))) continue;
            if (command_win64(tc, release_agent ? "release" : "debug", dir, release_agent) != 0) code = 1;
        }
    }
    return code;
}

static i32 command_test(const toolchain* tc, const char* filter, const char* update_golden) {
    if (!file_exists(cat2(tc->root, "\\tools\\test\\test_main.c"))) fail_json("no_test_runner", "tools/test/test_main.c does not exist yet (task 1A-08)");
    char* out_dir = cat2(tc->root, "\\out\\test\\win64-debug");
    const char* shaders = shaders_step(tc, "test");
    if (!shaders) return 1;
    binary_desc tests = { BIN_TESTS, "tests", 0, 0, cat2(tc->root, "\\out\\gen\\test\\win64-debug"), cat2(out_dir, "\\test.exe"), 0 };
    build_status s = build_binary(tc, &tests);
    if (s == BUILD_FAILED) {
        sbuf b = sb_make(1024);
        sb_put(&b, "{\"build\":\"fail\",\"target\":\"test\",\"ms\":"); sb_put_i64(&b, elapsed_ms()); sb_put(&b, "}");
        out_line(b.buf);
        return 1;
    }
    sbuf b = sb_make(1024);
    sb_put(&b, "{\"build\":"); sb_put_json(&b, s == BUILD_BUILT ? "ok" : "up_to_date");
    sb_put(&b, ",\"target\":\"test\",\"shaders\":"); sb_put_json(&b, shaders);
    sb_put(&b, ",\"ms\":"); sb_put_i64(&b, elapsed_ms()); sb_put(&b, "}");
    out_line(b.buf);
    // Host checks first (one JSON line each), then the unit tests. A failed host check fails the
    // command but the unit tests still run.
    i32 host_api = host_check_api(tc);
    i32 host = host_check_vtable_slots(tc);
    if (host == 2) {
        sbuf f = sb_make(1024);
        sb_put(&f, "{\"build\":\"fail\",\"target\":\"test\",\"error\":\"vtable_check_compile_failed\",\"ms\":"); sb_put_i64(&f, elapsed_ms()); sb_put(&f, "}");
        out_line(f.buf);
        return 1;
    }
    // The test runner prints one JSON line per test and a final summary line.
    const char* run_args[4];
    i32 run_count = 0;
    run_args[run_count++] = tests.out_file;
    run_args[run_count++] = filter ? filter : "";
    if (update_golden) { run_args[run_count++] = "--update-golden"; run_args[run_count++] = update_golden; }
    i32 code = run_process(run_args, run_count, 0, 0);
    if (code < 0) fail_json("test_runner_failed_to_start", tests.out_file);

    // Goldens: build each game and bench scene that has tests (debug: the protocol and replays),
    // then the runner's golden mode (one JSON line per golden and lint file, then {"goldens":...}).
    i32 golden_code = 0;
    if (!filter || starts_with(filter, "golden") || starts_with(filter, "lint")) {
        if (build_scene_dirs(tc, "tests", 0) != 0) golden_code = 1;
        const char* golden_args[5];
        i32 golden_count = 0;
        golden_args[golden_count++] = tests.out_file;
        golden_args[golden_count++] = "--golden";
        golden_args[golden_count++] = filter ? filter : "";
        if (update_golden) { golden_args[golden_count++] = "--update-golden"; golden_args[golden_count++] = update_golden; }
        i32 g = run_process(golden_args, golden_count, 0, 0);
        if (g != 0) golden_code = 1;
    } else {
        sbuf skip = sb_make(512);
        sb_put(&skip, "{\"goldens\":\"skipped\",\"reason\":\"the filter does not start with golden or lint (golden test names: golden.<game>.<scenario>.<rate>hz[.<label>])\"}");
        out_line(skip.buf);
    }
    // Perf budgets (task 1A-42): build each game and bench scene that has a perf.budget in release
    // with BMF_AGENT=1, then the runner's perf mode (one JSON line per budgeted replay with the zone
    // breakdown, appended to out/bench/history.jsonl, then {"perf":...}). Not with --update-golden.
    i32 perf_code = 0;
    if (!update_golden && (!filter || starts_with(filter, "perf"))) {
        if (build_scene_dirs(tc, "perf.budget", 1) != 0) perf_code = 1;
        const char* perf_args[3] = { tests.out_file, "--perf", filter ? filter : "" };
        if (run_process(perf_args, 3, 0, 0) != 0) perf_code = 1;
    } else {
        sbuf skip = sb_make(512);
        sb_put(&skip, update_golden ? "{\"perf\":\"skipped\",\"reason\":\"not with --update-golden\"}"
                                    : "{\"perf\":\"skipped\",\"reason\":\"the filter does not start with perf (perf test names: perf.<game>.<replay>)\"}");
        out_line(skip.buf);
    }
    return code == 0 && golden_code == 0 && perf_code == 0 && host == 0 && host_api == 0 ? 0 : 1;
}

static i32 command_clean(const char* root) {
    char* out = cat2(root, "\\out");
    if (!cstr_ends_with(out, "\\out")) fail_json("clean_refused", out);
    if (dir_exists(out) && !delete_tree(out)) fail_json("clean_failed", cat2(slashes_forward(out), " (a file may be in use)"));
    sbuf b = sb_make(1024);
    sb_put(&b, "{\"build\":\"ok\",\"target\":\"clean\",\"ms\":"); sb_put_i64(&b, elapsed_ms()); sb_put(&b, "}");
    out_line(b.buf);
    return 0;
}

// ================================================================ entry
void build_entry(void) {
    QueryPerformanceFrequency(&ticks_per_second);
    QueryPerformanceCounter(&build_start_ticks);
    arena_base = (u8*)VirtualAlloc(0, (size_t)ARENA_SIZE, WIN32_MEM_RESERVE | WIN32_MEM_COMMIT, WIN32_PAGE_READWRITE);
    if (!arena_base) ExitProcess(1);

    char* argv[16];
    i32 argc = parse_args(argv, 16);
    if (argc < 2) usage();
    const char* command = argv[1];

    if (cstr_eq(command, "clean")) {
        WCHAR exe[2048];
        GetModuleFileNameW(0, exe, 2048);
        ExitProcess((u32)command_clean(path_dir(narrow(exe))));
    }
    if (!cstr_eq(command, "win64") && !cstr_eq(command, "test") && !cstr_eq(command, "shaders") && !cstr_eq(command, "api")) usage();
    toolchain tc = find_toolchain();
    i32 code = 1;
    if (cstr_eq(command, "win64")) {
        b32 agent = argc == 5 && cstr_eq(argv[4], "--agent");
        if (argc != 4 && !agent) usage();
        code = command_win64(&tc, argv[2], argv[3], agent);
    } else if (cstr_eq(command, "shaders")) {
        if (argc != 2) usage();
        code = command_shaders(&tc);
    } else if (cstr_eq(command, "api")) {
        if (argc != 2) usage();
        code = command_api(&tc);
    } else if (cstr_eq(command, "test")) {
        const char* filter = 0;
        const char* update_golden = 0;
        for (i32 i = 2; i < argc; i++) {
            if (cstr_eq(argv[i], "--update-golden") && i + 1 < argc) update_golden = argv[++i];
            else if (!filter) filter = argv[i];
            else usage();
        }
        code = command_test(&tc, filter, update_golden);
    } else {
        usage();
    }
    ExitProcess((u32)code);
}
