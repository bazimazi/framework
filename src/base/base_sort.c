// base_sort.c: radix and insertion sorts (contracts: bmf.h, sorting section).
#include "../bmf.h"

void bmf_insertion_sort_u32(u32* keys, u32* payload, i32 n) {
    for (i32 i = 1; i < n; i++) {
        u32 k = keys[i];
        u32 v = payload ? payload[i] : 0;
        i32 j = i - 1;
        while (j >= 0 && keys[j] > k) {   // strict >: equal keys never move past each other
            keys[j + 1] = keys[j];
            if (payload) payload[j + 1] = payload[j];
            j--;
        }
        keys[j + 1] = k;
        if (payload) payload[j + 1] = v;
    }
}

void bmf_insertion_sort_u64(u64* keys, u32* payload, i32 n) {
    for (i32 i = 1; i < n; i++) {
        u64 k = keys[i];
        u32 v = payload ? payload[i] : 0;
        i32 j = i - 1;
        while (j >= 0 && keys[j] > k) {
            keys[j + 1] = keys[j];
            if (payload) payload[j + 1] = payload[j];
            j--;
        }
        keys[j + 1] = k;
        if (payload) payload[j + 1] = v;
    }
}

// One function body per key width, written out (no macro): histogram all digits in one read,
// then one scatter pass per digit that is not constant.

void bmf_sort_u32(u32* keys, u32* payload, i32 n, u32* keys_tmp, u32* payload_tmp) {
    BMF_ASSERT(n >= 0 && keys_tmp && (!payload || payload_tmp));
    if (n <= 32) { bmf_insertion_sort_u32(keys, payload, n); return; }
    i32 counts[4][256];
    bmf_memset(counts, 0, sizeof counts);
    for (i32 i = 0; i < n; i++) {
        u32 k = keys[i];
        counts[0][k & 0xFF]++;
        counts[1][(k >> 8) & 0xFF]++;
        counts[2][(k >> 16) & 0xFF]++;
        counts[3][k >> 24]++;
    }
    u32* src_k = keys; u32* dst_k = keys_tmp;
    u32* src_p = payload; u32* dst_p = payload ? payload_tmp : 0;   // no payload: both stay 0
    for (i32 pass = 0; pass < 4; pass++) {
        i32* c = counts[pass];
        i32 shift = pass * 8;
        if (c[(src_k[0] >> shift) & 0xFF] == n) continue;   // every key has this digit: skip
        i32 offset = 0;
        for (i32 d = 0; d < 256; d++) { i32 count = c[d]; c[d] = offset; offset += count; }
        for (i32 i = 0; i < n; i++) {
            u32 k = src_k[i];
            i32 at = c[(k >> shift) & 0xFF]++;
            dst_k[at] = k;
            if (src_p) dst_p[at] = src_p[i];
        }
        u32* t = src_k; src_k = dst_k; dst_k = t;
        t = src_p; src_p = dst_p; dst_p = t;
    }
    if (src_k != keys) {   // odd number of executed passes: copy back
        bmf_memcpy(keys, src_k, (i64)n * 4);
        if (payload) bmf_memcpy(payload, src_p, (i64)n * 4);
    }
}

void bmf_sort_u64(u64* keys, u32* payload, i32 n, u64* keys_tmp, u32* payload_tmp) {
    BMF_ASSERT(n >= 0 && keys_tmp && (!payload || payload_tmp));
    if (n <= 32) { bmf_insertion_sort_u64(keys, payload, n); return; }
    i32 counts[8][256];
    bmf_memset(counts, 0, sizeof counts);
    for (i32 i = 0; i < n; i++) {
        u64 k = keys[i];
        for (i32 d = 0; d < 8; d++) counts[d][(k >> (d * 8)) & 0xFF]++;
    }
    u64* src_k = keys; u64* dst_k = keys_tmp;
    u32* src_p = payload; u32* dst_p = payload ? payload_tmp : 0;   // no payload: both stay 0
    for (i32 pass = 0; pass < 8; pass++) {
        i32* c = counts[pass];
        i32 shift = pass * 8;
        if (c[(src_k[0] >> shift) & 0xFF] == n) continue;
        i32 offset = 0;
        for (i32 d = 0; d < 256; d++) { i32 count = c[d]; c[d] = offset; offset += count; }
        for (i32 i = 0; i < n; i++) {
            u64 k = src_k[i];
            i32 at = c[(k >> shift) & 0xFF]++;
            dst_k[at] = k;
            if (src_p) dst_p[at] = src_p[i];
        }
        u64* tk = src_k; src_k = dst_k; dst_k = tk;
        u32* tp = src_p; src_p = dst_p; dst_p = tp;
    }
    if (src_k != keys) {
        bmf_memcpy(keys, src_k, (i64)n * 8);
        if (payload) bmf_memcpy(payload, src_p, (i64)n * 4);
    }
}
