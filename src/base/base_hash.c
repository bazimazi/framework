// base_hash.c: see base_hash.h for the design.
#include "base_hash.h"

static const u64 HASH_K1 = 0x9E3779B97F4A7C15ull;
static const u64 HASH_K2 = 0xD6E8FEB86659FD93ull;
static const u64 HASH_K3 = 0xA0761D6478BD642Full;
static const u64 HASH_K4 = 0xE7037ED1A0B428DBull;

static inline u64 hash_rotl(u64 x, i32 r) { return (x << r) | (x >> (64 - r)); }
static inline u64 hash_round(u64 lane, u64 w) { return hash_rotl(lane ^ (w * HASH_K2), 31) * HASH_K1; }
static inline u64 hash_read64(const u8* p) { u64 w; __builtin_memcpy(&w, p, 8); return w; }   // little-endian targets only
static inline u64 hash_fmix(u64 x) {
    x ^= x >> 32; x *= HASH_K3;
    x ^= x >> 29; x *= HASH_K4;
    x ^= x >> 32;
    return x;
}

void bmf_hasher_init(bmf_hasher* h, u64 seed) {
    h->lanes[0] = seed + HASH_K1;
    h->lanes[1] = seed ^ HASH_K2;
    h->lanes[2] = seed - HASH_K3;
    h->lanes[3] = hash_rotl(seed, 17) ^ HASH_K4;
    h->buffer_len = 0;
    h->total_len = 0;
    h->seed = seed;
}

static inline void hash_stripe(u64* lanes, const u8* p) {
    lanes[0] = hash_round(lanes[0], hash_read64(p));
    lanes[1] = hash_round(lanes[1], hash_read64(p + 8));
    lanes[2] = hash_round(lanes[2], hash_read64(p + 16));
    lanes[3] = hash_round(lanes[3], hash_read64(p + 24));
}

void bmf_hasher_update(bmf_hasher* h, const void* data, i64 len) {
    const u8* p = (const u8*)data;
    h->total_len += len;
    if (h->buffer_len > 0) {   // complete a partial stripe first
        i64 need = 32 - h->buffer_len;
        i64 take = len < need ? len : need;
        bmf_memcpy(h->buffer + h->buffer_len, p, take);
        h->buffer_len += (i32)take;
        p += take;
        len -= take;
        if (h->buffer_len < 32) return;
        hash_stripe(h->lanes, h->buffer);
        h->buffer_len = 0;
    }
    u64 l0 = h->lanes[0], l1 = h->lanes[1], l2 = h->lanes[2], l3 = h->lanes[3];
    while (len >= 32) {
        l0 = hash_round(l0, hash_read64(p));
        l1 = hash_round(l1, hash_read64(p + 8));
        l2 = hash_round(l2, hash_read64(p + 16));
        l3 = hash_round(l3, hash_read64(p + 24));
        p += 32;
        len -= 32;
    }
    h->lanes[0] = l0; h->lanes[1] = l1; h->lanes[2] = l2; h->lanes[3] = l3;
    if (len > 0) {
        bmf_memcpy(h->buffer, p, len);
        h->buffer_len = (i32)len;
    }
}

u64 bmf_hasher_final(const bmf_hasher* h) {
    u64 acc = hash_rotl(h->lanes[0], 1) + hash_rotl(h->lanes[1], 7) + hash_rotl(h->lanes[2], 12) + hash_rotl(h->lanes[3], 18);
    acc ^= (u64)h->total_len * HASH_K1;
    const u8* p = h->buffer;
    i32 left = h->buffer_len;
    while (left >= 8) {
        acc = hash_round(acc, hash_read64(p));
        p += 8;
        left -= 8;
    }
    if (left > 0) {
        u8 last[8] = { 0 };
        bmf_memcpy(last, p, left);
        acc = hash_round(acc, hash_read64(last) ^ ((u64)left << 59));
    }
    return hash_fmix(acc ^ h->seed);
}

u64 bmf_hash(const void* data, i64 len, u64 seed) {
    bmf_hasher h;
    bmf_hasher_init(&h, seed);
    bmf_hasher_update(&h, data, len);
    return bmf_hasher_final(&h);
}

// ---------------------------------------------------------------- CRC-32 and Adler-32
static u32 hash_crc_table[256];
static b32 hash_crc_ready;

u32 bmf_crc32(u32 crc, const void* data, i64 len) {
    if (!hash_crc_ready) {   // built on first use; identical contents every time (not thread-racy in effect)
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (i32 k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            hash_crc_table[i] = c;
        }
        hash_crc_ready = 1;
    }
    const u8* p = (const u8*)data;
    crc = ~crc;
    for (i64 i = 0; i < len; i++) crc = hash_crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

u32 bmf_adler32(u32 adler, const void* data, i64 len) {
    const u8* p = (const u8*)data;
    u32 a = adler & 0xFFFF, b = adler >> 16;
    while (len > 0) {
        i64 chunk = len < 5552 ? len : 5552;   // largest n with no u32 overflow before the modulo (RFC 1950)
        len -= chunk;
        for (i64 i = 0; i < chunk; i++) { a += p[i]; b += a; }
        p += chunk;
        a %= 65521u;
        b %= 65521u;
    }
    return (b << 16) | a;
}
