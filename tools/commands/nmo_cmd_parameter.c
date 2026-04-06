/**
 * @file nmo_cmd_parameter.c
 * @brief CLI parameter command group implementation
 */

#include "nmo_cmd_parameter.h"

#include "nmo_cmd_object.h"

#include "../nmo_cli_common.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_json.h"
#include "../nmo_tool_session.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "app/nmo_context.h"
#include "app/nmo_param_value.h"
#include "object/nmo_object_types.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "format/nmo_object.h"
#include "type/nmo_type_system.h"

#include <stdio.h>
#include <string.h>

static int is_parameter_class(nmo_type_registry_t *registry, nmo_class_id_t class_id) {
    if (!registry) {
        return 0;
    }

    if (nmo_type_registry_is_class_derived_from(
            registry, (uint32_t)class_id, (uint32_t)NMO_CID_PARAMETER)) {
        return 1;
    }

    return class_id == NMO_CID_PARAMETERIN ||
           class_id == NMO_CID_PARAMETEROUT ||
           class_id == NMO_CID_PARAMETEROPERATION;
}

int nmo_cmd_parameter_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo parameter list <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];
    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    if (!registry) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Type registry unavailable\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to get objects\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    size_t param_count = 0;

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);

            if (!is_parameter_class(registry, class_id)) {
                continue;
            }

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "id", nmo_object_get_id(obj));
            yyjson_mut_obj_add_uint(doc, item, "class_id", class_id);

            const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
            if (class_name) {
                yyjson_mut_obj_add_str(doc, item, "class_name", class_name);
            }

            const char *name = nmo_object_get_name(obj);
            if (name && name[0]) {
                nmo_cli_json_add_str_safe(doc, item, "name", name);
            }

            yyjson_mut_arr_add_val(arr, item);
            param_count++;
        }

        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)param_count);
        yyjson_mut_obj_add_val(doc, data, "objects", arr);

        nmo_cli_json_write_enveloped_and_free(doc, data, "parameter.list", file_path, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    } else {
        static const nmo_cli_table_col_t columns[] = {
            {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 20, 30},
            {"Name", NMO_CLI_ALIGN_LEFT, 20, 50},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);
            if (!is_parameter_class(registry, class_id)) {
                continue;
            }

            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

            const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
            const char *name = nmo_object_get_name(obj);

            const char *cells[] = {
                id_buf,
                class_name ? class_name : "-",
                (name && name[0]) ? name : "-",
            };
            nmo_cli_table_add_row(&table, cells, 3);
            param_count++;
        }

        fprintf(out, "Parameters: %zu\n\n", param_count);
        nmo_cli_table_print(&table, out, colorize);
        nmo_cli_table_free(&table);
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_parameter_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *id_str = NULL;
    const char *file_path = NULL;

    int non_opt = 0;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            non_opt++;
            if (non_opt == 1) id_str = argv[i];
            else if (non_opt == 2) file_path = argv[i];
        }
    }
    if (!id_str || !file_path) {
        fprintf(stderr, "Usage: nmo parameter show <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t object_id;
    if (!nmo_tool_parse_u32(id_str, &object_id)) {
        fprintf(stderr, "Error: Invalid object ID '%s'\n", id_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];
    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *obj = repo ? nmo_object_repository_find_by_id(repo, object_id) : NULL;
    if (!obj) {
        fprintf(stderr, "Error: Object #%u not found\n", object_id);
        nmo_tool_close_session(ctx, session);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    if (!is_parameter_class(registry, cid)) {
        /* Fall back to generic object show for non-parameter objects */
        nmo_tool_close_session(ctx, session);
        return nmo_cmd_object_show(argc, argv, global);
    }

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    const char *name = nmo_object_get_name(obj);
    const char *class_name = nmo_cli_class_name_from_id(ctx, cid);

    /* Get parameter state and decode value */
    const nmo_parameter_state_t *pstate =
        (const nmo_parameter_state_t *)nmo_object_get_state(obj);

    char value_buf[1024];
    value_buf[0] = '\0';
    char summary_buf[1024];
    summary_buf[0] = '\0';

    if (pstate && registry) {
        nmo_param_value_to_string(pstate, registry, session,
                                  value_buf, sizeof(value_buf));
        nmo_param_value_format_summary(pstate, registry, session,
                                       summary_buf, sizeof(summary_buf));
    }

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "id", object_id);
        yyjson_mut_obj_add_uint(doc, data, "class_id", cid);
        if (class_name) yyjson_mut_obj_add_str(doc, data, "class_name", class_name);
        if (name && name[0]) nmo_cli_json_add_str_safe(doc, data, "name", name);

        if (pstate) {
            const char *type_name = nmo_param_value_type_name(pstate, registry);
            if (type_name) yyjson_mut_obj_add_str(doc, data, "type", type_name);
            yyjson_mut_obj_add_str(doc, data, "mode",
                                   nmo_param_mode_to_string(pstate->mode));
            if (value_buf[0]) {
                nmo_cli_json_add_str_safe(doc, data, "value", value_buf);
            }
            yyjson_mut_obj_add_uint(doc, data, "buffer_size",
                                    (uint64_t)pstate->buffer_data.count);
        }

        nmo_cli_json_write_enveloped_and_free(doc, data, "parameter.show", file_path,
            out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    } else {
        fprintf(out, "Parameter #%u\n", object_id);
        fprintf(out, "  Class: %s (CID %u)\n", class_name ? class_name : "?", cid);
        if (name && name[0]) fprintf(out, "  Name:  %s\n", name);
        if (summary_buf[0]) fprintf(out, "  Value: %s\n", summary_buf);
        if (pstate) {
            fprintf(out, "  Buffer: %zu bytes\n", pstate->buffer_data.count);
        }
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

