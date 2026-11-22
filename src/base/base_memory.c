// base_memory.c: arenas, scratch, memory layout, pools (contracts: bmf.h and base_memory.h).
#include "base_memory.h"

#if BMF_SANITIZE
void __asan_poison_memory_region(void const volatile* addr, size_t size);
void __asan_unpoison_memory_region(void const volatile* addr, size_t size);
#endif

static inline i64 memory_round_up(i64 v, i64 granule) { return (v + granule - 1) & ~(granule - 1); }

// ---------------------------------------------------------------- arena
void bmf_arena_init(bmf_arena* a, const char* name, u8* base, i64 reserved) {
    BMF_ASSERT(base && reserved > 0 && (reserved % BMF_PAGE_GRANULE) == 0);
    a->base = base;
    a->reserved = reserved;
    a->committed = 0;
    a->used = 0;
    a->high_water = 0;
    a->name = name;
}

void* bmf_arena_push_aligned(bmf_arena* a, i64 size, i64 align) {
    BMF_ASSERT(size >= 0);
    BMF_ASSERT(align >= 1 && align <= 4096 && (align & (align - 1)) == 0);
    i64 start = (a->used + align - 1) & ~(align - 1);
    i64 end = start + size;
    if (end > a->reserved) bmf_panic("arena reserve exhausted: raise this arena's size in bmf_game_desc.memory (D-39)");
    if (end > a->committed) {
        i64 new_committed = memory_round_up(end, BMF_PAGE_GRANULE);
        if (!bmf_platform_commit(a->base + a->committed, new_committed - a->committed)) bmf_panic("arena commit failed: out of memory");
#if BMF_SANITIZE
        __asan_poison_memory_region(a->base + a->committed, (size_t)(new_committed - a->committed));
#endif
        a->committed = new_committed;
    }
    a->used = end;
    if (end > a->high_water) a->high_water = end;
    u8* p = a->base + start;
#if BMF_SANITIZE
    __asan_unpoison_memory_region(p, (size_t)size);
#endif
#if BMF_DEBUG
    bmf_memset(p, 0xCD, size);
#endif
    return p;
}

void* bmf_arena_push(bmf_arena* a, i64 size) { return bmf_arena_push_aligned(a, size, 16); }

void* bmf_arena_push_zero(bmf_arena* a, i64 size, i64 align) {
    void* p = bmf_arena_push_aligned(a, size, align);
    bmf_memset(p, 0, size);
    return p;
}

i64 bmf_arena_mark(const bmf_arena* a) { return a->used; }

void bmf_arena_pop_to(bmf_arena* a, i64 mark) {
    BMF_ASSERT(mark >= 0 && mark <= a->used);
#if BMF_DEBUG
    bmf_memset(a->base + mark, 0xDD, a->used - mark);
#endif
#if BMF_SANITIZE
    __asan_poison_memory_region(a->base + mark, (size_t)(a->used - mark));
#endif
    a->used = mark;
}

void bmf_arena_reset(bmf_arena* a) { bmf_arena_pop_to(a, 0); }

// ---------------------------------------------------------------- scratch
static bmf_arena* memory_scratch_pair[2];   // main thread's pair (set by bmf_memory_init)

bmf_scratch bmf_scratch_begin(const bmf_arena* conflict) {
    BMF_ASSERT(memory_scratch_pair[0] && memory_scratch_pair[1]);
    bmf_arena* a = memory_scratch_pair[0] == conflict ? memory_scratch_pair[1] : memory_scratch_pair[0];
    bmf_scratch s = { a, a->used };
    return s;
}

void bmf_scratch_end(bmf_scratch s) { bmf_arena_pop_to(s.arena, s.mark); }

// ---------------------------------------------------------------- process memory layout
b32 bmf_memory_init(bmf_memory* m, const bmf_memory_desc* desc, i64 state_size) {
    i64 sizes[6] = {
        desc && desc->permanent ? desc->permanent : 64ll << 20,
        state_size > 0 ? state_size : 1,
        desc && desc->level ? desc->level : 64ll << 20,
        desc && desc->frame ? desc->frame : 32ll << 20,
        desc && desc->scratch ? desc->scratch : 16ll << 20,
        desc && desc->scratch ? desc->scratch : 16ll << 20,
    };
    i64 total = 0;
    for (i32 i = 0; i < 6; i++) {
        sizes[i] = memory_round_up(sizes[i], BMF_PAGE_GRANULE);
        total += sizes[i] + BMF_PAGE_GRANULE;   // arena + guard granule
    }
    u8* base = (u8*)bmf_platform_reserve(total);
    if (!base) return 0;
    bmf_arena* arenas[6] = { &m->permanent, &m->game_state, &m->level, &m->frame, &m->scratch[0], &m->scratch[1] };
    const char* names[6] = { "permanent", "game_state", "level", "frame", "scratch0", "scratch1" };
    u8* at = base;
    for (i32 i = 0; i < 6; i++) {
        bmf_arena_init(arenas[i], names[i], at, sizes[i]);
        at += sizes[i] + BMF_PAGE_GRANULE;
    }
    m->range_base = base;
    m->range_size = total;
    memory_scratch_pair[0] = &m->scratch[0];
    memory_scratch_pair[1] = &m->scratch[1];
    return 1;
}

// ---------------------------------------------------------------- pool
static const u32 POOL_ALIVE = 1u << 31;
static const u32 POOL_GENERATION_MASK = (1u << BMF_HANDLE_GENERATION_BITS) - 1u;

static inline bmf_handle pool_make_handle(i32 index, u32 generation) {
    return ((u32)index << BMF_HANDLE_GENERATION_BITS) | (generation & POOL_GENERATION_MASK);
}

void bmf_pool_init(bmf_pool* p, bmf_pool_slot* slots, i32 capacity) {
    BMF_ASSERT(capacity >= 1 && capacity <= BMF_POOL_MAX_CAPACITY);
    p->capacity = capacity;
    p->live_count = 0;
    p->free_head = 0;
    p->high_water = 0;
    for (i32 i = 0; i < capacity; i++) {
        slots[i].generation = 1;   // generation 0 is never used, so no handle is 0
        slots[i].link = i + 1 < capacity ? i + 1 : -1;
        slots[i].dense = -1;
    }
}

bmf_handle bmf_pool_alloc(bmf_pool* p, bmf_pool_slot* slots) {
    if (p->free_head < 0) return 0;
    i32 index = p->free_head;
    bmf_pool_slot* s = &slots[index];
    p->free_head = s->link;
    s->generation |= POOL_ALIVE;
    s->link = p->live_count;
    slots[p->live_count].dense = index;
    p->live_count++;
    if (p->live_count > p->high_water) p->high_water = p->live_count;
    return pool_make_handle(index, s->generation);
}

b32 bmf_pool_valid(const bmf_pool* p, const bmf_pool_slot* slots, bmf_handle h) {
    i32 index = (i32)(h >> BMF_HANDLE_GENERATION_BITS);
    if (h == 0 || index >= p->capacity) return 0;
    u32 g = slots[index].generation;
    return (g & POOL_ALIVE) && (g & POOL_GENERATION_MASK) == (h & POOL_GENERATION_MASK);
}

i32 bmf_pool_index(const bmf_pool* p, const bmf_pool_slot* slots, bmf_handle h) {
    BMF_ASSERT(bmf_pool_valid(p, slots, h));
    return (i32)(h >> BMF_HANDLE_GENERATION_BITS);
}

void bmf_pool_free(bmf_pool* p, bmf_pool_slot* slots, bmf_handle h) {
    BMF_ASSERT(bmf_pool_valid(p, slots, h));
    i32 index = (i32)(h >> BMF_HANDLE_GENERATION_BITS);
    bmf_pool_slot* s = &slots[index];
    // Swap-remove from the dense list: the last live item takes this item's position.
    i32 position = s->link;
    i32 last = p->live_count - 1;
    i32 moved = slots[last].dense;
    slots[position].dense = moved;
    slots[moved].link = position;
    slots[last].dense = -1;
    p->live_count = last;
    // Advance the generation (1..4095, skipping 0) and push the slot onto the free list.
    u32 g = (s->generation & POOL_GENERATION_MASK) + 1u;
    if (g > POOL_GENERATION_MASK) g = 1;
    s->generation = g;
    s->link = p->free_head;
    p->free_head = index;
}

bmf_handle bmf_pool_handle_of(const bmf_pool_slot* slots, i32 slot) {
    BMF_ASSERT(slots[slot].generation & POOL_ALIVE);
    return pool_make_handle(slot, slots[slot].generation);
}
