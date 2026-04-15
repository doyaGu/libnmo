/**
 * @file nmo_cmd_object_write.c
 * @brief CLI object write commands: rename, delete, create, copy
 */

#include "nmo_cmd_object.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_session_edit.h"
#include "session/nmo_runtime_kernel.h"
#include "app/nmo_save.h"
#include "app/nmo_object_import.h"
#include "core/nmo_arena.h"
#include "dsl/nmo_dsl.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * object rename - Rename an object and save to new file
 *
 * Two modes:
 *   Single:  nmo object rename <id> <new_name> <file> -o <output>
 *   Batch:   nmo object rename --name <pattern> --to <template> [opts] <file> -o <output>
 * ============================================================================ */

/** Rename entry used by both batch text and JSON output */
typedef struct {
    nmo_object_id_t id;
    nmo_class_id_t class_id;
    char old_name[256];
    char new_name[256];
    bool collision;
} rename_entry_t;

static nmo_status_t nmo_cmd_object_rename_with_edit(
    nmo_cmd_ctx_t *c,
    nmo_object_id_t object_id,
    const char *new_name)
{
    nmo_session_edit_t *edit = NULL;
    nmo_status_t rc =
        nmo_session_edit_begin(c->session, "cli object rename", &edit);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = nmo_session_edit_rename_object(edit, object_id, new_name);
    if (rc != NMO_OK) {
        nmo_session_edit_rollback(edit);
        return rc;
    }

    return nmo_session_edit_commit(edit);
}


/**
 * Batch rename mode.
 *
 * Options already parsed by caller:
 *   name_pattern, to_template, use_regex, class_filter, dry_run, output_path
 *   file_path (last positional arg)
 */
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

    /* Open session */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_object_query_t class_query = {0};
    nmo_core_query_build_options_t query_opts = {
        .class_name = class_filter,
        .include_derived_classes = true,
    };
    rc = nmo_core_query_build(&c, &class_query, NULL, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&c, rc);
    }
    const nmo_object_query_t *filter_query =
        class_filter != NULL ? &class_query : NULL;

    /* Get all objects */
    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    size_t obj_count = 0;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &obj_count);

    /* Collect rename entries */
    rename_entry_t *entries = NULL;
    size_t entry_count = 0;
    size_t entry_cap = 0;
    size_t collision_count = 0;

    for (size_t i = 0; i < obj_count; i++) {
        nmo_object_t *obj = objects[i];
        const char *name = nmo_object_get_name(obj);
        if (!name || !name[0]) continue;

        if (!nmo_core_query_matches_object(&c, filter_query, obj)) {
            continue;
        }

        /* Pattern match + template application */
        char new_name_buf[256];
        if (use_regex) {
            /* Regex mode: match via lightweight regex, full name as {0} */
            if (!nmo_core_regex_match(name, name_pattern, true))
                continue;

            char no_captures[1][256];
            if (nmo_tool_apply_rename_template(to_template, name,
                                               no_captures, 0,
                                               new_name_buf,
                                               sizeof(new_name_buf)) < 0) {
                fprintf(stderr, "Warning: Template expansion failed for '%s'\n",
                        name);
                continue;
            }
        } else {
            /* Glob mode */
            char captures[16][256];
            size_t cap_count = 0;
            if (!nmo_tool_wildcard_capture_ci(name_pattern, name,
                                              captures, 16, &cap_count)) {
                continue;
            }
            if (nmo_tool_apply_rename_template(to_template, name,
                                               captures, cap_count,
                                               new_name_buf,
                                               sizeof(new_name_buf)) < 0) {
                fprintf(stderr, "Warning: Template expansion failed for '%s'\n",
                        name);
                continue;
            }
        }

        /* Skip if name unchanged */
        if (strcmp(name, new_name_buf) == 0) continue;

        /* Grow entries array */
        if (entry_count >= entry_cap) {
            size_t new_cap = entry_cap ? entry_cap * 2 : 32;
            rename_entry_t *tmp = (rename_entry_t *)realloc(
                entries, new_cap * sizeof(rename_entry_t));
            if (!tmp) {
                fprintf(stderr, "Error: Out of memory\n");
                free(entries);
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
            }
            entries = tmp;
            entry_cap = new_cap;
        }

        rename_entry_t *e = &entries[entry_count++];
        e->id = nmo_object_get_id(obj);
        e->class_id = nmo_object_get_class_id(obj);
        snprintf(e->old_name, sizeof(e->old_name), "%s", name);
        snprintf(e->new_name, sizeof(e->new_name), "%s", new_name_buf);

        /* Collision check */
        nmo_object_t *existing = nmo_object_repository_find_by_name(
            repo, new_name_buf);
        e->collision = (existing &&
                        nmo_object_get_id(existing) != e->id);
        if (e->collision) collision_count++;
    }

    /* Perform renames (unless dry-run) */
    size_t rename_errors = 0;
    if (!dry_run) {
        for (size_t i = 0; i < entry_count; i++) {
            nmo_status_t rrc = nmo_cmd_object_rename_with_edit(
                &c, entries[i].id, entries[i].new_name);
            if (rrc != NMO_OK) {
                fprintf(stderr, "Error: Failed to rename object %u: %s\n",
                        entries[i].id, nmo_error_string(rrc));
                rename_errors++;
            }
        }

        /* Save file (only if at least one rename succeeded) */
        size_t succeeded = entry_count - rename_errors;
        if (succeeded > 0) {
            nmo_save_options_t save_opts = nmo_save_options_default();
            int save_rc = nmo_save_file(c.session, output_path, &save_opts);
            if (save_rc != NMO_OK) {
                fprintf(stderr, "Error saving file: %s\n",
                        nmo_error_string(save_rc));
                free(entries);
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
            }
        }

        if (rename_errors > 0) {
            fprintf(stderr, "Warning: %zu rename(s) failed\n", rename_errors);
        }
    }

    /* Output results */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        if (!doc) {
            free(entries);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "match_count",
                                   (uint64_t)entry_count);
        nmo_cli_json_add_uint_safe(doc, data, "collision_count",
                                   (uint64_t)collision_count);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < entry_count; i++) {
            yyjson_mut_val *item = yyjson_mut_obj(doc);
            nmo_cli_json_add_uint_safe(doc, item, "id",
                                       (uint64_t)entries[i].id);
            const char *cls = nmo_core_class_name(&c, entries[i].class_id);
            nmo_cli_json_add_str_safe(doc, item, "class_name",
                                      cls ? cls : "?");
            nmo_cli_json_add_str_safe(doc, item, "old_name",
                                      entries[i].old_name);
            nmo_cli_json_add_str_safe(doc, item, "new_name",
                                      entries[i].new_name);
            if (entries[i].collision) {
                nmo_cli_json_add_bool_safe(doc, item, "collision", true);
            }
            yyjson_mut_arr_add_val(arr, item);
        }
        yyjson_mut_obj_add_val(doc, data, "renames", arr);

        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "object.rename");
    } else {
        if (dry_run) {
            fprintf(c.out, "=== Dry Run: Batch Rename ===\n\n");

            if (entry_count > 0) {
                static const nmo_cli_table_col_t cols[] = {
                    {"ID",       NMO_CLI_ALIGN_RIGHT, 5,  0},
                    {"CLASS",    NMO_CLI_ALIGN_LEFT,  15, 25},
                    {"OLD NAME", NMO_CLI_ALIGN_LEFT,  20, 40},
                    {"NEW NAME", NMO_CLI_ALIGN_LEFT,  20, 40},
                };
                nmo_cli_table_t table;
                nmo_cli_table_init(&table, cols, 4);

                for (size_t i = 0; i < entry_count; i++) {
                    char id_buf[16];
                    snprintf(id_buf, sizeof(id_buf), "%u", entries[i].id);
                    const char *cls = nmo_core_class_name(&c,
                                                          entries[i].class_id);
                    const char *cells[] = {
                        id_buf,
                        cls ? cls : "?",
                        entries[i].old_name,
                        entries[i].new_name
                    };
                    nmo_cli_table_add_row(&table, cells, 4);
                }

                nmo_cli_table_print(&table, c.out, c.colorize);
                nmo_cli_table_free(&table);
                fprintf(c.out, "\n");
            }

            fprintf(c.out, "%zu object(s) would be renamed, %zu collisions\n",
                    entry_count, collision_count);
        } else {
            fprintf(c.out, "%zu object(s) renamed, %zu collisions\n",
                    entry_count, collision_count);
            if (entry_count > 0 && output_path) {
                fprintf(c.out, "Saved to: %s\n", output_path);
            }
        }

        /* Print collision warnings */
        for (size_t i = 0; i < entry_count; i++) {
            if (entries[i].collision) {
                fprintf(stderr, "Warning: Name '%s' collides with existing object\n",
                        entries[i].new_name);
            }
        }
    }

    free(entries);
    int exit_code = (rename_errors > 0) ? NMO_CLI_EXIT_INTERNAL_ERROR : NMO_CLI_EXIT_SUCCESS;
    return nmo_cmd_ctx_done(&c, exit_code);
}

/* ============================================================================
 * object rename - Rename an object and save to new file
 *
 * Two modes:
 *   Single:  nmo object rename <id> <new_name> <file> -o <output>
 *   Batch:   nmo object rename --name <pattern> --to <template> [opts] <file> -o <output>
 * ============================================================================ */

int nmo_cmd_object_rename(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    /* Unified option parsing for both single and batch modes */
    static const nmo_opt_def_t opts[] = {
        {"--name",    "-n", NMO_OPT_STRING, "Glob/regex pattern to match object names (batch mode)"},
        {"--to",      NULL, NMO_OPT_STRING, "Rename template with {0},{1}..{N} placeholders"},
        {"--regex",   NULL, NMO_OPT_FLAG,   "Treat --name as POSIX regex instead of glob"},
        {"--class",   "-c", NMO_OPT_STRING, "Restrict to objects of this class"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview renames without saving"},
        {"--output",  "-o", NMO_OPT_STRING, "Output file path"},
    };
    enum { OPT_NAME, OPT_TO, OPT_REGEX, OPT_CLASS, OPT_DRY_RUN, OPT_OUTPUT, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    /* Batch mode: --name is present */
    if (vals[OPT_NAME].present) {
        const char *file_path = r.pos_count > 0
            ? r.pos_args[r.pos_count - 1] : NULL;
        return nmo_cmd_object_rename_batch(
            vals[OPT_NAME].val.str,
            vals[OPT_TO].present ? vals[OPT_TO].val.str : NULL,
            vals[OPT_REGEX].present && vals[OPT_REGEX].val.flag,
            vals[OPT_CLASS].present ? vals[OPT_CLASS].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            file_path,
            global);
    }

    /* Single mode: <id> <new_name> <file> -o <output> */
    const char *output_path = vals[OPT_OUTPUT].present
        ? vals[OPT_OUTPUT].val.str : NULL;
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

    /* Open session manually (nmo_cmd_ctx_init would pick -o value as file) */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    /* Get repository */
    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);

    /* Find object by ID */
    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, object_id);
    if (!obj) {
        fprintf(stderr, "Error: Object %u not found\n", object_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    /* Save old name */
    const char *old_name = nmo_object_get_name(obj);
    char old_name_buf[256];
    if (old_name && old_name[0]) {
        snprintf(old_name_buf, sizeof(old_name_buf), "%s", old_name);
    } else {
        old_name_buf[0] = '\0';
    }

    /* Check for name collision */
    bool name_collision = false;
    nmo_object_t *existing = nmo_object_repository_find_by_name(repo, new_name);
    if (existing && nmo_object_get_id(existing) != object_id) {
        name_collision = true;
        if (!c.is_json) {
            fprintf(stderr, "Warning: Name '%s' already used by object %u\n",
                    new_name, nmo_object_get_id(existing));
        }
    }

    /* Perform rename */
    int rename_rc = nmo_cmd_object_rename_with_edit(&c, object_id, new_name);
    if (rename_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to rename object %u: %s\n",
                object_id, nmo_error_string(rename_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Save file */
    nmo_save_options_t save_opts = nmo_save_options_default();
    int save_rc = nmo_save_file(c.session, output_path, &save_opts);
    if (save_rc != NMO_OK) {
        fprintf(stderr, "Error saving file: %s\n", nmo_error_string(save_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    /* Output results */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        if (!doc) {
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_uint_safe(doc, data, "id", (uint64_t)object_id);
        nmo_cli_json_add_str_safe(doc, data, "old_name",
                                  old_name_buf[0] ? old_name_buf : "");
        nmo_cli_json_add_str_safe(doc, data, "new_name", new_name);
        nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        nmo_cli_json_add_bool_safe(doc, data, "name_collision", name_collision);

        nmo_cmd_ctx_json_end(&c, doc, data, "object.rename");
    } else {
        fprintf(c.out, "Renamed: %s -> %s (ID %u)\n",
                old_name_buf[0] ? old_name_buf : "(unnamed)", new_name, object_id);
        fprintf(c.out, "Saved to: %s\n", output_path);
        if (name_collision) {
            fprintf(c.out, "Warning: Name collision with existing object\n");
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * object delete - Delete objects with filter support and cascade preview
 *
 *   nmo object delete <id>[,<id>,...] <file> -o <output>
 *   nmo object delete --class <cls> [--name <pat>] [--filter <expr>] <file> -o <output>
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

/* Batch delete context -- stores copies of filter strings */
typedef struct {
    char class_str[64];
    char name_str[256];
    char filter_str[512];
    bool has_class;
    bool has_name;
    bool has_filter;
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
    int rc = nmo_cmd_ctx_init_with_file(&c, input_path, global);
    if (rc) return rc;

    /* Build query */
    nmo_object_query_t query;
    nmo_core_query_dsl_t query_dsl = {0};
    nmo_core_query_build_options_t query_opts = {
        .class_name = ctx->has_class ? ctx->class_str : NULL,
        .name_wildcard = ctx->has_name ? ctx->name_str : NULL,
        .filter_expr = ctx->has_filter ? ctx->filter_str : NULL,
        .include_derived_classes = true,
        .print_dsl_context = true,
    };
    rc = nmo_core_query_build(&c, &query, &query_dsl, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&c, rc);
    }

    /* Collect matching IDs */
    delete_id_collector_t col = {0};
    nmo_core_iter_result_t iter_result;
    rc = nmo_core_iter_objects(&c, &query, delete_collect_visitor, &col, &iter_result);

    nmo_core_query_dsl_destroy(&query_dsl);

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
    int del_rc = nmo_session_destroy_objects(c.session, col.ids, col.count, flags, &report);
    free(col.ids);
    if (del_rc != NMO_OK) {
        fprintf(stderr, "Error: Deletion failed: %s\n", nmo_error_string(del_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Save */
    nmo_save_options_t save_opts = nmo_save_options_default();
    int save_rc = nmo_save_file(c.session, output_path, &save_opts);
    if (save_rc != NMO_OK) {
        fprintf(stderr, "Error saving: %s\n", nmo_error_string(save_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    fprintf(stderr, "  Deleted %zu object(s) -> %s\n", report.deleted_objects, output_path);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

int nmo_cmd_object_delete(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o", NMO_OPT_STRING, "Output file (required unless --dry-run)"},
        {"--class",   "-c", NMO_OPT_STRING, "Filter by class (includes derived)"},
        {"--name",    "-n", NMO_OPT_STRING, "Filter by name wildcard pattern"},
        {"--filter",  "-f", NMO_OPT_STRING, "Filter by DSL expression"},
        {"--cascade", NULL, NMO_OPT_FLAG,   "Delete dependents (default: safe-detach)"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview only, do not save"},
        {"--strict",  NULL, NMO_OPT_FLAG,   "Fail if any ID not found"},
    };
    enum { OPT_OUTPUT, OPT_CLASS, OPT_NAME, OPT_FILTER, OPT_CASCADE,
           OPT_DRYRUN, OPT_STRICT, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool cascade  = vals[OPT_CASCADE].present && vals[OPT_CASCADE].val.flag;
    bool dry_run  = vals[OPT_DRYRUN].present  && vals[OPT_DRYRUN].val.flag;
    bool strict   = vals[OPT_STRICT].present  && vals[OPT_STRICT].val.flag;
    bool use_filter = vals[OPT_CLASS].present || vals[OPT_NAME].present ||
                      vals[OPT_FILTER].present;

    if (!dry_run && !output_path) {
        fprintf(stderr, "Error: -o/--output is required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Batch mode: all positional args are files (filter mode only) */
    if (global->batch_mode && use_filter && r.pos_count > 0 && output_path) {
        delete_batch_ctx_t batch_ctx;
        memset(&batch_ctx, 0, sizeof(batch_ctx));
        if (vals[OPT_CLASS].present) {
            snprintf(batch_ctx.class_str, sizeof(batch_ctx.class_str), "%s", vals[OPT_CLASS].val.str);
            batch_ctx.has_class = true;
        }
        if (vals[OPT_NAME].present) {
            snprintf(batch_ctx.name_str, sizeof(batch_ctx.name_str), "%s", vals[OPT_NAME].val.str);
            batch_ctx.has_name = true;
        }
        if (vals[OPT_FILTER].present) {
            snprintf(batch_ctx.filter_str, sizeof(batch_ctx.filter_str), "%s", vals[OPT_FILTER].val.str);
            batch_ctx.has_filter = true;
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

    /* Open session */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    /* Collect target IDs */
    nmo_object_id_t *target_ids = NULL;
    size_t target_count = 0;
    bool target_ids_owned = false;  /* true if we need to free target_ids */

    if (use_filter) {
        /* Filter-based collection */
        nmo_object_query_t query;
        nmo_core_query_dsl_t query_dsl = {0};
        nmo_core_query_build_options_t query_opts = {
            .class_name = vals[OPT_CLASS].present ? vals[OPT_CLASS].val.str : NULL,
            .name_wildcard = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
            .filter_expr = vals[OPT_FILTER].present ? vals[OPT_FILTER].val.str : NULL,
            .include_derived_classes = true,
        };
        rc = nmo_core_query_build(&c, &query, &query_dsl, &query_opts);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return nmo_cmd_ctx_done(&c, rc);
        }

        delete_id_collector_t col = {0};
        nmo_core_iter_result_t iter_result;
        rc = nmo_core_iter_objects(&c, &query, delete_collect_visitor, &col, &iter_result);

        nmo_core_query_dsl_destroy(&query_dsl);

        if (rc < 0) {
            free(col.ids);
            fprintf(stderr, "Error: Failed to iterate objects\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        target_ids = col.ids;
        target_count = col.count;
        target_ids_owned = true;
    } else {
        /* ID-based: parse comma-separated IDs from positional args */
        /* Allocate enough for all positional args (minus the file) */
        size_t max_ids = r.pos_count > 1 ? (r.pos_count - 1) * 16 : 16;
        target_ids = (nmo_object_id_t *)malloc(max_ids * sizeof(nmo_object_id_t));
        if (!target_ids) {
            fprintf(stderr, "Error: Out of memory\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        target_ids_owned = true;

        for (size_t i = 0; i < r.pos_count - 1; i++) {
            /* Parse comma-separated IDs: "42" or "42,43,44" */
            const char *s = r.pos_args[i];
            while (*s) {
                /* Extract one token up to comma or end */
                const char *comma = strchr(s, ',');
                size_t tok_len = comma ? (size_t)(comma - s) : strlen(s);
                char tok[32];
                if (tok_len >= sizeof(tok)) break;
                memcpy(tok, s, tok_len);
                tok[tok_len] = '\0';

                uint32_t id;
                if (!nmo_tool_parse_u32(tok, &id) || id == 0) {
                    fprintf(stderr, "Error: Invalid object ID '%s'\n", tok);
                    free(target_ids);
                    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
                }

                /* Grow array if needed */
                if (target_count >= max_ids) {
                    max_ids *= 2;
                    nmo_object_id_t *tmp = (nmo_object_id_t *)realloc(
                        target_ids, max_ids * sizeof(nmo_object_id_t));
                    if (!tmp) { free(target_ids); return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR); }
                    target_ids = tmp;
                }
                target_ids[target_count++] = (nmo_object_id_t)id;

                if (comma) s = comma + 1;
                else break;
            }
        }

        if (target_count == 0) {
            free(target_ids);
            fprintf(stderr, "Error: No valid object IDs specified\n");
            fprintf(stderr, "Usage: nmo object delete <id>[,<id>,...] <file> -o <output>\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }

        /* Validate IDs exist */
        if (strict) {
            for (size_t i = 0; i < target_count; i++) {
                if (!nmo_core_find_by_id(&c, target_ids[i])) {
                    fprintf(stderr, "Error: Object %u not found (--strict)\n", target_ids[i]);
                    free(target_ids);
                    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_NOT_FOUND);
                }
            }
        }
    }

    if (target_count == 0) {
        if (target_ids_owned) free(target_ids);
        if (!c.is_json)
            fprintf(c.out, "No objects matched.\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
    }

    /* Preview: expand cascade set */
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        if (target_ids_owned) free(target_ids);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    uint32_t flags = cascade ? NMO_RUNTIME_REQUEST_CASCADE
                             : NMO_RUNTIME_REQUEST_SAFE_DETACH;
    if (strict) flags |= NMO_RUNTIME_REQUEST_STRICT;

    nmo_object_id_t *expanded_ids = NULL;
    size_t expanded_count = 0;

    if (cascade) {
        int prev_rc = nmo_session_preview_destroy(
            c.session, target_ids, target_count, flags,
            arena, &expanded_ids, &expanded_count);
        if (prev_rc != NMO_OK) {
            /* Fallback: just the original set */
            expanded_ids = target_ids;
            expanded_count = target_count;
        }
    } else {
        expanded_ids = target_ids;
        expanded_count = target_count;
    }

    /* Compute total size that would be freed */
    size_t total_size = 0;
    for (size_t i = 0; i < expanded_count; i++) {
        nmo_object_t *obj = nmo_core_find_by_id(&c, expanded_ids[i]);
        if (obj) {
            nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
            if (chunk) total_size += nmo_chunk_get_data_size(chunk);
        }
    }

    /* Output */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        if (!doc) {
            nmo_arena_destroy(arena);
            if (target_ids_owned) free(target_ids);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_str(doc, data, "mode", cascade ? "cascade" : "safe_detach");
        nmo_cli_json_add_uint_safe(doc, data, "requested_count", (uint64_t)target_count);
        nmo_cli_json_add_uint_safe(doc, data, "expanded_count", (uint64_t)expanded_count);
        nmo_cli_json_add_uint_safe(doc, data, "total_size", (uint64_t)total_size);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < expanded_count; i++) {
            yyjson_mut_val *item = yyjson_mut_obj(doc);
            nmo_cli_json_add_uint_safe(doc, item, "id", (uint64_t)expanded_ids[i]);
            nmo_object_t *obj = nmo_core_find_by_id(&c, expanded_ids[i]);
            if (obj) {
                const char *name = nmo_object_get_name(obj);
                if (name && name[0])
                    nmo_cli_json_add_str_safe(doc, item, "name", name);
                char cbuf[32];
                const char *cls = nmo_core_class_name_or(
                    &c, nmo_object_get_class_id(obj), cbuf, sizeof(cbuf));
                nmo_cli_json_add_str_safe(doc, item, "class_name", cls);
            }
            yyjson_mut_arr_add_val(arr, item);
        }
        yyjson_mut_obj_add_val(doc, data, "objects", arr);

        if (!dry_run && output_path)
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);

        nmo_cmd_ctx_json_end(&c, doc, data, "object.delete");
    } else {
        const char *mode_str = cascade ? "cascade" : "safe-detach";
        if (dry_run)
            fprintf(c.out, "=== Dry Run: Delete (%s mode) ===\n\n", mode_str);

        if (expanded_count > 0) {
            static const nmo_cli_table_col_t cols[] = {
                {"ID",    NMO_CLI_ALIGN_RIGHT, 6, 0},
                {"Class", NMO_CLI_ALIGN_LEFT, 18, 0},
                {"Name",  NMO_CLI_ALIGN_LEFT, 30, 0},
            };
            nmo_cli_table_t table;
            nmo_cli_table_init(&table, cols, 3);

            for (size_t i = 0; i < expanded_count; i++) {
                char id_buf[16];
                snprintf(id_buf, sizeof(id_buf), "%u", expanded_ids[i]);
                const char *cls = "-";
                const char *name = "-";
                nmo_object_t *obj = nmo_core_find_by_id(&c, expanded_ids[i]);
                char cbuf[32];
                if (obj) {
                    cls = nmo_core_class_name_or(
                        &c, nmo_object_get_class_id(obj), cbuf, sizeof(cbuf));
                    const char *n = nmo_object_get_name(obj);
                    if (n && n[0]) name = n;
                }
                const char *cells[] = { id_buf, cls, name };
                nmo_cli_table_add_row(&table, cells, 3);
            }
            nmo_cli_table_print(&table, c.out, c.colorize);
            nmo_cli_table_free(&table);
        }

        if (cascade && expanded_count > target_count) {
            fprintf(c.out, "\nRequested: %zu, cascade expanded to: %zu object(s)\n",
                    target_count, expanded_count);
        } else {
            fprintf(c.out, "\n%zu object(s)", expanded_count);
        }
        fprintf(c.out, " (%zu bytes)\n", total_size);
    }

    /* Perform deletion (unless dry-run) */
    int exit_code = NMO_CLI_EXIT_SUCCESS;
    if (!dry_run) {
        nmo_arena_destroy(arena);
        arena = NULL;

        nmo_runtime_report_t report;
        memset(&report, 0, sizeof(report));
        int del_rc = nmo_session_destroy_objects(
            c.session, target_ids, target_count, flags, &report);

        if (del_rc != NMO_OK) {
            fprintf(stderr, "Error: Deletion failed: %s\n", nmo_error_string(del_rc));
            if (target_ids_owned) free(target_ids);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        /* Save file */
        nmo_save_options_t save_opts = nmo_save_options_default();
        int save_rc = nmo_save_file(c.session, output_path, &save_opts);
        if (save_rc != NMO_OK) {
            fprintf(stderr, "Error saving file: %s\n", nmo_error_string(save_rc));
            if (target_ids_owned) free(target_ids);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
        }

        if (!c.is_json) {
            fprintf(c.out, "Deleted %zu object(s), saved to: %s\n",
                    report.deleted_objects, output_path);
        }
    }

    if (arena) nmo_arena_destroy(arena);
    if (target_ids_owned) free(target_ids);
    return nmo_cmd_ctx_done(&c, exit_code);
}

/* ============================================================================
 * object create - Create a new object and save to file
 *
 *   nmo object create --class <name> [--name <name>] [--type-guid <guid>] <file> -o <output>
 * ============================================================================ */

int nmo_cmd_object_create(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--output",    "-o", NMO_OPT_STRING, "Output file (required unless --dry-run)"},
        {"--class",     "-c", NMO_OPT_STRING, "Class name (required)"},
        {"--name",      "-n", NMO_OPT_STRING, "Object name"},
        {"--type-guid", NULL, NMO_OPT_STRING, "Type GUID (d1,d2 format)"},
        {"--dry-run",   NULL, NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_CLASS, OPT_NAME, OPT_TYPE_GUID, OPT_DRYRUN, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    const char *class_str   = vals[OPT_CLASS].present  ? vals[OPT_CLASS].val.str  : NULL;
    const char *name        = vals[OPT_NAME].present   ? vals[OPT_NAME].val.str   : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;

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

    /* Open session */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    /* Resolve class */
    nmo_class_id_t class_id = nmo_core_class_id(&c, class_str);
    if (!class_id) {
        fprintf(stderr, "Error: Unknown class '%s'\n", class_str);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    /* Parse type GUID if provided */
    nmo_guid_t type_guid = NMO_GUID_NULL;
    if (vals[OPT_TYPE_GUID].present) {
        const char *guid_str = vals[OPT_TYPE_GUID].val.str;
        uint32_t d1 = 0, d2 = 0;
        if (sscanf(guid_str, "%x,%x", &d1, &d2) == 2) {
            type_guid.d1 = d1;
            type_guid.d2 = d2;
        } else {
            type_guid = nmo_guid_parse(guid_str);
            if (nmo_guid_is_null(type_guid)) {
                fprintf(stderr, "Error: Invalid GUID '%s'\n", guid_str);
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
            }
        }
    }

    /* Create object */
    nmo_object_id_t new_id = 0;
    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));

    int create_rc = nmo_session_create_object(
        c.session, class_id, name, type_guid, &new_id, &report);
    if (create_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to create object: %s\n",
                nmo_error_string(create_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Save file (unless dry-run) */
    if (!dry_run) {
        nmo_save_options_t save_opts = nmo_save_options_default();
        int save_rc = nmo_save_file(c.session, output_path, &save_opts);
        if (save_rc != NMO_OK) {
            fprintf(stderr, "Error saving file: %s\n", nmo_error_string(save_rc));
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
        }
    }

    /* Output */
    const char *cls = nmo_core_class_name(&c, class_id);
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        if (!doc)
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "id", (uint64_t)new_id);
        nmo_cli_json_add_str_safe(doc, data, "class_name", cls ? cls : class_str);
        nmo_cli_json_add_str_safe(doc, data, "name", name ? name : "");
        if (!dry_run && output_path)
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);

        nmo_cmd_ctx_json_end(&c, doc, data, "object.create");
    } else {
        if (dry_run)
            fprintf(c.out, "[dry-run] ");
        fprintf(c.out, "Created object #%u (%s)",
                new_id, cls ? cls : class_str);
        if (name && name[0])
            fprintf(c.out, " [name: %s]", name);
        fprintf(c.out, "\n");
        if (!dry_run && output_path)
            fprintf(c.out, "Saved to: %s\n", output_path);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * object copy - Copy objects with filter support
 *
 *   nmo object copy <id>[,<id>,...] <file> -o <output>
 *   nmo object copy --class <cls> [--name <pat>] <file> -o <output>
 * ============================================================================ */

int nmo_cmd_object_copy(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o", NMO_OPT_STRING, "Output file (required unless --dry-run)"},
        {"--class",   "-c", NMO_OPT_STRING, "Filter by class (includes derived)"},
        {"--name",    "-n", NMO_OPT_STRING, "Filter by name wildcard pattern"},
        {"--filter",  "-f", NMO_OPT_STRING, "Filter by DSL expression"},
        {"--cascade", NULL, NMO_OPT_FLAG,   "Copy dependents"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_CLASS, OPT_NAME, OPT_FILTER, OPT_CASCADE, OPT_DRYRUN, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool cascade   = vals[OPT_CASCADE].present && vals[OPT_CASCADE].val.flag;
    bool dry_run   = vals[OPT_DRYRUN].present  && vals[OPT_DRYRUN].val.flag;
    bool use_filter = vals[OPT_CLASS].present || vals[OPT_NAME].present ||
                      vals[OPT_FILTER].present;

    if (!dry_run && !output_path) {
        fprintf(stderr, "Error: -o/--output is required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_count > 0 ? r.pos_args[r.pos_count - 1] : NULL;
    if (!file_path) {
        fprintf(stderr, "Error: No input file specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    /* Collect target IDs -- same pattern as object delete */
    nmo_object_id_t *target_ids = NULL;
    size_t target_count = 0;
    bool target_ids_owned = false;

    if (use_filter) {
        nmo_object_query_t query;
        nmo_core_query_dsl_t query_dsl = {0};
        nmo_core_query_build_options_t query_opts = {
            .class_name = vals[OPT_CLASS].present ? vals[OPT_CLASS].val.str : NULL,
            .name_wildcard = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
            .filter_expr = vals[OPT_FILTER].present ? vals[OPT_FILTER].val.str : NULL,
            .include_derived_classes = true,
        };
        rc = nmo_core_query_build(&c, &query, &query_dsl, &query_opts);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return nmo_cmd_ctx_done(&c, rc);
        }

        delete_id_collector_t col = {0};
        nmo_core_iter_result_t iter_result;
        rc = nmo_core_iter_objects(&c, &query, delete_collect_visitor, &col, &iter_result);

        nmo_core_query_dsl_destroy(&query_dsl);

        if (rc < 0) {
            free(col.ids);
            fprintf(stderr, "Error: Failed to iterate objects\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        target_ids = col.ids;
        target_count = col.count;
        target_ids_owned = true;
    } else {
        /* ID-based: parse comma-separated IDs from positional args */
        size_t max_ids = r.pos_count > 1 ? (r.pos_count - 1) * 16 : 16;
        target_ids = (nmo_object_id_t *)malloc(max_ids * sizeof(nmo_object_id_t));
        if (!target_ids) {
            fprintf(stderr, "Error: Out of memory\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        target_ids_owned = true;

        for (size_t i = 0; i < r.pos_count - 1; i++) {
            const char *s = r.pos_args[i];
            while (*s) {
                const char *comma = strchr(s, ',');
                size_t tok_len = comma ? (size_t)(comma - s) : strlen(s);
                char tok[32];
                if (tok_len >= sizeof(tok)) break;
                memcpy(tok, s, tok_len);
                tok[tok_len] = '\0';

                uint32_t id;
                if (!nmo_tool_parse_u32(tok, &id) || id == 0) {
                    fprintf(stderr, "Error: Invalid object ID '%s'\n", tok);
                    free(target_ids);
                    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
                }

                if (target_count >= max_ids) {
                    max_ids *= 2;
                    nmo_object_id_t *tmp = (nmo_object_id_t *)realloc(
                        target_ids, max_ids * sizeof(nmo_object_id_t));
                    if (!tmp) { free(target_ids); return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR); }
                    target_ids = tmp;
                }
                target_ids[target_count++] = (nmo_object_id_t)id;

                if (comma) s = comma + 1;
                else break;
            }
        }

        if (target_count == 0) {
            free(target_ids);
            fprintf(stderr, "Error: No valid object IDs specified\n");
            fprintf(stderr, "Usage: nmo object copy <id>[,<id>,...] <file> -o <output>\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    if (target_count == 0) {
        if (target_ids_owned) free(target_ids);
        if (!c.is_json)
            fprintf(c.out, "No objects matched.\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
    }

    /* Snapshot count before */
    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    size_t count_before = nmo_object_repository_get_count(repo);

    /* Copy objects */
    uint32_t flags = cascade ? NMO_RUNTIME_REQUEST_CASCADE : NMO_RUNTIME_REQUEST_DEFAULT;

    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));

    int copy_rc = nmo_session_copy_objects(
        c.session, target_ids, target_count, flags, &report);
    if (copy_rc != NMO_OK) {
        fprintf(stderr, "Error: Copy failed: %s\n", nmo_error_string(copy_rc));
        if (target_ids_owned) free(target_ids);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Snapshot count after */
    size_t count_after = nmo_object_repository_get_count(repo);

    /* Save file (unless dry-run) */
    if (!dry_run) {
        nmo_save_options_t save_opts = nmo_save_options_default();
        int save_rc = nmo_save_file(c.session, output_path, &save_opts);
        if (save_rc != NMO_OK) {
            fprintf(stderr, "Error saving file: %s\n", nmo_error_string(save_rc));
            if (target_ids_owned) free(target_ids);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
        }
    }

    /* Output */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        if (!doc) {
            if (target_ids_owned) free(target_ids);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "copied_objects",
                                   (uint64_t)report.copied_objects);
        nmo_cli_json_add_uint_safe(doc, data, "count_before",
                                   (uint64_t)count_before);
        nmo_cli_json_add_uint_safe(doc, data, "count_after",
                                   (uint64_t)count_after);
        if (!dry_run && output_path)
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);

        nmo_cmd_ctx_json_end(&c, doc, data, "object.copy");
    } else {
        if (dry_run)
            fprintf(c.out, "[dry-run] ");
        fprintf(c.out, "Copied %zu object(s) (%zu -> %zu)\n",
                report.copied_objects, count_before, count_after);
        if (!dry_run && output_path)
            fprintf(c.out, "Saved to: %s\n", output_path);
    }

    if (target_ids_owned) free(target_ids);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * object import-json - Import objects from JSON (round-trip with object export)
 * ============================================================================ */

int nmo_cmd_object_import_json(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o", NMO_OPT_STRING, "Output file (required unless --dry-run)"},
        {"--create",  NULL, NMO_OPT_FLAG,   "Create objects not found by ID"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview changes without saving"},
    };
    enum { OPT_OUTPUT, OPT_CREATE, OPT_DRYRUN, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool create  = vals[OPT_CREATE].present && vals[OPT_CREATE].val.flag;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;

    if (!dry_run && !output_path) {
        fprintf(stderr, "Error: -o/--output required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 2) {
        fprintf(stderr, "Usage: nmo object import-json <json-file> <nmo-file> -o <output>\n");
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

    /* Open NMO session */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, nmo_path, global);
    if (rc) { free(json_data); return rc; }

    /* Import */
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        free(json_data);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    uint32_t flags = 0;
    if (create) flags |= NMO_IMPORT_CREATE_MISSING;
    if (dry_run) flags |= NMO_IMPORT_DRY_RUN;

    nmo_import_result_t result;
    memset(&result, 0, sizeof(result));

    nmo_status_t st = nmo_object_import_json(
        c.session, c.registry, arena,
        json_data, json_size, flags, &result);

    nmo_arena_destroy(arena);
    free(json_data);

    if (st != NMO_OK) {
        fprintf(stderr, "Error: Import failed: %s\n", nmo_error_string(st));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Save (unless dry-run) */
    if (!dry_run) {
        nmo_save_options_t save_opts = nmo_save_options_default();
        int save_rc = nmo_save_file(c.session, output_path, &save_opts);
        if (save_rc != NMO_OK) {
            fprintf(stderr, "Error saving: %s\n", nmo_error_string(save_rc));
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
        }
    }

    /* Output */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        if (!doc) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "objects_updated", (uint64_t)result.objects_updated);
        nmo_cli_json_add_uint_safe(doc, data, "objects_created", (uint64_t)result.objects_created);
        nmo_cli_json_add_uint_safe(doc, data, "fields_written", (uint64_t)result.fields_written);
        nmo_cli_json_add_uint_safe(doc, data, "fields_skipped", (uint64_t)result.fields_skipped);
        nmo_cli_json_add_uint_safe(doc, data, "errors", (uint64_t)result.errors);
        if (!dry_run && output_path)
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        nmo_cmd_ctx_json_end(&c, doc, data, "object.import-json");
    } else {
        if (dry_run)
            fprintf(c.out, "=== Dry Run: Import JSON ===\n");
        fprintf(c.out, "Objects updated : %zu\n", result.objects_updated);
        fprintf(c.out, "Objects created : %zu\n", result.objects_created);
        fprintf(c.out, "Fields written  : %zu\n", result.fields_written);
        fprintf(c.out, "Fields skipped  : %zu\n", result.fields_skipped);
        fprintf(c.out, "Errors          : %zu\n", result.errors);
        if (!dry_run && output_path)
            fprintf(c.out, "Saved to: %s\n", output_path);
    }

    return nmo_cmd_ctx_done(&c, result.errors > 0 ? NMO_CLI_EXIT_INTERNAL_ERROR : NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * object set-field - Set a typed field value on an object
 *
 *   nmo object set-field <id> <field> <value> <file> -o <output>
 *   nmo object set-field <id> <field> <value> <file> --dry-run
 * ============================================================================ */

int nmo_cmd_object_set_field(int argc, char **argv,
                             const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o", NMO_OPT_STRING, "Output file"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    const char *output = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;

    if (!dry_run && !output) {
        fprintf(stderr, "Error: -o required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 4) {
        fprintf(stderr, "Usage: nmo object set-field <id> <field> <value> <file> [-o output]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t object_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &object_id)) {
        fprintf(stderr, "Error: Invalid object ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *field_name = r.pos_args[1];
    const char *value_str  = r.pos_args[2];
    const char *file_path  = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    fprintf(c.out, "Object #%u:\n", object_id);

    nmo_field_set_entry_t entry = { .field_name = field_name, .value_str = value_str };
    nmo_field_set_result_t result = {0, 0};
    int set_rc = nmo_core_set_fields(&c, object_id, &entry, 1, dry_run, &result);

    if (set_rc != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&c, set_rc);
    }

    if (!dry_run && output) {
        nmo_save_options_t save_opts = nmo_save_options_default();
        int save_rc = nmo_save_file(c.session, output, &save_opts);
        if (save_rc != NMO_OK) {
            fprintf(stderr, "Error: Save failed: %s\n", nmo_error_string(save_rc));
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
        }
        fprintf(c.out, "Saved to: %s\n", output);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}
