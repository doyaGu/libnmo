/**
 * @file nmo_cmd_parameter.c
 * @brief CLI parameter command group implementation
 */

#include "nmo_cmd_parameter.h"

#include "nmo_cmd_behavior_internal.h"
#include "nmo_cmd_object.h"
#include "nmo_cmd_object_internal.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_edit_report_json.h"
#include "../nmo_cli_write.h"
#include "../nmo_tool_common.h"
#include "../nmo_opt.h"

#include "nmo.h"
#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_edit_plan.h"
#include "runtime/nmo_context.h"
#include "behavior/nmo_behavior_view.h"
#include "runtime/nmo_workspace.h"
#include "object/nmo_object_types.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "format/nmo_object.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_string.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_object_repository.h"
#include "core/nmo_parse.h"
#include "core/nmo_string.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int is_parameter_class(const nmo_type_registry_t *registry, nmo_class_id_t class_id) {
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

static int parameter_is_behavior_class(const nmo_type_registry_t *registry, nmo_class_id_t class_id) {
    if (!registry) {
        return class_id == NMO_CID_BEHAVIOR;
    }

    if (nmo_type_registry_is_class_derived_from(
            registry, (uint32_t)class_id, (uint32_t)NMO_CID_BEHAVIOR)) {
        return 1;
    }

    return class_id == NMO_CID_BEHAVIOR;
}

static bool parameter_query_predicate(const nmo_object_t *obj, void *user_data) {
    const nmo_type_registry_t *registry = (const nmo_type_registry_t *)user_data;
    if (!obj) return false;
    return is_parameter_class(registry, nmo_object_get_class_id(obj)) != 0;
}

static nmo_object_t *find_behavior_parameter_by_name(
    nmo_object_repository_t *repo,
    nmo_object_t *behavior,
    const char *name)
{
    if (!repo || !behavior || !name) {
        return NULL;
    }

    const nmo_behavior_state_t *bstate =
        (const nmo_behavior_state_t *)nmo_object_get_state(behavior);
    if (!bstate) {
        return NULL;
    }

    const nmo_array_t *arrays[] = {
        &bstate->in_parameters,
        &bstate->out_parameters,
        &bstate->local_parameters,
    };

    for (int array_index = 0; array_index < 3; array_index++) {
        const nmo_array_t *arr = arrays[array_index];
        if (!arr->data || arr->count == 0) continue;

        const nmo_object_id_t *ids = (const nmo_object_id_t *)arr->data;
        for (size_t i = 0; i < arr->count; i++) {
            nmo_object_t *param_obj =
                nmo_object_repository_find_by_id(repo, ids[i]);
            if (!param_obj) continue;

            const char *param_obj_name = nmo_object_get_name(param_obj);
            if (param_obj_name && strcmp(param_obj_name, name) == 0) {
                return param_obj;
            }
        }
    }

    return NULL;
}

static void parameter_list_add_json(yyjson_mut_doc *doc,
                                    yyjson_mut_val *arr,
                                    nmo_context_t *ctx,
                                    nmo_object_t *obj)
{
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    yyjson_mut_val *item = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, item, "id", nmo_object_get_id(obj));
    yyjson_mut_obj_add_uint(doc, item, "class_id", class_id);

    const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
    if (class_name) nmo_cli_json_add_str_safe(doc, item, "class_name", class_name);

    const char *name = nmo_object_get_name(obj);
    if (name && name[0]) nmo_cli_json_add_str_safe(doc, item, "name", name);

    yyjson_mut_arr_add_val(arr, item);
}

static void parameter_list_add_table_row(nmo_cli_table_t *table,
                                         nmo_context_t *ctx,
                                         nmo_object_t *obj)
{
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);

    char id_buf[16];
    snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

    const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
    const char *name = nmo_object_get_name(obj);

    const char *cells[] = {
        id_buf,
        class_name ? class_name : "-",
        (name && name[0]) ? name : "-",
    };
    nmo_cli_table_add_row(table, cells, 3);
}

typedef struct parameter_list_data {
    nmo_context_t *ctx;
    yyjson_mut_doc *doc;
    yyjson_mut_val *arr;
    nmo_cli_table_t *table;
    size_t count;
} parameter_list_data_t;

static int parameter_list_core_visitor(size_t index,
                                       nmo_object_t *obj,
                                       const nmo_cmd_ctx_t *c,
                                       void *user)
{
    (void)index;

    parameter_list_data_t *data = (parameter_list_data_t *)user;
    if (data->arr) {
        parameter_list_add_json(data->doc, data->arr, c->ctx, obj);
    } else if (data->table) {
        parameter_list_add_table_row(data->table, c->ctx, obj);
    }
    data->count++;
    return 0;
}

/**
 * @brief Allocate and format a parameter value string with dynamic sizing
 * @return Allocated string (caller must free), or NULL on allocation failure
 */
static char *format_parameter_value(const nmo_parameter_state_t *pstate,
                                     nmo_type_registry_t *registry,
                                     const nmo_workspace_t *workspace,
                                     size_t *out_len) {
    if (!pstate) {
        return NULL;
    }

    size_t buf_size = 8192;
    char *buffer = (char *)malloc(buf_size);
    if (!buffer) {
        return NULL;
    }

    if (nmo_behavior_param_value_to_string(
            pstate, registry, workspace, buffer, buf_size) != NMO_OK) {
        free(buffer);
        return NULL;
    }

    if (out_len) {
        *out_len = strlen(buffer);
    }
    return buffer;
}

/* Hex formatting is provided by nmo_format_hex() from core/nmo_string.h. */

static const char *parameter_trace_step_type_name(nmo_behavior_trace_step_type_t type) {
    switch (type) {
        case NMO_BEHAVIOR_TRACE_STEP_START:
            return "start";
        case NMO_BEHAVIOR_TRACE_STEP_SHARED_SOURCE:
            return "shared_source";
        case NMO_BEHAVIOR_TRACE_STEP_DIRECT_SOURCE:
            return "direct_source";
        default:
            return "unknown";
    }
}

static void parameter_add_source_chain_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *item,
    const nmo_array_t *chain,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *registry)
{
    if (!doc || !item || !chain || chain->count == 0 || !repo) {
        return;
    }

    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    const nmo_behavior_trace_step_t *steps =
        (const nmo_behavior_trace_step_t *)chain->data;

    for (size_t i = 0; i < chain->count; ++i) {
        yyjson_mut_val *step = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, step, "id", steps[i].id);
        yyjson_mut_obj_add_uint(doc, step, "class_id", steps[i].class_id);
        nmo_cli_json_add_str_safe(doc, step, "step",
                                  parameter_trace_step_type_name(steps[i].type));
        nmo_cli_json_add_str_safe(doc, step, "name", resolve_name(repo, steps[i].id));
        yyjson_mut_obj_add_uint(doc, step, "owner_id", steps[i].owner_id);
        nmo_cli_json_add_str_safe(doc, step, "owner_name",
                                  resolve_name(repo, steps[i].owner_id));

        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, steps[i].id);
        nmo_guid_t type_guid = get_param_type_guid(obj);
        if (!nmo_guid_is_null(type_guid)) {
            char guid_buf[64];
            nmo_guid_format(type_guid, guid_buf, sizeof(guid_buf));
            nmo_cli_json_add_str_safe(doc, step, "type_guid", guid_buf);
            nmo_cli_json_add_str_safe(doc, step, "type_name",
                                      resolve_type(registry, type_guid));
        }

        yyjson_mut_arr_add_val(arr, step);
    }

    yyjson_mut_obj_add_val(doc, item, "source_chain", arr);
}

static void parameter_add_resolved_source_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *item,
    nmo_context_t *ctx,
    nmo_object_t *resolved,
    const nmo_type_registry_t *registry,
    const nmo_workspace_t *workspace)
{
    if (!doc || !item || !resolved) {
        return;
    }

    nmo_object_id_t resolved_id = nmo_object_get_id(resolved);
    nmo_class_id_t resolved_class = nmo_object_get_class_id(resolved);
    yyjson_mut_obj_add_uint(doc, item, "resolved_source_id", resolved_id);
    yyjson_mut_obj_add_uint(doc, item, "resolved_class_id", resolved_class);

    const char *class_name = NULL;
    if (ctx) {
        class_name = nmo_cli_class_name_from_id(ctx, resolved_class);
    }
    if (class_name) {
        nmo_cli_json_add_str_safe(doc, item, "resolved_class_name", class_name);
    }

    const char *name = nmo_object_get_name(resolved);
    if (name && name[0]) {
        nmo_cli_json_add_str_safe(doc, item, "resolved_name", name);
    }

    nmo_guid_t type_guid = get_param_type_guid(resolved);
    if (!nmo_guid_is_null(type_guid)) {
        char guid_buf[64];
        nmo_guid_format(type_guid, guid_buf, sizeof(guid_buf));
        nmo_cli_json_add_str_safe(doc, item, "resolved_type_guid", guid_buf);
        nmo_cli_json_add_str_safe(doc, item, "resolved_type_name",
                                  resolve_type(registry, type_guid));
    }

    if (resolved_class == NMO_CID_PARAMETERIN) {
        return;
    }

    const nmo_parameter_state_t *pstate = nmo_parameter_get_state(resolved);
    if (!pstate) {
        return;
    }

    nmo_cli_json_add_str_safe(doc, item, "resolved_mode",
                              nmo_behavior_param_mode_to_string(pstate->mode));
    char *value = format_parameter_value(
        pstate, (nmo_type_registry_t *)registry, workspace, NULL);
    if (value) {
        if (value[0]) {
            nmo_cli_json_add_str_safe(doc, item, "resolved_value", value);
        }
        free(value);
    }
    if (pstate->buffer_data.data) {
        yyjson_mut_obj_add_uint(doc, item, "resolved_buffer_size",
                                (uint64_t)pstate->buffer_data.count);
    }
}

static void parameter_add_operation_param_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *item,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *registry,
    const char *prefix,
    nmo_object_id_t param_id)
{
    if (!doc || !item || !repo || !prefix || param_id == 0) {
        return;
    }

    char key[64];
    snprintf(key, sizeof(key), "%s_id", prefix);
    nmo_cli_json_add_uint_safe(doc, item, key, param_id);

    snprintf(key, sizeof(key), "%s_name", prefix);
    nmo_cli_json_add_str_safe(doc, item, key, resolve_name(repo, param_id));

    nmo_object_t *param = nmo_object_repository_find_by_id(repo, param_id);
    nmo_guid_t type_guid = get_param_type_guid(param);
    if (!nmo_guid_is_null(type_guid)) {
        char guid_buf[64];
        nmo_guid_format(type_guid, guid_buf, sizeof(guid_buf));
        snprintf(key, sizeof(key), "%s_type_guid", prefix);
        nmo_cli_json_add_str_safe(doc, item, key, guid_buf);
        snprintf(key, sizeof(key), "%s_type_name", prefix);
        nmo_cli_json_add_str_safe(doc, item, key, resolve_type(registry, type_guid));
    }
}

static void parameter_add_operation_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *item,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *registry,
    const nmo_parameteroperation_state_t *op)
{
    if (!doc || !item || !op) {
        return;
    }

    char guid_buf[64];
    nmo_guid_format(op->operation_guid, guid_buf, sizeof(guid_buf));
    nmo_cli_json_add_str_safe(doc, item, "operation_guid", guid_buf);

    const char *operation_name =
        nmo_type_registry_guid_to_name(registry, op->operation_guid);
    if (operation_name && operation_name[0]) {
        nmo_cli_json_add_str_safe(doc, item, "operation_name", operation_name);
    }

    if (op->has_owner) {
        yyjson_mut_obj_add_uint(doc, item, "owner_id", op->owner_id);
        if (repo) {
            nmo_cli_json_add_str_safe(doc, item, "owner_name",
                                      resolve_name(repo, op->owner_id));
        }
    }
    if (op->has_in1) {
        parameter_add_operation_param_json(doc, item, repo, registry, "in1", op->in1_id);
    }
    if (op->has_in2) {
        parameter_add_operation_param_json(doc, item, repo, registry, "in2", op->in2_id);
    }
    if (op->has_out) {
        parameter_add_operation_param_json(doc, item, repo, registry, "out", op->out_id);
    }
}

static void parameter_print_operation_param(
    FILE *out,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *registry,
    const char *label,
    nmo_object_id_t param_id)
{
    if (!out || !label || param_id == 0) {
        return;
    }

    fprintf(out, "%s: #%u", label, param_id);
    if (repo) {
        fprintf(out, " %s", resolve_name(repo, param_id));
    }

    nmo_object_t *param = repo ? nmo_object_repository_find_by_id(repo, param_id) : NULL;
    nmo_guid_t type_guid = get_param_type_guid(param);
    if (!nmo_guid_is_null(type_guid)) {
        fprintf(out, " [%s]", resolve_type(registry, type_guid));
    }
    fprintf(out, "\n");
}

static void parameter_print_operation_text(
    FILE *out,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *registry,
    const nmo_parameteroperation_state_t *op)
{
    if (!out || !op) {
        return;
    }

    char guid_buf[64];
    nmo_guid_format(op->operation_guid, guid_buf, sizeof(guid_buf));
    fprintf(out, "Operation GUID: %s", guid_buf);
    const char *operation_name =
        nmo_type_registry_guid_to_name(registry, op->operation_guid);
    if (operation_name && operation_name[0]) {
        fprintf(out, " (%s)", operation_name);
    }
    fprintf(out, "\n");

    if (op->has_owner) {
        fprintf(out, "Owner: #%u", op->owner_id);
        if (repo) {
            fprintf(out, " %s", resolve_name(repo, op->owner_id));
        }
        fprintf(out, "\n");
    }
    if (op->has_in1) {
        parameter_print_operation_param(out, repo, registry, "Input 1", op->in1_id);
    }
    if (op->has_in2) {
        parameter_print_operation_param(out, repo, registry, "Input 2", op->in2_id);
    }
    if (op->has_out) {
        parameter_print_operation_param(out, repo, registry, "Output", op->out_id);
    }
}

static void parameter_add_resolved_source_text(
    FILE *out,
    const nmo_array_t *chain,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *registry,
    const nmo_workspace_t *workspace)
{
    if (!out || !chain || chain->count == 0 || !repo) {
        return;
    }

    const nmo_behavior_trace_step_t *steps =
        (const nmo_behavior_trace_step_t *)chain->data;

    fprintf(out, "Source Chain:\n");
    for (size_t i = 0; i < chain->count; ++i) {
        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, steps[i].id);
        nmo_guid_t type_guid = get_param_type_guid(obj);
        fprintf(out, "  %zu. #%u %s", i, steps[i].id,
                resolve_name(repo, steps[i].id));
        if (!nmo_guid_is_null(type_guid)) {
            fprintf(out, " [%s]", resolve_type(registry, type_guid));
        }
        fprintf(out, " (%s)\n", parameter_trace_step_type_name(steps[i].type));
    }

    const nmo_behavior_trace_step_t *last = &steps[chain->count - 1];
    nmo_object_t *resolved = nmo_object_repository_find_by_id(repo, last->id);
    if (!resolved || nmo_object_get_class_id(resolved) == NMO_CID_PARAMETERIN) {
        return;
    }

    const nmo_parameter_state_t *pstate = nmo_parameter_get_state(resolved);
    if (!pstate) {
        return;
    }

    char *value = format_parameter_value(
        pstate, (nmo_type_registry_t *)registry, workspace, NULL);
    if (value) {
        fprintf(out, "Resolved: #%u %s = %s\n", last->id,
                resolve_name(repo, last->id), value);
        free(value);
    }
}

static void parameter_add_parameterin_resolution_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *item,
    nmo_context_t *ctx,
    nmo_workspace_t *workspace,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *registry,
    nmo_object_id_t param_id)
{
    if (!doc || !item || !workspace || !repo || param_id == 0) {
        return;
    }

    nmo_array_t chain;
    if (nmo_array_init(&chain, sizeof(nmo_behavior_trace_step_t), 8, NULL) != NMO_OK) {
        return;
    }

    if (nmo_behavior_analyze_trace_param_chain(workspace, param_id,
                                               &chain, 32) == NMO_OK &&
        chain.count > 0) {
        parameter_add_source_chain_json(doc, item, &chain, repo, registry);

        const nmo_behavior_trace_step_t *steps =
            (const nmo_behavior_trace_step_t *)chain.data;
        nmo_object_t *resolved =
            nmo_object_repository_find_by_id(repo, steps[chain.count - 1].id);
        if (resolved && nmo_object_get_class_id(resolved) != NMO_CID_PARAMETERIN) {
            parameter_add_resolved_source_json(doc, item, ctx, resolved, registry, workspace);
        } else {
            const char *reason =
                (chain.count == 1) ? "no_source" : "chain_ended_at_parameter_in";
            nmo_cli_json_add_str_safe(doc, item, "unresolved_reason", reason);
        }
    }

    nmo_array_dispose(&chain);
}

static void parameter_add_parameterin_resolution_text(
    FILE *out,
    nmo_workspace_t *workspace,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *registry,
    nmo_object_id_t param_id)
{
    if (!out || !workspace || !repo || param_id == 0) {
        return;
    }

    nmo_array_t chain;
    if (nmo_array_init(&chain, sizeof(nmo_behavior_trace_step_t), 8, NULL) != NMO_OK) {
        return;
    }

    if (nmo_behavior_analyze_trace_param_chain(workspace, param_id,
                                               &chain, 32) == NMO_OK &&
        chain.count > 0) {
        parameter_add_resolved_source_text(out, &chain, repo, registry, workspace);
    }

    nmo_array_dispose(&chain);
}

/* ---- parameter list: per-file handler for batch mode ---- */

static int parameter_list_single(const char *file_path,
                                 const nmo_cli_global_opts_t *global,
                                 void *user_data,
                                 yyjson_mut_doc *doc,
                                 yyjson_mut_val *data)
{
    const nmo_tool_text_output_ctx_t *text_ctx =
        (const nmo_tool_text_output_ctx_t *)user_data;

    nmo_context_t *ctx = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    char errbuf[256];

    if (!nmo_tool_open_document(file_path, &ctx, &document, &workspace,
                                errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    if (!registry) {
        fprintf(stderr, "Error: Type registry unavailable\n");
        nmo_tool_close_document(ctx, document, workspace);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_query_t query = {
        .predicate = parameter_query_predicate,
        .predicate_user_data = (void *)registry,
    };

    nmo_cmd_ctx_t cmd;
    nmo_cmd_ctx_init_from_repl_document(&cmd, ctx, document, workspace, false);

    if (doc && data) {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        parameter_list_data_t ld = { .ctx = ctx, .doc = doc, .arr = arr };
        if (nmo_core_object_query_run(&cmd, &query,
                                      parameter_list_core_visitor, &ld,
                                      NULL) != NMO_CLI_EXIT_SUCCESS) {
            fprintf(stderr, "Error: Failed to query objects\n");
            nmo_tool_close_document(ctx, document, workspace);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)ld.count);
        yyjson_mut_obj_add_val(doc, data, "objects", arr);
    } else {
        FILE *out = (text_ctx && text_ctx->out) ? text_ctx->out : stdout;
        bool colorize = text_ctx ? text_ctx->colorize : false;

        static const nmo_cli_table_col_t columns[] = {
            {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 20, 30},
            {"Name", NMO_CLI_ALIGN_LEFT, 20, 50},
        };
        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        parameter_list_data_t ld = { .ctx = ctx, .table = &table };
        if (nmo_core_object_query_run(&cmd, &query,
                                      parameter_list_core_visitor, &ld,
                                      NULL) != NMO_CLI_EXIT_SUCCESS) {
            fprintf(stderr, "Error: Failed to query objects\n");
            nmo_cli_table_free(&table);
            nmo_tool_close_document(ctx, document, workspace);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        fprintf(out, "Parameters: %zu\n\n", ld.count);
        nmo_cli_table_print(&table, out, colorize);
        nmo_cli_table_free(&table);
    }

    (void)global;
    nmo_tool_close_document(ctx, document, workspace);
    return NMO_CLI_EXIT_SUCCESS;
}

static int parameter_list_run(nmo_cmd_ctx_t *c) {
    if (!c->registry) {
        fprintf(stderr, "Error: Type registry unavailable\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_query_t query = {
        .predicate = parameter_query_predicate,
        .predicate_user_data = (void *)c->registry,
    };

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        parameter_list_data_t ld = { .doc = doc, .arr = arr };
        int rc = nmo_core_object_query_run(c, &query,
                                           parameter_list_core_visitor, &ld, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return rc;
        }

        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)ld.count);
        yyjson_mut_obj_add_val(doc, data, "objects", arr);

        nmo_cmd_ctx_json_end(c, doc, data, "parameter.list");
    } else {
        static const nmo_cli_table_col_t columns[] = {
            {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 20, 30},
            {"Name", NMO_CLI_ALIGN_LEFT, 20, 50},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        parameter_list_data_t ld = { .table = &table };
        int rc = nmo_core_object_query_run(c, &query,
                                           parameter_list_core_visitor, &ld, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            nmo_cli_table_free(&table);
            return rc;
        }

        fprintf(c->out, "Parameters: %zu\n\n", ld.count);
        nmo_cli_table_print(&table, c->out, c->colorize);
        nmo_cli_table_free(&table);
    }

    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_parameter_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Batch mode */
    if (global->batch_mode) {
        const char *paths[256];
        size_t count = nmo_tool_find_file_args(argc, argv, paths, 256);
        if (count == 0) {
            fprintf(stderr, "Error: No files specified\n");
            fprintf(stderr, "Usage: nmo --batch parameter list <file1> <file2> ...\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        return nmo_tool_batch_run(paths, count, global, "parameter.list",
                                  parameter_list_single, NULL);
    }

    /* Single file mode */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    rc = parameter_list_run(&c);
    return nmo_cmd_ctx_done(&c, rc);
}

static int parameter_show_run(nmo_cmd_ctx_t *ctx, uint32_t object_id,
                              int argc, char **argv,
                              const nmo_cli_global_opts_t *global,
                              bool close_ctx)
{
    nmo_cmd_ctx_t c = *ctx;
    nmo_object_repository_t *repo = nmo_tool_owner_repository(c.workspace);
    nmo_object_t *obj = repo ? nmo_object_repository_find_by_id(repo, object_id) : NULL;
    if (!obj) {
        fprintf(stderr, "Error: Object #%u not found\n", object_id);
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR) : NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    if (!is_parameter_class(c.registry, cid)) {
        /* Fall back to generic object show for non-parameter objects */
        if (close_ctx) {
            nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
            return nmo_cmd_object_show(argc, argv, global);
        }
        return nmo_cmd_object_show_in_session(ctx, argc, argv);
    }

    const char *name = nmo_object_get_name(obj);
    const char *class_name = nmo_cli_class_name_from_id(c.ctx, cid);

    /* Get parameter base state (handles CKParameter/Out/Local hierarchy) */
    const nmo_parameter_state_t *pstate = nmo_parameter_get_state(obj);

    /* Dynamic value buffer - only for classes with valid Parameter state */
    char *value_buf = NULL;
    if (pstate && c.registry && cid != NMO_CID_PARAMETERIN) {
        value_buf = format_parameter_value(
            pstate, (nmo_type_registry_t *)c.registry, c.workspace, NULL);
    }

    /* Summary buffer with dynamic allocation */
    size_t summary_size = 8192;
    char *summary_buf = (char *)malloc(summary_size);
    if (!summary_buf) {
        free(value_buf);
        fprintf(stderr, "Error: Memory allocation failed\n");
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    summary_buf[0] = '\0';

    if (pstate && c.registry && cid != NMO_CID_PARAMETERIN) {
        nmo_behavior_param_format_summary(pstate, (nmo_type_registry_t *)c.registry, c.workspace,
                                       summary_buf, summary_size);
    }

    /* Get owner and specialized info based on class */
    nmo_object_id_t owner_id = 0;
    nmo_object_id_t source_id = 0;
    uint32_t destination_count = 0;
    uint8_t is_shared = 0;
    nmo_guid_t type_guid = NMO_GUID_NULL;

    const void *state = nmo_object_get_state(obj);
    if (state) {
        if (cid == NMO_CID_PARAMETERIN) {
            const nmo_parameterin_state_t *pin_state =
                (const nmo_parameterin_state_t *)state;
            owner_id = pin_state->owner_id;
            source_id = pin_state->source_id;
            is_shared = pin_state->is_shared;
            type_guid = pin_state->type_guid;
        } else if (cid == NMO_CID_PARAMETEROUT) {
            const nmo_parameterout_state_t *pout_state =
                (const nmo_parameterout_state_t *)state;
            owner_id = pout_state->owner_id;
            destination_count = pout_state->destination_count;
            /* ParameterOut inherits from Parameter, so pstate is valid */
        } else if (cid == NMO_CID_PARAMETERLOCAL) {
            const nmo_parameterlocal_state_t *plocal_state =
                (const nmo_parameterlocal_state_t *)state;
            owner_id = plocal_state->owner_id;
            /* ParameterLocal inherits from Parameter, so pstate is valid */
        }
    }
    const nmo_parameteroperation_state_t *op_state =
        (cid == NMO_CID_PARAMETEROPERATION)
            ? (const nmo_parameteroperation_state_t *)state
            : NULL;

    /* For ParameterIn, get the type from its GUID */
    const char *type_name_override = NULL;
    if (cid == NMO_CID_PARAMETERIN && c.registry && !nmo_guid_is_null(type_guid)) {
        type_name_override = nmo_type_registry_guid_to_name((nmo_type_registry_t *)c.registry, type_guid);
    }

    /* Hex dump for small buffers */
    char hex_dump[512];
    hex_dump[0] = '\0';
    if (pstate && pstate->mode == CKPARAM_MODE_BUFFER &&
        pstate->buffer_data.data != NULL &&
        pstate->buffer_data.count > 0 && pstate->buffer_data.count < 64) {
        nmo_format_hex(pstate->buffer_data.data,
                       pstate->buffer_data.count, 64, hex_dump, sizeof(hex_dump));
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "id", object_id);
        yyjson_mut_obj_add_uint(doc, data, "class_id", cid);
        if (class_name) yyjson_mut_obj_add_str(doc, data, "class_name", class_name);
        if (name && name[0]) nmo_cli_json_add_str_safe(doc, data, "name", name);

        /* Type information */
        const char *type_name_display = type_name_override;
        if (!type_name_display && pstate && cid != NMO_CID_PARAMETERLOCAL) {
            type_name_display = nmo_behavior_param_type_name(pstate, (nmo_type_registry_t *)c.registry);
        }
        if (type_name_display) {
            yyjson_mut_obj_add_str(doc, data, "type", type_name_display);
        }

        if (pstate) {
            yyjson_mut_obj_add_str(doc, data, "mode",
                                   nmo_behavior_param_mode_to_string(pstate->mode));
            if (value_buf && value_buf[0]) {
                nmo_cli_json_add_str_safe(doc, data, "value", value_buf);
            }
            if (pstate->buffer_data.data) {
                yyjson_mut_obj_add_uint(doc, data, "buffer_size",
                                        (uint64_t)pstate->buffer_data.count);
            }

            if (hex_dump[0]) {
                yyjson_mut_obj_add_strcpy(doc, data, "hex", hex_dump);
            }
        }

        if (owner_id != 0) {
            yyjson_mut_obj_add_uint(doc, data, "owner_id", owner_id);
        }
        if (source_id != 0) {
            yyjson_mut_obj_add_uint(doc, data, "source_id", source_id);
            yyjson_mut_obj_add_bool(doc, data, "is_shared", is_shared != 0);
        }
        if (cid == NMO_CID_PARAMETERIN) {
            parameter_add_parameterin_resolution_json(
                doc, data, c.ctx, c.workspace, repo, c.registry, object_id);
        }
        if (op_state) {
            parameter_add_operation_json(doc, data, repo, c.registry, op_state);
        }
        if (destination_count > 0) {
            yyjson_mut_obj_add_uint(doc, data, "destination_count", destination_count);
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "parameter.show");
    } else {
        fprintf(c.out, "Parameter #%u\n", object_id);
        fprintf(c.out, "  Class: %s (CID %u)\n", class_name ? class_name : "?", cid);
        if (name && name[0]) fprintf(c.out, "  Name:  %s\n", name);

        /* Type information */
        const char *type_name_display = type_name_override;
        if (!type_name_display && pstate && cid != NMO_CID_PARAMETERLOCAL) {
            type_name_display = nmo_behavior_param_type_name(pstate, (nmo_type_registry_t *)c.registry);
        }
        if (type_name_display) {
            fprintf(c.out, "  Type:  %s\n", type_name_display);
        }

        if (pstate) {
            fprintf(c.out, "  Mode:  %s\n", nmo_behavior_param_mode_to_string(pstate->mode));
        }

        if (owner_id != 0) {
            nmo_object_t *owner_obj = repo ? nmo_object_repository_find_by_id(repo, owner_id) : NULL;
            if (owner_obj) {
                const char *owner_name = nmo_object_get_name(owner_obj);
                fprintf(c.out, "  Owner: #%u", owner_id);
                if (owner_name && owner_name[0]) {
                    fprintf(c.out, " (%s)", owner_name);
                }
                fprintf(c.out, "\n");
            } else {
                fprintf(c.out, "  Owner: #%u\n", owner_id);
            }
        }

        if (source_id != 0) {
            fprintf(c.out, "  Source: #%u%s\n", source_id, is_shared ? " (shared)" : " (direct)");
        }
        if (cid == NMO_CID_PARAMETERIN) {
            parameter_add_parameterin_resolution_text(
                c.out, c.workspace, repo, c.registry, object_id);
        }
        if (op_state) {
            parameter_print_operation_text(c.out, repo, c.registry, op_state);
        }

        if (destination_count > 0) {
            fprintf(c.out, "  Destinations: %u\n", destination_count);
        }

        if (summary_buf && summary_buf[0]) {
            fprintf(c.out, "  Value: %s\n", summary_buf);
        }

        if (pstate && pstate->buffer_data.data) {
            fprintf(c.out, "  Buffer: %zu bytes\n", pstate->buffer_data.count);
        }

        if (hex_dump[0]) {
            fprintf(c.out, "  Hex:   %s\n", hex_dump);
        }
    }

    free(value_buf);
    free(summary_buf);

    return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS) : NMO_CLI_EXIT_SUCCESS;
}

static int parameter_show_parse_id(int argc, char **argv, bool expect_file_operand,
                                   uint32_t *object_id, const char *usage)
{
    const char *id_str = NULL;
    int non_opt = 0;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            non_opt++;
            if (non_opt == 1) {
                id_str = argv[i];
            }
        }
    }

    if ((expect_file_operand && non_opt != 2) || (!expect_file_operand && non_opt != 1)) {
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!id_str || !nmo_tool_parse_u32(id_str, object_id)) {
        fprintf(stderr, "Error: Invalid object ID '%s'\n", id_str ? id_str : "(null)");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_parameter_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *usage = "nmo parameter show <id> <file>";
    uint32_t object_id = 0;
    int rc = parameter_show_parse_id(argc, argv, true, &object_id, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    return parameter_show_run(&c, object_id, argc, argv, global, true);
}

static int nmo_cmd_parameter_show_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv) {
    const char *usage = "parameter show <id>";
    uint32_t object_id = 0;
    int rc = parameter_show_parse_id(argc, argv, false, &object_id, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    return parameter_show_run(ctx, object_id, argc, argv, NULL, false);
}

/**
 * @brief Dump parameter details with decoded value
 */
static void dump_parameter_details(nmo_object_t *obj,
                                     nmo_context_t *ctx,
                                     const nmo_workspace_t *workspace,
                                     FILE *out) {
    if (!obj || !ctx || !out) {
        return;
    }

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    nmo_object_repository_t *repo =
        nmo_tool_owner_repository((nmo_workspace_t *)workspace);

    nmo_object_id_t object_id = nmo_object_get_id(obj);
    nmo_class_id_t cid = nmo_object_get_class_id(obj);

    const char *name = nmo_object_get_name(obj);
    const char *class_name = nmo_cli_class_name_from_id(ctx, cid);

    fprintf(out, "=== Parameter #%u ===\n", object_id);
    fprintf(out, "Class: %s (CID %u)\n", class_name ? class_name : "?", cid);
    if (name && name[0]) {
        fprintf(out, "Name:  %s\n", name);
    }

    const void *state = nmo_object_get_state(obj);
    if (!state) {
        fprintf(out, "No state available\n\n");
        return;
    }

    /* Parameter state is only valid for Parameter, ParameterLocal, ParameterOut */
    const nmo_parameter_state_t *pstate = NULL;
    nmo_guid_t type_guid = NMO_GUID_NULL;
    const char *mode_str = "none";

    if (cid == NMO_CID_PARAMETERIN) {
        const nmo_parameterin_state_t *pin_state = (const nmo_parameterin_state_t *)state;
        type_guid = pin_state->type_guid;
    } else if (cid != NMO_CID_PARAMETEROPERATION) {
        pstate = (const nmo_parameter_state_t *)state;
        type_guid = pstate->type_guid;
        mode_str = nmo_behavior_param_mode_to_string(pstate->mode);
    }

    const char *type_name = NULL;
    if (cid == NMO_CID_PARAMETERIN && !nmo_guid_is_null(type_guid)) {
        type_name = nmo_type_registry_guid_to_name(registry, type_guid);
    } else if (pstate) {
        type_name = nmo_behavior_param_type_name(pstate, registry);
    }

    if (type_name) {
        fprintf(out, "Type:  %s\n", type_name);
    }

    if (!nmo_guid_is_null(type_guid)) {
        char guid_str[64];
        nmo_guid_format(type_guid, guid_str, sizeof(guid_str));
        fprintf(out, "GUID:  %s\n", guid_str);
    }

    fprintf(out, "Mode:  %s\n", mode_str);

    /* Owner and specialized info */
    nmo_object_id_t owner_id = 0;
    nmo_object_id_t source_id = 0;
    uint32_t destination_count = 0;
    uint8_t is_shared = 0;

    if (cid == NMO_CID_PARAMETERIN) {
        const nmo_parameterin_state_t *pin_state = (const nmo_parameterin_state_t *)state;
        owner_id = pin_state->owner_id;
        source_id = pin_state->source_id;
        is_shared = pin_state->is_shared;
    } else if (cid == NMO_CID_PARAMETEROUT) {
        const nmo_parameterout_state_t *pout_state = (const nmo_parameterout_state_t *)state;
        owner_id = pout_state->owner_id;
        destination_count = pout_state->destination_count;
    } else if (cid == NMO_CID_PARAMETERLOCAL) {
        const nmo_parameterlocal_state_t *plocal_state = (const nmo_parameterlocal_state_t *)state;
        owner_id = plocal_state->owner_id;
    }

    if (owner_id != 0) {
        nmo_object_t *owner_obj = repo ? nmo_object_repository_find_by_id(repo, owner_id) : NULL;
        if (owner_obj) {
            const char *owner_name = nmo_object_get_name(owner_obj);
            fprintf(out, "Owner: #%u", owner_id);
            if (owner_name && owner_name[0]) {
                fprintf(out, " (%s)", owner_name);
            }
            fprintf(out, "\n");
        } else {
            fprintf(out, "Owner: #%u\n", owner_id);
        }
    }

    if (source_id != 0) {
        fprintf(out, "Source: #%u%s\n", source_id, is_shared ? " (shared)" : " (direct)");
    }
    if (cid == NMO_CID_PARAMETERIN) {
        parameter_add_parameterin_resolution_text(
            out, (nmo_workspace_t *)workspace, repo, registry, object_id);
    }
    if (cid == NMO_CID_PARAMETEROPERATION) {
        const nmo_parameteroperation_state_t *op =
            (const nmo_parameteroperation_state_t *)state;
        parameter_print_operation_text(out, repo, registry, op);
    }

    if (destination_count > 0) {
        fprintf(out, "Destinations: %u\n", destination_count);
    }

    /* Decoded value - only available for Parameter-derived classes */
    if (pstate) {
        char *value_buf = format_parameter_value(pstate, registry, workspace, NULL);
        if (value_buf) {
            fprintf(out, "Value: %s\n", value_buf);
            free(value_buf);
        }

        if (pstate->buffer_data.data) {
            fprintf(out, "Buffer: %zu bytes\n", pstate->buffer_data.count);
        }

        /* Raw hex dump (first 64 bytes) */
        if (pstate->mode == CKPARAM_MODE_BUFFER && pstate->buffer_data.data && pstate->buffer_data.count > 0) {
            fprintf(out, "Hex:   ");
            const uint8_t *data = (const uint8_t *)pstate->buffer_data.data;
            size_t display_len = (pstate->buffer_data.count > 64) ? 64 : pstate->buffer_data.count;
            for (size_t i = 0; i < display_len; ++i) {
                fprintf(out, "%02x ", data[i]);
                if ((i + 1) % 16 == 0 && i + 1 < display_len) {
                    fprintf(out, "\n       ");
                }
            }
            if (pstate->buffer_data.count > 64) {
                fprintf(out, "\n       ... (%zu bytes total)", pstate->buffer_data.count);
            }
            fprintf(out, "\n");
        }
    }

    fprintf(out, "\n");
}

typedef struct parameter_target_list {
    nmo_object_t **items;
    size_t count;
    size_t capacity;
    bool oom;
} parameter_target_list_t;

static int parameter_target_collect_visitor(size_t index,
                                            nmo_object_t *obj,
                                            const nmo_cmd_ctx_t *c,
                                            void *user)
{
    (void)index;
    (void)c;

    parameter_target_list_t *list = (parameter_target_list_t *)user;
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity ? list->capacity * 2u : 64u;
        nmo_object_t **new_items =
            (nmo_object_t **)realloc(list->items, new_capacity * sizeof(*new_items));
        if (!new_items) {
            list->oom = true;
            return 1;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }

    list->items[list->count++] = obj;
    return 0;
}

typedef struct parameter_dump_args {
    bool dump_all;
    bool has_filter;
    nmo_guid_t filter_guid;
    uint32_t object_id;
} parameter_dump_args_t;

static int parameter_dump_parse(int argc, char **argv, bool expect_file_operand,
                                parameter_dump_args_t *args, const char *usage)
{
    memset(args, 0, sizeof(*args));
    args->filter_guid = NMO_GUID_NULL;

    static const nmo_opt_def_t opts[] = {
        {"--all",  "-a", NMO_OPT_FLAG,   "Dump all parameters"},
        {"--type", NULL, NMO_OPT_STRING, "Filter by type GUID"},
        {"--json", "-j", NMO_OPT_FLAG,   "JSON output"},
    };
    nmo_opt_val_t vals[3];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 3, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool dump_all = vals[0].present && vals[0].val.flag;
    const char *type_guid_str = vals[1].present ? vals[1].val.str : NULL;
    const char *id_str = NULL;
    args->dump_all = dump_all;

    if (dump_all) {
        if ((expect_file_operand && r.pos_count != 1) ||
            (!expect_file_operand && r.pos_count != 0)) {
            fprintf(stderr, "Usage: %s\n", usage);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    } else {
        if ((expect_file_operand && r.pos_count != 2) ||
            (!expect_file_operand && r.pos_count != 1)) {
            fprintf(stderr, "Usage: %s\n", usage);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        id_str = r.pos_args[0];
    }

    uint32_t object_id = 0;
    if (!dump_all) {
        if (!id_str || !nmo_tool_parse_u32(id_str, &object_id)) {
            fprintf(stderr, "Error: Invalid object ID '%s'\n", id_str ? id_str : "(null)");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args->object_id = object_id;
    }

    nmo_guid_t filter_guid = NMO_GUID_NULL;
    bool has_filter = false;
    if (type_guid_str) {
        filter_guid = nmo_guid_parse(type_guid_str);
        if (nmo_guid_is_null(filter_guid)) {
            fprintf(stderr, "Error: Invalid GUID '%s'\n", type_guid_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        has_filter = true;
    }
    args->filter_guid = filter_guid;
    args->has_filter = has_filter;
    return NMO_CLI_EXIT_SUCCESS;
}

static int parameter_dump_run(nmo_cmd_ctx_t *ctx, const parameter_dump_args_t *args,
                              bool close_ctx)
{
    nmo_cmd_ctx_t c = *ctx;
    bool dump_all = args->dump_all;
    bool has_filter = args->has_filter;
    nmo_guid_t filter_guid = args->filter_guid;
    uint32_t object_id = args->object_id;
    int rc = NMO_CLI_EXIT_SUCCESS;
    nmo_object_repository_t *repo = nmo_tool_owner_repository(c.workspace);

    /* JSON setup */
    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *jdata = NULL;
    yyjson_mut_val *jarr = NULL;
    if (c.is_json) {
        doc = nmo_cmd_ctx_json_begin(&c);
        jdata = yyjson_mut_obj(doc);
        jarr = yyjson_mut_arr(doc);
    }

    size_t dump_count = 0;

    parameter_target_list_t all_targets = {0};
    nmo_object_t *single_target = NULL;
    nmo_object_t **targets = NULL;
    size_t target_count = 0;

    if (dump_all) {
        nmo_object_query_t query = {
            .predicate = parameter_query_predicate,
            .predicate_user_data = (void *)c.registry,
        };
        rc = nmo_core_object_query_run(&c, &query,
                                       parameter_target_collect_visitor,
                                       &all_targets, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS || all_targets.oom) {
            free(all_targets.items);
            fprintf(stderr, "Error: Failed to collect parameters\n");
            return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        targets = all_targets.items;
        target_count = all_targets.count;
    } else {
        single_target = repo ? nmo_object_repository_find_by_id(repo, object_id) : NULL;
        if (!single_target) {
            fprintf(stderr, "Error: Object #%u not found\n", object_id);
            return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR)
                             : NMO_CLI_EXIT_ARG_ERROR;
        }

        nmo_class_id_t cid = nmo_object_get_class_id(single_target);
        if (!is_parameter_class(c.registry, cid)) {
            fprintf(stderr, "Error: Object #%u is not a parameter (class %u)\n", object_id, cid);
            return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR)
                             : NMO_CLI_EXIT_ARG_ERROR;
        }
        targets = &single_target;
        target_count = 1;
    }

    for (size_t i = 0; i < target_count; ++i) {
        nmo_object_t *obj = targets[i];
        nmo_class_id_t class_id = nmo_object_get_class_id(obj);

        if (dump_all && !is_parameter_class(c.registry, class_id)) {
            continue;
        }

        /* Apply type GUID filter if specified */
        if (has_filter) {
            const void *st = nmo_object_get_state(obj);
            nmo_guid_t obj_type_guid = NMO_GUID_NULL;

            if (class_id == NMO_CID_PARAMETERIN) {
                const nmo_parameterin_state_t *pin = (const nmo_parameterin_state_t *)st;
                if (pin) obj_type_guid = pin->type_guid;
            } else if (st) {
                const nmo_parameter_state_t *pst = (const nmo_parameter_state_t *)st;
                obj_type_guid = pst->type_guid;
            }

            if (!nmo_guid_equals(obj_type_guid, filter_guid)) {
                if (!dump_all) {
                    fprintf(stderr, "Error: Parameter #%u type does not match filter\n", object_id);
                    free(all_targets.items);
                    return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR)
                                     : NMO_CLI_EXIT_ARG_ERROR;
                }
                continue;
            }
        }

        if (c.is_json) {
            /* Emit JSON object per parameter -- mirrors parameter show JSON */
            yyjson_mut_val *item = yyjson_mut_obj(doc);
            nmo_object_id_t oid = nmo_object_get_id(obj);
            yyjson_mut_obj_add_uint(doc, item, "id", oid);
            yyjson_mut_obj_add_uint(doc, item, "class_id", class_id);
            const char *cn = nmo_cli_class_name_from_id(c.ctx, class_id);
            if (cn) nmo_cli_json_add_str_safe(doc, item, "class_name", cn);
            const char *nm = nmo_object_get_name(obj);
            if (nm && nm[0]) nmo_cli_json_add_str_safe(doc, item, "name", nm);

            const void *state = nmo_object_get_state(obj);
            if (state) {
                nmo_guid_t tg = NMO_GUID_NULL;
                const nmo_parameter_state_t *pstate = NULL;
                nmo_object_id_t owner_id = 0;
                nmo_object_id_t source_id = 0;
                uint8_t is_shared = 0;
                uint32_t dest_count = 0;

                if (class_id == NMO_CID_PARAMETERIN) {
                    const nmo_parameterin_state_t *pin = (const nmo_parameterin_state_t *)state;
                    tg = pin->type_guid;
                    owner_id = pin->owner_id;
                    source_id = pin->source_id;
                    is_shared = pin->is_shared;
                } else if (class_id == NMO_CID_PARAMETEROUT) {
                    const nmo_parameterout_state_t *pout = (const nmo_parameterout_state_t *)state;
                    pstate = (const nmo_parameter_state_t *)state;
                    tg = pstate->type_guid;
                    owner_id = pout->owner_id;
                    dest_count = pout->destination_count;
                } else if (class_id == NMO_CID_PARAMETERLOCAL) {
                    const nmo_parameterlocal_state_t *ploc = (const nmo_parameterlocal_state_t *)state;
                    pstate = (const nmo_parameter_state_t *)state;
                    tg = pstate->type_guid;
                    owner_id = ploc->owner_id;
                } else if (class_id != NMO_CID_PARAMETEROPERATION) {
                    pstate = (const nmo_parameter_state_t *)state;
                    tg = pstate->type_guid;
                }

                const char *tn = NULL;
                if (class_id == NMO_CID_PARAMETERIN && !nmo_guid_is_null(tg)) {
                    tn = nmo_type_registry_guid_to_name(c.registry, tg);
                } else if (pstate) {
                    tn = nmo_behavior_param_type_name(pstate, c.registry);
                }
                if (tn) nmo_cli_json_add_str_safe(doc, item, "type_name", tn);

                if (!nmo_guid_is_null(tg)) {
                    char gbuf[64];
                    nmo_guid_format(tg, gbuf, sizeof(gbuf));
                    nmo_cli_json_add_str_safe(doc, item, "type_guid", gbuf);
                }

                if (pstate) {
                    nmo_cli_json_add_str_safe(doc, item, "mode",
                        nmo_behavior_param_mode_to_string(pstate->mode));
                    char *vbuf = format_parameter_value(
                        pstate, (nmo_type_registry_t *)c.registry, c.workspace, NULL);
                    if (vbuf) {
                        nmo_cli_json_add_str_safe(doc, item, "value", vbuf);
                        free(vbuf);
                    }
                    if (pstate->buffer_data.data) {
                        yyjson_mut_obj_add_uint(doc, item, "buffer_size",
                            (uint64_t)pstate->buffer_data.count);
                    }
                }

                if (owner_id) yyjson_mut_obj_add_uint(doc, item, "owner_id", owner_id);
                if (source_id) {
                    yyjson_mut_obj_add_uint(doc, item, "source_id", source_id);
                    yyjson_mut_obj_add_bool(doc, item, "is_shared", is_shared != 0);
                }
                if (class_id == NMO_CID_PARAMETERIN) {
                    parameter_add_parameterin_resolution_json(
                        doc, item, c.ctx, c.workspace, repo, c.registry, oid);
                }
                if (class_id == NMO_CID_PARAMETEROPERATION) {
                    const nmo_parameteroperation_state_t *op =
                        (const nmo_parameteroperation_state_t *)state;
                    parameter_add_operation_json(doc, item, repo, c.registry, op);
                }
                if (dest_count) yyjson_mut_obj_add_uint(doc, item, "destination_count", dest_count);
            }

            yyjson_mut_arr_add_val(jarr, item);
        } else {
            dump_parameter_details(obj, c.ctx, c.workspace, c.out);
        }
        dump_count++;
    }

    if (c.is_json) {
        yyjson_mut_obj_add_uint(doc, jdata, "count", (uint64_t)dump_count);
        yyjson_mut_obj_add_val(doc, jdata, "parameters", jarr);
        nmo_cmd_ctx_json_end(&c, doc, jdata, "parameter.dump");
    }

    free(all_targets.items);
    return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS) : NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_parameter_dump(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *usage =
        "nmo parameter dump [--all] [--type <guid>] <id> <file>\n"
        "       nmo parameter dump --all [--type <guid>] <file>";
    parameter_dump_args_t args;
    int rc = parameter_dump_parse(argc, argv, true, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    return parameter_dump_run(&c, &args, true);
}

static int nmo_cmd_parameter_dump_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv) {
    const char *usage =
        "parameter dump [--all] [--type <guid>] [<id>]\n"
        "       parameter dump --all [--type <guid>]";
    parameter_dump_args_t args;
    int rc = parameter_dump_parse(argc, argv, false, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    return parameter_dump_run(ctx, &args, false);
}

/* ============================================================================
 * parameter set - Set a parameter value and save
 *
 *   nmo parameter set <param-id> <value> <file> -o <output>
 *   nmo parameter set --owner <beh-id> --name "Speed" <value> <file> -o <output>
 *   nmo parameter set --owner <beh-id> --index 2 <value> <file> -o <output>
 *   nmo parameter set --hex <param-id> <hex-value> <file> -o <output>
 *   nmo parameter set --dry-run <param-id> <value> <file>
 * ============================================================================ */

/* get_mutable_pstate moved to library: nmo_parameter_get_mutable_state() */

/**
 * @brief Find a settable parameter by owner behavior + name.
 *
 * Looks up the parameter in the owner's behavior arrays, then skips
 * ParameterIn (no buffer data).
 */
static nmo_object_t *find_param_by_owner_name(
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *registry,
    nmo_object_t *owner_obj,
    const char *param_name)
{
    (void)registry;
    nmo_object_t *pobj =
        find_behavior_parameter_by_name(repo, owner_obj, param_name);
    if (!pobj) return NULL;
    /* ParameterIn has no buffer data -- skip it for set operations */
    if (nmo_object_get_class_id(pobj) == NMO_CID_PARAMETERIN) return NULL;
    return pobj;
}

/**
 * @brief Find a parameter by owner behavior + flat index.
 *
 * Index counts across out_parameters then local_parameters (skips
 * in_parameters since they have no buffer data).
 */
static nmo_object_t *find_param_by_owner_index(
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *registry,
    nmo_object_t *owner_obj,
    uint32_t flat_index)
{
    const void *owner_state = nmo_object_get_state(owner_obj);
    if (!owner_state) return NULL;

    const nmo_behavior_state_t *bstate = (const nmo_behavior_state_t *)owner_state;

    const nmo_array_t *arrays[] = {
        &bstate->out_parameters,
        &bstate->local_parameters,
    };

    uint32_t running = 0;
    for (int a = 0; a < 2; a++) {
        const nmo_array_t *arr = arrays[a];
        if (!arr->data || arr->count == 0) continue;
        const nmo_object_id_t *ids = (const nmo_object_id_t *)arr->data;
        for (size_t i = 0; i < arr->count; i++) {
            nmo_object_t *pobj = nmo_object_repository_find_by_id(repo, ids[i]);
            if (!pobj) continue;
            nmo_class_id_t pcid = nmo_object_get_class_id(pobj);
            if (!is_parameter_class(registry, pcid)) continue;
            if (pcid == NMO_CID_PARAMETERIN) continue;

            if (running == flat_index) {
                return pobj;
            }
            running++;
        }
    }
    return NULL;
}

typedef struct parameter_set_args {
    const char *id_str;
    const char *owner_str;
    const char *name_str;
    const char *value_str;
    uint32_t param_index;
    bool has_index;
    bool hex_mode;
    nmo_object_id_t param_id;
    const char *param_name;
    const char *type_name;
    const char *mode_name;
    char *old_value_str;
    char *new_value_str;
    nmo_edit_plan_t *edit_plan;
    nmo_edit_report_t edit_report;
    bool edit_report_ready;
} parameter_set_args_t;

static int parameter_reject_in_session_output_option(bool present)
{
    if (present) {
        fprintf(stderr, "Error: REPL mutations update the loaded session; use 'save <path>' to write a file\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static char *parameter_strdup_malloc(const char *src)
{
    if (src == NULL) {
        return NULL;
    }
    size_t len = strlen(src) + 1u;
    char *copy = (char *)malloc(len);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, src, len);
    return copy;
}

static void parameter_set_args_cleanup(parameter_set_args_t *args)
{
    if (args == NULL) {
        return;
    }
    free(args->old_value_str);
    free(args->new_value_str);
    args->old_value_str = NULL;
    args->new_value_str = NULL;
    nmo_edit_plan_destroy(args->edit_plan);
    args->edit_plan = NULL;
    if (args->edit_report_ready) {
        nmo_edit_report_dispose(&args->edit_report);
        args->edit_report_ready = false;
    }
}

static int parameter_set_exit_code(nmo_status_t status)
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

static int parameter_set_execute_plan(
    nmo_cmd_ctx_t *c,
    parameter_set_args_t *args,
    bool dry_run)
{
    nmo_status_t rc = nmo_edit_report_init(&args->edit_report);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to initialize parameter set report: %s\n",
                nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    args->edit_report_ready = true;

    nmo_edit_executor_options_t options = nmo_edit_executor_options_default();
    options.dry_run = dry_run;
    rc = nmo_edit_executor_execute(
        c->workspace, args->edit_plan, &options, &args->edit_report);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to set parameter value: %s\n",
                nmo_error_string(rc));
        return parameter_set_exit_code(rc);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int parameter_set_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    parameter_set_args_t *args = (parameter_set_args_t *)user_data;
    if (c == NULL || args == NULL || args->value_str == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (!c->registry) {
        fprintf(stderr, "Error: Type registry unavailable\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_repository_t *repo = nmo_tool_owner_repository(c->workspace);
    nmo_object_t *param_obj = NULL;

    if (args->owner_str != NULL) {
        uint32_t owner_id;
        if (!nmo_tool_parse_u32(args->owner_str, &owner_id)) {
            fprintf(stderr, "Error: Invalid owner ID '%s'\n", args->owner_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        nmo_object_t *owner_obj = repo ? nmo_object_repository_find_by_id(repo, owner_id) : NULL;
        if (!owner_obj) {
            fprintf(stderr, "Error: Owner object #%u not found\n", owner_id);
            return NMO_CLI_EXIT_NOT_FOUND;
        }
        nmo_class_id_t owner_cid = nmo_object_get_class_id(owner_obj);
        if (!parameter_is_behavior_class(c->registry, owner_cid)) {
            fprintf(stderr, "Error: Owner object #%u is not a CKBehavior (class %u)\n",
                    owner_id, owner_cid);
            return NMO_CLI_EXIT_ARG_ERROR;
        }

        if (args->name_str) {
            param_obj = find_param_by_owner_name(repo, c->registry, owner_obj, args->name_str);
            if (!param_obj) {
                fprintf(stderr, "Error: No parameter named '%s' in owner #%u\n",
                        args->name_str, owner_id);
                return NMO_CLI_EXIT_NOT_FOUND;
            }
        } else if (args->has_index) {
            param_obj = find_param_by_owner_index(
                repo, c->registry, owner_obj, args->param_index);
            if (!param_obj) {
                fprintf(stderr, "Error: Parameter index %u out of range in owner #%u\n",
                        args->param_index, owner_id);
                return NMO_CLI_EXIT_NOT_FOUND;
            }
        } else {
            fprintf(stderr, "Error: --owner requires --name or --index\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    } else {
        uint32_t param_id;
        if (!nmo_tool_parse_u32(args->id_str, &param_id)) {
            fprintf(stderr, "Error: Invalid parameter ID '%s'\n", args->id_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        param_obj = repo ? nmo_object_repository_find_by_id(repo, param_id) : NULL;
        if (!param_obj) {
            fprintf(stderr, "Error: Object #%u not found\n", param_id);
            return NMO_CLI_EXIT_NOT_FOUND;
        }
        nmo_class_id_t cid = nmo_object_get_class_id(param_obj);
        if (!is_parameter_class(c->registry, cid)) {
            fprintf(stderr, "Error: Object #%u is not a parameter (class %u)\n",
                    param_id, cid);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (cid == NMO_CID_PARAMETERIN) {
            fprintf(stderr, "Error: ParameterIn objects have no buffer data to set\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }

    nmo_parameter_state_t *pstate = nmo_parameter_get_mutable_state(param_obj);
    if (!pstate) {
        fprintf(stderr, "Error: Cannot access parameter state\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    args->param_id = nmo_object_get_id(param_obj);
    args->old_value_str = format_parameter_value(
        pstate, (nmo_type_registry_t *)c->registry, c->workspace, NULL);
    args->param_name = nmo_object_get_name(param_obj);
    args->type_name = nmo_behavior_param_type_name(pstate, (nmo_type_registry_t *)c->registry);
    args->mode_name = nmo_behavior_param_mode_to_string(pstate->mode);

    nmo_status_t plan_rc = nmo_edit_plan_create(&args->edit_plan);
    if (plan_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to create parameter edit plan: %s\n",
                nmo_error_string(plan_rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (args->hex_mode) {
        if (pstate->mode != CKPARAM_MODE_BUFFER) {
            fprintf(stderr, "Error: --hex only supported for MODE_BUFFER parameters\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }

        size_t max_len = strlen(args->value_str) / 2 + 1;
        uint8_t *hex_buf = (uint8_t *)malloc(max_len);
        if (!hex_buf) {
            fprintf(stderr, "Error: Out of memory\n");
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        size_t hex_len = 0;
        nmo_status_t parse_rc =
            nmo_parse_hex_bytes(args->value_str, hex_buf, max_len, &hex_len);
        if (parse_rc != NMO_OK) {
            fprintf(stderr, "Error: Invalid hex string '%s'\n", args->value_str);
            free(hex_buf);
            return NMO_CLI_EXIT_ARG_ERROR;
        }

        plan_rc = nmo_edit_plan_add_set_parameter_bytes(
            args->edit_plan, args->param_id, NULL, hex_buf, hex_len, NULL);
        free(hex_buf);
        if (plan_rc != NMO_OK) {
            fprintf(stderr, "Error: Failed to add raw parameter write op: %s\n",
                    nmo_error_string(plan_rc));
            return parameter_set_exit_code(plan_rc);
        }
    } else if (pstate->mode == CKPARAM_MODE_BUFFER) {
        const nmo_type_descriptor_t *type_desc =
            nmo_type_registry_find_by_guid(c->registry, pstate->type_guid);
        if (!type_desc) {
            fprintf(stderr, "Error: Unknown parameter type\n");
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        if (!pstate->buffer_data.data || pstate->buffer_data.count == 0) {
            fprintf(stderr, "Error: Parameter has no buffer data\n");
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        (void)type_desc;
        plan_rc = nmo_edit_plan_add_set_parameter_value(
            args->edit_plan, args->param_id, NULL, args->value_str, NULL);
        if (plan_rc != NMO_OK) {
            fprintf(stderr, "Error: Failed to add parameter value write op: %s\n",
                    nmo_error_string(plan_rc));
            return parameter_set_exit_code(plan_rc);
        }
    } else if (pstate->mode == CKPARAM_MODE_OBJECT) {
        uint32_t ref_id = 0;
        if (args->value_str[0] == '#') {
            if (!nmo_tool_parse_u32(args->value_str + 1, &ref_id)) {
                fprintf(stderr, "Error: Invalid object ID '%s'\n", args->value_str);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
        } else {
            nmo_object_t *ref_obj = nmo_object_repository_find_by_name(repo, args->value_str);
            if (!ref_obj) {
                if (nmo_tool_parse_u32(args->value_str, &ref_id)) {
                    /* parsed below */
                } else {
                    fprintf(stderr, "Error: Object '%s' not found\n", args->value_str);
                    return NMO_CLI_EXIT_NOT_FOUND;
                }
            } else {
                ref_id = nmo_object_get_id(ref_obj);
            }
        }
        char ref_buf[32];
        snprintf(ref_buf, sizeof(ref_buf), "%u", ref_id);
        plan_rc = nmo_edit_plan_add_set_parameter_value(
            args->edit_plan, args->param_id, NULL, ref_buf, NULL);
        if (plan_rc != NMO_OK) {
            fprintf(stderr, "Error: Failed to add object reference write op: %s\n",
                    nmo_error_string(plan_rc));
            return parameter_set_exit_code(plan_rc);
        }
    } else {
        fprintf(stderr, "Error: Unsupported parameter mode '%s'\n", args->mode_name);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    int exec_rc = parameter_set_execute_plan(c, args, dry_run);
    if (exec_rc != NMO_CLI_EXIT_SUCCESS) {
        return exec_rc;
    }

    if (dry_run) {
        args->new_value_str = parameter_strdup_malloc(args->value_str);
    } else {
        args->new_value_str = format_parameter_value(
            pstate, (nmo_type_registry_t *)c->registry, c->workspace, NULL);
    }
    if (!args->new_value_str) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static int parameter_set_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    parameter_set_args_t *args = (parameter_set_args_t *)user_data;
    if (c == NULL || args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        if (!doc) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        if (args->edit_report_ready && !dry_run && output_path != NULL) {
            (void)nmo_edit_report_set_output_path(&args->edit_report, output_path);
        }
        nmo_cli_edit_report_add_schema_v2_json(
            doc, data,
            args->edit_report_ready ? &args->edit_report : NULL,
            dry_run);
        yyjson_mut_obj_add_uint(doc, data, "id", args->param_id);
        if (args->param_name && args->param_name[0])
            nmo_cli_json_add_str_safe(doc, data, "name", args->param_name);
        if (args->type_name)
            nmo_cli_json_add_str_safe(doc, data, "type", args->type_name);
        nmo_cli_json_add_str_safe(doc, data, "mode", args->mode_name);
        if (args->old_value_str)
            nmo_cli_json_add_str_safe(doc, data, "old_value", args->old_value_str);
        if (args->new_value_str)
            nmo_cli_json_add_str_safe(doc, data, "new_value", args->new_value_str);
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }

        nmo_cmd_ctx_json_end(c, doc, data, "parameter.set");
    } else {
        fprintf(c->out, "Parameter #%u", args->param_id);
        if (args->param_name && args->param_name[0])
            fprintf(c->out, " (%s)", args->param_name);
        fprintf(c->out, "\n");
        if (args->type_name)
            fprintf(c->out, "  Type:  %s\n", args->type_name);
        fprintf(c->out, "  Old:   %s\n", args->old_value_str ? args->old_value_str : "(none)");
        fprintf(c->out, "  New:   %s\n", args->new_value_str ? args->new_value_str : "(none)");

        if (dry_run) {
            fprintf(c->out, "  (dry run - not saved)\n");
        } else if (output_path) {
            fprintf(c->out, "Saved to: %s\n", output_path);
        }
    }

    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_parameter_set(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o", NMO_OPT_STRING, "Output file (required unless --dry-run)"},
        {"--owner",   "-b", NMO_OPT_STRING, "Owner behavior/object ID"},
        {"--name",    "-n", NMO_OPT_STRING, "Parameter name within owner"},
        {"--index",   "-i", NMO_OPT_UINT,   "Parameter index within owner"},
        {"--hex",     NULL, NMO_OPT_FLAG,   "Value is raw hex bytes"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Show old/new without saving"},
        {"--id",      NULL, NMO_OPT_UINT,   "Parameter object ID"},
    };
    enum { OPT_OUTPUT, OPT_OWNER, OPT_NAME, OPT_INDEX, OPT_HEX, OPT_DRYRUN,
           OPT_ID, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    const char *owner_str   = vals[OPT_OWNER].present  ? vals[OPT_OWNER].val.str  : NULL;
    const char *name_str    = vals[OPT_NAME].present   ? vals[OPT_NAME].val.str   : NULL;
    bool has_index          = vals[OPT_INDEX].present;
    uint32_t param_index    = has_index ? vals[OPT_INDEX].val.u : 0;
    bool hex_mode           = vals[OPT_HEX].present && vals[OPT_HEX].val.flag;
    bool dry_run            = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
    bool has_direct_id      = vals[OPT_ID].present;
    char direct_id_buf[32];
    if (has_direct_id) {
        snprintf(direct_id_buf, sizeof(direct_id_buf), "%u", vals[OPT_ID].val.u);
    }

    /* Determine positional args layout */
    bool owner_mode = (owner_str != NULL);
    const char *id_str = NULL;
    const char *value_str = NULL;
    const char *file_path = NULL;

    if (owner_mode) {
        /* --owner <beh-id> [--name|--index] <value> <file> */
        if (r.pos_count < 2) {
            fprintf(stderr, "Usage: nmo parameter set --owner <beh-id> "
                    "[--name <name> | --index <n>] <value> <file> -o <output>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        value_str = r.pos_args[0];
        file_path = r.pos_args[r.pos_count - 1];
    } else {
        /* [--id <param-id> | <param-id>] <value> <file> */
        if ((has_direct_id && r.pos_count < 2) || (!has_direct_id && r.pos_count < 3)) {
            fprintf(stderr, "Usage: nmo parameter set [--id <param-id> | <param-id>] <value> <file> -o <output>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        id_str    = has_direct_id ? direct_id_buf : r.pos_args[0];
        value_str = has_direct_id ? r.pos_args[0] : r.pos_args[1];
        file_path = r.pos_args[r.pos_count - 1];
    }

    parameter_set_args_t args = {
        .id_str = id_str,
        .owner_str = owner_str,
        .name_str = name_str,
        .value_str = value_str,
        .param_index = param_index,
        .has_index = has_index,
        .hex_mode = hex_mode,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "parameter.set",
        .output_required_unless_dry_run = true,
    };
    int rc = nmo_cli_run_write_command(
        file_path,
        output_path,
        dry_run,
        global,
        &spec,
        parameter_set_mutate,
        parameter_set_report,
        &args);
    parameter_set_args_cleanup(&args);
    return rc;
}

int nmo_cmd_parameter_set_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv,
                                     nmo_cmd_in_session_result_t *result)
{
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o", NMO_OPT_STRING, "Output file (required unless --dry-run)"},
        {"--owner",   "-b", NMO_OPT_STRING, "Owner behavior/object ID"},
        {"--name",    "-n", NMO_OPT_STRING, "Parameter name within owner"},
        {"--index",   "-i", NMO_OPT_UINT,   "Parameter index within owner"},
        {"--hex",     NULL, NMO_OPT_FLAG,   "Value is raw hex bytes"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Show old/new without saving"},
        {"--id",      NULL, NMO_OPT_UINT,   "Parameter object ID"},
    };
    enum { OPT_OUTPUT, OPT_OWNER, OPT_NAME, OPT_INDEX, OPT_HEX, OPT_DRYRUN,
           OPT_ID, OPT_COUNT };

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (parameter_reject_in_session_output_option(vals[OPT_OUTPUT].present) != NMO_CLI_EXIT_SUCCESS) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
    if (result != NULL) {
        result->dry_run = dry_run;
    }

    const char *owner_str = vals[OPT_OWNER].present ? vals[OPT_OWNER].val.str : NULL;
    bool has_direct_id = vals[OPT_ID].present;
    char direct_id_buf[32];
    if (has_direct_id) {
        snprintf(direct_id_buf, sizeof(direct_id_buf), "%u", vals[OPT_ID].val.u);
    }

    const char *id_str = NULL;
    const char *value_str = NULL;
    if (owner_str != NULL) {
        if (r.pos_count != 1) {
            fprintf(stderr, "Usage: parameter set --owner <object-id> "
                    "[--name <name> | --index <n>] <value>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        value_str = r.pos_args[0];
    } else {
        if ((has_direct_id && r.pos_count != 1) ||
            (!has_direct_id && r.pos_count != 2)) {
            fprintf(stderr, "Usage: parameter set [--id <param-id> | <param-id>] <value>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        id_str = has_direct_id ? direct_id_buf : r.pos_args[0];
        value_str = has_direct_id ? r.pos_args[0] : r.pos_args[1];
    }

    parameter_set_args_t args = {
        .id_str = id_str,
        .owner_str = owner_str,
        .name_str = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
        .value_str = value_str,
        .param_index = vals[OPT_INDEX].present ? vals[OPT_INDEX].val.u : 0,
        .has_index = vals[OPT_INDEX].present,
        .hex_mode = vals[OPT_HEX].present && vals[OPT_HEX].val.flag,
    };
    int rc = parameter_set_mutate(ctx, dry_run, NULL, &args);
    if (rc == NMO_CLI_EXIT_SUCCESS) {
        rc = parameter_set_report(ctx, dry_run, NULL, &args);
    }
    if (result != NULL && rc == NMO_CLI_EXIT_SUCCESS && !dry_run) {
        result->changed = true;
    }
    parameter_set_args_cleanup(&args);
    return rc;
}

int nmo_cmd_parameter_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: parameter list|show|dump ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "show") == 0 || strcmp(argv[0], "s") == 0) {
        return nmo_cmd_parameter_show_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "dump") == 0 || strcmp(argv[0], "d") == 0) {
        return nmo_cmd_parameter_dump_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "list") == 0 || strcmp(argv[0], "ls") == 0) {
        return parameter_list_run(ctx);
    }

    fprintf(stderr, "Unsupported parameter read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
}

