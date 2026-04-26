/**
 * @file nmo_cmd_patch.c
 * @brief Strict patch apply/diff commands.
 */

#include "nmo_cmd_patch.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_json.h"
#include "../nmo_edit_report_json.h"
#include "../nmo_cli_write.h"
#include "../nmo_opt.h"

#include "behavior/nmo_behavior_edit.h"
#include "behavior/nmo_edit_plan.h"
#include "behavior/nmo_edit_plan_json.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "runtime/nmo_context.h"
#include "yyjson.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct patch_plan {
    yyjson_doc *doc;
    nmo_edit_plan_manifest_t manifest;
    bool owns_manifest;
    uint32_t version;
    const char *input;
    const char *output;
    nmo_edit_plan_t *edit_plan;
} patch_plan_t;

static void patch_add_normalized_manifest_json(yyjson_mut_doc *doc,
                                               yyjson_mut_val *data,
                                               const patch_plan_t *plan) {
    if (!doc || !data || !plan || !plan->edit_plan) {
        return;
    }

    char *json = NULL;
    if (nmo_edit_plan_manifest_json_write(
            plan->edit_plan, plan->input, plan->output, &json) != NMO_OK ||
        json == NULL) {
        return;
    }

    yyjson_doc *manifest_doc = yyjson_read(json, strlen(json), 0);
    nmo_edit_plan_manifest_json_free(json);
    if (manifest_doc == NULL) {
        return;
    }

    yyjson_val *manifest_root = yyjson_doc_get_root(manifest_doc);
    yyjson_mut_val *manifest = yyjson_val_mut_copy(doc, manifest_root);
    yyjson_doc_free(manifest_doc);
    if (manifest != NULL) {
        yyjson_mut_obj_add_val(doc, data, "manifest", manifest);
    }
}

static void patch_add_edit_report_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *data,
    const patch_plan_t *plan,
    nmo_edit_report_t *report,
    bool dry_run) {
    if (plan != NULL && plan->output != NULL && report != NULL &&
        report->output_path == NULL) {
        (void)nmo_edit_report_set_output_path(report, plan->output);
    }
    nmo_cli_edit_report_add_schema_v2_json(doc, data, report, dry_run);
    if (plan != NULL) {
        nmo_cli_json_add_str_safe(doc, data, "input", plan->input);
        nmo_cli_json_add_str_safe(doc, data, "output", plan->output);
        patch_add_normalized_manifest_json(doc, data, plan);
    }
}

static void patch_plan_free(patch_plan_t *plan) {
    if (!plan) {
        return;
    }
    if (plan->owns_manifest) {
        nmo_edit_plan_manifest_dispose(&plan->manifest);
    } else {
        nmo_edit_plan_destroy(plan->edit_plan);
    }
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
                                  nmo_edit_plan_t *edit_plan) {
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

    nmo_behavior_replace_bb_desc_t desc = {0};
    desc.behavior_id = (nmo_object_id_t)yyjson_get_uint(id_val);
    desc.name = name;
    desc.block_guid = guid;
    desc.block_version = version;
    desc.preserve_links = patch_optional_bool(op_obj, "preserve_links", false);
    desc.preserve_params =
        patch_optional_bool(op_obj, "preserve_params", false);

    nmo_status_t st = nmo_edit_plan_add_replace_bb(edit_plan, &desc);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_fold(yyjson_val *op_obj,
                            nmo_edit_plan_t *edit_plan) {
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

    nmo_object_id_t *node_ids = NULL;
    size_t node_count = 0;
    nmo_behavior_fold_map_t *input_maps = NULL;
    size_t input_map_count = 0;
    nmo_behavior_fold_map_t *output_maps = NULL;
    size_t output_map_count = 0;
    nmo_behavior_fold_map_t *parameter_maps = NULL;
    size_t parameter_map_count = 0;
    nmo_behavior_fold_interface_mode_t interface_mode =
        NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE;

    rc = patch_parse_id_array(yyjson_obj_get(op_obj, "nodes"),
                              &node_ids,
                              &node_count,
                              "fold operation");
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        goto cleanup;
    }
    rc = patch_parse_fold_maps(yyjson_obj_get(op_obj, "inputs"),
                               NMO_BEHAVIOR_FOLD_MAP_INPUT,
                               &input_maps,
                               &input_map_count,
                               "fold inputs");
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        goto cleanup;
    }
    rc = patch_parse_fold_maps(yyjson_obj_get(op_obj, "outputs"),
                               NMO_BEHAVIOR_FOLD_MAP_OUTPUT,
                               &output_maps,
                               &output_map_count,
                               "fold outputs");
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        goto cleanup;
    }
    rc = patch_parse_fold_maps(yyjson_obj_get(op_obj, "parameters"),
                               NMO_BEHAVIOR_FOLD_MAP_PARAMETER,
                               &parameter_maps,
                               &parameter_map_count,
                               "fold parameters");
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        goto cleanup;
    }
    rc = patch_parse_interface_mode(op_obj, &interface_mode);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        goto cleanup;
    }

    nmo_object_id_t anchor_id = 0;
    yyjson_val *anchor_val = yyjson_obj_get(op_obj, "anchor");
    if (anchor_val) {
        if (!yyjson_is_uint(anchor_val) ||
            yyjson_get_uint(anchor_val) > UINT32_MAX) {
            fprintf(stderr, "Error: Invalid fold anchor\n");
            rc = NMO_CLI_EXIT_ARG_ERROR;
            goto cleanup;
        }
        anchor_id = (nmo_object_id_t)yyjson_get_uint(anchor_val);
    }

    nmo_behavior_fold_desc_t desc = {0};
    desc.parent_id = (nmo_object_id_t)yyjson_get_uint(parent_val);
    desc.node_ids = node_ids;
    desc.node_count = node_count;
    desc.anchor_id = anchor_id;
    desc.block_guid = guid;
    desc.name = name;
    desc.block_version = version;
    desc.preserve_boundary = yyjson_get_bool(preserve_val);
    desc.input_maps = input_maps;
    desc.input_map_count = input_map_count;
    desc.output_maps = output_maps;
    desc.output_map_count = output_map_count;
    desc.parameter_maps = parameter_maps;
    desc.parameter_map_count = parameter_map_count;
    desc.interface_mode = interface_mode;

    nmo_status_t st = nmo_edit_plan_add_fold(edit_plan, &desc);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        rc = st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
        goto cleanup;
    }
    rc = NMO_CLI_EXIT_SUCCESS;

cleanup:
    free(node_ids);
    free(input_maps);
    free(output_maps);
    free(parameter_maps);
    return rc;
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
        yyjson_get_uint(version) > UINT32_MAX) {
        fprintf(stderr, "Error: Patch version must be 1 or 2\n");
        patch_plan_free(out_plan);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    out_plan->version = (uint32_t)yyjson_get_uint(version);
    if (out_plan->version != 1u && out_plan->version != 2u) {
        fprintf(stderr, "Error: Patch version must be 1 or 2\n");
        patch_plan_free(out_plan);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (out_plan->version == 2u) {
        char *manifest_json = yyjson_val_write(root, 0, NULL);
        if (manifest_json == NULL) {
            fprintf(stderr, "Error: Out of memory\n");
            patch_plan_free(out_plan);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        nmo_status_t read_status = nmo_edit_plan_manifest_json_read(
            manifest_json, strlen(manifest_json), &out_plan->manifest);
        free(manifest_json);
        if (read_status != NMO_OK) {
            const char *message = nmo_last_error_message();
            fprintf(stderr, "Error: %s\n",
                    message != NULL && message[0] != '\0'
                        ? message
                        : nmo_error_string(read_status));
            patch_plan_free(out_plan);
            return read_status == NMO_ERR_NOMEM
                ? NMO_CLI_EXIT_INTERNAL_ERROR
                : NMO_CLI_EXIT_ARG_ERROR;
        }
        out_plan->input = out_plan->manifest.input_path;
        out_plan->output = out_plan->manifest.output_path;
        out_plan->edit_plan = out_plan->manifest.plan;
        out_plan->owns_manifest = true;
        if (out_plan->input == NULL || out_plan->output == NULL ||
            out_plan->edit_plan == NULL) {
            fprintf(stderr, "Error: Patch root requires input and output\n");
            patch_plan_free(out_plan);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        return NMO_CLI_EXIT_SUCCESS;
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

    nmo_status_t st = nmo_edit_plan_create(&out_plan->edit_plan);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Out of memory\n");
        patch_plan_free(out_plan);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

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
            rc = patch_parse_replace_bb(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "fold") == 0) {
            rc = patch_parse_fold(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else {
            fprintf(stderr, "Error: Unsupported patch op '%s'\n", op);
            patch_plan_free(out_plan);
            return NMO_CLI_EXIT_ARG_ERROR;
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

    if (emit_diff) {
        nmo_edit_report_t edit_report;
        nmo_status_t report_rc = nmo_edit_report_init(&edit_report);
        if (report_rc != NMO_OK) {
            return nmo_cmd_ctx_done(&ctx, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        nmo_edit_executor_options_t options =
            nmo_edit_executor_options_default();
        options.dry_run = true;
        nmo_status_t st = nmo_edit_executor_execute(
            ctx.workspace, plan->edit_plan, &options, &edit_report);
        if (st != NMO_OK) {
            fprintf(stderr, "Error: patch diff failed: %s\n",
                    nmo_error_string(st));
            nmo_edit_report_dispose(&edit_report);
            int exit_code = (st == NMO_ERR_INVALID_ARGUMENT ||
                             st == NMO_ERR_NOT_FOUND)
                ? NMO_CLI_EXIT_ARG_ERROR
                : NMO_CLI_EXIT_INTERNAL_ERROR;
            return nmo_cmd_ctx_done(&ctx, exit_code);
        }

        if (ctx.is_json) {
            yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&ctx);
            if (!doc) {
                nmo_edit_report_dispose(&edit_report);
                return nmo_cmd_ctx_done(&ctx,
                                        NMO_CLI_EXIT_INTERNAL_ERROR);
            }
            yyjson_mut_val *data = yyjson_mut_obj(doc);
            patch_add_edit_report_json(doc, data, plan, &edit_report, true);
            int json_rc = nmo_cmd_ctx_json_end(&ctx, doc, data,
                                               "patch.diff");
            nmo_edit_report_dispose(&edit_report);
            return json_rc;
        }

        for (size_t i = 0; i < edit_report.operation_count; ++i) {
            const nmo_edit_operation_result_t *op = &edit_report.operations[i];
            fprintf(ctx.out, "%s #%u: result #%u, status %s\n",
                    nmo_cli_edit_report_op_kind_string(op->kind),
                    op->primary_id,
                    op->result_id,
                    nmo_error_string(op->status));
        }
        nmo_edit_report_dispose(&edit_report);
        return nmo_cmd_ctx_done(&ctx, NMO_CLI_EXIT_SUCCESS);
    }

    nmo_edit_report_t edit_report;
    nmo_status_t report_rc = nmo_edit_report_init(&edit_report);
    if (report_rc != NMO_OK) {
        return nmo_cmd_ctx_done(&ctx, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    nmo_edit_executor_options_t options =
        nmo_edit_executor_options_default();
    options.dry_run = dry_run;
    nmo_status_t st = nmo_edit_executor_execute(
        ctx.workspace, plan->edit_plan, &options, &edit_report);
    if (st != NMO_OK) {
        int exit_code = (st == NMO_ERR_INVALID_ARGUMENT ||
                         st == NMO_ERR_NOT_FOUND)
            ? NMO_CLI_EXIT_ARG_ERROR
            : NMO_CLI_EXIT_INTERNAL_ERROR;
        if (ctx.is_json) {
            yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&ctx);
            if (!doc) {
                nmo_edit_report_dispose(&edit_report);
                return nmo_cmd_ctx_done(&ctx,
                                        NMO_CLI_EXIT_INTERNAL_ERROR);
            }
            yyjson_mut_val *data = yyjson_mut_obj(doc);
            patch_add_edit_report_json(doc, data, plan, &edit_report,
                                       dry_run);
            int json_rc = nmo_cmd_ctx_json_end(&ctx, doc, data,
                                               "patch.apply");
            nmo_edit_report_dispose(&edit_report);
            (void)json_rc;
            return nmo_cmd_ctx_done(&ctx, exit_code);
        }
        size_t failed_index = 0;
        for (size_t i = 0; i < edit_report.operation_count; ++i) {
            if (edit_report.operations[i].status != NMO_OK) {
                failed_index = i;
                break;
            }
        }
        const nmo_edit_op_t *edit_op =
            nmo_edit_plan_get(plan->edit_plan, failed_index);
        const nmo_edit_operation_result_t *failed_op =
            failed_index < edit_report.operation_count
                ? &edit_report.operations[failed_index]
                : NULL;
        if (edit_op) {
            fprintf(stderr, "Error: %s #%u failed: %s",
                    nmo_cli_edit_report_op_kind_string(edit_op->kind),
                    edit_op->primary_id,
                    nmo_error_string(st));
            if (failed_op && failed_op->diagnostic_code) {
                fprintf(stderr, " (%s)", failed_op->diagnostic_code);
            }
            if (failed_op && failed_op->diagnostic_message) {
                fprintf(stderr, ": %s", failed_op->diagnostic_message);
            }
        } else {
            fprintf(stderr, "Error: patch operation failed: %s",
                    nmo_error_string(st));
        }
        fputc('\n', stderr);
        nmo_edit_report_dispose(&edit_report);
        return nmo_cmd_ctx_done(&ctx, exit_code);
    }

    if (!dry_run) {
        nmo_save_options_t save_opts = nmo_tool_owner_save_options_default();
        int save_rc = nmo_cli_save_document(ctx.document, plan->output,
                                           &save_opts);
        if (save_rc != NMO_CLI_EXIT_SUCCESS) {
            nmo_edit_report_dispose(&edit_report);
            return nmo_cmd_ctx_done(&ctx, save_rc);
        }
    }

    if (ctx.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&ctx);
        if (!doc) {
            nmo_edit_report_dispose(&edit_report);
            return nmo_cmd_ctx_done(&ctx, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        patch_add_edit_report_json(doc, data, plan, &edit_report, dry_run);
        int json_rc = nmo_cmd_ctx_json_end(&ctx, doc, data, "patch.apply");
        nmo_edit_report_dispose(&edit_report);
        return json_rc;
    }

    if (dry_run) {
        fprintf(ctx.out, "[dry-run] Applied %zu operation(s)\n",
                nmo_edit_plan_count(plan->edit_plan));
    } else {
        fprintf(ctx.out, "Applied %zu operation(s)\n",
                nmo_edit_plan_count(plan->edit_plan));
        fprintf(ctx.out, "Saved to: %s\n", plan->output);
    }
    nmo_edit_report_dispose(&edit_report);
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

