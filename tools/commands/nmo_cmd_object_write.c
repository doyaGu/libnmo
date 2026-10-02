/**
 * @file nmo_cmd_object_write.c
 * @brief CLI object write commands: rename, delete, create, copy
 */

#include "nmo_cmd_object.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_write.h"
#include "../nmo_cli_output.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "object/nmo_context.h"
#include "document/nmo_document_save.h"
#include "object/nmo_object_edit.h"
#include "core/nmo_arena.h"
#include "core/nmo_parse.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int reject_in_session_output_option(bool present)
{
    if (present) {
        fprintf(stderr, "Error: REPL mutations update the loaded session; use 'save <path>' to write a file\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * object rename - Rename an object and save to new file
 *
 * Two modes:
 *   Single:  nmo object rename <id> <new_name> <file> -o <output>
 *   Batch:   nmo object rename --name <pattern> --to <template> [opts] <file> -o <output>
 * ============================================================================ */

/* "output" in JSON and "Saved to: <path>" in text, once a save happened. */
static bool object_write_add_output(nmo_cli_record_t *rec, bool dry_run,
                                    const char *output_path)
{
    if (dry_run || output_path == NULL) {
        return true;
    }
    return nmo_cli_record_str(rec, "output", NULL, output_path) &&
           nmo_cli_record_raw_fmt(rec, "Saved to: %s\n", output_path);
}

/* Emit a write report; frees `rec`, which is dropped when `ok` is false. */
static int object_write_emit(nmo_cmd_ctx_t *c, nmo_cli_record_t *rec, bool ok,
                             const char *cmd_name)
{
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    return nmo_cmd_ctx_emit_record(c, rec, cmd_name, 0, c->colorize);
}

/* Text-only "No objects matched." report; JSON prints nothing. */
static int object_write_emit_no_match(nmo_cmd_ctx_t *c, const char *cmd_name)
{
    if (c->is_json) {
        return NMO_CLI_EXIT_SUCCESS;
    }
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL && nmo_cli_record_raw(rec, "No objects matched.\n");
    return object_write_emit(c, rec, ok, cmd_name);
}

/** Rename entry used by both batch text and JSON output */
typedef struct {
    nmo_object_id_t id;
    nmo_class_id_t class_id;
    char *old_name; /**< malloc'd */
    char *new_name; /**< malloc'd */
    bool collision;
} rename_entry_t;

static void rename_entries_free(rename_entry_t *entries, size_t count)
{
    if (!entries) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        free(entries[i].old_name);
        free(entries[i].new_name);
    }
    free(entries);
}

typedef struct rename_collect_data {
    nmo_object_repository_t *repo;
    const nmo_object_query_t *filter_query;
    const char *name_pattern;
    const char *to_template;
    bool use_regex;
    rename_entry_t *entries;
    size_t count;
    size_t capacity;
    size_t collision_count;
    bool oom;
} rename_collect_data_t;

static int collect_rename_entry(size_t index,
                                nmo_object_t *obj,
                                const nmo_cmd_ctx_t *c,
                                void *user)
{
    (void)index;

    rename_collect_data_t *data = (rename_collect_data_t *)user;
    if (!data || !obj) {
        return 0;
    }

    const char *name = nmo_object_get_name(obj);
    if (!name || !name[0]) return 0;

    if (!nmo_core_query_matches_object(c, data->filter_query, obj)) {
        return 0;
    }

    char *new_name = NULL;
    if (data->use_regex) {
        nmo_object_query_t name_query = {
            .name = data->name_pattern,
            .name_mode = NMO_OBJECT_QUERY_NAME_REGEX,
            .name_case_insensitive = true
        };
        if (!nmo_core_query_matches_object(c, &name_query, obj)) {
            return 0;
        }

        new_name = nmo_tool_apply_rename_template(data->to_template, name, NULL, 0);
    } else {
        char **captures = NULL;
        size_t cap_count = 0;
        if (!nmo_tool_wildcard_capture_ci(data->name_pattern, name,
                                          &captures, &cap_count)) {
            return 0;
        }
        new_name = nmo_tool_apply_rename_template(
            data->to_template, name, (const char *const *)captures, cap_count);
        nmo_tool_captures_free(captures, cap_count);
    }
    if (!new_name) {
        fprintf(stderr, "Warning: Template expansion failed for '%s'\n", name);
        return 0;
    }

    if (strcmp(name, new_name) == 0) {
        free(new_name);
        return 0;
    }

    if (data->count >= data->capacity) {
        size_t new_capacity = data->capacity ? data->capacity * 2 : 32;
        rename_entry_t *new_entries = (rename_entry_t *)realloc(
            data->entries, new_capacity * sizeof(*new_entries));
        if (!new_entries) {
            free(new_name);
            data->oom = true;
            return 1;
        }
        data->entries = new_entries;
        data->capacity = new_capacity;
    }

    char *old_name = nmo_tool_strdup(name);
    if (!old_name) {
        free(new_name);
        data->oom = true;
        return 1;
    }

    rename_entry_t *entry = &data->entries[data->count++];
    entry->id = nmo_object_get_id(obj);
    entry->class_id = nmo_object_get_class_id(obj);
    entry->old_name = old_name;
    entry->new_name = new_name;

    nmo_object_t *existing = nmo_object_repository_find_by_name(
        data->repo, new_name);
    entry->collision =
        (existing && nmo_object_get_id(existing) != entry->id);
    if (entry->collision) {
        data->collision_count++;
    }

    return 0;
}

static nmo_status_t nmo_cmd_object_rename_with_edit(
    nmo_cmd_ctx_t *c,
    nmo_object_id_t object_id,
    const char *new_name)
{
    nmo_workspace_edit_t *edit = NULL;
    nmo_status_t rc = c->workspace != NULL
        ? nmo_workspace_edit_begin(c->workspace, "cli object rename", &edit)
        : NMO_ERR_INVALID_ARGUMENT;
    if (rc != NMO_OK) {
        return rc;
    }

    rc = nmo_object_edit_rename(edit, object_id, new_name);
    if (rc != NMO_OK) {
        nmo_workspace_edit_rollback(edit);
        return rc;
    }

    return nmo_workspace_edit_commit(edit);
}


/**
 * Batch rename mode.
 *
 * Options already parsed by caller:
 *   name_pattern, to_template, use_regex, class_filter, dry_run, output_path
 *   file_path (last positional arg)
 */
typedef struct object_rename_batch_args {
    const char *name_pattern;
    const char *to_template;
    bool use_regex;
    const char *class_filter;
    rename_entry_t *entries;
    size_t entry_count;
    size_t collision_count;
    size_t rename_errors;
} object_rename_batch_args_t;

static int object_rename_batch_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    object_rename_batch_args_t *args = (object_rename_batch_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_object_query_t class_query = {0};
    nmo_core_query_build_options_t query_opts = {
        .class_name = args->class_filter,
        .include_derived_classes = true,
    };
    int rc = nmo_core_query_build(c, &class_query, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    const nmo_object_query_t *filter_query =
        args->class_filter != NULL ? &class_query : NULL;

    nmo_object_repository_t *repo = nmo_tool_owner_repository(c->workspace);
    rename_collect_data_t rename_data = {
        .repo = repo,
        .filter_query = filter_query,
        .name_pattern = args->name_pattern,
        .to_template = args->to_template,
        .use_regex = args->use_regex,
    };
    rc = nmo_core_object_query_run(c, NULL, collect_rename_entry,
                                   &rename_data, NULL);
    if (rc != NMO_CLI_EXIT_SUCCESS || rename_data.oom) {
        fprintf(stderr, "Error: Out of memory\n");
        rename_entries_free(rename_data.entries, rename_data.count);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    args->entries = rename_data.entries;
    args->entry_count = rename_data.count;
    args->collision_count = rename_data.collision_count;

    if (!dry_run) {
        for (size_t i = 0; i < args->entry_count; i++) {
            nmo_status_t rrc = nmo_cmd_object_rename_with_edit(
                c, args->entries[i].id, args->entries[i].new_name);
            if (rrc != NMO_OK) {
                fprintf(stderr, "Error: Failed to rename object %u: %s\n",
                        args->entries[i].id, nmo_error_string(rrc));
                args->rename_errors++;
            }
        }

        if (args->rename_errors > 0) {
            fprintf(stderr, "Warning: %zu rename(s) failed\n", args->rename_errors);
        }
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static bool object_rename_batch_should_save(
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)dry_run;
    (void)output_path;
    object_rename_batch_args_t *args = (object_rename_batch_args_t *)user_data;
    return args != NULL && args->entry_count > args->rename_errors;
}

static int object_rename_batch_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    object_rename_batch_args_t *args = (object_rename_batch_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    static const nmo_cli_table_col_t cols[] = {
        {"ID",       NMO_CLI_ALIGN_RIGHT, 5,  0},
        {"CLASS",    NMO_CLI_ALIGN_LEFT,  15, 25},
        {"OLD NAME", NMO_CLI_ALIGN_LEFT,  20, 40},
        {"NEW NAME", NMO_CLI_ALIGN_LEFT,  20, 40},
    };
    const bool table = dry_run && args->entry_count > 0;
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_bool(rec, "dry_run", NULL, dry_run) &&
              nmo_cli_record_uint(rec, "match_count", NULL, (uint64_t)args->entry_count) &&
              nmo_cli_record_uint(rec, "collision_count", NULL,
                                  (uint64_t)args->collision_count);
    if (ok && dry_run) {
        ok = nmo_cli_record_raw(rec, "=== Dry Run: Batch Rename ===\n\n");
    }
    nmo_cli_record_array_t *arr = ok ? nmo_cli_record_array(rec, "renames", NULL) : NULL;
    ok = arr != NULL && (!table || nmo_cli_record_array_set_table(arr, cols, 4));
    for (size_t i = 0; ok && i < args->entry_count; i++) {
        const rename_entry_t *entry = &args->entries[i];
        const char *cls = nmo_core_class_name(c, entry->class_id);
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = item != NULL &&
             nmo_cli_record_uint(item, "id", "ID", (uint64_t)entry->id) &&
             nmo_cli_record_str(item, "class_name", "CLASS", cls ? cls : "?") &&
             nmo_cli_record_str(item, "old_name", "OLD NAME", entry->old_name) &&
             nmo_cli_record_str(item, "new_name", "NEW NAME", entry->new_name);
        if (ok && entry->collision) {
            ok = nmo_cli_record_bool(item, "collision", NULL, true);
        }
        ok = nmo_cli_record_array_add(ok ? arr : NULL, item) && ok;
    }
    if (ok && table) {
        ok = nmo_cli_record_raw(rec, "\n");
    }
    if (ok && dry_run) {
        ok = nmo_cli_record_raw_fmt(rec, "%zu object(s) would be renamed, %zu collisions\n",
                                    args->entry_count, args->collision_count);
    } else if (ok) {
        ok = nmo_cli_record_raw_fmt(rec, "%zu object(s) renamed, %zu collisions\n",
                                    args->entry_count, args->collision_count);
        if (ok && output_path) {
            ok = nmo_cli_record_str(rec, "output", NULL, output_path);
            if (ok && args->entry_count > 0) {
                ok = nmo_cli_record_raw_fmt(rec, "Saved to: %s\n", output_path);
            }
        }
    }
    int rc = object_write_emit(c, rec, ok, "object.rename");

    if (!c->is_json) {
        for (size_t i = 0; i < args->entry_count; i++) {
            if (args->entries[i].collision) {
                fprintf(stderr, "Warning: Name '%s' collides with existing object\n",
                        args->entries[i].new_name);
            }
        }
    }

    return args->rename_errors > 0 ? NMO_CLI_EXIT_INTERNAL_ERROR : rc;
}

static int nmo_cmd_object_rename_batch(
    const char *name_pattern,
    const char *to_template,
    bool use_regex,
    const char *class_filter,
    bool dry_run,
    const char *output_path,
    const char *file_path,
    const nmo_cli_global_opts_t *global)
{
    /* Validate required args */
    if (!to_template) {
        fprintf(stderr, "Error: --to is required for batch rename\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!dry_run && !output_path) {
        fprintf(stderr, "Error: -o/--output is required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!file_path) {
        fprintf(stderr, "Error: No input file specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    object_rename_batch_args_t args = {
        .name_pattern = name_pattern,
        .to_template = to_template,
        .use_regex = use_regex,
        .class_filter = class_filter,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "object.rename",
        .output_required_unless_dry_run = true,
        .should_save = object_rename_batch_should_save,
    };
    int rc = nmo_cli_run_write_command(
        file_path,
        output_path,
        dry_run,
        global,
        &spec,
        object_rename_batch_mutate,
        object_rename_batch_report,
        &args);
    rename_entries_free(args.entries, args.entry_count);
    return rc;
}

/* ============================================================================
 * object rename - Rename an object and save to new file
 *
 * Two modes:
 *   Single:  nmo object rename <id> <new_name> <file> -o <output>
 *   Batch:   nmo object rename --name <pattern> --to <template> [opts] <file> -o <output>
 * ============================================================================ */

typedef struct object_rename_single_args {
    uint32_t object_id;
    const char *new_name;
    char *old_name; /**< malloc'd copy taken before the rename, NULL when unnamed */
    bool name_collision;
} object_rename_single_args_t;

static int object_rename_single_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)dry_run;
    (void)output_path;
    object_rename_single_args_t *args = (object_rename_single_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_object_repository_t *repo = nmo_tool_owner_repository(c->workspace);
    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, args->object_id);
    if (!obj) {
        fprintf(stderr, "Error: Object %u not found\n", args->object_id);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *old_name = nmo_object_get_name(obj);
    free(args->old_name);
    args->old_name = (old_name && old_name[0]) ? nmo_tool_strdup(old_name) : NULL;

    nmo_object_t *existing = nmo_object_repository_find_by_name(repo, args->new_name);
    args->name_collision =
        existing && nmo_object_get_id(existing) != args->object_id;
    if (args->name_collision && !c->is_json) {
        fprintf(stderr, "Warning: Name '%s' already used by object %u\n",
                args->new_name, nmo_object_get_id(existing));
    }

    if (!dry_run) {
        int rename_rc = nmo_cmd_object_rename_with_edit(c, args->object_id, args->new_name);
        if (rename_rc != NMO_OK) {
            fprintf(stderr, "Error: Failed to rename object %u: %s\n",
                    args->object_id, nmo_error_string(rename_rc));
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static int object_rename_single_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    object_rename_single_args_t *args = (object_rename_single_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "id", NULL, (uint64_t)args->object_id) &&
              nmo_cli_record_str(rec, "old_name", NULL,
                                 args->old_name ? args->old_name : "") &&
              nmo_cli_record_str(rec, "new_name", NULL, args->new_name) &&
              nmo_cli_record_bool(rec, "dry_run", NULL, dry_run) &&
              nmo_cli_record_raw_fmt(rec, "%sRenamed: %s -> %s (ID %u)\n",
                                     dry_run ? "[dry-run] " : "",
                                     args->old_name ? args->old_name : "(unnamed)",
                                     args->new_name, args->object_id) &&
              object_write_add_output(rec, dry_run, output_path) &&
              nmo_cli_record_bool(rec, "name_collision", NULL, args->name_collision);
    if (ok && args->name_collision) {
        ok = nmo_cli_record_raw(rec, "Warning: Name collision with existing object\n");
    }
    return object_write_emit(c, rec, ok, "object.rename");
}

int nmo_cmd_object_rename(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    /* Unified option parsing for both single and batch modes */
    static const nmo_opt_def_t opts[] = {
        {"--name",    "-n", NMO_OPT_STRING, "Glob/regex pattern to match object names (batch mode)"},
        {"--to",      NULL, NMO_OPT_STRING, "Rename template with {0},{1}..{N} placeholders"},
        {"--regex",   NULL, NMO_OPT_FLAG,   "Treat --name as POSIX regex instead of glob"},
        {"--class",   "-c", NMO_OPT_STRING, "Restrict to objects of this class"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview renames without saving"},
        NMO_OPT_DEF_OUTPUT,
    };
    enum { OPT_NAME, OPT_TO, OPT_REGEX, OPT_CLASS, OPT_DRY_RUN, OPT_OUTPUT, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    /* Batch mode: --name is present */
    if (vals[OPT_NAME].present) {
        const char *file_path = r.pos_count > 0
            ? r.pos_args[r.pos_count - 1] : NULL;
        return nmo_cmd_object_rename_batch(
            vals[OPT_NAME].val.str,
            nmo_opt_str(&vals[OPT_TO]),
            nmo_opt_flag(&vals[OPT_REGEX]),
            nmo_opt_str(&vals[OPT_CLASS]),
            nmo_opt_flag(&vals[OPT_DRY_RUN]),
            nmo_opt_str(&vals[OPT_OUTPUT]),
            file_path,
            global);
    }

    /* Single mode: <id> <new_name> <file> -o <output> */
    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    if (!output_path) {
        fprintf(stderr, "Error: Output file not specified (use -o or --output)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (r.pos_count < 3) {
        fprintf(stderr, "Error: Expected <id> <new_name> <file>\n");
        fprintf(stderr, "Usage: nmo object rename <id> <new_name> <file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *id_str = r.pos_args[0];
    const char *new_name = r.pos_args[1];
    const char *file_path = r.pos_args[r.pos_count - 1];

    /* Parse object ID */
    uint32_t object_id;
    if (!nmo_tool_parse_u32(id_str, &object_id)) {
        fprintf(stderr, "Error: Invalid object ID '%s'\n", id_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    object_rename_single_args_t args = {
        .object_id = object_id,
        .new_name = new_name,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "object.rename",
        .output_required_unless_dry_run = true,
    };
    int rc = nmo_cli_run_write_command(
        file_path,
        output_path,
        false,
        global,
        &spec,
        object_rename_single_mutate,
        object_rename_single_report,
        &args);
    free(args.old_name);
    return rc;
}

int nmo_cmd_object_rename_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv,
                                     nmo_cmd_in_session_result_t *result)
{
    static const nmo_opt_def_t opts[] = {
        {"--name",    "-n", NMO_OPT_STRING, "Glob/regex pattern to match object names (batch mode)"},
        {"--to",      NULL, NMO_OPT_STRING, "Rename template with {0},{1}..{N} placeholders"},
        {"--regex",   NULL, NMO_OPT_FLAG,   "Treat --name as POSIX regex instead of glob"},
        {"--class",   "-c", NMO_OPT_STRING, "Restrict to objects of this class"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview renames without saving"},
        NMO_OPT_DEF_OUTPUT,
    };
    enum { OPT_NAME, OPT_TO, OPT_REGEX, OPT_CLASS, OPT_DRY_RUN, OPT_OUTPUT, OPT_COUNT };

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (reject_in_session_output_option(vals[OPT_OUTPUT].present) != NMO_CLI_EXIT_SUCCESS) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    bool dry_run = nmo_opt_flag(&vals[OPT_DRY_RUN]);
    if (result != NULL) {
        result->dry_run = dry_run;
    }

    if (vals[OPT_NAME].present) {
        if (r.pos_count != 0) {
            fprintf(stderr, "Error: Unexpected positional argument for object rename batch mode\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (!vals[OPT_TO].present) {
            fprintf(stderr, "Error: --to is required for batch rename\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }

        object_rename_batch_args_t args = {
            .name_pattern = vals[OPT_NAME].val.str,
            .to_template = vals[OPT_TO].val.str,
            .use_regex = nmo_opt_flag(&vals[OPT_REGEX]),
            .class_filter = nmo_opt_str(&vals[OPT_CLASS]),
        };
        int rc = object_rename_batch_mutate(ctx, dry_run, NULL, &args);
        if (rc == NMO_CLI_EXIT_SUCCESS) {
            rc = object_rename_batch_report(ctx, dry_run, NULL, &args);
        }
        if (result != NULL && rc == NMO_CLI_EXIT_SUCCESS && !dry_run) {
            result->changed = args.entry_count > args.rename_errors;
        }
        rename_entries_free(args.entries, args.entry_count);
        return rc;
    }

    if (r.pos_count != 2) {
        fprintf(stderr, "Usage: object rename <id> <new_name>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t object_id = 0;
    if (!nmo_tool_parse_u32(r.pos_args[0], &object_id)) {
        fprintf(stderr, "Error: Invalid object ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    object_rename_single_args_t args = {
        .object_id = object_id,
        .new_name = r.pos_args[1],
    };
    int rc = object_rename_single_mutate(ctx, dry_run, NULL, &args);
    if (rc == NMO_CLI_EXIT_SUCCESS) {
        rc = object_rename_single_report(ctx, dry_run, NULL, &args);
    }
    if (result != NULL && rc == NMO_CLI_EXIT_SUCCESS && !dry_run) {
        result->changed = true;
    }
    free(args.old_name);
    return rc;
}

/* ============================================================================
 * object delete - Delete objects with filter support and cascade preview
 *
 *   nmo object delete <id>[,<id>,...] <file> -o <output>
 *   nmo object delete --class <cls> [--name <pat>] <file> -o <output>
 *   nmo object delete --cascade --dry-run <id> <file>
 * ============================================================================ */

/** Collect matching object IDs into a dynamic array */
typedef struct {
    nmo_object_id_t *ids;
    size_t count;
    size_t capacity;
} delete_id_collector_t;

static int delete_collect_visitor(size_t index, nmo_object_t *obj,
                                   const nmo_cmd_ctx_t *c, void *user)
{
    (void)index; (void)c;
    delete_id_collector_t *col = (delete_id_collector_t *)user;
    if (col->count >= col->capacity) {
        size_t new_cap = col->capacity ? col->capacity * 2 : 64;
        nmo_object_id_t *tmp = (nmo_object_id_t *)realloc(
            col->ids, new_cap * sizeof(nmo_object_id_t));
        if (!tmp) return -1;
        col->ids = tmp;
        col->capacity = new_cap;
    }
    col->ids[col->count++] = nmo_object_get_id(obj);
    return 0;
}

/* Batch delete context -- filter strings point into argv */
typedef struct {
    const char *class_str;
    const char *name_str;
    bool has_class;
    bool has_name;
    bool cascade;
    bool strict;
} delete_batch_ctx_t;

static int delete_batch_handler(
    const char *input_path,
    const char *output_path,
    const nmo_cli_global_opts_t *global,
    void *user_data,
    struct yyjson_mut_doc *doc,
    struct yyjson_mut_val *result_data)
{
    delete_batch_ctx_t *ctx = NULL;
    if (doc && result_data) {
        ctx = (delete_batch_ctx_t *)user_data;
    } else {
        const nmo_tool_text_output_ctx_t *text_ctx =
            (const nmo_tool_text_output_ctx_t *)user_data;
        ctx = text_ctx ? (delete_batch_ctx_t *)text_ctx->user_data : NULL;
    }
    if (!ctx) {
        fprintf(stderr, "Error: Invalid batch delete context\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cli_write_init_ctx(&c, input_path, global);
    if (rc) return rc;

    /* Build query */
    nmo_object_query_t query;
    nmo_core_query_build_options_t query_opts = {
        .class_name = ctx->has_class ? ctx->class_str : NULL,
        .name_wildcard = ctx->has_name ? ctx->name_str : NULL,
        .include_derived_classes = true,
    };
    rc = nmo_core_query_build(&c, &query, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&c, rc);
    }

    /* Collect matching IDs */
    delete_id_collector_t col = {0};
    nmo_core_iter_result_t iter_result = {0};
    rc = nmo_core_object_query_run(&c, &query, delete_collect_visitor, &col, &iter_result);

    if (rc < 0) {
        free(col.ids);
        fprintf(stderr, "Error: Failed to iterate objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    if (col.count == 0) {
        fprintf(stderr, "  No objects matched in %s\n", input_path);
        free(col.ids);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
    }

    /* Delete */
    uint32_t flags = ctx->cascade ? NMO_RUNTIME_REQUEST_CASCADE : NMO_RUNTIME_REQUEST_SAFE_DETACH;
    if (ctx->strict) flags |= NMO_RUNTIME_REQUEST_STRICT;

    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));
    int del_rc = nmo_tool_owner_destroy_objects(c.workspace, col.ids, col.count, flags, &report);
    free(col.ids);
    if (del_rc != NMO_OK) {
        fprintf(stderr, "Error: Deletion failed: %s\n", nmo_error_string(del_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Save */
    nmo_save_options_t save_opts = nmo_tool_owner_save_options_default();
    int save_rc = nmo_cli_save_document(c.document, output_path, &save_opts);
    if (save_rc != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&c, save_rc);
    }

    fprintf(stderr, "  Deleted %zu object(s) -> %s\n", report.deleted_objects, output_path);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

typedef struct object_delete_preview_entry {
    nmo_object_id_t id;
    bool has_object;
    char *class_name; /**< malloc'd; "-" when the object is missing */
    char *name;       /**< malloc'd, NULL when unnamed */
} object_delete_preview_entry_t;

typedef struct object_delete_args {
    const char *class_name;
    const char *name_wildcard;
    const char **id_args;
    size_t id_arg_count;
    bool use_filter;
    bool cascade;
    bool strict;
    nmo_object_id_t *target_ids;
    size_t target_count;
    object_delete_preview_entry_t *preview_entries;
    size_t expanded_count;
    size_t total_size;
    nmo_runtime_report_t report;
} object_delete_args_t;

static void object_delete_args_cleanup(object_delete_args_t *args)
{
    if (args == NULL) {
        return;
    }
    free(args->target_ids);
    args->target_ids = NULL;
    args->target_count = 0;

    if (args->preview_entries != NULL) {
        for (size_t i = 0; i < args->expanded_count; i++) {
            free(args->preview_entries[i].name);
            free(args->preview_entries[i].class_name);
        }
        free(args->preview_entries);
        args->preview_entries = NULL;
    }
    args->expanded_count = 0;
}

static int object_delete_collect_targets(nmo_cmd_ctx_t *c, object_delete_args_t *args)
{
    if (args->use_filter) {
        nmo_object_query_t query;
        nmo_core_query_build_options_t query_opts = {
            .class_name = args->class_name,
            .name_wildcard = args->name_wildcard,
            .include_derived_classes = true,
        };
        int rc = nmo_core_query_build(c, &query, &query_opts);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return rc;
        }

        delete_id_collector_t col = {0};
        nmo_core_iter_result_t iter_result = {0};
        rc = nmo_core_object_query_run(c, &query, delete_collect_visitor, &col, &iter_result);

        if (rc < 0) {
            free(col.ids);
            fprintf(stderr, "Error: Failed to iterate objects\n");
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        args->target_ids = col.ids;
        args->target_count = col.count;
        return NMO_CLI_EXIT_SUCCESS;
    }

    size_t max_ids = args->id_arg_count > 0 ? args->id_arg_count * 16 : 16;
    args->target_ids = (nmo_object_id_t *)malloc(max_ids * sizeof(nmo_object_id_t));
    if (args->target_ids == NULL) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    for (size_t i = 0; i < args->id_arg_count; i++) {
        const char *s = args->id_args[i];
        while (*s) {
            const char *comma = strchr(s, ',');
            size_t tok_len = comma ? (size_t)(comma - s) : strlen(s);
            char *tok = nmo_tool_strdup_fmt("%.*s", (int)tok_len, s);
            if (tok == NULL) {
                fprintf(stderr, "Error: Out of memory\n");
                return NMO_CLI_EXIT_INTERNAL_ERROR;
            }

            uint32_t id;
            bool valid = nmo_tool_parse_u32(tok, &id) && id != 0;
            if (!valid) {
                fprintf(stderr, "Error: Invalid object ID '%s'\n", tok);
                free(tok);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            free(tok);

            if (args->target_count >= max_ids) {
                max_ids *= 2;
                nmo_object_id_t *tmp = (nmo_object_id_t *)realloc(
                    args->target_ids, max_ids * sizeof(nmo_object_id_t));
                if (tmp == NULL) {
                    return NMO_CLI_EXIT_INTERNAL_ERROR;
                }
                args->target_ids = tmp;
            }
            args->target_ids[args->target_count++] = (nmo_object_id_t)id;

            if (comma) {
                s = comma + 1;
            } else {
                break;
            }
        }
    }

    if (args->target_count == 0) {
        fprintf(stderr, "Error: No valid object IDs specified\n");
        fprintf(stderr, "Usage: nmo object delete <id>[,<id>,...] <file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (args->strict) {
        for (size_t i = 0; i < args->target_count; i++) {
            if (!nmo_core_find_by_id(c, args->target_ids[i])) {
                fprintf(stderr, "Error: Object %u not found (--strict)\n", args->target_ids[i]);
                return NMO_CLI_EXIT_NOT_FOUND;
            }
        }
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static int object_delete_collect_preview(
    nmo_cmd_ctx_t *c,
    object_delete_args_t *args,
    uint32_t flags)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (arena == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_id_t *expanded_ids = args->target_ids;
    size_t expanded_count = args->target_count;

    if (args->cascade) {
        int prev_rc = nmo_tool_owner_preview_destroy(
            c->workspace,
            args->target_ids,
            args->target_count,
            flags,
            arena,
            &expanded_ids,
            &expanded_count);
        if (prev_rc != NMO_OK) {
            expanded_ids = args->target_ids;
            expanded_count = args->target_count;
        }
    }

    args->preview_entries = (object_delete_preview_entry_t *)calloc(
        expanded_count, sizeof(object_delete_preview_entry_t));
    if (expanded_count > 0 && args->preview_entries == NULL) {
        nmo_arena_destroy(arena);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    args->expanded_count = expanded_count;
    args->total_size = 0;

    for (size_t i = 0; i < expanded_count; i++) {
        object_delete_preview_entry_t *entry = &args->preview_entries[i];
        entry->id = expanded_ids[i];

        nmo_object_t *obj = nmo_core_find_by_id(c, expanded_ids[i]);
        entry->class_name = obj != NULL
            ? nmo_core_class_name_dup(c, nmo_object_get_class_id(obj))
            : nmo_tool_strdup("-");
        if (entry->class_name == NULL) {
            nmo_arena_destroy(arena);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        if (obj == NULL) {
            continue;
        }

        entry->has_object = true;

        const char *name = nmo_object_get_name(obj);
        if (name != NULL && name[0] != '\0') {
            entry->name = nmo_tool_strdup(name);
            if (entry->name == NULL) {
                nmo_arena_destroy(arena);
                return NMO_CLI_EXIT_INTERNAL_ERROR;
            }
        }

        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        if (chunk != NULL) {
            args->total_size += nmo_chunk_get_data_size(chunk);
        }
    }

    nmo_arena_destroy(arena);
    return NMO_CLI_EXIT_SUCCESS;
}

static int object_delete_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    object_delete_args_t *args = (object_delete_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    int rc = object_delete_collect_targets(c, args);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    if (args->target_count == 0) {
        return NMO_CLI_EXIT_SUCCESS;
    }

    uint32_t flags = args->cascade ? NMO_RUNTIME_REQUEST_CASCADE
                                   : NMO_RUNTIME_REQUEST_SAFE_DETACH;
    if (args->strict) {
        flags |= NMO_RUNTIME_REQUEST_STRICT;
    }

    rc = object_delete_collect_preview(c, args, flags);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    if (dry_run) {
        return NMO_CLI_EXIT_SUCCESS;
    }

    memset(&args->report, 0, sizeof(args->report));
    int del_rc = nmo_tool_owner_destroy_objects(
        c->workspace,
        args->target_ids,
        args->target_count,
        flags,
        &args->report);
    if (del_rc != NMO_OK) {
        fprintf(stderr, "Error: Deletion failed: %s\n", nmo_error_string(del_rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static bool object_delete_should_save(
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)dry_run;
    (void)output_path;
    object_delete_args_t *args = (object_delete_args_t *)user_data;
    return args != NULL && args->target_count > 0;
}

static int object_delete_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    object_delete_args_t *args = (object_delete_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (args->target_count == 0) {
        return object_write_emit_no_match(c, "object.delete");
    }

    static const nmo_cli_table_col_t cols[] = {
        {"ID",    NMO_CLI_ALIGN_RIGHT, 6, 0},
        {"Class", NMO_CLI_ALIGN_LEFT, 18, 0},
        {"Name",  NMO_CLI_ALIGN_LEFT, 30, 0},
    };
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_bool(rec, "dry_run", NULL, dry_run) &&
              nmo_cli_record_str(rec, "mode", NULL, args->cascade ? "cascade" : "safe_detach") &&
              nmo_cli_record_uint(rec, "requested_count", NULL, (uint64_t)args->target_count) &&
              nmo_cli_record_uint(rec, "expanded_count", NULL, (uint64_t)args->expanded_count) &&
              nmo_cli_record_uint(rec, "total_size", NULL, (uint64_t)args->total_size);
    if (ok && dry_run) {
        ok = nmo_cli_record_raw_fmt(rec, "=== Dry Run: Delete (%s mode) ===\n\n",
                                    args->cascade ? "cascade" : "safe-detach");
    }

    /* Rows: ID, Class ("-" when missing), Name ("-" when unnamed). */
    nmo_cli_record_array_t *arr = ok ? nmo_cli_record_array(rec, "objects", NULL) : NULL;
    ok = arr != NULL &&
         (args->expanded_count == 0 ||
          nmo_cli_record_array_set_table(arr, cols, sizeof(cols) / sizeof(cols[0])));
    for (size_t i = 0; ok && i < args->expanded_count; i++) {
        const object_delete_preview_entry_t *entry = &args->preview_entries[i];
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = item != NULL && nmo_cli_record_uint(item, "id", "ID", (uint64_t)entry->id);
        if (ok && entry->has_object && entry->name != NULL && entry->name[0] != '\0') {
            ok = nmo_cli_record_str(item, "name", NULL, entry->name);
        }
        if (ok && entry->has_object) {
            ok = nmo_cli_record_str(item, "class_name", "Class", entry->class_name);
        } else if (ok) {
            ok = nmo_cli_record_text(item, "Class", entry->class_name);
        }
        ok = ok && nmo_cli_record_text(item, "Name", entry->name != NULL ? entry->name : "-");
        ok = nmo_cli_record_array_add(ok ? arr : NULL, item) && ok;
    }

    if (ok && args->cascade && args->expanded_count > args->target_count) {
        ok = nmo_cli_record_raw_fmt(rec, "\nRequested: %zu, cascade expanded to: %zu object(s)\n",
                                    args->target_count, args->expanded_count);
    } else if (ok) {
        ok = nmo_cli_record_raw_fmt(rec, "\n%zu object(s)", args->expanded_count);
    }
    ok = ok && nmo_cli_record_raw_fmt(rec, " (%zu bytes)\n", args->total_size);
    if (ok && !dry_run && output_path != NULL) {
        ok = nmo_cli_record_str(rec, "output", NULL, output_path) &&
             nmo_cli_record_raw_fmt(rec, "Deleted %zu object(s), saved to: %s\n",
                                    args->report.deleted_objects, output_path);
    } else if (ok && !dry_run) {
        ok = nmo_cli_record_raw_fmt(rec, "Deleted %zu object(s)\n",
                                    args->report.deleted_objects);
    }
    return object_write_emit(c, rec, ok, "object.delete");
}

int nmo_cmd_object_delete(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_WRITE_OUTPUT,
        {"--class",   "-c", NMO_OPT_STRING, "Filter by class (includes derived)"},
        {"--name",    "-n", NMO_OPT_STRING, "Filter by name wildcard pattern"},
        {"--cascade", NULL, NMO_OPT_FLAG,   "Delete dependents (default: safe-detach)"},
        NMO_OPT_DEF_DRY_RUN,
        {"--strict",  NULL, NMO_OPT_FLAG,   "Fail if any ID not found"},
    };
    enum { OPT_OUTPUT, OPT_CLASS, OPT_NAME, OPT_CASCADE,
           OPT_DRYRUN, OPT_STRICT, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool cascade  = nmo_opt_flag(&vals[OPT_CASCADE]);
    bool dry_run  = nmo_opt_flag(&vals[OPT_DRYRUN]);
    bool strict   = nmo_opt_flag(&vals[OPT_STRICT]);
    bool use_filter = vals[OPT_CLASS].present || vals[OPT_NAME].present;

    if (!dry_run && !output_path) {
        fprintf(stderr, "Error: -o/--output is required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Batch mode: all positional args are files (filter mode only) */
    if (global->batch_mode && use_filter && r.pos_count > 0 && output_path) {
        delete_batch_ctx_t batch_ctx;
        memset(&batch_ctx, 0, sizeof(batch_ctx));
        if (vals[OPT_CLASS].present) {
            batch_ctx.class_str = vals[OPT_CLASS].val.str;
            batch_ctx.has_class = true;
        }
        if (vals[OPT_NAME].present) {
            batch_ctx.name_str = vals[OPT_NAME].val.str;
            batch_ctx.has_name = true;
        }
        batch_ctx.cascade = cascade;
        batch_ctx.strict = strict;
        return nmo_tool_batch_write_run(
            r.pos_args, r.pos_count, output_path, global,
            "object.delete", delete_batch_handler, &batch_ctx);
    }

    /* Determine input file */
    const char *file_path = r.pos_count > 0 ? r.pos_args[r.pos_count - 1] : NULL;
    if (!file_path) {
        fprintf(stderr, "Error: No input file specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    object_delete_args_t args = {
        .class_name = nmo_opt_str(&vals[OPT_CLASS]),
        .name_wildcard = nmo_opt_str(&vals[OPT_NAME]),
        .id_args = r.pos_args,
        .id_arg_count = r.pos_count > 0 ? r.pos_count - 1 : 0,
        .use_filter = use_filter,
        .cascade = cascade,
        .strict = strict,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "object.delete",
        .output_required_unless_dry_run = true,
        .should_save = object_delete_should_save,
    };
    int rc = nmo_cli_run_write_command(
        file_path,
        output_path,
        dry_run,
        global,
        &spec,
        object_delete_mutate,
        object_delete_report,
        &args);
    object_delete_args_cleanup(&args);
    return rc;
}

int nmo_cmd_object_delete_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv,
                                     nmo_cmd_in_session_result_t *result)
{
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_WRITE_OUTPUT,
        {"--class",   "-c", NMO_OPT_STRING, "Filter by class (includes derived)"},
        {"--name",    "-n", NMO_OPT_STRING, "Filter by name wildcard pattern"},
        {"--cascade", NULL, NMO_OPT_FLAG,   "Delete dependents (default: safe-detach)"},
        NMO_OPT_DEF_DRY_RUN,
        {"--strict",  NULL, NMO_OPT_FLAG,   "Fail if any ID not found"},
    };
    enum { OPT_OUTPUT, OPT_CLASS, OPT_NAME, OPT_CASCADE,
           OPT_DRYRUN, OPT_STRICT, OPT_COUNT };

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (reject_in_session_output_option(vals[OPT_OUTPUT].present) != NMO_CLI_EXIT_SUCCESS) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    bool use_filter = vals[OPT_CLASS].present || vals[OPT_NAME].present;
    if (!use_filter && r.pos_count == 0) {
        fprintf(stderr, "Usage: object delete [options] <id>[,<id>,...]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (result != NULL) {
        result->dry_run = dry_run;
    }

    object_delete_args_t args = {
        .class_name = nmo_opt_str(&vals[OPT_CLASS]),
        .name_wildcard = nmo_opt_str(&vals[OPT_NAME]),
        .id_args = r.pos_args,
        .id_arg_count = r.pos_count,
        .use_filter = use_filter,
        .cascade = nmo_opt_flag(&vals[OPT_CASCADE]),
        .strict = nmo_opt_flag(&vals[OPT_STRICT]),
    };
    int rc = object_delete_mutate(ctx, dry_run, NULL, &args);
    if (rc == NMO_CLI_EXIT_SUCCESS) {
        rc = object_delete_report(ctx, dry_run, NULL, &args);
    }
    if (result != NULL && rc == NMO_CLI_EXIT_SUCCESS && !dry_run) {
        result->changed = args.report.deleted_objects > 0;
    }
    object_delete_args_cleanup(&args);
    return rc;
}

/* ============================================================================
 * object create - Create a new object and save to file
 *
 *   nmo object create --class <name> [--name <name>] [--type-guid <guid>] <file> -o <output>
 * ============================================================================ */

typedef struct object_create_args {
    const char *class_str;
    const char *name;
    nmo_guid_t type_guid;
    nmo_class_id_t class_id;
    nmo_object_id_t new_id;
} object_create_args_t;

static int object_create_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)dry_run;
    (void)output_path;
    object_create_args_t *args = (object_create_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    args->class_id = nmo_core_class_id(c, args->class_str);
    if (!args->class_id) {
        fprintf(stderr, "Error: Unknown class '%s'\n", args->class_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_workspace_edit_t *edit = NULL;
    nmo_status_t create_rc =
        nmo_workspace_edit_begin(c->workspace, "object.create", &edit);
    if (create_rc == NMO_OK) {
        const nmo_object_create_desc_t desc = {
            .class_id = args->class_id,
            .name = args->name,
            .type_guid = args->type_guid,
        };
        create_rc = nmo_object_edit_create(edit, &desc, &args->new_id);
    }
    if (create_rc != NMO_OK) {
        nmo_workspace_edit_rollback(edit);
        fprintf(stderr, "Error: Failed to create object: %s\n",
                nmo_error_string(create_rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    create_rc = nmo_workspace_edit_commit(edit);
    if (create_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to commit object creation: %s\n",
                nmo_error_string(create_rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static int object_create_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    object_create_args_t *args = (object_create_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *cls = nmo_core_class_name(c, args->class_id);
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_bool(rec, "dry_run", NULL, dry_run) &&
              nmo_cli_record_uint(rec, "id", NULL, (uint64_t)args->new_id) &&
              nmo_cli_record_str(rec, "class_name", NULL, cls ? cls : args->class_str) &&
              nmo_cli_record_str(rec, "name", NULL, args->name ? args->name : "") &&
              nmo_cli_record_raw_fmt(rec, "%sCreated object #%u (%s)",
                                     dry_run ? "[dry-run] " : "",
                                     args->new_id, cls ? cls : args->class_str);
    if (ok && args->name && args->name[0]) {
        ok = nmo_cli_record_raw_fmt(rec, " [name: %s]", args->name);
    }
    ok = ok && nmo_cli_record_raw(rec, "\n") &&
         object_write_add_output(rec, dry_run, output_path);
    return object_write_emit(c, rec, ok, "object.create");
}

/*
 * Parse "<hex d1>,<hex d2>" (comma points at the separator inside guid_str).
 * Returns false when either half is empty or not a 32-bit hex value.
 */
static bool parse_guid_hex_pair(const char *guid_str, const char *comma, nmo_guid_t *out_guid)
{
    size_t left_len = (size_t)(comma - guid_str);
    if (left_len == 0 || comma[1] == '\0') {
        return false;
    }
    char *left = nmo_tool_strdup_fmt("%.*s", (int)left_len, guid_str);
    if (left == NULL) {
        return false;
    }
    bool ok = nmo_parse_u32_range_base(left, 16, 0, UINT32_MAX, &out_guid->d1) == NMO_OK &&
              nmo_parse_u32_range_base(comma + 1, 16, 0, UINT32_MAX, &out_guid->d2) == NMO_OK;
    free(left);
    return ok;
}

static int parse_object_type_guid_arg(const char *guid_str, nmo_guid_t *out_guid)
{
    if (out_guid == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    *out_guid = NMO_GUID_NULL;
    if (guid_str == NULL) {
        return NMO_CLI_EXIT_SUCCESS;
    }

    const char *comma = strchr(guid_str, ',');
    if (comma != NULL) {
        if (!parse_guid_hex_pair(guid_str, comma, out_guid)) {
            fprintf(stderr, "Error: Invalid GUID '%s'\n", guid_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        return NMO_CLI_EXIT_SUCCESS;
    }

    *out_guid = nmo_guid_parse(guid_str);
    if (nmo_guid_is_null(*out_guid)) {
        fprintf(stderr, "Error: Invalid GUID '%s'\n", guid_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_object_create(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_WRITE_OUTPUT,
        {"--class",     "-c", NMO_OPT_STRING, "Class name (required)"},
        {"--name",      "-n", NMO_OPT_STRING, "Object name"},
        {"--type-guid", NULL, NMO_OPT_STRING, "Type GUID (d1,d2 format)"},
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_CLASS, OPT_NAME, OPT_TYPE_GUID, OPT_DRYRUN, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    const char *class_str   = nmo_opt_str(&vals[OPT_CLASS]);
    const char *name        = nmo_opt_str(&vals[OPT_NAME]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);

    if (!dry_run && !output_path) {
        fprintf(stderr, "Error: -o/--output is required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!class_str) {
        fprintf(stderr, "Error: --class/-c is required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_count > 0 ? r.pos_args[r.pos_count - 1] : NULL;
    if (!file_path) {
        fprintf(stderr, "Error: No input file specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Parse type GUID if provided */
    nmo_guid_t type_guid = NMO_GUID_NULL;
    if (vals[OPT_TYPE_GUID].present) {
        const char *guid_str = vals[OPT_TYPE_GUID].val.str;
        const char *comma = strchr(guid_str, ',');
        if (comma != NULL) {
            if (!parse_guid_hex_pair(guid_str, comma, &type_guid)) {
                fprintf(stderr, "Error: Invalid GUID '%s'\n", guid_str);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
        } else {
            type_guid = nmo_guid_parse(guid_str);
            if (nmo_guid_is_null(type_guid)) {
                fprintf(stderr, "Error: Invalid GUID '%s'\n", guid_str);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
        }
    }

    object_create_args_t args = {
        .class_str = class_str,
        .name = name,
        .type_guid = type_guid,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "object.create",
        .output_required_unless_dry_run = true,
    };
    return nmo_cli_run_write_command(
        file_path,
        output_path,
        dry_run,
        global,
        &spec,
        object_create_mutate,
        object_create_report,
        &args);
}

int nmo_cmd_object_create_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv,
                                     nmo_cmd_in_session_result_t *result)
{
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_WRITE_OUTPUT,
        {"--class",     "-c", NMO_OPT_STRING, "Class name (required)"},
        {"--name",      "-n", NMO_OPT_STRING, "Object name"},
        {"--type-guid", NULL, NMO_OPT_STRING, "Type GUID (d1,d2 format)"},
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_CLASS, OPT_NAME, OPT_TYPE_GUID, OPT_DRYRUN, OPT_COUNT };

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (reject_in_session_output_option(vals[OPT_OUTPUT].present) != NMO_CLI_EXIT_SUCCESS) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count != 0) {
        fprintf(stderr, "Error: Unexpected positional argument for object create\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *class_str = nmo_opt_str(&vals[OPT_CLASS]);
    if (class_str == NULL) {
        fprintf(stderr, "Error: --class/-c is required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_guid_t type_guid = NMO_GUID_NULL;
    int guid_rc = parse_object_type_guid_arg(
        nmo_opt_str(&vals[OPT_TYPE_GUID]),
        &type_guid);
    if (guid_rc != NMO_CLI_EXIT_SUCCESS) {
        return guid_rc;
    }

    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    if (result != NULL) {
        result->dry_run = dry_run;
    }

    object_create_args_t args = {
        .class_str = class_str,
        .name = nmo_opt_str(&vals[OPT_NAME]),
        .type_guid = type_guid,
    };
    if (dry_run) {
        args.class_id = nmo_core_class_id(ctx, args.class_str);
        if (!args.class_id) {
            fprintf(stderr, "Error: Unknown class '%s'\n", args.class_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        return object_create_report(ctx, true, NULL, &args);
    }

    int rc = object_create_mutate(ctx, false, NULL, &args);
    if (rc == NMO_CLI_EXIT_SUCCESS) {
        rc = object_create_report(ctx, false, NULL, &args);
    }
    if (result != NULL && rc == NMO_CLI_EXIT_SUCCESS) {
        result->changed = true;
    }
    return rc;
}

/* ============================================================================
 * object copy - Copy objects with filter support
 *
 *   nmo object copy <id>[,<id>,...] <file> -o <output>
 *   nmo object copy --class <cls> [--name <pat>] <file> -o <output>
 * ============================================================================ */

typedef struct object_copy_args {
    const char *class_name;
    const char *name_wildcard;
    const char **id_args;
    size_t id_arg_count;
    bool use_filter;
    bool cascade;
    nmo_object_id_t *target_ids;
    size_t target_count;
    size_t count_before;
    size_t count_after;
    nmo_runtime_report_t report;
} object_copy_args_t;

static void object_copy_args_cleanup(object_copy_args_t *args)
{
    if (args == NULL) {
        return;
    }
    free(args->target_ids);
    args->target_ids = NULL;
    args->target_count = 0;
}

static int object_copy_collect_targets(nmo_cmd_ctx_t *c, object_copy_args_t *args)
{
    if (args->use_filter) {
        nmo_object_query_t query;
        nmo_core_query_build_options_t query_opts = {
            .class_name = args->class_name,
            .name_wildcard = args->name_wildcard,
            .include_derived_classes = true,
        };
        int rc = nmo_core_query_build(c, &query, &query_opts);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return rc;
        }

        delete_id_collector_t col = {0};
        nmo_core_iter_result_t iter_result = {0};
        rc = nmo_core_object_query_run(c, &query, delete_collect_visitor, &col, &iter_result);

        if (rc < 0) {
            free(col.ids);
            fprintf(stderr, "Error: Failed to iterate objects\n");
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        args->target_ids = col.ids;
        args->target_count = col.count;
        return NMO_CLI_EXIT_SUCCESS;
    }

    size_t max_ids = args->id_arg_count > 0 ? args->id_arg_count * 16 : 16;
    args->target_ids = (nmo_object_id_t *)malloc(max_ids * sizeof(nmo_object_id_t));
    if (args->target_ids == NULL) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    for (size_t i = 0; i < args->id_arg_count; i++) {
        const char *s = args->id_args[i];
        while (*s) {
            const char *comma = strchr(s, ',');
            size_t tok_len = comma ? (size_t)(comma - s) : strlen(s);
            char *tok = nmo_tool_strdup_fmt("%.*s", (int)tok_len, s);
            if (tok == NULL) {
                fprintf(stderr, "Error: Out of memory\n");
                return NMO_CLI_EXIT_INTERNAL_ERROR;
            }

            uint32_t id;
            bool valid = nmo_tool_parse_u32(tok, &id) && id != 0;
            if (!valid) {
                fprintf(stderr, "Error: Invalid object ID '%s'\n", tok);
                free(tok);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            free(tok);

            if (args->target_count >= max_ids) {
                max_ids *= 2;
                nmo_object_id_t *tmp = (nmo_object_id_t *)realloc(
                    args->target_ids, max_ids * sizeof(nmo_object_id_t));
                if (tmp == NULL) {
                    return NMO_CLI_EXIT_INTERNAL_ERROR;
                }
                args->target_ids = tmp;
            }
            args->target_ids[args->target_count++] = (nmo_object_id_t)id;

            if (comma) {
                s = comma + 1;
            } else {
                break;
            }
        }
    }

    if (args->target_count == 0) {
        fprintf(stderr, "Error: No valid object IDs specified\n");
        fprintf(stderr, "Usage: nmo object copy <id>[,<id>,...] <file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static int object_copy_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)dry_run;
    (void)output_path;
    object_copy_args_t *args = (object_copy_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    int rc = object_copy_collect_targets(c, args);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    if (args->target_count == 0) {
        return NMO_CLI_EXIT_SUCCESS;
    }

    nmo_object_repository_t *repo = nmo_tool_owner_repository(c->workspace);
    args->count_before = nmo_object_repository_get_count(repo);

    uint32_t flags = args->cascade ? NMO_RUNTIME_REQUEST_CASCADE : NMO_RUNTIME_REQUEST_DEFAULT;
    memset(&args->report, 0, sizeof(args->report));

    int copy_rc = nmo_tool_owner_copy_objects(
        c->workspace, args->target_ids, args->target_count, flags, &args->report);
    if (copy_rc != NMO_OK) {
        fprintf(stderr, "Error: Copy failed: %s\n", nmo_error_string(copy_rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    args->count_after = nmo_object_repository_get_count(repo);
    return NMO_CLI_EXIT_SUCCESS;
}

static bool object_copy_should_save(
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)dry_run;
    (void)output_path;
    object_copy_args_t *args = (object_copy_args_t *)user_data;
    return args != NULL && args->target_count > 0;
}

static int object_copy_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    object_copy_args_t *args = (object_copy_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (args->target_count == 0) {
        return object_write_emit_no_match(c, "object.copy");
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_bool(rec, "dry_run", NULL, dry_run) &&
              nmo_cli_record_uint(rec, "copied_objects", NULL,
                                  (uint64_t)args->report.copied_objects) &&
              nmo_cli_record_uint(rec, "count_before", NULL, (uint64_t)args->count_before) &&
              nmo_cli_record_uint(rec, "count_after", NULL, (uint64_t)args->count_after) &&
              nmo_cli_record_raw_fmt(rec, "%sCopied %zu object(s) (%zu -> %zu)\n",
                                     dry_run ? "[dry-run] " : "",
                                     args->report.copied_objects, args->count_before,
                                     args->count_after) &&
              object_write_add_output(rec, dry_run, output_path);
    return object_write_emit(c, rec, ok, "object.copy");
}

int nmo_cmd_object_copy(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_WRITE_OUTPUT,
        {"--class",   "-c", NMO_OPT_STRING, "Filter by class (includes derived)"},
        {"--name",    "-n", NMO_OPT_STRING, "Filter by name wildcard pattern"},
        {"--cascade", NULL, NMO_OPT_FLAG,   "Copy dependents"},
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_CLASS, OPT_NAME, OPT_CASCADE, OPT_DRYRUN, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool cascade   = nmo_opt_flag(&vals[OPT_CASCADE]);
    bool dry_run   = nmo_opt_flag(&vals[OPT_DRYRUN]);
    bool use_filter = vals[OPT_CLASS].present || vals[OPT_NAME].present;

    if (!dry_run && !output_path) {
        fprintf(stderr, "Error: -o/--output is required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_count > 0 ? r.pos_args[r.pos_count - 1] : NULL;
    if (!file_path) {
        fprintf(stderr, "Error: No input file specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    object_copy_args_t args = {
        .class_name = nmo_opt_str(&vals[OPT_CLASS]),
        .name_wildcard = nmo_opt_str(&vals[OPT_NAME]),
        .id_args = r.pos_args,
        .id_arg_count = r.pos_count > 0 ? r.pos_count - 1 : 0,
        .use_filter = use_filter,
        .cascade = cascade,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "object.copy",
        .output_required_unless_dry_run = true,
        .should_save = object_copy_should_save,
    };
    int rc = nmo_cli_run_write_command(
        file_path,
        output_path,
        dry_run,
        global,
        &spec,
        object_copy_mutate,
        object_copy_report,
        &args);
    object_copy_args_cleanup(&args);
    return rc;
}

int nmo_cmd_object_copy_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv,
                                   nmo_cmd_in_session_result_t *result)
{
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_WRITE_OUTPUT,
        {"--class",   "-c", NMO_OPT_STRING, "Filter by class (includes derived)"},
        {"--name",    "-n", NMO_OPT_STRING, "Filter by name wildcard pattern"},
        {"--cascade", NULL, NMO_OPT_FLAG,   "Copy dependents"},
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_CLASS, OPT_NAME, OPT_CASCADE, OPT_DRYRUN, OPT_COUNT };

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (reject_in_session_output_option(vals[OPT_OUTPUT].present) != NMO_CLI_EXIT_SUCCESS) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    bool use_filter = vals[OPT_CLASS].present || vals[OPT_NAME].present;
    if (!use_filter && r.pos_count == 0) {
        fprintf(stderr, "Usage: object copy [options] <id>[,<id>,...]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (result != NULL) {
        result->dry_run = dry_run;
    }

    object_copy_args_t args = {
        .class_name = nmo_opt_str(&vals[OPT_CLASS]),
        .name_wildcard = nmo_opt_str(&vals[OPT_NAME]),
        .id_args = r.pos_args,
        .id_arg_count = r.pos_count,
        .use_filter = use_filter,
        .cascade = nmo_opt_flag(&vals[OPT_CASCADE]),
    };

    int rc = NMO_CLI_EXIT_SUCCESS;
    if (dry_run) {
        rc = object_copy_collect_targets(ctx, &args);
        if (rc == NMO_CLI_EXIT_SUCCESS) {
            nmo_object_repository_t *repo = nmo_tool_owner_repository(ctx->workspace);
            args.count_before = repo ? nmo_object_repository_get_count(repo) : 0;
            args.count_after = args.count_before;
            rc = object_copy_report(ctx, true, NULL, &args);
        }
    } else {
        rc = object_copy_mutate(ctx, false, NULL, &args);
        if (rc == NMO_CLI_EXIT_SUCCESS) {
            rc = object_copy_report(ctx, false, NULL, &args);
        }
    }

    if (result != NULL && rc == NMO_CLI_EXIT_SUCCESS && !dry_run) {
        result->changed = args.report.copied_objects > 0;
    }
    object_copy_args_cleanup(&args);
    return rc;
}

/* ============================================================================
 * object import - Import object export snapshot JSON
 * ============================================================================ */

typedef struct object_import_args {
    const char *json_data;
    size_t json_size;
    uint32_t import_flags;
    nmo_import_result_t result;
} object_import_args_t;

static int object_import_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)dry_run;
    (void)output_path;
    object_import_args_t *args = (object_import_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    memset(&args->result, 0, sizeof(args->result));
    nmo_status_t st = c->workspace != NULL
        ? nmo_object_edit_import_json(
              c->workspace,
              args->json_data,
              args->json_size,
              args->import_flags,
              &args->result)
        : NMO_ERR_INVALID_ARGUMENT;

    if (st != NMO_OK) {
        fprintf(stderr, "Error: Import failed: %s\n", nmo_error_string(st));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static int object_import_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    object_import_args_t *args = (object_import_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const nmo_import_result_t *result = &args->result;
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL && nmo_cli_record_bool(rec, "dry_run", NULL, dry_run);
    if (ok && dry_run) {
        ok = nmo_cli_record_raw(rec, "=== Dry Run: Import JSON ===\n");
    }
    ok = ok &&
         nmo_cli_record_uint(rec, "objects_updated", NULL, (uint64_t)result->objects_updated) &&
         nmo_cli_record_raw_fmt(rec, "Objects updated : %zu\n", result->objects_updated) &&
         nmo_cli_record_uint(rec, "objects_created", NULL, (uint64_t)result->objects_created) &&
         nmo_cli_record_raw_fmt(rec, "Objects created : %zu\n", result->objects_created) &&
         nmo_cli_record_uint(rec, "fields_written", NULL, (uint64_t)result->fields_written) &&
         nmo_cli_record_raw_fmt(rec, "Fields written  : %zu\n", result->fields_written) &&
         nmo_cli_record_uint(rec, "fields_skipped", NULL, (uint64_t)result->fields_skipped) &&
         nmo_cli_record_raw_fmt(rec, "Fields skipped  : %zu\n", result->fields_skipped) &&
         nmo_cli_record_uint(rec, "errors", NULL, (uint64_t)result->errors) &&
         nmo_cli_record_raw_fmt(rec, "Errors          : %zu\n", result->errors) &&
         object_write_add_output(rec, dry_run, output_path);
    int rc = object_write_emit(c, rec, ok, "object.import");
    return result->errors > 0 ? NMO_CLI_EXIT_INTERNAL_ERROR : rc;
}

int nmo_cmd_object_import(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--format",  "-f", NMO_OPT_STRING, "Input format (json)"},
        NMO_OPT_DEF_WRITE_OUTPUT,
        {"--create",  NULL, NMO_OPT_FLAG,   "Create objects not found by ID"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview changes without saving"},
    };
    enum { OPT_FORMAT, OPT_OUTPUT, OPT_CREATE, OPT_DRYRUN, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    const char *input_format = nmo_opt_str(&vals[OPT_FORMAT]);
    bool create  = nmo_opt_flag(&vals[OPT_CREATE]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);

    if (!input_format ||
        (!nmo_tool_streq_ci(input_format, "json") &&
         !nmo_tool_streq_ci(input_format, "json-pretty"))) {
        fprintf(stderr, "Error: object import requires -f json\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!dry_run && !output_path) {
        fprintf(stderr, "Error: -o/--output required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 2) {
        fprintf(stderr, "Usage: nmo object import -f json <json-file> <nmo-file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *json_path = r.pos_args[0];
    const char *nmo_path = r.pos_args[r.pos_count - 1];

    /* Read JSON file */
    FILE *f = fopen(json_path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open JSON file '%s'\n", json_path);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0) {
        fclose(f);
        fprintf(stderr, "Error: Empty JSON file\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    char *json_data = (char *)malloc((size_t)fsize + 1);
    if (!json_data) {
        fclose(f);
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    size_t json_size = fread(json_data, 1, (size_t)fsize, f);
    fclose(f);
    json_data[json_size] = '\0';

    uint32_t flags = 0;
    if (create) flags |= NMO_IMPORT_CREATE_MISSING;
    if (dry_run) flags |= NMO_IMPORT_DRY_RUN;

    object_import_args_t args = {
        .json_data = json_data,
        .json_size = json_size,
        .import_flags = flags,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "object.import",
        .output_required_unless_dry_run = true,
    };
    int rc = nmo_cli_run_write_command(
        nmo_path,
        output_path,
        dry_run,
        global,
        &spec,
        object_import_mutate,
        object_import_report,
        &args);
    free(json_data);
    return rc;
}

/* ============================================================================
 * object set-field - Set a typed field value on an object
 *
 *   nmo object set-field <id> <field> <value> <file> -o <output>
 *   nmo object set-field <id> <field> <value> <file> --dry-run
 * ============================================================================ */

typedef struct object_set_field_args {
    nmo_core_object_selector_t selector;
    uint32_t object_id;
    const char *field_name;
    const char *value_str;
    nmo_field_set_result_t result;
} object_set_field_args_t;

static int object_set_field_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    object_set_field_args_t *args = (object_set_field_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_object_t *obj = NULL;
    nmo_object_id_t object_id = 0;
    int resolve_rc = nmo_core_resolve_one_object(c, &args->selector, &obj, &object_id);
    if (resolve_rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo object set-field [--id <id> | --name <name> | <id>] <field> <value> <file> [-o output]\n");
        return resolve_rc;
    }
    args->object_id = object_id;

    nmo_field_set_entry_t entry = {
        .field_name = args->field_name,
        .value_str = args->value_str,
    };
    int rc = nmo_core_set_fields(c, args->object_id, &entry, 1, dry_run,
                                 &args->result);
    nmo_core_field_set_preview(c, "Object", args->object_id, &args->result,
                               dry_run);
    return rc;
}

static int object_set_field_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    object_set_field_args_t *args = (object_set_field_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    return nmo_core_field_set_report(c, "object.set-field", args->object_id,
                                     &args->result, dry_run, output_path);
}

int nmo_cmd_object_set_field(int argc, char **argv,
                             const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        NMO_OPT_DEF_DRY_RUN,
        {"--id",      "-i", NMO_OPT_UINT,   "Object ID"},
        {"--name",    "-n", NMO_OPT_STRING, "Object name"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_ID, OPT_NAME, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);

    if (!dry_run && !output) {
        fprintf(stderr, "Error: -o required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = NULL;
    int field_index = 0;
    if (has_selector_opt) {
        if (r.pos_count < 3) {
            fprintf(stderr, "Usage: nmo object set-field [--id <id> | --name <name> | <id>] <field> <value> <file> [-o output]\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    } else {
        if (r.pos_count < 4) {
            fprintf(stderr, "Usage: nmo object set-field [--id <id> | --name <name> | <id>] <field> <value> <file> [-o output]\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        positional_id = r.pos_args[0];
        field_index = 1;
    }

    const char *field_name = r.pos_args[field_index];
    const char *value_str  = r.pos_args[field_index + 1];
    const char *file_path  = r.pos_args[r.pos_count - 1];

    object_set_field_args_t args = {
        .selector = {
            .has_id = vals[OPT_ID].present,
            .id = nmo_opt_uint_or(&vals[OPT_ID], 0),
            .positional_id = positional_id,
            .name = nmo_opt_str(&vals[OPT_NAME]),
            .selector_label = "Object",
            .type_label = "object",
        },
        .field_name = field_name,
        .value_str = value_str,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "object.set-field",
        .output_required_unless_dry_run = true,
    };
    int rc = nmo_cli_run_write_command(
        file_path,
        output,
        dry_run,
        global,
        &spec,
        object_set_field_mutate,
        object_set_field_report,
        &args);
    nmo_field_set_result_free(&args.result);
    return rc;
}

