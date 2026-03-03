/**
 * @file nmo_hex.h
 * @brief Shared hex encoding helpers.
 */

#ifndef NMO_HEX_H
#define NMO_HEX_H

#include "nmo_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Write a single byte as two hex characters.
 *
 * @param out Two-character output buffer (no NUL terminator is written).
 * @param value Byte value.
 * @param uppercase Emit 'A'..'F' instead of 'a'..'f'.
 */
NMO_API void nmo_hex_write_byte(char out[2], uint8_t value, bool uppercase);

/**
 * @brief Convert raw bytes to a NUL-terminated hex string.
 *
 * @param bytes Input bytes.
 * @param len Byte length.
 * @param uppercase Emit 'A'..'F' instead of 'a'..'f'.
 * @return Newly allocated string (caller frees with `free()`), or NULL on
 *         allocation/argument failure.
 */
NMO_API char *nmo_hex_bytes_to_string(const void *bytes, size_t len, bool uppercase);

#ifdef __cplusplus
}
#endif

#endif /* NMO_HEX_H */
