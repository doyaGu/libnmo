/**
 * @file nmo_utils.h
 * @brief Common utility functions and macros for libnmo
 * 
 * This header provides reusable utility functions categorized by functionality:
 * - Alignment and size calculation
 * - Byte order conversion (endianness)
 * - Buffer operations and bounds checking
 * - Min/max helpers
 */

#ifndef NMO_UTILS_H
#define NMO_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Alignment Utilities
 * ======================================================================== */

/**
 * @brief Align size to 4-byte (DWORD) boundary
 * @param bytes Size in bytes
 * @return Size rounded up to nearest 4-byte boundary
 */
static inline size_t nmo_align_dword(size_t bytes) {
    return (bytes + 3u) & ~3u;
}

/**
 * @brief Align size to arbitrary power-of-2 boundary
 * @param size Size to align
 * @param alignment Alignment boundary (must be power of 2)
 * @return Size rounded up to alignment boundary
 */
static inline size_t nmo_align(size_t size, size_t alignment) {
    return (size + alignment - 1u) & ~(alignment - 1u);
}

/**
 * @brief Calculate number of DWORDs needed for byte count
 * @param bytes Number of bytes
 * @return Number of 4-byte DWORDs needed (rounded up)
 */
static inline size_t nmo_bytes_to_dwords(size_t bytes) {
    return (bytes + 3u) / 4u;
}

/* ========================================================================
 * Byte Order Conversion (Endianness)
 * ======================================================================== */

/**
 * @brief Swap bytes in 16-bit value
 */
static inline uint16_t nmo_bswap16(uint16_t x) {
    return (uint16_t)(((x & 0x00FFu) << 8) | 
                      ((x & 0xFF00u) >> 8));
}

/**
 * @brief Swap bytes in 32-bit value
 */
static inline uint32_t nmo_bswap32(uint32_t x) {
    return ((x & 0x000000FFu) << 24) |
           ((x & 0x0000FF00u) << 8) |
           ((x & 0x00FF0000u) >> 8) |
           ((x & 0xFF000000u) >> 24);
}

/**
 * @brief Swap bytes in 64-bit value
 */
static inline uint64_t nmo_bswap64(uint64_t x) {
    return ((x & 0x00000000000000FFull) << 56) |
           ((x & 0x000000000000FF00ull) << 40) |
           ((x & 0x0000000000FF0000ull) << 24) |
           ((x & 0x00000000FF000000ull) << 8) |
           ((x & 0x000000FF00000000ull) >> 8) |
           ((x & 0x0000FF0000000000ull) >> 24) |
           ((x & 0x00FF000000000000ull) >> 40) |
           ((x & 0xFF00000000000000ull) >> 56);
}

/**
 * @brief Convert little-endian 16-bit to host endianness
 */
static inline uint16_t nmo_le16toh(uint16_t x) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return nmo_bswap16(x);
#else
    return x;
#endif
}

/**
 * @brief Convert little-endian 32-bit to host endianness
 */
static inline uint32_t nmo_le32toh(uint32_t x) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return nmo_bswap32(x);
#else
    return x;
#endif
}

/**
 * @brief Convert little-endian 64-bit to host endianness
 */
static inline uint64_t nmo_le64toh(uint64_t x) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return nmo_bswap64(x);
#else
    return x;
#endif
}

/**
 * @brief Convert host endianness to little-endian 16-bit
 */
static inline uint16_t nmo_htole16(uint16_t x) {
    return nmo_le16toh(x); /* Conversion is symmetric */
}

/**
 * @brief Convert host endianness to little-endian 32-bit
 */
static inline uint32_t nmo_htole32(uint32_t x) {
    return nmo_le32toh(x); /* Conversion is symmetric */
}

/**
 * @brief Convert host endianness to little-endian 64-bit
 */
static inline uint64_t nmo_htole64(uint64_t x) {
    return nmo_le64toh(x); /* Conversion is symmetric */
}

/**
 * @brief Swap bytes in array of 16-bit words (for LEndian16 chunk data)
 * @param data Pointer to data buffer
 * @param word_count Number of 16-bit words (not bytes!)
 */
static inline void nmo_swap_16bit_words(void *data, size_t word_count) {
    uint16_t *words = (uint16_t *)data;
    for (size_t i = 0; i < word_count; i++) {
        words[i] = nmo_bswap16(words[i]);
    }
}

/* ========================================================================
 * Little-Endian Read/Write Helpers
 * ======================================================================== */

/**
 * @brief Read uint16_t from little-endian byte buffer
 */
static inline uint16_t nmo_read_u16_le(const uint8_t *data) {
    return (uint16_t)data[0] |
           ((uint16_t)data[1] << 8);
}

/**
 * @brief Read uint32_t from little-endian byte buffer
 */
static inline uint32_t nmo_read_u32_le(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

/**
 * @brief Read uint64_t from little-endian byte buffer
 */
static inline uint64_t nmo_read_u64_le(const uint8_t *data) {
    return (uint64_t)data[0] |
           ((uint64_t)data[1] << 8) |
           ((uint64_t)data[2] << 16) |
           ((uint64_t)data[3] << 24) |
           ((uint64_t)data[4] << 32) |
           ((uint64_t)data[5] << 40) |
           ((uint64_t)data[6] << 48) |
           ((uint64_t)data[7] << 56);
}

/**
 * @brief Write uint16_t to little-endian byte buffer
 */
static inline void nmo_write_u16_le(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8) & 0xFFu);
}

/**
 * @brief Write uint32_t to little-endian byte buffer
 */
static inline void nmo_write_u32_le(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8) & 0xFFu);
    data[2] = (uint8_t)((value >> 16) & 0xFFu);
    data[3] = (uint8_t)((value >> 24) & 0xFFu);
}

/**
 * @brief Write uint64_t to little-endian byte buffer
 */
static inline void nmo_write_u64_le(uint8_t *data, uint64_t value) {
    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8) & 0xFFu);
    data[2] = (uint8_t)((value >> 16) & 0xFFu);
    data[3] = (uint8_t)((value >> 24) & 0xFFu);
    data[4] = (uint8_t)((value >> 32) & 0xFFu);
    data[5] = (uint8_t)((value >> 40) & 0xFFu);
    data[6] = (uint8_t)((value >> 48) & 0xFFu);
    data[7] = (uint8_t)((value >> 56) & 0xFFu);
}

/* ========================================================================
 * Min/Max Helpers
 * ======================================================================== */

#ifndef NMO_MIN
#define NMO_MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef NMO_MAX
#define NMO_MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

/**
 * @brief Clamp value between min and max
 */
static inline int nmo_clamp_int(int value, int min_val, int max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

/**
 * @brief Clamp size_t value between min and max
 */
static inline size_t nmo_clamp_size(size_t value, size_t min_val, size_t max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

/* ========================================================================
 * Buffer Bounds Checking Macros
 * ======================================================================== */

/**
 * @brief Check if buffer read/write would overflow
 * @param pos Current position in buffer
 * @param needed Bytes needed for operation
 * @param total Total buffer size
 * @return 1 if operation is safe, 0 if would overflow
 */
static inline int nmo_check_buffer_bounds(size_t pos, size_t needed, size_t total) {
    return (pos <= total) && (needed <= total - pos);
}

/**
 * @brief Macro for buffer bounds checking with error handling
 * 
 * Usage in functions that return nmo_result_t:
 * NMO_CHECK_BUFFER_SIZE(pos, needed, size);
 */
#define NMO_CHECK_BUFFER_SIZE(pos, needed, size)                              \
    do {                                                                      \
        if (!nmo_check_buffer_bounds((pos), (needed), (size))) {             \
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN, \
                NMO_SEVERITY_ERROR,                                          \
                "Buffer bounds check failed: pos=%zu, needed=%zu, size=%zu", \
                (size_t)(pos), (size_t)(needed), (size_t)(size)));          \
        }                                                                     \
    } while (0)

/* ========================================================================
 * Memory Utilities
 * ======================================================================== */

/**
 * @brief Safe memcpy with bounds checking
 * @return 1 if copy succeeded, 0 if bounds check failed
 */
static inline int nmo_safe_memcpy(void *dest, size_t dest_size, 
                                   const void *src, size_t count) {
    if (count > dest_size) {
        return 0;
    }
    memcpy(dest, src, count);
    return 1;
}

/**
 * @brief Calculate padding bytes needed for alignment
 * @param size Current size
 * @param alignment Desired alignment (must be power of 2)
 * @return Number of padding bytes needed
 */
static inline size_t nmo_padding_bytes(size_t size, size_t alignment) {
    size_t aligned = nmo_align(size, alignment);
    return aligned - size;
}

#ifdef __cplusplus
}
#endif

#endif /* NMO_UTILS_H */
