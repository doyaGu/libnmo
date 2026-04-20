/**
 * @file nmo_cmd_patch.c
 * @brief Strict patch apply/diff commands.
 */

#include "nmo_cmd_patch.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_json.h"
#include "../nmo_cli_write.h"
#include "../nmo_opt.h"

#include "behavior/nmo_behavior_rewrite.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "yyjson.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum patch_operation_kind {
    PATCH_OP_REPLACE_BB = 1,
    PATCH_OP_FOLD = 2,
} patch_operation_kind_t;

typedef struct patch_operation {
    patch_operation_kind_t kind;
    nmo_behavior_replace_bb_desc_t replace_bb;
    nmo_behavior_rewrite_report_t report;
    nmo_behavior_fold_desc_t fold;
    nmo_object_id_t *fold_nodes;
    nmo_behavior_fold_map_t *fold_input_maps;
    nmo_behavior_fold_map_t *fold_output_maps;
    nmo_behavior_fold_map_t *fold_parameter_maps;
    nmo_behavior_fold_report_t fold_report;
} patch_operation_t;

typedef struct patch_plan {
    yyjson_doc *doc;
    const char *input;
    const char *output;
    patch_operation_t *operations;
    size_t operation_count;
} patch_plan_t;

static void patch_guid_to_string(nmo_guid_t guid, char *buf, size_t size) {
    if (!buf || size == 0) {
        return;
    }
    snprintf(buf, size, "%08X-%08X", guid.d1, guid.d2);
}

static void patch_plan_free(patch_plan_t *plan) {
    if (!plan) {
        return;
    }
    for (size_t i = 0; i < plan->operation_count; ++i) {
        patch_operation_t *op = &plan->operations[i];
        free(op->fold_nodes);
        free(op->fold_input_maps);
        free(op->fold_output_maps);
        free(op->fold_parameter_maps);
        nmo_behavior_fold_report_free(&op->fold_report);
    }
    free(plan->operations);
    if (plan->doc) {
        yyjson_doc_free(plan->doc);
    }
    memset(plan, 0, sizeof(*plan));
}

static bool patch_key_allowed(const char *key,
                              const char *const *allowed,
                              size_t allowed_count) {
    if (!key) {
        return false;
    }
    for (size_t i = 0; i < allowed_count; ++i) {
        if (strcmp(key, allowed[i]) == 0) {
            return true;
        }
    }
    return false;
}

static int patch_reject_unknown_fields(yyjson_val *obj,
                                       const char *where,
                                       const char *const *allowed,
                                       size_t allowed_count) {
    size_t idx;
    size_t max;
    yyjson_val *key;
    yyjson_val *val;
    yyjson_obj_foreach(obj, idx, max, key, val) {
        (void)val;
        const char *name = yyjson_get_str(key);
        if (!patch_key_allowed(name, allowed, allowed_count)) {
            fprintf(stderr, "Error: Unknown field '%s' in %s\n",
                    name ? name : "(null)", where);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static const char *patch_required_string(yyjson_val *obj,
                                         const char *key,
                                         const char *where) {
    yyjson_val *val = yyjson_obj_get(obj, key);
    const char *str = yyjson_get_str(val);
    if (!str || str[0] == '\0') {
        fprintf(stderr, "Error: Missing or invalid string '%s' in %s\n",
                key, where);
        return NULL;
    }
    return str;
}

static bool patch_optional_bool(yyjson_val *obj,
                                const char *key,
                                bool default_value) {
    yyjson_val *val = yyjson_obj_get(obj, key);
    if (!val) {
        return default_value;
    }
    return yyjson_is_bool(val) && yyjson_get_bool(val);
}

static bool patch_read_u32(yyjson_val *obj,
                           const char *key,
                           uint32_t *out_value) {
    yyjson_val *val = yyjson_obj_get(obj, key);
    if (!val || !yyjson_is_uint(val) || yyjson_get_uint(val) > UINT32_MAX) {
        return false;
    }
    *out_value = (uint32_t)yyjson_get_uint(val);
    return true;
}

static int patch_parse_id_array(yyjson_val *arr,
                                nmo_object_id_t **out_ids,
                                size_t *out_count,
                                const char *where) {
    if (!arr || !yyjson_is_arr(arr) || yyjson_arr_size(arr) == 0) {
        fprintf(stderr, "Error: Missing or invalid nodes in %s\n", where);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    size_t count = yyjson_arr_size(arr);
    nmo_object_id_t *ids =
        (nmo_object_id_t *)calloc(count, sizeof(*ids));
    if (!ids) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    size_t idx;
    size_t max;
    yyjson_val *item;
    yyjson_arr_foreach(arr, idx, max, item) {
        if (!yyjson_is_uint(item) || yyjson_get_uint(item) == 0 ||
            yyjson_get_uint(item) > UINT32_MAX) {
            fprintf(stderr, "Error: Invalid node id in %s\n", where);
            free(ids);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        ids[idx] = (nmo_object_id_t)yyjson_get_uint(item);
    }

    *out_ids = ids;
    *out_count = count;
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_fold_maps(yyjson_val *arr,
                                 nmo_behavior_fold_map_kind_t kind,
                                 nmo_behavior_fold_map_t **out_maps,
                                 size_t *out_count,
                                 const char *where) {
    *out_maps = NULL;
    *out_count = 0;
    if (!arr) {
        return NMO_CLI_EXIT_SUCCESS;
    }
    if (!yyjson_is_arr(arr)) {
        fprintf(stderr, "Error: %s must be an array\n", where);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    size_t count = yyjson_arr_size(arr);
    if (count == 0) {
        return NMO_CLI_EXIT_SUCCESS;
    }
    nmo_behavior_fold_map_t *maps =
        (nmo_behavior_fold_map_t *)calloc(count, sizeof(*maps));
    if (!maps) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    static const char *const allowed[] = {
        "old_index",
        "new_index",
        "old_io_id",
        "new_io_id",
        "old_parameter_id",
        "new_parameter_id",
        "label",
    };
    size_t idx;
    size_t max;
    yyjson_val *item;
    yyjson_arr_foreach(arr, idx, max, item) {
        if (!yyjson_is_obj(item) ||
            patch_reject_unknown_fields(
                item, where, allowed, sizeof(allowed) / sizeof(allowed[0])) !=
                NMO_CLI_EXIT_SUCCESS) {
            free(maps);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        uint32_t old_index = 0;
        uint32_t new_index = 0;
        if (!patch_read_u32(item, "old_index", &old_index) ||
            !patch_read_u32(item, "new_index", &new_index)) {
            fprintf(stderr,
                    "Error: %s entries require old_index and new_index\n",
                    where);
            free(maps);
            return NMO_CLI_EXIT_ARG_ERROR;
        }

        maps[idx].kind = kind;
        maps[idx].old_index = old_index;
        maps[idx].new_index = new_index;
        yyjson_val *old_id = yyjson_obj_get(item, "old_io_id");
        if (!old_id) {
            old_id = yyjson_obj_get(item, "old_parameter_id");
        }
        yyjson_val *new_id = yyjson_obj_get(item, "new_io_id");
        if (!new_id) {
            new_id = yyjson_obj_get(item, "new_parameter_id");
        }
        if (old_id &&
            (!yyjson_is_uint(old_id) ||
             yyjson_get_uint(old_id) > UINT32_MAX)) {
            fprintf(stderr, "Error: Invalid old id in %s\n", where);
            free(maps);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (new_id &&
            (!yyjson_is_uint(new_id) ||
             yyjson_get_uint(new_id) > UINT32_MAX)) {
            fprintf(stderr, "Error: Invalid new id in %s\n", where);
            free(maps);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (old_id) {
            maps[idx].old_id = (nmo_object_id_t)yyjson_get_uint(old_id);
        }
        if (new_id) {
            maps[idx].new_id = (nmo_object_id_t)yyjson_get_uint(new_id);
        }
        maps[idx].label = yyjson_get_str(yyjson_obj_get(item, "label"));
    }

    *out_maps = maps;
    *out_count = count;
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_interface_mode(
    yyjson_val *obj,
    nmo_behavior_fold_interface_mode_t *out_mode) {
    yyjson_val *val = yyjson_obj_get(obj, "interface");
    if (val && !yyjson_is_str(val)) {
        fprintf(stderr, "Error: Invalid fold interface mode\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    const char *text = val ? yyjson_get_str(val) : "preserve";
    if (strcmp(text, "preserve") == 0) {
        *out_mode = NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE;
        return NMO_CLI_EXIT_SUCCESS;
    }
    if (strcmp(text, "canonicalize") == 0) {
        *out_mode = NMO_BEHAVIOR_FOLD_INTERFACE_CANONICALIZE;
        return NMO_CLI_EXIT_SUCCESS;
    }
    if (strcmp(text, "remove") == 0) {
        *out_mode = NMO_BEHAVIOR_FOLD_INTERFACE_REMOVE;
        return NMO_CLI_EXIT_SUCCESS;
    }
    fprintf(stderr, "Error: Invalid fold interface mode '%s'\n", text);
    return NMO_CLI_EXIT_ARG_ERROR;
}

static int patch_parse_replace_bb(yyjson_val *op_obj,
                                  patch_operation_t *out_op) {
    static const char *const allowed[] = {
        "op",
        "behavior_id",
        "name",
        "guid",
        "version",
        "preserve_links",
        "preserve_params",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "replace_bb operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    yyjson_val *id_val = yyjson_obj_get(op_obj, "behavior_id");
    if (!id_val || !yyjson_is_uint(id_val) || yyjson_get_uint(id_val) == 0 ||
        yyjson_get_uint(id_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid behavior_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *name = patch_required_string(op_obj, "name",
                                             "replace_bb operation");
    const char *guid_str = patch_required_string(op_obj, "guid",
                                                 "replace_bb operation");
    if (!name || !guid_str) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_guid_t guid = nmo_guid_parse(guid_str);
    if (nmo_guid_is_null(guid)) {
        fprintf(stderr, "Error: Invalid GUID '%s'\n", guid_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t version = 65536u;
    yyjson_val *version_val = yyjson_obj_get(op_obj, "version");
    if (version_val) {
        if (!yyjson_is_uint(version_val) ||
            yyjson_get_uint(version_val) > UINT32_MAX) {
            fprintf(stderr, "Error: Invalid replace_bb version\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        version = (uint32_t)yyjson_get_uint(version_val);
    }

    memset(out_op, 0, sizeof(*out_op));
    out_op->kind = PATCH_OP_REPLACE_BB;
    out_op->replace_bb.behavior_id =
        (nmo_object_id_t)yyjson_get_uint(id_val);
    out_op->replace_bb.name = name;
    out_op->replace_bb.block_guid = guid;
    out_op->replace_bb.block_version = version;
    out_op->replace_bb.preserve_links =
        patch_optional_bool(op_obj, "preserve_links", false);
    out_op->replace_bb.preserve_params =
        patch_optional_bool(op_obj, "preserve_params", false);
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_fold(yyjson_val *op_obj,
                            patch_operation_t *out_op) {
    static const char *const allowed[] = {
        "op",
        "parent",
        "nodes",
        "anchor",
        "name",
        "guid",
        "version",
        "preserve_boundary",
        "inputs",
        "outputs",
        "parameters",
        "interface",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "fold operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    yyjson_val *parent_val = yyjson_obj_get(op_obj, "parent");
    if (!parent_val || !yyjson_is_uint(parent_val) ||
        yyjson_get_uint(parent_val) == 0 ||
        yyjson_get_uint(parent_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid fold parent\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *name = patch_required_string(op_obj, "name",
                                             "fold operation");
    const char *guid_str = patch_required_string(op_obj, "guid",
                                                 "fold operation");
    if (!name || !guid_str) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_guid_t guid = nmo_guid_parse(guid_str);
    if (nmo_guid_is_null(guid)) {
        fprintf(stderr, "Error: Invalid GUID '%s'\n", guid_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    yyjson_val *preserve_val = yyjson_obj_get(op_obj, "preserve_boundary");
    if (!preserve_val || !yyjson_is_bool(preserve_val)) {
        fprintf(stderr, "Error: fold preserve_boundary is required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t version = 65536u;
    yyjson_val *version_val = yyjson_obj_get(op_obj, "version");
    if (version_val) {
        if (!yyjson_is_uint(version_val) ||
            yyjson_get_uint(version_val) > UINT32_MAX) {
            fprintf(stderr, "Error: Invalid fold version\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        version = (uint32_t)yyjson_get_uint(version_val);
    }

    memset(out_op, 0, sizeof(*out_op));
    out_op->kind = PATCH_OP_FOLD;
    rc = patch_parse_id_array(yyjson_obj_get(op_obj, "nodes"),
                              &out_op->fold_nodes,
                              &out_op->fold.node_count,
                              "fold operation");
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    rc = patch_parse_fold_maps(yyjson_obj_get(op_obj, "inputs"),
                               NMO_BEHAVIOR_FOLD_MAP_INPUT,
                               &out_op->fold_input_maps,
                               &out_op->fold.input_map_count,
                               "fold inputs");
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    rc = patch_parse_fold_maps(yyjson_obj_get(op_obj, "outputs"),
                               NMO_BEHAVIOR_FOLD_MAP_OUTPUT,
                               &out_op->fold_output_maps,
                               &out_op->fold.output_map_count,
                               "fold outputs");
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    rc = patch_parse_fold_maps(yyjson_obj_get(op_obj, "parameters"),
                               NMO_BEHAVIOR_FOLD_MAP_PARAMETER,
                               &out_op->fold_parameter_maps,
                               &out_op->fold.parameter_map_count,
                               "fold parameters");
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    rc = patch_parse_interface_mode(op_obj, &out_op->fold.interface_mode);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    nmo_object_id_t anchor_id = 0;
    yyjson_val *anchor_val = yyjson_obj_get(op_obj, "anchor");
    if (anchor_val) {
        if (!yyjson_is_uint(anchor_val) ||
            yyjson_get_uint(anchor_val) > UINT32_MAX) {
            fprintf(stderr, "Error: Invalid fold anchor\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        anchor_id = (nmo_object_id_t)yyjson_get_uint(anchor_val);
    }

    out_op->fold.parent_id = (nmo_object_id_t)yyjson_get_uint(parent_val);
    out_op->fold.node_ids = out_op->fold_nodes;
    out_op->fold.anchor_id = anchor_id;
    out_op->fold.block_guid = guid;
    out_op->fold.name = name;
    out_op->fold.block_version = version;
    out_op->fold.preserve_boundary = yyjson_get_bool(preserve_val);
    out_op->fold.input_maps = out_op->fold_input_maps;
    out_op->fold.output_maps = out_op->fold_output_maps;
    out_op->fold.parameter_maps = out_op->fold_parameter_maps;
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_plan(const char *path, patch_plan_t *out_plan) {
    if (!path || !out_plan) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    memset(out_plan, 0, sizeof(*out_plan));

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_file(path, 0, NULL, &err);
    if (!doc) {
        fprintf(stderr, "Error: Failed to read patch JSON '%s': %s\n",
                path, err.msg ? err.msg : "parse error");
        return NMO_CLI_EXIT_IO_ERROR;
    }
    out_plan->doc = doc;

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        fprintf(stderr, "Error: Patch root must be an object\n");
        patch_plan_free(out_plan);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    static const char *const root_allowed[] = {
        "version",
        "input",
        "output",
        "operations",
    };
    int rc = patch_reject_unknown_fields(
        root, "patch root",
        root_allowed, sizeof(root_allowed) / sizeof(root_allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        patch_plan_free(out_plan);
        return rc;
    }

    yyjson_val *version = yyjson_obj_get(root, "version");
    if (!version || !yyjson_is_uint(version) ||
        yyjson_get_uint(version) != 1) {
        fprintf(stderr, "Error: Patch version must be 1\n");
        patch_plan_free(out_plan);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    out_plan->input = patch_required_string(root, "input", "patch root");
    out_plan->output = patch_required_string(root, "output", "patch root");
    if (!out_plan->input || !out_plan->output) {
        patch_plan_free(out_plan);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    yyjson_val *ops = yyjson_obj_get(root, "operations");
    if (!ops || !yyjson_is_arr(ops) || yyjson_arr_size(ops) == 0) {
        fprintf(stderr, "Error: Patch operations must be a non-empty array\n");
        patch_plan_free(out_plan);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    size_t op_count = yyjson_arr_size(ops);
    patch_operation_t *operations =
        (patch_operation_t *)calloc(op_count, sizeof(*operations));
    if (!operations) {
        fprintf(stderr, "Error: Out of memory\n");
        patch_plan_free(out_plan);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    out_plan->operations = operations;
    out_plan->operation_count = op_count;

    size_t idx;
    size_t max;
    yyjson_val *op_obj;
    yyjson_arr_foreach(ops, idx, max, op_obj) {
        if (!yyjson_is_obj(op_obj)) {
            fprintf(stderr, "Error: Patch operation must be an object\n");
            patch_plan_free(out_plan);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        const char *op = patch_required_string(op_obj, "op",
                                               "patch operation");
        if (!op) {
            patch_plan_free(out_plan);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (strcmp(op, "replace_bb") == 0) {
            rc = patch_parse_replace_bb(op_obj, &operations[idx]);
        } else if (strcmp(op, "fold") == 0) {
            rc = patch_parse_fold(op_obj, &operations[idx]);
        } else {
            fprintf(stderr, "Error: Unsupported patch op '%s'\n", op);
            patch_plan_free(out_plan);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            patch_plan_free(out_plan);
            return rc;
        }
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_apply_plan(patch_plan_t *plan,
                            bool dry_run,
                            const nmo_cli_global_opts_t *global,
                            bool emit_diff) {
    nmo_cmd_ctx_t ctx;
    int rc = nmo_cli_write_init_ctx(&ctx, plan->input, global);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    for (size_t i = 0; i < plan->operation_count; ++i) {
        patch_operation_t *op = &plan->operations[i];
        nmo_status_t st = NMO_OK;
        if (op->kind == PATCH_OP_REPLACE_BB) {
            st = nmo_behavior_replace_bb(
                ctx.ctx, ctx.session, &op->replace_bb, &op->report);
        } else if (op->kind == PATCH_OP_FOLD) {
            st = dry_run || emit_diff
                ? nmo_behavior_fold_analyze(ctx.ctx, ctx.session,
                                            &op->fold,
                                            &op->fold_report)
                : nmo_behavior_fold(ctx.ctx, ctx.session,
                                    &op->fold,
                                    &op->fold_report);
        } else {
            return nmo_cmd_ctx_done(&ctx, NMO_CLI_EXIT_ARG_ERROR);
        }
        if (st != NMO_OK) {
            if (op->kind == PATCH_OP_REPLACE_BB) {
                fprintf(stderr,
                        "Error: replace_bb #%u is not leaf-replaceable "
                        "(sub_behaviors=%zu, sub_behavior_links=%zu, operations=%zu)",
                        op->replace_bb.behavior_id,
                        op->report.sub_behavior_count,
                        op->report.sub_behavior_link_count,
                        op->report.operation_count);
                if (op->report.diagnostic_message) {
                    fprintf(stderr, ": %s", op->report.diagnostic_message);
                }
            } else {
                fprintf(stderr, "Error: fold #%u rejected",
                        op->fold.parent_id);
                if (op->fold_report.diagnostic_code) {
                    fprintf(stderr, " (%s)",
                            op->fold_report.diagnostic_code);
                }
                if (op->fold_report.diagnostic_message) {
                    fprintf(stderr, ": %s",
                            op->fold_report.diagnostic_message);
                }
            }
            fputc('\n', stderr);
            int exit_code = (st == NMO_ERR_INVALID_ARGUMENT ||
                             st == NMO_ERR_NOT_FOUND)
                ? NMO_CLI_EXIT_ARG_ERROR
                : NMO_CLI_EXIT_INTERNAL_ERROR;
            return nmo_cmd_ctx_done(&ctx, exit_code);
        }
    }

    if (emit_diff) {
        for (size_t i = 0; i < plan->operation_count; ++i) {
            patch_operation_t *op = &plan->operations[i];
            if (op->kind == PATCH_OP_FOLD) {
                fprintf(ctx.out,
                        "fold #%u: anchor #%u, nodes=%zu, can_write=%s\n",
                        op->fold.parent_id,
                        op->fold_report.anchor_id,
                        op->fold_report.selected_node_count,
                        op->fold_report.can_write ? "yes" : "no");
                continue;
            }
            char before_guid[24];
            char after_guid[24];
            patch_guid_to_string(op->report.before_guid, before_guid,
                                 sizeof(before_guid));
            patch_guid_to_string(op->report.after_guid, after_guid,
                                 sizeof(after_guid));
            fprintf(ctx.out,
                    "replace_bb #%u: guid %s -> %s, name -> %s\n",
                    op->replace_bb.behavior_id,
                    before_guid,
                    after_guid,
                    op->replace_bb.name ? op->replace_bb.name : "");
        }
        return nmo_cmd_ctx_done(&ctx, NMO_CLI_EXIT_SUCCESS);
    }

    if (!dry_run) {
        nmo_save_options_t save_opts = nmo_save_options_default();
        int save_rc = nmo_cli_save_session(ctx.session, plan->output,
                                           &save_opts);
        if (save_rc != NMO_CLI_EXIT_SUCCESS) {
            return nmo_cmd_ctx_done(&ctx, save_rc);
        }
    }

    if (ctx.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&ctx);
        if (!doc) {
            return nmo_cmd_ctx_done(&ctx, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_str_safe(doc, data, "input", plan->input);
        nmo_cli_json_add_str_safe(doc, data, "output", plan->output);
        yyjson_mut_obj_add_uint(doc, data, "operation_count",
                                (uint64_t)plan->operation_count);
        yyjson_mut_val *ops = yyjson_mut_arr(doc);
        for (size_t i = 0; i < plan->operation_count; ++i) {
            patch_operation_t *op = &plan->operations[i];
            yyjson_mut_val *item = yyjson_mut_obj(doc);
            if (op->kind == PATCH_OP_FOLD) {
                nmo_cli_json_add_str_safe(doc, item, "op", "fold");
                yyjson_mut_obj_add_uint(doc, item, "parent",
                                        (uint64_t)op->fold.parent_id);
                yyjson_mut_obj_add_uint(doc, item, "anchor",
                                        (uint64_t)op->fold_report.anchor_id);
                yyjson_mut_obj_add_bool(doc, item, "can_write",
                                        op->fold_report.can_write);
                yyjson_mut_obj_add_bool(doc, item, "rejected",
                                        op->fold_report.rejected);
                yyjson_mut_obj_add_uint(
                    doc, item, "selected_node_count",
                    (uint64_t)op->fold_report.selected_node_count);
            } else {
                nmo_cli_json_add_str_safe(doc, item, "op", "replace_bb");
                yyjson_mut_obj_add_uint(
                    doc, item, "behavior_id",
                    (uint64_t)op->replace_bb.behavior_id);
                yyjson_mut_obj_add_bool(doc, item, "changed",
                                        op->report.changed);
            }
            yyjson_mut_arr_add_val(ops, item);
        }
        yyjson_mut_obj_add_val(doc, data, "operations", ops);
        return nmo_cmd_ctx_json_end(&ctx, doc, data, "patch.apply");
    }

    if (dry_run) {
        fprintf(ctx.out, "[dry-run] Applied %zu operation(s)\n",
                plan->operation_count);
    } else {
        fprintf(ctx.out, "Applied %zu operation(s)\n",
                plan->operation_count);
        fprintf(ctx.out, "Saved to: %s\n", plan->output);
    }
    return nmo_cmd_ctx_done(&ctx, NMO_CLI_EXIT_SUCCESS);
}

static int patch_parse_apply_args(int argc,
                                  char **argv,
                                  bool *out_dry_run,
                                  const char **out_patch_path) {
    static const nmo_opt_def_t opts[] = {
        {"--dry-run", NULL, NMO_OPT_FLAG, "Preview without saving"},
    };
    enum { OPT_DRY_RUN, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[4];
    nmo_opt_result_t r = {
        .vals = vals,
        .pos_args = pos,
        .pos_capacity = 4,
    };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0 ||
        r.pos_count != 1) {
        fprintf(stderr, "Usage: nmo patch apply <patch.json> [--dry-run]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    *out_dry_run = vals[OPT_DRY_RUN].present &&
                   vals[OPT_DRY_RUN].val.flag;
    *out_patch_path = r.pos_args[0];
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_patch_apply(int argc,
                        char **argv,
                        const nmo_cli_global_opts_t *global) {
    bool dry_run = false;
    const char *patch_path = NULL;
    int rc = patch_parse_apply_args(argc, argv, &dry_run, &patch_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    patch_plan_t plan;
    rc = patch_parse_plan(patch_path, &plan);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    rc = patch_apply_plan(&plan, dry_run, global, false);
    patch_plan_free(&plan);
    return rc;
}

int nmo_cmd_patch_diff(int argc,
                       char **argv,
                       const nmo_cli_global_opts_t *global) {
    if (argc != 2 || !argv || !argv[1]) {
        fprintf(stderr, "Usage: nmo patch diff <patch.json>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    patch_plan_t plan;
    int rc = patch_parse_plan(argv[1], &plan);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    rc = patch_apply_plan(&plan, true, global, true);
    patch_plan_free(&plan);
    return rc;
}
