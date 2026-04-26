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
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_parse.h"
#include "runtime/nmo_context.h"
#include "yyjson.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct patch_plan {
    yyjson_doc *doc;
    uint32_t version;
    const char *input;
    const char *output;
    nmo_edit_plan_t *edit_plan;
} patch_plan_t;

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
    }
}

static void patch_plan_free(patch_plan_t *plan) {
    if (!plan) {
        return;
    }
    nmo_edit_plan_destroy(plan->edit_plan);
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

static int patch_parse_add_io(yyjson_val *op_obj,
                              nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "behavior_id",
        "kind",
        "name",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "add_io operation",
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

    const char *kind_text = patch_required_string(
        op_obj, "kind", "add_io operation");
    const char *name = patch_required_string(
        op_obj, "name", "add_io operation");
    if (!kind_text || !name) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_script_edit_io_kind_t kind = NMO_SCRIPT_EDIT_IO_INPUT;
    if (strcmp(kind_text, "input") == 0) {
        kind = NMO_SCRIPT_EDIT_IO_INPUT;
    } else if (strcmp(kind_text, "output") == 0) {
        kind = NMO_SCRIPT_EDIT_IO_OUTPUT;
    } else {
        fprintf(stderr, "Error: Invalid add_io kind '%s'\n", kind_text);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_status_t st = nmo_edit_plan_add_io(
        edit_plan, (nmo_object_id_t)yyjson_get_uint(id_val), kind, name);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_add_node(yyjson_val *op_obj,
                                nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "behavior_id",
        "guid",
        "name",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "add_node operation",
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

    const char *guid_str = patch_required_string(
        op_obj, "guid", "add_node operation");
    const char *name = patch_required_string(
        op_obj, "name", "add_node operation");
    if (!guid_str || !name) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_guid_t guid = nmo_guid_parse(guid_str);
    if (nmo_guid_is_null(guid)) {
        fprintf(stderr, "Error: Invalid GUID '%s'\n", guid_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_status_t st = nmo_edit_plan_add_node(
        edit_plan, (nmo_object_id_t)yyjson_get_uint(id_val), guid, name);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_remove_node(yyjson_val *op_obj,
                                   nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "parent_id",
        "node_id",
        "delete_flags",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "remove_node operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    yyjson_val *parent_val = yyjson_obj_get(op_obj, "parent_id");
    if (!parent_val || !yyjson_is_uint(parent_val) ||
        yyjson_get_uint(parent_val) == 0 ||
        yyjson_get_uint(parent_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid parent_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    yyjson_val *node_val = yyjson_obj_get(op_obj, "node_id");
    if (!node_val || !yyjson_is_uint(node_val) ||
        yyjson_get_uint(node_val) == 0 ||
        yyjson_get_uint(node_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid node_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t delete_flags = 0u;
    yyjson_val *delete_flags_val = yyjson_obj_get(op_obj, "delete_flags");
    if (delete_flags_val != NULL) {
        if (!yyjson_is_uint(delete_flags_val) ||
            yyjson_get_uint(delete_flags_val) > UINT32_MAX) {
            fprintf(stderr, "Error: Invalid remove_node delete_flags\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        delete_flags = (uint32_t)yyjson_get_uint(delete_flags_val);
    }

    nmo_status_t st = nmo_edit_plan_add_remove_node(
        edit_plan,
        (nmo_object_id_t)yyjson_get_uint(parent_val),
        (nmo_object_id_t)yyjson_get_uint(node_val),
        delete_flags);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_add_behavior_link(yyjson_val *op_obj,
                                         nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "parent_id",
        "from_io_id",
        "to_io_id",
        "activation_delay",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "add_behavior_link operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    yyjson_val *parent_val = yyjson_obj_get(op_obj, "parent_id");
    if (!parent_val || !yyjson_is_uint(parent_val) ||
        yyjson_get_uint(parent_val) == 0 ||
        yyjson_get_uint(parent_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid parent_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    yyjson_val *from_val = yyjson_obj_get(op_obj, "from_io_id");
    if (!from_val || !yyjson_is_uint(from_val) ||
        yyjson_get_uint(from_val) == 0 ||
        yyjson_get_uint(from_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid from_io_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    yyjson_val *to_val = yyjson_obj_get(op_obj, "to_io_id");
    if (!to_val || !yyjson_is_uint(to_val) ||
        yyjson_get_uint(to_val) == 0 ||
        yyjson_get_uint(to_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid to_io_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t activation_delay = 0u;
    yyjson_val *delay_val = yyjson_obj_get(op_obj, "activation_delay");
    if (delay_val != NULL) {
        if (!yyjson_is_uint(delay_val) ||
            yyjson_get_uint(delay_val) > UINT32_MAX) {
            fprintf(stderr,
                    "Error: Invalid add_behavior_link activation_delay\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        activation_delay = (uint32_t)yyjson_get_uint(delay_val);
    }

    nmo_status_t st = nmo_edit_plan_add_behavior_link(
        edit_plan,
        (nmo_object_id_t)yyjson_get_uint(parent_val),
        (nmo_object_id_t)yyjson_get_uint(from_val),
        (nmo_object_id_t)yyjson_get_uint(to_val),
        activation_delay);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_rewire_behavior_link(yyjson_val *op_obj,
                                            nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "link_id",
        "from_io_id",
        "to_io_id",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "rewire_behavior_link operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    yyjson_val *link_val = yyjson_obj_get(op_obj, "link_id");
    if (!link_val || !yyjson_is_uint(link_val) ||
        yyjson_get_uint(link_val) == 0 ||
        yyjson_get_uint(link_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid link_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    yyjson_val *from_val = yyjson_obj_get(op_obj, "from_io_id");
    if (!from_val || !yyjson_is_uint(from_val) ||
        yyjson_get_uint(from_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid from_io_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    yyjson_val *to_val = yyjson_obj_get(op_obj, "to_io_id");
    if (!to_val || !yyjson_is_uint(to_val) ||
        yyjson_get_uint(to_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid to_io_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (yyjson_get_uint(from_val) == 0 && yyjson_get_uint(to_val) == 0) {
        fprintf(stderr,
                "Error: rewire_behavior_link requires from_io_id or to_io_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_status_t st = nmo_edit_plan_add_rewire_behavior_link(
        edit_plan,
        (nmo_object_id_t)yyjson_get_uint(link_val),
        (nmo_object_id_t)yyjson_get_uint(from_val),
        (nmo_object_id_t)yyjson_get_uint(to_val));
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_set_behavior_link_delay(yyjson_val *op_obj,
                                               nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "link_id",
        "activation_delay",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "set_behavior_link_delay operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    yyjson_val *link_val = yyjson_obj_get(op_obj, "link_id");
    if (!link_val || !yyjson_is_uint(link_val) ||
        yyjson_get_uint(link_val) == 0 ||
        yyjson_get_uint(link_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid link_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    yyjson_val *delay_val = yyjson_obj_get(op_obj, "activation_delay");
    if (!delay_val || !yyjson_is_uint(delay_val) ||
        yyjson_get_uint(delay_val) > UINT32_MAX) {
        fprintf(stderr,
                "Error: Missing or invalid set_behavior_link_delay activation_delay\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_status_t st = nmo_edit_plan_add_set_behavior_link_delay(
        edit_plan,
        (nmo_object_id_t)yyjson_get_uint(link_val),
        (uint32_t)yyjson_get_uint(delay_val));
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_remove_behavior_link(yyjson_val *op_obj,
                                            nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "parent_id",
        "link_id",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "remove_behavior_link operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    yyjson_val *parent_val = yyjson_obj_get(op_obj, "parent_id");
    if (!parent_val || !yyjson_is_uint(parent_val) ||
        yyjson_get_uint(parent_val) == 0 ||
        yyjson_get_uint(parent_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid parent_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    yyjson_val *link_val = yyjson_obj_get(op_obj, "link_id");
    if (!link_val || !yyjson_is_uint(link_val) ||
        yyjson_get_uint(link_val) == 0 ||
        yyjson_get_uint(link_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid link_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_status_t st = nmo_edit_plan_add_remove_behavior_link(
        edit_plan,
        (nmo_object_id_t)yyjson_get_uint(parent_val),
        (nmo_object_id_t)yyjson_get_uint(link_val));
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_remove_io(yyjson_val *op_obj,
                                 nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "io_id",
        "detach_links",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "remove_io operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    yyjson_val *id_val = yyjson_obj_get(op_obj, "io_id");
    if (!id_val || !yyjson_is_uint(id_val) || yyjson_get_uint(id_val) == 0 ||
        yyjson_get_uint(id_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid io_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_status_t st = nmo_edit_plan_add_remove_io(
        edit_plan,
        (nmo_object_id_t)yyjson_get_uint(id_val),
        patch_optional_bool(op_obj, "detach_links", false));
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_rename_io(yyjson_val *op_obj,
                                 nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "io_id",
        "name",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "rename_io operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    yyjson_val *id_val = yyjson_obj_get(op_obj, "io_id");
    if (!id_val || !yyjson_is_uint(id_val) || yyjson_get_uint(id_val) == 0 ||
        yyjson_get_uint(id_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid io_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *name = patch_required_string(
        op_obj, "name", "rename_io operation");
    if (!name) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_status_t st = nmo_edit_plan_add_rename_io(
        edit_plan, (nmo_object_id_t)yyjson_get_uint(id_val), name);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_script_interface_mode(
    yyjson_val *obj,
    nmo_script_edit_interface_mode_t *out_mode) {
    const char *text = patch_required_string(
        obj, "mode", "interface_policy operation");
    if (!text) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (strcmp(text, "preserve") == 0) {
        *out_mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE;
        return NMO_CLI_EXIT_SUCCESS;
    }
    if (strcmp(text, "canonicalize") == 0) {
        *out_mode = NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE;
        return NMO_CLI_EXIT_SUCCESS;
    }
    if (strcmp(text, "remove") == 0) {
        *out_mode = NMO_SCRIPT_EDIT_INTERFACE_REMOVE;
        return NMO_CLI_EXIT_SUCCESS;
    }
    fprintf(stderr, "Error: Invalid interface_policy mode '%s'\n", text);
    return NMO_CLI_EXIT_ARG_ERROR;
}

static bool patch_parse_parameter_kind(
    const char *text,
    nmo_script_edit_parameter_kind_t *out_kind) {
    if (!text || !out_kind) {
        return false;
    }
    if (strcmp(text, "in") == 0 || strcmp(text, "input") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_IN;
        return true;
    }
    if (strcmp(text, "out") == 0 || strcmp(text, "output") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_OUT;
        return true;
    }
    if (strcmp(text, "local") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_LOCAL;
        return true;
    }
    if (strcmp(text, "shared") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_SHARED;
        return true;
    }
    return false;
}

static int patch_parse_add_parameter(yyjson_val *op_obj,
                                     nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "owner_id",
        "kind",
        "type_guid",
        "name",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "add_parameter operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    yyjson_val *owner_val = yyjson_obj_get(op_obj, "owner_id");
    if (!owner_val || !yyjson_is_uint(owner_val) ||
        yyjson_get_uint(owner_val) == 0 ||
        yyjson_get_uint(owner_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid owner_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *kind_text = patch_required_string(
        op_obj, "kind", "add_parameter operation");
    const char *type_guid_text = patch_required_string(
        op_obj, "type_guid", "add_parameter operation");
    const char *name = patch_required_string(
        op_obj, "name", "add_parameter operation");
    if (!kind_text || !type_guid_text || !name) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_script_edit_parameter_kind_t kind = NMO_SCRIPT_EDIT_PARAM_IN;
    if (!patch_parse_parameter_kind(kind_text, &kind)) {
        fprintf(stderr, "Error: Invalid add_parameter kind '%s'\n", kind_text);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_guid_t type_guid = nmo_guid_parse(type_guid_text);
    if (nmo_guid_is_null(type_guid)) {
        fprintf(stderr, "Error: Invalid parameter type GUID '%s'\n",
                type_guid_text);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_status_t st = nmo_edit_plan_add_parameter(
        edit_plan,
        (nmo_object_id_t)yyjson_get_uint(owner_val),
        kind,
        type_guid,
        name);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_disconnect_parameter(yyjson_val *op_obj,
                                            nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "target_id",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "disconnect_parameter operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    yyjson_val *target_val = yyjson_obj_get(op_obj, "target_id");
    if (!target_val || !yyjson_is_uint(target_val) ||
        yyjson_get_uint(target_val) == 0 ||
        yyjson_get_uint(target_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid target_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_status_t st = nmo_edit_plan_add_disconnect_parameter(
        edit_plan, (nmo_object_id_t)yyjson_get_uint(target_val));
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_connect_parameter(yyjson_val *op_obj,
                                         nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "source_id",
        "target_id",
        "target_operation",
        "target_handle",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "connect_parameter operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    yyjson_val *source_val = yyjson_obj_get(op_obj, "source_id");
    if (!source_val || !yyjson_is_uint(source_val) ||
        yyjson_get_uint(source_val) == 0 ||
        yyjson_get_uint(source_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid source_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    yyjson_val *target_val = yyjson_obj_get(op_obj, "target_id");
    yyjson_val *target_operation_val =
        yyjson_obj_get(op_obj, "target_operation");
    yyjson_val *target_handle_val = yyjson_obj_get(op_obj, "target_handle");
    bool has_target_id = target_val != NULL;
    bool has_target_ref = target_operation_val != NULL || target_handle_val != NULL;
    nmo_status_t st = NMO_OK;
    if (has_target_id == has_target_ref) {
        fprintf(stderr,
                "Error: connect_parameter requires either target_id or "
                "target_operation plus target_handle\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (has_target_id) {
        if (!yyjson_is_uint(target_val) ||
            yyjson_get_uint(target_val) == 0 ||
            yyjson_get_uint(target_val) > UINT32_MAX) {
            fprintf(stderr, "Error: Missing or invalid target_id\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        st = nmo_edit_plan_add_connect_parameter(
            edit_plan,
            (nmo_object_id_t)yyjson_get_uint(source_val),
            (nmo_object_id_t)yyjson_get_uint(target_val));
    } else {
        if (!target_operation_val || !yyjson_is_uint(target_operation_val) ||
            yyjson_get_uint(target_operation_val) == 0) {
            fprintf(stderr, "Error: Missing or invalid target_operation\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (!target_handle_val || !yyjson_is_str(target_handle_val) ||
            yyjson_get_str(target_handle_val)[0] == '\0') {
            fprintf(stderr, "Error: Missing or invalid target_handle\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        st = nmo_edit_plan_add_connect_parameter_to_handle(
            edit_plan,
            (nmo_object_id_t)yyjson_get_uint(source_val),
            (size_t)(yyjson_get_uint(target_operation_val) - 1u),
            yyjson_get_str(target_handle_val));
    }
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_remove_parameter(yyjson_val *op_obj,
                                        nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "parameter_id",
        "detach",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "remove_parameter operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    yyjson_val *parameter_val = yyjson_obj_get(op_obj, "parameter_id");
    if (!parameter_val || !yyjson_is_uint(parameter_val) ||
        yyjson_get_uint(parameter_val) == 0 ||
        yyjson_get_uint(parameter_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid parameter_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_status_t st = nmo_edit_plan_add_remove_parameter(
        edit_plan,
        (nmo_object_id_t)yyjson_get_uint(parameter_val),
        patch_optional_bool(op_obj, "detach", false));
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_set_parameter_value(yyjson_val *op_obj,
                                           nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "parameter_id",
        "value",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "set_parameter_value operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    yyjson_val *parameter_val = yyjson_obj_get(op_obj, "parameter_id");
    if (!parameter_val || !yyjson_is_uint(parameter_val) ||
        yyjson_get_uint(parameter_val) == 0 ||
        yyjson_get_uint(parameter_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid parameter_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *value = patch_required_string(
        op_obj, "value", "set_parameter_value operation");
    if (!value) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_status_t st = nmo_edit_plan_add_set_parameter_value(
        edit_plan, (nmo_object_id_t)yyjson_get_uint(parameter_val), value, NULL);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_set_parameter_bytes(yyjson_val *op_obj,
                                           nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "parameter_id",
        "hex",
        "resize",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "set_parameter_bytes operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    uint32_t parameter_id = 0u;
    if (!patch_read_u32(op_obj, "parameter_id", &parameter_id) ||
        parameter_id == 0u) {
        fprintf(stderr, "Error: Missing or invalid parameter_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *hex = patch_required_string(
        op_obj, "hex", "set_parameter_bytes operation");
    if (!hex) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    size_t byte_capacity = strlen(hex) / 2u + 1u;
    uint8_t *bytes = (uint8_t *)malloc(byte_capacity);
    if (bytes == NULL) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    size_t byte_count = 0u;
    if (nmo_parse_hex_bytes(hex, bytes, byte_capacity, &byte_count) != NMO_OK) {
        fprintf(stderr, "Error: Invalid hex string '%s'\n", hex);
        free(bytes);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const nmo_parameter_write_options_t options = {
        .resize = patch_optional_bool(op_obj, "resize", false),
    };
    nmo_status_t st = nmo_edit_plan_add_set_parameter_bytes(
        edit_plan,
        (nmo_object_id_t)parameter_id,
        bytes,
        byte_count,
        &options);
    free(bytes);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_set_data_cell(yyjson_val *op_obj,
                                     nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "dataarray_id",
        "row",
        "col",
        "value",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "set_data_cell operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    uint32_t dataarray_id = 0u;
    uint32_t row = 0u;
    uint32_t col = 0u;
    if (!patch_read_u32(op_obj, "dataarray_id", &dataarray_id) ||
        dataarray_id == 0u) {
        fprintf(stderr, "Error: Missing or invalid dataarray_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!patch_read_u32(op_obj, "row", &row)) {
        fprintf(stderr, "Error: Missing or invalid row\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!patch_read_u32(op_obj, "col", &col)) {
        fprintf(stderr, "Error: Missing or invalid col\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *value = patch_required_string(
        op_obj, "value", "set_data_cell operation");
    if (!value) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_status_t st = nmo_edit_plan_add_data_cell(
        edit_plan, (nmo_object_id_t)dataarray_id, row, col, value);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_optional_object_id(
    yyjson_val *obj,
    const char *key,
    const char *where,
    nmo_object_id_t *out_id) {
    if (!obj || !key || !out_id) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    *out_id = 0u;
    yyjson_val *val = yyjson_obj_get(obj, key);
    if (val == NULL) {
        return NMO_CLI_EXIT_SUCCESS;
    }
    if (!yyjson_is_uint(val) || yyjson_get_uint(val) > UINT32_MAX) {
        fprintf(stderr, "Error: Invalid %s in %s\n", key, where);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    *out_id = (nmo_object_id_t)yyjson_get_uint(val);
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_add_operation(yyjson_val *op_obj,
                                     nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "parent_id",
        "operation_guid",
        "in1_id",
        "in2_id",
        "out_id",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "add_operation operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    yyjson_val *parent_val = yyjson_obj_get(op_obj, "parent_id");
    if (!parent_val || !yyjson_is_uint(parent_val) ||
        yyjson_get_uint(parent_val) == 0 ||
        yyjson_get_uint(parent_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid parent_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *guid_text = patch_required_string(
        op_obj, "operation_guid", "add_operation operation");
    if (!guid_text) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    nmo_guid_t operation_guid = nmo_guid_parse(guid_text);
    if (nmo_guid_is_null(operation_guid)) {
        fprintf(stderr, "Error: Invalid operation GUID '%s'\n", guid_text);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_object_id_t in1_id = 0u;
    nmo_object_id_t in2_id = 0u;
    nmo_object_id_t out_id = 0u;
    rc = patch_parse_optional_object_id(
        op_obj, "in1_id", "add_operation operation", &in1_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    rc = patch_parse_optional_object_id(
        op_obj, "in2_id", "add_operation operation", &in2_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    rc = patch_parse_optional_object_id(
        op_obj, "out_id", "add_operation operation", &out_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    nmo_status_t st = nmo_edit_plan_add_operation(
        edit_plan,
        (nmo_object_id_t)yyjson_get_uint(parent_val),
        operation_guid,
        in1_id,
        in2_id,
        out_id);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_remove_operation(yyjson_val *op_obj,
                                        nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "operation_id",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "remove_operation operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    yyjson_val *operation_val = yyjson_obj_get(op_obj, "operation_id");
    if (!operation_val || !yyjson_is_uint(operation_val) ||
        yyjson_get_uint(operation_val) == 0 ||
        yyjson_get_uint(operation_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid operation_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_status_t st = nmo_edit_plan_add_remove_operation(
        edit_plan, (nmo_object_id_t)yyjson_get_uint(operation_val));
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_rewire_operation(yyjson_val *op_obj,
                                        nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "operation_id",
        "in1_id",
        "in2_id",
        "out_id",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "rewire_operation operation",
        allowed, sizeof(allowed) / sizeof(allowed[0]));
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    yyjson_val *operation_val = yyjson_obj_get(op_obj, "operation_id");
    if (!operation_val || !yyjson_is_uint(operation_val) ||
        yyjson_get_uint(operation_val) == 0 ||
        yyjson_get_uint(operation_val) > UINT32_MAX) {
        fprintf(stderr, "Error: Missing or invalid operation_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_object_id_t in1_id = 0u;
    nmo_object_id_t in2_id = 0u;
    nmo_object_id_t out_id = 0u;
    uint32_t slot_flags = 0u;
    if (yyjson_obj_get(op_obj, "in1_id") != NULL) {
        rc = patch_parse_optional_object_id(
            op_obj, "in1_id", "rewire_operation operation", &in1_id);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return rc;
        }
        slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_IN1;
    }
    if (yyjson_obj_get(op_obj, "in2_id") != NULL) {
        rc = patch_parse_optional_object_id(
            op_obj, "in2_id", "rewire_operation operation", &in2_id);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return rc;
        }
        slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_IN2;
    }
    if (yyjson_obj_get(op_obj, "out_id") != NULL) {
        rc = patch_parse_optional_object_id(
            op_obj, "out_id", "rewire_operation operation", &out_id);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return rc;
        }
        slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_OUT;
    }
    if (slot_flags == 0u) {
        fprintf(stderr,
                "Error: rewire_operation requires in1_id, in2_id, or out_id\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_status_t st = nmo_edit_plan_add_rewire_operation(
        edit_plan,
        (nmo_object_id_t)yyjson_get_uint(operation_val),
        slot_flags,
        in1_id,
        in2_id,
        out_id);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to build edit plan\n");
        return st == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_parse_interface_policy(yyjson_val *op_obj,
                                        nmo_edit_plan_t *edit_plan) {
    static const char *const allowed[] = {
        "op",
        "behavior_id",
        "mode",
    };
    int rc = patch_reject_unknown_fields(
        op_obj, "interface_policy operation",
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

    nmo_script_edit_interface_mode_t mode =
        NMO_SCRIPT_EDIT_INTERFACE_PRESERVE;
    rc = patch_parse_script_interface_mode(op_obj, &mode);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    nmo_status_t st = nmo_edit_plan_add_interface_policy(
        edit_plan, (nmo_object_id_t)yyjson_get_uint(id_val), mode);
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
        } else if (strcmp(op, "add_node") == 0 && out_plan->version == 2u) {
            rc = patch_parse_add_node(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "remove_node") == 0 &&
                   out_plan->version == 2u) {
            rc = patch_parse_remove_node(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "add_behavior_link") == 0 &&
                   out_plan->version == 2u) {
            rc = patch_parse_add_behavior_link(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "rewire_behavior_link") == 0 &&
                   out_plan->version == 2u) {
            rc = patch_parse_rewire_behavior_link(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "set_behavior_link_delay") == 0 &&
                   out_plan->version == 2u) {
            rc = patch_parse_set_behavior_link_delay(
                op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "remove_behavior_link") == 0 &&
                   out_plan->version == 2u) {
            rc = patch_parse_remove_behavior_link(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "add_parameter") == 0 &&
                   out_plan->version == 2u) {
            rc = patch_parse_add_parameter(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "disconnect_parameter") == 0 &&
                   out_plan->version == 2u) {
            rc = patch_parse_disconnect_parameter(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "connect_parameter") == 0 &&
                   out_plan->version == 2u) {
            rc = patch_parse_connect_parameter(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "remove_parameter") == 0 &&
                   out_plan->version == 2u) {
            rc = patch_parse_remove_parameter(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "set_parameter_value") == 0 &&
                   out_plan->version == 2u) {
            rc = patch_parse_set_parameter_value(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "set_parameter_bytes") == 0 &&
                   out_plan->version == 2u) {
            rc = patch_parse_set_parameter_bytes(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "set_data_cell") == 0 &&
                   out_plan->version == 2u) {
            rc = patch_parse_set_data_cell(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "add_operation") == 0 &&
                   out_plan->version == 2u) {
            rc = patch_parse_add_operation(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "remove_operation") == 0 &&
                   out_plan->version == 2u) {
            rc = patch_parse_remove_operation(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "rewire_operation") == 0 &&
                   out_plan->version == 2u) {
            rc = patch_parse_rewire_operation(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "add_io") == 0 && out_plan->version == 2u) {
            rc = patch_parse_add_io(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "remove_io") == 0 && out_plan->version == 2u) {
            rc = patch_parse_remove_io(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "rename_io") == 0 && out_plan->version == 2u) {
            rc = patch_parse_rename_io(op_obj, out_plan->edit_plan);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                patch_plan_free(out_plan);
                return rc;
            }
            continue;
        } else if (strcmp(op, "interface_policy") == 0 &&
                   out_plan->version == 2u) {
            rc = patch_parse_interface_policy(op_obj, out_plan->edit_plan);
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

