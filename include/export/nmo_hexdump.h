/**
 * @file nmo_hexdump.h
 * @brief Hexdump utilities (hexdump -C compatible output)
 */

#ifndef NMO_EXPORT_HEXDUMP_H
#define NMO_EXPORT_HEXDUMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NMO_HEXDUMP_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_HEXDUMP_API_TIER NMO_API_TIER_ADVANCED_C

typedef struct nmo_hexdump_ansi_style {
    const char *offset;
    const char *hex;
    const char *ascii;
    const char *delim;
    const char *reset;
} nmo_hexdump_ansi_style_t;

typedef struct nmo_hexdump_options {
    size_t bytes_per_line;
    size_t group_size;
    size_t indent_spaces;
    bool show_ascii;
    bool show_final_offset;
    bool colorize;
    nmo_hexdump_ansi_style_t ansi;
} nmo_hexdump_options_t;

void nmo_hexdump_init_options(nmo_hexdump_options_t *options);

void nmo_hexdump_canonical(FILE *out,
                           const void *data,
                           size_t size,
                           const nmo_hexdump_options_t *options);

#ifdef __cplusplus
}
#endif

#endif /* NMO_EXPORT_HEXDUMP_H */
