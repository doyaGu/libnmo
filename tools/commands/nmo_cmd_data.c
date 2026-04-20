/**
 * @file nmo_cmd_data.c
 * @brief CLI data array command group implementation
 */

#include "nmo_cmd_data.h"
#include "nmo_cmd_object_internal.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_write.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "core/nmo_arena.h"
#include "core/nmo_parse.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/nmo_object_enum_defs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int nmo_cmd_data_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: data list|show|dump ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "list") == 0 || strcmp(argv[0], "ls") == 0) {
        return nmo_cmd_object_list_class_in_session(ctx, argc, argv, "CKDataArray");
    }
    if (strcmp(argv[0], "show") == 0 || strcmp(argv[0], "s") == 0 ||
        strcmp(argv[0], "dump") == 0 || strcmp(argv[0], "d") == 0) {
        return nmo_cmd_object_show_class_in_session(
            ctx, argc, argv, NMO_CID_DATAARRAY, "CKDataArray");
    }

    fprintf(stderr, "Unsupported data read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
}

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

static bool is_parameter_reference_class(nmo_class_id_t class_id) {
    return class_id == NMO_CID_PARAMETER ||
           class_id == NMO_CID_PARAMETERIN ||
           class_id == NMO_CID_PARAMETEROUT ||
           class_id == NMO_CID_PARAMETERLOCAL ||
           class_id == NMO_CID_PARAMETEROPERATION;
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

static int validate_dataarray_reference_value(
    const nmo_cmd_ctx_t *c,
    CK_ARRAYTYPE col_type,
    const char *value_str)
{
    if (col_type != CKARRAYTYPE_OBJECT && col_type != CKARRAYTYPE_PARAMETER) {
        return NMO_CLI_EXIT_SUCCESS;
    }

    nmo_object_id_t ref_id = 0;
    if (nmo_parse_object_id(value_str, &ref_id) != NMO_OK) {
        return NMO_CLI_EXIT_SUCCESS;
    }
    if (ref_id == 0) {
        return NMO_CLI_EXIT_SUCCESS;
    }

    nmo_object_t *ref = nmo_core_find_by_id(c, ref_id);
    if (ref == NULL) {
        fprintf(stderr, "Error: Referenced %s #%u not found\n",
                col_type == CKARRAYTYPE_PARAMETER ? "parameter" : "object",
                ref_id);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    (void)c;
    if (col_type == CKARRAYTYPE_PARAMETER &&
        !is_parameter_reference_class(nmo_object_get_class_id(ref))) {
        fprintf(stderr, "Error: Referenced parameter #%u not found\n", ref_id);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    return NMO_CLI_EXIT_SUCCESS;
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

typedef struct data_list_json_data {
    yyjson_mut_doc *doc;
    yyjson_mut_val *arr;
    uint32_t found;
} data_list_json_data_t;

typedef struct data_list_table_data {
    nmo_cli_table_t *table;
    uint32_t found;
} data_list_table_data_t;

static int data_list_json_visitor(size_t index,
                                  nmo_object_t *obj,
                                  const nmo_cmd_ctx_t *c,
                                  void *user)
{
    (void)index;
    (void)c;
    data_list_json_data_t *data = (data_list_json_data_t *)user;
    if (obj == NULL || data == NULL || data->doc == NULL || data->arr == NULL) {
        return 0;
    }

    const nmo_dataarray_state_t *state =
        (const nmo_dataarray_state_t *)nmo_object_get_state(obj);
    if (state == NULL) {
        return 0;
    }

    yyjson_mut_doc *doc = data->doc;
    yyjson_mut_val *item = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, item, "id", nmo_object_get_id(obj));
    const char *name = nmo_object_get_name(obj);
    nmo_cli_json_add_str_safe(doc, item, "name",
                              (name && name[0]) ? name : "");
    yyjson_mut_obj_add_uint(doc, item, "column_count", state->column_count);
    yyjson_mut_obj_add_uint(doc, item, "row_count", state->row_count);
    yyjson_mut_arr_add_val(data->arr, item);
    data->found++;
    return 0;
}

static int data_list_table_visitor(size_t index,
                                   nmo_object_t *obj,
                                   const nmo_cmd_ctx_t *c,
                                   void *user)
{
    (void)index;
    (void)c;
    data_list_table_data_t *data = (data_list_table_data_t *)user;
    if (obj == NULL || data == NULL || data->table == NULL) {
        return 0;
    }

    const nmo_dataarray_state_t *state =
        (const nmo_dataarray_state_t *)nmo_object_get_state(obj);
    if (state == NULL) {
        return 0;
    }

    char id_buf[16];
    snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

    const char *name = nmo_object_get_name(obj);
    if (!name || !name[0]) name = "-";

    char col_buf[16];
    snprintf(col_buf, sizeof(col_buf), "%u", state->column_count);

    char row_buf[16];
    snprintf(row_buf, sizeof(row_buf), "%u", state->row_count);

    const char *cells[] = {id_buf, name, col_buf, row_buf};
    nmo_cli_table_add_row(data->table, cells, 4);
    data->found++;
    return 0;
}

/* ============================================================================
 * data list
 * ============================================================================ */

int nmo_cmd_data_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_query_t query = {0};
    nmo_core_query_set_class_id(&query, NMO_CID_DATAARRAY, false);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        data_list_json_data_t jd = { .doc = doc, .arr = arr };
        rc = nmo_core_object_query_run(&c, &query,
                                       data_list_json_visitor, &jd, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return nmo_cmd_ctx_done(&c, rc);
        }

        yyjson_mut_obj_add_uint(doc, data, "count", jd.found);
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
        data_list_table_data_t td = { .table = &table };
        rc = nmo_core_object_query_run(&c, &query,
                                       data_list_table_visitor, &td, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            nmo_cli_table_free(&table);
            return nmo_cmd_ctx_done(&c, rc);
        }

        fprintf(c.out, "Data arrays: %u\n\n", td.found);
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
        {"--id",   "-i", NMO_OPT_UINT,   "Data array object ID"},
        {"--name", "-n", NMO_OPT_STRING, "Data array object name"},
    };
    enum { OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = (!has_selector_opt && r.pos_count >= 2) ? r.pos_args[0] : NULL;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_core_object_selector_t selector = {
        .has_id = vals[OPT_ID].present,
        .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
        .positional_id = positional_id,
        .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
        .required_base_class = NMO_CID_DATAARRAY,
        .selector_label = "Data array",
        .type_label = "CKDataArray",
    };
    nmo_object_t *obj = NULL;
    nmo_object_id_t obj_id = 0;
    rc = nmo_core_resolve_one_object(&c, &selector, &obj, &obj_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo data show [--id <id> | --name <name> | <id>] <file>\n");
        return nmo_cmd_ctx_done(&c, rc);
    }

    nmo_dataarray_state_t *state =
        (nmo_dataarray_state_t *)nmo_object_get_state(obj);
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
        {"--id",   "-i", NMO_OPT_UINT,   "Data array object ID"},
        {"--name", "-n", NMO_OPT_STRING, "Data array object name"},
        {"--row",  "-r", NMO_OPT_STRING, "Dump single row by index"},
    };
    enum { OPT_ID, OPT_NAME, OPT_ROW, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *row_str = vals[OPT_ROW].present ? vals[OPT_ROW].val.str : NULL;
    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = (!has_selector_opt && r.pos_count >= 2) ? r.pos_args[0] : NULL;

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

    nmo_core_object_selector_t selector = {
        .has_id = vals[OPT_ID].present,
        .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
        .positional_id = positional_id,
        .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
        .required_base_class = NMO_CID_DATAARRAY,
        .selector_label = "Data array",
        .type_label = "CKDataArray",
    };
    nmo_object_t *obj = NULL;
    nmo_object_id_t obj_id = 0;
    rc = nmo_core_resolve_one_object(&c, &selector, &obj, &obj_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo data dump [--id <id> | --name <name> | <id>] <file> [--row <n>]\n");
        return nmo_cmd_ctx_done(&c, rc);
    }

    nmo_dataarray_state_t *state =
        (nmo_dataarray_state_t *)nmo_object_get_state(obj);
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
        const char *name = nmo_object_get_name(obj);
        fprintf(c.out, "Data array #%u", obj_id);
        if (name && name[0]) fprintf(c.out, " (%s)", name);
        fprintf(c.out, "\n");
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

typedef struct data_set_cell_args {
    nmo_core_object_selector_t selector;
    uint32_t obj_id;
    uint32_t row;
    uint32_t col;
    const char *value_str;
    const char *name;
    const char *col_name;
    const char *col_type_name;
    char old_buf[256];
    char new_buf[256];
} data_set_cell_args_t;

static int data_set_cell_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    data_set_cell_args_t *args = (data_set_cell_args_t *)user_data;
    if (c == NULL || args == NULL || args->value_str == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_object_t *obj = NULL;
    nmo_object_id_t obj_id = 0;
    int resolve_rc = nmo_core_resolve_one_object(c, &args->selector, &obj, &obj_id);
    if (resolve_rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo data set-cell [--id <id> | --name <name> | <id>] --row <r> --col <c> --value <val> <file> -o <output>\n");
        return resolve_rc;
    }
    args->obj_id = obj_id;

    nmo_dataarray_state_t *state =
        (nmo_dataarray_state_t *)nmo_object_get_state(obj);
    if (!state) {
        fprintf(stderr, "Error: No data for object %u\n", args->obj_id);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (args->row >= state->row_count) {
        fprintf(stderr, "Error: Row %u out of range (row_count=%u)\n",
                args->row, state->row_count);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (args->col >= state->column_count) {
        fprintf(stderr, "Error: Column %u out of range (column_count=%u)\n",
                args->col, state->column_count);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    CK_ARRAYTYPE col_type = state->column_formats[args->col].type;
    args->col_type_name = arraytype_name(col_type);
    args->col_name = state->column_formats[args->col].name;
    if (!args->col_name || !args->col_name[0]) args->col_name = "(unnamed)";

    nmo_dataarray_row_t *target_row = &state->rows[args->row];
    if (args->col >= target_row->column_count) {
        fprintf(stderr, "Error: Row %u has only %u cells (column %u requested)\n",
                args->row, target_row->column_count, args->col);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    format_cell(args->old_buf, sizeof(args->old_buf),
                &target_row->cells[args->col], col_type, c);

    int ref_rc = validate_dataarray_reference_value(c, col_type, args->value_str);
    if (ref_rc != NMO_CLI_EXIT_SUCCESS) {
        return ref_rc;
    }

    nmo_session_edit_t *edit = NULL;
    nmo_status_t set_rc = nmo_session_edit_begin(c->session, "data set-cell", &edit);
    if (set_rc == NMO_OK) {
        set_rc = nmo_session_edit_set_dataarray_cell(
            edit, args->obj_id, args->row, args->col, args->value_str);
    }
    if (set_rc != NMO_OK) {
        if (edit) {
            nmo_session_edit_rollback(edit);
        }
        fprintf(stderr, "Error: Cannot parse '%s' as %s\n",
                args->value_str, args->col_type_name);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    format_cell(args->new_buf, sizeof(args->new_buf),
                &target_row->cells[args->col], col_type, c);

    if (dry_run) {
        nmo_session_edit_rollback(edit);
    } else {
        nmo_status_t commit_rc = nmo_session_edit_commit(edit);
        if (commit_rc != NMO_OK) {
            fprintf(stderr, "Error: Failed to commit edit: %s\n",
                    nmo_error_string(commit_rc));
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    }

    args->name = nmo_object_get_name(obj);
    return NMO_CLI_EXIT_SUCCESS;
}

static int data_set_cell_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    data_set_cell_args_t *args = (data_set_cell_args_t *)user_data;
    if (c == NULL || args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        if (!doc) return NMO_CLI_EXIT_INTERNAL_ERROR;

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, data, "id", args->obj_id);
        nmo_cli_json_add_str_safe(doc, data, "name",
                                  (args->name && args->name[0]) ? args->name : "");
        yyjson_mut_obj_add_uint(doc, data, "row", args->row);
        yyjson_mut_obj_add_uint(doc, data, "col", args->col);
        nmo_cli_json_add_str_safe(doc, data, "column_name", args->col_name);
        yyjson_mut_obj_add_str(doc, data, "column_type", args->col_type_name);
        nmo_cli_json_add_str_safe(doc, data, "old_value", args->old_buf);
        nmo_cli_json_add_str_safe(doc, data, "new_value", args->new_buf);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        if (!dry_run && output_path)
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);

        nmo_cmd_ctx_json_end(c, doc, data, "data.set-cell");
    } else {
        fprintf(c->out, "Data array #%u", args->obj_id);
        if (args->name && args->name[0]) fprintf(c->out, " (%s)", args->name);
        fprintf(c->out, "\n");
        fprintf(c->out, "  Cell:  [%u,%u] (column '%s', type %s)\n",
                args->row, args->col, args->col_name, args->col_type_name);
        fprintf(c->out, "  Old:   %s\n", args->old_buf);
        fprintf(c->out, "  New:   %s\n", args->new_buf);

        if (dry_run) {
            fprintf(c->out, "  (dry run - not saved)\n");
        } else if (output_path) {
            fprintf(c->out, "Saved to: %s\n", output_path);
        }
    }

    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_data_set_cell(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o", NMO_OPT_STRING, "Output file (required unless --dry-run)"},
        {"--row",     "-r", NMO_OPT_UINT,   "Row index (0-based)"},
        {"--col",     "-c", NMO_OPT_UINT,   "Column index (0-based)"},
        {"--value",   "-v", NMO_OPT_STRING, "New cell value"},
        {"--dry-run", NULL,  NMO_OPT_FLAG,   "Preview without saving"},
        {"--id",      NULL,  NMO_OPT_UINT,   "Data array object ID"},
        {"--name",    "-n",  NMO_OPT_STRING, "Data array object name"},
    };
    enum { OPT_OUTPUT, OPT_ROW, OPT_COL, OPT_VALUE, OPT_DRYRUN,
           OPT_ID, OPT_NAME, OPT_COUNT };

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

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = NULL;
    const char *file_path = NULL;
    if (has_selector_opt) {
        if (r.pos_count >= 1) {
            file_path = r.pos_args[r.pos_count - 1];
        }
    } else if (r.pos_count >= 2) {
        positional_id = r.pos_args[0];
        file_path = r.pos_args[r.pos_count - 1];
    }

    if (!has_selector_opt && positional_id == NULL) {
        fprintf(stderr, "Error: No data array selector specified\n");
        fprintf(stderr, "Usage: nmo data set-cell [--id <id> | --name <name> | <id>] --row <r> --col <c> --value <val> <file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!has_row || !has_col || !value_str) {
        fprintf(stderr, "Error: --row, --col, and --value are required\n");
        fprintf(stderr, "Usage: nmo data set-cell [--id <id> | --name <name> | <id>] --row <r> --col <c> --value <val> <file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!file_path) {
        fprintf(stderr, "Error: No input file specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    data_set_cell_args_t args = {
        .selector = {
            .has_id = vals[OPT_ID].present,
            .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
            .positional_id = positional_id,
            .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
            .required_base_class = NMO_CID_DATAARRAY,
            .selector_label = "Data array",
            .type_label = "CKDataArray",
        },
        .row = row,
        .col = col,
        .value_str = value_str,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "data.set-cell",
        .output_required_unless_dry_run = true,
    };
    return nmo_cli_run_write_command(
        file_path,
        output_path,
        dry_run,
        global,
        &spec,
        data_set_cell_mutate,
        data_set_cell_report,
        &args);
}
