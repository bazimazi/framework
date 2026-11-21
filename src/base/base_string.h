// base_string.h: strings, UTF-8/UTF-16, formatting, number parsing and the JSON writer
// (task 1A-06, SPEC §5.4).
//
// bmf_str is a pointer and a length; it is never NUL-terminated by contract (SPEC §5.1). The
// functions here never allocate except bmf_fmt_arena, which pushes onto the given arena.
// Everything is deterministic (no locale, no platform calls), so it is allowed in tick unless a
// comment says otherwise; formatting floats or parsing is still slow work, keep it out of hot loops.
//
// Float text: bmf_fmt "%f" and bmf_json_f32 print the SHORTEST decimal that parses back to the
// same f32 (round-trip exact, ties to the closest), in JavaScript Number notation: "0.1", "100",
// "1e+21", "1.5e-7", "-0". bmf_parse_f32 is correctly rounded (ties to even) for any input length.
#ifndef BMF_BASE_STRING_H
#define BMF_BASE_STRING_H

#include "base_types.h"
#include "base_memory.h"
#include "base_math.h"

typedef struct bmf_str { const u8* ptr; i32 len; } bmf_str;

// ---------------------------------------------------------------- C strings
// Length of a NUL-terminated string. O(n).
i32 bmf_cstr_len(const char* s);
// Byte equality of two NUL-terminated strings (0 pointers compare equal only to each other). O(n).
b32 bmf_cstr_eq(const char* a, const char* b);

// ---------------------------------------------------------------- bmf_str
// All O(1) unless noted; none allocate. Slices point into the original bytes.
static inline bmf_str bmf_str_make(const void* ptr, i32 len) { bmf_str s = { (const u8*)ptr, len }; return s; }
// Wraps a NUL-terminated string (not including the NUL). O(n).
static inline bmf_str bmf_str_from_cstr(const char* s) { return bmf_str_make(s, s ? bmf_cstr_len(s) : 0); }
// Bytes [start, end) with both clamped to [0, len].
bmf_str bmf_str_slice(bmf_str s, i32 start, i32 end);
// Byte equality. O(n).
b32 bmf_str_eq(bmf_str a, bmf_str b);
b32 bmf_str_eq_cstr(bmf_str a, const char* b);
b32 bmf_str_starts_with(bmf_str s, bmf_str prefix);
b32 bmf_str_ends_with(bmf_str s, bmf_str suffix);
// Index of the first occurrence of `needle` at or after `from`, or -1. An empty needle matches
// at `from`. O(n * m) worst case.
i32 bmf_str_find(bmf_str s, bmf_str needle, i32 from);
// Index of the first byte equal to `byte` at or after `from`, or -1. O(n).
i32 bmf_str_find_byte(bmf_str s, u8 byte, i32 from);
// Without leading/trailing ASCII whitespace (space, \t, \n, \r, \v, \f). O(n).
bmf_str bmf_str_trim(bmf_str s);

// Splits on a separator byte: "a,,b" gives "a", "", "b"; "" gives one empty piece.
//   bmf_str_splitter it = bmf_str_split(s, ','); bmf_str piece;
//   while (bmf_str_split_next(&it, &piece)) { ... }
typedef struct bmf_str_splitter { bmf_str rest; u8 separator; b32 done; } bmf_str_splitter;
static inline bmf_str_splitter bmf_str_split(bmf_str s, u8 separator) { bmf_str_splitter it = { s, separator, 0 }; return it; }
// Writes the next piece and returns 1, or returns 0 when there are no more pieces. O(piece).
b32 bmf_str_split_next(bmf_str_splitter* it, bmf_str* piece);

// ---------------------------------------------------------------- UTF-8 / UTF-16
// Decodes the code point starting at byte `at` (0 <= at < s.len). Returns the number of bytes
// consumed (1..4). Invalid or truncated sequences, overlong forms, surrogates and values above
// U+10FFFF decode as U+FFFD and consume 1 byte. O(1).
i32 bmf_utf8_decode(bmf_str s, i32 at, u32* codepoint);
// Encodes one code point into out[0..3] and returns the byte count (1..4). Surrogates and values
// above U+10FFFF encode U+FFFD. O(1).
i32 bmf_utf8_encode(u32 codepoint, u8* out);
// UTF-16 (for Win32 APIs) to UTF-8 and back. Unpaired surrogates / invalid UTF-8 become U+FFFD.
// They write at most `capacity` units and return the number of units the whole conversion needs
// (so a return value > capacity means the output was truncated at a code point boundary).
// No NUL is written. O(n).
i32 bmf_utf16_to_utf8(const u16* src, i32 src_len, u8* dst, i32 capacity);
i32 bmf_utf8_to_utf16(bmf_str src, u16* dst, i32 capacity);

// ---------------------------------------------------------------- formatting
// printf-like formatting. Specifiers:
//   %i %u %x       i32, u32, u32 as lowercase hex (no prefix)
//   %li %lu %lx    the same for i64 / u64
//   %f             a float (f32 or f64 argument, printed as f32): shortest round-trip text
//   %.Nf           fixed N decimals (N = 0..9), exactly rounded half to even: "%.2f" of 0.125 -> "0.12"
//   %c             one byte (int argument)
//   %s             const char* (0 prints "(null)")
//   %S             bmf_str, by value
//   %v2            bmf_v2, by value, printed as "[x,y]" with shortest floats
//   %%             a percent sign
// Flags and width for every specifier: "%5i" pads left with spaces, "%-5i" pads right, "%05i" pads
// with zeros after the sign, "%016lx". An unknown specifier is a programmer error (asserted) and
// prints as written. Argument types must match exactly (varargs are not type-checked).
//
// bmf_fmt_buf writes at most capacity - 1 bytes plus a NUL (capacity > 0) and returns the full
// length the text needs (excluding the NUL): a result >= capacity means it was truncated.
// O(output length). Allowed in tick (deterministic) but slow-ish for floats (~100 ns each).
i32 bmf_fmt_buf(char* buf, i32 capacity, const char* fmt, ...);
i32 bmf_fmt_va(char* buf, i32 capacity, const char* fmt, __builtin_va_list args);
// Formats onto an arena (exact size plus a NUL that is not counted in the returned length).
// The text lives as long as the arena allocation. O(output length).
bmf_str bmf_fmt_arena(bmf_arena* arena, const char* fmt, ...);

// Shortest round-trip text of an f32 into buf (at most 22 bytes, e.g. "-100000000000000000000",
// plus NUL: capacity >= 23 never truncates). Returns the length. NaN prints "nan", infinities
// "inf" / "-inf". O(1).
i32 bmf_f32_to_text(f32 x, char* buf, i32 capacity);

// ---------------------------------------------------------------- parsing
// The whole string must be the number: no surrounding whitespace (trim first), no trailing
// characters. On error *out is unchanged unless stated.
// Integers: optional + or -, decimal digits. BMF_ERR_RANGE when the value does not fit.
bmf_err bmf_parse_i32(bmf_str s, i32* out);
bmf_err bmf_parse_i64(bmf_str s, i64* out);
// Floats: [+-] digits [. digits] [e|E [+-] digits], at least one digit in the significand
// (".5" and "5." are accepted), or "inf", "infinity", "nan" (any case, optional sign).
// Correctly rounded to nearest, ties to even, for any number of digits. Overflow writes +-Inf to
// *out and returns BMF_ERR_RANGE; underflow rounds to 0 or a denormal and is BMF_OK.
bmf_err bmf_parse_f32(bmf_str s, f32* out);

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

#endif // BMF_BASE_STRING_H
