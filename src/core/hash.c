/**
 * @file nmo_hash.c
 * @brief Optimized hash function implementations (Phase 5)
 * 
 * Based on:
 * - MurmurHash3 by Austin Appleby (public domain)
 * - XXHash by Yann Collet (BSD 2-Clause License)
 */

#include "core/nmo_hash.h"
#include <string.h>

/* ==================== MurmurHash3 Implementation ==================== */

/**
 * Rotl32 - rotate left
 */
static inline uint32_t rotl32(uint32_t x, int8_t r) {
    return (x << r) | (x >> (32 - r));
}

/**
 * MurmurHash3 finalizer
 */
static inline uint32_t fmix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

/**
 * MurmurHash3 32-bit
 */
uint32_t nmo_murmur3_32(const void *data, size_t len, uint32_t seed) {
    const uint8_t *bytes = (const uint8_t *)data;
    const size_t nblocks = len / 4;
    
    uint32_t h1 = seed;
    
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;
    
    /* Body: process 4-byte blocks */
    const uint32_t *blocks = (const uint32_t *)(bytes + nblocks * 4);
    
    for (size_t i = 0; i < nblocks; i++) {
        uint32_t k1;
        memcpy(&k1, blocks + i, sizeof(uint32_t));
        
        k1 *= c1;
        k1 = rotl32(k1, 15);
        k1 *= c2;
        
        h1 ^= k1;
        h1 = rotl32(h1, 13);
        h1 = h1 * 5 + 0xe6546b64;
    }
    
    /* Tail: process remaining bytes */
    const uint8_t *tail = bytes + nblocks * 4;
    uint32_t k1 = 0;
    
    switch (len & 3) {
        case 3: k1 ^= (uint32_t)tail[2] << 16; /* fallthrough */
        case 2: k1 ^= (uint32_t)tail[1] << 8;  /* fallthrough */
        case 1: k1 ^= (uint32_t)tail[0];
                k1 *= c1;
                k1 = rotl32(k1, 15);
                k1 *= c2;
                h1 ^= k1;
    }
    
    /* Finalization */
    h1 ^= (uint32_t)len;
    h1 = fmix32(h1);
    
    return h1;
}

/**
 * MurmurHash3 128-bit (x64 version)
 */
void nmo_murmur3_128(const void *data, size_t len, uint32_t seed, void *out) {
    const uint8_t *bytes = (const uint8_t *)data;
    const size_t nblocks = len / 16;
    
    uint64_t h1 = seed;
    uint64_t h2 = seed;
    
    const uint64_t c1 = 0x87c37b91114253d5ULL;
    const uint64_t c2 = 0x4cf5ad432745937fULL;
    
    /* Body */
    const uint64_t *blocks = (const uint64_t *)bytes;
    
    for (size_t i = 0; i < nblocks; i++) {
        uint64_t k1, k2;
        memcpy(&k1, blocks + i * 2, sizeof(uint64_t));
        memcpy(&k2, blocks + i * 2 + 1, sizeof(uint64_t));
        
        k1 *= c1;
        k1 = (k1 << 31) | (k1 >> 33);
        k1 *= c2;
        h1 ^= k1;
        
        h1 = (h1 << 27) | (h1 >> 37);
        h1 += h2;
        h1 = h1 * 5 + 0x52dce729;
        
        k2 *= c2;
        k2 = (k2 << 33) | (k2 >> 31);
        k2 *= c1;
        h2 ^= k2;
        
        h2 = (h2 << 31) | (h2 >> 33);
        h2 += h1;
        h2 = h2 * 5 + 0x38495ab5;
    }
    
    /* Tail */
    const uint8_t *tail = bytes + nblocks * 16;
    uint64_t k1 = 0;
    uint64_t k2 = 0;
    
    switch (len & 15) {
        case 15: k2 ^= (uint64_t)tail[14] << 48; /* fallthrough */
        case 14: k2 ^= (uint64_t)tail[13] << 40; /* fallthrough */
        case 13: k2 ^= (uint64_t)tail[12] << 32; /* fallthrough */
        case 12: k2 ^= (uint64_t)tail[11] << 24; /* fallthrough */
        case 11: k2 ^= (uint64_t)tail[10] << 16; /* fallthrough */
        case 10: k2 ^= (uint64_t)tail[9] << 8;   /* fallthrough */
        case 9:  k2 ^= (uint64_t)tail[8];
                 k2 *= c2;
                 k2 = (k2 << 33) | (k2 >> 31);
                 k2 *= c1;
                 h2 ^= k2;
                 /* fallthrough */
        case 8:  k1 ^= (uint64_t)tail[7] << 56; /* fallthrough */
        case 7:  k1 ^= (uint64_t)tail[6] << 48; /* fallthrough */
        case 6:  k1 ^= (uint64_t)tail[5] << 40; /* fallthrough */
        case 5:  k1 ^= (uint64_t)tail[4] << 32; /* fallthrough */
        case 4:  k1 ^= (uint64_t)tail[3] << 24; /* fallthrough */
        case 3:  k1 ^= (uint64_t)tail[2] << 16; /* fallthrough */
        case 2:  k1 ^= (uint64_t)tail[1] << 8;  /* fallthrough */
        case 1:  k1 ^= (uint64_t)tail[0];
                 k1 *= c1;
                 k1 = (k1 << 31) | (k1 >> 33);
                 k1 *= c2;
                 h1 ^= k1;
    }
    
    /* Finalization */
    h1 ^= len;
    h2 ^= len;
    
    h1 += h2;
    h2 += h1;
    
    h1 ^= h1 >> 33;
    h1 *= 0xff51afd7ed558ccdULL;
    h1 ^= h1 >> 33;
    h1 *= 0xc4ceb9fe1a85ec53ULL;
    h1 ^= h1 >> 33;
    
    h2 ^= h2 >> 33;
    h2 *= 0xff51afd7ed558ccdULL;
    h2 ^= h2 >> 33;
    h2 *= 0xc4ceb9fe1a85ec53ULL;
    h2 ^= h2 >> 33;
    
    h1 += h2;
    h2 += h1;
    
    ((uint64_t *)out)[0] = h1;
    ((uint64_t *)out)[1] = h2;
}

/* ==================== XXHash32 Implementation ==================== */

#define PRIME32_1  0x9E3779B1U
#define PRIME32_2  0x85EBCA77U
#define PRIME32_3  0xC2B2AE3DU
#define PRIME32_4  0x27D4EB2FU
#define PRIME32_5  0x165667B1U

static inline uint32_t xxh_rotl32(uint32_t x, int r) {
    return (x << r) | (x >> (32 - r));
}

/**
 * XXHash32 implementation
 */
uint32_t nmo_xxhash32(const void *data, size_t len, uint32_t seed) {
    const uint8_t *p = (const uint8_t *)data;
    const uint8_t *end = p + len;
    uint32_t h32;
    
    if (len >= 16) {
        const uint8_t *limit = end - 16;
        uint32_t v1 = seed + PRIME32_1 + PRIME32_2;
        uint32_t v2 = seed + PRIME32_2;
        uint32_t v3 = seed;
        uint32_t v4 = seed - PRIME32_1;
        
        do {
            uint32_t k1, k2, k3, k4;
            memcpy(&k1, p, sizeof(uint32_t)); p += 4;
            memcpy(&k2, p, sizeof(uint32_t)); p += 4;
            memcpy(&k3, p, sizeof(uint32_t)); p += 4;
            memcpy(&k4, p, sizeof(uint32_t)); p += 4;
            
            v1 += k1 * PRIME32_2;
            v1 = xxh_rotl32(v1, 13);
            v1 *= PRIME32_1;
            
            v2 += k2 * PRIME32_2;
            v2 = xxh_rotl32(v2, 13);
            v2 *= PRIME32_1;
            
            v3 += k3 * PRIME32_2;
            v3 = xxh_rotl32(v3, 13);
            v3 *= PRIME32_1;
            
            v4 += k4 * PRIME32_2;
            v4 = xxh_rotl32(v4, 13);
            v4 *= PRIME32_1;
        } while (p <= limit);
        
        h32 = xxh_rotl32(v1, 1) + xxh_rotl32(v2, 7) + xxh_rotl32(v3, 12) + xxh_rotl32(v4, 18);
    } else {
        h32 = seed + PRIME32_5;
    }
    
    h32 += (uint32_t)len;
    
    /* Process remaining bytes */
    while (p + 4 <= end) {
        uint32_t k1;
        memcpy(&k1, p, sizeof(uint32_t));
        h32 += k1 * PRIME32_3;
        h32 = xxh_rotl32(h32, 17) * PRIME32_4;
        p += 4;
    }
    
    while (p < end) {
        h32 += (*p) * PRIME32_5;
        h32 = xxh_rotl32(h32, 11) * PRIME32_1;
        p++;
    }
    
    /* Final mix */
    h32 ^= h32 >> 15;
    h32 *= PRIME32_2;
    h32 ^= h32 >> 13;
    h32 *= PRIME32_3;
    h32 ^= h32 >> 16;
    
    return h32;
}
