/**
 * @file nmo_cli_output.c
 * @brief CLI unified output formatting utilities
 */

#include "nmo_cli_output.h"

#include "nmo_cli_hex.h"

#include "format/nmo_chunk.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

const char *nmo_cli_chunk_options_to_string(uint32_t options, char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) {
        return "-";
    }

    buf[0] = '\0';
    size_t pos = 0;
    bool first = true;

    const uint32_t known_mask =
        (uint32_t)NMO_CHUNK_OPTION_IDS |
        (uint32_t)NMO_CHUNK_OPTION_MAN |
        (uint32_t)NMO_CHUNK_OPTION_CHN |
        (uint32_t)NMO_CHUNK_OPTION_FILE |
        (uint32_t)NMO_CHUNK_OPTION_ALLOWDYN |
        (uint32_t)NMO_CHUNK_OPTION_LISTBIG |
        (uint32_t)NMO_CHUNK_DONTDELETE_PTR |
        (uint32_t)NMO_CHUNK_DONTDELETE_PARSER |
        (uint32_t)NMO_CHUNK_OPTION_PACKED;

#define NMO_CLI_APPEND_FLAG(bit, name) \
    do { \
        if ((options & (uint32_t)(bit)) != 0) { \
            const char *s__ = (name); \
            size_t slen__ = strlen(s__); \
            if (!first) { \
                if (pos + 1 < buf_size) { \
                    buf[pos++] = '|'; \
                } \
            } \
            size_t to_copy__ = slen__; \
            if (pos + to_copy__ >= buf_size) { \
                to_copy__ = (buf_size > pos + 1) ? (buf_size - pos - 1) : 0; \
            } \
            if (to_copy__ > 0) { \
                memcpy(buf + pos, s__, to_copy__); \
                pos += to_copy__; \
                buf[pos] = '\0'; \
            } \
            first = false; \
        } \
    } while (0)

    NMO_CLI_APPEND_FLAG(NMO_CHUNK_OPTION_IDS, "IDS");
    NMO_CLI_APPEND_FLAG(NMO_CHUNK_OPTION_MAN, "MAN");
    NMO_CLI_APPEND_FLAG(NMO_CHUNK_OPTION_CHN, "CHN");
    NMO_CLI_APPEND_FLAG(NMO_CHUNK_OPTION_FILE, "FILE");
    NMO_CLI_APPEND_FLAG(NMO_CHUNK_OPTION_ALLOWDYN, "ALLOWDYN");
    NMO_CLI_APPEND_FLAG(NMO_CHUNK_OPTION_LISTBIG, "LISTBIG");
    NMO_CLI_APPEND_FLAG(NMO_CHUNK_DONTDELETE_PTR, "DONTDELETE_PTR");
    NMO_CLI_APPEND_FLAG(NMO_CHUNK_DONTDELETE_PARSER, "DONTDELETE_PARSER");
    NMO_CLI_APPEND_FLAG(NMO_CHUNK_OPTION_PACKED, "PACKED");

#undef NMO_CLI_APPEND_FLAG

    uint32_t unknown = options & ~known_mask;
    if (unknown != 0) {
        char unknown_buf[32];
        snprintf(unknown_buf, sizeof(unknown_buf), "0x%X", unknown);
        if (!first && pos + 1 < buf_size) {
            buf[pos++] = '|';
            buf[pos] = '\0';
        }
        size_t slen = strlen(unknown_buf);
        size_t to_copy = slen;
        if (pos + to_copy >= buf_size) {
            to_copy = (buf_size > pos + 1) ? (buf_size - pos - 1) : 0;
        }
        if (to_copy > 0) {
            memcpy(buf + pos, unknown_buf, to_copy);
            pos += to_copy;
            buf[pos] = '\0';
        }
        first = false;
    }

    if (first) {
        snprintf(buf, buf_size, "-");
    }
    return buf;
}

static char *nmo_cli_escape_bytes(const char *value) {
    if (!value) {
        return strdup("");
    }

    size_t len = strlen(value);
    size_t max_len = len * 4 + 1; /* worst-case: \xHH */
    char *out = (char *)malloc(max_len);
    if (!out) {
        return strdup("");
    }

    size_t pos = 0;
    const unsigned char *p = (const unsigned char *)value;
    for (; *p; ++p) {
        unsigned char c = *p;
        if (c >= 0x20 && c <= 0x7E) {
            out[pos++] = (char)c;
        } else {
            if (pos + 4 >= max_len) {
                break;
            }
            out[pos++] = '\\';
            out[pos++] = 'x';
            char hex[2];
            nmo_cli_hex_write_byte(hex, (uint8_t)c, true);
            out[pos++] = hex[0];
            out[pos++] = hex[1];
        }
    }
    out[pos] = '\0';
    return out;
}

/* ============================================================================
 * Table utilities
 * ============================================================================ */

void nmo_cli_table_init(nmo_cli_table_t *table, const nmo_cli_table_col_t *columns, size_t column_count) {
    if (!table) {
        return;
    }
    memset(table, 0, sizeof(*table));
    table->columns = columns;
    table->column_count = column_count;
}

bool nmo_cli_table_add_row(nmo_cli_table_t *table, const char **cells, size_t cell_count) {
    if (!table || !cells) {
        return false;
    }

    nmo_cli_table_row_t *row = (nmo_cli_table_row_t *)calloc(1, sizeof(nmo_cli_table_row_t));
    if (!row) {
        return false;
    }

    row->cell_count = cell_count;
    row->cells = (char **)calloc(cell_count, sizeof(char *));
    if (!row->cells) {
        free(row);
        return false;
    }

    for (size_t i = 0; i < cell_count; ++i) {
        if (cells[i]) {
            row->cells[i] = nmo_cli_escape_bytes(cells[i]);
        } else {
            row->cells[i] = strdup("");
        }
    }

    if (table->last_row) {
        table->last_row->next = row;
    } else {
        table->first_row = row;
    }
    table->last_row = row;
    table->row_count++;

    return true;
}

/**
 * Compute column widths based on content
 */
static void compute_widths(nmo_cli_table_t *table) {
    if (!table || table->computed_widths) {
        return;
    }

    table->computed_widths = (int *)calloc(table->column_count, sizeof(int));
    if (!table->computed_widths) {
        return;
    }

    /* Start with header widths */
    for (size_t i = 0; i < table->column_count; ++i) {
        int w = table->columns[i].min_width;
        if (table->columns[i].header) {
            int hlen = (int)strlen(table->columns[i].header);
            if (hlen > w) {
                w = hlen;
            }
        }
        table->computed_widths[i] = w;
    }

    /* Check row content */
    for (nmo_cli_table_row_t *row = table->first_row; row; row = row->next) {
        for (size_t i = 0; i < row->cell_count && i < table->column_count; ++i) {
            if (row->cells[i]) {
                int clen = (int)strlen(row->cells[i]);
                if (clen > table->computed_widths[i]) {
                    int max_w = table->columns[i].max_width;
                    if (max_w > 0 && clen > max_w) {
                        clen = max_w;
                    }
                    table->computed_widths[i] = clen;
                }
            }
        }
    }
}

/**
 * Print cell with alignment and width
 */
static void print_cell(FILE *out, const char *text, int width, nmo_cli_align_t align) {
    if (!text) {
        text = "";
    }
    int len = (int)strlen(text);
    int pad = width - len;
    if (pad < 0) {
        pad = 0;
    }

    switch (align) {
    case NMO_CLI_ALIGN_RIGHT:
        fprintf(out, "%*s%s", pad, "", text);
        break;
    case NMO_CLI_ALIGN_CENTER:
        fprintf(out, "%*s%s%*s", pad / 2, "", text, (pad + 1) / 2, "");
        break;
    case NMO_CLI_ALIGN_LEFT:
    default:
        fprintf(out, "%s%*s", text, pad, "");
        break;
    }
}

void nmo_cli_table_print(const nmo_cli_table_t *table, FILE *out, bool colorize) {
    if (!table || !out) {
        return;
    }

    /* Non-const cast for computing widths */
    compute_widths((nmo_cli_table_t *)table);
    if (!table->computed_widths) {
        return;
    }

    /* Print header */
    if (colorize) {
        fprintf(out, "%s", NMO_CLI_COLOR_BOLD);
    }
    for (size_t i = 0; i < table->column_count; ++i) {
        if (i > 0) {
            fprintf(out, "  ");
        }
        print_cell(out, table->columns[i].header, table->computed_widths[i], table->columns[i].align);
    }
    if (colorize) {
        fprintf(out, "%s", NMO_CLI_COLOR_RESET);
    }
    fprintf(out, "\n");

    /* Print separator */
    for (size_t i = 0; i < table->column_count; ++i) {
        if (i > 0) {
            fprintf(out, "  ");
        }
        for (int j = 0; j < table->computed_widths[i]; ++j) {
            fputc('-', out);
        }
    }
    fprintf(out, "\n");

    /* Print rows */
    for (nmo_cli_table_row_t *row = table->first_row; row; row = row->next) {
        for (size_t i = 0; i < table->column_count; ++i) {
            if (i > 0) {
                fprintf(out, "  ");
            }
            const char *cell = (i < row->cell_count) ? row->cells[i] : "";
            print_cell(out, cell, table->computed_widths[i], table->columns[i].align);
        }
        fprintf(out, "\n");
    }
}

void nmo_cli_table_free(nmo_cli_table_t *table) {
    if (!table) {
        return;
    }

    nmo_cli_table_row_t *row = table->first_row;
    while (row) {
        nmo_cli_table_row_t *next = row->next;
        for (size_t i = 0; i < row->cell_count; ++i) {
            free(row->cells[i]);
        }
        free(row->cells);
        free(row);
        row = next;
    }

    free(table->computed_widths);
    memset(table, 0, sizeof(*table));
}

/* ============================================================================
 * Simple output utilities
 * ============================================================================ */

void nmo_cli_print_heading(FILE *out, const char *title, bool colorize) {
    if (!out || !title) {
        return;
    }
    if (colorize) {
        fprintf(out, "%s%s%s\n", NMO_CLI_COLOR_BOLD, title, NMO_CLI_COLOR_RESET);
    } else {
        fprintf(out, "%s\n", title);
    }
}

void nmo_cli_print_kv(FILE *out, const char *key, const char *value, int key_width, bool colorize) {
    if (!out) {
        return;
    }
    char *escaped = nmo_cli_escape_bytes(value ? value : "");
    if (colorize) {
        fprintf(out, "%s%-*s%s: %s\n",
                NMO_CLI_COLOR_CYAN,
                key_width, key ? key : "",
                NMO_CLI_COLOR_RESET,
                escaped);
    } else {
        fprintf(out, "%-*s: %s\n", key_width, key ? key : "", escaped);
    }
    free(escaped);
}

void nmo_cli_print_error(FILE *out, const char *format, ...) {
    if (!out || !format) {
        return;
    }
    fprintf(out, "Error: ");
    va_list args;
    va_start(args, format);
    vfprintf(out, format, args);
    va_end(args);
    fprintf(out, "\n");
}

void nmo_cli_print_warning(FILE *out, const char *format, ...) {
    if (!out || !format) {
        return;
    }
    fprintf(out, "Warning: ");
    va_list args;
    va_start(args, format);
    vfprintf(out, format, args);
    va_end(args);
    fprintf(out, "\n");
}

/* ============================================================================
 * Tree printing
 * ============================================================================ */

/**
 * Print tree recursively with proper box-drawing characters
 */
static void print_tree_recursive(FILE *out,
                                 const nmo_cli_tree_node_t *node,
                                 const char *prefix,
                                 bool is_last,
                                 bool colorize,
                                 nmo_cli_tree_render_fn render_fn) {
    if (!node || !out) {
        return;
    }

    /* Print connector */
    fprintf(out, "%s%s", prefix, is_last ? "`-- " : "|-- ");

    /* Print node label */
    if (render_fn) {
        render_fn(out, node, colorize);
    } else {
        fprintf(out, "%s", node->label ? node->label : "(null)");
    }
    fprintf(out, "\n");

    /* Build prefix for children */
    size_t prefix_len = strlen(prefix);
    char *child_prefix = (char *)malloc(prefix_len + 5);
    if (!child_prefix) {
        return;
    }
    snprintf(child_prefix, prefix_len + 5, "%s%s", prefix, is_last ? "    " : "|   ");

    /* Print children */
    for (nmo_cli_tree_node_t *child = node->first_child; child; child = child->next_sibling) {
        bool child_is_last = (child->next_sibling == NULL);
        print_tree_recursive(out, child, child_prefix, child_is_last, colorize, render_fn);
    }

    free(child_prefix);
}

void nmo_cli_print_tree(const nmo_cli_tree_node_t *root,
                        FILE *out,
                        bool colorize,
                        nmo_cli_tree_render_fn render_fn) {
    if (!root || !out) {
        return;
    }

    /* Print root without connector */
    if (render_fn) {
        render_fn(out, root, colorize);
    } else {
        fprintf(out, "%s", root->label ? root->label : "(root)");
    }
    fprintf(out, "\n");

    /* Print children */
    for (nmo_cli_tree_node_t *child = root->first_child; child; child = child->next_sibling) {
        bool is_last = (child->next_sibling == NULL);
        print_tree_recursive(out, child, "", is_last, colorize, render_fn);
    }
}
