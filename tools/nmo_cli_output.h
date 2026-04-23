/**
 * @file nmo_cli_output.h
 * @brief CLI unified output formatting utilities
 */

#ifndef NMO_CLI_OUTPUT_H
#define NMO_CLI_OUTPUT_H

#include "nmo_cli_common.h"

#include "export/nmo_ansi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ANSI color codes (shared, keep CLI macro names for compatibility) */
#define NMO_CLI_COLOR_RESET   NMO_ANSI_RESET
#define NMO_CLI_COLOR_BOLD    NMO_ANSI_BOLD
#define NMO_CLI_COLOR_DIM     NMO_ANSI_DIM
#define NMO_CLI_COLOR_RED     NMO_ANSI_RED
#define NMO_CLI_COLOR_GREEN   NMO_ANSI_GREEN
#define NMO_CLI_COLOR_YELLOW  NMO_ANSI_YELLOW
#define NMO_CLI_COLOR_BLUE    NMO_ANSI_BLUE
#define NMO_CLI_COLOR_MAGENTA NMO_ANSI_MAGENTA
#define NMO_CLI_COLOR_CYAN    NMO_ANSI_CYAN
#define NMO_CLI_COLOR_WHITE   NMO_ANSI_WHITE

/**
 * @brief Table column alignment
 */
typedef enum {
    NMO_CLI_ALIGN_LEFT = 0,
    NMO_CLI_ALIGN_RIGHT,
    NMO_CLI_ALIGN_CENTER
} nmo_cli_align_t;

/**
 * @brief Table column definition
 */
typedef struct {
    const char *header;
    nmo_cli_align_t align;
    int min_width;
    int max_width;  /**< 0 = no max */
} nmo_cli_table_col_t;

/**
 * @brief Table row data
 */
typedef struct nmo_cli_table_row {
    char **cells;
    size_t cell_count;
    struct nmo_cli_table_row *next;
} nmo_cli_table_row_t;

/**
 * @brief Table builder
 */
typedef struct {
    const nmo_cli_table_col_t *columns;
    size_t column_count;
    nmo_cli_table_row_t *first_row;
    nmo_cli_table_row_t *last_row;
    size_t row_count;
    int *computed_widths;
} nmo_cli_table_t;

/**
 * @brief Initialize a table builder
 * @param table Table to initialize
 * @param columns Column definitions
 * @param column_count Number of columns
 */
void nmo_cli_table_init(nmo_cli_table_t *table, const nmo_cli_table_col_t *columns, size_t column_count);

/**
 * @brief Add a row to the table
 * @param table Table
 * @param cells Array of cell values (strings)
 * @param cell_count Number of cells (should match column_count)
 * @return true on success
 */
bool nmo_cli_table_add_row(nmo_cli_table_t *table, const char **cells, size_t cell_count);

/**
 * @brief Print table to stream
 * @param table Table to print
 * @param out Output stream
 * @param colorize Use ANSI colors
 */
void nmo_cli_table_print(const nmo_cli_table_t *table, FILE *out, bool colorize);

/**
 * @brief Free table resources
 * @param table Table to free
 */
void nmo_cli_table_free(nmo_cli_table_t *table);

/**
 * @brief Print a heading/section title
 * @param out Output stream
 * @param title Heading text
 * @param colorize Use ANSI colors
 */
void nmo_cli_print_heading(FILE *out, const char *title, bool colorize);

/**
 * @brief Print a key-value pair
 * @param out Output stream
 * @param key Key name
 * @param value Value string
 * @param key_width Minimum width for key column (for alignment)
 * @param colorize Use ANSI colors
 */
void nmo_cli_print_kv(FILE *out, const char *key, const char *value, int key_width, bool colorize);

/**
 * @brief Print an error message
 * @param out Output stream
 * @param format Printf format string
 * @param ... Format arguments
 */
void nmo_cli_print_error(FILE *out, const char *format, ...);

/**
 * @brief Print a warning message
 * @param out Output stream
 * @param format Printf format string
 * @param ... Format arguments
 */
void nmo_cli_print_warning(FILE *out, const char *format, ...);

/**
 * @brief Tree node for tree printing
 */
typedef struct nmo_cli_tree_node {
    const char *label;
    void *user_data;
    struct nmo_cli_tree_node *first_child;
    struct nmo_cli_tree_node *next_sibling;
} nmo_cli_tree_node_t;

/**
 * @brief Callback for custom tree node rendering
 */
typedef void (*nmo_cli_tree_render_fn)(FILE *out, const nmo_cli_tree_node_t *node, bool colorize);

/**
 * @brief Print a tree structure
 * @param root Root node
 * @param out Output stream
 * @param colorize Use ANSI colors
 * @param render_fn Custom render function (NULL for default)
 */
void nmo_cli_print_tree(const nmo_cli_tree_node_t *root,
                        FILE *out,
                        bool colorize,
                        nmo_cli_tree_render_fn render_fn);

/**
 * @brief Format chunk option flags as a compact string
 *
 * Example: "IDS|CHN|PACKED" or "-".
 * Unknown bits are appended as hex (e.g. "...|0x2000").
 *
 * @param options Chunk option bitfield
 * @param buf Output buffer
 * @param buf_size Output buffer size
 * @return buf on success, or "-" if buf is invalid
 */
const char *nmo_cli_chunk_options_to_string(uint32_t options, char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CLI_OUTPUT_H */
