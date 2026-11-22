// win32_core.c: Win32 platform core (SPEC §9, task 1A-11). Implements src/platform/platform.h
// except window and events (win32_window.c, 1A-12): process start and arguments, buffered stdio,
// virtual memory, time and sleep, files, file-change polling, DLLs, futex and semaphore,
// threads. The fatal-error hook (bmf.h) and crash reports are in win32_crash.c.

// Crash reports live in win32_crash.c (included after this file).
enum { WIN32_CRASH_STACK_GUARANTEE = 64 * 1024 };   // stack kept for the report after an overflow
static void win32_crash_install(void);
static void win32_crash_module_add(void* module);
static void win32_crash_module_remove(void* module);

// ---------------------------------------------------------------- virtual memory (base_memory.h hooks)
void* bmf_platform_reserve(i64 size) {
    return VirtualAlloc(0, (size_t)size, WIN32_MEM_RESERVE, WIN32_PAGE_NOACCESS);
}

b32 bmf_platform_commit(void* base, i64 size) {
    return VirtualAlloc(base, (size_t)size, WIN32_MEM_COMMIT, WIN32_PAGE_READWRITE) != 0;
}

void bmf_platform_decommit(void* base, i64 size) {
    VirtualFree(base, (size_t)size, WIN32_MEM_DECOMMIT);
}

// ---------------------------------------------------------------- process state
// Everything the platform core keeps between calls. Fixed-size, no heap (R4).
enum {
    WIN32_ARG_MAX = 1024,                    // arguments; more is a panic
    WIN32_ARG_BYTES = 3 * 32768 + WIN32_ARG_MAX,   // the command line is <= 32767 UTF-16 units; each unit
                                             // becomes <= 3 UTF-8 bytes (a surrogate pair: 4 for 2)
    WIN32_STDIO_BUFFER = 64 * 1024,
};

static struct {
    b32 initialized;
    u32 main_thread_id;
    i64 counter_frequency;     // QueryPerformanceFrequency, ticks per second
    HANDLE sleep_timer;
    HANDLE std_handles[2];     // indexed by bmf_stream
    i32 stdio_len[2];
    char stdio_buffer[2][WIN32_STDIO_BUFFER];
    i32 arg_count;
    const char* arg_values[WIN32_ARG_MAX];
    char arg_bytes[WIN32_ARG_BYTES];
} win32_core;

// ---------------------------------------------------------------- command line
// Splits a Windows command line into UTF-8 arguments with the Universal CRT rules:
// - argument 0 (the program) ends at the next space/tab, or is everything between the first pair
//   of quotes; backslashes in it are literal;
// - other arguments are separated by spaces/tabs outside quotes;
// - 2n backslashes before a quote give n backslashes and the quote opens/closes quoting;
//   2n+1 backslashes before a quote give n backslashes and a literal quote;
// - inside quotes, "" gives a literal quote and quoting continues;
// - backslashes not followed by a quote are literal.
// Writes NUL-terminated strings into bytes[0..bytes_capacity) and pointers into argv. Returns the
// argument count, or -1 if either capacity is too small. O(n). Uses 64 KiB of stack.
i32 bmf_win32_split_command_line(const WCHAR* command_line, char* bytes, i32 bytes_capacity,
                                 const char** argv, i32 argv_capacity) {
    WCHAR unit_buffer[32768];   // the current argument in UTF-16
    const WCHAR* p = command_line;
    i32 count = 0, bytes_used = 0;
    b32 first = 1;
    for (;;) {
        if (!first) {
            while (*p == ' ' || *p == '\t') p++;
            if (*p == 0) break;
        }
        i32 units = 0;
        if (first) {
            if (*p == '"') {
                p++;
                while (*p && *p != '"' && units < 32767) unit_buffer[units++] = *p++;
                if (*p == '"') p++;
            } else {
                while (*p && *p != ' ' && *p != '\t' && units < 32767) unit_buffer[units++] = *p++;
            }
        } else {
            b32 in_quotes = 0;
            while (*p && units < 32767) {
                if (!in_quotes && (*p == ' ' || *p == '\t')) break;
                if (*p == '\\') {
                    i32 backslashes = 0;
                    while (*p == '\\') { backslashes++; p++; }
                    if (*p == '"') {
                        for (i32 i = 0; i < backslashes / 2 && units < 32767; i++) unit_buffer[units++] = '\\';
                        if (backslashes % 2) { if (units < 32767) unit_buffer[units++] = '"'; p++; }
                        // even count: the quote is handled by the next iteration
                    } else {
                        for (i32 i = 0; i < backslashes && units < 32767; i++) unit_buffer[units++] = '\\';
                    }
                    continue;
                }
                if (*p == '"') {
                    if (in_quotes && p[1] == '"') { unit_buffer[units++] = '"'; p += 2; continue; }
                    in_quotes = !in_quotes;
                    p++;
                    continue;
                }
                unit_buffer[units++] = *p++;
            }
        }
        first = 0;
        if (count >= argv_capacity) return -1;
        i32 need = bmf_utf16_to_utf8(unit_buffer, units, (u8*)bytes + bytes_used, bytes_capacity - bytes_used - 1);
        if (need > bytes_capacity - bytes_used - 1) return -1;
        argv[count++] = bytes + bytes_used;
        bytes_used += need;
        bytes[bytes_used++] = 0;
        if (*p == 0) break;
    }
    return count;
}

void bmf_platform_init(void) {
    BMF_ASSERT(!win32_core.initialized);
    bmf_fpu_setup();
    win32_core.initialized = 1;
    win32_core.main_thread_id = GetCurrentThreadId();
    QueryPerformanceFrequency(&win32_core.counter_frequency);
    win32_core.std_handles[BMF_STREAM_STDOUT] = GetStdHandle((u32)WIN32_STD_OUTPUT_HANDLE);
    win32_core.std_handles[BMF_STREAM_STDERR] = GetStdHandle((u32)WIN32_STD_ERROR_HANDLE);
    win32_crash_install();   // win32_crash.c: unhandled exceptions become crash reports
    // High-resolution timers need Windows 10 1803; older systems get a normal one (~1-16 ms late).
    win32_core.sleep_timer = CreateWaitableTimerExW(0, 0, WIN32_CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, WIN32_TIMER_ALL_ACCESS);
    if (!win32_core.sleep_timer) win32_core.sleep_timer = CreateWaitableTimerExW(0, 0, 0, WIN32_TIMER_ALL_ACCESS);
    if (!win32_core.sleep_timer) bmf_panic("CreateWaitableTimerExW failed");
    win32_core.arg_count = bmf_win32_split_command_line(GetCommandLineW(), win32_core.arg_bytes, WIN32_ARG_BYTES,
                                                        win32_core.arg_values, WIN32_ARG_MAX);
    if (win32_core.arg_count < 1) bmf_panic("command line: more than 1024 arguments");
}

bmf_platform_args bmf_platform_get_args(void) {
    BMF_ASSERT(win32_core.initialized);
    bmf_platform_args args = { win32_core.arg_count, win32_core.arg_values };
    return args;
}

void bmf_platform_exit(i32 code) {
    bmf_platform_flush(BMF_STREAM_STDOUT);
    bmf_platform_flush(BMF_STREAM_STDERR);
    ExitProcess((u32)code);
}

i32 bmf_platform_exe_dir(char* out, i32 capacity) {
    WCHAR path[BMF_PLATFORM_PATH_MAX];
    u32 n = GetModuleFileNameW(0, path, BMF_PLATFORM_PATH_MAX);
    if (n == 0 || n >= BMF_PLATFORM_PATH_MAX) return 0;
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/') n--;   // keep the separator
    i32 need = bmf_utf16_to_utf8(path, (i32)n, (u8*)out, capacity - 1);
    if (need > capacity - 1) return 0;
    out[need] = 0;
    return need;
}

// ---------------------------------------------------------------- stdio
static void win32_write_all(HANDLE handle, const char* data, i32 len) {
    while (len > 0) {
        u32 written = 0;
        if (!WriteFile(handle, data, (u32)len, &written, 0) || written == 0) return;   // closed pipe: drop
        data += written;
        len -= (i32)written;
    }
}

void bmf_platform_flush(bmf_stream stream) {
    BMF_ASSERT(stream == BMF_STREAM_STDOUT || stream == BMF_STREAM_STDERR);
    win32_write_all(win32_core.std_handles[stream], win32_core.stdio_buffer[stream], win32_core.stdio_len[stream]);
    win32_core.stdio_len[stream] = 0;
}

i32 bmf_platform_stdin_read(void* buffer, i32 capacity) {
    BMF_ASSERT(buffer && capacity > 0);
    HANDLE input = GetStdHandle((u32)WIN32_STD_INPUT_HANDLE);
    if (!input || input == WIN32_INVALID_HANDLE_VALUE) return 0;
    u32 got = 0;
    if (!ReadFile(input, buffer, (u32)capacity, &got, 0)) {
        u32 error = GetLastError();
        return error == WIN32_ERROR_BROKEN_PIPE || error == WIN32_ERROR_NO_DATA ? 0 : -1;
    }
    return (i32)got;
}

void bmf_platform_write(bmf_stream stream, const char* data, i32 len) {
    BMF_ASSERT(stream == BMF_STREAM_STDOUT || stream == BMF_STREAM_STDERR);
    BMF_ASSERT(win32_core.initialized && GetCurrentThreadId() == win32_core.main_thread_id);
    char* buffer = win32_core.stdio_buffer[stream];
    if (len > WIN32_STDIO_BUFFER - win32_core.stdio_len[stream]) {
        bmf_platform_flush(stream);
        if (len >= WIN32_STDIO_BUFFER) { win32_write_all(win32_core.std_handles[stream], data, len); return; }
    }
    bmf_memcpy(buffer + win32_core.stdio_len[stream], data, len);
    win32_core.stdio_len[stream] += len;
    i32 last_newline = -1;
    for (i32 i = len - 1; i >= 0; i--) if (data[i] == '\n') { last_newline = win32_core.stdio_len[stream] - len + i; break; }
    if (last_newline < 0) return;
    win32_write_all(win32_core.std_handles[stream], buffer, last_newline + 1);
    i32 rest = win32_core.stdio_len[stream] - (last_newline + 1);
    bmf_memmove(buffer, buffer + last_newline + 1, rest);
    win32_core.stdio_len[stream] = rest;
}

// ---------------------------------------------------------------- time
i64 bmf_platform_time_ns(void) {
    i64 counter = 0;
    QueryPerformanceCounter(&counter);
    i64 frequency = win32_core.counter_frequency;
    // Split so the multiply cannot overflow: the remainder is < frequency (10 MHz typically).
    return (counter / frequency) * 1000000000 + (counter % frequency) * 1000000000 / frequency;
}

void bmf_platform_sleep_ns(i64 ns) {
    BMF_ASSERT(win32_core.initialized && GetCurrentThreadId() == win32_core.main_thread_id);
    if (ns <= 0) return;
    i64 due = -((ns + 99) / 100);   // negative = relative, in 100 ns units, rounded up
    if (!SetWaitableTimer(win32_core.sleep_timer, &due, 0, 0, 0, 0)) bmf_panic("SetWaitableTimer failed");
    WaitForSingleObject(win32_core.sleep_timer, (u32)WIN32_INFINITE);
}

// ---------------------------------------------------------------- files
// UTF-8 path to a NUL-terminated UTF-16 path. Returns 0 if it does not fit in `capacity` units.
static b32 win32_path(const char* utf8, WCHAR* out, i32 capacity) {
    i32 need = bmf_utf8_to_utf16(bmf_str_from_cstr(utf8), out, capacity - 1);
    if (need > capacity - 1) return 0;
    out[need] = 0;
    return 1;
}

static bmf_err win32_last_file_error(void) {
    u32 code = GetLastError();
    if (code == WIN32_ERROR_FILE_NOT_FOUND || code == WIN32_ERROR_PATH_NOT_FOUND) return BMF_ERR_NOT_FOUND;
    return BMF_ERR_IO;
}

// Opens an existing file for reading. Other processes may read, write, delete or replace it meanwhile.
static HANDLE win32_open_read(const WCHAR* path) {
    return CreateFileW(path, (u32)WIN32_GENERIC_READ,
                       WIN32_FILE_SHARE_READ | WIN32_FILE_SHARE_WRITE | WIN32_FILE_SHARE_DELETE, 0,
                       WIN32_OPEN_EXISTING, WIN32_FILE_ATTRIBUTE_NORMAL, 0);
}

bmf_err bmf_platform_file_read(const char* path, bmf_arena* arena, bmf_str* out) {
    out->ptr = 0;
    out->len = 0;
    WCHAR wide[BMF_PLATFORM_PATH_MAX];
    if (!win32_path(path, wide, BMF_PLATFORM_PATH_MAX)) return BMF_ERR_RANGE;
    HANDLE file = win32_open_read(wide);
    if (file == WIN32_INVALID_HANDLE_VALUE) return win32_last_file_error();
    i64 size = 0;
    if (!GetFileSizeEx(file, &size)) { CloseHandle(file); return BMF_ERR_IO; }
    if (size >= 0x7FFFFFFF) { CloseHandle(file); return BMF_ERR_RANGE; }
    i64 mark = bmf_arena_mark(arena);
    u8* data = (u8*)bmf_arena_push_aligned(arena, size + 1, 16);
    i64 done = 0;
    while (done < size) {
        u32 got = 0;
        if (!ReadFile(file, data + done, (u32)(size - done), &got, 0) || got == 0) break;
        done += got;
    }
    CloseHandle(file);
    if (done != size) { bmf_arena_pop_to(arena, mark); return BMF_ERR_IO; }   // shrank or failed meanwhile
    data[size] = 0;
    out->ptr = data;
    out->len = (i32)size;
    return BMF_OK;
}

bmf_err bmf_platform_file_map(const char* path, bmf_file_map* out) {
    out->data = 0;
    out->size = 0;
    WCHAR wide[BMF_PLATFORM_PATH_MAX];
    if (!win32_path(path, wide, BMF_PLATFORM_PATH_MAX)) return BMF_ERR_RANGE;
    HANDLE file = win32_open_read(wide);
    if (file == WIN32_INVALID_HANDLE_VALUE) return win32_last_file_error();
    i64 size = 0;
    if (!GetFileSizeEx(file, &size)) { CloseHandle(file); return BMF_ERR_IO; }
    if (size == 0) { CloseHandle(file); return BMF_OK; }   // Windows cannot map an empty file
    HANDLE mapping = CreateFileMappingW(file, 0, WIN32_PAGE_READONLY, 0, 0, 0);
    CloseHandle(file);   // the mapping keeps the file referenced
    if (!mapping) return BMF_ERR_IO;
    void* view = MapViewOfFile(mapping, WIN32_FILE_MAP_READ, 0, 0, 0);
    CloseHandle(mapping);   // the view keeps the mapping alive
    if (!view) return BMF_ERR_IO;
    out->data = (const u8*)view;
    out->size = size;
    return BMF_OK;
}

void bmf_platform_file_unmap(bmf_file_map* map) {
    if (map->data) UnmapViewOfFile(map->data);
    map->data = 0;
    map->size = 0;
}

bmf_err bmf_platform_file_write_atomic(const char* path, const void* data, i64 size) {
    const void* parts[1] = { data };
    i64 sizes[1] = { size };
    return bmf_platform_file_write_atomic_parts(path, parts, sizes, 1);
}

bmf_err bmf_platform_file_write_atomic_parts(const char* path, const void* const* parts, const i64* sizes, i32 count) {
    WCHAR target[BMF_PLATFORM_PATH_MAX];
    if (!win32_path(path, target, BMF_PLATFORM_PATH_MAX - 40)) return BMF_ERR_RANGE;   // room for the temp suffix
    // Temp name in the same folder (a rename across volumes is not atomic): "<path>.<pid>-<tid>.tmp".
    WCHAR temp[BMF_PLATFORM_PATH_MAX];
    i32 n = 0;
    while (target[n]) { temp[n] = target[n]; n++; }
    char suffix[40];
    i32 suffix_len = bmf_fmt_buf(suffix, (i32)sizeof suffix, ".%u-%u.tmp", GetCurrentProcessId(), GetCurrentThreadId());
    for (i32 i = 0; i < suffix_len; i++) temp[n++] = (WCHAR)suffix[i];
    temp[n] = 0;

    HANDLE file = CreateFileW(temp, WIN32_GENERIC_WRITE, 0, 0, WIN32_CREATE_ALWAYS, WIN32_FILE_ATTRIBUTE_NORMAL, 0);
    if (file == WIN32_INVALID_HANDLE_VALUE) return win32_last_file_error();
    b32 complete = 1;
    for (i32 p = 0; p < count && complete; p++) {
        const u8* bytes = (const u8*)parts[p];
        i64 done = 0;
        while (done < sizes[p]) {
            i64 chunk = sizes[p] - done < (1 << 30) ? sizes[p] - done : (1 << 30);
            u32 written = 0;
            if (!WriteFile(file, bytes + done, (u32)chunk, &written, 0) || written == 0) break;
            done += written;
        }
        complete = done == sizes[p];
    }
    CloseHandle(file);
    if (!complete) { DeleteFileW(temp); return BMF_ERR_IO; }
    // Virus scanners and the search indexer open freshly written files for a moment without delete
    // sharing, which makes the rename fail with ACCESS_DENIED or SHARING_VIOLATION (seen in about
    // 1 of 15 test runs). Retry with backoff: 0+1+2+4+8+16+32 = 63 ms at most before giving up.
    u32 backoff_ms = 0;
    for (;;) {
        if (MoveFileExW(temp, target, WIN32_MOVEFILE_REPLACE_EXISTING)) return BMF_OK;
        u32 code = GetLastError();
        b32 transient = code == WIN32_ERROR_ACCESS_DENIED || code == WIN32_ERROR_SHARING_VIOLATION;
        if (!transient || backoff_ms > 32) {
            DeleteFileW(temp);
            return code == WIN32_ERROR_FILE_NOT_FOUND || code == WIN32_ERROR_PATH_NOT_FOUND ? BMF_ERR_NOT_FOUND : BMF_ERR_IO;
        }
        Sleep(backoff_ms);   // kernel32 Sleep: callable from any thread, unlike bmf_platform_sleep_ns
        backoff_ms = backoff_ms ? backoff_ms * 2 : 1;
    }
}

bmf_err bmf_platform_dir_create(const char* path) {
    WCHAR wide[BMF_PLATFORM_PATH_MAX];
    if (!win32_path(path, wide, BMF_PLATFORM_PATH_MAX)) return BMF_ERR_RANGE;
    if (CreateDirectoryW(wide, 0) || GetLastError() == WIN32_ERROR_ALREADY_EXISTS) return BMF_OK;
    return win32_last_file_error();
}

i64 bmf_platform_utc_ns(void) {
    u64 file_time = 0;   // 100 ns units since 1601-01-01 UTC
    GetSystemTimeAsFileTime(&file_time);
    return (i64)(file_time - 116444736000000000ull) * 100;
}

bmf_err bmf_platform_file_delete(const char* path) {
    WCHAR wide[BMF_PLATFORM_PATH_MAX];
    if (!win32_path(path, wide, BMF_PLATFORM_PATH_MAX)) return BMF_ERR_RANGE;
    return DeleteFileW(wide) ? BMF_OK : win32_last_file_error();
}

bmf_err bmf_platform_file_info(const char* path, bmf_file_info* out) {
    out->size = -1;
    out->modified = 0;
    WCHAR wide[BMF_PLATFORM_PATH_MAX];
    if (!win32_path(path, wide, BMF_PLATFORM_PATH_MAX)) return BMF_ERR_RANGE;
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(wide, WIN32_GetFileExInfoStandard, &data)) return win32_last_file_error();
    out->size = (i64)(((u64)data.nFileSizeHigh << 32) | data.nFileSizeLow);
    out->modified = ((u64)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
    return BMF_OK;
}

void bmf_platform_file_watch_init(bmf_file_watch* watch, const char* path, i64 now_ns) {
    watch->path = path;
    bmf_platform_file_info(path, &watch->last);   // a missing file reads as size -1, modified 0
    watch->next_check_ns = now_ns + (i64)BMF_FILE_WATCH_INTERVAL_MS * 1000000;
}

b32 bmf_platform_file_watch_poll(bmf_file_watch* watch, i64 now_ns) {
    if (now_ns < watch->next_check_ns) return 0;
    watch->next_check_ns = now_ns + (i64)BMF_FILE_WATCH_INTERVAL_MS * 1000000;
    bmf_file_info info;
    bmf_platform_file_info(watch->path, &info);
    if (info.size == watch->last.size && info.modified == watch->last.modified) return 0;
    watch->last = info;
    return 1;
}

// ---------------------------------------------------------------- dynamic libraries
bmf_library bmf_platform_library_load(const char* path) {
    WCHAR wide[BMF_PLATFORM_PATH_MAX];
    if (!win32_path(path, wide, BMF_PLATFORM_PATH_MAX)) return 0;
    HMODULE module = LoadLibraryW(wide);
    if (module) win32_crash_module_add(module);   // faults in its code are crashes of ours
    return (bmf_library)module;
}

void* bmf_platform_library_symbol(bmf_library library, const char* name) {
    return GetProcAddress((HMODULE)library, name);
}

void bmf_platform_library_unload(bmf_library library) {
    if (!library) return;
    win32_crash_module_remove(library);
    FreeLibrary((HMODULE)library);
}

// ---------------------------------------------------------------- futex and semaphore
void bmf_platform_futex_wait(i32* address, i32 expected) {
    WaitOnAddress(address, &expected, sizeof expected, (u32)WIN32_INFINITE);
}

void bmf_platform_futex_wake_one(i32* address) { WakeByAddressSingle(address); }
void bmf_platform_futex_wake_all(i32* address) { WakeByAddressAll(address); }

// Lost-wakeup argument: a waiter increments `waiters` before WaitOnAddress re-reads `count`; a
// signaler increments `count` before reading `waiters`. Both are sequentially consistent, so
// either the waiter sees the new count (WaitOnAddress returns at once) or the signaler sees the
// waiter and wakes it.
void bmf_semaphore_signal(bmf_semaphore* semaphore, i32 count) {
    BMF_ASSERT(count >= 1);
    bmf_atomic_add_i32(&semaphore->count, count);
    if (__atomic_load_n(&semaphore->waiters, __ATOMIC_SEQ_CST) == 0) return;
    if (count == 1) bmf_platform_futex_wake_one(&semaphore->count);
    else bmf_platform_futex_wake_all(&semaphore->count);
}

void bmf_semaphore_wait(bmf_semaphore* semaphore) {
    for (;;) {
        i32 available = bmf_atomic_load_i32(&semaphore->count);
        while (available > 0) {
            if (bmf_atomic_cas_i32(&semaphore->count, available, available - 1)) return;
            available = bmf_atomic_load_i32(&semaphore->count);
        }
        bmf_atomic_add_i32(&semaphore->waiters, 1);
        bmf_platform_futex_wait(&semaphore->count, 0);
        bmf_atomic_add_i32(&semaphore->waiters, -1);
    }
}

// ---------------------------------------------------------------- threads
// A starting thread gets its function and argument through a slot; the new thread copies them
// and frees the slot before running user code.
static struct {
    i32 in_use;   // 0 free, 1 claimed (atomic)
    bmf_thread_fn fn;
    void* user;
} win32_thread_slots[BMF_THREAD_MAX_STARTING];

static u32 win32_thread_main(void* parameter) {
    bmf_fpu_setup();   // D-37 rule 10: before any user code
    u32 guarantee = WIN32_CRASH_STACK_GUARANTEE;   // room to report a stack overflow on this thread
    SetThreadStackGuarantee(&guarantee);
    i32 slot = (i32)(i64)parameter;
    bmf_thread_fn fn = win32_thread_slots[slot].fn;
    void* user = win32_thread_slots[slot].user;
    bmf_atomic_store_i32(&win32_thread_slots[slot].in_use, 0);
    fn(user);
    return 0;
}

bmf_thread bmf_platform_thread_start(bmf_thread_fn fn, void* user, i64 stack_size, const char* name) {
    BMF_ASSERT(fn && stack_size >= 64 * 1024);
    i32 slot = -1;
    for (i32 i = 0; i < BMF_THREAD_MAX_STARTING && slot < 0; i++)
        if (bmf_atomic_cas_i32(&win32_thread_slots[i].in_use, 0, 1)) slot = i;
    if (slot < 0) bmf_panic("bmf_platform_thread_start: more than 64 threads starting at once");
    win32_thread_slots[slot].fn = fn;
    win32_thread_slots[slot].user = user;
    // The slot fields are published by CreateThread (a full barrier) before the thread runs.
    HANDLE handle = CreateThread(0, (size_t)stack_size, win32_thread_main, (void*)(i64)slot,
                                 WIN32_STACK_SIZE_PARAM_IS_A_RESERVATION, 0);
    if (!handle) bmf_panic("CreateThread failed");
    if (name) {
        WCHAR wide[128];
        i32 need = bmf_utf8_to_utf16(bmf_str_from_cstr(name), wide, 127);
        wide[need < 127 ? need : 127] = 0;
        SetThreadDescription(handle, wide);
    }
    bmf_thread thread = { handle };
    return thread;
}

void bmf_platform_thread_join(bmf_thread* thread) {
    BMF_ASSERT(thread->handle);
    WaitForSingleObject(thread->handle, (u32)WIN32_INFINITE);
    CloseHandle(thread->handle);
    thread->handle = 0;
}

i32 bmf_platform_cpu_count(void) {
    return (i32)GetActiveProcessorCount(WIN32_ALL_PROCESSOR_GROUPS);
}

b32 bmf_platform_is_main_thread(void) { return GetCurrentThreadId() == win32_core.main_thread_id; }
