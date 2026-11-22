// base_memory.h: the engine side of memory (SPEC §5.2, D-16, D-39): the virtual-memory platform
// hooks, arena setup and the process memory layout. Arenas, scratch arenas, pools and
// bmf_memory_desc are public: see bmf.h.
#ifndef BMF_BASE_MEMORY_H
#define BMF_BASE_MEMORY_H

#include "../bmf.h"

// ---------------------------------------------------------------- platform hooks (src/platform/*)
// Virtual memory. Sizes and addresses are multiples of BMF_PAGE_GRANULE.
void* bmf_platform_reserve(i64 size);              // address space only, no access; 0 on failure
b32   bmf_platform_commit(void* base, i64 size);   // make readable/writable, zero-filled
void  bmf_platform_decommit(void* base, i64 size); // give pages back, keep the reservation

enum { BMF_PAGE_GRANULE = 64 * 1024 };   // commit/reserve granule (Windows allocation granularity, wasm page)

// Initializes an arena over `reserved` bytes at `base` (from bmf_platform_reserve or a
// bmf_memory layout). `reserved` is a multiple of BMF_PAGE_GRANULE. Commits nothing. O(1).
void bmf_arena_init(bmf_arena* a, const char* name, u8* base, i64 reserved);

// ---------------------------------------------------------------- process memory layout (D-16, D-39)
typedef struct bmf_memory {
    bmf_arena permanent;
    bmf_arena game_state;
    bmf_arena level;
    bmf_arena frame;
    bmf_arena scratch[2];
    u8* range_base;    // the one reserved range that holds every arena and its guard granule
    i64 range_size;
} bmf_memory;

// Reserves one range for all arenas (each rounded up to BMF_PAGE_GRANULE, each followed by a
// guard granule), initializes them, and makes scratch[0..1] the main thread's scratch pair.
// `state_size` sizes the game_state arena. Returns 0 if the reservation fails.
b32 bmf_memory_init(bmf_memory* m, const bmf_memory_desc* desc, i64 state_size);

#endif // BMF_BASE_MEMORY_H
