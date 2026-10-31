/**
 * @file nmo_cmd_group.c
 * @brief CLI group command group: list, show, add and remove CKGroup members
 */

#include "nmo_cmd_group.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_write.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_record.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_edit.h"
#include "object/builtin/nmo_group_schemas.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int group_list_run(nmo_cmd_ctx_t *c, int argc, char **argv);
static int group_show_run(nmo_cmd_ctx_t *c, int argc, char **argv);

int nmo_cmd_group_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: group list|show ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (strcmp(argv[0], "list") == 0 || strcmp(argv[0], "ls") == 0) {
        return group_list_run(ctx, argc, argv);
    }
    if (strcmp(argv[0], "show") == 0 || strcmp(argv[0], "s") == 0) {
        return group_show_run(ctx, argc, argv);
    }
    fprintf(stderr, "Unsupported group read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
}

/* ============================================================================
 * Helpers
 * ============================================================================ */

static const nmo_group_state_t *group_state(const nmo_object_t *group)
{
    return group != NULL && nmo_object_get_class_id(group) == NMO_CID_GROUP
        ? (const nmo_group_state_t *)nmo_object_get_state(group)
        : NULL;
}

static nmo_object_id_t group_member_id(const nmo_group_state_t *state, size_t index)
{
    const nmo_ref_t *ref = (const nmo_ref_t *)nmo_array_get(&state->object_ids, index);
    return ref != NULL ? nmo_ref_runtime_id(ref) : 0u;
}

static bool group_has_member(const nmo_group_state_t *state, nmo_object_id_t id)
{
    for (size_t i = 0; state != NULL && i < state->object_ids.count; ++i) {
        if (group_member_id(state, i) == id) {
            return true;
        }
    }
    return false;
}

static bool group_text_is_id(const char *text)
{
    if (text == NULL || *text == '\0') {
        return false;
    }
    for (const char *p = text; *p != '\0'; ++p) {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
    }
    return true;
}

/* An object given by id (all digits) or by exact name. */
static int group_resolve_object(const nmo_cmd_ctx_t *c,
                                const char *text,
                                nmo_class_id_t base_class,
                                const char *label,
                                nmo_object_t **out_object,
                                nmo_object_id_t *out_id)
{
    nmo_core_object_selector_t selector = {
        .positional_id = group_text_is_id(text) ? text : NULL,
        .name = group_text_is_id(text) ? NULL : text,
        .required_base_class = base_class,
        .selector_label = label,
        .type_label = label,
    };
    return nmo_core_resolve_one_object(c, &selector, out_object, out_id);
}

static const nmo_cli_table_col_t group_list_columns[] = {
    {"ID",      NMO_CLI_ALIGN_RIGHT, 6,  0},
    {"Name",    NMO_CLI_ALIGN_LEFT,  24, 60},
    {"Members", NMO_CLI_ALIGN_RIGHT, 7,  0},
};

static const nmo_cli_table_col_t group_member_columns[] = {
    {"ID",    NMO_CLI_ALIGN_RIGHT, 6,  0},
    {"Class", NMO_CLI_ALIGN_LEFT,  20, 0},
    {"Name",  NMO_CLI_ALIGN_LEFT,  24, 60},
};

static nmo_cli_record_t *group_member_record_new(const nmo_cmd_ctx_t *c, nmo_object_id_t id)
{
    nmo_object_t *obj = nmo_core_find_by_id(c, id);
    const char *name = obj != NULL ? nmo_object_get_name(obj) : NULL;
    const char *class_name = obj != NULL ? nmo_core_class_name(c, nmo_object_get_class_id(obj)) : NULL;
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "id", "ID", id) &&
              nmo_cli_record_str(rec, "class", "Class", class_name ? class_name : "-") &&
              nmo_cli_record_str(rec, "name", "Name", name) &&
              (name && name[0] ? true : nmo_cli_record_set_text(rec, "-"));
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

/* ============================================================================
 * group list
 * ============================================================================ */

typedef struct group_list_data {
    nmo_object_id_t member;
    nmo_cli_record_array_t *groups;
    size_t count;
    bool failed;
} group_list_data_t;

static int group_list_visitor(size_t index, nmo_object_t *obj, const nmo_cmd_ctx_t *c, void *user)
{
    (void)index;
    (void)c;
    group_list_data_t *data = (group_list_data_t *)user;
    const nmo_group_state_t *state = group_state(obj);
    if (state == NULL || (data->member != 0u && !group_has_member(state, data->member))) {
        return 0;
    }
    const char *name = nmo_object_get_name(obj);
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "id", "ID", nmo_object_get_id(obj)) &&
              nmo_cli_record_str(rec, "name", "Name", name) &&
              (name && name[0] ? true : nmo_cli_record_set_text(rec, "-")) &&
              nmo_cli_record_uint(rec, "member_count", "Members", state->object_ids.count) &&
              nmo_cli_record_array_add(data->groups, rec);
    if (!ok) {
        nmo_cli_record_free(rec);
        data->failed = true;
        return 1;
    }
    data->count++;
    return 0;
}

static int group_list_run(nmo_cmd_ctx_t *c, int argc, char **argv)
{
    static const nmo_opt_def_t opts[] = {
        {"--member", "-m", NMO_OPT_STRING, "Only the groups holding this object (id or name)"},
    };
    enum { OPT_MEMBER, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[4];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    int rc = nmo_cmd_ctx_parse_and_open(c, argc, argv, opts, OPT_COUNT, &r);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    group_list_data_t data = {0};
    const char *member = nmo_opt_str(&vals[OPT_MEMBER]);
    if (member != NULL) {
        nmo_object_t *obj = NULL;
        rc = group_resolve_object(c, member, NMO_CID_OBJECT, "Object", &obj, &data.member);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return rc;
        }
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    data.groups = rec != NULL ? nmo_cli_record_array(rec, "groups", NULL) : NULL;
    bool ok = data.groups != NULL &&
              nmo_cli_record_array_set_table(
                  data.groups, group_list_columns,
                  sizeof(group_list_columns) / sizeof(group_list_columns[0]));
    if (!ok) {
        nmo_cli_record_free(rec);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    nmo_object_query_t query = {0};
    nmo_core_query_set_class_id(&query, NMO_CID_GROUP, false);
    rc = nmo_core_object_query_run(c, &query, group_list_visitor, &data, NULL);
    if (rc != NMO_CLI_EXIT_SUCCESS || data.failed) {
        nmo_cli_record_free(rec);
        return rc != NMO_CLI_EXIT_SUCCESS ? rc : NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    ok = nmo_cli_record_uint(rec, "count", NULL, data.count) &&
         nmo_cli_record_raw_fmt(rec, "Groups: %zu\n\n", data.count);
    if (!ok) {
        nmo_cli_record_free(rec);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return nmo_cmd_ctx_emit_record(c, rec, "group.list", 0, c->colorize);
}

int nmo_cmd_group_list(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    nmo_cmd_ctx_t c;
    nmo_cmd_ctx_begin(&c, global);
    return nmo_cmd_ctx_done(&c, group_list_run(&c, argc, argv));
}

/* ============================================================================
 * group show
 * ============================================================================ */

static int group_show_run(nmo_cmd_ctx_t *c, int argc, char **argv)
{
    static const nmo_opt_def_t opts[] = {
        {"--id",   NULL, NMO_OPT_UINT,   "Group object ID"},
        {"--name", "-n", NMO_OPT_STRING, "Group name"},
    };
    enum { OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[4];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    /* A file command's last positional is the file; a session has none. */
    bool file_command = c->state == NMO_CMD_CTX_PENDING_FILE;
    int rc = nmo_cmd_ctx_parse_and_open(c, argc, argv, opts, OPT_COUNT, &r);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    size_t selector_args = file_command && r.pos_count > 0 ? r.pos_count - 1 : r.pos_count;
    nmo_core_object_selector_t selector = {
        .has_id = vals[OPT_ID].present,
        .id = nmo_opt_uint_or(&vals[OPT_ID], 0),
        .positional_id = selector_args > 0 ? r.pos_args[0] : NULL,
        .name = nmo_opt_str(&vals[OPT_NAME]),
        .required_base_class = NMO_CID_GROUP,
        .selector_label = "Group",
        .type_label = "CKGroup",
    };
    if (selector.positional_id != NULL && !group_text_is_id(selector.positional_id)) {
        selector.name = selector.positional_id;
        selector.positional_id = NULL;
    }
    nmo_object_t *group = NULL;
    nmo_object_id_t group_id = 0;
    rc = nmo_core_resolve_one_object(c, &selector, &group, &group_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo group show [--id <id> | --name <name> | <id or name>] <file>\n");
        return rc;
    }
    const nmo_group_state_t *state = group_state(group);
    if (state == NULL) {
        fprintf(stderr, "Error: Group #%u has no state\n", group_id);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    const char *name = nmo_object_get_name(group);
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "id", NULL, group_id) &&
              nmo_cli_record_str(rec, "name", NULL, name ? name : "") &&
              nmo_cli_record_uint(rec, "member_count", NULL, state->object_ids.count) &&
              nmo_cli_record_raw_fmt(rec, "Group #%u (%s): %zu members\n\n",
                                     group_id, name ? name : "", state->object_ids.count);
    nmo_cli_record_array_t *members = ok ? nmo_cli_record_array(rec, "members", NULL) : NULL;
    ok = ok && members != NULL &&
         nmo_cli_record_array_set_table(
             members, group_member_columns,
             sizeof(group_member_columns) / sizeof(group_member_columns[0]));
    for (size_t i = 0; ok && i < state->object_ids.count; ++i) {
        nmo_cli_record_t *member = group_member_record_new(c, group_member_id(state, i));
        ok = member != NULL && nmo_cli_record_array_add(members, member);
        if (!ok) {
            nmo_cli_record_free(member);
        }
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return nmo_cmd_ctx_emit_record(c, rec, "group.show", 0, c->colorize);
}

int nmo_cmd_group_show(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    nmo_cmd_ctx_t c;
    nmo_cmd_ctx_begin(&c, global);
    return nmo_cmd_ctx_done(&c, group_show_run(&c, argc, argv));
}

/* ============================================================================
 * group add / group remove
 * ============================================================================ */

typedef struct group_change_args {
    bool remove;
    bool create;
    const char *group_name;
    bool has_group_id;
    nmo_object_id_t group_id;
    const char *const *members;
    size_t member_count;
    /* Results */
    nmo_object_id_t resolved_group;
    const char *resolved_name;
    bool created;
    nmo_object_id_t *changed;
    size_t changed_count;
    size_t unchanged_count;
} group_change_args_t;

/* The group by id or name; with --create, a missing named group is made. */
static int group_change_find_group(nmo_cmd_ctx_t *c,
                                   group_change_args_t *args,
                                   nmo_workspace_edit_t *edit)
{
    if (args->create && !args->has_group_id) {
        nmo_object_t *existing = NULL;
        if (nmo_core_find_by_name(c, args->group_name, &existing) != NMO_CLI_EXIT_SUCCESS ||
            existing == NULL) {
            nmo_status_t st = nmo_object_edit_create(
                edit,
                &(nmo_object_create_desc_t){
                    .class_id = NMO_CID_GROUP,
                    .name = args->group_name,
                    .type_guid = NMO_GUID_NULL,
                },
                &args->resolved_group);
            if (st == NMO_OK) {
                /* CKGroup::Save writes the member list even when empty. */
                st = nmo_object_edit_set_fields(
                    edit,
                    args->resolved_group,
                    &(nmo_session_field_edit_t){
                        .field_name = "has_group_data",
                        .value_str = "1",
                    },
                    1u,
                    NULL);
            }
            if (st != NMO_OK) {
                fprintf(stderr, "Error: Cannot create group '%s': %s\n",
                        args->group_name, nmo_error_string(st));
                return NMO_CLI_EXIT_INTERNAL_ERROR;
            }
            args->created = true;
            args->resolved_name = args->group_name;
            return NMO_CLI_EXIT_SUCCESS;
        }
    }
    nmo_core_object_selector_t selector = {
        .has_id = args->has_group_id,
        .id = args->group_id,
        .name = args->has_group_id ? NULL : args->group_name,
        .required_base_class = NMO_CID_GROUP,
        .selector_label = "Group",
        .type_label = "CKGroup",
    };
    nmo_object_t *group = NULL;
    int rc = nmo_core_resolve_one_object(c, &selector, &group, &args->resolved_group);
    if (rc == NMO_CLI_EXIT_SUCCESS) {
        args->resolved_name = nmo_object_get_name(group);
    }
    return rc;
}

static int group_change_mutate(nmo_cmd_ctx_t *c, bool dry_run, const char *output_path, void *user_data)
{
    (void)output_path;
    group_change_args_t *args = (group_change_args_t *)user_data;
    nmo_workspace_edit_t *edit = NULL;
    nmo_status_t st = nmo_workspace_edit_begin(
        c->workspace, args->remove ? "group remove" : "group add", &edit);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Cannot begin edit: %s\n", nmo_error_string(st));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    int rc = group_change_find_group(c, args, edit);
    args->changed = rc == NMO_CLI_EXIT_SUCCESS
        ? (nmo_object_id_t *)calloc(args->member_count, sizeof(*args->changed))
        : NULL;
    if (rc == NMO_CLI_EXIT_SUCCESS && args->changed == NULL) {
        rc = NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    for (size_t i = 0; rc == NMO_CLI_EXIT_SUCCESS && i < args->member_count; ++i) {
        nmo_object_t *member = NULL;
        nmo_object_id_t member_id = 0;
        rc = group_resolve_object(c, args->members[i], NMO_CID_BEOBJECT, "Member",
                                  &member, &member_id);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            break;
        }
        bool held = group_has_member(
            group_state(nmo_core_find_by_id(c, args->resolved_group)), member_id);
        if (held != args->remove) {
            args->unchanged_count++;
            continue;
        }
        st = args->remove
            ? nmo_object_edit_remove_group_member(edit, args->resolved_group, member_id)
            : nmo_object_edit_add_group_member(edit, args->resolved_group, member_id);
        if (st != NMO_OK) {
            fprintf(stderr, "Error: Cannot %s '%s': %s\n",
                    args->remove ? "remove" : "add", args->members[i], nmo_error_string(st));
            rc = st == NMO_ERR_INVALID_ARGUMENT ? NMO_CLI_EXIT_ARG_ERROR : NMO_CLI_EXIT_INTERNAL_ERROR;
            break;
        }
        args->changed[args->changed_count++] = member_id;
    }
    if (rc != NMO_CLI_EXIT_SUCCESS || dry_run) {
        nmo_workspace_edit_rollback(edit);
        return rc;
    }
    st = nmo_workspace_edit_commit(edit);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to commit edit: %s\n", nmo_error_string(st));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int group_change_report(nmo_cmd_ctx_t *c, bool dry_run, const char *output_path, void *user_data)
{
    group_change_args_t *args = (group_change_args_t *)user_data;
    const char *verb = args->remove ? "Removed" : "Added";
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "group_id", NULL, args->resolved_group) &&
              nmo_cli_record_str(rec, "group", NULL, args->resolved_name ? args->resolved_name : "") &&
              nmo_cli_record_bool(rec, "created", NULL, args->created) &&
              nmo_cli_record_bool(rec, "dry_run", NULL, dry_run) &&
              nmo_cli_record_uint(rec, "unchanged", NULL, args->unchanged_count) &&
              (args->created && dry_run
                   ? nmo_cli_record_raw_fmt(rec, "Group '%s' (new)\n", args->resolved_name)
                   : nmo_cli_record_raw_fmt(rec, "Group #%u (%s)\n", args->resolved_group,
                                            args->resolved_name ? args->resolved_name : "")) &&
              nmo_cli_record_raw_fmt(rec, "%s %zu member(s)", verb, args->changed_count) &&
              (args->unchanged_count > 0
                   ? nmo_cli_record_raw_fmt(rec, args->remove ? ", %zu not in the group"
                                                              : ", %zu already in it",
                                            args->unchanged_count)
                   : true) &&
              nmo_cli_record_raw(rec, "\n");
    nmo_cli_record_array_t *changed =
        ok ? nmo_cli_record_array(rec, args->remove ? "removed" : "added", NULL) : NULL;
    ok = ok && changed != NULL &&
         nmo_cli_record_array_set_table(
             changed, group_member_columns,
             sizeof(group_member_columns) / sizeof(group_member_columns[0]));
    for (size_t i = 0; ok && i < args->changed_count; ++i) {
        nmo_cli_record_t *member = group_member_record_new(c, args->changed[i]);
        ok = member != NULL && nmo_cli_record_array_add(changed, member);
        if (!ok) {
            nmo_cli_record_free(member);
        }
    }
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
    return nmo_cmd_ctx_emit_record(c, rec, args->remove ? "group.remove" : "group.add",
                                   0, c->colorize);
}

static int group_change(int argc, char **argv, const nmo_cli_global_opts_t *global, bool remove)
{
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_WRITE_OUTPUT,
        NMO_OPT_DEF_DRY_RUN,
        {"--id",     NULL, NMO_OPT_UINT,   "Group object ID"},
        {"--name",   "-n", NMO_OPT_STRING, "Group name"},
        {"--create", NULL, NMO_OPT_FLAG,   "Make the named group when the file has none"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_ID, OPT_NAME, OPT_CREATE, OPT_COUNT };
    const char *usage = remove
        ? "Usage: nmo group remove (--name <group> | --id <id>) <member>... <file> -o <output>\n"
        : "Usage: nmo group add (--name <group> | --id <id>) [--create] <member>... <file> -o <output>\n";

    nmo_cli_global_opts_t local_global = *global;
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[256];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_cli_parse_args(argc, argv, opts, remove ? OPT_CREATE : OPT_COUNT, &r,
                           &local_global) != NMO_CLI_EXIT_SUCCESS) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!vals[OPT_ID].present && nmo_opt_str(&vals[OPT_NAME]) == NULL) {
        fprintf(stderr, "Error: No group specified\n%s", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 2) {
        fprintf(stderr, "Error: No members or no input file specified\n%s", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    group_change_args_t args = {
        .remove = remove,
        .create = !remove && nmo_opt_flag(&vals[OPT_CREATE]),
        .group_name = nmo_opt_str(&vals[OPT_NAME]),
        .has_group_id = vals[OPT_ID].present,
        .group_id = nmo_opt_uint_or(&vals[OPT_ID], 0),
        .members = r.pos_args,
        .member_count = r.pos_count - 1,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = remove ? "group.remove" : "group.add",
        .output_required_unless_dry_run = true,
    };
    int rc = nmo_cli_run_write_command(
        r.pos_args[r.pos_count - 1],
        nmo_opt_str(&vals[OPT_OUTPUT]),
        nmo_opt_flag(&vals[OPT_DRYRUN]),
        &local_global,
        &spec,
        group_change_mutate,
        group_change_report,
        &args);
    free(args.changed);
    return rc;
}

int nmo_cmd_group_add(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    return group_change(argc, argv, global, false);
}

int nmo_cmd_group_remove(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    return group_change(argc, argv, global, true);
}
