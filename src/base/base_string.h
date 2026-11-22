// base_string.h: the engine side of strings (task 1A-06, SPEC §5.4): UTF-16 conversion for
// Win32 APIs and the JSON writer (control protocol, build and test output). bmf_str,
// UTF-8, formatting and parsing are public: see bmf.h.
#ifndef BMF_BASE_STRING_H
#define BMF_BASE_STRING_H

#include "../bmf.h"

// ---------------------------------------------------------------- UTF-16
// UTF-16 (for Win32 APIs) to UTF-8 and back. Unpaired surrogates / invalid UTF-8 become U+FFFD.
// They write at most `capacity` units and return the number of units the whole conversion needs
// (so a return value > capacity means the output was truncated at a code point boundary).
// No NUL is written. O(n).
i32 bmf_utf16_to_utf8(const u16* src, i32 src_len, u8* dst, i32 capacity);
i32 bmf_utf8_to_utf16(bmf_str src, u16* dst, i32 capacity);

// ---------------------------------------------------------------- JSON writer
// Writes compact JSON into a caller-owned buffer, inserting commas itself.
//   bmf_json_writer w; bmf_json_init(&w, buf, sizeof buf);
//   bmf_json_object_begin(&w); bmf_json_key(&w, "ok"); bmf_json_i64(&w, 1); bmf_json_object_end(&w);
// The buffer always holds a NUL-terminated prefix of the output; `overflow` is set when the text
// did not fit (it is then truncated, never written past `capacity`). Strings are escaped and
// invalid UTF-8 becomes �. Floats are shortest round-trip; NaN and infinities, which JSON
// cannot express, are written as the strings "NaN", "Infinity", "-Infinity".
// Nesting depth is at most 32 (asserted). All calls O(size of what they write).
typedef struct bmf_json_writer {
    char* buf;
    i32 len;
    i32 capacity;
    i32 depth;
    u32 has_items;       // bit d: the container at depth d already has an item (needs a comma)
    b32 after_key;       // a key was written: the next value needs no comma
    b32 overflow;
} bmf_json_writer;

void bmf_json_init(bmf_json_writer* w, char* buf, i32 capacity);
void bmf_json_object_begin(bmf_json_writer* w);
void bmf_json_object_end(bmf_json_writer* w);
void bmf_json_array_begin(bmf_json_writer* w);
void bmf_json_array_end(bmf_json_writer* w);
// The key of the next value inside an object.
void bmf_json_key(bmf_json_writer* w, const char* key);
void bmf_json_string(bmf_json_writer* w, const char* s);   // 0 writes null
void bmf_json_str(bmf_json_writer* w, bmf_str s);
void bmf_json_i64(bmf_json_writer* w, i64 v);
void bmf_json_u64(bmf_json_writer* w, u64 v);
void bmf_json_f32(bmf_json_writer* w, f32 v);
void bmf_json_bool(bmf_json_writer* w, b32 v);
void bmf_json_null(bmf_json_writer* w);
// Already-formatted JSON (a number, or a nested document) written as one value. Not checked.
void bmf_json_raw(bmf_json_writer* w, const char* json);

// ---------------------------------------------------------------- edit distance
enum { BMF_STR_EDIT_DISTANCE_MAX_LEN = 64 };

// Levenshtein distance (insertions, deletions, substitutions of bytes) between two strings of at
// most BMF_STR_EDIT_DISTANCE_MAX_LEN bytes; -1 when either is longer. For "did you mean" hints
// (reflection paths, protocol commands). O(a.len x b.len).
i32 bmf_str_edit_distance(bmf_str a, bmf_str b);

#endif // BMF_BASE_STRING_H
