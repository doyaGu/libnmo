#if !defined(_WIN32) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif

/**
 * @file nmo_cmd_validate.c
 * @brief CLI validate command group implementation
 *
 * Phase 4 - Reference graph and validation rules
 */

#include "nmo_cmd_validate.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_write.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_json.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "chunk/nmo_chunk_inspect.h"
#include "document/nmo_document_save.h"
#include "runtime/nmo_context.h"
#include "core/nmo_arena.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_ref_graph.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/nmo_object_guids.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_query.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int nmo_cmd_validate_all_in_session(nmo_cmd_ctx_t *cmd, int argc, char **argv);
static int nmo_cmd_validate_structure_in_session(nmo_cmd_ctx_t *c, int argc, char **argv);
static int nmo_cmd_validate_references_in_session(nmo_cmd_ctx_t *c, int argc, char **argv);
static int nmo_cmd_validate_resources_in_session(nmo_cmd_ctx_t *c, int argc, char **argv);
static int nmo_cmd_validate_orphans_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

int nmo_cmd_validate_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: validate all|structure|references|resources|orphans ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "all") == 0 || strcmp(argv[0], "a") == 0) {
        return nmo_cmd_validate_all_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "structure") == 0 || strcmp(argv[0], "st") == 0) {
        return nmo_cmd_validate_structure_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "references") == 0 || strcmp(argv[0], "ref") == 0) {
        return nmo_cmd_validate_references_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "resources") == 0 || strcmp(argv[0], "res") == 0) {
        return nmo_cmd_validate_resources_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "orphans") == 0 || strcmp(argv[0], "orp") == 0) {
        return nmo_cmd_validate_orphans_in_session(ctx, argc, argv);
    }

    fprintf(stderr, "Unsupported validate read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
}

/**
 * Check if --fix or --suggest-fixes flag is present.
 */
static bool parse_fix_flag(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--fix") == 0 || strcmp(argv[i], "--suggest-fixes") == 0) {
            return true;
        }
    }
    return false;
}

typedef struct validate_all_data {
    const nmo_cli_global_opts_t *global;
    FILE *out;
    yyjson_mut_doc *doc;
    size_t error_count;
    size_t warning_count;
} validate_all_data_t;

static void print_load_issue(FILE *out, const nmo_load_issue_t *issue)
{
    fprintf(out,
            "Parse error: object=%u file=%u class=%" PRIu32 " schema=%s section=0x%08X "
            "dword=%zu status=%d: %s\n",
            issue->object_id, issue->file_id, (uint32_t)issue->class_id,
            issue->schema_name[0] ? issue->schema_name : "unknown",
            issue->section_id, issue->dword_offset, issue->status,
            issue->message[0] ? issue->message : nmo_error_string(issue->status));
}

static const char *parse_string_option(int argc, char **argv,
                                       const char *long_name, const char *short_name)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], long_name) == 0 || strcmp(argv[i], short_name) == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

static bool parse_flag(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], name) == 0) return true;
    }
    return false;
}

static bool paths_refer_same_file(const char *a, const char *b)
{
    if (!a || !b) return false;
    char full_a[4096];
    char full_b[4096];
#ifdef _WIN32
    if (!_fullpath(full_a, a, sizeof(full_a)) ||
        !_fullpath(full_b, b, sizeof(full_b))) {
        return nmo_tool_streq_ci(a, b);
    }
    return nmo_tool_streq_ci(full_a, full_b);
#else
    if (!realpath(a, full_a) || !realpath(b, full_b)) {
        return strcmp(a, b) == 0;
    }
    return strcmp(full_a, full_b) == 0;
#endif
}

static int validate_all_object(size_t index, nmo_object_t *obj,
                               const nmo_cmd_ctx_t *c, void *user)
{
    (void)index;
    (void)c;

    validate_all_data_t *data = (validate_all_data_t *)user;
    if (!data || !obj) {
        return 0;
    }

    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    if (!chunk) {
        data->warning_count++;
        if (!data->doc && data->global && data->global->verbosity > 0) {
            fprintf(data->out, "Warning: Object %u has no chunk\n",
                    nmo_object_get_id(obj));
        }
        return 0;
    }

    nmo_chunk_validation_t result;
    nmo_status_t rc = nmo_inspector_validate_chunk(chunk, &result);
    if (rc != NMO_OK || !result.is_valid) {
        data->error_count++;
        if (!data->doc) {
            fprintf(data->out, "Error: Object %u chunk validation failed: %s\n",
                    nmo_object_get_id(obj),
                    result.error_message[0] ? result.error_message : "unknown");
        }
    }

    return 0;
}

typedef struct validate_structure_data {
    const nmo_cli_global_opts_t *global;
    bool suggest_fixes;
    yyjson_mut_doc *doc;
    yyjson_mut_val *issues;
    size_t error_count;
    size_t warning_count;
    size_t checked_count;
} validate_structure_data_t;

static int validate_structure_object(size_t index, nmo_object_t *obj,
                                     const nmo_cmd_ctx_t *c, void *user)
{
    (void)index;

    validate_structure_data_t *data = (validate_structure_data_t *)user;
    if (!data || !obj) {
        return 0;
    }

    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    nmo_object_id_t obj_id = nmo_object_get_id(obj);

    if (!chunk) {
        data->warning_count++;
        if (c->is_json) {
            yyjson_mut_val *issue = yyjson_mut_obj(data->doc);
            yyjson_mut_obj_add_str(data->doc, issue, "severity", "warning");
            yyjson_mut_obj_add_uint(data->doc, issue, "id", obj_id);
            yyjson_mut_obj_add_uint(data->doc, issue, "class_id",
                                    nmo_object_get_class_id(obj));
            const char *class_name = nmo_cli_class_name_from_id(
                c->ctx, nmo_object_get_class_id(obj));
            if (class_name) {
                yyjson_mut_obj_add_str(data->doc, issue, "class_name", class_name);
            }
            yyjson_mut_obj_add_str(data->doc, issue, "message", "missing chunk");
            if (data->suggest_fixes) {
                yyjson_mut_obj_add_str(data->doc, issue, "fix",
                                       "re-save file to regenerate chunks");
            }
            yyjson_mut_arr_add_val(data->issues, issue);
        } else if (data->global && data->global->verbosity > 0) {
            fprintf(c->out, "Warning: Object %u has no chunk\n", obj_id);
            if (data->suggest_fixes) {
                fprintf(c->out, "  Fix: Re-save file to regenerate chunks\n");
            }
        }
        return 0;
    }

    data->checked_count++;

    if (!c->is_json && data->global && data->global->verbosity >= 2) {
        size_t ds = 0;
        (void)nmo_chunk_get_data(chunk, &ds);
        fprintf(c->out, "  Object %u: chunk %zu bytes\n", obj_id, ds);
    }

    nmo_chunk_validation_t result;
    nmo_status_t vrc = nmo_inspector_validate_chunk(chunk, &result);
    if (vrc != NMO_OK || !result.is_valid) {
        data->error_count++;
        if (c->is_json) {
            yyjson_mut_val *issue = yyjson_mut_obj(data->doc);
            yyjson_mut_obj_add_str(data->doc, issue, "severity", "error");
            yyjson_mut_obj_add_uint(data->doc, issue, "id", obj_id);
            yyjson_mut_obj_add_uint(data->doc, issue, "class_id",
                                    nmo_object_get_class_id(obj));
            const char *class_name = nmo_cli_class_name_from_id(
                c->ctx, nmo_object_get_class_id(obj));
            if (class_name) {
                yyjson_mut_obj_add_str(data->doc, issue, "class_name", class_name);
            }
            yyjson_mut_obj_add_str(data->doc, issue, "message",
                                   result.error_message[0] ? result.error_message : "validation failed");
            if (data->suggest_fixes) {
                yyjson_mut_obj_add_str(data->doc, issue, "fix",
                                       "re-save with nmo convert to regenerate chunk data");
            }
            yyjson_mut_arr_add_val(data->issues, issue);
        } else {
            fprintf(c->out, "Error: Object %u chunk invalid: %s\n",
                    obj_id,
                    result.error_message[0] ? result.error_message : "validation failed");
            if (data->suggest_fixes) {
                fprintf(c->out, "  Fix: Re-save with 'nmo convert' to regenerate chunk data\n");
            }
        }
    }

    return 0;
}

/* ============================================================================
 * validate all (single-file core + batch support)
 * ============================================================================ */

/**
 * Core validation logic for a single file.
 * When doc/data are non-NULL (JSON batch mode), populates data.
 * When NULL (text mode), prints directly to stdout.
 */
static int validate_all_run(nmo_cmd_ctx_t *cmd,
                            const nmo_cli_global_opts_t *global,
                            yyjson_mut_doc *doc,
                            yyjson_mut_val *data);

static int validate_all_single(const char *file_path,
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

    nmo_load_diagnostics_t diagnostics;
    nmo_load_diagnostics_init(&diagnostics);
    nmo_load_options_t options = nmo_load_options_default();
    options.diagnostics = &diagnostics;
    if (!nmo_tool_open_document_opts(file_path, &options, &ctx, &document, &workspace,
                                     errbuf, sizeof(errbuf))) {
        nmo_load_diagnostics_destroy(&diagnostics);
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    FILE *out = (text_ctx && text_ctx->out) ? text_ctx->out : stdout;
    bool colorize = (text_ctx != NULL) ? text_ctx->colorize : nmo_cli_should_colorize(global, out);

    nmo_cmd_ctx_t cmd;
    nmo_cmd_ctx_init_from_repl_document(&cmd, ctx, document, workspace, colorize);
    cmd.out = out;
    cmd.load_diagnostics = &diagnostics;

    int rc = validate_all_run(&cmd, global, doc, data);
    nmo_tool_close_document(ctx, document, workspace);
    nmo_load_diagnostics_destroy(&diagnostics);
    return rc;
}

static int validate_all_run(nmo_cmd_ctx_t *cmd,
                            const nmo_cli_global_opts_t *global,
                            yyjson_mut_doc *doc,
                            yyjson_mut_val *data)
{
    validate_all_data_t validate_data = {
        .global = global,
        .out = cmd->out,
        .doc = doc,
    };
    if (cmd->load_diagnostics) {
        validate_data.error_count += cmd->load_diagnostics->count;
        if (!doc) {
            for (size_t i = 0; i < cmd->load_diagnostics->count; ++i) {
                print_load_issue(cmd->out, &cmd->load_diagnostics->issues[i]);
            }
        }
    }
    nmo_core_iter_result_t query_result = {0};
    if (nmo_core_object_query_run(cmd, NULL,
                                  validate_all_object, &validate_data,
                                  &query_result) != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Error: Failed to query objects\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    int exit_code = NMO_CLI_EXIT_SUCCESS;
    if (validate_data.error_count > 0) {
        exit_code = global->strict_mode ? NMO_CLI_EXIT_STRICT_FAILURE : NMO_CLI_EXIT_SUCCESS;
    }
    if (validate_data.warning_count > 0 && global->fail_on_warning) {
        exit_code = NMO_CLI_EXIT_WARNING;
    }

    if (doc && data) {
        /* JSON batch mode: populate data object */
        yyjson_mut_obj_add_bool(doc, data, "valid", validate_data.error_count == 0);
        yyjson_mut_obj_add_uint(doc, data, "error_count",
                                (uint64_t)validate_data.error_count);
        yyjson_mut_obj_add_uint(doc, data, "warning_count",
                                (uint64_t)validate_data.warning_count);
        yyjson_mut_obj_add_uint(doc, data, "object_count",
                                (uint64_t)query_result.matched);
    } else {
        /* Text mode: output summary */
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", query_result.matched);
        nmo_cli_print_kv(cmd->out, "Objects", buf, 12, cmd->colorize);
        snprintf(buf, sizeof(buf), "%zu", validate_data.error_count);
        nmo_cli_print_kv(cmd->out, "Errors", buf, 12, cmd->colorize);
        snprintf(buf, sizeof(buf), "%zu", validate_data.warning_count);
        nmo_cli_print_kv(cmd->out, "Warnings", buf, 12, cmd->colorize);
        fprintf(cmd->out, "Result: %s\n",
                validate_data.error_count == 0 ? "VALID" : "INVALID");
    }

    return exit_code;
}

static int nmo_cmd_validate_all_in_session(nmo_cmd_ctx_t *cmd, int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const nmo_cli_global_opts_t *global = cmd->global;
    if (cmd->is_json) {
        yyjson_mut_doc *doc = NULL;
        yyjson_mut_val *data = NULL;
        if (!nmo_cli_json_create_data_doc(&doc, &data)) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        int rc = validate_all_run(cmd, global, doc, data);

        yyjson_mut_obj_add_str(doc, data, "file", cmd->file_path);
        nmo_cli_json_write_enveloped_and_free(
            doc, data, "validate.all", cmd->file_path, cmd->out,
            global && global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        return rc;
    }

    nmo_cli_print_heading(cmd->out, "Validation Results", cmd->colorize);
    nmo_cli_print_kv(cmd->out, "File", cmd->file_path, 12, cmd->colorize);
    fprintf(cmd->out, "\n");

    return validate_all_run(cmd, global, NULL, NULL);
}

int nmo_cmd_validate_all(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Batch mode: process multiple files */
    if (global->batch_mode) {
        const char *paths[64];
        size_t count = nmo_tool_find_file_args(argc, argv, paths, 64);
        if (count == 0) {
            fprintf(stderr, "Error: No files specified\n");
            fprintf(stderr, "Usage: nmo --batch validate all <file1> <file2> ...\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        return nmo_tool_batch_run(paths, count, global, "validate.all",
                                   validate_all_single, NULL);
    }

    /* Single file mode - validate_all_single opens its own session */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (rc) return rc;

    const char *file_path = nmo_tool_find_file_arg(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo validate all <file>\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if (c.is_json) {
        /* Single-file JSON: use the batch handler to populate, then wrap */
        yyjson_mut_doc *doc = NULL;
        yyjson_mut_val *data = NULL;
        if (!nmo_cli_json_create_data_doc(&doc, &data)) {
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        rc = validate_all_single(file_path, global, NULL, doc, data);

        yyjson_mut_obj_add_str(doc, data, "file", file_path);
        nmo_cli_json_write_enveloped_and_free(doc, data, "validate.all", file_path,
                                              c.out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        return nmo_cmd_ctx_done(&c, rc);
    }

    /* Single-file text mode */
    nmo_tool_text_output_ctx_t text_ctx = {
        .out = c.out,
        .colorize = c.colorize,
        .user_data = NULL
    };
    nmo_cli_print_heading(c.out, "Validation Results", c.colorize);
    nmo_cli_print_kv(c.out, "File", file_path, 12, c.colorize);
    fprintf(c.out, "\n");

    rc = validate_all_single(file_path, global, &text_ctx, NULL, NULL);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * validate structure
 * ============================================================================ */

static int nmo_cmd_validate_structure_in_session(nmo_cmd_ctx_t *c, int argc, char **argv) {
    bool suggest_fixes = parse_fix_flag(argc, argv);

    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *data = NULL;
    yyjson_mut_val *issues = NULL;

    if (c->is_json) {
        doc = nmo_cmd_ctx_json_begin(c);
        data = yyjson_mut_obj(doc);
        issues = yyjson_mut_arr(doc);
    } else {
        nmo_cli_print_heading(c->out, "Structure Validation", c->colorize);
        nmo_cli_print_kv(c->out, "File", c->file_path, 14, c->colorize);
        fprintf(c->out, "\n");
    }

    validate_structure_data_t structure_data = {
        .global = c->global,
        .suggest_fixes = suggest_fixes,
        .doc = doc,
        .issues = issues,
    };
    if (c->load_diagnostics) {
        structure_data.error_count += c->load_diagnostics->count;
        for (size_t i = 0; i < c->load_diagnostics->count; ++i) {
            const nmo_load_issue_t *issue = &c->load_diagnostics->issues[i];
            if (c->is_json) {
                yyjson_mut_val *entry = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_str(doc, entry, "severity", "error");
                yyjson_mut_obj_add_uint(doc, entry, "id", issue->object_id);
                yyjson_mut_obj_add_uint(doc, entry, "file_id", issue->file_id);
                yyjson_mut_obj_add_sint(doc, entry, "class_id", issue->class_id);
                yyjson_mut_obj_add_strcpy(doc, entry, "schema", issue->schema_name);
                yyjson_mut_obj_add_uint(doc, entry, "section", issue->section_id);
                yyjson_mut_obj_add_uint(doc, entry, "dword_offset", issue->dword_offset);
                yyjson_mut_obj_add_sint(doc, entry, "status", issue->status);
                yyjson_mut_obj_add_strcpy(doc, entry, "message", issue->message);
                yyjson_mut_arr_add_val(issues, entry);
            } else {
                print_load_issue(c->out, issue);
            }
        }
    }
    nmo_core_iter_result_t query_result = {0};
    int rc = nmo_core_object_query_run(c, NULL, validate_structure_object,
                                       &structure_data, &query_result);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        if (doc) {
            yyjson_mut_doc_free(doc);
        }
        fprintf(stderr, "Error: Failed to query objects\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    int exit_code = NMO_CLI_EXIT_SUCCESS;
    if (structure_data.error_count > 0 && c->global && c->global->strict_mode) {
        exit_code = NMO_CLI_EXIT_STRICT_FAILURE;
    }
    if (structure_data.warning_count > 0 && c->global && c->global->fail_on_warning) {
        exit_code = NMO_CLI_EXIT_WARNING;
    }

    if (c->is_json) {
        yyjson_mut_obj_add_str(doc, data, "file", c->file_path);
        yyjson_mut_obj_add_bool(doc, data, "valid", structure_data.error_count == 0);
        yyjson_mut_obj_add_uint(doc, data, "object_count",
                                (uint64_t)query_result.matched);
        yyjson_mut_obj_add_uint(doc, data, "checked_chunks",
                                (uint64_t)structure_data.checked_count);
        yyjson_mut_obj_add_uint(doc, data, "error_count",
                                (uint64_t)structure_data.error_count);
        yyjson_mut_obj_add_uint(doc, data, "warning_count",
                                (uint64_t)structure_data.warning_count);
        yyjson_mut_obj_add_val(doc, data, "issues", issues);

        nmo_cmd_ctx_json_end(c, doc, data, "validate.structure");
    } else {
        fprintf(c->out, "\nSummary:\n");
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", query_result.matched);
        nmo_cli_print_kv(c->out, "Objects", buf, 14, c->colorize);
        snprintf(buf, sizeof(buf), "%zu", structure_data.checked_count);
        nmo_cli_print_kv(c->out, "Chunks", buf, 14, c->colorize);
        snprintf(buf, sizeof(buf), "%zu", structure_data.error_count);
        nmo_cli_print_kv(c->out, "Errors", buf, 14, c->colorize);
        snprintf(buf, sizeof(buf), "%zu", structure_data.warning_count);
        nmo_cli_print_kv(c->out, "Warnings", buf, 14, c->colorize);
    }

    return exit_code;
}

int nmo_cmd_validate_structure(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    nmo_load_diagnostics_t diagnostics;
    nmo_load_diagnostics_init(&diagnostics);
    nmo_load_options_t options = nmo_load_options_default();
    options.diagnostics = &diagnostics;
    int rc = nmo_cmd_ctx_init_with_load_options(&c, argc, argv, global, &options);
    if (rc) {
        nmo_load_diagnostics_destroy(&diagnostics);
        return rc;
    }
    rc = nmo_cmd_validate_structure_in_session(&c, argc, argv);
    rc = nmo_cmd_ctx_done(&c, rc);
    nmo_load_diagnostics_destroy(&diagnostics);
    return rc;
}

/* ============================================================================
 * validate references
 * ============================================================================ */

static nmo_object_id_t validate_display_target_id(
    const nmo_object_repository_t *repo,
    nmo_object_id_t target_id,
    bool *out_was_unresolved)
{
    nmo_object_id_t raw_id = NMO_OBJECT_ID_NONE;
    bool unresolved = nmo_object_repository_get_unresolved_ref_raw(
        repo, target_id, &raw_id);
    if (out_was_unresolved) *out_was_unresolved = unresolved;
    return unresolved ? raw_id : target_id;
}

typedef bool (*validate_typed_ref_issue_fn)(
    void *user_data,
    const nmo_object_t *source,
    const nmo_ref_t *ref,
    const char *field,
    size_t index);

static bool validate_ref_has_issue(const nmo_ref_t *ref)
{
    return ref != NULL &&
           ref->state != NMO_REF_NONE &&
           ref->state != NMO_REF_RESOLVED;
}

static const char *validate_ref_state_name(nmo_ref_state_t state)
{
    switch (state) {
    case NMO_REF_UNRESOLVED: return "unresolved";
    case NMO_REF_AMBIGUOUS: return "ambiguous";
    case NMO_REF_CLASS_MISMATCH: return "class_mismatch";
    case NMO_REF_NONE: return "none";
    case NMO_REF_RESOLVED: return "resolved";
    default: return "unknown";
    }
}

typedef struct validate_ref_walk_ctx {
    const nmo_type_registry_t *types;
    const nmo_object_t *source;
    validate_typed_ref_issue_fn visitor;
    void *user_data;
    size_t *issue_count;
    bool keep_going;
} validate_ref_walk_ctx_t;

typedef struct validate_repeated_view {
    const void *data;
    size_t count;
    size_t element_size;
} validate_repeated_view_t;

static bool validate_get_repeated_view(
    const nmo_type_descriptor_t *owner_type,
    const void *owner_instance,
    const nmo_type_field_t *field,
    const nmo_type_registry_t *types,
    validate_repeated_view_t *out)
{
    if (owner_type == NULL || owner_instance == NULL || field == NULL ||
        out == NULL || !nmo_field_is_array(field)) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    const void *field_ptr = nmo_field_get_ptr_const(owner_instance, field);
    if (field_ptr == NULL) return false;

    if (field->size == sizeof(nmo_array_t)) {
        const nmo_array_t *array = (const nmo_array_t *)field_ptr;
        out->data = array->data;
        out->count = array->count;
        out->element_size = array->element_size;
    } else if (field->size == sizeof(void *) &&
               field->count_field_name != NULL) {
        uint32_t count = 0;
        if (nmo_field_resolve_count(
                owner_type, field, owner_instance, &count) != NMO_OK) {
            return false;
        }
        const nmo_type_descriptor_t *element_type =
            types != NULL
                ? nmo_type_registry_find_by_guid(types, field->type_guid)
                : NULL;
        out->data = *(const void *const *)field_ptr;
        out->count = count;
        out->element_size = nmo_field_resolve_element_size(
            field, element_type);
    } else {
        return false;
    }

    if (out->count == 0) return true;
    return out->data != NULL && out->element_size != 0 &&
           out->count <= SIZE_MAX / out->element_size;
}

static bool validate_report_typed_ref(
    validate_ref_walk_ctx_t *ctx,
    const nmo_ref_t *ref,
    const char *field,
    size_t index)
{
    if (!validate_ref_has_issue(ref)) return true;
    ++*ctx->issue_count;
    if (ctx->visitor != NULL && !ctx->visitor(
            ctx->user_data, ctx->source, ref, field, index)) {
        ctx->keep_going = false;
        return false;
    }
    return true;
}

static void validate_build_ref_path(
    char *out,
    size_t out_size,
    const char *prefix,
    size_t prefix_index,
    bool prefix_has_index,
    const char *field_name,
    bool materialize_prefix_index)
{
    const bool omit_ref_name = prefix != NULL && prefix[0] != '\0' &&
        field_name != NULL && strcmp(field_name, "ref") == 0;
    if (prefix == NULL || prefix[0] == '\0') {
        snprintf(out, out_size, "%s", field_name != NULL ? field_name : "ref");
    } else if (omit_ref_name) {
        snprintf(out, out_size, "%s", prefix);
    } else if (prefix_has_index && materialize_prefix_index) {
        snprintf(out, out_size, "%s[%zu].%s", prefix, prefix_index,
                 field_name != NULL ? field_name : "ref");
    } else {
        snprintf(out, out_size, "%s.%s", prefix,
                 field_name != NULL ? field_name : "ref");
    }
}

static bool validate_visit_reflected_refs(
    validate_ref_walk_ctx_t *ctx,
    const nmo_type_descriptor_t *type,
    const void *instance,
    const char *prefix,
    size_t prefix_index,
    bool prefix_has_index,
    unsigned depth)
{
    if (ctx == NULL || type == NULL || instance == NULL || depth >= 16) {
        return true;
    }

    const size_t field_count = nmo_type_get_field_count(type);
    for (size_t field_index = 0; field_index < field_count; ++field_index) {
        const nmo_type_field_t *field = nmo_type_get_field_by_index(
            type, field_index);
        if (field == NULL || nmo_field_is_base_embedding(type, field)) {
            continue;
        }
        const void *field_ptr = nmo_field_get_ptr_const(instance, field);
        if (field_ptr == NULL) continue;

        char path[256];
        validate_build_ref_path(
            path, sizeof(path), prefix, prefix_index, prefix_has_index,
            field->name, nmo_field_is_array(field));

        if (nmo_field_is_ref(field)) {
            if (!nmo_field_uses_ref_records(field)) continue;
            if (!nmo_field_is_array(field)) {
                if (field->size == sizeof(nmo_ref_t) &&
                    !validate_report_typed_ref(
                        ctx, (const nmo_ref_t *)field_ptr, path,
                        prefix_has_index ? prefix_index : 0u)) {
                    return false;
                }
                continue;
            }

            validate_repeated_view_t view;
            if (!validate_get_repeated_view(
                    type, instance, field, ctx->types, &view) ||
                view.element_size != sizeof(nmo_ref_t)) {
                continue;
            }
            const nmo_ref_t *refs = (const nmo_ref_t *)view.data;
            for (size_t i = 0; i < view.count; ++i) {
                if (!validate_report_typed_ref(ctx, &refs[i], path, i)) {
                    return false;
                }
            }
            continue;
        }

        const nmo_type_descriptor_t *nested =
            nmo_type_registry_find_by_guid(ctx->types, field->type_guid);
        if (nested == NULL ||
            (nested->category & NMO_TYPE_CATEGORY_STRUCT) == 0 ||
            !nmo_type_has_reflection(nested)) {
            continue;
        }

        if (nmo_field_is_array(field)) {
            validate_repeated_view_t view;
            if (!validate_get_repeated_view(
                    type, instance, field, ctx->types, &view) ||
                view.element_size != nested->size) {
                continue;
            }
            for (size_t i = 0; i < view.count; ++i) {
                const void *element = (const unsigned char *)view.data +
                    i * view.element_size;
                if (!validate_visit_reflected_refs(
                        ctx, nested, element, path, i, true, depth + 1u)) {
                    return false;
                }
            }
            continue;
        }

        const void *nested_instance = field_ptr;
        if ((field->flags & NMO_FIELD_POINTER) != 0) {
            nested_instance = *(const void *const *)field_ptr;
        }
        if (nested_instance != NULL && !validate_visit_reflected_refs(
                ctx, nested, nested_instance, path, prefix_index,
                prefix_has_index, depth + 1u)) {
            return false;
        }
    }
    return true;
}

static bool validate_visit_skin_bone_ref_issues(
    const nmo_type_registry_t *types,
    nmo_object_t *source,
    validate_typed_ref_issue_fn visitor,
    void *user_data,
    size_t *issue_count)
{
    const nmo_3dentity_state_t *entity =
        (const nmo_3dentity_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                types, source, CKPGUID_3DENTITY);
    if (entity == NULL || entity->skin == NULL ||
        entity->skin->bones == NULL) {
        return true;
    }
    for (size_t i = 0; i < entity->skin->bone_count; ++i) {
        const nmo_ref_t *ref = &entity->skin->bones[i].bone;
        if (!validate_ref_has_issue(ref)) continue;
        ++*issue_count;
        if (visitor != NULL && !visitor(
                user_data, source, ref, "skin.bones", i)) {
            return false;
        }
    }
    return true;
}

static size_t validate_foreach_typed_ref_issue(
    const nmo_cmd_ctx_t *c,
    nmo_object_repository_t *repo,
    validate_typed_ref_issue_fn visitor,
    void *user_data)
{
    size_t issue_count = 0;
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(c->ctx);
    if (type_rt == NULL || type_rt->types == NULL) return 0;

    size_t object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *source = nmo_object_repository_get_by_index(repo, i);
        if (source == NULL) continue;

        const nmo_type_descriptor_t *current =
            nmo_type_registry_find_by_class_id_inherited(
                type_rt->types, (uint32_t)nmo_object_get_class_id(source));
        for (size_t depth = 0; current != NULL && depth < 64; ++depth) {
            const void *instance =
                nmo_type_query_object_get_ancestor_state_by_guid(
                    type_rt->types, source, current->guid);
            if (instance == NULL) break;
            validate_ref_walk_ctx_t walk = {
                .types = type_rt->types,
                .source = source,
                .visitor = visitor,
                .user_data = user_data,
                .issue_count = &issue_count,
                .keep_going = true,
            };
            if (!validate_visit_reflected_refs(
                    &walk, current, instance, NULL, 0, false, 0) ||
                !walk.keep_going) {
                return issue_count;
            }
            if (nmo_guid_is_null(current->base_type)) break;
            current = nmo_type_registry_find_by_guid(
                type_rt->types, current->base_type);
        }
        if (!validate_visit_skin_bone_ref_issues(
                type_rt->types, source, visitor, user_data, &issue_count)) {
            return issue_count;
        }
    }
    return issue_count;
}

typedef struct validate_json_ref_issue_ctx {
    yyjson_mut_doc *doc;
    yyjson_mut_val *array;
    nmo_cmd_ctx_t *command;
} validate_json_ref_issue_ctx_t;

static bool validate_add_json_ref_issue(
    void *user_data,
    const nmo_object_t *source,
    const nmo_ref_t *ref,
    const char *field,
    size_t index)
{
    validate_json_ref_issue_ctx_t *ctx =
        (validate_json_ref_issue_ctx_t *)user_data;
    yyjson_mut_val *issue = yyjson_mut_obj(ctx->doc);
    yyjson_mut_obj_add_uint(ctx->doc, issue, "source_id", source->id);
    yyjson_mut_obj_add_uint(ctx->doc, issue, "target_id", ref->raw_id);
    yyjson_mut_obj_add_str(
        ctx->doc, issue, "state", validate_ref_state_name(ref->state));
    yyjson_mut_obj_add_str(ctx->doc, issue, "field", field);
    yyjson_mut_obj_add_uint(ctx->doc, issue, "index", (uint64_t)index);
    const char *source_class = nmo_cli_class_name_from_id(
        ctx->command->ctx, nmo_object_get_class_id(source));
    if (source_class != NULL) {
        yyjson_mut_obj_add_str(ctx->doc, issue, "source_class", source_class);
    }
    const char *source_name = nmo_object_get_name(source);
    if (source_name != NULL && source_name[0] != '\0') {
        nmo_cli_json_add_str_safe(
            ctx->doc, issue, "source_name", source_name);
    }
    yyjson_mut_arr_add_val(ctx->array, issue);
    return true;
}

typedef struct validate_text_ref_issue_ctx {
    FILE *out;
} validate_text_ref_issue_ctx_t;

static bool validate_print_text_ref_issue(
    void *user_data,
    const nmo_object_t *source,
    const nmo_ref_t *ref,
    const char *field,
    size_t index)
{
    validate_text_ref_issue_ctx_t *ctx =
        (validate_text_ref_issue_ctx_t *)user_data;
    fprintf(ctx->out, "  Source %u %s[%zu] -> %u: %s\n",
            source->id, field, index, ref->raw_id,
            validate_ref_state_name(ref->state));
    return true;
}

static nmo_class_id_t validate_expected_class_for_kind(nmo_ref_kind_t kind)
{
    switch (kind) {
    case NMO_REF_KIND_MESH: return NMO_CID_MESH;
    case NMO_REF_KIND_MATERIAL: return NMO_CID_MATERIAL;
    case NMO_REF_KIND_TEXTURE: return NMO_CID_TEXTURE;
    case NMO_REF_KIND_BEHAVIOR_LINK: return NMO_CID_BEHAVIORLINK;
    case NMO_REF_KIND_PARAMETER: return NMO_CID_PARAMETER;
    case NMO_REF_KIND_PLACE: return NMO_CID_PLACE;
    case NMO_REF_KIND_SKIN_BONE: return NMO_CID_BODYPART;
    case NMO_REF_KIND_DATA_ARRAY: return NMO_CID_DATAARRAY;
    case NMO_REF_KIND_SCRIPT: return NMO_CID_BEHAVIOR;
    default: return 0;
    }
}

static bool validate_edge_has_class_mismatch(
    const nmo_cmd_ctx_t *command,
    const nmo_object_repository_t *repo,
    const nmo_ref_edge_t *edge)
{
    nmo_class_id_t expected = validate_expected_class_for_kind(edge->kind);
    if (expected == 0) return false;
    const nmo_object_t *target = nmo_object_repository_find_by_id(repo, edge->to);
    if (target == NULL) return false;
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(command->ctx);
    return type_rt != NULL && type_rt->types != NULL &&
        !nmo_type_registry_is_class_derived_from(
            type_rt->types, (uint32_t)nmo_object_get_class_id(target),
            (uint32_t)expected);
}

static size_t validate_count_edge_class_mismatches(
    const nmo_cmd_ctx_t *command,
    const nmo_object_repository_t *repo,
    const nmo_ref_edge_t *edges,
    size_t edge_count)
{
    size_t count = 0;
    for (size_t i = 0; i < edge_count; ++i) {
        if (validate_edge_has_class_mismatch(command, repo, &edges[i])) ++count;
    }
    return count;
}

static int nmo_cmd_validate_references_in_session(nmo_cmd_ctx_t *c, int argc, char **argv) {
    bool suggest_fixes = parse_fix_flag(argc, argv);
    bool normalize = parse_flag(argc, argv, "--normalize");
    const char *output_path = parse_string_option(argc, argv, "--output", "-o");
    if (normalize && !output_path) {
        fprintf(stderr, "Error: --normalize requires -o/--output\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (normalize && paths_refer_same_file(output_path, c->file_path)) {
        fprintf(stderr, "Error: --normalize must write to a different file\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Get reference graph from session cache */
    nmo_object_repository_t *repo = nmo_tool_owner_repository(c->workspace);
    nmo_ref_graph_t *graph = nmo_tool_owner_ref_graph(c->workspace);
    if (!graph) {
        fprintf(stderr, "Error: Failed to create reference graph\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Validate references */
    nmo_ref_edge_t *broken_edges = NULL;
    size_t broken_count = 0;
    nmo_status_t status = nmo_ref_graph_validate(graph, &broken_edges, &broken_count);
    nmo_ref_edge_t *all_edges = NULL;
    size_t all_edge_count = 0;
    if (nmo_ref_graph_get_edges(graph, &all_edges, &all_edge_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to enumerate reference graph\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Get stats */
    nmo_ref_graph_stats_t stats;
    nmo_ref_graph_get_stats(graph, &stats);
    size_t record_issue_count = validate_foreach_typed_ref_issue(
        c, repo, NULL, NULL);
    size_t edge_mismatch_count = validate_count_edge_class_mismatches(
        c, repo, all_edges, all_edge_count);
    size_t typed_issue_count = record_issue_count + edge_mismatch_count;

    size_t normalized_changed = 0;
    if (normalize) {
        const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(c->ctx);
        nmo_status_t normalize_status = nmo_runtime_normalize_invalid_refs(
            repo, type_rt, &normalized_changed);
        if (normalize_status != NMO_OK) {
            fprintf(stderr, "Error normalizing references: %s\n",
                    nmo_error_string(normalize_status));
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        nmo_save_options_t save_options = nmo_tool_owner_save_options_default();
        int save_rc = nmo_cli_save_document(c->document, output_path, &save_options);
        if (save_rc != NMO_CLI_EXIT_SUCCESS) return save_rc;
    }

    int exit_code = NMO_CLI_EXIT_SUCCESS;
    if ((status != NMO_OK || typed_issue_count > 0) &&
        c->global && c->global->strict_mode) {
        exit_code = NMO_CLI_EXIT_STRICT_FAILURE;
    }

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        /* Summary stats */
        yyjson_mut_obj_add_uint(doc, data, "total_references", (uint64_t)stats.total_edges);
        yyjson_mut_obj_add_uint(doc, data, "broken_count",
                                (uint64_t)(broken_count + typed_issue_count));
        yyjson_mut_obj_add_uint(doc, data, "typed_issue_count",
                                (uint64_t)typed_issue_count);
        yyjson_mut_obj_add_uint(doc, data, "self_refs", (uint64_t)stats.self_refs);
        yyjson_mut_obj_add_bool(doc, data, "valid",
                                status == NMO_OK && typed_issue_count == 0);
        if (normalize) {
            yyjson_mut_obj_add_uint(doc, data, "normalized_count",
                                    (uint64_t)normalized_changed);
            yyjson_mut_obj_add_str(doc, data, "output", output_path);
        }

        /* Stats by kind */
        yyjson_mut_val *by_kind = yyjson_mut_obj(doc);
        for (int i = 0; i < NMO_REF_KIND_MAX; ++i) {
            if (stats.edge_counts[i] > 0) {
                yyjson_mut_obj_add_uint(doc, by_kind, nmo_ref_kind_name((nmo_ref_kind_t)i),
                                        (uint64_t)stats.edge_counts[i]);
            }
        }
        yyjson_mut_obj_add_val(doc, data, "by_kind", by_kind);

        /* Broken references list */
        if (broken_count > 0) {
            yyjson_mut_val *broken_arr = yyjson_mut_arr(doc);
            for (size_t i = 0; i < broken_count; ++i) {
                yyjson_mut_val *edge = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, edge, "source_id", broken_edges[i].from);
                bool unresolved = false;
                nmo_object_id_t target_id = validate_display_target_id(
                    repo, broken_edges[i].to, &unresolved);
                yyjson_mut_obj_add_uint(doc, edge, "target_id", target_id);
                if (unresolved) {
                    yyjson_mut_obj_add_str(doc, edge, "state", "unresolved");
                }
                yyjson_mut_obj_add_str(doc, edge, "kind", nmo_ref_kind_name(broken_edges[i].kind));
                yyjson_mut_obj_add_str(doc, edge, "field",
                                       broken_edges[i].field_path ? broken_edges[i].field_path : "unknown");
                if (broken_edges[i].index > 0) {
                    yyjson_mut_obj_add_uint(doc, edge, "index", broken_edges[i].index);
                }

                /* Add source object info */
                nmo_object_t *source = nmo_object_repository_find_by_id(repo, broken_edges[i].from);
                if (source) {
                    const char *source_class = nmo_cli_class_name_from_id(c->ctx, nmo_object_get_class_id(source));
                    if (source_class) {
                        yyjson_mut_obj_add_str(doc, edge, "source_class", source_class);
                    }
                    const char *source_name = nmo_object_get_name(source);
                    if (source_name && source_name[0]) {
                        nmo_cli_json_add_str_safe(doc, edge, "source_name", source_name);
                    }
                }

                if (suggest_fixes) {
                    yyjson_mut_obj_add_str(doc, edge, "fix",
                                           "null the reference field or re-save to strip dangling refs");
                }
                yyjson_mut_arr_add_val(broken_arr, edge);
            }
            yyjson_mut_obj_add_val(doc, data, "broken_references", broken_arr);
        }

        if (typed_issue_count > 0) {
            yyjson_mut_val *typed_arr = yyjson_mut_arr(doc);
            validate_json_ref_issue_ctx_t issue_ctx = {
                .doc = doc,
                .array = typed_arr,
                .command = c
            };
            (void)validate_foreach_typed_ref_issue(
                c, repo, validate_add_json_ref_issue, &issue_ctx);
            for (size_t i = 0; i < all_edge_count; ++i) {
                if (!validate_edge_has_class_mismatch(c, repo, &all_edges[i])) {
                    continue;
                }
                yyjson_mut_val *issue = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(
                    doc, issue, "source_id", all_edges[i].from);
                yyjson_mut_obj_add_uint(
                    doc, issue, "target_id", all_edges[i].to);
                yyjson_mut_obj_add_str(doc, issue, "state", "class_mismatch");
                yyjson_mut_obj_add_str(
                    doc, issue, "field",
                    all_edges[i].field_path ? all_edges[i].field_path : "unknown");
                yyjson_mut_obj_add_uint(
                    doc, issue, "index", all_edges[i].index);
                yyjson_mut_obj_add_str(
                    doc, issue, "kind", nmo_ref_kind_name(all_edges[i].kind));
                yyjson_mut_arr_add_val(typed_arr, issue);
            }
            yyjson_mut_obj_add_val(doc, data, "typed_reference_issues", typed_arr);
        }

        nmo_cmd_ctx_json_end(c, doc, data, "validate.references");
    } else {
        /* Text output */
        nmo_cli_print_heading(c->out, "Reference Validation", c->colorize);
        nmo_cli_print_kv(c->out, "File", c->file_path, 16, c->colorize);
        fprintf(c->out, "\n");

        /* Summary */
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", stats.total_edges);
        nmo_cli_print_kv(c->out, "Total references", buf, 16, c->colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.self_refs);
        nmo_cli_print_kv(c->out, "Self-references", buf, 16, c->colorize);
        snprintf(buf, sizeof(buf), "%zu", broken_count + typed_issue_count);
        nmo_cli_print_kv(c->out, "Broken references", buf, 16, c->colorize);
        fprintf(c->out, "\n");

        /* Status */
        if (status == NMO_OK && typed_issue_count == 0) {
            fprintf(c->out, "All references valid\n");
        } else {
            fprintf(c->out, "Broken references found\n\n");

            /* List broken references */
            static const nmo_cli_table_col_t cols[] = {
                {"Source", NMO_CLI_ALIGN_RIGHT, 8, 0},
                {"Target", NMO_CLI_ALIGN_RIGHT, 8, 0},
                {"Kind", NMO_CLI_ALIGN_LEFT, 15, 0},
                {"Field", NMO_CLI_ALIGN_LEFT, 20, 0},
                {"Source Class", NMO_CLI_ALIGN_LEFT, 18, 0},
                {"Source Name", NMO_CLI_ALIGN_LEFT, 20, 0},
            };

            nmo_cli_table_t table;
            nmo_cli_table_init(&table, cols, sizeof(cols) / sizeof(cols[0]));

            for (size_t i = 0; i < broken_count; ++i) {
                char from_buf[16], to_buf[16];
                snprintf(from_buf, sizeof(from_buf), "%u", broken_edges[i].from);
                nmo_object_id_t target_id = validate_display_target_id(
                    repo, broken_edges[i].to, NULL);
                snprintf(to_buf, sizeof(to_buf), "%u", target_id);

                char field_buf[32];
                const char *field_name = broken_edges[i].field_path ? broken_edges[i].field_path : "unknown";
                if (broken_edges[i].index > 0) {
                    snprintf(field_buf, sizeof(field_buf), "%s[%u]",
                             field_name, broken_edges[i].index);
                } else {
                    snprintf(field_buf, sizeof(field_buf), "%s", field_name);
                }

                nmo_object_t *source = nmo_object_repository_find_by_id(repo, broken_edges[i].from);
                const char *source_class = "-";
                const char *source_name = "-";

                if (source) {
                    const char *sc = nmo_cli_class_name_from_id(c->ctx, nmo_object_get_class_id(source));
                    if (sc) source_class = sc;
                    const char *sn = nmo_object_get_name(source);
                    if (sn && sn[0]) source_name = sn;
                }

                const char *cells[] = {
                    from_buf,
                    to_buf,
                    nmo_ref_kind_name(broken_edges[i].kind),
                    field_buf,
                    source_class,
                    source_name
                };
                nmo_cli_table_add_row(&table, cells, 6);
            }

            nmo_cli_table_print(&table, c->out, c->colorize);
            nmo_cli_table_free(&table);

            if (typed_issue_count > 0) {
                validate_text_ref_issue_ctx_t issue_ctx = {.out = c->out};
                (void)validate_foreach_typed_ref_issue(
                    c, repo, validate_print_text_ref_issue, &issue_ctx);
                for (size_t i = 0; i < all_edge_count; ++i) {
                    if (!validate_edge_has_class_mismatch(
                            c, repo, &all_edges[i])) {
                        continue;
                    }
                    fprintf(c->out,
                            "  Source %u %s[%u] -> %u: class_mismatch\n",
                            all_edges[i].from,
                            all_edges[i].field_path
                                ? all_edges[i].field_path : "unknown",
                            all_edges[i].index, all_edges[i].to);
                }
            }

            if (suggest_fixes) {
                fprintf(c->out, "\nSuggested fixes:\n");
                fprintf(c->out, "  - Re-save file with 'nmo convert' to strip dangling references\n");
                fprintf(c->out, "  - Or null specific reference fields via DSL script mode\n");
            }
        }
        if (normalize) {
            fprintf(c->out, "\nNormalized references: %zu\nOutput: %s\n",
                    normalized_changed, output_path);
        }
    }

    return exit_code;
}

int nmo_cmd_validate_references(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    nmo_load_diagnostics_t diagnostics;
    nmo_load_diagnostics_init(&diagnostics);
    nmo_load_options_t options = nmo_load_options_default();
    options.diagnostics = &diagnostics;
    int rc = nmo_cmd_ctx_init_with_load_options(&c, argc, argv, global, &options);
    if (rc) {
        nmo_load_diagnostics_destroy(&diagnostics);
        return rc;
    }
    rc = nmo_cmd_validate_references_in_session(&c, argc, argv);
    rc = nmo_cmd_ctx_done(&c, rc);
    nmo_load_diagnostics_destroy(&diagnostics);
    return rc;
}

/* ============================================================================
 * validate resources
 * ============================================================================ */

static int nmo_cmd_validate_resources_in_session(nmo_cmd_ctx_t *c, int argc, char **argv) {
    (void)argc;
    (void)argv;

    const nmo_tool_plugin_diagnostics_t *diag =
        nmo_document_get_plugin_diagnostics(c->document);

    size_t error_count = 0;
    size_t warning_count = 0;

    if (diag) {
        error_count = diag->missing_count;
        warning_count = diag->outdated_count;
        if (!diag->extension_registry_available) {
            warning_count += 1;
        }
    }

    int exit_code = NMO_CLI_EXIT_SUCCESS;
    if (error_count > 0 && c->global && c->global->strict_mode) {
        exit_code = NMO_CLI_EXIT_STRICT_FAILURE;
    }
    if (warning_count > 0 && c->global && c->global->fail_on_warning) {
        exit_code = NMO_CLI_EXIT_WARNING;
    }

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file", c->file_path);
        yyjson_mut_obj_add_bool(doc, data, "registry_available", diag ? diag->extension_registry_available : false);
        yyjson_mut_obj_add_uint(doc, data, "missing_count", (uint64_t)(diag ? diag->missing_count : 0));
        yyjson_mut_obj_add_uint(doc, data, "outdated_count", (uint64_t)(diag ? diag->outdated_count : 0));
        yyjson_mut_obj_add_uint(doc, data, "entry_count", (uint64_t)(diag ? diag->entry_count : 0));
        yyjson_mut_obj_add_uint(doc, data, "error_count", (uint64_t)error_count);
        yyjson_mut_obj_add_uint(doc, data, "warning_count", (uint64_t)warning_count);

        yyjson_mut_val *entries = yyjson_mut_arr(doc);
        if (diag && diag->entries) {
            for (size_t i = 0; i < diag->entry_count; ++i) {
                const nmo_tool_plugin_dependency_status_t *e = &diag->entries[i];
                yyjson_mut_val *entry = yyjson_mut_obj(doc);

                char guid_buf[64];
                nmo_guid_format(e->guid, guid_buf, sizeof(guid_buf));
                yyjson_mut_obj_add_strcpy(doc, entry, "guid", guid_buf);
                yyjson_mut_obj_add_uint(doc, entry, "category", (uint64_t)e->category);
                yyjson_mut_obj_add_uint(doc, entry, "required_version", e->required_version);
                yyjson_mut_obj_add_uint(doc, entry, "resolved_version", e->resolved_version);
                if (e->resolved_name) {
                    yyjson_mut_obj_add_str(doc, entry, "name", e->resolved_name);
                }
                yyjson_mut_obj_add_uint(doc, entry, "status_flags", e->status_flags);

                yyjson_mut_val *status = yyjson_mut_arr(doc);
                if (e->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_MISSING) {
                    yyjson_mut_arr_add_str(doc, status, "missing");
                }
                if (e->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_VERSION_TOO_OLD) {
                    yyjson_mut_arr_add_str(doc, status, "outdated");
                }
                if (e->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_MANAGER_UNAVAILABLE) {
                    yyjson_mut_arr_add_str(doc, status, "manager_unavailable");
                }
                yyjson_mut_obj_add_val(doc, entry, "status", status);

                yyjson_mut_arr_add_val(entries, entry);
            }
        }
        yyjson_mut_obj_add_val(doc, data, "entries", entries);

        nmo_cmd_ctx_json_end(c, doc, data, "validate.resources");
    } else {
        nmo_cli_print_heading(c->out, "Resource Validation", c->colorize);
        nmo_cli_print_kv(c->out, "File", c->file_path, 18, c->colorize);

        if (!diag) {
            fprintf(c->out, "\nPlugin diagnostics unavailable\n");
        } else {
            fprintf(c->out, "\n");
            nmo_cli_print_kv(c->out, "Registry", diag->extension_registry_available ? "available" : "unavailable", 18, c->colorize);
            char buf[32];
            snprintf(buf, sizeof(buf), "%zu", diag->entry_count);
            nmo_cli_print_kv(c->out, "Entries", buf, 18, c->colorize);
            snprintf(buf, sizeof(buf), "%zu", diag->missing_count);
            nmo_cli_print_kv(c->out, "Missing", buf, 18, c->colorize);
            snprintf(buf, sizeof(buf), "%zu", diag->outdated_count);
            nmo_cli_print_kv(c->out, "Outdated", buf, 18, c->colorize);

            if (diag->entries && diag->entry_count > 0 &&
                c->global && c->global->verbosity > 0) {
                fprintf(c->out, "\nEntries:\n");
                for (size_t i = 0; i < diag->entry_count; ++i) {
                    const nmo_tool_plugin_dependency_status_t *e = &diag->entries[i];
                    char guid_buf[64];
                    nmo_guid_format(e->guid, guid_buf, sizeof(guid_buf));
                    fprintf(c->out, "  %s", guid_buf);
                    if (e->resolved_name) {
                        fprintf(c->out, " (%s)", e->resolved_name);
                    }
                    if (e->status_flags) {
                        fprintf(c->out, " [flags=0x%X]", e->status_flags);
                    }
                    fprintf(c->out, "\n");
                }
            }
        }
    }

    return exit_code;
}

int nmo_cmd_validate_resources(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;
    rc = nmo_cmd_validate_resources_in_session(&c, argc, argv);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * validate orphans
 * ============================================================================ */

/**
 * Binary search for an ID in a sorted array.
 */
static bool id_is_in_set(const nmo_object_id_t *arr, size_t count,
                          nmo_object_id_t id) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (arr[mid] < id) {
            lo = mid + 1;
        } else if (arr[mid] > id) {
            hi = mid;
        } else {
            return true;
        }
    }
    return false;
}

typedef struct orphan_info {
    nmo_object_t *obj;
    size_t outgoing;
    size_t data_size;
    bool is_direct;  /* true = zero incoming, false = chain orphan */
} orphan_info_t;

typedef struct validate_orphan_data {
    nmo_ref_graph_t *graph;
    const nmo_object_id_t *orphan_ids;
    size_t orphan_id_count;
    const nmo_object_query_t *filter_query;
    orphan_info_t *orphan_list;
    size_t orphan_cap;
    size_t likely_orphans;
    size_t likely_orphan_size;
    size_t total_filtered;
    size_t direct_orphan_count;
    size_t chain_orphan_count;
} validate_orphan_data_t;

static int validate_orphan_object(size_t index, nmo_object_t *obj,
                                  const nmo_cmd_ctx_t *c, void *user)
{
    (void)index;

    validate_orphan_data_t *data = (validate_orphan_data_t *)user;
    if (!data || !obj) {
        return 0;
    }

    nmo_object_id_t obj_id = nmo_object_get_id(obj);

    if (!id_is_in_set(data->orphan_ids, data->orphan_id_count, obj_id)) {
        if (nmo_core_query_matches_object(c, data->filter_query, obj)) {
            data->total_filtered++;
        }
        return 0;
    }

    if (!nmo_core_query_matches_object(c, data->filter_query, obj)) {
        return 0;
    }
    data->total_filtered++;

    nmo_ref_edge_t *in_edges = NULL;
    size_t in_count = 0;
    nmo_ref_graph_get_object_edges(data->graph, obj_id, NMO_REF_DIR_INCOMING,
                                   &in_edges, &in_count);

    bool is_direct = (in_count == 0);

    nmo_ref_edge_t *out_edges = NULL;
    size_t out_count = 0;
    nmo_ref_graph_get_object_edges(data->graph, obj_id, NMO_REF_DIR_OUTGOING,
                                   &out_edges, &out_count);

    size_t data_size = 0;
    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    if (chunk) {
        data_size = nmo_chunk_get_data_size(chunk);
    }

    data->likely_orphans++;
    data->likely_orphan_size += data_size;
    if (is_direct) {
        data->direct_orphan_count++;
    } else {
        data->chain_orphan_count++;
    }

    if (data->likely_orphans <= data->orphan_cap) {
        size_t orphan_index = data->likely_orphans - 1;
        data->orphan_list[orphan_index].obj = obj;
        data->orphan_list[orphan_index].outgoing = out_count;
        data->orphan_list[orphan_index].data_size = data_size;
        data->orphan_list[orphan_index].is_direct = is_direct;
    }

    return 0;
}

static int validate_orphans_run_in_ctx(nmo_cmd_ctx_t *c,
                                       int argc,
                                       char **argv,
                                       const nmo_cli_global_opts_t *global,
                                       bool allow_strip,
                                       bool close_ctx)
{
    static const nmo_opt_def_t opts[] = {
        {"--class",   "-c", NMO_OPT_STRING, "Filter by class name"},
        {"--strict",  NULL,  NMO_OPT_FLAG,   "Exit code 3 if orphans found"},
        {"--summary", NULL,  NMO_OPT_FLAG,   "Summary only (no per-object listing)"},
        {"--strip",   NULL,  NMO_OPT_FLAG,   "Remove orphans and save to --output"},
        {"--output",  "-o",  NMO_OPT_STRING, "Output file for --strip"},
    };
    enum { OPT_CLASS, OPT_STRICT, OPT_SUMMARY, OPT_STRIP, OPT_OUTPUT };
    nmo_opt_val_t vals[5];
    const char *pos_arr[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 5, &r) < 0) {
        return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_ARG_ERROR)
                         : NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *class_filter_str = vals[OPT_CLASS].present ? vals[OPT_CLASS].val.str : NULL;
    bool strict = vals[OPT_STRICT].present || (global && global->strict_mode);
    bool summary_only = vals[OPT_SUMMARY].present && vals[OPT_SUMMARY].val.flag;
    bool do_strip = vals[OPT_STRIP].present && vals[OPT_STRIP].val.flag;
    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;

    if (!allow_strip && (do_strip || vals[OPT_OUTPUT].present)) {
        fprintf(stderr, "Validation write/fix options are not supported in in-session read mode.\n");
        return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_ARG_ERROR)
                         : NMO_CLI_EXIT_ARG_ERROR;
    }

    if (do_strip && !output_path) {
        fprintf(stderr, "Error: --strip requires -o/--output\n");
        return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_ARG_ERROR)
                         : NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_object_query_t class_query = {0};
    nmo_core_query_build_options_t query_opts = {
        .class_name = class_filter_str,
        .include_derived_classes = true,
    };
    int rc = nmo_core_query_build(c, &class_query, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return close_ctx ? nmo_cmd_ctx_done(c, rc) : rc;
    }
    const nmo_object_query_t *filter_query =
        class_filter_str != NULL ? &class_query : NULL;

    nmo_core_iter_result_t object_query_result = {0};
    rc = nmo_core_object_query_run(c, NULL, NULL, NULL, &object_query_result);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Error: Failed to query objects\n");
        return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    size_t object_count = object_query_result.matched;

    /* Get reference graph from session cache */
    nmo_ref_graph_t *graph = nmo_tool_owner_ref_graph(c->workspace);
    if (!graph) {
        fprintf(stderr, "Error: Failed to create reference graph\n");
        return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Arena for mark-sweep allocations */
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        fprintf(stderr, "Error: Failed to create arena\n");
        return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Use library API for core orphan detection */
    nmo_object_repository_t *repo = nmo_tool_owner_repository(c->workspace);
    nmo_object_id_t *orphan_ids = NULL;
    size_t orphan_id_count = 0;
    {
        nmo_status_t ms = nmo_ref_graph_find_orphans(
            graph, repo, c->registry, arena,
            &orphan_ids, &orphan_id_count);
        if (ms != NMO_OK) {
            nmo_arena_destroy(arena);
            fprintf(stderr, "Error: Orphan detection failed\n");
            return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    }

    size_t reachable_count = object_count - orphan_id_count;

    orphan_info_t *orphan_list = NULL;
    size_t orphan_cap = 0;
    if (object_count > 0) {
        orphan_list = (orphan_info_t *)nmo_arena_alloc(arena,
            object_count * sizeof(orphan_info_t),
            _Alignof(orphan_info_t));
        if (!orphan_list) {
            nmo_arena_destroy(arena);
            fprintf(stderr, "Error: Allocation failed\n");
            return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        orphan_cap = object_count;
    }

    validate_orphan_data_t orphan_data = {
        .graph = graph,
        .orphan_ids = orphan_ids,
        .orphan_id_count = orphan_id_count,
        .filter_query = filter_query,
        .orphan_list = orphan_list,
        .orphan_cap = orphan_cap,
    };
    rc = nmo_core_object_query_run(c, NULL, validate_orphan_object,
                                   &orphan_data, NULL);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        nmo_arena_destroy(arena);
        fprintf(stderr, "Error: Failed to query objects\n");
        return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Determine exit code */
    int exit_code = NMO_CLI_EXIT_SUCCESS;
    if (orphan_data.likely_orphans > 0 && strict) {
        exit_code = NMO_CLI_EXIT_STRICT_FAILURE;
    }

    double orphan_pct = (orphan_data.total_filtered > 0)
        ? (100.0 * (double)orphan_data.likely_orphans / (double)orphan_data.total_filtered)
        : 0.0;

    /* Global (pre-filter) reachability stats */
    size_t unreachable_count = object_count - reachable_count;

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file", c->file_path);
        yyjson_mut_obj_add_uint(doc, data, "total_objects",
                                (uint64_t)orphan_data.total_filtered);
        yyjson_mut_obj_add_uint(doc, data, "reachable_count",
                                (uint64_t)reachable_count);
        yyjson_mut_obj_add_uint(doc, data, "unreachable_count",
                                (uint64_t)unreachable_count);
        yyjson_mut_obj_add_uint(doc, data, "likely_orphans",
                                (uint64_t)orphan_data.likely_orphans);
        yyjson_mut_obj_add_uint(doc, data, "likely_orphan_size",
                                (uint64_t)orphan_data.likely_orphan_size);
        yyjson_mut_obj_add_real(doc, data, "orphan_percentage", orphan_pct);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < orphan_data.likely_orphans && i < orphan_cap; ++i) {
            nmo_object_t *obj = orphan_list[i].obj;
            yyjson_mut_val *entry = yyjson_mut_obj(doc);

            yyjson_mut_obj_add_uint(doc, entry, "id",
                                    (uint64_t)nmo_object_get_id(obj));

            nmo_class_id_t cid = nmo_object_get_class_id(obj);
            const char *cname = nmo_core_class_name(c, cid);
            char cbuf[32];
            if (!cname) {
                snprintf(cbuf, sizeof(cbuf), "Class#%u", cid);
                cname = cbuf;
            }
            yyjson_mut_obj_add_str(doc, entry, "class_name", cname);
            yyjson_mut_obj_add_uint(doc, entry, "size",
                                    (uint64_t)orphan_list[i].data_size);
            yyjson_mut_obj_add_uint(doc, entry, "outgoing",
                                    (uint64_t)orphan_list[i].outgoing);

            const char *name = nmo_object_get_name(obj);
            if (name && name[0]) {
                nmo_cli_json_add_str_safe(doc, entry, "name", name);
            }

            yyjson_mut_obj_add_str(doc, entry, "orphan_kind",
                                    orphan_list[i].is_direct ? "direct" : "chain");
            yyjson_mut_arr_add_val(arr, entry);
        }
        yyjson_mut_obj_add_val(doc, data, "objects", arr);

        nmo_cmd_ctx_json_end(c, doc, data, "validate.orphans");
    } else {
        /* Text output */
        nmo_cli_print_heading(c->out, "Orphan Detection", c->colorize);
        nmo_cli_print_kv(c->out, "File", c->file_path, 18, c->colorize);
        fprintf(c->out, "\n");

        if (orphan_data.likely_orphans > 0 && !summary_only) {
            static const nmo_cli_table_col_t cols[] = {
                {"ID",       NMO_CLI_ALIGN_RIGHT, 6, 0},
                {"CLASS",    NMO_CLI_ALIGN_LEFT, 18, 0},
                {"SIZE",     NMO_CLI_ALIGN_RIGHT, 8, 0},
                {"OUTGOING", NMO_CLI_ALIGN_RIGHT, 8, 0},
                {"KIND",     NMO_CLI_ALIGN_LEFT, 8, 0},
                {"NAME",     NMO_CLI_ALIGN_LEFT, 24, 0},
            };

            nmo_cli_table_t table;
            nmo_cli_table_init(&table, cols, sizeof(cols) / sizeof(cols[0]));

            for (size_t i = 0; i < orphan_data.likely_orphans && i < orphan_cap; ++i) {
                nmo_object_t *obj = orphan_list[i].obj;
                char id_buf[16], size_buf[16], out_buf[16];
                snprintf(id_buf, sizeof(id_buf), "%u",
                         nmo_object_get_id(obj));
                snprintf(size_buf, sizeof(size_buf), "%zu",
                         orphan_list[i].data_size);
                snprintf(out_buf, sizeof(out_buf), "%zu",
                         orphan_list[i].outgoing);

                nmo_class_id_t cid = nmo_object_get_class_id(obj);
                const char *cname = nmo_core_class_name(c, cid);
                char cbuf[32];
                if (!cname) {
                    snprintf(cbuf, sizeof(cbuf), "Class#%u", cid);
                    cname = cbuf;
                }

                const char *kind = orphan_list[i].is_direct ? "direct" : "chain";

                const char *name = nmo_object_get_name(obj);
                const char *name_str = (name && name[0]) ? name : "-";

                const char *cells[] = {
                    id_buf, cname, size_buf, out_buf, kind, name_str
                };
                nmo_cli_table_add_row(&table, cells, 6);
            }

            nmo_cli_table_print(&table, c->out, c->colorize);
            nmo_cli_table_free(&table);
        }

        /* Summary */
        double reachable_pct = (object_count > 0)
            ? (100.0 * (double)reachable_count / (double)object_count)
            : 0.0;

        fprintf(c->out, "\nReachable: %zu/%zu objects (%.1f%%)\n",
                reachable_count, object_count, reachable_pct);
        fprintf(c->out, "Unreachable: %zu objects (%.1f%%), %zu bytes\n",
                unreachable_count,
                (object_count > 0)
                    ? (100.0 * (double)unreachable_count / (double)object_count)
                    : 0.0,
                orphan_data.likely_orphan_size);
        fprintf(c->out, "  Direct orphans (zero incoming): %zu\n",
                orphan_data.direct_orphan_count);
        fprintf(c->out, "  Chain orphans (reachable only from other orphans): %zu\n",
                orphan_data.chain_orphan_count);
    }

    /* --strip: remove orphan objects and save cleaned file */
    if (do_strip && orphan_data.likely_orphans > 0) {
        if (orphan_data.likely_orphans >= object_count) {
            if (!c->is_json) {
                fprintf(c->out, "\nAll objects are orphans - nothing to save.\n");
            }
            if (arena) nmo_arena_destroy(arena);
            return close_ctx ? nmo_cmd_ctx_done(c, exit_code) : exit_code;
        }

        nmo_object_id_t *orphan_ids = (nmo_object_id_t *)malloc(
            orphan_data.likely_orphans * sizeof(nmo_object_id_t));
        if (!orphan_ids) {
            fprintf(stderr, "Error: Out of memory for strip operation\n");
            if (arena) nmo_arena_destroy(arena);
            return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        {
            for (size_t i = 0; i < orphan_data.likely_orphans && i < orphan_cap; i++)
                orphan_ids[i] = nmo_object_get_id(orphan_list[i].obj);

            /* Destroy arena before modifying session (arena owns mark-sweep data) */
            nmo_arena_destroy(arena);
            arena = NULL;

            nmo_runtime_report_t report;
            memset(&report, 0, sizeof(report));
            nmo_tool_owner_destroy_objects(c->workspace, orphan_ids,
                                        orphan_data.likely_orphans, 0, &report);
            free(orphan_ids);

            nmo_save_options_t save_opts = nmo_tool_owner_save_options_default();
            int save_rc = nmo_cli_save_document(c->document, output_path, &save_opts);
            if (save_rc != NMO_CLI_EXIT_SUCCESS) {
                return close_ctx ? nmo_cmd_ctx_done(c, save_rc) : save_rc;
            }

            if (!c->is_json) {
                fprintf(c->out, "\nStripped %zu orphan(s), saved to %s\n",
                        report.deleted_objects, output_path);
            }
        }
    }

    if (arena) nmo_arena_destroy(arena);
    return close_ctx ? nmo_cmd_ctx_done(c, exit_code) : exit_code;
}

int nmo_cmd_validate_orphans(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;
    return validate_orphans_run_in_ctx(&c, argc, argv, global, true, true);
}

static int nmo_cmd_validate_orphans_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    nmo_cli_global_opts_t global;
    if (ctx && ctx->global) {
        global = *ctx->global;
    } else {
        nmo_cli_global_opts_init(&global);
    }
    return validate_orphans_run_in_ctx(ctx, argc, argv, &global, false, false);
}

