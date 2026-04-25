/**
 * @file nmo_cmd_debug.c
 * @brief CLI debug command group implementation (non-interactive diagnostics)
 */

#include "nmo_cmd_debug.h"

#include "../nmo_cmd_core.h"
#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_json.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_write.h"
#include "../nmo_tool_common.h"
#include "../nmo_opt.h"

#include "behavior/nmo_edit_plan.h"
#include "nmo.h"
#include "document/nmo_document_stats.h"
#include "document/nmo_document_save.h"
#include "runtime/nmo_context.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct nmo_debug_chunks_data {
    yyjson_mut_doc *doc;
    yyjson_mut_val *chunks;
    nmo_cli_table_t *table;
    size_t chunk_count;
} nmo_debug_chunks_data_t;

typedef struct nmo_debug_probe_args {
    const char *kind;
    nmo_object_id_t behavior_id;
    const char *name;
    nmo_edit_report_t report;
} nmo_debug_probe_args_t;

static const char *debug_probe_edit_op_kind_string(nmo_edit_op_kind_t kind) {
    switch (kind) {
    case NMO_EDIT_OP_ADD_NODE:
        return "add_node";
    default:
        return "edit";
    }
}

static void debug_probe_add_impact_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const char *name,
    const nmo_edit_object_impact_t *items,
    size_t count)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t i = 0; items != NULL && i < count; ++i) {
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, item, "object_id",
                                (uint64_t)items[i].id);
        yyjson_mut_obj_add_uint(doc, item, "id", (uint64_t)items[i].id);
        nmo_cli_json_add_str_safe(
            doc, item, "cause",
            debug_probe_edit_op_kind_string(items[i].cause));
        nmo_cli_json_add_str_safe(doc, item, "role", items[i].role);
        yyjson_mut_arr_add_val(arr, item);
    }
    yyjson_mut_obj_add_val(doc, obj, name, arr);
}

static void debug_probe_add_operations_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const nmo_edit_report_t *report)
{
    yyjson_mut_val *ops = yyjson_mut_arr(doc);
    for (size_t i = 0; report != NULL && i < report->operation_count; ++i) {
        const nmo_edit_operation_result_t *op = &report->operations[i];
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        yyjson_mut_val *handles = yyjson_mut_arr(doc);
        yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)(i + 1u));
        nmo_cli_json_add_str_safe(
            doc, item, "op", debug_probe_edit_op_kind_string(op->kind));
        nmo_cli_json_add_str_safe(
            doc, item, "kind", debug_probe_edit_op_kind_string(op->kind));
        yyjson_mut_obj_add_uint(doc, item, "primary_id",
                                (uint64_t)op->primary_id);
        yyjson_mut_obj_add_uint(doc, item, "result_id",
                                (uint64_t)op->result_id);
        yyjson_mut_obj_add_uint(doc, item, "status", (uint64_t)op->status);
        nmo_cli_json_add_str_safe(doc, item, "status_name",
                                  nmo_error_string(op->status));
        if (op->diagnostic_code != NULL) {
            nmo_cli_json_add_str_safe(doc, item, "diagnostic_code",
                                      op->diagnostic_code);
        }
        if (op->diagnostic_message != NULL) {
            nmo_cli_json_add_str_safe(doc, item, "diagnostic_message",
                                      op->diagnostic_message);
        }
        for (size_t j = 0; j < op->handle_count; ++j) {
            yyjson_mut_val *handle = yyjson_mut_obj(doc);
            nmo_cli_json_add_str_safe(doc, handle, "name",
                                      op->handles[j].name);
            yyjson_mut_obj_add_uint(doc, handle, "object_id",
                                    (uint64_t)op->handles[j].id);
            yyjson_mut_obj_add_uint(doc, handle, "id",
                                    (uint64_t)op->handles[j].id);
            yyjson_mut_arr_add_val(handles, handle);
        }
        yyjson_mut_obj_add_val(doc, item, "handles", handles);
        yyjson_mut_arr_add_val(ops, item);
    }
    yyjson_mut_obj_add_val(doc, obj, "operations", ops);
}

static const char *debug_probe_risk_severity_string(
    nmo_behavior_semantic_risk_severity_t severity)
{
    switch (severity) {
    case NMO_BEHAVIOR_SEMANTIC_RISK_SAFE:
        return "safe";
    case NMO_BEHAVIOR_SEMANTIC_RISK_WARN:
        return "warn";
    case NMO_BEHAVIOR_SEMANTIC_RISK_REJECT:
        return "reject";
    default:
        return "warn";
    }
}

static void debug_probe_add_semantic_risks_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const nmo_edit_report_t *report)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t i = 0; report != NULL && i < report->semantic_risk_count; ++i) {
        const nmo_behavior_semantic_risk_t *risk = &report->semantic_risks[i];
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        nmo_cli_json_add_str_safe(
            doc, item, "severity",
            debug_probe_risk_severity_string(risk->severity));
        nmo_cli_json_add_str_safe(doc, item, "code", risk->code);
        nmo_cli_json_add_str_safe(doc, item, "message", risk->message);
        yyjson_mut_obj_add_uint(doc, item, "object_id",
                                (uint64_t)risk->object_id);
        yyjson_mut_arr_add_val(arr, item);
    }
    yyjson_mut_obj_add_val(doc, obj, "semantic_risks", arr);
}

static void debug_probe_add_validation_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const nmo_edit_report_t *report)
{
    const nmo_edit_validation_report_t zero = {0};
    const nmo_edit_validation_report_t *validation =
        report != NULL ? &report->validation : &zero;
    yyjson_mut_val *item = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, item, "final_status",
                            (uint64_t)validation->final_status);
    nmo_cli_json_add_str_safe(doc, item, "final_status_name",
                              nmo_error_string(validation->final_status));
    yyjson_mut_obj_add_uint(doc, item, "roundtrip_status",
                            (uint64_t)validation->roundtrip_status);
    nmo_cli_json_add_str_safe(doc, item, "roundtrip_status_name",
                              nmo_error_string(validation->roundtrip_status));
    yyjson_mut_obj_add_uint(doc, item, "reference_status",
                            (uint64_t)validation->reference_status);
    nmo_cli_json_add_str_safe(doc, item, "reference_status_name",
                              nmo_error_string(validation->reference_status));
    yyjson_mut_obj_add_uint(doc, item, "behavior_index_status",
                            (uint64_t)validation->behavior_index_status);
    nmo_cli_json_add_str_safe(doc, item, "behavior_index_status_name",
                              nmo_error_string(validation->behavior_index_status));
    yyjson_mut_obj_add_uint(doc, item, "interface_status",
                            (uint64_t)validation->interface_status);
    nmo_cli_json_add_str_safe(doc, item, "interface_status_name",
                              nmo_error_string(validation->interface_status));
    yyjson_mut_obj_add_val(doc, obj, "validation", item);
}

static int debug_probe_parse(int argc,
                             char **argv,
                             nmo_debug_probe_args_t *args,
                             const char **out_input_path,
                             const char **out_output_path,
                             bool *out_dry_run)
{
    if (argc < 2 || argv == NULL || args == NULL || out_input_path == NULL ||
        out_output_path == NULL || out_dry_run == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    memset(args, 0, sizeof(*args));
    args->kind = argv[1];
    args->name = "nmo debug probe";
    *out_input_path = NULL;
    *out_output_path = NULL;
    *out_dry_run = false;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--behavior") == 0 && i + 1 < argc) {
            args->behavior_id = (nmo_object_id_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            args->name = argv[++i];
        } else if ((strcmp(argv[i], "-o") == 0 ||
                    strcmp(argv[i], "--output") == 0) &&
                   i + 1 < argc) {
            *out_output_path = argv[++i];
        } else if (strcmp(argv[i], "--dry-run") == 0) {
            *out_dry_run = true;
        } else if (argv[i][0] != '-') {
            *out_input_path = argv[i];
        } else {
            fprintf(stderr, "Error: Unsupported debug probe option '%s'\n",
                    argv[i]);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }

    if (strcmp(args->kind, "2d-text") != 0 &&
        strcmp(args->kind, "console") != 0 &&
        strcmp(args->kind, "control-marker") != 0) {
        fprintf(stderr, "Error: Unsupported debug probe kind '%s'\n",
                args->kind);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (args->behavior_id == 0u || *out_input_path == NULL) {
        fprintf(stderr,
                "Usage: nmo debug probe 2d-text|console|control-marker "
                "--behavior <id> [--name <name>] [--dry-run] <file> "
                "-o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int debug_probe_mutate(nmo_cmd_ctx_t *ctx,
                              bool dry_run,
                              const char *output_path,
                              void *user_data)
{
    (void)output_path;
    nmo_debug_probe_args_t *args = (nmo_debug_probe_args_t *)user_data;
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = NMO_OK;
    const nmo_guid_t bb_2d_text = NMO_GUID(0x055B29FEu, 0x662D5CA0u);
    const nmo_guid_t bb_output_to_console =
        NMO_GUID(0x18655B3Fu, 0x68291DC3u);
    const nmo_guid_t bb_nop = NMO_GUID(0x302561C4u, 0x0D282980u);
    nmo_guid_t probe_guid = bb_2d_text;

    if (ctx == NULL || args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_edit_report_init(&args->report);
    if (strcmp(args->kind, "console") == 0) {
        probe_guid = bb_output_to_console;
    } else if (strcmp(args->kind, "control-marker") == 0) {
        probe_guid = bb_nop;
    }
    status = nmo_edit_plan_create(&plan);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_node(
            plan, args->behavior_id, probe_guid, args->name);
    }
    if (status == NMO_OK) {
        nmo_edit_executor_options_t options =
            nmo_edit_executor_options_default();
        options.dry_run = dry_run;
        status = nmo_edit_executor_execute(
            ctx->workspace, plan, &options, &args->report);
    }
    nmo_edit_plan_destroy(plan);
    if (status != NMO_OK) {
        fprintf(stderr, "Error: debug probe failed: %s\n",
                nmo_error_string(status));
        return status == NMO_ERR_INVALID_ARGUMENT || status == NMO_ERR_NOT_FOUND
            ? NMO_CLI_EXIT_ARG_ERROR
            : NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int debug_probe_report(nmo_cmd_ctx_t *ctx,
                              bool dry_run,
                              const char *output_path,
                              void *user_data)
{
    nmo_debug_probe_args_t *args = (nmo_debug_probe_args_t *)user_data;
    if (ctx == NULL || args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        if (doc == NULL) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "ok", args->report.ok);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_str_safe(doc, data, "probe_kind", args->kind);
        yyjson_mut_obj_add_uint(doc, data, "behavior_id",
                                (uint64_t)args->behavior_id);
        yyjson_mut_obj_add_uint(doc, data, "operation_count",
                                (uint64_t)args->report.operation_count);
        if (output_path != NULL) {
            nmo_cli_json_add_str_safe(doc, data, "output_path", output_path);
        }
        yyjson_mut_obj_add_val(doc, data, "errors", yyjson_mut_arr(doc));
        yyjson_mut_obj_add_val(doc, data, "warnings", yyjson_mut_arr(doc));
        debug_probe_add_operations_json(doc, data, &args->report);
        debug_probe_add_impact_json(
            doc, data, "changed_objects",
            args->report.changed_objects,
            args->report.changed_object_count);
        debug_probe_add_impact_json(
            doc, data, "created_objects",
            args->report.created_objects,
            args->report.created_object_count);
        debug_probe_add_impact_json(
            doc, data, "deleted_objects",
            args->report.deleted_objects,
            args->report.deleted_object_count);
        debug_probe_add_semantic_risks_json(doc, data, &args->report);
        debug_probe_add_validation_json(doc, data, &args->report);
        int rc = nmo_cmd_ctx_json_end(ctx, doc, data, "debug.probe");
        nmo_edit_report_dispose(&args->report);
        return rc;
    }

    fprintf(ctx->out, "%sInjected %zu debug probe operation(s)\n",
            dry_run ? "[dry-run] " : "",
            args->report.operation_count);
    if (!dry_run && output_path != NULL) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    nmo_edit_report_dispose(&args->report);
    return NMO_CLI_EXIT_SUCCESS;
}

static void debug_add_load_phase_stats_json(yyjson_mut_doc *doc,
                                            yyjson_mut_val *data,
                                            const nmo_load_perf_stats_t *stats) {
    if (doc == NULL || data == NULL || stats == NULL) {
        return;
    }

    yyjson_mut_val *phase_stats = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, phase_stats, "packed_header1_bytes", (uint64_t)stats->packed_header1_bytes);
    yyjson_mut_obj_add_uint(doc, phase_stats, "unpacked_header1_bytes", (uint64_t)stats->unpacked_header1_bytes);
    yyjson_mut_obj_add_uint(doc, phase_stats, "packed_data_bytes", (uint64_t)stats->packed_data_bytes);
    yyjson_mut_obj_add_uint(doc, phase_stats, "unpacked_data_bytes", (uint64_t)stats->unpacked_data_bytes);

    yyjson_mut_val *phases = yyjson_mut_obj(doc);
    for (int i = 0; i < NMO_LOAD_PERF_PHASE_COUNT; i++) {
        const nmo_phase_time_t *phase = &stats->phases[i];
        yyjson_mut_val *entry = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, entry, "calls", phase->calls);
        yyjson_mut_obj_add_real(doc, entry, "milliseconds", phase->milliseconds);
        yyjson_mut_obj_add_val(doc, phases, nmo_load_perf_phase_name((nmo_load_perf_phase_t)i), entry);
    }
    yyjson_mut_obj_add_val(doc, phase_stats, "phases", phases);
    yyjson_mut_obj_add_val(doc, data, "phase_stats", phase_stats);
}

static void debug_print_load_phase_stats(FILE *out, const nmo_load_perf_stats_t *stats) {
    if (out == NULL || stats == NULL) {
        return;
    }

    fprintf(out, "\nPhase Timings:\n");
    fprintf(out, "  %-28s %8s %12s\n", "phase", "calls", "ms");
    for (int i = 0; i < NMO_LOAD_PERF_PHASE_COUNT; i++) {
        const nmo_phase_time_t *phase = &stats->phases[i];
        fprintf(out, "  %-28s %8llu %12.3f\n",
                nmo_load_perf_phase_name((nmo_load_perf_phase_t)i),
                (unsigned long long)phase->calls,
                phase->milliseconds);
    }

    fprintf(out, "\nSection Bytes:\n");
    fprintf(out, "  Header1: packed=%zu unpacked=%zu\n",
            stats->packed_header1_bytes,
            stats->unpacked_header1_bytes);
    fprintf(out, "  Data:    packed=%zu unpacked=%zu\n",
            stats->packed_data_bytes,
            stats->unpacked_data_bytes);
}

static bool debug_load_profile_from_arg(const char *arg, nmo_load_profile_t *out_profile) {
    if (arg == NULL || out_profile == NULL) {
        return false;
    }
    if (strcmp(arg, "full") == 0) {
        *out_profile = NMO_LOAD_PROFILE_FULL;
        return true;
    }
    if (strcmp(arg, "metadata") == 0) {
        *out_profile = NMO_LOAD_PROFILE_METADATA;
        return true;
    }
    if (strcmp(arg, "header") == 0 || strcmp(arg, "header-only") == 0) {
        *out_profile = NMO_LOAD_PROFILE_HEADER_ONLY;
        return true;
    }
    return false;
}

static const char *debug_load_profile_name(nmo_load_profile_t profile) {
    switch (profile) {
        case NMO_LOAD_PROFILE_FULL:
            return "full";
        case NMO_LOAD_PROFILE_METADATA:
            return "metadata";
        case NMO_LOAD_PROFILE_HEADER_ONLY:
            return "header-only";
        default:
            return "unknown";
    }
}

static int debug_parse_load_profile(int argc, char **argv,
                                    nmo_load_profile_t *profile)
{
    *profile = NMO_LOAD_PROFILE_FULL;
    for (int i = 0; i < argc; i++) {
        const char *value = NULL;
        if (strncmp(argv[i], "--profile=", 10) == 0) {
            value = argv[i] + 10;
        } else if (strncmp(argv[i], "--load-profile=", 15) == 0) {
            value = argv[i] + 15;
        }

        if (value != NULL && !debug_load_profile_from_arg(value, profile)) {
            fprintf(stderr, "Error: Invalid load profile '%s'\n", value);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int debug_chunks_object(size_t index, nmo_object_t *obj,
                               const nmo_cmd_ctx_t *c, void *user)
{
    (void)index;

    nmo_debug_chunks_data_t *data = (nmo_debug_chunks_data_t *)user;
    if (!data || !obj) {
        return 0;
    }

    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    if (!chunk) {
        return 0;
    }

    if (data->doc && data->chunks) {
        yyjson_mut_val *cv = yyjson_mut_obj(data->doc);
        yyjson_mut_obj_add_uint(data->doc, cv, "id", nmo_object_get_id(obj));
        yyjson_mut_obj_add_uint(data->doc, cv, "class_id", chunk->class_id);
        yyjson_mut_obj_add_uint(data->doc, cv, "data_size",
                                (uint64_t)nmo_chunk_get_data_size(chunk));
        yyjson_mut_obj_add_uint(data->doc, cv, "compressed_size",
                                (uint64_t)chunk->compressed_size);
        yyjson_mut_obj_add_uint(data->doc, cv, "options", chunk->chunk_options);

        const char *class_name = nmo_cli_class_name_from_id(c->ctx, chunk->class_id);
        if (class_name) {
            yyjson_mut_obj_add_str(data->doc, cv, "class_name", class_name);
        }

        const char *name = nmo_object_get_name(obj);
        if (name && name[0]) {
            nmo_cli_json_add_str_safe(data->doc, cv, "name", name);
        }

        yyjson_mut_arr_add_val(data->chunks, cv);
    } else if (data->table) {
        char oid[16], cid[16], dsz[16], csz[16];
        char opt_buf[64];
        char opt_cell[96];
        snprintf(oid, sizeof(oid), "%u", nmo_object_get_id(obj));
        snprintf(cid, sizeof(cid), "%u", chunk->class_id);
        snprintf(dsz, sizeof(dsz), "%zu", nmo_chunk_get_data_size(chunk));
        snprintf(csz, sizeof(csz), "%zu", chunk->compressed_size);

        const char *opt = nmo_cli_chunk_options_to_string(chunk->chunk_options,
            opt_buf, sizeof(opt_buf));
        if (chunk->chunk_options == 0) {
            snprintf(opt_cell, sizeof(opt_cell), "-");
        } else {
            snprintf(opt_cell, sizeof(opt_cell), "%s (0x%04X)", opt, chunk->chunk_options);
        }

        const char *class_name = nmo_cli_class_name_from_id(c->ctx, chunk->class_id);

        const char *cells[] = {oid, cid, class_name ? class_name : "-", dsz, csz, opt_cell};
        (void)nmo_cli_table_add_row(data->table, cells, 6);
    }

    data->chunk_count++;
    return 0;
}

typedef struct nmo_debug_objects_data {
    yyjson_mut_doc *doc;
    yyjson_mut_val *objects;
    nmo_cli_table_t *table;
} nmo_debug_objects_data_t;

static int debug_objects_object(size_t index, nmo_object_t *obj,
                                const nmo_cmd_ctx_t *c, void *user)
{
    nmo_debug_objects_data_t *data = (nmo_debug_objects_data_t *)user;
    if (!data || !obj) {
        return 0;
    }

    if (data->doc && data->objects) {
        yyjson_mut_val *o = yyjson_mut_obj(data->doc);
        yyjson_mut_obj_add_uint(data->doc, o, "index", (uint64_t)index);
        yyjson_mut_obj_add_uint(data->doc, o, "id", nmo_object_get_id(obj));
        yyjson_mut_obj_add_uint(data->doc, o, "class_id", nmo_object_get_class_id(obj));
        yyjson_mut_obj_add_uint(data->doc, o, "flags", nmo_object_get_flags(obj));

        const char *name = nmo_object_get_name(obj);
        if (name && name[0]) {
            nmo_cli_json_add_str_safe(data->doc, o, "name", name);
        }

        const char *class_name = nmo_cli_class_name_from_id(c->ctx, nmo_object_get_class_id(obj));
        if (class_name) {
            yyjson_mut_obj_add_str(data->doc, o, "class_name", class_name);
        }

        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        yyjson_mut_obj_add_bool(data->doc, o, "has_chunk", chunk != NULL);
        if (chunk) {
            yyjson_mut_obj_add_uint(data->doc, o, "chunk_size",
                                    (uint64_t)nmo_chunk_get_data_size(chunk));
        }

        yyjson_mut_arr_add_val(data->objects, o);
    } else if (data->table) {
        char idx[24], id[16], flags[16], chunk_sz[24];
        snprintf(idx, sizeof(idx), "%zu", index);
        snprintf(id, sizeof(id), "%u", nmo_object_get_id(obj));
        snprintf(flags, sizeof(flags), "0x%08X", nmo_object_get_flags(obj));

        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        if (chunk) {
            snprintf(chunk_sz, sizeof(chunk_sz), "%zu", nmo_chunk_get_data_size(chunk));
        } else {
            snprintf(chunk_sz, sizeof(chunk_sz), "-");
        }

        const char *class_name = nmo_cli_class_name_from_id(c->ctx, nmo_object_get_class_id(obj));
        const char *name = nmo_object_get_name(obj);

        const char *cells[] = {
            idx, id, flags,
            class_name ? class_name : "-",
            (name && name[0]) ? name : "-",
            chunk_sz
        };
        (void)nmo_cli_table_add_row(data->table, cells, 6);
    }

    return 0;
}

typedef struct nmo_debug_export_data {
    yyjson_mut_doc *doc;
    yyjson_mut_val *objects;
    bool include_data;
    size_t max_bytes;
} nmo_debug_export_data_t;

static int debug_export_object(size_t index, nmo_object_t *obj,
                               const nmo_cmd_ctx_t *c, void *user)
{
    nmo_debug_export_data_t *data = (nmo_debug_export_data_t *)user;
    if (!data || !data->doc || !data->objects || !obj) {
        return 0;
    }

    yyjson_mut_val *o = yyjson_mut_obj(data->doc);
    yyjson_mut_obj_add_uint(data->doc, o, "index", (uint64_t)index);
    yyjson_mut_obj_add_uint(data->doc, o, "id", nmo_object_get_id(obj));
    yyjson_mut_obj_add_uint(data->doc, o, "class_id", nmo_object_get_class_id(obj));
    yyjson_mut_obj_add_uint(data->doc, o, "flags", nmo_object_get_flags(obj));

    const char *name = nmo_object_get_name(obj);
    if (name && name[0]) {
        nmo_cli_json_add_str_safe(data->doc, o, "name", name);
    }

    const char *class_name = nmo_cli_class_name_from_id(c->ctx, nmo_object_get_class_id(obj));
    if (class_name) {
        yyjson_mut_obj_add_str(data->doc, o, "class_name", class_name);
    }

    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    if (chunk) {
        yyjson_mut_val *cv = yyjson_mut_obj(data->doc);
        yyjson_mut_obj_add_uint(data->doc, cv, "class_id", chunk->class_id);
        yyjson_mut_obj_add_uint(data->doc, cv, "data_size",
                                (uint64_t)nmo_chunk_get_data_size(chunk));
        yyjson_mut_obj_add_uint(data->doc, cv, "compressed_size",
                                (uint64_t)chunk->compressed_size);
        yyjson_mut_obj_add_uint(data->doc, cv, "uncompressed_size",
                                (uint64_t)chunk->uncompressed_size);
        yyjson_mut_obj_add_uint(data->doc, cv, "options", (uint64_t)chunk->chunk_options);
        yyjson_mut_obj_add_uint(data->doc, cv, "id_count",
                                (uint64_t)nmo_chunk_get_id_count(chunk));
        yyjson_mut_obj_add_uint(data->doc, cv, "subchunk_count",
                                (uint64_t)nmo_chunk_get_sub_chunk_count(chunk));

        if (data->include_data) {
            size_t data_size = 0;
            const uint8_t *chunk_data = (const uint8_t *)nmo_chunk_get_data(chunk, &data_size);
            (void)nmo_cli_json_add_data_hex(data->doc, cv, chunk_data,
                                            data_size, data->max_bytes, false);
        }

        yyjson_mut_obj_add_val(data->doc, o, "chunk", cv);
    }

    yyjson_mut_arr_add_val(data->objects, o);
    return 0;
}

/* ============================================================================
 * debug load-phases
 * ============================================================================ */

static int debug_load_phases_run_in_ctx(nmo_cmd_ctx_t *c,
                                        nmo_load_profile_t profile,
                                        const nmo_load_perf_stats_t *phase_stats,
                                        bool close_ctx)
{
    nmo_load_perf_stats_t empty_phase_stats;
    if (!phase_stats) {
        nmo_load_perf_stats_reset(&empty_phase_stats);
        phase_stats = &empty_phase_stats;
    }

    /* Get finish loading stats */
    nmo_runtime_load_stats_t stats;
    bool has_stats = (nmo_document_get_runtime_load_stats(c->document, &stats) == NMO_OK);

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file", c->file_path);
        yyjson_mut_obj_add_str(doc, data, "profile", debug_load_profile_name(profile));
        yyjson_mut_obj_add_bool(doc, data, "stats_available", has_stats);

        if (has_stats) {
            yyjson_mut_obj_add_uint(doc, data, "total_objects", (uint64_t)stats.total_objects);

            yyjson_mut_val *refs = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, refs, "total", stats.references.total);
            yyjson_mut_obj_add_uint(doc, refs, "resolved", stats.references.resolved);
            yyjson_mut_obj_add_uint(doc, refs, "unresolved", stats.references.unresolved);
            yyjson_mut_obj_add_uint(doc, refs, "ambiguous", stats.references.ambiguous);
            yyjson_mut_obj_add_val(doc, data, "references", refs);

            yyjson_mut_val *idx = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, idx, "class_entries", (uint64_t)stats.indexes.class_entries);
            yyjson_mut_obj_add_uint(doc, idx, "name_entries", (uint64_t)stats.indexes.name_entries);
            yyjson_mut_obj_add_uint(doc, idx, "guid_entries", (uint64_t)stats.indexes.guid_entries);
            yyjson_mut_obj_add_uint(doc, idx, "memory_usage", (uint64_t)stats.indexes.memory_usage);
            yyjson_mut_obj_add_val(doc, data, "indexes", idx);

            yyjson_mut_obj_add_uint(doc, data, "manager_errors", stats.manager_errors);
        }
        debug_add_load_phase_stats_json(doc, data, phase_stats);

        nmo_cmd_ctx_json_end(c, doc, data, "debug.load-phases");
    } else {
        nmo_cli_print_heading(c->out, "Load Phases", c->colorize);
        nmo_cli_print_kv(c->out, "File", c->file_path, 16, c->colorize);
        nmo_cli_print_kv(c->out, "Profile", debug_load_profile_name(profile), 16, c->colorize);

        if (!has_stats) {
            fprintf(c->out, "\nLoad statistics unavailable\n");
        } else {
            char buf[64];
            fprintf(c->out, "\n");

            snprintf(buf, sizeof(buf), "%zu", stats.total_objects);
            nmo_cli_print_kv(c->out, "Total Objects", buf, 16, c->colorize);

            fprintf(c->out, "\nReferences:\n");
            snprintf(buf, sizeof(buf), "%u", stats.references.total);
            nmo_cli_print_kv(c->out, "  Total", buf, 14, c->colorize);
            snprintf(buf, sizeof(buf), "%u", stats.references.resolved);
            nmo_cli_print_kv(c->out, "  Resolved", buf, 14, c->colorize);
            snprintf(buf, sizeof(buf), "%u", stats.references.unresolved);
            nmo_cli_print_kv(c->out, "  Unresolved", buf, 14, c->colorize);
            snprintf(buf, sizeof(buf), "%u", stats.references.ambiguous);
            nmo_cli_print_kv(c->out, "  Ambiguous", buf, 14, c->colorize);

            fprintf(c->out, "\nIndexes:\n");
            snprintf(buf, sizeof(buf), "%zu", stats.indexes.class_entries);
            nmo_cli_print_kv(c->out, "  Classes", buf, 14, c->colorize);
            snprintf(buf, sizeof(buf), "%zu", stats.indexes.name_entries);
            nmo_cli_print_kv(c->out, "  Names", buf, 14, c->colorize);
            snprintf(buf, sizeof(buf), "%zu", stats.indexes.guid_entries);
            nmo_cli_print_kv(c->out, "  GUIDs", buf, 14, c->colorize);
            snprintf(buf, sizeof(buf), "%zu bytes", stats.indexes.memory_usage);
            nmo_cli_print_kv(c->out, "  Memory", buf, 14, c->colorize);

            fprintf(c->out, "\n");
            snprintf(buf, sizeof(buf), "%u", stats.manager_errors);
            nmo_cli_print_kv(c->out, "Manager Errors", buf, 16, c->colorize);
        }
        debug_print_load_phase_stats(c->out, phase_stats);
    }

    return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_SUCCESS)
                     : NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_debug_load_phases(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (rc) return rc;

    nmo_load_profile_t profile;
    rc = debug_parse_load_profile(argc, argv, &profile);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&c, rc);
    }

    c.file_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!c.file_path) {
        fprintf(stderr, "Error: No file specified\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_load_perf_stats_t phase_stats;
    nmo_load_perf_stats_reset(&phase_stats);

    nmo_load_options_t load_opts = nmo_load_options_default();
    load_opts.profile = profile;
    load_opts.collect_perf_stats = true;
    load_opts.perf_stats = &phase_stats;

    char errbuf[256];
    if (!nmo_tool_open_document_opts(c.file_path, &load_opts,
                                     &c.ctx, &c.document, &c.workspace,
                                     errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }
    c.owns_document = true;
    c.registry = nmo_context_get_type_registry(c.ctx);
    return debug_load_phases_run_in_ctx(&c, profile, &phase_stats, true);
}

static int nmo_cmd_debug_load_phases_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    nmo_load_profile_t profile;
    int rc = debug_parse_load_profile(argc, argv, &profile);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    return debug_load_phases_run_in_ctx(ctx, profile, NULL, false);
}

/* ============================================================================
 * debug chunks - Iterate objects to list chunk debug info
 * ============================================================================ */

static int debug_chunks_run_in_ctx(nmo_cmd_ctx_t *c, bool close_ctx)
{
    int rc = NMO_CLI_EXIT_SUCCESS;
    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_val *chunks = yyjson_mut_arr(doc);
        nmo_debug_chunks_data_t chunks_data = {
            .doc = doc,
            .chunks = chunks,
        };
        nmo_core_iter_result_t result = {0};
        rc = nmo_core_object_query_run(c, NULL, debug_chunks_object,
                                       &chunks_data, &result);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            yyjson_mut_doc_free(doc);
            fprintf(stderr, "Error: Failed to query objects\n");
            return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_obj_add_uint(doc, data, "object_count", (uint64_t)result.matched);
        yyjson_mut_obj_add_uint(doc, data, "chunk_count",
                                (uint64_t)chunks_data.chunk_count);
        yyjson_mut_obj_add_val(doc, data, "chunks", chunks);

        nmo_cmd_ctx_json_end(c, doc, data, "debug.chunks");
    } else {
        nmo_cli_print_heading(c->out, "Chunk Debug Info", c->colorize);

        static const nmo_cli_table_col_t columns[] = {
            {"ObjectID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"ClassID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 15, 25},
            {"DataSize", NMO_CLI_ALIGN_RIGHT, 8, 0},
            {"PackSize", NMO_CLI_ALIGN_RIGHT, 8, 0},
            {"Options", NMO_CLI_ALIGN_LEFT, 8, 32},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        nmo_debug_chunks_data_t chunks_data = {
            .table = &table,
        };
        nmo_core_iter_result_t result = {0};
        rc = nmo_core_object_query_run(c, NULL, debug_chunks_object,
                                       &chunks_data, &result);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            nmo_cli_table_free(&table);
            fprintf(stderr, "Error: Failed to query objects\n");
            return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        fprintf(c->out, "Chunks: %zu (from %zu objects)\n\n",
                chunks_data.chunk_count, result.matched);
        nmo_cli_table_print(&table, c->out, c->colorize);
        nmo_cli_table_free(&table);
    }

    return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_SUCCESS)
                     : NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_debug_chunks(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;
    return debug_chunks_run_in_ctx(&c, true);
}

static int nmo_cmd_debug_chunks_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!ctx) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    return debug_chunks_run_in_ctx(ctx, false);
}

/* ============================================================================
 * debug objects
 * ============================================================================ */

static int debug_objects_run_in_ctx(nmo_cmd_ctx_t *c, bool close_ctx)
{
    int rc = NMO_CLI_EXIT_SUCCESS;
    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_val *objs = yyjson_mut_arr(doc);
        nmo_debug_objects_data_t objects_data = {
            .doc = doc,
            .objects = objs,
        };
        nmo_core_iter_result_t result = {0};
        rc = nmo_core_object_query_run(c, NULL, debug_objects_object,
                                       &objects_data, &result);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            yyjson_mut_doc_free(doc);
            fprintf(stderr, "Error: Failed to query objects\n");
            return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_obj_add_uint(doc, data, "object_count", (uint64_t)result.matched);
        yyjson_mut_obj_add_val(doc, data, "objects", objs);

        nmo_cmd_ctx_json_end(c, doc, data, "debug.objects");
    } else {
        nmo_cli_print_heading(c->out, "Object Debug Info", c->colorize);

        static const nmo_cli_table_col_t columns[] = {
            {"Idx", NMO_CLI_ALIGN_RIGHT, 4, 0},
            {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"Flags", NMO_CLI_ALIGN_RIGHT, 10, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 15, 25},
            {"Name", NMO_CLI_ALIGN_LEFT, 20, 40},
            {"Chunk", NMO_CLI_ALIGN_RIGHT, 8, 0},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        nmo_debug_objects_data_t objects_data = {
            .table = &table,
        };
        nmo_core_iter_result_t result = {0};
        rc = nmo_core_object_query_run(c, NULL, debug_objects_object,
                                       &objects_data, &result);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            nmo_cli_table_free(&table);
            fprintf(stderr, "Error: Failed to query objects\n");
            return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        fprintf(c->out, "Objects: %zu\n\n", result.matched);
        nmo_cli_table_print(&table, c->out, c->colorize);
        nmo_cli_table_free(&table);
    }

    return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_SUCCESS)
                     : NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_debug_objects(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;
    return debug_objects_run_in_ctx(&c, true);
}

static int nmo_cmd_debug_objects_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!ctx) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    return debug_objects_run_in_ctx(ctx, false);
}

/* ============================================================================
 * debug export
 * ============================================================================ */

static int debug_export_parse(int argc, char **argv,
                              bool *include_data,
                              size_t *max_bytes)
{
    static const nmo_opt_def_t opts[] = {
        {"--data",      "--include-data", NMO_OPT_FLAG, "Include chunk data"},
        {"--max-bytes", NULL,             NMO_OPT_UINT, "Max bytes for data dump (default: 4096)"},
    };
    nmo_opt_val_t vals[2];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 2, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    *include_data = vals[0].val.flag;
    *max_bytes = vals[1].present ? (size_t)vals[1].val.u : 4096;
    return NMO_CLI_EXIT_SUCCESS;
}

static int debug_export_run_in_ctx(nmo_cmd_ctx_t *c,
                                   bool include_data,
                                   size_t max_bytes,
                                   bool close_ctx)
{
    yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
    yyjson_mut_val *data = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, data, "file", c->file_path);
    yyjson_mut_obj_add_bool(doc, data, "include_data", include_data);
    yyjson_mut_obj_add_uint(doc, data, "max_bytes", (uint64_t)max_bytes);

    yyjson_mut_val *objs = yyjson_mut_arr(doc);
    nmo_debug_export_data_t export_data = {
        .doc = doc,
        .objects = objs,
        .include_data = include_data,
        .max_bytes = max_bytes,
    };
    nmo_core_iter_result_t result = {0};
    int rc = nmo_core_object_query_run(c, NULL, debug_export_object,
                                       &export_data, &result);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        yyjson_mut_doc_free(doc);
        fprintf(stderr, "Error: Failed to query objects\n");
        return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    yyjson_mut_obj_add_uint(doc, data, "object_count", (uint64_t)result.matched);
    yyjson_mut_obj_add_val(doc, data, "objects", objs);
    nmo_cmd_ctx_json_end(c, doc, data, "debug.export");

    if (!c->is_json && c->global && c->global->output_path) {
        fprintf(stdout, "Exported %zu objects to %s\n", result.matched, c->global->output_path);
    }

    return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_SUCCESS)
                     : NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_debug_export(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    bool include_data = false;
    size_t max_bytes = 4096;
    int rc = debug_export_parse(argc, argv, &include_data, &max_bytes);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;
    return debug_export_run_in_ctx(&c, include_data, max_bytes, true);
}

int nmo_cmd_debug_probe(int argc,
                        char **argv,
                        const nmo_cli_global_opts_t *global)
{
    nmo_debug_probe_args_t args;
    const char *input_path = NULL;
    const char *output_path = NULL;
    bool dry_run = false;
    int rc = debug_probe_parse(
        argc, argv, &args, &input_path, &output_path, &dry_run);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    const nmo_cli_write_spec_t spec = {
        .command_name = "debug.probe",
        .output_required_unless_dry_run = true,
        .should_save = NULL,
    };
    return nmo_cli_run_write_command(
        input_path,
        output_path,
        dry_run,
        global,
        &spec,
        debug_probe_mutate,
        debug_probe_report,
        &args);
}

static int nmo_cmd_debug_export_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    bool include_data = false;
    size_t max_bytes = 4096;
    int rc = debug_export_parse(argc, argv, &include_data, &max_bytes);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    return debug_export_run_in_ctx(ctx, include_data, max_bytes, false);
}

int nmo_cmd_debug_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: debug load-phases|chunks|objects|export|probe ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "load-phases") == 0 || strcmp(argv[0], "lp") == 0) {
        return nmo_cmd_debug_load_phases_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "chunks") == 0 || strcmp(argv[0], "ch") == 0) {
        return nmo_cmd_debug_chunks_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "objects") == 0 || strcmp(argv[0], "obj") == 0) {
        return nmo_cmd_debug_objects_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "export") == 0 || strcmp(argv[0], "x") == 0) {
        return nmo_cmd_debug_export_in_session(ctx, argc, argv);
    }

    fprintf(stderr, "Unsupported debug read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
}

