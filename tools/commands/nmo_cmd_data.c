/**
 * @file nmo_cmd_data.c
 * @brief CLI data array command group implementation
 */

#include "nmo_cmd_data.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "app/nmo_save.h"
#include "core/nmo_arena.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/nmo_object_enum_defs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Helpers
 * ============================================================================ */

static const char *arraytype_name(CK_ARRAYTYPE type) {
    switch (type) {
        case CKARRAYTYPE_INT:       return "int";
        case CKARRAYTYPE_FLOAT:     return "float";
        case CKARRAYTYPE_STRING:    return "string";
        case CKARRAYTYPE_OBJECT:    return "object";
        case CKARRAYTYPE_PARAMETER: return "parameter";
        default:                    return "unknown";
    }
}

static void format_cell(char *buf, size_t buf_size,
                        const nmo_dataarray_cell_t *cell,
                        CK_ARRAYTYPE type,
                        const nmo_cmd_ctx_t *c) {
    switch (type) {
        case CKARRAYTYPE_INT:
            snprintf(buf, buf_size, "%d", cell->int_value);
            break;
        case CKARRAYTYPE_FLOAT:
            snprintf(buf, buf_size, "%.6g", (double)cell->float_value);
            break;
        case CKARRAYTYPE_STRING:
            if (cell->string_value) {
                snprintf(buf, buf_size, "%s", cell->string_value);
            } else {
                snprintf(buf, buf_size, "(null)");
            }
            break;
        case CKARRAYTYPE_OBJECT: {
            nmo_object_t *obj = nmo_core_find_by_id(c, cell->object_id);
            if (obj) {
                const char *name = nmo_object_get_name(obj);
                if (name && name[0]) {
                    snprintf(buf, buf_size, "#%u (%s)", cell->object_id, name);
                } else {
                    snprintf(buf, buf_size, "#%u", cell->object_id);
                }
            } else {
                snprintf(buf, buf_size, "#%u", cell->object_id);
            }
            break;
        }
        case CKARRAYTYPE_PARAMETER:
            snprintf(buf, buf_size, "#%u", cell->parameter_id);
            break;
        default:
            snprintf(buf, buf_size, "?");
            break;
    }
}

static void add_cell_json(yyjson_mut_doc *doc, yyjson_mut_val *arr,
                          const nmo_dataarray_cell_t *cell,
                          CK_ARRAYTYPE type) {
    switch (type) {
        case CKARRAYTYPE_INT:
            yyjson_mut_arr_add_int(doc, arr, cell->int_value);
            break;
        case CKARRAYTYPE_FLOAT:
            yyjson_mut_arr_add_real(doc, arr, (double)cell->float_value);
            break;
        case CKARRAYTYPE_STRING:
            if (cell->string_value) {
                nmo_cli_json_add_str_safe_to_arr(doc, arr, cell->string_value);
            } else {
                yyjson_mut_arr_add_null(doc, arr);
            }
            break;
        case CKARRAYTYPE_OBJECT:
            yyjson_mut_arr_add_uint(doc, arr, cell->object_id);
            break;
        case CKARRAYTYPE_PARAMETER:
            yyjson_mut_arr_add_uint(doc, arr, cell->parameter_id);
            break;
        default:
            yyjson_mut_arr_add_null(doc, arr);
            break;
    }
}

/* ============================================================================
 * data list
 * ============================================================================ */

int nmo_cmd_data_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    (void)nmo_session_get_objects(c.session, &objects, &object_count);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        uint32_t found = 0;

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            if (!obj || nmo_object_get_class_id(obj) != NMO_CID_DATAARRAY) continue;

            nmo_dataarray_state_t *state =
                (nmo_dataarray_state_t *)nmo_object_get_data(obj);
            if (!state) continue;

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "id", nmo_object_get_id(obj));
            const char *name = nmo_object_get_name(obj);
            nmo_cli_json_add_str_safe(doc, item, "name",
                                      (name && name[0]) ? name : "");
            yyjson_mut_obj_add_uint(doc, item, "column_count", state->column_count);
            yyjson_mut_obj_add_uint(doc, item, "row_count", state->row_count);
            yyjson_mut_arr_add_val(arr, item);
            found++;
        }

        yyjson_mut_obj_add_uint(doc, data, "count", found);
        yyjson_mut_obj_add_val(doc, data, "arrays", arr);
        nmo_cmd_ctx_json_end(&c, doc, data, "data.list");
    } else {
        static const nmo_cli_table_col_t columns[] = {
            {"ID",      NMO_CLI_ALIGN_RIGHT, 6,  0},
            {"Name",    NMO_CLI_ALIGN_LEFT,  20, 60},
            {"Columns", NMO_CLI_ALIGN_RIGHT, 7,  0},
            {"Rows",    NMO_CLI_ALIGN_RIGHT, 6,  0},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));
        uint32_t found = 0;

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            if (!obj || nmo_object_get_class_id(obj) != NMO_CID_DATAARRAY) continue;

            nmo_dataarray_state_t *state =
                (nmo_dataarray_state_t *)nmo_object_get_data(obj);
            if (!state) continue;

            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

            const char *name = nmo_object_get_name(obj);
            if (!name || !name[0]) name = "-";

            char col_buf[16];
            snprintf(col_buf, sizeof(col_buf), "%u", state->column_count);

            char row_buf[16];
            snprintf(row_buf, sizeof(row_buf), "%u", state->row_count);

            const char *cells[] = {id_buf, name, col_buf, row_buf};
            nmo_cli_table_add_row(&table, cells, 4);
            found++;
        }

        fprintf(c.out, "Data arrays: %u\n\n", found);
        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * data show
 * ============================================================================ */

int nmo_cmd_data_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--id", "-i", NMO_OPT_STRING, "Object ID"},
    };
    nmo_opt_val_t vals[1];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *id_str = vals[0].present ? vals[0].val.str : NULL;

    /* Positional: <id> <file> */
    if (!id_str && r.pos_count >= 2) {
        id_str = r.pos_args[0];
    }

    if (!id_str) {
        fprintf(stderr, "Error: No object ID specified\n");
        fprintf(stderr, "Usage: nmo data show <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t obj_id;
    if (!nmo_tool_parse_u32_dec(id_str, &obj_id)) {
        fprintf(stderr, "Error: Invalid object ID '%s'\n", id_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_t *obj = nmo_core_find_by_id(&c, obj_id);
    if (!obj) {
        fprintf(stderr, "Error: Object %u not found\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_NOT_FOUND);
    }
    if (nmo_object_get_class_id(obj) != NMO_CID_DATAARRAY) {
        fprintf(stderr, "Error: Object %u is not a CKDataArray\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_dataarray_state_t *state =
        (nmo_dataarray_state_t *)nmo_object_get_data(obj);
    if (!state) {
        fprintf(stderr, "Error: No data for object %u\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    const char *name = nmo_object_get_name(obj);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "id", obj_id);
        nmo_cli_json_add_str_safe(doc, data, "name",
                                  (name && name[0]) ? name : "");
        yyjson_mut_obj_add_uint(doc, data, "column_count", state->column_count);
        yyjson_mut_obj_add_uint(doc, data, "row_count", state->row_count);
        yyjson_mut_obj_add_int(doc, data, "sort_order", state->order);
        yyjson_mut_obj_add_uint(doc, data, "sort_column", state->column_index);
        yyjson_mut_obj_add_int(doc, data, "key_column", state->key_column);

        yyjson_mut_val *cols = yyjson_mut_arr(doc);
        for (uint32_t i = 0; i < state->column_count; ++i) {
            yyjson_mut_val *col = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, col, "index", i);
            nmo_cli_json_add_str_safe(doc, col, "name",
                state->column_formats[i].name ? state->column_formats[i].name : "");
            yyjson_mut_obj_add_str(doc, col, "type",
                arraytype_name(state->column_formats[i].type));
            yyjson_mut_arr_add_val(cols, col);
        }
        yyjson_mut_obj_add_val(doc, data, "columns", cols);

        nmo_cmd_ctx_json_end(&c, doc, data, "data.show");
    } else {
        nmo_cli_print_heading(c.out, "Data Array", c.colorize);

        char id_buf[16];
        snprintf(id_buf, sizeof(id_buf), "%u", obj_id);
        nmo_cli_print_kv(c.out, "ID", id_buf, 14, c.colorize);
        nmo_cli_print_kv(c.out, "Name",
                         (name && name[0]) ? name : "-", 14, c.colorize);

        char col_buf[16];
        snprintf(col_buf, sizeof(col_buf), "%u", state->column_count);
        nmo_cli_print_kv(c.out, "Columns", col_buf, 14, c.colorize);

        char row_buf[16];
        snprintf(row_buf, sizeof(row_buf), "%u", state->row_count);
        nmo_cli_print_kv(c.out, "Rows", row_buf, 14, c.colorize);

        char order_buf[32];
        const char *order_label = "none";
        if (state->order == 1) order_label = "ascending";
        else if (state->order == 2) order_label = "descending";
        snprintf(order_buf, sizeof(order_buf), "%s (column %u)",
                 order_label, state->column_index);
        nmo_cli_print_kv(c.out, "Sort Order", order_buf, 14, c.colorize);

        char key_buf[16];
        if (state->key_column >= 0) {
            snprintf(key_buf, sizeof(key_buf), "%d", state->key_column);
        } else {
            snprintf(key_buf, sizeof(key_buf), "none");
        }
        nmo_cli_print_kv(c.out, "Key Column", key_buf, 14, c.colorize);

        /* Column schema table */
        if (state->column_count > 0) {
            fprintf(c.out, "\n");

            static const nmo_cli_table_col_t col_defs[] = {
                {"Index", NMO_CLI_ALIGN_RIGHT, 5,  0},
                {"Name",  NMO_CLI_ALIGN_LEFT,  20, 60},
                {"Type",  NMO_CLI_ALIGN_LEFT,  10, 0},
            };

            nmo_cli_table_t table;
            nmo_cli_table_init(&table, col_defs,
                               sizeof(col_defs) / sizeof(col_defs[0]));

            for (uint32_t i = 0; i < state->column_count; ++i) {
                char idx_buf[16];
                snprintf(idx_buf, sizeof(idx_buf), "%u", i);

                const char *cname = state->column_formats[i].name;
                if (!cname || !cname[0]) cname = "-";

                const char *tname = arraytype_name(state->column_formats[i].type);

                const char *cells[] = {idx_buf, cname, tname};
                nmo_cli_table_add_row(&table, cells, 3);
            }

            nmo_cli_table_print(&table, c.out, c.colorize);
            nmo_cli_table_free(&table);
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * data dump
 * ============================================================================ */

int nmo_cmd_data_dump(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--id",  "-i", NMO_OPT_STRING, "Object ID"},
        {"--row", "-r", NMO_OPT_STRING, "Dump single row by index"},
    };
    nmo_opt_val_t vals[2];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 2, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *id_str = vals[0].present ? vals[0].val.str : NULL;
    const char *row_str = vals[1].present ? vals[1].val.str : NULL;

    /* Positional: <id> <file> */
    if (!id_str && r.pos_count >= 2) {
        id_str = r.pos_args[0];
    }

    if (!id_str) {
        fprintf(stderr, "Error: No object ID specified\n");
        fprintf(stderr, "Usage: nmo data dump <id> <file> [--row <n>]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t obj_id;
    if (!nmo_tool_parse_u32_dec(id_str, &obj_id)) {
        fprintf(stderr, "Error: Invalid object ID '%s'\n", id_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t single_row = 0;
    bool has_single_row = false;
    if (row_str) {
        if (!nmo_tool_parse_u32_dec(row_str, &single_row)) {
            fprintf(stderr, "Error: Invalid row index '%s'\n", row_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        has_single_row = true;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_t *obj = nmo_core_find_by_id(&c, obj_id);
    if (!obj) {
        fprintf(stderr, "Error: Object %u not found\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_NOT_FOUND);
    }
    if (nmo_object_get_class_id(obj) != NMO_CID_DATAARRAY) {
        fprintf(stderr, "Error: Object %u is not a CKDataArray\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_dataarray_state_t *state =
        (nmo_dataarray_state_t *)nmo_object_get_data(obj);
    if (!state) {
        fprintf(stderr, "Error: No data for object %u\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    if (has_single_row && single_row >= state->row_count) {
        fprintf(stderr, "Error: Row %u out of range (row_count=%u)\n",
                single_row, state->row_count);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        /* Column names */
        yyjson_mut_val *col_names = yyjson_mut_arr(doc);
        for (uint32_t ci = 0; ci < state->column_count; ++ci) {
            const char *cname = state->column_formats[ci].name;
            nmo_cli_json_add_str_safe_to_arr(doc, col_names,
                                             cname ? cname : "");
        }
        yyjson_mut_obj_add_val(doc, data, "columns", col_names);

        /* Rows */
        yyjson_mut_val *rows_arr = yyjson_mut_arr(doc);
        uint32_t row_start = has_single_row ? single_row : 0;
        uint32_t row_end = has_single_row ? single_row + 1 : state->row_count;

        for (uint32_t ri = row_start; ri < row_end; ++ri) {
            const nmo_dataarray_row_t *row = &state->rows[ri];
            yyjson_mut_val *row_arr = yyjson_mut_arr(doc);
            for (uint32_t ci = 0; ci < state->column_count && ci < row->column_count; ++ci) {
                add_cell_json(doc, row_arr, &row->cells[ci],
                              state->column_formats[ci].type);
            }
            yyjson_mut_arr_add_val(rows_arr, row_arr);
        }
        yyjson_mut_obj_add_val(doc, data, "rows", rows_arr);

        nmo_cmd_ctx_json_end(&c, doc, data, "data.dump");
    } else if (has_single_row) {
        /* Single row: key-value format */
        const nmo_dataarray_row_t *row = &state->rows[single_row];
        fprintf(c.out, "Row %u:\n", single_row);

        for (uint32_t ci = 0; ci < state->column_count && ci < row->column_count; ++ci) {
            const char *cname = state->column_formats[ci].name;
            if (!cname || !cname[0]) cname = "(unnamed)";

            char val_buf[256];
            format_cell(val_buf, sizeof(val_buf), &row->cells[ci],
                        state->column_formats[ci].type, &c);
            nmo_cli_print_kv(c.out, cname, val_buf, 20, c.colorize);
        }
    } else {
        /* All rows: table format */

        /* Build dynamic column definitions */
        size_t ncols = state->column_count;
        if (ncols == 0) {
            fprintf(c.out, "(no columns)\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
        }

        /* Use stack allocation for reasonable column counts, heap for large */
        nmo_cli_table_col_t col_defs_stack[32];
        nmo_cli_table_col_t *col_defs = col_defs_stack;
        if (ncols > 32) {
            col_defs = (nmo_cli_table_col_t *)malloc(
                ncols * sizeof(nmo_cli_table_col_t));
            if (!col_defs) {
                fprintf(stderr, "Error: Out of memory\n");
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
            }
        }

        for (size_t ci = 0; ci < ncols; ++ci) {
            const char *cname = state->column_formats[ci].name;
            col_defs[ci].header = (cname && cname[0]) ? cname : "-";
            col_defs[ci].align = (state->column_formats[ci].type == CKARRAYTYPE_INT ||
                                  state->column_formats[ci].type == CKARRAYTYPE_FLOAT)
                                 ? NMO_CLI_ALIGN_RIGHT : NMO_CLI_ALIGN_LEFT;
            col_defs[ci].min_width = 8;
            col_defs[ci].max_width = 40;
        }

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, col_defs, ncols);

        /* Temporary cell string array */
        const char *cell_strs_stack[32];
        const char **cell_strs = cell_strs_stack;
        char (*cell_bufs_stack)[256] = NULL;
        char (*cell_bufs)[256] = NULL;

        /* Allocate buffers for cell formatting */
        cell_bufs = (char (*)[256])malloc(ncols * 256);
        if (!cell_bufs) {
            fprintf(stderr, "Error: Out of memory\n");
            nmo_cli_table_free(&table);
            if (col_defs != col_defs_stack) free(col_defs);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        (void)cell_bufs_stack;

        if (ncols > 32) {
            cell_strs = (const char **)malloc(ncols * sizeof(const char *));
            if (!cell_strs) {
                fprintf(stderr, "Error: Out of memory\n");
                free(cell_bufs);
                nmo_cli_table_free(&table);
                if (col_defs != col_defs_stack) free(col_defs);
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
            }
        }

        for (uint32_t ri = 0; ri < state->row_count; ++ri) {
            const nmo_dataarray_row_t *row = &state->rows[ri];
            for (size_t ci = 0; ci < ncols; ++ci) {
                if (ci < row->column_count) {
                    format_cell(cell_bufs[ci], 256, &row->cells[ci],
                                state->column_formats[ci].type, &c);
                } else {
                    snprintf(cell_bufs[ci], 256, "-");
                }
                cell_strs[ci] = cell_bufs[ci];
            }
            nmo_cli_table_add_row(&table, cell_strs, ncols);
        }

        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);

        free(cell_bufs);
        if (cell_strs != cell_strs_stack) free((void *)cell_strs);
        if (col_defs != col_defs_stack) free(col_defs);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * data set-cell
 * ============================================================================ */

/**
 * Parse a value string and write it into a cell according to column type.
 * For string type, allocates from the arena.
 * Returns true on success, false on parse error.
 */
static bool parse_cell_value(const char *value_str,
                             CK_ARRAYTYPE type,
                             nmo_dataarray_cell_t *cell,
                             nmo_arena_t *arena) {
    switch (type) {
    case CKARRAYTYPE_INT: {
        char *end = NULL;
        long v = strtol(value_str, &end, 0);
        if (!end || *end != '\0') return false;
        cell->int_value = (int32_t)v;
        return true;
    }
    case CKARRAYTYPE_FLOAT: {
        char *end = NULL;
        double v = strtod(value_str, &end);
        if (!end || *end != '\0') return false;
        cell->float_value = (float)v;
        return true;
    }
    case CKARRAYTYPE_STRING: {
        size_t len = strlen(value_str);
        char *copy = (char *)nmo_arena_alloc(arena, len + 1, 1);
        if (!copy) return false;
        memcpy(copy, value_str, len + 1);
        cell->string_value = copy;
        return true;
    }
    case CKARRAYTYPE_OBJECT: {
        /* Accept #<id> or plain <id> */
        const char *s = value_str;
        if (s[0] == '#') s++;
        char *end = NULL;
        unsigned long v = strtoul(s, &end, 10);
        if (!end || *end != '\0') return false;
        cell->object_id = (nmo_object_id_t)v;
        return true;
    }
    case CKARRAYTYPE_PARAMETER: {
        const char *s = value_str;
        if (s[0] == '#') s++;
        char *end = NULL;
        unsigned long v = strtoul(s, &end, 10);
        if (!end || *end != '\0') return false;
        cell->parameter_id = (nmo_object_id_t)v;
        return true;
    }
    default:
        return false;
    }
}

int nmo_cmd_data_set_cell(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o", NMO_OPT_STRING, "Output file (required unless --dry-run)"},
        {"--row",     "-r", NMO_OPT_UINT,   "Row index (0-based)"},
        {"--col",     "-c", NMO_OPT_UINT,   "Column index (0-based)"},
        {"--value",   "-v", NMO_OPT_STRING, "New cell value"},
        {"--dry-run", NULL,  NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_ROW, OPT_COL, OPT_VALUE, OPT_DRYRUN, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool has_row   = vals[OPT_ROW].present;
    uint32_t row   = has_row ? vals[OPT_ROW].val.u : 0;
    bool has_col   = vals[OPT_COL].present;
    uint32_t col   = has_col ? vals[OPT_COL].val.u : 0;
    const char *value_str = vals[OPT_VALUE].present ? vals[OPT_VALUE].val.str : NULL;
    bool dry_run   = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;

    /* Positional: <id> <file> */
    const char *id_str = NULL;
    const char *file_path = NULL;
    if (r.pos_count >= 2) {
        id_str = r.pos_args[0];
        file_path = r.pos_args[r.pos_count - 1];
    } else if (r.pos_count == 1) {
        id_str = r.pos_args[0];
    }

    if (!id_str) {
        fprintf(stderr, "Error: No object ID specified\n");
        fprintf(stderr, "Usage: nmo data set-cell <id> --row <r> --col <c> --value <val> <file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!has_row || !has_col || !value_str) {
        fprintf(stderr, "Error: --row, --col, and --value are required\n");
        fprintf(stderr, "Usage: nmo data set-cell <id> --row <r> --col <c> --value <val> <file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!dry_run && !output_path) {
        fprintf(stderr, "Error: -o/--output is required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t obj_id;
    if (!nmo_tool_parse_u32_dec(id_str, &obj_id)) {
        fprintf(stderr, "Error: Invalid object ID '%s'\n", id_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session with explicit file path */
    nmo_cmd_ctx_t c;
    int rc;
    if (file_path) {
        rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    } else {
        rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    }
    if (rc) return rc;

    /* Find object */
    nmo_object_t *obj = nmo_core_find_by_id(&c, obj_id);
    if (!obj) {
        fprintf(stderr, "Error: Object %u not found\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_NOT_FOUND);
    }
    if (nmo_object_get_class_id(obj) != NMO_CID_DATAARRAY) {
        fprintf(stderr, "Error: Object %u is not a CKDataArray\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_dataarray_state_t *state =
        (nmo_dataarray_state_t *)nmo_object_get_data(obj);
    if (!state) {
        fprintf(stderr, "Error: No data for object %u\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Validate bounds */
    if (row >= state->row_count) {
        fprintf(stderr, "Error: Row %u out of range (row_count=%u)\n",
                row, state->row_count);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }
    if (col >= state->column_count) {
        fprintf(stderr, "Error: Column %u out of range (column_count=%u)\n",
                col, state->column_count);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    /* Get column type */
    CK_ARRAYTYPE col_type = state->column_formats[col].type;
    const char *col_name = state->column_formats[col].name;
    if (!col_name || !col_name[0]) col_name = "(unnamed)";

    /* Format old value */
    nmo_dataarray_row_t *target_row = &state->rows[row];
    if (col >= target_row->column_count) {
        fprintf(stderr, "Error: Row %u has only %u cells (column %u requested)\n",
                row, target_row->column_count, col);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    char old_buf[256];
    format_cell(old_buf, sizeof(old_buf), &target_row->cells[col], col_type, &c);

    /* Parse new value */
    nmo_arena_t *arena = nmo_session_get_arena(c.session);
    nmo_dataarray_cell_t new_cell;
    memset(&new_cell, 0, sizeof(new_cell));

    if (!parse_cell_value(value_str, col_type, &new_cell, arena)) {
        fprintf(stderr, "Error: Cannot parse '%s' as %s\n",
                value_str, arraytype_name(col_type));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    /* Write the cell */
    target_row->cells[col] = new_cell;

    /* Format new value for display */
    char new_buf[256];
    format_cell(new_buf, sizeof(new_buf), &target_row->cells[col], col_type, &c);

    /* Output */
    const char *name = nmo_object_get_name(obj);
    int exit_code = NMO_CLI_EXIT_SUCCESS;

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        if (!doc) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, data, "id", obj_id);
        nmo_cli_json_add_str_safe(doc, data, "name",
                                  (name && name[0]) ? name : "");
        yyjson_mut_obj_add_uint(doc, data, "row", row);
        yyjson_mut_obj_add_uint(doc, data, "col", col);
        nmo_cli_json_add_str_safe(doc, data, "column_name", col_name);
        yyjson_mut_obj_add_str(doc, data, "column_type", arraytype_name(col_type));
        nmo_cli_json_add_str_safe(doc, data, "old_value", old_buf);
        nmo_cli_json_add_str_safe(doc, data, "new_value", new_buf);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        if (!dry_run && output_path)
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);

        nmo_cmd_ctx_json_end(&c, doc, data, "data.set-cell");
    } else {
        fprintf(c.out, "Data array #%u", obj_id);
        if (name && name[0]) fprintf(c.out, " (%s)", name);
        fprintf(c.out, "\n");
        fprintf(c.out, "  Cell:  [%u,%u] (column '%s', type %s)\n",
                row, col, col_name, arraytype_name(col_type));
        fprintf(c.out, "  Old:   %s\n", old_buf);
        fprintf(c.out, "  New:   %s\n", new_buf);

        if (dry_run) {
            fprintf(c.out, "  (dry run - not saved)\n");
        }
    }

    /* Save */
    if (!dry_run && output_path) {
        nmo_save_options_t save_opts = nmo_save_options_default();
        int save_rc = nmo_save_file(c.session, output_path, &save_opts);
        if (save_rc != NMO_OK) {
            fprintf(stderr, "Error saving file: %s\n", nmo_error_string(save_rc));
            exit_code = NMO_CLI_EXIT_IO_ERROR;
        } else if (!c.is_json) {
            fprintf(c.out, "Saved to: %s\n", output_path);
        }
    }

    return nmo_cmd_ctx_done(&c, exit_code);
}
