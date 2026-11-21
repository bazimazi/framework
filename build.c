// build.c: the bmf build program (SPEC §4, D-12). CRT-free (D-04); built by bootstrap.bat.
//
//   build win64 debug|release <game_dir>   runtime exe (+ game.dll in debug) in out/<game>/win64-<config>/
//   build test [filter]                    build and run the unit-test runner (out/test/win64-debug/)
//   build clean                            delete out/
//
// Every binary is one translation unit (D-08): build.c writes a tiny generated root to
// out/gen/<...>/<binary>.c that sets the build mode and #includes src/bmf.c (plus game.c or the
// test runner), then runs one clang command that compiles and links it.
// A binary is skipped when the hash of its flags, clang version, generated root and every input
// file is unchanged since its last successful build (stamp file next to the output).
// Output: clang diagnostics pass through unchanged (`file:line:col: error: ...`); the last line
// is one JSON summary. Exit code 0 on success, 1 on failure.

#include "src/base/base_types.h"
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
} binary_desc;

static const char* const SOURCE_EXTS[] = { ".c", ".h", ".hlsl", ".wgsl", ".inc", 0 };

static build_status build_binary(const toolchain* tc, const binary_desc* d) {
    // 1. Generated unity root (D-08).
    char* src = slashes_forward(cat2(tc->root, "\\src"));
    sbuf root = sb_make(8192);
    sb_put(&root, "// Generated by build.c. Do not edit.\n");
    if (d->kind == BIN_RUNTIME_EXE) {
        sb_put(&root, "#define BMF_BUILD_RUNTIME_EXE 1\n");
        sb_put(&root, d->release ? "#define BMF_GAME_LINK_STATIC 1\n" : "#define BMF_GAME_LINK_DLL 1\n");
    }
    if (d->kind == BIN_GAME_DLL) sb_put(&root, "#define BMF_BUILD_GAME_DLL 1\n");
    if (d->kind == BIN_TESTS) sb_put(&root, "#define BMF_BUILD_TESTS 1\n");
    sb_put(&root, "#include \""); sb_put(&root, src); sb_put(&root, "/bmf.c\"\n");
    b32 includes_game = d->kind == BIN_GAME_DLL || (d->kind == BIN_RUNTIME_EXE && d->release);
    if (includes_game) { sb_put(&root, "#include \""); sb_put(&root, slashes_forward(d->game_dir)); sb_put(&root, "/game.c\"\n"); }
    if (d->kind == BIN_TESTS) { sb_put(&root, "#include \""); sb_put(&root, slashes_forward(tc->root)); sb_put(&root, "/tools/test/test_main.c\"\n"); }
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
    if (d->kind == BIN_GAME_DLL) args_add_all(&args, LINK_WIN64_DLL);
    else args_add_all(&args, LINK_WIN64_EXE);
    args_add(&args, cat2("-L", tc->sdk_lib));
    args_add_all(&args, LIBS_WIN64);
    check_banned(&args);

    // 3. Input hash: clang version, command line (minus the clang path), generated root, inputs.
    u64 h = hash_cstr(HASH_SEED, tc->clang_version);
    for (i32 i = 1; i < args.count; i++) h = hash_cstr(h, args.items[i]);
    h = hash_cstr(h, root.buf);
    char* list_items[8192];
    path_list inputs = { list_items, 0, 8192 };
    list_files(&inputs, cat2(tc->root, "\\src"), SOURCE_EXTS);
    list_files(&inputs, cat2(tc->root, "\\shaders"), SOURCE_EXTS);
    if (includes_game) list_files(&inputs, d->game_dir, SOURCE_EXTS);
    if (d->kind == BIN_TESTS) {
        list_files(&inputs, cat2(tc->root, "\\tools\\test"), SOURCE_EXTS);
        list_files(&inputs, cat2(tc->root, "\\tests"), SOURCE_EXTS);
    }
    sort_paths(&inputs);
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
    if (old_stamp && cstr_eq(old_stamp, stamp.buf) && file_exists(d->out_file)) return BUILD_UP_TO_DATE;

    // 4. Compile and link.
    make_dirs(d->gen_dir);
    make_dirs(path_dir(d->out_file));
    if (!write_file(root_path, root.buf, root.len)) fail_json("write_failed", root_path);
    DeleteFileW(wide(stamp_path));
    i32 code = run_process(args.items, args.count, 0, 0);
    if (code != 0) return BUILD_FAILED;
    if (!write_file(stamp_path, stamp.buf, stamp.len)) fail_json("write_failed", stamp_path);
    return BUILD_BUILT;
}

static const char* status_name(build_status s) {
    return s == BUILD_BUILT ? "built" : s == BUILD_UP_TO_DATE ? "up_to_date" : "failed";
}

// ================================================================ commands
static void usage(void) {
    out_line("usage: build win64 debug|release <game_dir> | build test [filter] | build clean");
    fail_json("usage", 0);
}

static i32 command_win64(const toolchain* tc, const char* config, const char* game_arg) {
    b32 release;
    if (cstr_eq(config, "debug")) release = 0;
    else if (cstr_eq(config, "release")) release = 1;
    else { usage(); return 1; }
    char* game_dir = full_path(game_arg);
    if (!game_dir || !dir_exists(game_dir)) fail_json("game_dir_not_found", game_arg);
    if (!file_exists(cat2(game_dir, "\\game.c"))) fail_json("game_c_not_found", cat2(game_dir, "\\game.c"));
    const char* game = path_name(game_dir);
    char* target_config = cat2("win64-", config);
    char* out_dir = cat3(tc->root, "\\out\\", cat3(game, "\\", target_config));
    char* gen_dir = cat3(tc->root, "\\out\\gen\\", cat3(game, "\\", target_config));

    // Debug builds the game DLL first: it is what hot reload needs, and it still builds while a
    // running game.exe keeps the exe locked.
    binary_desc bins[2];
    i32 bin_count = 0;
    if (!release) {
        binary_desc dll = { BIN_GAME_DLL, "game_dll", 0, game_dir, gen_dir, cat2(out_dir, "\\game.dll") };
        bins[bin_count++] = dll;
    }
    binary_desc exe = { BIN_RUNTIME_EXE, "runtime", release, game_dir, gen_dir, cat2(out_dir, "\\game.exe") };
    bins[bin_count++] = exe;
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
    sb_put(&b, ",\"game\":"); sb_put_json(&b, game);
    sb_put(&b, ",\"ms\":"); sb_put_i64(&b, elapsed_ms());
    sb_put(&b, ",\"out\":"); sb_put_json(&b, slashes_forward(out_dir));
    sb_put(&b, ",\"binaries\":{");
    for (i32 i = 0; i < bin_count; i++) {
        if (i) sb_put_char(&b, ',');
        sb_put_json(&b, bins[i].name); sb_put_char(&b, ':'); sb_put_json(&b, status[i]);
    }
    sb_put(&b, "}}");
    out_line(b.buf);
    return failed ? 1 : 0;
}

static i32 command_test(const toolchain* tc, const char* filter) {
    if (!file_exists(cat2(tc->root, "\\tools\\test\\test_main.c"))) fail_json("no_test_runner", "tools/test/test_main.c does not exist yet (task 1A-08)");
    char* out_dir = cat2(tc->root, "\\out\\test\\win64-debug");
    binary_desc tests = { BIN_TESTS, "tests", 0, 0, cat2(tc->root, "\\out\\gen\\test\\win64-debug"), cat2(out_dir, "\\test.exe") };
    build_status s = build_binary(tc, &tests);
    if (s == BUILD_FAILED) {
        sbuf b = sb_make(1024);
        sb_put(&b, "{\"build\":\"fail\",\"target\":\"test\",\"ms\":"); sb_put_i64(&b, elapsed_ms()); sb_put(&b, "}");
        out_line(b.buf);
        return 1;
    }
    sbuf b = sb_make(1024);
    sb_put(&b, "{\"build\":"); sb_put_json(&b, s == BUILD_BUILT ? "ok" : "up_to_date");
    sb_put(&b, ",\"target\":\"test\",\"ms\":"); sb_put_i64(&b, elapsed_ms()); sb_put(&b, "}");
    out_line(b.buf);
    // The test runner prints one JSON line per test and a final summary line; its exit code is ours.
    const char* run_args[2] = { tests.out_file, filter };
    i32 code = run_process(run_args, filter ? 2 : 1, 0, 0);
    if (code < 0) fail_json("test_runner_failed_to_start", tests.out_file);
    return code == 0 ? 0 : 1;
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
    if (!cstr_eq(command, "win64") && !cstr_eq(command, "test")) usage();
    toolchain tc = find_toolchain();
    i32 code = 1;
    if (cstr_eq(command, "win64")) {
        if (argc != 4) usage();
        code = command_win64(&tc, argv[2], argv[3]);
    } else if (cstr_eq(command, "test")) {
        if (argc > 3) usage();
        code = command_test(&tc, argc == 3 ? argv[2] : 0);
    } else {
        usage();
    }
    ExitProcess((u32)code);
}
