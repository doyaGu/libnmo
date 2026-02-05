/**
 * @file nmo_cli_hex.h
 * @brief CLI hex encoding helpers
 */

#ifndef NMO_CLI_HEX_H
#define NMO_CLI_HEX_H

#include <stdbool.h>
#include <stddef.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert bytes to a NUL-terminated hex string.
 * @param bytes Input bytes
 * @param len Byte length
 * @param uppercase Emit 'A'..'F' instead of 'a'..'f'
 * @return Newly allocated string (caller frees), or NULL on allocation failure.
 */
char *nmo_cli_bytes_to_hex(const void *bytes, size_t len, bool uppercase);

/**
 * @brief Write a single byte as two hex characters.
 * @param out Two-character output buffer (no NUL terminator written)
 * @param value Byte value
 * @param uppercase Emit 'A'..'F' instead of 'a'..'f'
 */
void nmo_cli_hex_write_byte(char out[2], uint8_t value, bool uppercase);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CLI_HEX_H */
