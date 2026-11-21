// base_memory.h: arenas, scratch arenas, pools and the process memory layout (SPEC §5.2, D-16, D-39).
#ifndef BMF_BASE_MEMORY_H
#define BMF_BASE_MEMORY_H

#include "base_types.h"

// ---------------------------------------------------------------- platform hooks (src/platform/*)
// Virtual memory. Sizes and addresses are multiples of BMF_PAGE_GRANULE.
void* bmf_platform_reserve(i64 size);              // address space only, no access; 0 on failure
b32   bmf_platform_commit(void* base, i64 size);   // make readable/writable, zero-filled
void  bmf_platform_decommit(void* base, i64 size); // give pages back, keep the reservation

enum { BMF_PAGE_GRANULE = 64 * 1024 };   // commit/reserve granule (Windows allocation granularity, wasm page)

// ---------------------------------------------------------------- arena
// Linear allocator over a reserved address range. Memory is committed on demand in 64 KiB
// granules and stays committed after pop/reset. One never-committed guard granule follows the
// reserved range, so running off the end faults instead of corrupting the next arena.
// Debug builds fill pushed bytes with 0xCD and popped/reset bytes with 0xDD.
typedef struct bmf_arena {
    u8* base;
    i64 reserved;     // usable bytes (guard granule not included)
    i64 committed;
    i64 used;
    i64 high_water;   // max `used` ever reached (reported by the `mem` protocol command)
    const char* name;
} bmf_arena;

// Initializes an arena over `reserved` bytes at `base` (from bmf_platform_reserve or a
// bmf_memory layout). `reserved` is a multiple of BMF_PAGE_GRANULE. Commits nothing. O(1).
void bmf_arena_init(bmf_arena* a, const char* name, u8* base, i64 reserved);

// Returns `size` bytes aligned to `align` (power of two, 1..4096). Contents are undefined
// (0xCD in debug); use bmf_arena_push_zero for zeroed memory. Panics when the arena's reserve
// is exhausted (a memory budget error, D-39). O(1) plus commit cost on growth.
void* bmf_arena_push_aligned(bmf_arena* a, i64 size, i64 align);

// bmf_arena_push_aligned with 16-byte alignment (SPEC §5.2 default).
void* bmf_arena_push(bmf_arena* a, i64 size);

// Like bmf_arena_push_aligned, but zero-filled. O(size).
void* bmf_arena_push_zero(bmf_arena* a, i64 size, i64 align);

// Current position, for bmf_arena_pop_to. O(1).
i64 bmf_arena_mark(const bmf_arena* a);

// Frees everything pushed after `mark`. O(1) (O(popped bytes) in debug for the 0xDD fill).
void bmf_arena_pop_to(bmf_arena* a, i64 mark);

// Frees everything. Keeps memory committed.
void bmf_arena_reset(bmf_arena* a);

// ---------------------------------------------------------------- scratch arenas
// Two scratch arenas so a function can use scratch memory while its caller holds the other one:
// pass the arena your caller gave you as `conflict` and you get the other. Always pair
// bmf_scratch_begin with bmf_scratch_end.
// Limit: main thread only until the job system (1B-12) gives workers their own pair.
typedef struct bmf_scratch {
    bmf_arena* arena;
    i64 mark;
} bmf_scratch;

bmf_scratch bmf_scratch_begin(const bmf_arena* conflict);
void bmf_scratch_end(bmf_scratch s);

// ---------------------------------------------------------------- process memory layout (D-16, D-39)
typedef struct bmf_memory_desc {   // bytes; 0 = default
    i64 permanent;   // default 64 MiB
    i64 level;       // default 64 MiB
    i64 frame;       // default 32 MiB
    i64 scratch;     // each of the two; default 16 MiB
} bmf_memory_desc;

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

// ---------------------------------------------------------------- pool (pointer-free, D-21)
// Fixed-capacity slot allocator with generational handles. It allocates slot indices only: the
// caller keeps item data in its own arrays indexed by slot index. All state is in `bmf_pool` plus
// one caller-owned `bmf_pool_slot[capacity]` array, so a pool can live inside the game state block.
typedef u32 bmf_handle;   // index:20 | generation:12. 0 is never a valid handle.

enum { BMF_POOL_MAX_CAPACITY = (1 << 20) - 1, BMF_HANDLE_GENERATION_BITS = 12 };

typedef struct bmf_pool_slot {
    u32 generation;   // 1..4095 while in use or after first use; bit 31 = alive
    i32 link;         // alive: position in the dense list; free: next free slot (-1 = end)
    i32 dense;        // dense list element at this position: slot index of a live item
} bmf_pool_slot;

typedef struct bmf_pool {
    i32 capacity;
    i32 live_count;   // dense list length
    i32 free_head;    // first free slot, -1 when full
    i32 high_water;   // max live_count ever reached
} bmf_pool;

// Prepares a pool of `capacity` slots (1..BMF_POOL_MAX_CAPACITY). O(capacity).
void bmf_pool_init(bmf_pool* p, bmf_pool_slot* slots, i32 capacity);

// Takes a free slot. Returns its handle, or 0 when the pool is full. O(1).
bmf_handle bmf_pool_alloc(bmf_pool* p, bmf_pool_slot* slots);

// Releases a live handle (asserts it is valid). Its generation advances, so copies of the handle
// become stale. O(1). Changes the dense order (the last live item moves into the hole).
void bmf_pool_free(bmf_pool* p, bmf_pool_slot* slots, bmf_handle h);

// True if `h` refers to a live slot of this pool. O(1).
// Limit: 12-bit generations wrap after 4095 reuses of one slot, after which a very old stale
// handle to that slot compares valid again.
b32 bmf_pool_valid(const bmf_pool* p, const bmf_pool_slot* slots, bmf_handle h);

// Slot index of a live handle (asserts it is valid). O(1).
i32 bmf_pool_index(const bmf_pool* p, const bmf_pool_slot* slots, bmf_handle h);

// Dense iteration: for (i32 i = 0; i < p->live_count; i++) { i32 slot = bmf_pool_dense_slot(slots, i); ... }
// The order depends only on the sequence of alloc/free calls (deterministic). O(1).
static inline i32 bmf_pool_dense_slot(const bmf_pool_slot* slots, i32 i) { return slots[i].dense; }

// Handle of the live item in `slot`. O(1).
bmf_handle bmf_pool_handle_of(const bmf_pool_slot* slots, i32 slot);

#endif // BMF_BASE_MEMORY_H
