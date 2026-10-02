/**
 * @file nmo_cmd_data.c
 * @brief CLI data array command group implementation
 */

#include "nmo_cmd_data.h"
#include "nmo_cmd_object_internal.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_record.h"
#include "../nmo_edit_report_json.h"
#include "../nmo_cli_write.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "edit/nmo_edit_plan.h"
#include "object/nmo_context.h"
#include "core/nmo_arena.h"
#include "core/nmo_parse.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
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

/* malloc'd text for one cell (free with free()); NULL only on OOM. */
static char *format_cell_dup(const nmo_dataarray_cell_t *cell,
                             CK_ARRAYTYPE type,
                             const nmo_cmd_ctx_t *c) {
    switch (type) {
        case CKARRAYTYPE_INT:
            return nmo_tool_strdup_fmt("%d", cell->int_value);
        case CKARRAYTYPE_FLOAT:
            return nmo_tool_strdup_fmt("%.6g", (double)cell->float_value);
        case CKARRAYTYPE_STRING:
            return nmo_tool_strdup(cell->string_value ? cell->string_value : "(null)");
        case CKARRAYTYPE_OBJECT: {
            const nmo_object_id_t runtime_id =
                nmo_ref_runtime_id(&cell->object_ref);
            const nmo_object_id_t display_id =
                nmo_ref_serialized_id(&cell->object_ref);
            nmo_object_t *obj = nmo_core_find_by_id(c, runtime_id);
            const char *name = obj ? nmo_object_get_name(obj) : NULL;
            if (name && name[0]) {
                return nmo_tool_strdup_fmt("#%u (%s)", display_id, name);
            }
            return nmo_tool_strdup_fmt("#%u", display_id);
        }
        case CKARRAYTYPE_PARAMETER:
            return nmo_tool_strdup_fmt("#%u",
                                       nmo_ref_serialized_id(&cell->parameter.ref));
        default:
            return nmo_tool_strdup("?");
    }
}

static int validate_dataarray_reference_value(
    const nmo_cmd_ctx_t *c,
    CK_ARRAYTYPE col_type,
    nmo_guid_t parameter_type_guid,
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
    if (col_type == CKARRAYTYPE_PARAMETER) {
        const nmo_type_registry_t *registry = nmo_context_get_type_registry(c->ctx);
        if (!nmo_type_query_object_is_derived_from_class(
                registry, ref, NMO_CID_PARAMETER)) {
            fprintf(stderr, "Error: Referenced object #%u is not a CKParameter\n", ref_id);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        const nmo_parameter_state_t *parameter_state =
            (const nmo_parameter_state_t *)
                nmo_type_query_object_get_ancestor_state_by_guid(
                    registry, ref, CKPGUID_PARAMETER);
        if (parameter_state == NULL ||
            (!nmo_guid_is_null(parameter_type_guid) &&
             !nmo_guid_equals(parameter_state->type_guid, parameter_type_guid))) {
            fprintf(stderr, "Error: Referenced parameter #%u has an incompatible type\n", ref_id);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
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
            yyjson_mut_arr_add_uint(
                doc, arr, nmo_ref_serialized_id(&cell->object_ref));
            break;
        case CKARRAYTYPE_PARAMETER:
            yyjson_mut_arr_add_uint(
                doc, arr, nmo_ref_serialized_id(&cell->parameter.ref));
            break;
        default:
            yyjson_mut_arr_add_null(doc, arr);
            break;
    }
}

typedef struct data_list_data {
    nmo_cli_record_t **items;
    size_t count;
    size_t capacity;
} data_list_data_t;

static int data_list_visitor(size_t index,
                             nmo_object_t *obj,
                             const nmo_cmd_ctx_t *c,
                             void *user)
{
    (void)index;
    (void)c;
    data_list_data_t *data = (data_list_data_t *)user;
    if (obj == NULL || data == NULL) {
        return 0;
    }

    const nmo_dataarray_state_t *state =
        (const nmo_dataarray_state_t *)nmo_object_get_state(obj);
    if (state == NULL) {
        return 0;
    }

    const char *name = nmo_object_get_name(obj);
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_uint(rec, "id", "ID", nmo_object_get_id(obj));
    ok = ok && nmo_cli_record_str(rec, "name", "Name", name);
    if (ok && (!name || !name[0])) {
        ok = nmo_cli_record_set_text(rec, "-");
    }
    ok = ok && nmo_cli_record_uint(rec, "column_count", "Columns", state->column_count);
    ok = ok && nmo_cli_record_uint(rec, "row_count", "Rows", state->row_count);
    if (ok && data->count == data->capacity) {
        size_t capacity = data->capacity ? data->capacity * 2u : 16u;
        nmo_cli_record_t **grown = (nmo_cli_record_t **)realloc(
            data->items, capacity * sizeof(*grown));
        ok = grown != NULL;
        if (ok) {
            data->items = grown;
            data->capacity = capacity;
        }
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        return 0;
    }
    data->items[data->count++] = rec;
    return 0;
}

static const nmo_cli_table_col_t data_list_columns[] = {
    {"ID",      NMO_CLI_ALIGN_RIGHT, 6,  0},
    {"Name",    NMO_CLI_ALIGN_LEFT,  20, 60},
    {"Columns", NMO_CLI_ALIGN_RIGHT, 7,  0},
    {"Rows",    NMO_CLI_ALIGN_RIGHT, 6,  0},
};

/* Takes ownership of the collected items. */
static nmo_cli_record_t *data_list_record_new(data_list_data_t *data)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "count", NULL, data->count) &&
              nmo_cli_record_raw_fmt(rec, "Data arrays: %zu\n\n", data->count);
    nmo_cli_record_array_t *arrays =
        ok ? nmo_cli_record_array(rec, "arrays", NULL) : NULL;
    ok = ok && arrays != NULL &&
         nmo_cli_record_array_set_table(
             arrays, data_list_columns,
             sizeof(data_list_columns) / sizeof(data_list_columns[0]));
    for (size_t i = 0; i < data->count; ++i) {
        if (ok) {
            ok = nmo_cli_record_array_add(arrays, data->items[i]);
        } else {
            nmo_cli_record_free(data->items[i]);
        }
    }
    free(data->items);
    data->items = NULL;
    data->count = 0;
    data->capacity = 0;
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
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

    data_list_data_t ld = {0};
    rc = nmo_core_object_query_run(&c, &query, data_list_visitor, &ld, NULL);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        for (size_t i = 0; i < ld.count; ++i) {
            nmo_cli_record_free(ld.items[i]);
        }
        free(ld.items);
        return nmo_cmd_ctx_done(&c, rc);
    }

    rc = nmo_cmd_ctx_emit_record(&c, data_list_record_new(&ld), "data.list",
                                 0, c.colorize);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * data show
 * ============================================================================ */

static const nmo_cli_table_col_t data_show_columns[] = {
    {"Index", NMO_CLI_ALIGN_RIGHT, 5,  0},
    {"Name",  NMO_CLI_ALIGN_LEFT,  20, 60},
    {"Type",  NMO_CLI_ALIGN_LEFT,  10, 0},
};

static nmo_cli_record_t *data_show_record_new(nmo_object_id_t obj_id,
                                              const char *name,
                                              const nmo_dataarray_state_t *state)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL && nmo_cli_record_title(rec, "Data Array");
    ok = ok && nmo_cli_record_uint(rec, "id", "ID", obj_id);
    ok = ok && nmo_cli_record_str(rec, "name", "Name", name);
    if (ok && (!name || !name[0])) {
        ok = nmo_cli_record_set_text(rec, "-");
    }
    ok = ok && nmo_cli_record_uint(rec, "column_count", "Columns", state->column_count);
    ok = ok && nmo_cli_record_uint(rec, "row_count", "Rows", state->row_count);

    const char *order_label = "none";
    if (state->order == 1) order_label = "ascending";
    else if (state->order == 2) order_label = "descending";
    ok = ok && nmo_cli_record_int(rec, "sort_order", NULL, state->order);
    ok = ok && nmo_cli_record_uint(rec, "sort_column", NULL, state->column_index);
    ok = ok && nmo_cli_record_text_fmt(rec, "Sort Order", "%s (column %u)",
                                       order_label, state->column_index);

    ok = ok && nmo_cli_record_int(rec, "key_column", "Key Column", state->key_column);
    if (ok && state->key_column < 0) {
        ok = nmo_cli_record_set_text(rec, "none");
    }

    /* Column schema; the text view shows it as a table when non-empty. */
    if (ok && state->column_count > 0) {
        ok = nmo_cli_record_raw(rec, "\n");
    }
    nmo_cli_record_array_t *cols =
        ok ? nmo_cli_record_array(rec, "columns", NULL) : NULL;
    ok = ok && cols != NULL;
    if (ok && state->column_count > 0) {
        ok = nmo_cli_record_array_set_table(
            cols, data_show_columns,
            sizeof(data_show_columns) / sizeof(data_show_columns[0]));
    }
    for (uint32_t i = 0; ok && i < state->column_count; ++i) {
        const char *cname = state->column_formats[i].name;
        nmo_cli_record_t *col = nmo_cli_record_new();
        ok = col != NULL &&
             nmo_cli_record_uint(col, "index", "Index", i) &&
             nmo_cli_record_str(col, "name", "Name", cname) &&
             (cname && cname[0] ? true : nmo_cli_record_set_text(col, "-")) &&
             nmo_cli_record_str(col, "type", "Type",
                                arraytype_name(state->column_formats[i].type));
        if (!ok) {
            nmo_cli_record_free(col);
        } else {
            ok = nmo_cli_record_array_add(cols, col);
        }
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

int nmo_cmd_data_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--id",   "-i", NMO_OPT_UINT,   "Data array object ID"},
        {"--name", "-n", NMO_OPT_STRING, "Data array object name"},
    };
    enum { OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = (!has_selector_opt && r.pos_count >= 2) ? r.pos_args[0] : NULL;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_core_object_selector_t selector = {
        .has_id = vals[OPT_ID].present,
        .id = nmo_opt_uint_or(&vals[OPT_ID], 0),
        .positional_id = positional_id,
        .name = nmo_opt_str(&vals[OPT_NAME]),
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

    nmo_cli_record_t *rec =
        data_show_record_new(obj_id, nmo_object_get_name(obj), state);
    if (!rec) {
        fprintf(stderr, "Error: Out of memory while describing data array %u\n", obj_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    rc = nmo_cmd_ctx_emit_record(&c, rec, "data.show", 14, c.colorize);
    return nmo_cmd_ctx_done(&c, rc);
}

typedef struct data_dump_rows {
    const nmo_dataarray_state_t *state;
    uint32_t row_start;
    uint32_t row_end;
} data_dump_rows_t;

static bool data_dump_rows_json(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                const void *data)
{
    const data_dump_rows_t *rows = (const data_dump_rows_t *)data;
    const nmo_dataarray_state_t *state = rows->state;
    yyjson_mut_val *rows_arr = yyjson_mut_arr(doc);
    if (!rows_arr) {
        return false;
    }
    for (uint32_t ri = rows->row_start; ri < rows->row_end; ++ri) {
        const nmo_dataarray_row_t *row = &state->rows[ri];
        yyjson_mut_val *row_arr = yyjson_mut_arr(doc);
        if (!row_arr) {
            return false;
        }
        for (uint32_t ci = 0; ci < state->column_count && ci < row->column_count; ++ci) {
            add_cell_json(doc, row_arr, &row->cells[ci],
                          state->column_formats[ci].type);
        }
        yyjson_mut_arr_add_val(rows_arr, row_arr);
    }
    return yyjson_mut_obj_add_val(doc, obj, "rows", rows_arr);
}

/*
 * One text-only field per cell, labelled by its column header. Cells missing
 * from a short row show `missing` (NULL: skipped).
 */
static bool data_dump_add_cells(nmo_cli_record_t *rec,
                                const nmo_dataarray_state_t *state,
                                const nmo_dataarray_row_t *row,
                                const char *const *headers,
                                const char *missing,
                                const nmo_cmd_ctx_t *c)
{
    bool ok = true;
    for (uint32_t ci = 0; ok && ci < state->column_count; ++ci) {
        if (ci >= row->column_count) {
            if (missing) {
                ok = nmo_cli_record_text(rec, headers[ci], missing);
            }
            continue;
        }
        char *value = format_cell_dup(&row->cells[ci],
                                      state->column_formats[ci].type, c);
        ok = nmo_cli_record_text(rec, headers[ci], value ? value : "");
        free(value);
    }
    return ok;
}

/*
 * JSON: the column names and the rows. Text: the single row as "column: value"
 * lines, or every row as a table over `col_defs` (borrowed).
 */
static nmo_cli_record_t *data_dump_record_new(const nmo_cmd_ctx_t *c,
                                              nmo_object_id_t obj_id,
                                              const char *name,
                                              const nmo_dataarray_state_t *state,
                                              const data_dump_rows_t *rows,
                                              bool has_single_row,
                                              const nmo_cli_table_col_t *col_defs)
{
    const size_t ncols = state->column_count;
    const char **names = (const char **)calloc(ncols ? ncols : 1u, sizeof(*names));
    const char **labels = (const char **)calloc(ncols ? ncols : 1u, sizeof(*labels));
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = names != NULL && labels != NULL && rec != NULL;
    for (size_t ci = 0; ok && ci < ncols; ++ci) {
        const char *cname = state->column_formats[ci].name;
        names[ci] = cname ? cname : "";
        labels[ci] = (cname && cname[0]) ? cname : "(unnamed)";
    }
    ok = ok && nmo_cli_record_str_list(rec, "columns", NULL, names, ncols, NULL);
    ok = ok && nmo_cli_record_json(rec, data_dump_rows_json, rows);

    if (ok && has_single_row) {
        ok = nmo_cli_record_raw_fmt(rec, "Data array #%u", obj_id) &&
             (name && name[0] ? nmo_cli_record_raw_fmt(rec, " (%s)", name) : true) &&
             nmo_cli_record_raw_fmt(rec, "\nRow %u:\n", rows->row_start) &&
             data_dump_add_cells(rec, state, &state->rows[rows->row_start],
                                 labels, NULL, c);
    } else if (ok && ncols == 0) {
        ok = nmo_cli_record_raw(rec, "(no columns)\n");
    } else if (ok) {
        for (size_t ci = 0; ci < ncols; ++ci) {
            labels[ci] = col_defs[ci].header;
        }
        nmo_cli_record_array_t *table = nmo_cli_record_array(rec, NULL, NULL);
        ok = table != NULL && nmo_cli_record_array_set_table(table, col_defs, ncols);
        for (uint32_t ri = 0; ok && ri < state->row_count; ++ri) {
            nmo_cli_record_t *item = nmo_cli_record_new();
            ok = item != NULL &&
                 data_dump_add_cells(item, state, &state->rows[ri], labels,
                                     "-", c);
            if (!ok) {
                nmo_cli_record_free(item);
            } else {
                ok = nmo_cli_record_array_add(table, item);
            }
        }
    }
    free(names);
    free(labels);
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
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
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *row_str = nmo_opt_str(&vals[OPT_ROW]);
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
        .id = nmo_opt_uint_or(&vals[OPT_ID], 0),
        .positional_id = positional_id,
        .name = nmo_opt_str(&vals[OPT_NAME]),
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

    /* Table columns for the all-rows text view. */
    const size_t ncols = state->column_count;
    nmo_cli_table_col_t *col_defs = (nmo_cli_table_col_t *)calloc(
        ncols ? ncols : 1u, sizeof(*col_defs));
    if (!col_defs) {
        fprintf(stderr, "Error: Out of memory\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
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

    const data_dump_rows_t rows = {
        .state = state,
        .row_start = has_single_row ? single_row : 0,
        .row_end = has_single_row ? single_row + 1 : state->row_count,
    };
    nmo_cli_record_t *rec = data_dump_record_new(
        &c, obj_id, nmo_object_get_name(obj), state, &rows, has_single_row,
        col_defs);
    if (!rec) {
        fprintf(stderr, "Error: Out of memory\n");
    }
    rc = nmo_cmd_ctx_emit_record(&c, rec, "data.dump", 20, c.colorize);
    free(col_defs);
    return nmo_cmd_ctx_done(&c, rc);
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
    char *old_value; /**< malloc'd */
    char *new_value; /**< malloc'd */
    nmo_edit_plan_t *edit_plan;
    nmo_edit_report_t edit_report;
    bool edit_report_ready;
} data_set_cell_args_t;

static void data_set_cell_args_cleanup(data_set_cell_args_t *args)
{
    if (args == NULL) {
        return;
    }
    nmo_edit_plan_destroy(args->edit_plan);
    args->edit_plan = NULL;
    if (args->edit_report_ready) {
        nmo_edit_report_dispose(&args->edit_report);
        args->edit_report_ready = false;
    }
    free(args->old_value);
    free(args->new_value);
    args->old_value = NULL;
    args->new_value = NULL;
}

static int data_set_cell_exit_code(nmo_status_t status)
{
    switch (status) {
    case NMO_ERR_INVALID_ARGUMENT:
    case NMO_ERR_NOT_FOUND:
    case NMO_ERR_OUT_OF_BOUNDS:
        return NMO_CLI_EXIT_ARG_ERROR;
    default:
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
}

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

    free(args->old_value);
    args->old_value = format_cell_dup(&target_row->cells[args->col], col_type, c);
    if (args->old_value == NULL) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    int ref_rc = validate_dataarray_reference_value(
        c, col_type, state->column_formats[args->col].parameter_type_guid,
        args->value_str);
    if (ref_rc != NMO_CLI_EXIT_SUCCESS) {
        return ref_rc;
    }

    nmo_status_t set_rc = nmo_edit_plan_create(&args->edit_plan);
    if (set_rc == NMO_OK) {
        set_rc = nmo_edit_plan_add_data_cell(
            args->edit_plan, args->obj_id, args->row, args->col, args->value_str);
    }
    if (set_rc == NMO_OK) {
        set_rc = nmo_edit_report_init(&args->edit_report);
    }
    if (set_rc == NMO_OK) {
        args->edit_report_ready = true;
        nmo_edit_executor_options_t options = nmo_edit_executor_options_default();
        options.dry_run = dry_run;
        set_rc = nmo_edit_executor_execute(
            c->workspace, args->edit_plan, &options, &args->edit_report);
    }
    if (set_rc != NMO_OK) {
        fprintf(stderr, "Error: Cannot parse '%s' as %s\n",
                args->value_str, args->col_type_name);
        return data_set_cell_exit_code(set_rc);
    }

    free(args->new_value);
    args->new_value = dry_run
        ? nmo_tool_strdup(args->value_str)
        : format_cell_dup(&target_row->cells[args->col], col_type, c);
    if (args->new_value == NULL) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    args->name = nmo_object_get_name(obj);
    return NMO_CLI_EXIT_SUCCESS;
}

typedef struct data_set_cell_report_json {
    const nmo_edit_report_t *report;
    bool dry_run;
} data_set_cell_report_json_t;

static bool data_set_cell_report_json(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                      const void *data)
{
    const data_set_cell_report_json_t *ctx =
        (const data_set_cell_report_json_t *)data;
    nmo_cli_edit_report_add_schema_v2_json(doc, obj, ctx->report, ctx->dry_run);
    return true;
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

    if (args->edit_report_ready && !dry_run && output_path != NULL) {
        (void)nmo_edit_report_set_output_path(&args->edit_report, output_path);
    }
    const data_set_cell_report_json_t report = {
        .report = args->edit_report_ready ? &args->edit_report : NULL,
        .dry_run = dry_run,
    };
    const bool named = args->name && args->name[0];
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_json(rec, data_set_cell_report_json, &report) &&
              nmo_cli_record_uint(rec, "id", NULL, args->obj_id) &&
              nmo_cli_record_str(rec, "name", NULL, named ? args->name : "") &&
              nmo_cli_record_uint(rec, "row", NULL, args->row) &&
              nmo_cli_record_uint(rec, "col", NULL, args->col) &&
              nmo_cli_record_str(rec, "column_name", NULL, args->col_name) &&
              nmo_cli_record_str(rec, "column_type", NULL, args->col_type_name) &&
              nmo_cli_record_str(rec, "old_value", NULL, args->old_value) &&
              nmo_cli_record_str(rec, "new_value", NULL, args->new_value) &&
              nmo_cli_record_raw_fmt(rec, "Data array #%u", args->obj_id) &&
              (named ? nmo_cli_record_raw_fmt(rec, " (%s)", args->name) : true) &&
              nmo_cli_record_raw_fmt(rec, "\n  Cell:  [%u,%u] (column '%s', type %s)\n",
                                     args->row, args->col, args->col_name,
                                     args->col_type_name) &&
              nmo_cli_record_raw_fmt(rec, "  Old:   %s\n", args->old_value) &&
              nmo_cli_record_raw_fmt(rec, "  New:   %s\n", args->new_value);
    if (ok && dry_run) {
        ok = nmo_cli_record_raw(rec, "  (dry run - not saved)\n");
    } else if (ok && output_path) {
        ok = nmo_cli_record_str(rec, "output", NULL, output_path) &&
             nmo_cli_record_raw_fmt(rec, "Saved to: %s\n", output_path);
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    return nmo_cmd_ctx_emit_record(c, rec, "data.set-cell", 0, c->colorize);
}

int nmo_cmd_data_set_cell(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_WRITE_OUTPUT,
        {"--row",     "-r", NMO_OPT_UINT,   "Row index (0-based)"},
        {"--col",     "-c", NMO_OPT_UINT,   "Column index (0-based)"},
        {"--value",   "-v", NMO_OPT_STRING, "New cell value"},
        NMO_OPT_DEF_DRY_RUN,
        {"--id",      NULL,  NMO_OPT_UINT,   "Data array object ID"},
        {"--name",    "-n",  NMO_OPT_STRING, "Data array object name"},
    };
    enum { OPT_OUTPUT, OPT_ROW, OPT_COL, OPT_VALUE, OPT_DRYRUN,
           OPT_ID, OPT_NAME, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool has_row   = vals[OPT_ROW].present;
    uint32_t row   = has_row ? vals[OPT_ROW].val.u : 0;
    bool has_col   = vals[OPT_COL].present;
    uint32_t col   = has_col ? vals[OPT_COL].val.u : 0;
    const char *value_str = nmo_opt_str(&vals[OPT_VALUE]);
    bool dry_run   = nmo_opt_flag(&vals[OPT_DRYRUN]);

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
            .id = nmo_opt_uint_or(&vals[OPT_ID], 0),
            .positional_id = positional_id,
            .name = nmo_opt_str(&vals[OPT_NAME]),
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
    int rc = nmo_cli_run_write_command(
        file_path,
        output_path,
        dry_run,
        global,
        &spec,
        data_set_cell_mutate,
        data_set_cell_report,
        &args);
    data_set_cell_args_cleanup(&args);
    return rc;
}

