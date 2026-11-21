// base_hash.h: 64-bit hash (one-shot and streaming), CRC-32 and Adler-32 (SPEC §5.4).
#ifndef BMF_BASE_HASH_H
#define BMF_BASE_HASH_H

#include "base_types.h"

// bmf hash, design (own, documented here so it can be re-derived):
// - Four independent 64-bit lanes absorb 32-byte stripes. Each lane takes one little-endian
//   u64 word w per stripe: lane = rotl(lane ^ (w * K2), 31) * K1.
//   Four lanes keep four multiply chains in flight (instruction-level parallelism).
// - The 0..31 tail bytes are absorbed one u64 word at a time into a single accumulator with the
//   same round, then a last partial word padded with zeros.
// - Finalization folds the lanes with rotations, mixes in the total length, and applies the
//   avalanche function fmix: x ^= x >> 32; x *= K3; x ^= x >> 29; x *= K4; x ^= x >> 32.
// - Constants: K1 = 2^64 / golden ratio (odd), K2..K4 odd constants with balanced bits.
// - Byte order is little-endian on every bmf target, so results match across targets.
// - Not cryptographic. Used for state hashes, build stamps, asset ids and layout hashes.
// The one-shot and streaming forms give identical results for the same bytes and seed.

typedef struct bmf_hasher {
    u64 lanes[4];
    u8  buffer[32];
    i32 buffer_len;
    i64 total_len;
    u64 seed;
} bmf_hasher;

void bmf_hasher_init(bmf_hasher* h, u64 seed);                    // O(1)
void bmf_hasher_update(bmf_hasher* h, const void* data, i64 len); // O(len)
u64  bmf_hasher_final(const bmf_hasher* h);                       // O(1); does not modify h
u64  bmf_hash(const void* data, i64 len, u64 seed);               // O(len)

// CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320), as used by PNG and zlib.
// Start with crc = 0 and pass the previous result to continue. O(len).
u32 bmf_crc32(u32 crc, const void* data, i64 len);

// Adler-32 (RFC 1950), as used by zlib. Start with adler = 1. O(len).
u32 bmf_adler32(u32 adler, const void* data, i64 len);

#endif // BMF_BASE_HASH_H
