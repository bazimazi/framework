// reflect.c: reflection of the game state block (task 1A-30). Contracts in reflect.h, invariants
// in src/debug/README.md.

// ---------------------------------------------------------------- field types
i32 bmf_field_type_size(bmf_field_type type) {
    switch (type) {
    case BMF_T_I32: case BMF_T_U32: case BMF_T_F32: case BMF_T_B32: case BMF_T_HANDLE: return 4;
    case BMF_T_I64: case BMF_T_U64: case BMF_T_V2: return 8;
    case BMF_T_V3: return 12;
    case BMF_T_V4: case BMF_T_RNG: return 16;
    case BMF_T_U8: return 1;
    default: return 0;
    }
}

i32 bmf_field_type_align(bmf_field_type type) {
    switch (type) {
    case BMF_T_I64: case BMF_T_U64: case BMF_T_RNG: return 8;
    case BMF_T_U8: return 1;
    case BMF_T_I32: case BMF_T_U32: case BMF_T_F32: case BMF_T_B32: case BMF_T_HANDLE:
    case BMF_T_V2: case BMF_T_V3: case BMF_T_V4: return 4;
    default: return 0;
    }
}

const char* bmf_field_type_name(bmf_field_type type) {
    switch (type) {
    case BMF_T_I32: return "i32";
    case BMF_T_U32: return "u32";
    case BMF_T_I64: return "i64";
    case BMF_T_U64: return "u64";
    case BMF_T_F32: return "f32";
    case BMF_T_V2: return "v2";
    case BMF_T_V3: return "v3";
    case BMF_T_V4: return "v4";
    case BMF_T_B32: return "b32";
    case BMF_T_U8: return "u8";
    case BMF_T_HANDLE: return "handle";
    case BMF_T_RNG: return "rng";
    default: return "?";
    }
}

// Values a `set` takes for one element: one per component.
static i32 reflect_components(bmf_field_type type) {
    switch (type) {
    case BMF_T_V2: case BMF_T_RNG: return 2;
    case BMF_T_V3: return 3;
    case BMF_T_V4: return 4;
    default: return 1;
    }
}

static b32 reflect_name_valid(const char* name) {
    if (!name) return 0;
    i32 length = 0;
    for (const char* c = name; *c; c++, length++) {
        b32 ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '.';
        if (!ok || length >= BMF_FIELD_NAME_MAX) return 0;
    }
    return length > 0;
}

// ---------------------------------------------------------------- validation and hash
bmf_err bmf_reflect_check_fields(const bmf_field* fields, i32 count, i32 state_size, char* error, i32 error_capacity) {
    BMF_ASSERT(error && error_capacity > 0);
    error[0] = 0;
    if (count < 0 || count > BMF_FIELD_MAX_COUNT || (count > 0 && !fields)) {
        bmf_fmt_buf(error, error_capacity, "field_count %i out of range 0..%i (or no table)", count, BMF_FIELD_MAX_COUNT);
        return BMF_ERR_INVALID;
    }
    for (i32 f = 0; f < count; f++) {
        const bmf_field* field = &fields[f];
        const char* field_name = field->name ? field->name : "(null)";
        if (!reflect_name_valid(field->name)) {
            bmf_fmt_buf(error, error_capacity, "field %i \"%s\": name must be 1..%i bytes of a-z A-Z 0-9 _ .", f, field_name, BMF_FIELD_NAME_MAX);
            return BMF_ERR_INVALID;
        }
        if (bmf_cstr_eq(field->name, "tick")) {
            bmf_fmt_buf(error, error_capacity, "field %i \"tick\": the name is a built-in path (the engine's tick counter)", f);
            return BMF_ERR_INVALID;
        }
        for (i32 other = 0; other < f; other++) {
            if (bmf_cstr_eq(fields[other].name, field->name)) {
                bmf_fmt_buf(error, error_capacity, "field %i \"%s\": duplicate name (also field %i)", f, field_name, other);
                return BMF_ERR_INVALID;
            }
        }
        i32 size = bmf_field_type_size(field->type);
        if (size == 0) {
            bmf_fmt_buf(error, error_capacity, "field %i \"%s\": unknown type %i", f, field_name, (i32)field->type);
            return BMF_ERR_INVALID;
        }
        if (field->count < 1) {
            bmf_fmt_buf(error, error_capacity, "field %i \"%s\": count %i must be >= 1", f, field_name, field->count);
            return BMF_ERR_INVALID;
        }
        i64 end = (i64)field->offset + (i64)size * (i64)field->count;
        if (field->offset < 0 || end > state_size) {
            bmf_fmt_buf(error, error_capacity, "field %i \"%s\": bytes %i..%li are outside the state block (state_size %i)",
                        f, field_name, field->offset, end, state_size);
            return BMF_ERR_INVALID;
        }
        i32 align = bmf_field_type_align(field->type);
        if (field->offset % align != 0) {
            bmf_fmt_buf(error, error_capacity, "field %i \"%s\": offset %i is not a multiple of %i", f, field_name, field->offset, align);
            return BMF_ERR_INVALID;
        }
    }
    // Count fields last: they may name a field that comes later in the table.
    for (i32 f = 0; f < count; f++) {
        const bmf_field* field = &fields[f];
        if (!field->count_field) continue;
        if (field->count < 2) {
            bmf_fmt_buf(error, error_capacity, "field %i \"%s\": count_field is only for arrays (count >= 2)", f, field->name);
            return BMF_ERR_INVALID;
        }
        i32 found = -1;
        for (i32 other = 0; other < count; other++)
            if (bmf_cstr_eq(fields[other].name, field->count_field)) found = other;
        if (found < 0) {
            bmf_fmt_buf(error, error_capacity, "field %i \"%s\": count_field \"%s\" is not a field", f, field->name, field->count_field);
            return BMF_ERR_INVALID;
        }
        if (fields[found].type != BMF_T_I32 || fields[found].count != 1) {
            bmf_fmt_buf(error, error_capacity, "field %i \"%s\": count_field \"%s\" must be an I32 scalar", f, field->name, field->count_field);
            return BMF_ERR_INVALID;
        }
    }
    return BMF_OK;
}

static void reflect_hash_u32(bmf_hasher* h, u32 v) {
    u8 bytes[4] = { (u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24) };   // little-endian on every target
    bmf_hasher_update(h, bytes, 4);
}

u64 bmf_fields_hash(const bmf_field* fields, i32 count) {
    if (count <= 0) return 0;
    bmf_hasher h;
    bmf_hasher_init(&h, 0x626d665f6669656cull);   // "bmf_fiel"
    reflect_hash_u32(&h, (u32)count);
    for (i32 f = 0; f < count; f++) {
        i32 length = bmf_cstr_len(fields[f].name);
        bmf_hasher_update(&h, fields[f].name, length + 1);   // with the NUL: "ab"+"c" differs from "a"+"bc"
        reflect_hash_u32(&h, (u32)fields[f].type);
        reflect_hash_u32(&h, (u32)fields[f].offset);
        reflect_hash_u32(&h, (u32)fields[f].count);
    }
    u64 hash = bmf_hasher_final(&h);
    return hash ? hash : 1;   // 0 means "no fields"
}

// ---------------------------------------------------------------- lookup
const char* bmf_reflect_status_code(bmf_reflect_status status) {
    switch (status) {
    case BMF_REFLECT_OK: return "ok";
    case BMF_REFLECT_UNKNOWN_PATH: return "unknown_path";
    case BMF_REFLECT_BAD_PATH: return "bad_path";
    case BMF_REFLECT_INDEX_OUT_OF_RANGE: return "index_out_of_range";
    case BMF_REFLECT_BAD_VALUE: return "bad_value";
    case BMF_REFLECT_READ_ONLY: return "read_only";
    default: return "?";
    }
}

// Field index of `name`, BMF_REFLECT_TICK for the built-in tick path, or -1.
static i32 reflect_find(const bmf_reflect* r, bmf_str name) {
    if (r->tick && bmf_str_eq_cstr(name, "tick")) return BMF_REFLECT_TICK;
    for (i32 f = 0; f < r->field_count; f++)
        if (bmf_str_eq_cstr(name, r->fields[f].name)) return f;
    return -1;
}

void bmf_reflect_target_bytes(const bmf_reflect* r, bmf_reflect_target target, i32* offset, i32* size) {
    BMF_ASSERT(target.field >= 0 && target.field < r->field_count);
    const bmf_field* fd = &r->fields[target.field];
    i32 element = bmf_field_type_size(fd->type);
    *offset = fd->offset + (target.index > 0 ? element * target.index : 0);
    *size = target.index < 0 && fd->count > 1 ? element * fd->count : element;
}

i32 bmf_reflect_live_count(const bmf_reflect* r, i32 field) {
    BMF_ASSERT(field >= 0 && field < r->field_count);
    const bmf_field* fd = &r->fields[field];
    if (!fd->count_field) return fd->count;
    i32 counter = reflect_find(r, bmf_str_from_cstr(fd->count_field));
    BMF_ASSERT(counter >= 0);   // bmf_reflect_check_fields guarantees it
    i32 live;
    __builtin_memcpy(&live, r->state + r->fields[counter].offset, 4);
    return live < 0 ? 0 : live > fd->count ? fd->count : live;
}

const char* bmf_reflect_suggest(const bmf_reflect* r, bmf_str name) {
    if (name.len <= 0) return 0;
    for (i32 f = 0; f < r->field_count; f++) {
        bmf_str candidate = bmf_str_from_cstr(r->fields[f].name);
        if (candidate.len > name.len && candidate.ptr[name.len] == '.' && bmf_str_starts_with(candidate, name)) return r->fields[f].name;
    }
    if (name.len > BMF_FIELD_NAME_MAX) return 0;
    const char* best = 0;
    i32 best_distance = 1 + name.len / 3 + 1;   // anything at most 1 + len/3 qualifies
    if (r->tick) {
        i32 d = bmf_str_edit_distance(name, bmf_str_from_cstr("tick"));
        if (d < best_distance) { best = "tick"; best_distance = d; }
    }
    for (i32 f = 0; f < r->field_count; f++) {
        i32 d = bmf_str_edit_distance(name, bmf_str_from_cstr(r->fields[f].name));
        if (d < best_distance) { best = r->fields[f].name; best_distance = d; }
    }
    return best;
}

bmf_reflect_status bmf_reflect_resolve(const bmf_reflect* r, bmf_str path, bmf_reflect_target* out, char* detail, i32 detail_capacity) {
    BMF_ASSERT(r && out && detail && detail_capacity > 0);
    detail[0] = 0;
    out->field = -1;
    out->index = -1;
    i32 bracket = bmf_str_find_byte(path, '[', 0);
    bmf_str name = bracket < 0 ? path : bmf_str_slice(path, 0, bracket);
    if (name.len == 0) {
        bmf_fmt_buf(detail, detail_capacity, "empty field name in path '%S'", path);
        return BMF_REFLECT_BAD_PATH;
    }
    i32 field = reflect_find(r, name);
    if (field == -1) {
        const char* suggestion = bmf_reflect_suggest(r, name);
        if (suggestion) bmf_fmt_buf(detail, detail_capacity, "no field '%S'; did you mean '%s'?", name, suggestion);
        else bmf_fmt_buf(detail, detail_capacity, "no field '%S' (dump lists the fields)", name);
        return BMF_REFLECT_UNKNOWN_PATH;
    }
    i32 index = -1;
    if (bracket >= 0) {
        if (path.ptr[path.len - 1] != ']' || path.len - bracket < 3) {
            bmf_fmt_buf(detail, detail_capacity, "bad path '%S': use name, name[i] or name[*]", path);
            return BMF_REFLECT_BAD_PATH;
        }
        bmf_str inside = bmf_str_slice(path, bracket + 1, path.len - 1);
        if (!bmf_str_eq_cstr(inside, "*")) {
            b32 digits = inside.len > 0;
            for (i32 i = 0; i < inside.len; i++) digits &= inside.ptr[i] >= '0' && inside.ptr[i] <= '9';
            if (!digits || bmf_parse_i32(inside, &index) != BMF_OK) {
                bmf_fmt_buf(detail, detail_capacity, "bad index '%S' in '%S': use a number from 0 or *", inside, path);
                return BMF_REFLECT_BAD_PATH;
            }
        }
        if (field == BMF_REFLECT_TICK || r->fields[field].count == 1) {
            bmf_fmt_buf(detail, detail_capacity, "'%S' is not an array", name);
            return BMF_REFLECT_BAD_PATH;
        }
    }
    if (index >= 0) {
        i32 live = bmf_reflect_live_count(r, field);
        if (index >= live) {
            const bmf_field* fd = &r->fields[field];
            if (fd->count_field)
                bmf_fmt_buf(detail, detail_capacity, "%S: index %i out of range: live count %i (%s), capacity %i", path, index, live, fd->count_field, fd->count);
            else
                bmf_fmt_buf(detail, detail_capacity, "%S: index %i out of range: count %i", path, index, fd->count);
            return BMF_REFLECT_INDEX_OUT_OF_RANGE;
        }
    }
    out->field = field;
    out->index = index;
    return BMF_REFLECT_OK;
}

// ---------------------------------------------------------------- get, dump
static void reflect_write_element(bmf_json_writer* w, bmf_field_type type, const u8* p) {
    i32 i32_value; u32 u32_value; i64 i64_value; u64 u64_value; f32 floats[4];
    switch (type) {
    case BMF_T_I32: __builtin_memcpy(&i32_value, p, 4); bmf_json_i64(w, i32_value); break;
    case BMF_T_U32: case BMF_T_HANDLE: __builtin_memcpy(&u32_value, p, 4); bmf_json_u64(w, u32_value); break;
    case BMF_T_I64: __builtin_memcpy(&i64_value, p, 8); bmf_json_i64(w, i64_value); break;
    case BMF_T_U64: __builtin_memcpy(&u64_value, p, 8); bmf_json_u64(w, u64_value); break;
    case BMF_T_U8: bmf_json_u64(w, p[0]); break;
    case BMF_T_B32:
        __builtin_memcpy(&i32_value, p, 4);
        if (i32_value == 0 || i32_value == 1) bmf_json_bool(w, i32_value);
        else bmf_json_i64(w, i32_value);
        break;
    case BMF_T_F32: __builtin_memcpy(floats, p, 4); bmf_json_f32(w, floats[0]); break;
    case BMF_T_V2: case BMF_T_V3: case BMF_T_V4: {
        i32 n = reflect_components(type);
        __builtin_memcpy(floats, p, (size_t)n * 4);
        bmf_json_array_begin(w);
        for (i32 i = 0; i < n; i++) bmf_json_f32(w, floats[i]);
        bmf_json_array_end(w);
        break;
    }
    case BMF_T_RNG: {
        u64 words[2];
        __builtin_memcpy(words, p, 16);
        bmf_json_array_begin(w);
        bmf_json_u64(w, words[0]);
        bmf_json_u64(w, words[1]);
        bmf_json_array_end(w);
        break;
    }
    default: bmf_json_null(w); break;
    }
}

// Writes the element `index` of `field`, or every live element as an array (index -1 on an
// array), or the scalar (index -1, count 1).
static void reflect_write_target(const bmf_reflect* r, bmf_reflect_target target, bmf_json_writer* w) {
    if (target.field == BMF_REFLECT_TICK) { bmf_json_i64(w, *r->tick); return; }
    const bmf_field* fd = &r->fields[target.field];
    i32 size = bmf_field_type_size(fd->type);
    const u8* base = r->state + fd->offset;
    if (target.index >= 0) { reflect_write_element(w, fd->type, base + (i64)size * target.index); return; }
    if (fd->count == 1) { reflect_write_element(w, fd->type, base); return; }
    i32 live = bmf_reflect_live_count(r, target.field);
    bmf_json_array_begin(w);
    for (i32 i = 0; i < live; i++) reflect_write_element(w, fd->type, base + (i64)size * i);
    bmf_json_array_end(w);
}

bmf_reflect_status bmf_reflect_get(const bmf_reflect* r, bmf_str path, bmf_json_writer* w, char* detail, i32 detail_capacity) {
    bmf_reflect_target target;
    bmf_reflect_status status = bmf_reflect_resolve(r, path, &target, detail, detail_capacity);
    if (status != BMF_REFLECT_OK) return status;
    reflect_write_target(r, target, w);
    return BMF_REFLECT_OK;
}

void bmf_reflect_dump(const bmf_reflect* r, bmf_str prefix, bmf_json_writer* w) {
    bmf_json_object_begin(w);
    if (r->tick && bmf_str_starts_with(bmf_str_from_cstr("tick"), prefix)) {
        bmf_json_key(w, "tick");
        bmf_json_i64(w, *r->tick);
    }
    for (i32 f = 0; f < r->field_count; f++) {
        if (!bmf_str_starts_with(bmf_str_from_cstr(r->fields[f].name), prefix)) continue;
        bmf_json_key(w, r->fields[f].name);
        bmf_reflect_target target = { f, -1 };
        reflect_write_target(r, target, w);
    }
    bmf_json_object_end(w);
}

// ---------------------------------------------------------------- set
// Unsigned integer in decimal or 0x hex, at most `max`.
static b32 reflect_parse_unsigned(bmf_str s, u64 max, u64* out) {
    u64 value = 0;
    i32 start = 0;
    u64 base = 10;
    if (s.len > 2 && s.ptr[0] == '0' && (s.ptr[1] == 'x' || s.ptr[1] == 'X')) { base = 16; start = 2; }
    if (s.len <= start) return 0;
    for (i32 i = start; i < s.len; i++) {
        u8 c = s.ptr[i];
        u64 digit;
        if (c >= '0' && c <= '9') digit = (u64)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') digit = (u64)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F') digit = (u64)(c - 'A' + 10);
        else return 0;
        if (value > (max - digit) / base) return 0;   // value * base + digit > max
        value = value * base + digit;
    }
    *out = value;
    return 1;
}

// Parses one finite f32; writes the reason into detail on failure.
static b32 reflect_parse_finite(bmf_str token, f32* out, char* detail, i32 detail_capacity) {
    f32 v = 0.0f;
    bmf_err e = bmf_parse_f32(token, &v);
    if (e == BMF_ERR_INVALID) { bmf_fmt_buf(detail, detail_capacity, "'%S' is not a number", token); return 0; }
    if (e != BMF_OK || (bmf_f32_bits(v) & 0x7F800000u) == 0x7F800000u) {
        bmf_fmt_buf(detail, detail_capacity, "'%S': NaN and Inf are not allowed in the state (D-37 rule 11)", token);
        return 0;
    }
    *out = v;
    return 1;
}

bmf_reflect_status bmf_reflect_set(const bmf_reflect* r, bmf_str path, const bmf_str* values, i32 value_count, char* detail, i32 detail_capacity) {
    bmf_reflect_target target;
    bmf_reflect_status status = bmf_reflect_resolve(r, path, &target, detail, detail_capacity);
    if (status != BMF_REFLECT_OK) return status;
    if (target.field == BMF_REFLECT_TICK) {
        bmf_fmt_buf(detail, detail_capacity, "tick is read-only (it advances with step)");
        return BMF_REFLECT_READ_ONLY;
    }
    const bmf_field* fd = &r->fields[target.field];
    if (target.index < 0 && fd->count > 1) {
        bmf_fmt_buf(detail, detail_capacity, "set writes one element: use '%s[i]'", fd->name);
        return BMF_REFLECT_BAD_PATH;
    }
    i32 components = reflect_components(fd->type);
    if (value_count != components) {
        bmf_fmt_buf(detail, detail_capacity, "%s is %s: needs %i value%s, got %i", fd->name, bmf_field_type_name(fd->type),
                    components, components == 1 ? "" : "s", value_count);
        return BMF_REFLECT_BAD_VALUE;
    }
    // Parse everything into `bytes` first; the state is written only when all values are valid.
    u8 bytes[16];
    i32 size = bmf_field_type_size(fd->type);
    const char* type_name = bmf_field_type_name(fd->type);
    switch (fd->type) {
    case BMF_T_I32: {
        i32 v;
        if (bmf_parse_i32(values[0], &v) != BMF_OK) { bmf_fmt_buf(detail, detail_capacity, "'%S' is not an i32", values[0]); return BMF_REFLECT_BAD_VALUE; }
        __builtin_memcpy(bytes, &v, 4);
        break;
    }
    case BMF_T_I64: {
        i64 v;
        if (bmf_parse_i64(values[0], &v) != BMF_OK) { bmf_fmt_buf(detail, detail_capacity, "'%S' is not an i64", values[0]); return BMF_REFLECT_BAD_VALUE; }
        __builtin_memcpy(bytes, &v, 8);
        break;
    }
    case BMF_T_U32: case BMF_T_HANDLE: case BMF_T_U8: case BMF_T_U64: {
        u64 max = fd->type == BMF_T_U8 ? 0xFFull : fd->type == BMF_T_U64 ? ~0ull : 0xFFFFFFFFull;
        u64 v;
        if (!reflect_parse_unsigned(values[0], max, &v)) {
            bmf_fmt_buf(detail, detail_capacity, "'%S' is not a %s (0..%lu, decimal or 0x hex)", values[0], type_name, max);
            return BMF_REFLECT_BAD_VALUE;
        }
        u32 v32 = (u32)v;
        u8 v8 = (u8)v;
        if (size == 8) __builtin_memcpy(bytes, &v, 8);
        else if (size == 4) __builtin_memcpy(bytes, &v32, 4);
        else bytes[0] = v8;
        break;
    }
    case BMF_T_B32: {
        i32 v;
        if (bmf_str_eq_cstr(values[0], "0") || bmf_str_eq_cstr(values[0], "false")) v = 0;
        else if (bmf_str_eq_cstr(values[0], "1") || bmf_str_eq_cstr(values[0], "true")) v = 1;
        else { bmf_fmt_buf(detail, detail_capacity, "'%S' is not a b32 (0, 1, true or false)", values[0]); return BMF_REFLECT_BAD_VALUE; }
        __builtin_memcpy(bytes, &v, 4);
        break;
    }
    case BMF_T_F32: case BMF_T_V2: case BMF_T_V3: case BMF_T_V4: {
        f32 v[4];
        for (i32 i = 0; i < components; i++)
            if (!reflect_parse_finite(values[i], &v[i], detail, detail_capacity)) return BMF_REFLECT_BAD_VALUE;
        __builtin_memcpy(bytes, v, (size_t)size);
        break;
    }
    case BMF_T_RNG: {
        u64 words[2];
        for (i32 i = 0; i < 2; i++) {
            if (!reflect_parse_unsigned(values[i], ~0ull, &words[i])) {
                bmf_fmt_buf(detail, detail_capacity, "'%S' is not a u64 (decimal or 0x hex)", values[i]);
                return BMF_REFLECT_BAD_VALUE;
            }
        }
        if ((words[1] & 1u) == 0) {
            bmf_fmt_buf(detail, detail_capacity, "%s: inc %lu must be odd (PCG32 stream)", fd->name, words[1]);
            return BMF_REFLECT_BAD_VALUE;
        }
        __builtin_memcpy(bytes, words, 16);
        break;
    }
    default:
        BMF_ASSERT(0);
        return BMF_REFLECT_BAD_VALUE;
    }
    // A count field must stay within the capacity of every array it counts.
    if (fd->type == BMF_T_I32 && fd->count == 1) {
        i32 v;
        __builtin_memcpy(&v, bytes, 4);
        for (i32 f = 0; f < r->field_count; f++) {
            const bmf_field* array = &r->fields[f];
            if (!array->count_field || !bmf_cstr_eq(array->count_field, fd->name)) continue;
            if (v < 0 || v > array->count) {
                bmf_fmt_buf(detail, detail_capacity, "%s is the live count of %s (capacity %i): %i is outside 0..%i",
                            fd->name, array->name, array->count, v, array->count);
                return BMF_REFLECT_BAD_VALUE;
            }
        }
    }
    i32 element = target.index < 0 ? 0 : target.index;
    __builtin_memcpy(r->state + fd->offset + (i64)size * element, bytes, (size_t)size);
    return BMF_REFLECT_OK;
}

// ---------------------------------------------------------------- NaN/Inf scan (D-37 rule 11)
b32 bmf_reflect_find_nonfinite(const bmf_reflect* r, char* path, i32 path_capacity) {
    BMF_ASSERT(path && path_capacity > 0);
    path[0] = 0;
    for (i32 f = 0; f < r->field_count; f++) {
        const bmf_field* fd = &r->fields[f];
        if (fd->type != BMF_T_F32 && fd->type != BMF_T_V2 && fd->type != BMF_T_V3 && fd->type != BMF_T_V4) continue;
        i32 components = reflect_components(fd->type);
        const u8* base = r->state + fd->offset;
        for (i32 e = 0; e < fd->count; e++) {
            for (i32 c = 0; c < components; c++) {
                u32 bits;
                __builtin_memcpy(&bits, base + ((i64)e * components + c) * 4, 4);
                if ((bits & 0x7F800000u) != 0x7F800000u) continue;
                if (fd->count == 1) bmf_fmt_buf(path, path_capacity, "%s", fd->name);
                else bmf_fmt_buf(path, path_capacity, "%s[%i]", fd->name, e);
                return 1;
            }
        }
    }
    return 0;
}
