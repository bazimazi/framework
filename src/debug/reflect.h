// reflect.h: reflection of the game state block (SPEC §8.4, D-22, task 1A-30).
//
// A game describes its state block with a table of bmf_field (bmf.h): name, type, offset, element
// count and optionally the name of an I32 field that holds the live element count. This module
// validates such a table, hashes it (bmf_fields_hash, compared to state_layout_hash), and reads,
// writes, dumps and scans the state through it. The control protocol (1A-31) maps `get`, `set`,
// `dump` and `watch` onto these calls; the state hash pass (1A-34) uses the NaN/Inf scan.
//
// Paths: `name` (a scalar field, or every live element of an array), `name[i]` (one element,
// 0 <= i < live count) and `name[*]` (every live element). Names are matched exactly against the
// table, plus the built-in path `tick` (the engine's tick counter, read-only). A field of type
// BMF_T_RNG is a bmf_rng embedded in the state, reflected as its two words [state, inc].
//
// Values (JSON, written with bmf_json_writer): I32 I64 U32 U64 U8 HANDLE as integers, F32 as the
// shortest round-trip float, V2 V3 V4 as float arrays, B32 as true/false (or the integer when it is
// neither 0 nor 1), RNG as [state, inc]. An array path gives a JSON array of element values.
// Set takes one token per component: integers in decimal (unsigned types also 0x hex), floats as
// bmf_parse_f32 reads them (NaN and Inf are refused: they must never be in the state, D-37 rule
// 11), B32 as 0, 1, true or false.
//
// Errors are a closed set (bmf_reflect_status) with a one-line detail for agents, including a
// "did you mean" suggestion (edit distance, or the fields under a prefix) for unknown names.
//
// Thread rule for every function: main thread only, never from tick (the protocol runs between
// ticks). Nothing here allocates. Costs are O(fields) per lookup plus the size of the value.
#ifndef BMF_REFLECT_H
#define BMF_REFLECT_H

#include "../bmf.h"
#include "../base/base_string.h"

// ---------------------------------------------------------------- field tables
// Bytes a field offset must be a multiple of: the size of the type's scalar component (I64, U64
// and RNG 8, U8 1, everything else 4); 0 for an invalid type. O(1).
i32 bmf_field_type_align(bmf_field_type type);

// Lowercase name of a field type ("i32", "v2", "rng", ...), "?" for an invalid type. O(1).
const char* bmf_field_type_name(bmf_field_type type);

// Checks a field table for a state block of `state_size` bytes: 0 <= count <= BMF_FIELD_MAX_COUNT
// (fields != 0 when count > 0); names 1..63 bytes of a-z A-Z 0-9 _ . , unique and not a built-in
// path (`tick`); known type; count >= 1; inside the state block; offset aligned
// (bmf_field_type_align); count_field only on arrays (count >= 2) and naming an I32 scalar field
// anywhere in the table. On failure returns BMF_ERR_INVALID with a one-line message naming the
// field and the rule in error[0..error_capacity) (e.g. `field 2 "pos": offset 6 is not a multiple
// of 4`). O(count^2) name comparisons.
bmf_err bmf_reflect_check_fields(const bmf_field* fields, i32 count, i32 state_size, char* error, i32 error_capacity);

// Hash of a field table's layout: for every field in table order its name, type, offset and
// count (not count_field: it does not change the layout). 0 for an empty table, never 0 otherwise.
// Identical on every
// target (bmf_hash over explicit little-endian bytes). Games declare it as state_layout_hash; the
// engine checks the declared value in debug builds (SPEC §8.4). O(total name length).
u64 bmf_fields_hash(const bmf_field* fields, i32 count);

// ---------------------------------------------------------------- reflecting a state block
// What reflection works on. The pointers belong to the engine and must stay valid while used;
// `fields` must have passed bmf_reflect_check_fields for `state_size`. `tick` may be 0 (no
// built-in tick path).
typedef struct bmf_reflect {
    const bmf_field* fields;
    i32 field_count;
    u8* state;
    i32 state_size;
    const i64* tick;
} bmf_reflect;

typedef enum bmf_reflect_status {
    BMF_REFLECT_OK = 0,
    BMF_REFLECT_UNKNOWN_PATH = 1,         // "unknown_path": no field of that name
    BMF_REFLECT_BAD_PATH = 2,             // "bad_path": malformed, an index on a scalar, or a set of many elements
    BMF_REFLECT_INDEX_OUT_OF_RANGE = 3,   // "index_out_of_range": index >= live count
    BMF_REFLECT_BAD_VALUE = 4,            // "bad_value": wrong number of values, unparsable, out of range, NaN/Inf
    BMF_REFLECT_READ_ONLY = 5,            // "read_only": a built-in path that cannot be set
    BMF_REFLECT_STATUS_COUNT = 6,
} bmf_reflect_status;

// The protocol's error code for a status ("ok", "unknown_path", ...). O(1).
const char* bmf_reflect_status_code(bmf_reflect_status status);

enum { BMF_REFLECT_TICK = -2 };   // bmf_reflect_target.field of the built-in `tick` path

// A resolved path: the field index (or BMF_REFLECT_TICK) and the element (-1 = every live element).
typedef struct bmf_reflect_target {
    i32 field;
    i32 index;
} bmf_reflect_target;

// Parses `path` and finds its field and element (bounds-checked against the live count). On an
// error returns its status and writes a one-line detail into detail[0..detail_capacity), e.g.
// `no field 'bal.pos'; did you mean 'ball.pos'?`. O(fields x name length).
bmf_reflect_status bmf_reflect_resolve(const bmf_reflect* r, bmf_str path, bmf_reflect_target* out, char* detail, i32 detail_capacity);

// Byte range in the state block of a resolved target's element (index -1 on a scalar: the
// scalar; on an array: the whole array). The built-in tick has none (asserted). O(1).
void bmf_reflect_target_bytes(const bmf_reflect* r, bmf_reflect_target target, i32* offset, i32* size);

// Live element count of field `field`: the value of its count_field clamped to 0..count, or count
// when it has none. O(fields) (finds the count field by name).
i32 bmf_reflect_live_count(const bmf_reflect* r, i32 field);

// Writes the value at `path` to `w` as one JSON value (see Values above). On an error writes
// nothing and returns the status with a detail. O(fields + value size).
bmf_reflect_status bmf_reflect_get(const bmf_reflect* r, bmf_str path, bmf_json_writer* w, char* detail, i32 detail_capacity);

// Writes `values` (one token per component) into the single element at `path` (a scalar field or
// `name[i]`). All values are parsed and checked before anything is written, so a failed set leaves
// the state unchanged. Setting a count field is limited to 0..the smallest capacity of the arrays
// it counts; an RNG's inc must be odd. O(fields + value count).
bmf_reflect_status bmf_reflect_set(const bmf_reflect* r, bmf_str path, const bmf_str* values, i32 value_count, char* detail, i32 detail_capacity);

// Writes one JSON object {"path": value, ...} with the built-in paths and every field whose name
// starts with `prefix` (empty = all), in table order; arrays hold their live elements.
// O(fields + dumped bytes).
void bmf_reflect_dump(const bmf_reflect* r, bmf_str prefix, bmf_json_writer* w);

// Finds the first NaN or Inf in any F32/V2/V3/V4 field, every element up to its full count (dead
// elements are state too). Returns 1 and writes its path ("name" or "name[i]") into
// path[0..path_capacity), or returns 0. O(reflected float bytes).
b32 bmf_reflect_find_nonfinite(const bmf_reflect* r, char* path, i32 path_capacity);

// The known name closest to `name` for a "did you mean" hint: the first field whose name starts
// with `name` + ".", else the field or built-in path with the smallest edit distance if that is
// at most 1 + length / 3. 0 when nothing is close. O(fields x 63^2).
const char* bmf_reflect_suggest(const bmf_reflect* r, bmf_str name);

#endif // BMF_REFLECT_H
