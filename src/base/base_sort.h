// base_sort.h: stable sorts for integer keys with u32 payloads (SPEC §5.4). Used by draw2d (sort
// keys), physics (pair and cell keys) and the baker. Results depend only on the input, never on
// addresses (D-37 rule 5).
#ifndef BMF_BASE_SORT_H
#define BMF_BASE_SORT_H

#include "base_types.h"

// Sorts keys[0..n) ascending, moving payload[i] with keys[i]. Stable: equal keys keep their input
// order. `payload` may be 0 (keys only). `keys_tmp` and `payload_tmp` are caller scratch of n
// elements (payload_tmp may be 0 when payload is 0). The result is in keys/payload.
// LSD radix sort with 8-bit digits; a pass is skipped when every key has the same digit there.
// n <= 32 uses insertion sort. O(n * passes), passes <= 4.
void bmf_sort_u32(u32* keys, u32* payload, i32 n, u32* keys_tmp, u32* payload_tmp);

// As bmf_sort_u32 for u64 keys. passes <= 8.
void bmf_sort_u64(u64* keys, u32* payload, i32 n, u64* keys_tmp, u32* payload_tmp);

// Stable insertion sorts (any n; O(n^2)). Exposed for small arrays and as the test reference.
void bmf_insertion_sort_u32(u32* keys, u32* payload, i32 n);
void bmf_insertion_sort_u64(u64* keys, u32* payload, i32 n);

#endif // BMF_BASE_SORT_H
