// Tests for src/base/base_memory.{h,c} (task 1A-03).

static bmf_arena memory_test_arena(i64 reserve) {
    bmf_arena a;
    u8* base = (u8*)bmf_platform_reserve(reserve + BMF_PAGE_GRANULE);   // + guard granule, never committed
    bmf_arena_init(&a, "test", base, reserve);
    return a;
}

static void test_memory_arena_alignment(bmf_test* t) {
    bmf_arena a = memory_test_arena(1 << 20);
    i32 bad = 0;
    u8* prev_end = a.base;
    for (i32 i = 0; i < 200; i++) {
        i64 align = 1ll << (i % 13);   // 1..4096
        i64 size = (i * 37) % 300;
        u8* p = (u8*)bmf_arena_push_aligned(&a, size, align);
        if (((u64)p & (u64)(align - 1)) != 0) bad++;
        if (p < prev_end) bad++;   // no overlap with the previous push
        prev_end = p + size;
    }
    u8* d = (u8*)bmf_arena_push(&a, 1);
    if (((u64)d & 15u) != 0) bad++;
    bmf_check_eq_i64(t, bad, 0, __FILE__, __LINE__, "misaligned or overlapping pushes");
    bmf_check(t, a.used == (i64)(d + 1 - a.base), __FILE__, __LINE__, "used matches last push end");
}

static void test_memory_arena_pop_reset_fill(bmf_test* t) {
    bmf_arena a = memory_test_arena(1 << 20);
    u8* first = (u8*)bmf_arena_push(&a, 100);
    i64 mark = bmf_arena_mark(&a);
    u8* second = (u8*)bmf_arena_push(&a, 1000);
#if BMF_DEBUG
    bmf_check_eq_u64(t, second[0], 0xCD, __FILE__, __LINE__, "debug fill on push");
    bmf_check_eq_u64(t, second[999], 0xCD, __FILE__, __LINE__, "debug fill on push (last byte)");
#endif
    bmf_memset(second, 7, 1000);
    bmf_arena_pop_to(&a, mark);
    bmf_check_eq_i64(t, a.used, mark, __FILE__, __LINE__, "used after pop");
#if BMF_DEBUG
    bmf_check_eq_u64(t, second[500], 0xDD, __FILE__, __LINE__, "debug fill on pop");
#endif
    u8* again = (u8*)bmf_arena_push(&a, 1000);
    bmf_check(t, again == second, __FILE__, __LINE__, "pop makes the space reusable");
    bmf_arena_reset(&a);
    bmf_check_eq_i64(t, a.used, 0, __FILE__, __LINE__, "used after reset");
    bmf_check(t, (u8*)bmf_arena_push(&a, 100) == first, __FILE__, __LINE__, "reset restarts at the base");
    u8* z = (u8*)bmf_arena_push_zero(&a, 4096, 64);
    i32 nonzero = 0;
    for (i32 i = 0; i < 4096; i++) nonzero += z[i] != 0;
    bmf_check_eq_i64(t, nonzero, 0, __FILE__, __LINE__, "push_zero is zeroed");
}

static void test_memory_arena_high_water_and_commit(bmf_test* t) {
    bmf_arena a = memory_test_arena(4 * BMF_PAGE_GRANULE);
    bmf_arena_push(&a, 10);
    bmf_check_eq_i64(t, a.committed, BMF_PAGE_GRANULE, __FILE__, __LINE__, "first push commits one granule");
    i64 mark = bmf_arena_mark(&a);
    u8* big = (u8*)bmf_arena_push_aligned(&a, 3 * BMF_PAGE_GRANULE, 1);
    bmf_memset(big, 1, 3 * BMF_PAGE_GRANULE);   // every committed byte is writable
    i64 peak = a.used;
    bmf_check_eq_i64(t, a.committed, 4 * BMF_PAGE_GRANULE, __FILE__, __LINE__, "commit grows to cover the push");
    bmf_arena_pop_to(&a, mark);
    bmf_arena_push(&a, 10);
    bmf_check_eq_i64(t, a.high_water, peak, __FILE__, __LINE__, "high-water mark keeps the peak");
    bmf_check_eq_i64(t, a.committed, 4 * BMF_PAGE_GRANULE, __FILE__, __LINE__, "memory stays committed after pop");
    // Exactly filling the reserve is allowed.
    bmf_arena_reset(&a);
    u8* all = (u8*)bmf_arena_push_aligned(&a, 4 * BMF_PAGE_GRANULE, 1);
    all[4 * BMF_PAGE_GRANULE - 1] = 9;
    bmf_check_eq_i64(t, a.used, 4 * BMF_PAGE_GRANULE, __FILE__, __LINE__, "full reserve usable");
}

// Expected crash: one byte past a full arena lands in the guard granule.
static void test_memory_arena_guard_faults(bmf_test* t) {
    bmf_arena a = memory_test_arena(BMF_PAGE_GRANULE);
    u8* p = (u8*)bmf_arena_push_aligned(&a, BMF_PAGE_GRANULE, 1);
    volatile u8* past = p + BMF_PAGE_GRANULE;
    *past = 1;
    bmf_check(t, 0, __FILE__, __LINE__, "write into the guard granule did not fault");
}

// Expected crash: pushing beyond the reserve panics with a budget message.
static void test_memory_arena_overflow_panics(bmf_test* t) {
    bmf_arena a = memory_test_arena(BMF_PAGE_GRANULE);
    bmf_arena_push(&a, BMF_PAGE_GRANULE);
    bmf_arena_push(&a, 1);
    bmf_check(t, 0, __FILE__, __LINE__, "overflow did not panic");
}

static void test_memory_layout_and_scratch(bmf_test* t) {
    bmf_memory m;
    bmf_memory_desc desc = { 1 << 20, 2 << 20, 1 << 20, 1 << 20 };
    bmf_check(t, bmf_memory_init(&m, &desc, 100000), __FILE__, __LINE__, "memory_init");
    bmf_arena* arenas[6] = { &m.permanent, &m.game_state, &m.level, &m.frame, &m.scratch[0], &m.scratch[1] };
    i32 bad = 0;
    for (i32 i = 0; i < 6; i++) {
        if (arenas[i]->reserved % BMF_PAGE_GRANULE) bad++;
        if (i > 0 && arenas[i]->base != arenas[i - 1]->base + arenas[i - 1]->reserved + BMF_PAGE_GRANULE) bad++;   // guard gap
    }
    bmf_check_eq_i64(t, bad, 0, __FILE__, __LINE__, "arenas are contiguous with one guard granule between");
    bmf_check_eq_i64(t, m.game_state.reserved, BMF_PAGE_GRANULE * 2, __FILE__, __LINE__, "state arena rounded up to granules");
    bmf_check_eq_i64(t, m.level.reserved, 2 << 20, __FILE__, __LINE__, "level size from desc");

    bmf_scratch outer = bmf_scratch_begin(0);
    bmf_arena_push(outer.arena, 64);
    bmf_scratch inner = bmf_scratch_begin(outer.arena);
    bmf_check(t, inner.arena != outer.arena, __FILE__, __LINE__, "conflicting scratch gives the other arena");
    bmf_arena_push(inner.arena, 128);
    bmf_scratch_end(inner);
    bmf_check_eq_i64(t, inner.arena->used, 0, __FILE__, __LINE__, "scratch_end restores the mark");
    bmf_scratch_end(outer);
    bmf_check_eq_i64(t, outer.arena->used, 0, __FILE__, __LINE__, "outer scratch_end restores the mark");
}

static void test_memory_pool_basic(bmf_test* t) {
    bmf_pool p;
    bmf_pool_slot slots[8];
    bmf_pool_init(&p, slots, 8);
    bmf_handle h[9];
    i32 zeros = 0;
    for (i32 i = 0; i < 8; i++) { h[i] = bmf_pool_alloc(&p, slots); zeros += h[i] == 0; }
    bmf_check_eq_i64(t, zeros, 0, __FILE__, __LINE__, "8 allocations in a pool of 8");
    h[8] = bmf_pool_alloc(&p, slots);
    bmf_check_eq_u64(t, h[8], 0, __FILE__, __LINE__, "full pool returns 0");
    bmf_check_eq_i64(t, p.live_count, 8, __FILE__, __LINE__, "live count");
    bmf_pool_free(&p, slots, h[3]);
    bmf_check(t, !bmf_pool_valid(&p, slots, h[3]), __FILE__, __LINE__, "freed handle is invalid");
    bmf_handle reused = bmf_pool_alloc(&p, slots);
    bmf_check_eq_i64(t, bmf_pool_index(&p, slots, reused), bmf_pool_index(&p, slots, h[0]) + 3, __FILE__, __LINE__, "free slot is reused");
    bmf_check(t, reused != h[3], __FILE__, __LINE__, "reused slot gets a new generation");
    bmf_check(t, !bmf_pool_valid(&p, slots, h[3]), __FILE__, __LINE__, "stale handle stays invalid after reuse");
    bmf_check(t, !bmf_pool_valid(&p, slots, 0), __FILE__, __LINE__, "handle 0 is invalid");
    bmf_check(t, !bmf_pool_valid(&p, slots, (8u << BMF_HANDLE_GENERATION_BITS) | 1u), __FILE__, __LINE__, "out-of-range index is invalid");
    bmf_check_eq_i64(t, p.high_water, 8, __FILE__, __LINE__, "pool high water");
}

// 4096 alloc/free cycles of one slot: every previous handle is stale, no handle is 0, and the
// generation wraps 4095 -> 1.
static void test_memory_pool_generations(bmf_test* t) {
    bmf_pool p;
    bmf_pool_slot slots[2];
    bmf_pool_init(&p, slots, 2);
    bmf_handle prev = 0;
    i32 bad = 0, zero_handles = 0, wraps = 0;
    for (i32 cycle = 0; cycle < 4096; cycle++) {
        bmf_handle h = bmf_pool_alloc(&p, slots);
        zero_handles += h == 0;
        if (prev && bmf_pool_valid(&p, slots, prev)) bad++;
        if (!bmf_pool_valid(&p, slots, h)) bad++;
        if (prev && (h & 0xFFFu) < (prev & 0xFFFu)) wraps++;
        bmf_pool_free(&p, slots, h);
        if (bmf_pool_valid(&p, slots, h)) bad++;
        prev = h;
    }
    bmf_check_eq_i64(t, bad, 0, __FILE__, __LINE__, "stale/valid errors over 4096 cycles");
    bmf_check_eq_i64(t, zero_handles, 0, __FILE__, __LINE__, "no handle is 0");
    bmf_check_eq_i64(t, wraps, 1, __FILE__, __LINE__, "generation wrapped exactly once (4095 -> 1)");
}

// Random alloc/free against a reference set: the dense list holds exactly the live slots, and
// handle_of round-trips.
static void test_memory_pool_dense_iteration(bmf_test* t) {
    enum { CAP = 64 };
    bmf_pool p;
    bmf_pool_slot slots[CAP];
    bmf_pool_init(&p, slots, CAP);
    bmf_handle live[CAP];
    i32 live_count = 0, bad = 0;
    u32 x = 12345;
    for (i32 step = 0; step < 20000; step++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        if ((x & 3) != 0 && live_count < CAP) {
            live[live_count++] = bmf_pool_alloc(&p, slots);
        } else if (live_count > 0) {
            i32 k = (i32)((x >> 8) % (u32)live_count);
            bmf_pool_free(&p, slots, live[k]);
            live[k] = live[--live_count];
        }
        if (p.live_count != live_count) { bad++; continue; }
        u8 seen[CAP] = { 0 };
        for (i32 i = 0; i < p.live_count; i++) {
            i32 slot = bmf_pool_dense_slot(slots, i);
            if (slot < 0 || slot >= CAP || seen[slot]) { bad++; break; }
            seen[slot] = 1;
            if (!bmf_pool_valid(&p, slots, bmf_pool_handle_of(slots, slot))) bad++;
        }
        for (i32 i = 0; i < live_count; i++) if (!seen[bmf_pool_index(&p, slots, live[i])]) bad++;
    }
    bmf_check_eq_i64(t, bad, 0, __FILE__, __LINE__, "dense list mismatches over 20000 random steps");
}
