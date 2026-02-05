/**
 * @file nmo_hexdump.h
 * @brief Hexdump utilities (hexdump -C compatible output)
 */

#ifndef NMO_HEXDUMP_H
#define NMO_HEXDUMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ANSI styling for hexdump output.
 *
 * If @ref nmo_hexdump_options_t::colorize is false, these are ignored.
 * Any field may be NULL or an empty string.
 */
typedef struct nmo_hexdump_ansi_style {
    const char *offset;
    const char *hex;
    const char *ascii;
    const char *delim;
    const char *reset;
} nmo_hexdump_ansi_style_t;

/**
 * @brief Options for hexdump rendering.
 */
typedef struct nmo_hexdump_options {
    size_t bytes_per_line;      /**< Default 16 */
    size_t group_size;          /**< Default 8 (extra space between groups) */
    size_t indent_spaces;       /**< Default 0 */
    bool show_ascii;            /**< Default true */
    bool show_final_offset;     /**< Default true (prints trailing size line) */
    bool colorize;              /**< Default false */
    nmo_hexdump_ansi_style_t ansi;
} nmo_hexdump_options_t;

/**
 * @brief Initialize options to sensible defaults (hexdump -C compatible).
 */
void nmo_hexdump_init_options(nmo_hexdump_options_t *options);

/**
 * @brief Write canonical hexdump (compatible with `hexdump -C`).
 *
 * Output is safe for terminals: it never prints raw bytes directly.
 * The ASCII column prints printable ASCII (0x20..0x7E), else '.'.
 */
void nmo_hexdump_canonical(FILE *out,
                           const void *data,
                           size_t size,
                           const nmo_hexdump_options_t *options);

#ifdef __cplusplus
}
#endif

#endif /* NMO_HEXDUMP_H */
