// win32_crash.c: the fatal-error hook (bmf.h) and crash reports (task 1A-35). Contracts in
// platform.h ("Crash reports"). Everything here runs at crash time, possibly on a broken stack
// (64 KiB guaranteed) or with corrupt memory around it: static buffers only, no arena, no
// buffered stdio (stderr is written with WriteFile).
//
// Exceptions are caught by a vectored handler, not SetUnhandledExceptionFilter: D-09 builds with
// -fno-asynchronous-unwind-tables, so our frames have no unwind data, the system cannot walk the
// stack to its top-level handler, and the process ends without the filter ever running. A
// vectored handler runs before any stack walk. It sees first-chance exceptions too, so it takes
// only fault codes raised by code in our own modules (the exe and libraries loaded through
// bmf_platform_library_load); everything else (a driver probing memory under its own __try) goes
// on to the normal search.

static struct {
    i32 owner;                      // id of the thread writing the report, 0 = no crash yet
    i32 printed;                    // 1 once the report line is out
    bmf_platform_crash_hook hook;
} win32_crash;

static char win32_crash_line[16 * 1024];

// Address ranges of our modules: [0] the exe, then loaded libraries (game DLLs).
enum { WIN32_CRASH_MAX_MODULES = 16 };
static struct { u64 base, end; } win32_crash_modules[WIN32_CRASH_MAX_MODULES];

// The image range from the module's own PE headers (base + SizeOfImage).
static void win32_crash_module_range(void* module, u64* base, u64* end) {
    const u8* image = (const u8*)module;
    i32 nt_offset;
    u32 size_of_image;
    bmf_memcpy(&nt_offset, image + 0x3C, 4);                 // IMAGE_DOS_HEADER.e_lfanew
    bmf_memcpy(&size_of_image, image + nt_offset + 80, 4);   // signature 4 + IMAGE_FILE_HEADER 20 + OptionalHeader64.SizeOfImage at 56
    *base = (u64)image;
    *end = (u64)image + size_of_image;
}

static void win32_crash_module_add(void* module) {
    for (i32 i = 1; i < WIN32_CRASH_MAX_MODULES; i++) {
        if (win32_crash_modules[i].base) continue;
        win32_crash_module_range(module, &win32_crash_modules[i].base, &win32_crash_modules[i].end);
        return;
    }   // a full table: faults in that library go unreported (16 libraries are never loaded at once)
}

static void win32_crash_module_remove(void* module) {
    for (i32 i = 1; i < WIN32_CRASH_MAX_MODULES; i++)
        if (win32_crash_modules[i].base == (u64)module) win32_crash_modules[i].base = win32_crash_modules[i].end = 0;
}

static b32 win32_crash_is_ours(u64 address) {
    for (i32 i = 0; i < WIN32_CRASH_MAX_MODULES; i++)
        if (address >= win32_crash_modules[i].base && address < win32_crash_modules[i].end) return 1;
    return 0;
}

void bmf_platform_set_crash_hook(bmf_platform_crash_hook hook) { win32_crash.hook = hook; }

// Appends `text` (JSON-escaped when `escape`) to the line; keeps room for the closing "}\n".
static i32 win32_crash_put(i32 len, const char* text, b32 escape) {
    i32 limit = (i32)sizeof win32_crash_line - 4;
    for (const char* c = text; c && *c && len < limit - 1; c++) {
        char ch = *c;
        if (escape && (ch == '"' || ch == '\\')) { win32_crash_line[len++] = '\\'; win32_crash_line[len++] = ch; }
        else if (escape && (u8)ch < 0x20) win32_crash_line[len++] = ' ';
        else win32_crash_line[len++] = ch;
    }
    return len;
}

static void win32_crash_write_stderr(const char* text, i32 length) {
    u32 written = 0;
    WriteFile(GetStdHandle((u32)WIN32_STD_ERROR_HANDLE), text, (u32)length, &written, 0);
}

// 1 when this thread now owns the crash report; 0 when it owns it already (a crash inside the
// report: the caller ends the process). A second crashing thread never returns: it waits for the
// owner to end the process.
static b32 win32_crash_enter(void) {
    i32 me = (i32)GetCurrentThreadId();
    if (bmf_atomic_cas_i32(&win32_crash.owner, 0, me)) return 1;
    if (bmf_atomic_load_i32(&win32_crash.owner) == me) {
        if (!bmf_atomic_load_i32(&win32_crash.printed)) {
            const char* fallback = "{\"event\":\"crash\",\"reason\":\"crash_in_crash_report\"}\n";
            win32_crash_write_stderr(fallback, bmf_cstr_len(fallback));
            bmf_atomic_store_i32(&win32_crash.printed, 1);
        }
        return 0;
    }
    for (;;) Sleep(1000);
}

// The report line: own keys, then the hook's (files of the crash dump), then "}\n".
static void win32_crash_report(const char* file, i32 line, const char* kind, const char* what, b32 has_code, u32 code, u64 address) {
    char number[128];
    i32 len = 0;
    len = win32_crash_put(len, "{\"event\":\"crash\",\"reason\":\"", 0);
    len = win32_crash_put(len, kind, 1);
    len = win32_crash_put(len, "\",\"detail\":\"", 0);
    len = win32_crash_put(len, what, 1);
    len = win32_crash_put(len, "\",\"file\":\"", 0);
    len = win32_crash_put(len, file, 1);
    bmf_fmt_buf(number, (i32)sizeof number, "\",\"line\":%i", line < 0 ? 0 : line);
    len = win32_crash_put(len, number, 0);
    if (has_code) {
        bmf_fmt_buf(number, (i32)sizeof number, ",\"code\":\"0x%08x\",\"address\":\"0x%016lx\"", code, address);
        len = win32_crash_put(len, number, 0);
    }
    if (win32_crash.hook) {
        i32 room = (i32)sizeof win32_crash_line - 4 - len;
        i32 added = room > 0 ? win32_crash.hook(win32_crash_line + len, room) : 0;
        if (added > 0 && added <= room) len += added;
    }
    win32_crash_line[len++] = '}';
    win32_crash_line[len++] = '\n';
    win32_crash_write_stderr(win32_crash_line, len);
    bmf_atomic_store_i32(&win32_crash.printed, 1);
}

void bmf_platform_fatal(const char* file, i32 line, const char* kind, const char* what) {
    if (!win32_crash_enter()) return;   // inside the report already: the caller's trap ends the process
    win32_crash_report(file, line, kind, what, 0, 0, 0);
    // The caller traps next; the handler below sees this thread as the owner and ends the process.
}

static i32 win32_crash_handler(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* r = info->ExceptionRecord;
    u32 code = r->ExceptionCode;
    b32 fault = code == (u32)WIN32_EXCEPTION_ACCESS_VIOLATION || code == (u32)WIN32_EXCEPTION_INT_DIVIDE_BY_ZERO ||
                code == (u32)WIN32_EXCEPTION_ILLEGAL_INSTRUCTION || code == (u32)WIN32_EXCEPTION_STACK_OVERFLOW ||
                code == (u32)WIN32_EXCEPTION_BREAKPOINT || code == 0xC0000095u /* integer overflow */ ||
                code == 0xC0000096u /* privileged instruction */ || code == 0xC0000006u /* in-page error */;
    if (!fault || !win32_crash_is_ours((u64)r->ExceptionAddress)) return WIN32_EXCEPTION_CONTINUE_SEARCH;
    if (!win32_crash_enter()) TerminateProcess(GetCurrentProcess(), code);   // the trap after an assert/panic, or a crash in the report
    static char detail[128];
    if (code == (u32)WIN32_EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
        u64 operation = r->ExceptionInformation[0];
        bmf_fmt_buf(detail, (i32)sizeof detail, "access_violation %s 0x%016lx",
                    operation == 0 ? "reading" : operation == 8 ? "executing" : "writing", r->ExceptionInformation[1]);
    } else {
        const char* name = code == (u32)WIN32_EXCEPTION_ACCESS_VIOLATION ? "access_violation"
                         : code == (u32)WIN32_EXCEPTION_INT_DIVIDE_BY_ZERO ? "int_divide_by_zero"
                         : code == (u32)WIN32_EXCEPTION_ILLEGAL_INSTRUCTION ? "illegal_instruction (UBSan trap or __builtin_trap)"
                         : code == (u32)WIN32_EXCEPTION_STACK_OVERFLOW ? "stack_overflow"
                         : code == (u32)WIN32_EXCEPTION_BREAKPOINT ? "breakpoint" : "exception";
        bmf_fmt_buf(detail, (i32)sizeof detail, "%s", name);
    }
    win32_crash_report("?", 0, "exception", detail, 1, code, (u64)r->ExceptionAddress);
    TerminateProcess(GetCurrentProcess(), code);   // exit code: the exception code
    return WIN32_EXCEPTION_CONTINUE_SEARCH;
}

static void win32_crash_install(void) {
    win32_crash_module_range(GetModuleHandleW(0), &win32_crash_modules[0].base, &win32_crash_modules[0].end);
    AddVectoredExceptionHandler(1, win32_crash_handler);
    u32 guarantee = WIN32_CRASH_STACK_GUARANTEE;
    SetThreadStackGuarantee(&guarantee);
}
