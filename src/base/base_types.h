// base_types.h: fixed-width types, platform detection, asserts (SPEC §5.1, D-15).
// Included first by every unity root (runtime, game DLL, tests, build.c).
#ifndef BMF_BASE_TYPES_H
#define BMF_BASE_TYPES_H

#include <stddef.h>   // offsetof, size_t (compiler-provided freestanding header, D-02)

// ---------------------------------------------------------------- types (SPEC §5.1)
typedef signed char        i8;
typedef short              i16;
typedef int                i32;
typedef long long          i64;
typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned           u32;
typedef unsigned long long u64;
typedef float              f32;
typedef double             f64;
typedef i32                b32;

// Result of a function that can fail (SPEC §16). Results go through out-params. Closed enum:
// modules add values here, never their own error types.
typedef enum bmf_err {
    BMF_OK = 0,
    BMF_ERR_INVALID = 1,     // malformed input (parse error, bad argument value from outside)
    BMF_ERR_RANGE = 2,       // well-formed but out of the representable range
    BMF_ERR_NOT_FOUND = 3,   // a file or path does not exist
    BMF_ERR_IO = 4,          // the OS refused or failed a file operation (access denied, disk full, locked)
    BMF_ERR_UNSUPPORTED = 5, // the active backend or device cannot do this (e.g. a GPU readback on the null backend)
    BMF_ERR_PENDING = 6,     // an asynchronous operation has not finished yet; call again later
} bmf_err;

#define BMF_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)

BMF_STATIC_ASSERT(sizeof(i8) == 1 && sizeof(u8) == 1, "8-bit types");
BMF_STATIC_ASSERT(sizeof(i16) == 2 && sizeof(u16) == 2, "16-bit types");
BMF_STATIC_ASSERT(sizeof(i32) == 4 && sizeof(u32) == 4, "32-bit types");
BMF_STATIC_ASSERT(sizeof(i64) == 8 && sizeof(u64) == 8, "64-bit types");
BMF_STATIC_ASSERT(sizeof(f32) == 4 && sizeof(f64) == 8, "float types");

// ---------------------------------------------------------------- platform and architecture
#if defined(_WIN32)
#define BMF_OS_WINDOWS 1
#elif defined(__wasm__)
#define BMF_OS_WEB 1
#else
#error "bmf: unsupported OS"
#endif
#ifndef BMF_OS_WINDOWS
#define BMF_OS_WINDOWS 0
#endif
#ifndef BMF_OS_WEB
#define BMF_OS_WEB 0
#endif

#if defined(__x86_64__)
#define BMF_ARCH_X64 1
#elif defined(__wasm32__)
#define BMF_ARCH_WASM32 1
#elif defined(__aarch64__)
#define BMF_ARCH_ARM64 1
#else
#error "bmf: unsupported architecture"
#endif
#ifndef BMF_ARCH_X64
#define BMF_ARCH_X64 0
#endif
#ifndef BMF_ARCH_WASM32
#define BMF_ARCH_WASM32 0
#endif
#ifndef BMF_ARCH_ARM64
#define BMF_ARCH_ARM64 0
#endif

#ifndef BMF_DEBUG
#define BMF_DEBUG 0
#endif
#ifndef BMF_SANITIZE
#define BMF_SANITIZE 0
#endif

#if BMF_OS_WINDOWS
#define BMF_EXPORT __declspec(dllexport)
#else
#define BMF_EXPORT __attribute__((visibility("default")))
#endif

// ---------------------------------------------------------------- fatal errors and asserts
// bmf_platform_fatal is implemented once per binary by its platform layer (runtime, tests,
// build.c). It reports `what` with file and line (from 1A-35 on: writes the crash dump) and
// returns; the caller then traps. It must only use pre-allocated memory.
void bmf_platform_fatal(const char* file, i32 line, const char* kind, const char* what);

// Reports a failed assert and stops the process. Never returns.
static inline __attribute__((noreturn, noinline, cold)) void bmf_assert_fail(const char* file, i32 line, const char* expr) {
    bmf_platform_fatal(file, line, "assert", expr);
    __builtin_trap();
}

// Reports an unrecoverable runtime error (corrupt data, out of memory: not programmer errors,
// which are BMF_ASSERTs) and stops the process, in debug and release. Never returns.
// The crash report carries the return address instead of a file and line.
static inline __attribute__((noreturn, noinline, cold)) void bmf_panic(const char* message) {
    bmf_platform_fatal("?", 0, "panic", message);
    __builtin_trap();
}

// BMF_ASSERT checks programmer errors in debug builds and compiles to nothing in release.
#if BMF_DEBUG
#define BMF_ASSERT(cond) ((cond) ? (void)0 : bmf_assert_fail(__FILE__, __LINE__, #cond))
#else
#define BMF_ASSERT(cond) ((void)0)
#endif

#endif // BMF_BASE_TYPES_H
