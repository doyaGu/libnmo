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
#include "../nmo_cli_record.h"
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
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/nmo_object_guids.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_query.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
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
    nmo_cli_record_array_t *lines;
    size_t error_count;
    size_t warning_count;
} validate_all_data_t;

/* One load diagnostic: a structure issue in JSON, a "Parse error" line in text. */
static nmo_cli_record_t *validate_load_issue_record(const nmo_load_issue_t *issue)
{
    nmo_cli_record_t *item = nmo_cli_record_new();
    if (!item) {
        return NULL;
    }
    nmo_cli_record_str(item, "severity", NULL, "error");
    nmo_cli_record_uint(item, "id", NULL, issue->object_id);
    nmo_cli_record_uint(item, "file_id", NULL, issue->file_id);
    nmo_cli_record_int(item, "class_id", NULL, (int64_t)issue->class_id);
    nmo_cli_record_str(item, "schema", NULL, issue->schema_name);
    nmo_cli_record_uint(item, "section", NULL, issue->section_id);
    nmo_cli_record_uint(item, "dword_offset", NULL, issue->dword_offset);
    nmo_cli_record_int(item, "status", NULL, issue->status);
    nmo_cli_record_str(item, "message", NULL, issue->message);
    nmo_cli_record_set_summary_fmt(
        item,
        "Parse error: object=%u file=%u class=%" PRIu32 " schema=%s section=0x%08X "
        "dword=%zu status=%d: %s",
        issue->object_id, issue->file_id, (uint32_t)issue->class_id,
        issue->schema_name[0] ? issue->schema_name : "unknown",
        issue->section_id, issue->dword_offset, issue->status,
        issue->message[0] ? issue->message : nmo_error_string(issue->status));
    return item;
}

/* A text-only line among the items of an array. */
static bool validate_add_line(nmo_cli_record_array_t *array, const char *format, ...)
{
    nmo_cli_record_t *item = nmo_cli_record_new();
    if (!item) {
        return false;
    }
    nmo_cli_record_omit_json(item);
    va_list args;
    va_start(args, format);
    char *text = nmo_tool_vstrdup_fmt(format, args);
    va_end(args);
    bool ok = text != NULL && nmo_cli_record_set_summary(item, text);
    free(text);
    if (!ok) {
        nmo_cli_record_free(item);
        return false;
    }
    return nmo_cli_record_array_add(array, item);
}

/* Write `rec` for `c` as the report of `input_file`; keeps `rc` unless the
 * report itself cannot be written. */
static int validate_emit(const nmo_cmd_ctx_t *c, nmo_cli_record_t *rec,
                         const char *command, const char *input_file,
                         int key_width, int rc)
{
    nmo_cmd_ctx_t out = *c;
    out.file_path = input_file;
    int emit_rc = nmo_cmd_ctx_emit_record(&out, rec, command, key_width,
                                          c->colorize);
    return emit_rc != NMO_CLI_EXIT_SUCCESS ? emit_rc : rc;
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
    /* Both resolvers allocate the result when given a NULL buffer. */
#ifdef _WIN32
    char *full_a = _fullpath(NULL, a, 0);
    char *full_b = _fullpath(NULL, b, 0);
    bool same = (full_a && full_b)
        ? nmo_tool_streq_ci(full_a, full_b)
        : nmo_tool_streq_ci(a, b);
#else
    char *full_a = realpath(a, NULL);
    char *full_b = realpath(b, NULL);
    bool same = (full_a && full_b)
        ? strcmp(full_a, full_b) == 0
        : strcmp(a, b) == 0;
#endif
    free(full_a);
    free(full_b);
    return same;
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
        if (data->global && data->global->verbosity > 0) {
            validate_add_line(data->lines, "Warning: Object %u has no chunk",
                              nmo_object_get_id(obj));
        }
        return 0;
    }

    nmo_chunk_validation_t result;
    nmo_status_t rc = nmo_inspector_validate_chunk(chunk, &result);
    if (rc != NMO_OK || !result.is_valid) {
        data->error_count++;
        validate_add_line(data->lines,
                          "Error: Object %u chunk validation failed: %s",
                          nmo_object_get_id(obj),
                          result.error_message[0] ? result.error_message : "unknown");
    }

    return 0;
}

typedef struct validate_structure_data {
    const nmo_cli_global_opts_t *global;
    bool suggest_fixes;
    nmo_cli_record_t **issues; /* owned until moved into the report */
    size_t issue_count;
    size_t issue_capacity;
    bool out_of_memory;
    size_t error_count;
    size_t warning_count;
    size_t checked_count;
} validate_structure_data_t;

/* Queue an issue or line for the report; takes ownership of `item`. */
static void validate_structure_push(validate_structure_data_t *data,
                                    nmo_cli_record_t *item)
{
    if (!item) {
        data->out_of_memory = true;
        return;
    }
    if (data->issue_count == data->issue_capacity) {
        size_t capacity = data->issue_capacity ? data->issue_capacity * 2u : 16u;
        nmo_cli_record_t **grown = (nmo_cli_record_t **)realloc(
            data->issues, capacity * sizeof(*grown));
        if (!grown) {
            nmo_cli_record_free(item);
            data->out_of_memory = true;
            return;
        }
        data->issues = grown;
        data->issue_capacity = capacity;
    }
    data->issues[data->issue_count++] = item;
}

static nmo_cli_record_t *validate_structure_issue_record(
    const nmo_cmd_ctx_t *c, const nmo_object_t *obj, const char *severity,
    const char *message, const char *fix)
{
    nmo_cli_record_t *item = nmo_cli_record_new();
    if (!item) {
        return NULL;
    }
    nmo_cli_record_str(item, "severity", NULL, severity);
    nmo_cli_record_uint(item, "id", NULL, nmo_object_get_id(obj));
    nmo_cli_record_uint(item, "class_id", NULL, nmo_object_get_class_id(obj));
    const char *class_name = nmo_cli_class_name_from_id(
        c->ctx, nmo_object_get_class_id(obj));
    if (class_name) {
        nmo_cli_record_str(item, "class_name", NULL, class_name);
    }
    nmo_cli_record_str(item, "message", NULL, message);
    if (fix) {
        nmo_cli_record_str(item, "fix", NULL, fix);
    }
    return item;
}

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
    unsigned verbosity = data->global ? (unsigned)data->global->verbosity : 0u;

    if (!chunk) {
        data->warning_count++;
        nmo_cli_record_t *item = validate_structure_issue_record(
            c, obj, "warning", "missing chunk",
            data->suggest_fixes ? "re-save file to regenerate chunks" : NULL);
        if (item && verbosity > 0) {
            nmo_cli_record_set_summary_fmt(
                item, "Warning: Object %u has no chunk%s", obj_id,
                data->suggest_fixes
                    ? "\n  Fix: Re-save file to regenerate chunks" : "");
        }
        validate_structure_push(data, item);
        return 0;
    }

    data->checked_count++;

    if (verbosity >= 2) {
        size_t ds = 0;
        (void)nmo_chunk_get_data(chunk, &ds);
        nmo_cli_record_t *line = nmo_cli_record_new();
        if (line) {
            nmo_cli_record_omit_json(line);
            nmo_cli_record_set_summary_fmt(
                line, "  Object %u: chunk %zu bytes", obj_id, ds);
        }
        validate_structure_push(data, line);
    }

    nmo_chunk_validation_t result;
    nmo_status_t vrc = nmo_inspector_validate_chunk(chunk, &result);
    if (vrc != NMO_OK || !result.is_valid) {
        data->error_count++;
        const char *message =
            result.error_message[0] ? result.error_message : "validation failed";
        nmo_cli_record_t *item = validate_structure_issue_record(
            c, obj, "error", message,
            data->suggest_fixes
                ? "re-save with nmo convert to regenerate chunk data" : NULL);
        if (item) {
            nmo_cli_record_set_summary_fmt(
                item, "Error: Object %u chunk invalid: %s%s", obj_id, message,
                data->suggest_fixes
                    ? "\n  Fix: Re-save with 'nmo convert' to regenerate chunk data"
                    : "");
        }
        validate_structure_push(data, item);
    }

    return 0;
}

/* ============================================================================
 * validate all (single-file core + batch support)
 * ============================================================================ */

/**
 * Core validation logic for a single file: appends the load diagnostics, the
 * per-object findings, and the summary to `rec`.
 */
static int validate_all_run(nmo_cmd_ctx_t *cmd,
                            const nmo_cli_global_opts_t *global,
                            nmo_cli_record_t *rec)
{
    validate_all_data_t validate_data = {
        .global = global,
        .lines = nmo_cli_record_array(rec, NULL, NULL),
    };
    nmo_cli_record_array_omit_heading(validate_data.lines);
    if (cmd->load_diagnostics) {
        validate_data.error_count += cmd->load_diagnostics->count;
        for (size_t i = 0; i < cmd->load_diagnostics->count; ++i) {
            nmo_cli_record_array_add(
                validate_data.lines,
                validate_load_issue_record(&cmd->load_diagnostics->issues[i]));
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

    nmo_cli_record_bool(rec, "valid", NULL, validate_data.error_count == 0);
    nmo_cli_record_text_fmt(rec, "Objects", "%zu", query_result.matched);
    nmo_cli_record_uint(rec, "error_count", "Errors", validate_data.error_count);
    nmo_cli_record_uint(rec, "warning_count", "Warnings",
                        validate_data.warning_count);
    nmo_cli_record_uint(rec, "object_count", NULL, query_result.matched);
    nmo_cli_record_raw_fmt(rec, "Result: %s\n",
                           validate_data.error_count == 0 ? "VALID" : "INVALID");
    return exit_code;
}

/* Open `file_path` and append its validation report to `rec`. */
static int validate_all_file(const char *file_path,
                             const nmo_cli_global_opts_t *global,
                             nmo_cli_record_t *rec)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    char *open_error = NULL;

    nmo_load_diagnostics_t diagnostics;
    nmo_load_diagnostics_init(&diagnostics);
    nmo_load_options_t options = nmo_load_options_default();
    options.diagnostics = &diagnostics;
    if (!nmo_tool_open_document_opts(file_path, &options, &ctx, &document, &workspace,
                                     &open_error)) {
        nmo_load_diagnostics_destroy(&diagnostics);
        fprintf(stderr, "Error: %s\n", open_error ? open_error : "Failed to open file");
        free(open_error);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_cmd_ctx_t cmd;
    nmo_cmd_ctx_init_from_repl_document(&cmd, ctx, document, workspace, false);
    cmd.load_diagnostics = &diagnostics;

    int rc = validate_all_run(&cmd, global, rec);
    nmo_tool_close_document(ctx, document, workspace);
    nmo_load_diagnostics_destroy(&diagnostics);
    return rc;
}

/* nmo_tool_batch_run handler: JSON into `data` when `doc` is set, else text. */
static int validate_all_single(const char *file_path,
                               const nmo_cli_global_opts_t *global,
                               void *user_data,
                               yyjson_mut_doc *doc,
                               yyjson_mut_val *data)
{
    const nmo_tool_text_output_ctx_t *text_ctx =
        (const nmo_tool_text_output_ctx_t *)user_data;

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!rec) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    int rc = validate_all_file(file_path, global, rec);
    if (doc && data) {
        if (!nmo_cli_record_to_json(rec, doc, data)) {
            rc = NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    } else {
        FILE *out = (text_ctx && text_ctx->out) ? text_ctx->out : stdout;
        bool colorize = text_ctx ? text_ctx->colorize
                                 : nmo_cli_should_colorize(global, out);
        nmo_cli_record_print_kv(rec, out, 12, colorize);
    }
    nmo_cli_record_free(rec);
    return rc;
}

/* The "Validation Results" report of `file_path`, before its findings. */
static nmo_cli_record_t *validate_all_report_new(const char *file_path)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!rec) {
        return NULL;
    }
    nmo_cli_record_title(rec, "Validation Results");
    nmo_cli_record_text(rec, "File", file_path);
    nmo_cli_record_raw(rec, "\n");
    return rec;
}

static int nmo_cmd_validate_all_in_session(nmo_cmd_ctx_t *cmd, int argc, char **argv)
{
    (void)argc;
    (void)argv;

    nmo_cli_record_t *rec = validate_all_report_new(cmd->file_path);
    if (!rec) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    int rc = validate_all_run(cmd, cmd->global, rec);
    nmo_cli_record_str(rec, "file", NULL, cmd->file_path);
    return validate_emit(cmd, rec, "validate.all", cmd->file_path, 12, rc);
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

    /* Single file mode - validate_all_file opens its own session */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (rc) return rc;

    const char *file_path = nmo_tool_find_file_arg(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo validate all <file>\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_cli_record_t *rec = validate_all_report_new(file_path);
    if (!rec) {
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    rc = validate_all_file(file_path, global, rec);
    nmo_cli_record_str(rec, "file", NULL, file_path);
    rc = validate_emit(&c, rec, "validate.all", file_path, 12, rc);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * validate structure
 * ============================================================================ */

static int nmo_cmd_validate_structure_in_session(nmo_cmd_ctx_t *c, int argc, char **argv) {
    validate_structure_data_t structure_data = {
        .global = c->global,
        .suggest_fixes = parse_fix_flag(argc, argv),
    };
    if (c->load_diagnostics) {
        structure_data.error_count += c->load_diagnostics->count;
        for (size_t i = 0; i < c->load_diagnostics->count; ++i) {
            validate_structure_push(
                &structure_data,
                validate_load_issue_record(&c->load_diagnostics->issues[i]));
        }
    }
    nmo_core_iter_result_t query_result = {0};
    int rc = nmo_core_object_query_run(c, NULL, validate_structure_object,
                                       &structure_data, &query_result);
    nmo_cli_record_t *rec = NULL;
    if (rc == NMO_CLI_EXIT_SUCCESS && !structure_data.out_of_memory) {
        rec = nmo_cli_record_new();
    }
    if (!rec) {
        for (size_t i = 0; i < structure_data.issue_count; ++i) {
            nmo_cli_record_free(structure_data.issues[i]);
        }
        free(structure_data.issues);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            fprintf(stderr, "Error: Failed to query objects\n");
        }
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    int exit_code = NMO_CLI_EXIT_SUCCESS;
    if (structure_data.error_count > 0 && c->global && c->global->strict_mode) {
        exit_code = NMO_CLI_EXIT_STRICT_FAILURE;
    }
    if (structure_data.warning_count > 0 && c->global && c->global->fail_on_warning) {
        exit_code = NMO_CLI_EXIT_WARNING;
    }

    nmo_cli_record_title(rec, "Structure Validation");
    nmo_cli_record_str(rec, "file", "File", c->file_path);
    nmo_cli_record_raw(rec, "\n");
    nmo_cli_record_bool(rec, "valid", NULL, structure_data.error_count == 0);
    nmo_cli_record_uint(rec, "object_count", NULL, query_result.matched);
    nmo_cli_record_uint(rec, "checked_chunks", NULL, structure_data.checked_count);
    nmo_cli_record_uint(rec, "error_count", NULL, structure_data.error_count);
    nmo_cli_record_uint(rec, "warning_count", NULL, structure_data.warning_count);
    nmo_cli_record_array_t *issues = nmo_cli_record_array(rec, "issues", NULL);
    nmo_cli_record_array_omit_heading(issues);
    for (size_t i = 0; i < structure_data.issue_count; ++i) {
        nmo_cli_record_array_add(issues, structure_data.issues[i]);
    }
    free(structure_data.issues);
    nmo_cli_record_raw(rec, "\nSummary:\n");
    nmo_cli_record_text_fmt(rec, "Objects", "%zu", query_result.matched);
    nmo_cli_record_text_fmt(rec, "Chunks", "%zu", structure_data.checked_count);
    nmo_cli_record_text_fmt(rec, "Errors", "%zu", structure_data.error_count);
    nmo_cli_record_text_fmt(rec, "Warnings", "%zu", structure_data.warning_count);

    return validate_emit(c, rec, "validate.structure", c->file_path, 14,
                         exit_code);
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

/* malloc'd "<prefix>.<field>" style path of a reference field. */
static char *validate_build_ref_path_dup(
    const char *prefix,
    size_t prefix_index,
    bool prefix_has_index,
    const char *field_name,
    bool materialize_prefix_index)
{
    const char *name = field_name != NULL ? field_name : "ref";
    const bool omit_ref_name = prefix != NULL && prefix[0] != '\0' &&
        field_name != NULL && strcmp(field_name, "ref") == 0;
    if (prefix == NULL || prefix[0] == '\0') {
        return nmo_tool_strdup(name);
    }
    if (omit_ref_name) {
        return nmo_tool_strdup(prefix);
    }
    if (prefix_has_index && materialize_prefix_index) {
        return nmo_tool_strdup_fmt("%s[%zu].%s", prefix, prefix_index, name);
    }
    return nmo_tool_strdup_fmt("%s.%s", prefix, name);
}

static bool validate_visit_reflected_refs(
    validate_ref_walk_ctx_t *ctx,
    const nmo_type_descriptor_t *type,
    const void *instance,
    const char *prefix,
    size_t prefix_index,
    bool prefix_has_index,
    unsigned depth);

/* Visit one field of a reflected struct; false stops the whole walk. */
static bool validate_visit_reflected_field(
    validate_ref_walk_ctx_t *ctx,
    const nmo_type_descriptor_t *type,
    const void *instance,
    const nmo_type_field_t *field,
    const void *field_ptr,
    const char *path,
    size_t prefix_index,
    bool prefix_has_index,
    unsigned depth)
{
    if (nmo_field_is_ref(field)) {
        if (!nmo_field_uses_ref_records(field)) return true;
        if (!nmo_field_is_array(field)) {
            if (field->size == sizeof(nmo_ref_t) &&
                !validate_report_typed_ref(
                    ctx, (const nmo_ref_t *)field_ptr, path,
                    prefix_has_index ? prefix_index : 0u)) {
                return false;
            }
            return true;
        }

        validate_repeated_view_t view;
        if (!validate_get_repeated_view(
                type, instance, field, ctx->types, &view) ||
            view.element_size != sizeof(nmo_ref_t)) {
            return true;
        }
        const nmo_ref_t *refs = (const nmo_ref_t *)view.data;
        for (size_t i = 0; i < view.count; ++i) {
            if (!validate_report_typed_ref(ctx, &refs[i], path, i)) {
                return false;
            }
        }
        return true;
    }

    const nmo_type_descriptor_t *nested =
        nmo_type_registry_find_by_guid(ctx->types, field->type_guid);
    if (nested == NULL ||
        (nested->category & NMO_TYPE_CATEGORY_STRUCT) == 0 ||
        !nmo_type_has_reflection(nested)) {
        return true;
    }

    if (nmo_field_is_array(field)) {
        validate_repeated_view_t view;
        if (!validate_get_repeated_view(
                type, instance, field, ctx->types, &view) ||
            view.element_size != nested->size) {
            return true;
        }
        for (size_t i = 0; i < view.count; ++i) {
            const void *element = (const unsigned char *)view.data +
                i * view.element_size;
            if (!validate_visit_reflected_refs(
                    ctx, nested, element, path, i, true, depth + 1u)) {
                return false;
            }
        }
        return true;
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
    return true;
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

        char *path = validate_build_ref_path_dup(
            prefix, prefix_index, prefix_has_index,
            field->name, nmo_field_is_array(field));
        if (path == NULL) {
            /* Out of memory: stop the walk the same way a visitor would. */
            ctx->keep_going = false;
            return false;
        }
        bool keep_going = validate_visit_reflected_field(
            ctx, type, instance, field, field_ptr, path,
            prefix_index, prefix_has_index, depth);
        free(path);
        if (!keep_going) {
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

static bool validate_visit_dataarray_ref_issues(
    const nmo_type_registry_t *types,
    nmo_object_t *source,
    validate_typed_ref_issue_fn visitor,
    void *user_data,
    size_t *issue_count)
{
    const nmo_dataarray_state_t *dataarray =
        (const nmo_dataarray_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                types, source, CKPGUID_DATAARRAY);
    if (dataarray == NULL || dataarray->column_formats == NULL ||
        dataarray->rows == NULL) {
        return true;
    }

    for (size_t row_index = 0; row_index < dataarray->row_count; ++row_index) {
        const nmo_dataarray_row_t *row = &dataarray->rows[row_index];
        if (row->cells == NULL) continue;
        const size_t column_count = row->column_count < dataarray->column_count
            ? row->column_count : dataarray->column_count;
        for (size_t column_index = 0;
             column_index < column_count;
             ++column_index) {
            const CK_ARRAYTYPE column_type =
                dataarray->column_formats[column_index].type;
            const nmo_ref_t *ref = NULL;
            if (column_type == CKARRAYTYPE_OBJECT) {
                ref = &row->cells[column_index].object_ref;
            } else if (column_type == CKARRAYTYPE_PARAMETER) {
                ref = &row->cells[column_index].parameter.ref;
            }
            if (!validate_ref_has_issue(ref)) continue;
            ++*issue_count;
            const size_t index = row_index * dataarray->column_count +
                column_index;
            if (visitor != NULL && !visitor(
                    user_data, source, ref, "rows", index)) {
                return false;
            }
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
        if (!validate_visit_dataarray_ref_issues(
                type_rt->types, source, visitor, user_data, &issue_count)) {
            return issue_count;
        }
    }
    return issue_count;
}

typedef struct validate_ref_issue_record_ctx {
    const nmo_cmd_ctx_t *command;
    nmo_cli_record_array_t *array;
} validate_ref_issue_record_ctx_t;

static bool validate_add_ref_issue_record(
    void *user_data,
    const nmo_object_t *source,
    const nmo_ref_t *ref,
    const char *field,
    size_t index)
{
    validate_ref_issue_record_ctx_t *ctx =
        (validate_ref_issue_record_ctx_t *)user_data;
    nmo_cli_record_t *item = nmo_cli_record_new();
    if (!item) {
        return false;
    }
    const char *state = validate_ref_state_name(ref->state);
    nmo_cli_record_uint(item, "source_id", NULL, source->id);
    nmo_cli_record_uint(item, "target_id", NULL, ref->raw_id);
    nmo_cli_record_str(item, "state", NULL, state);
    nmo_cli_record_str(item, "field", NULL, field);
    nmo_cli_record_uint(item, "index", NULL, (uint64_t)index);
    const char *source_class = nmo_cli_class_name_from_id(
        ctx->command->ctx, nmo_object_get_class_id(source));
    if (source_class != NULL) {
        nmo_cli_record_str(item, "source_class", NULL, source_class);
    }
    nmo_cli_record_str_opt(item, "source_name", NULL,
                           nmo_object_get_name(source), NULL);
    nmo_cli_record_set_summary_fmt(item, "  Source %u %s[%zu] -> %u: %s",
                                   source->id, field, index, ref->raw_id,
                                   state);
    return nmo_cli_record_array_add(ctx->array, item);
}

static nmo_class_id_t validate_expected_class_for_kind(nmo_ref_kind_t kind)
{
    switch (kind) {
    case NMO_REF_KIND_MESH: return NMO_CID_MESH;
    case NMO_REF_KIND_MATERIAL: return NMO_CID_MATERIAL;
    case NMO_REF_KIND_TEXTURE: return NMO_CID_TEXTURE;
    case NMO_REF_KIND_BEHAVIOR_LINK: return NMO_CID_BEHAVIORLINK;
    case NMO_REF_KIND_PLACE: return NMO_CID_PLACE;
    case NMO_REF_KIND_SKIN_BONE: return NMO_CID_BODYPART;
    case NMO_REF_KIND_DATA_ARRAY: return NMO_CID_DATAARRAY;
    case NMO_REF_KIND_SCRIPT: return NMO_CID_BEHAVIOR;
    default: return 0;
    }
}

static bool validate_target_matches_kind(
    const nmo_type_registry_t *types,
    nmo_ref_kind_t kind,
    nmo_class_id_t target_class_id)
{
    if (types == NULL) return true;

    /* CKParameterIn is a CKObject sibling of CKParameter, while
     * CKParameterOut and CKParameterLocal derive from CKParameter.  The
     * graph kind describes the semantic parameter family, not that C++
     * inheritance branch. */
    if (kind == NMO_REF_KIND_PARAMETER) {
        return target_class_id == NMO_CID_PARAMETERIN ||
            nmo_type_registry_is_class_derived_from(
                types, (uint32_t)target_class_id, NMO_CID_PARAMETER);
    }

    nmo_class_id_t expected = validate_expected_class_for_kind(kind);
    return expected == 0 || nmo_type_registry_is_class_derived_from(
        types, (uint32_t)target_class_id, (uint32_t)expected);
}

static bool validate_edge_has_class_mismatch(
    const nmo_cmd_ctx_t *command,
    const nmo_object_repository_t *repo,
    const nmo_ref_edge_t *edge)
{
    const nmo_object_t *target = nmo_object_repository_find_by_id(repo, edge->to);
    if (target == NULL) return false;
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(command->ctx);
    return type_rt != NULL && type_rt->types != NULL &&
        !validate_target_matches_kind(
            type_rt->types, edge->kind, nmo_object_get_class_id(target));
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

static int validate_compare_object_ids(const void *lhs, const void *rhs)
{
    const nmo_object_id_t a = *(const nmo_object_id_t *)lhs;
    const nmo_object_id_t b = *(const nmo_object_id_t *)rhs;
    return (a > b) - (a < b);
}

static const nmo_cli_table_col_t validate_broken_ref_columns[] = {
    {"Source", NMO_CLI_ALIGN_RIGHT, 8, 0},
    {"Target", NMO_CLI_ALIGN_RIGHT, 8, 0},
    {"Kind", NMO_CLI_ALIGN_LEFT, 15, 0},
    {"Field", NMO_CLI_ALIGN_LEFT, 20, 0},
    {"Source Class", NMO_CLI_ALIGN_LEFT, 18, 0},
    {"Source Name", NMO_CLI_ALIGN_LEFT, 20, 0},
};

/* One graph edge whose target is missing; a table row in text. */
static nmo_cli_record_t *validate_broken_edge_record(
    const nmo_cmd_ctx_t *c,
    const nmo_object_repository_t *repo,
    const nmo_ref_edge_t *edge,
    bool suggest_fixes)
{
    nmo_cli_record_t *item = nmo_cli_record_new();
    if (!item) {
        return NULL;
    }
    bool unresolved = false;
    nmo_object_id_t target_id = validate_display_target_id(
        repo, edge->to, &unresolved);
    const char *field_name = edge->field_path ? edge->field_path : "unknown";
    nmo_cli_record_uint(item, "source_id", "Source", edge->from);
    nmo_cli_record_uint(item, "target_id", "Target", target_id);
    if (unresolved) {
        nmo_cli_record_str(item, "state", NULL, "unresolved");
    }
    nmo_cli_record_str(item, "kind", "Kind", nmo_ref_kind_name(edge->kind));
    nmo_cli_record_str(item, "field", NULL, field_name);
    if (edge->index > 0) {
        nmo_cli_record_uint(item, "index", NULL, edge->index);
        nmo_cli_record_text_fmt(item, "Field", "%s[%u]", field_name, edge->index);
    } else {
        nmo_cli_record_text(item, "Field", field_name);
    }

    const nmo_object_t *source = nmo_object_repository_find_by_id(repo, edge->from);
    const char *source_class = source
        ? nmo_cli_class_name_from_id(c->ctx, nmo_object_get_class_id(source))
        : NULL;
    if (source_class) {
        nmo_cli_record_str(item, "source_class", "Source Class", source_class);
    } else {
        nmo_cli_record_text(item, "Source Class", "-");
    }
    nmo_cli_record_str_opt(item, "source_name", "Source Name",
                           source ? nmo_object_get_name(source) : NULL, "-");
    if (suggest_fixes) {
        nmo_cli_record_str(item, "fix", NULL,
                           "null the reference field or re-save to strip dangling refs");
    }
    return item;
}

/* One graph edge whose target has the wrong class for its kind. */
static nmo_cli_record_t *validate_mismatch_edge_record(const nmo_ref_edge_t *edge)
{
    nmo_cli_record_t *item = nmo_cli_record_new();
    if (!item) {
        return NULL;
    }
    const char *field_name = edge->field_path ? edge->field_path : "unknown";
    nmo_cli_record_uint(item, "source_id", NULL, edge->from);
    nmo_cli_record_uint(item, "target_id", NULL, edge->to);
    nmo_cli_record_str(item, "state", NULL, "class_mismatch");
    nmo_cli_record_str(item, "field", NULL, field_name);
    nmo_cli_record_uint(item, "index", NULL, edge->index);
    nmo_cli_record_str(item, "kind", NULL, nmo_ref_kind_name(edge->kind));
    nmo_cli_record_set_summary_fmt(item, "  Source %u %s[%u] -> %u: class_mismatch",
                                   edge->from, field_name, edge->index, edge->to);
    return item;
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
        const size_t object_count = nmo_object_repository_get_count(repo);
        if (object_count > SIZE_MAX / sizeof(nmo_object_id_t)) {
            fprintf(stderr, "Error normalizing references: object count overflow\n");
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        nmo_object_id_t *changed_ids = object_count > 0u
            ? (nmo_object_id_t *)malloc(
                object_count * sizeof(nmo_object_id_t))
            : NULL;
        if (object_count > 0u && changed_ids == NULL) {
            fprintf(stderr, "Error normalizing references: out of memory\n");
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        size_t changed_object_count = 0u;
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
            if (object == NULL) continue;
            size_t object_changes = 0u;
            nmo_status_t normalize_status =
                nmo_runtime_normalize_object_invalid_refs(
                    repo, type_rt, object, &object_changes);
            if (normalize_status != NMO_OK) {
                fprintf(stderr, "Error normalizing references: %s\n",
                        nmo_error_string(normalize_status));
                free(changed_ids);
                return NMO_CLI_EXIT_INTERNAL_ERROR;
            }
            if (object_changes > 0u) {
                changed_ids[changed_object_count++] = object->id;
                normalized_changed += object_changes;
            }
        }
        if (changed_object_count > 1u) {
            qsort(changed_ids, changed_object_count,
                  sizeof(*changed_ids), validate_compare_object_ids);
        }
        nmo_save_options_t save_options = nmo_tool_owner_save_options_default();
        nmo_file_info_t file_info = nmo_document_get_file_info(c->document);
        const uint32_t compression_mask =
            NMO_FILE_WRITE_CHUNK_COMPRESSED_OLD |
            NMO_FILE_WRITE_WHOLE_COMPRESSED;
        const bool source_compressed =
            (file_info.write_mode & compression_mask) != 0u;
        save_options.compress_header = source_compressed;
        save_options.compress_data = source_compressed;
        save_options.flags |= NMO_SAVE_CHANGED_OBJECTS_ONLY;
        save_options.changed_object_ids = changed_ids;
        save_options.changed_object_count = changed_object_count;
        int save_rc = nmo_cli_save_document(c->document, output_path, &save_options);
        free(changed_ids);
        if (save_rc != NMO_CLI_EXIT_SUCCESS) return save_rc;
    }

    int exit_code = NMO_CLI_EXIT_SUCCESS;
    if ((status != NMO_OK || typed_issue_count > 0) &&
        c->global && c->global->strict_mode) {
        exit_code = NMO_CLI_EXIT_STRICT_FAILURE;
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!rec) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    const bool valid = status == NMO_OK && typed_issue_count == 0;
    nmo_cli_record_title(rec, "Reference Validation");
    nmo_cli_record_text(rec, "File", c->file_path);
    nmo_cli_record_raw(rec, "\n");
    nmo_cli_record_uint(rec, "total_references", "Total references",
                        stats.total_edges);
    nmo_cli_record_uint(rec, "broken_count", NULL,
                        broken_count + typed_issue_count);
    nmo_cli_record_uint(rec, "typed_issue_count", NULL, typed_issue_count);
    nmo_cli_record_uint(rec, "self_refs", "Self-references", stats.self_refs);
    nmo_cli_record_text_fmt(rec, "Broken references", "%zu",
                            broken_count + typed_issue_count);
    nmo_cli_record_bool(rec, "valid", NULL, valid);
    if (normalize) {
        nmo_cli_record_uint(rec, "normalized_count", NULL, normalized_changed);
        nmo_cli_record_str(rec, "output", NULL, output_path);
    }
    nmo_cli_record_t *by_kind = nmo_cli_record_object(rec, "by_kind");
    for (int i = 0; i < NMO_REF_KIND_MAX; ++i) {
        if (stats.edge_counts[i] > 0) {
            nmo_cli_record_uint(by_kind, nmo_ref_kind_name((nmo_ref_kind_t)i),
                                NULL, stats.edge_counts[i]);
        }
    }
    nmo_cli_record_raw(rec, "\n");
    nmo_cli_record_raw(rec, valid ? "All references valid\n"
                                  : "Broken references found\n\n");

    /* The broken-reference table is shown whenever the file is invalid. */
    if (broken_count > 0 || !valid) {
        nmo_cli_record_array_t *broken = nmo_cli_record_array(
            rec, broken_count > 0 ? "broken_references" : NULL, NULL);
        if (!valid) {
            nmo_cli_record_array_set_table(
                broken, validate_broken_ref_columns,
                sizeof(validate_broken_ref_columns) /
                    sizeof(validate_broken_ref_columns[0]));
        }
        for (size_t i = 0; i < broken_count; ++i) {
            nmo_cli_record_array_add(
                broken, validate_broken_edge_record(
                            c, repo, &broken_edges[i], suggest_fixes));
        }
    }

    if (typed_issue_count > 0) {
        nmo_cli_record_array_t *typed = nmo_cli_record_array(
            rec, "typed_reference_issues", NULL);
        nmo_cli_record_array_omit_heading(typed);
        validate_ref_issue_record_ctx_t issue_ctx = {
            .command = c,
            .array = typed,
        };
        (void)validate_foreach_typed_ref_issue(
            c, repo, validate_add_ref_issue_record, &issue_ctx);
        for (size_t i = 0; i < all_edge_count; ++i) {
            if (validate_edge_has_class_mismatch(c, repo, &all_edges[i])) {
                nmo_cli_record_array_add(
                    typed, validate_mismatch_edge_record(&all_edges[i]));
            }
        }
    }

    if (!valid && suggest_fixes) {
        nmo_cli_record_raw(
            rec,
            "\nSuggested fixes:\n"
            "  - Re-save file with 'nmo convert' to strip dangling references\n"
            "  - Or null specific reference fields via DSL script mode\n");
    }
    if (normalize) {
        nmo_cli_record_raw_fmt(rec, "\nNormalized references: %zu\nOutput: %s\n",
                               normalized_changed, output_path);
    }

    return validate_emit(c, rec, "validate.references", c->file_path, 16,
                         exit_code);
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

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!rec) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    nmo_cli_record_title(rec, "Resource Validation");
    nmo_cli_record_str(rec, "file", "File", c->file_path);
    nmo_cli_record_raw(rec, diag ? "\n" : "\nPlugin diagnostics unavailable\n");
    nmo_cli_record_bool(rec, "registry_available", diag ? "Registry" : NULL,
                        diag ? diag->extension_registry_available : false);
    nmo_cli_record_set_text(rec, diag && diag->extension_registry_available
                                     ? "available" : "unavailable");
    if (diag) {
        nmo_cli_record_text_fmt(rec, "Entries", "%zu", diag->entry_count);
    }
    nmo_cli_record_uint(rec, "missing_count", diag ? "Missing" : NULL,
                        diag ? diag->missing_count : 0);
    nmo_cli_record_uint(rec, "outdated_count", diag ? "Outdated" : NULL,
                        diag ? diag->outdated_count : 0);
    nmo_cli_record_uint(rec, "entry_count", NULL, diag ? diag->entry_count : 0);
    nmo_cli_record_uint(rec, "error_count", NULL, error_count);
    nmo_cli_record_uint(rec, "warning_count", NULL, warning_count);

    /* The entry lines are verbose-only text. */
    nmo_cli_record_array_t *entries = nmo_cli_record_array(rec, "entries", NULL);
    if (diag && diag->entries && diag->entry_count > 0 &&
        c->global && c->global->verbosity > 0) {
        nmo_cli_record_array_set_heading(entries, "Entries:");
    }
    for (size_t i = 0; diag && diag->entries && i < diag->entry_count; ++i) {
        const nmo_tool_plugin_dependency_status_t *e = &diag->entries[i];
        nmo_cli_record_t *entry = nmo_cli_record_new();
        if (!entry) {
            break;
        }
        char guid_buf[NMO_GUID_STRING_SIZE];
        nmo_guid_format(e->guid, guid_buf, sizeof(guid_buf));
        nmo_cli_record_str(entry, "guid", NULL, guid_buf);
        nmo_cli_record_uint(entry, "category", NULL, (uint64_t)e->category);
        nmo_cli_record_uint(entry, "required_version", NULL, e->required_version);
        nmo_cli_record_uint(entry, "resolved_version", NULL, e->resolved_version);
        if (e->resolved_name) {
            nmo_cli_record_str(entry, "name", NULL, e->resolved_name);
        }
        nmo_cli_record_uint(entry, "status_flags", NULL, e->status_flags);
        const char *status[3];
        size_t status_count = 0;
        if (e->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_MISSING) {
            status[status_count++] = "missing";
        }
        if (e->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_VERSION_TOO_OLD) {
            status[status_count++] = "outdated";
        }
        if (e->status_flags & NMO_TOOL_PLUGIN_DEP_STATUS_MANAGER_UNAVAILABLE) {
            status[status_count++] = "manager_unavailable";
        }
        nmo_cli_record_str_list(entry, "status", NULL, status, status_count, NULL);
        const char *name_open = e->resolved_name ? " (" : "";
        const char *name = e->resolved_name ? e->resolved_name : "";
        const char *name_close = e->resolved_name ? ")" : "";
        if (e->status_flags) {
            nmo_cli_record_set_summary_fmt(
                entry, "  %s%s%s%s [flags=0x%X]", guid_buf, name_open, name,
                name_close, e->status_flags);
        } else {
            nmo_cli_record_set_summary_fmt(
                entry, "  %s%s%s%s", guid_buf, name_open, name, name_close);
        }
        nmo_cli_record_array_add(entries, entry);
    }

    return validate_emit(c, rec, "validate.resources", c->file_path, 18,
                         exit_code);
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

static const nmo_cli_table_col_t validate_orphan_columns[] = {
    {"ID",       NMO_CLI_ALIGN_RIGHT, 6, 0},
    {"CLASS",    NMO_CLI_ALIGN_LEFT, 18, 0},
    {"SIZE",     NMO_CLI_ALIGN_RIGHT, 8, 0},
    {"OUTGOING", NMO_CLI_ALIGN_RIGHT, 8, 0},
    {"KIND",     NMO_CLI_ALIGN_LEFT, 8, 0},
    {"NAME",     NMO_CLI_ALIGN_LEFT, 24, 0},
};

/* One likely orphan; a table row in text. */
static nmo_cli_record_t *validate_orphan_record(const nmo_cmd_ctx_t *c,
                                                const orphan_info_t *orphan)
{
    nmo_cli_record_t *item = nmo_cli_record_new();
    if (!item) {
        return NULL;
    }
    nmo_object_t *obj = orphan->obj;
    nmo_cli_record_uint(item, "id", "ID", nmo_object_get_id(obj));
    char *cname = nmo_core_class_name_dup(c, nmo_object_get_class_id(obj));
    nmo_cli_record_str(item, "class_name", "CLASS", cname ? cname : "");
    free(cname);
    nmo_cli_record_uint(item, "size", "SIZE", orphan->data_size);
    nmo_cli_record_uint(item, "outgoing", "OUTGOING", orphan->outgoing);
    const char *name = nmo_object_get_name(obj);
    nmo_cli_record_str_opt(item, "name", NULL, name, NULL);
    nmo_cli_record_str(item, "orphan_kind", "KIND",
                       orphan->is_direct ? "direct" : "chain");
    nmo_cli_record_text(item, "NAME", (name && name[0]) ? name : "-");
    return item;
}

static int validate_orphans_run_in_ctx(nmo_cmd_ctx_t *c,
                                       int argc,
                                       char **argv,
                                       const nmo_cli_global_opts_t *global,
                                       bool allow_strip,
                                       bool close_ctx)
{
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_CLASS_FILTER,
        {"--strict",  NULL,  NMO_OPT_FLAG,   "Exit code 3 if orphans found"},
        {"--summary", NULL,  NMO_OPT_FLAG,   "Summary only (no per-object listing)"},
        {"--strip",   NULL,  NMO_OPT_FLAG,   "Remove orphans and save to --output"},
        {"--output",  "-o",  NMO_OPT_STRING, "Output file for --strip"},
    };
    enum { OPT_CLASS, OPT_STRICT, OPT_SUMMARY, OPT_STRIP, OPT_OUTPUT };
    nmo_opt_val_t vals[5];
    const char *pos_arr[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos_arr);
    if (nmo_opt_parse(argc, argv, opts, 5, &r) < 0) {
        return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_ARG_ERROR)
                         : NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *class_filter_str = nmo_opt_str(&vals[OPT_CLASS]);
    bool strict = vals[OPT_STRICT].present || (global && global->strict_mode);
    bool summary_only = nmo_opt_flag(&vals[OPT_SUMMARY]);
    bool do_strip = nmo_opt_flag(&vals[OPT_STRIP]);
    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);

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

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!rec) {
        nmo_arena_destroy(arena);
        return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    nmo_cli_record_title(rec, "Orphan Detection");
    nmo_cli_record_str(rec, "file", "File", c->file_path);
    nmo_cli_record_raw(rec, "\n");
    nmo_cli_record_uint(rec, "total_objects", NULL, orphan_data.total_filtered);
    nmo_cli_record_uint(rec, "reachable_count", NULL, reachable_count);
    nmo_cli_record_uint(rec, "unreachable_count", NULL, unreachable_count);
    nmo_cli_record_uint(rec, "likely_orphans", NULL, orphan_data.likely_orphans);
    nmo_cli_record_uint(rec, "likely_orphan_size", NULL,
                        orphan_data.likely_orphan_size);
    nmo_cli_record_real(rec, "orphan_percentage", NULL, orphan_pct, "%.1f");

    nmo_cli_record_array_t *objects = nmo_cli_record_array(rec, "objects", NULL);
    if (orphan_data.likely_orphans > 0 && !summary_only) {
        nmo_cli_record_array_set_table(
            objects, validate_orphan_columns,
            sizeof(validate_orphan_columns) / sizeof(validate_orphan_columns[0]));
    }
    for (size_t i = 0; i < orphan_data.likely_orphans && i < orphan_cap; ++i) {
        nmo_cli_record_array_add(
            objects, validate_orphan_record(c, &orphan_list[i]));
    }

    double reachable_pct = (object_count > 0)
        ? (100.0 * (double)reachable_count / (double)object_count)
        : 0.0;
    double unreachable_pct = (object_count > 0)
        ? (100.0 * (double)unreachable_count / (double)object_count)
        : 0.0;
    nmo_cli_record_raw_fmt(rec, "\nReachable: %zu/%zu objects (%.1f%%)\n",
                           reachable_count, object_count, reachable_pct);
    nmo_cli_record_raw_fmt(rec, "Unreachable: %zu objects (%.1f%%), %zu bytes\n",
                           unreachable_count, unreachable_pct,
                           orphan_data.likely_orphan_size);
    nmo_cli_record_raw_fmt(rec, "  Direct orphans (zero incoming): %zu\n",
                           orphan_data.direct_orphan_count);
    nmo_cli_record_raw_fmt(
        rec, "  Chain orphans (reachable only from other orphans): %zu\n",
        orphan_data.chain_orphan_count);

    exit_code = validate_emit(c, rec, "validate.orphans", c->file_path, 18,
                              exit_code);

    /* --strip: remove orphan objects and save cleaned file */
    if (do_strip && orphan_data.likely_orphans > 0) {
        if (orphan_data.likely_orphans >= object_count) {
            if (!c->is_json) {
                fprintf(c->out, "\nAll objects are orphans - nothing to save.\n");
            }
            if (arena) nmo_arena_destroy(arena);
            return close_ctx ? nmo_cmd_ctx_done(c, exit_code) : exit_code;
        }

        nmo_object_id_t *strip_ids = (nmo_object_id_t *)malloc(
            orphan_data.likely_orphans * sizeof(nmo_object_id_t));
        if (!strip_ids) {
            fprintf(stderr, "Error: Out of memory for strip operation\n");
            if (arena) nmo_arena_destroy(arena);
            return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        {
            for (size_t i = 0; i < orphan_data.likely_orphans && i < orphan_cap; i++)
                strip_ids[i] = nmo_object_get_id(orphan_list[i].obj);

            /* Destroy arena before modifying session (arena owns mark-sweep data) */
            nmo_arena_destroy(arena);
            arena = NULL;

            nmo_runtime_report_t report;
            memset(&report, 0, sizeof(report));
            nmo_tool_owner_destroy_objects(c->workspace, strip_ids,
                                        orphan_data.likely_orphans, 0, &report);
            free(strip_ids);

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

