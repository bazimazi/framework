// snap.c: state hashing and snapshots (task 1A-34). Contracts in snap.h, invariants in
// src/debug/README.md.

typedef struct snap_slot {
    char name[BMF_SNAP_NAME_MAX];   // "" = free
    char label[BMF_SNAP_NAME_MAX + 8];   // "snap.<name>" (the `mem` report)
    i64 tick;
    i32 state_size;
    u64 state_layout_hash;
    b32 reserved;
    bmf_arena arena;                // the copy of the state block, at arena.base
} snap_slot;

static snap_slot snap_slots[BMF_SNAP_MAX_SLOTS];

b32 bmf_snap_hash(u64* out_hash, char* path, i32 path_capacity) {
    bmf_reflect r = bmf_core_reflect();
    if (bmf_reflect_find_nonfinite(&r, path, path_capacity)) return 0;
    *out_hash = bmf_core_state_hash();
    return 1;
}

static b32 snap_name_valid(bmf_str name, char* detail, i32 detail_capacity) {
    b32 valid = name.len >= 1 && name.len < BMF_SNAP_NAME_MAX;
    for (i32 i = 0; valid && i < name.len; i++) {
        u8 c = name.ptr[i];
        valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
    }
    if (!valid) bmf_fmt_buf(detail, detail_capacity, "snapshot names are 1..%i characters of A-Z a-z 0-9 _ . -", (i32)BMF_SNAP_NAME_MAX - 1);
    return valid;
}

static i32 snap_find(bmf_str name) {
    for (i32 i = 0; i < BMF_SNAP_MAX_SLOTS; i++)
        if (snap_slots[i].name[0] && bmf_str_eq_cstr(name, snap_slots[i].name)) return i;
    return -1;
}

bmf_snap_status bmf_snap_save(bmf_str name, char* detail, i32 detail_capacity) {
    if (!snap_name_valid(name, detail, detail_capacity)) return BMF_SNAP_BAD_NAME;
    i32 index = snap_find(name);
    for (i32 i = 0; index < 0 && i < BMF_SNAP_MAX_SLOTS; i++)
        if (!snap_slots[i].name[0]) index = i;
    if (index < 0) {
        bmf_fmt_buf(detail, detail_capacity, "all %i snapshot slots are taken (save over an existing name)", (i32)BMF_SNAP_MAX_SLOTS);
        return BMF_SNAP_FULL;
    }
    snap_slot* slot = &snap_slots[index];
    if (!slot->reserved) {
        i64 size = ((i64)BMF_CORE_MAX_STATE_SIZE + BMF_PAGE_GRANULE - 1) / BMF_PAGE_GRANULE * BMF_PAGE_GRANULE;
        u8* base = (u8*)bmf_platform_reserve(size + BMF_PAGE_GRANULE);   // plus a guard granule
        if (!base) {
            bmf_fmt_buf(detail, detail_capacity, "address space reservation failed");
            return BMF_SNAP_NO_MEMORY;
        }
        bmf_arena_init(&slot->arena, "snap", base, size);
        slot->reserved = 1;
    }
    const bmf_game_desc* game = bmf_core_game();
    bmf_arena_reset(&slot->arena);
    u8* copy = (u8*)bmf_arena_push_aligned(&slot->arena, game->state_size, 64);
    bmf_memcpy(copy, bmf_core_state(), game->state_size);
    slot->tick = bmf_core_ctx()->tick;
    slot->state_size = game->state_size;
    slot->state_layout_hash = game->state_layout_hash;
    bmf_memcpy(slot->name, name.ptr, name.len);
    slot->name[name.len] = 0;
    bmf_fmt_buf(slot->label, (i32)sizeof slot->label, "snap.%s", slot->name);
    return BMF_SNAP_OK;
}

bmf_snap_status bmf_snap_load(bmf_str name, i64* out_tick, char* detail, i32 detail_capacity) {
    if (!snap_name_valid(name, detail, detail_capacity)) return BMF_SNAP_BAD_NAME;
    i32 index = snap_find(name);
    if (index < 0) {
        bmf_fmt_buf(detail, detail_capacity, "no snapshot '%S' (snap save %S first)", name, name);
        return BMF_SNAP_UNKNOWN;
    }
    snap_slot* slot = &snap_slots[index];
    const bmf_game_desc* game = bmf_core_game();
    if (slot->state_size != game->state_size || slot->state_layout_hash != game->state_layout_hash) {
        slot->name[0] = 0;   // saved before a reload changed the layout: it can never load again
        bmf_fmt_buf(detail, detail_capacity, "snapshot '%S' was saved with another state layout (a reload reset the game); it is dropped", name);
        return BMF_SNAP_UNKNOWN;
    }
    bmf_core_load_state(slot->arena.base, slot->tick);
    bmf_replay_on_state_replaced("snap load", "a snapshot was loaded during the recording");
    *out_tick = slot->tick;
    return BMF_SNAP_OK;
}

void bmf_snap_clear(void) {
    for (i32 i = 0; i < BMF_SNAP_MAX_SLOTS; i++) snap_slots[i].name[0] = 0;
}

i32 bmf_snap_arenas(const bmf_arena** out, const char** names, i32 capacity) {
    i32 count = 0;
    for (i32 i = 0; i < BMF_SNAP_MAX_SLOTS && count < capacity; i++) {
        if (!snap_slots[i].name[0]) continue;
        out[count] = &snap_slots[i].arena;
        names[count] = snap_slots[i].label;
        count++;
    }
    return count;
}
