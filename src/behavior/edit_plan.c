/**
 * @file edit_plan.c
 * @brief Unified edit plan storage and transaction executor.
 */

#include "behavior/nmo_edit_plan.h"

#include "behavior/nmo_semantic_validator.h"
#include "behavior/nmo_script_edit.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_data.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_manager_guids.h"
#include "object/nmo_object_repository.h"

#include "../runtime/runtime_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct nmo_edit_plan {
    nmo_edit_op_t *ops;
    size_t count;
    size_t capacity;
    bool has_probe_selector_analysis;
    nmo_probe_selector_result_t probe_selector_analysis;
};

typedef struct edit_plan_manager_snapshot {
    bool has_message_manager;
    const char **message_names;
    uint32_t message_name_count;
    struct edit_plan_attribute_entry {
        const char *name;
        const char *category;
        nmo_guid_t type_guid;
        uint32_t compatible_class_id;
        uint32_t flags;
    } *attribute_entries;
    uint32_t attribute_entry_count;
} edit_plan_manager_snapshot_t;

typedef nmo_edit_handle_ref_t edit_plan_handle_ref_t;

static char *edit_plan_strdup(const char *text)
{
    if (text == NULL) {
        return NULL;
    }
    size_t len = strlen(text) + 1u;
    char *copy = (char *)malloc(len);
    if (copy != NULL) {
        memcpy(copy, text, len);
    }
    return copy;
}

static nmo_status_t edit_plan_probe_analysis_copy(
    nmo_probe_selector_result_t *dst,
    const nmo_probe_selector_result_t *src)
{
    if (dst == NULL || src == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_probe_analysis_dispose(dst);
    *dst = *src;
    dst->candidates = NULL;
    dst->candidate_count = 0u;
    dst->candidate_capacity = 0u;
    for (size_t i = 0; i < src->candidate_count; ++i) {
        nmo_status_t rc =
            nmo_probe_selector_result_add_candidate(dst, &src->candidates[i]);
        if (rc != NMO_OK) {
            nmo_probe_analysis_dispose(dst);
            return rc;
        }
    }
    return NMO_OK;
}

static void edit_plan_manager_entry_options_dispose(
    nmo_manager_entry_options_t *options)
{
    if (options == NULL) {
        return;
    }
    free((void *)options->key);
    free((void *)options->create.category);
    options->key = NULL;
    options->create.category = NULL;
}

static nmo_status_t edit_plan_manager_entry_options_clone(
    nmo_manager_entry_options_t *dst,
    const nmo_manager_entry_options_t *src)
{
    if (dst == NULL || src == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *dst = *src;
    dst->key = NULL;
    dst->create.category = NULL;
    dst->key = edit_plan_strdup(src->key);
    if (src->key != NULL && dst->key == NULL) {
        return NMO_ERR_NOMEM;
    }
    dst->create.category = edit_plan_strdup(src->create.category);
    if (src->create.category != NULL && dst->create.category == NULL) {
        edit_plan_manager_entry_options_dispose(dst);
        return NMO_ERR_NOMEM;
    }
    return NMO_OK;
}

static nmo_status_t edit_plan_parameter_write_options_clone(
    nmo_parameter_write_options_t *dst,
    const nmo_parameter_write_options_t *src)
{
    if (dst == NULL || src == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *dst = *src;
    return edit_plan_manager_entry_options_clone(
        &dst->manager_entry, &src->manager_entry);
}

static void edit_plan_parameter_write_options_dispose(
    nmo_parameter_write_options_t *options)
{
    if (options == NULL) {
        return;
    }
    edit_plan_manager_entry_options_dispose(&options->manager_entry);
}

static nmo_status_t edit_plan_add_node_options_clone(
    nmo_add_node_options_t *dst,
    const nmo_add_node_options_t *src)
{
    if (dst == NULL || src == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *dst = *src;
    return edit_plan_manager_entry_options_clone(
        &dst->manager_entry, &src->manager_entry);
}

static void edit_plan_add_node_options_dispose(
    nmo_add_node_options_t *options)
{
    if (options == NULL) {
        return;
    }
    edit_plan_manager_entry_options_dispose(&options->manager_entry);
}

static void edit_plan_manager_snapshot_dispose(
    edit_plan_manager_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    if (snapshot->message_names != NULL) {
        for (uint32_t i = 0; i < snapshot->message_name_count; ++i) {
            free((void *)snapshot->message_names[i]);
        }
        free(snapshot->message_names);
    }
    if (snapshot->attribute_entries != NULL) {
        for (uint32_t i = 0; i < snapshot->attribute_entry_count; ++i) {
            free((void *)snapshot->attribute_entries[i].name);
            free((void *)snapshot->attribute_entries[i].category);
        }
        free(snapshot->attribute_entries);
    }
    memset(snapshot, 0, sizeof(*snapshot));
}

static nmo_status_t edit_plan_read_manager_snapshot(
    nmo_workspace_t *workspace,
    edit_plan_manager_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    nmo_session_t *session = nmo_workspace_internal_session(workspace);
    const nmo_file_state_t *file_state =
        session != NULL ? nmo_session_get_file_state(session) : NULL;
    if (session == NULL || file_state == NULL ||
        file_state->manager_data == NULL) {
        return NMO_OK;
    }

    for (uint32_t i = 0; i < file_state->manager_data_count; ++i) {
        const nmo_manager_data_t *manager = &file_state->manager_data[i];
        if (!nmo_guid_equals(manager->guid, NMO_MANAGER_GUID_MESSAGE) ||
            manager->chunk == NULL) {
            continue;
        }
        snapshot->has_message_manager = true;
        nmo_chunk_t *chunk =
            nmo_chunk_clone(manager->chunk, nmo_session_get_arena(session));
        if (chunk == NULL ||
            nmo_chunk_start_read(chunk) != NMO_OK ||
            nmo_chunk_seek_identifier(chunk, 0x53u) != NMO_OK) {
            return NMO_OK;
        }
        int32_t count = 0;
        if (nmo_chunk_read_int(chunk, &count) != NMO_OK || count < 0) {
            return NMO_OK;
        }
        snapshot->message_names =
            (const char **)calloc((size_t)count, sizeof(char *));
        if (count > 0 && snapshot->message_names == NULL) {
            return NMO_ERR_NOMEM;
        }
        snapshot->message_name_count = (uint32_t)count;
        for (int32_t index = 0; index < count; ++index) {
            char *name = NULL;
            if (nmo_chunk_read_string(chunk, &name) == 0u) {
                edit_plan_manager_snapshot_dispose(snapshot);
                return NMO_OK;
            }
            snapshot->message_names[index] = edit_plan_strdup(name ? name : "");
            if (snapshot->message_names[index] == NULL) {
                edit_plan_manager_snapshot_dispose(snapshot);
                return NMO_ERR_NOMEM;
            }
        }
        break;
    }

    for (uint32_t i = 0; i < file_state->manager_data_count; ++i) {
        const nmo_manager_data_t *manager = &file_state->manager_data[i];
        if (!nmo_guid_equals(manager->guid, NMO_MANAGER_GUID_ATTRIBUTE) ||
            manager->chunk == NULL) {
            continue;
        }
        nmo_chunk_t *chunk =
            nmo_chunk_clone(manager->chunk, nmo_session_get_arena(session));
        if (chunk == NULL) {
            return NMO_ERR_NOMEM;
        }
        if (nmo_chunk_start_read(chunk) != NMO_OK) {
            return NMO_OK;
        }
        if (nmo_chunk_seek_identifier(chunk, 0x52u) != NMO_OK) {
            if (nmo_chunk_start_read(chunk) != NMO_OK) {
                return NMO_OK;
            }
            uint32_t identifier = 0u;
            if (nmo_chunk_read_identifier(chunk, &identifier) != NMO_OK ||
                identifier != 0x52u) {
                return NMO_OK;
            }
        }
        int32_t category_count = 0;
        int32_t attribute_count = 0;
        if (nmo_chunk_read_int(chunk, &category_count) != NMO_OK ||
            nmo_chunk_read_int(chunk, &attribute_count) != NMO_OK ||
            category_count < 0 || attribute_count < 0) {
            return NMO_OK;
        }
        const char **categories =
            (const char **)calloc((size_t)category_count, sizeof(char *));
        if (category_count > 0 && categories == NULL) {
            return NMO_ERR_NOMEM;
        }
        for (int32_t cat = 0; cat < category_count; ++cat) {
            int32_t present = 0;
            if (nmo_chunk_read_int(chunk, &present) != NMO_OK) {
                goto attribute_snapshot_cleanup;
            }
            if (present != 0) {
                char *name = NULL;
                uint32_t flags = 0u;
                (void)nmo_chunk_read_string(chunk, &name);
                (void)nmo_chunk_read_dword(chunk, &flags);
                categories[cat] = edit_plan_strdup(name ? name : "");
                if (categories[cat] == NULL) {
                    free(categories);
                    return NMO_ERR_NOMEM;
                }
            }
        }
        snapshot->attribute_entries =
            (struct edit_plan_attribute_entry *)calloc(
                (size_t)attribute_count, sizeof(*snapshot->attribute_entries));
        if (attribute_count > 0 && snapshot->attribute_entries == NULL) {
            for (int32_t cat = 0; cat < category_count; ++cat) {
                free((void *)categories[cat]);
            }
            free(categories);
            return NMO_ERR_NOMEM;
        }
        snapshot->attribute_entry_count = (uint32_t)attribute_count;
        for (int32_t attr_index = 0; attr_index < attribute_count; ++attr_index) {
            int32_t present = 0;
            if (nmo_chunk_read_int(chunk, &present) != NMO_OK) {
                break;
            }
            if (present == 0) {
                continue;
            }
            char *name = NULL;
            nmo_guid_t type_guid = {0};
            int32_t category_index = -1;
            int32_t compatible_class_id = 0;
            uint32_t flags = 0u;
            (void)nmo_chunk_read_string(chunk, &name);
            if (nmo_chunk_read_guid(chunk, &type_guid) != NMO_OK ||
                nmo_chunk_read_int(chunk, &category_index) != NMO_OK ||
                nmo_chunk_read_int(chunk, &compatible_class_id) != NMO_OK ||
                nmo_chunk_read_dword(chunk, &flags) != NMO_OK) {
                break;
            }
            snapshot->attribute_entries[attr_index].name =
                edit_plan_strdup(name ? name : "");
            if (snapshot->attribute_entries[attr_index].name == NULL) {
                for (int32_t cat = 0; cat < category_count; ++cat) {
                    free((void *)categories[cat]);
                }
                free(categories);
                return NMO_ERR_NOMEM;
            }
            if (category_index >= 0 && category_index < category_count &&
                categories[category_index] != NULL) {
                snapshot->attribute_entries[attr_index].category =
                    edit_plan_strdup(categories[category_index]);
                if (snapshot->attribute_entries[attr_index].category == NULL) {
                    for (int32_t cat = 0; cat < category_count; ++cat) {
                        free((void *)categories[cat]);
                    }
                    free(categories);
                    return NMO_ERR_NOMEM;
                }
            }
            snapshot->attribute_entries[attr_index].type_guid = type_guid;
            snapshot->attribute_entries[attr_index].compatible_class_id =
                compatible_class_id < 0 ? 0u : (uint32_t)compatible_class_id;
            snapshot->attribute_entries[attr_index].flags = flags;
        }
attribute_snapshot_cleanup:
        for (int32_t cat = 0; cat < category_count; ++cat) {
            free((void *)categories[cat]);
        }
        free(categories);
        break;
    }

    return NMO_OK;
}

static bool edit_plan_manager_snapshot_has_name(
    const edit_plan_manager_snapshot_t *snapshot,
    const char *name)
{
    if (snapshot == NULL || name == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < snapshot->message_name_count; ++i) {
        if (snapshot->message_names[i] != NULL &&
            strcmp(snapshot->message_names[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static bool edit_plan_find_created_message_entry(
    const edit_plan_manager_snapshot_t *before,
    const edit_plan_manager_snapshot_t *after,
    const char **out_key,
    uint32_t *out_index)
{
    if (after == NULL || after->message_names == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < after->message_name_count; ++i) {
        const char *name = after->message_names[i];
        if (name != NULL && !edit_plan_manager_snapshot_has_name(before, name)) {
            if (out_key != NULL) {
                *out_key = name;
            }
            if (out_index != NULL) {
                *out_index = i;
            }
            return true;
        }
    }
    return false;
}

static bool edit_plan_find_created_attribute_entry(
    const edit_plan_manager_snapshot_t *before,
    const edit_plan_manager_snapshot_t *after,
    const char **out_key,
    const char **out_category,
    nmo_guid_t *out_type_guid,
    uint32_t *out_index,
    uint32_t *out_compatible_class_id,
    uint32_t *out_flags)
{
    if (after == NULL || after->attribute_entries == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < after->attribute_entry_count; ++i) {
        const struct edit_plan_attribute_entry *entry =
            &after->attribute_entries[i];
        if (entry->name == NULL || entry->name[0] == '\0') {
            continue;
        }
        bool existed = false;
        if (before != NULL && before->attribute_entries != NULL) {
            for (uint32_t j = 0; j < before->attribute_entry_count; ++j) {
                const char *before_name = before->attribute_entries[j].name;
                if (before_name != NULL && strcmp(before_name, entry->name) == 0) {
                    existed = true;
                    break;
                }
            }
        }
        if (existed) {
            continue;
        }
        if (out_key != NULL) {
            *out_key = entry->name;
        }
        if (out_category != NULL) {
            *out_category = entry->category;
        }
        if (out_type_guid != NULL) {
            *out_type_guid = entry->type_guid;
        }
        if (out_index != NULL) {
            *out_index = i;
        }
        if (out_compatible_class_id != NULL) {
            *out_compatible_class_id = entry->compatible_class_id;
        }
        if (out_flags != NULL) {
            *out_flags = entry->flags;
        }
        return true;
    }
    return false;
}

static const nmo_manager_entry_options_t *edit_plan_op_manager_entry(
    const nmo_edit_op_t *op)
{
    if (op == NULL) {
        return NULL;
    }
    switch (op->kind) {
    case NMO_EDIT_OP_SET_PARAMETER_VALUE:
        return &op->data.set_value.options.manager_entry;
    case NMO_EDIT_OP_SET_PARAMETER_BYTES:
        return &op->data.set_bytes.options.manager_entry;
    case NMO_EDIT_OP_ADD_NODE:
        return &op->data.add_node.options.manager_entry;
    default:
        return NULL;
    }
}

static uint32_t edit_plan_get_parameter_manager_value(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t parameter_id)
{
    nmo_workspace_t *workspace = nmo_script_edit_workspace(tx);
    nmo_object_repository_t *repo =
        workspace != NULL ? nmo_workspace_internal_repository(workspace) : NULL;
    nmo_object_t *object =
        repo != NULL ? nmo_object_repository_find_by_id(repo, parameter_id)
                     : NULL;
    const nmo_parameter_state_t *state =
        object != NULL ? (const nmo_parameter_state_t *)nmo_object_get_state(object)
                       : NULL;
    return state != NULL ? state->manager_value : 0u;
}

static nmo_status_t edit_plan_dup_object_ids(
    const nmo_object_id_t *ids,
    size_t count,
    nmo_object_id_t **out_ids)
{
    *out_ids = NULL;
    if (count == 0) {
        return NMO_OK;
    }
    if (ids == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_object_id_t *copy =
        (nmo_object_id_t *)malloc(count * sizeof(*copy));
    if (copy == NULL) {
        return NMO_ERR_NOMEM;
    }
    memcpy(copy, ids, count * sizeof(*copy));
    *out_ids = copy;
    return NMO_OK;
}

static nmo_status_t edit_plan_dup_fold_maps(
    const nmo_behavior_fold_map_t *maps,
    size_t count,
    nmo_behavior_fold_map_t **out_maps)
{
    *out_maps = NULL;
    if (count == 0) {
        return NMO_OK;
    }
    if (maps == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_behavior_fold_map_t *copy =
        (nmo_behavior_fold_map_t *)calloc(count, sizeof(*copy));
    if (copy == NULL) {
        return NMO_ERR_NOMEM;
    }
    for (size_t i = 0; i < count; ++i) {
        copy[i] = maps[i];
        if (maps[i].label != NULL) {
            copy[i].label = edit_plan_strdup(maps[i].label);
            if (copy[i].label == NULL) {
                for (size_t j = 0; j < i; ++j) {
                    free((void *)copy[j].label);
                }
                free(copy);
                return NMO_ERR_NOMEM;
            }
        }
    }
    *out_maps = copy;
    return NMO_OK;
}

static void edit_plan_free_fold_maps(
    nmo_behavior_fold_map_t *maps,
    size_t count)
{
    if (maps == NULL) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        free((void *)maps[i].label);
    }
    free(maps);
}

static nmo_status_t edit_plan_reserve(nmo_edit_plan_t *plan, size_t needed)
{
    if (plan == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (needed <= plan->capacity) {
        return NMO_OK;
    }
    size_t new_capacity = plan->capacity == 0 ? 8u : plan->capacity * 2u;
    while (new_capacity < needed) {
        new_capacity *= 2u;
    }
    nmo_edit_op_t *new_ops =
        (nmo_edit_op_t *)realloc(plan->ops, new_capacity * sizeof(*new_ops));
    if (new_ops == NULL) {
        return NMO_ERR_NOMEM;
    }
    plan->ops = new_ops;
    plan->capacity = new_capacity;
    return NMO_OK;
}

static edit_plan_handle_ref_t edit_plan_no_handle_ref(void)
{
    return (edit_plan_handle_ref_t){0};
}

static edit_plan_handle_ref_t edit_plan_ref_or_none(
    const nmo_edit_handle_ref_t *ref)
{
    return ref != NULL ? *ref : edit_plan_no_handle_ref();
}

typedef struct edit_plan_handle_ref_clone_slot {
    edit_plan_handle_ref_t *dst;
    const edit_plan_handle_ref_t *src;
} edit_plan_handle_ref_clone_slot_t;

static nmo_status_t edit_plan_append_op(
    nmo_edit_plan_t *plan,
    nmo_edit_op_kind_t kind,
    nmo_object_id_t primary_id,
    bool allow_zero_primary,
    nmo_edit_op_t **out_op)
{
    if (plan == NULL || out_op == NULL ||
        (!allow_zero_primary && primary_id == 0)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_reserve(plan, plan->count + 1u));
    nmo_edit_op_t *op = &plan->ops[plan->count];
    memset(op, 0, sizeof(*op));
    op->kind = kind;
    op->primary_id = primary_id;
    *out_op = op;
    return NMO_OK;
}

static nmo_status_t edit_plan_append_blank(
    nmo_edit_plan_t *plan,
    nmo_edit_op_kind_t kind,
    nmo_object_id_t primary_id,
    nmo_edit_op_t **out_op)
{
    return edit_plan_append_op(plan, kind, primary_id, false, out_op);
}

static void edit_plan_handle_ref_dispose(edit_plan_handle_ref_t *ref)
{
    if (ref == NULL) {
        return;
    }
    free((void *)ref->handle_name);
    memset(ref, 0, sizeof(*ref));
}

static nmo_status_t edit_plan_handle_ref_clone(
    edit_plan_handle_ref_t *dst,
    const edit_plan_handle_ref_t *src)
{
    if (dst == NULL || src == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *dst = *src;
    dst->handle_name = NULL;
    if (!src->has_ref) {
        dst->operation_index = 0u;
        return NMO_OK;
    }
    if (src->handle_name == NULL || src->handle_name[0] == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    char *handle_copy = edit_plan_strdup(src->handle_name);
    if (handle_copy == NULL) {
        return NMO_ERR_NOMEM;
    }
    dst->handle_name = handle_copy;
    return NMO_OK;
}

static nmo_status_t edit_plan_handle_ref_clone_slots(
    const edit_plan_handle_ref_clone_slot_t *slots,
    size_t slot_count)
{
    if (slot_count == 0u) {
        return NMO_OK;
    }
    if (slots == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < slot_count; ++i) {
        if (slots[i].dst == NULL || slots[i].src == NULL) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        nmo_status_t st =
            edit_plan_handle_ref_clone(slots[i].dst, slots[i].src);
        if (st != NMO_OK) {
            return st;
        }
    }
    return NMO_OK;
}

static nmo_status_t edit_plan_copy_parameter_write_options(
    nmo_parameter_write_options_t *out_options,
    bool *out_has_options,
    const nmo_parameter_write_options_t *options)
{
    if (out_options == NULL || out_has_options == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_has_options = false;
    if (options == NULL) {
        return NMO_OK;
    }
    NMO_RETURN_IF_ERROR(edit_plan_parameter_write_options_clone(
        out_options, options));
    *out_has_options = true;
    return NMO_OK;
}

static nmo_status_t edit_plan_copy_bytes(
    const uint8_t *bytes,
    size_t byte_count,
    const uint8_t **out_bytes)
{
    if (out_bytes == NULL || (bytes == NULL && byte_count > 0u)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_bytes = NULL;
    if (byte_count == 0u) {
        return NMO_OK;
    }
    uint8_t *copy = (uint8_t *)malloc(byte_count);
    if (copy == NULL) {
        return NMO_ERR_NOMEM;
    }
    memcpy(copy, bytes, byte_count);
    *out_bytes = copy;
    return NMO_OK;
}

static void edit_op_dispose(nmo_edit_op_t *op)
{
    if (op == NULL) {
        return;
    }
    if (op->kind == NMO_EDIT_OP_SET_PARAMETER_VALUE) {
        free((void *)op->data.set_value.value);
        edit_plan_handle_ref_dispose(&op->data.set_value.parameter_ref);
        edit_plan_parameter_write_options_dispose(
            &op->data.set_value.options);
    } else if (op->kind == NMO_EDIT_OP_SET_PARAMETER_BYTES) {
        free((void *)op->data.set_bytes.bytes);
        edit_plan_handle_ref_dispose(&op->data.set_bytes.parameter_ref);
        edit_plan_parameter_write_options_dispose(
            &op->data.set_bytes.options);
    } else if (op->kind == NMO_EDIT_OP_ADD_NODE) {
        free((void *)op->data.add_node.name);
        edit_plan_add_node_options_dispose(&op->data.add_node.options);
    } else if (op->kind == NMO_EDIT_OP_ADD_IO) {
        free((void *)op->data.add_io.name);
    } else if (op->kind == NMO_EDIT_OP_RENAME_IO) {
        free((void *)op->data.rename_io.name);
    } else if (op->kind == NMO_EDIT_OP_ADD_BEHAVIOR_LINK) {
        edit_plan_handle_ref_dispose(&op->data.add_link.from_io_ref);
        edit_plan_handle_ref_dispose(&op->data.add_link.to_io_ref);
    } else if (op->kind == NMO_EDIT_OP_ADD_PARAMETER) {
        free((void *)op->data.add_parameter.name);
    } else if (op->kind == NMO_EDIT_OP_CONNECT_PARAMETER) {
        edit_plan_handle_ref_dispose(
            &op->data.connect_parameter.target_parameter_ref);
    } else if (op->kind == NMO_EDIT_OP_ADD_OPERATION) {
        edit_plan_handle_ref_dispose(
            &op->data.add_operation.in1_parameter_ref);
        edit_plan_handle_ref_dispose(
            &op->data.add_operation.in2_parameter_ref);
        edit_plan_handle_ref_dispose(
            &op->data.add_operation.out_parameter_ref);
    } else if (op->kind == NMO_EDIT_OP_REWIRE_OPERATION) {
        edit_plan_handle_ref_dispose(
            &op->data.rewire_operation.in1_parameter_ref);
        edit_plan_handle_ref_dispose(
            &op->data.rewire_operation.in2_parameter_ref);
        edit_plan_handle_ref_dispose(
            &op->data.rewire_operation.out_parameter_ref);
    } else if (op->kind == NMO_EDIT_OP_SET_DATA_CELL) {
        free((void *)op->data.data_cell.value);
    } else if (op->kind == NMO_EDIT_OP_FOLD) {
        free((void *)op->data.fold.desc.name);
        free(op->data.fold.node_ids);
        edit_plan_free_fold_maps(
            op->data.fold.input_maps,
            op->data.fold.desc.input_map_count);
        edit_plan_free_fold_maps(
            op->data.fold.output_maps,
            op->data.fold.desc.output_map_count);
        edit_plan_free_fold_maps(
            op->data.fold.parameter_maps,
            op->data.fold.desc.parameter_map_count);
    } else if (op->kind == NMO_EDIT_OP_REPLACE_BB) {
        free((void *)op->data.replace_bb.desc.name);
    }
    memset(op, 0, sizeof(*op));
}

static nmo_status_t edit_op_clone_handle_ref_slots_or_dispose(
    nmo_edit_op_t *op,
    const edit_plan_handle_ref_clone_slot_t *slots,
    size_t slot_count)
{
    nmo_status_t st = edit_plan_handle_ref_clone_slots(slots, slot_count);
    if (st != NMO_OK) {
        edit_op_dispose(op);
        return st;
    }
    return NMO_OK;
}

static void edit_op_clear_owned_pointers(nmo_edit_op_t *op)
{
    if (op == NULL) {
        return;
    }
    switch (op->kind) {
    case NMO_EDIT_OP_SET_PARAMETER_VALUE:
        op->data.set_value.value = NULL;
        op->data.set_value.parameter_ref.handle_name = NULL;
        op->data.set_value.options.manager_entry.key = NULL;
        op->data.set_value.options.manager_entry.create.category = NULL;
        break;
    case NMO_EDIT_OP_SET_PARAMETER_BYTES:
        op->data.set_bytes.bytes = NULL;
        op->data.set_bytes.parameter_ref.handle_name = NULL;
        op->data.set_bytes.options.manager_entry.key = NULL;
        op->data.set_bytes.options.manager_entry.create.category = NULL;
        break;
    case NMO_EDIT_OP_ADD_NODE:
        op->data.add_node.name = NULL;
        op->data.add_node.options.manager_entry.key = NULL;
        op->data.add_node.options.manager_entry.create.category = NULL;
        break;
    case NMO_EDIT_OP_ADD_IO:
        op->data.add_io.name = NULL;
        break;
    case NMO_EDIT_OP_RENAME_IO:
        op->data.rename_io.name = NULL;
        break;
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK:
        op->data.add_link.from_io_ref.handle_name = NULL;
        op->data.add_link.to_io_ref.handle_name = NULL;
        break;
    case NMO_EDIT_OP_ADD_PARAMETER:
        op->data.add_parameter.name = NULL;
        break;
    case NMO_EDIT_OP_CONNECT_PARAMETER:
        op->data.connect_parameter.target_parameter_ref.handle_name = NULL;
        break;
    case NMO_EDIT_OP_ADD_OPERATION:
        op->data.add_operation.in1_parameter_ref.handle_name = NULL;
        op->data.add_operation.in2_parameter_ref.handle_name = NULL;
        op->data.add_operation.out_parameter_ref.handle_name = NULL;
        break;
    case NMO_EDIT_OP_REWIRE_OPERATION:
        op->data.rewire_operation.in1_parameter_ref.handle_name = NULL;
        op->data.rewire_operation.in2_parameter_ref.handle_name = NULL;
        op->data.rewire_operation.out_parameter_ref.handle_name = NULL;
        break;
    case NMO_EDIT_OP_SET_DATA_CELL:
        op->data.data_cell.value = NULL;
        break;
    case NMO_EDIT_OP_FOLD:
        op->data.fold.desc.name = NULL;
        op->data.fold.desc.node_ids = NULL;
        op->data.fold.desc.input_maps = NULL;
        op->data.fold.desc.output_maps = NULL;
        op->data.fold.desc.parameter_maps = NULL;
        op->data.fold.node_ids = NULL;
        op->data.fold.input_maps = NULL;
        op->data.fold.output_maps = NULL;
        op->data.fold.parameter_maps = NULL;
        break;
    case NMO_EDIT_OP_REPLACE_BB:
        op->data.replace_bb.desc.name = NULL;
        break;
    default:
        break;
    }
}

static nmo_status_t edit_op_copy(
    nmo_edit_op_t *dst,
    const nmo_edit_op_t *src)
{
    if (dst == NULL || src == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof(*dst));
    *dst = *src;
    edit_op_clear_owned_pointers(dst);
    switch (src->kind) {
    case NMO_EDIT_OP_SET_PARAMETER_VALUE:
        dst->data.set_value.value =
            edit_plan_strdup(src->data.set_value.value);
        if (src->data.set_value.value && !dst->data.set_value.value) {
            return NMO_ERR_NOMEM;
        }
        NMO_RETURN_IF_ERROR(edit_op_clone_handle_ref_slots_or_dispose(
            dst,
            (edit_plan_handle_ref_clone_slot_t[]){
                {
                    &dst->data.set_value.parameter_ref,
                    &src->data.set_value.parameter_ref,
                },
            },
            1u));
        if (src->data.set_value.has_options) {
            nmo_status_t st = edit_plan_parameter_write_options_clone(
                &dst->data.set_value.options,
                &src->data.set_value.options);
            if (st != NMO_OK) {
                edit_op_dispose(dst);
                return st;
            }
        }
        break;
    case NMO_EDIT_OP_SET_PARAMETER_BYTES:
        dst->data.set_bytes.bytes = NULL;
        if (src->data.set_bytes.byte_count > 0) {
            uint8_t *copy =
                (uint8_t *)malloc(src->data.set_bytes.byte_count);
            if (copy == NULL) {
                return NMO_ERR_NOMEM;
            }
            memcpy(copy, src->data.set_bytes.bytes,
                   src->data.set_bytes.byte_count);
            dst->data.set_bytes.bytes = copy;
        }
        NMO_RETURN_IF_ERROR(edit_op_clone_handle_ref_slots_or_dispose(
            dst,
            (edit_plan_handle_ref_clone_slot_t[]){
                {
                    &dst->data.set_bytes.parameter_ref,
                    &src->data.set_bytes.parameter_ref,
                },
            },
            1u));
        if (src->data.set_bytes.has_options) {
            nmo_status_t st = edit_plan_parameter_write_options_clone(
                &dst->data.set_bytes.options,
                &src->data.set_bytes.options);
            if (st != NMO_OK) {
                edit_op_dispose(dst);
                return st;
            }
        }
        break;
    case NMO_EDIT_OP_ADD_NODE:
        dst->data.add_node.name = edit_plan_strdup(src->data.add_node.name);
        if (src->data.add_node.name && !dst->data.add_node.name) {
            return NMO_ERR_NOMEM;
        }
        if (src->data.add_node.has_options) {
            nmo_status_t st = edit_plan_add_node_options_clone(
                &dst->data.add_node.options,
                &src->data.add_node.options);
            if (st != NMO_OK) {
                edit_op_dispose(dst);
                return st;
            }
        }
        break;
    case NMO_EDIT_OP_ADD_IO:
        dst->data.add_io.name = edit_plan_strdup(src->data.add_io.name);
        if (src->data.add_io.name && !dst->data.add_io.name) {
            return NMO_ERR_NOMEM;
        }
        break;
    case NMO_EDIT_OP_RENAME_IO:
        dst->data.rename_io.name = edit_plan_strdup(src->data.rename_io.name);
        if (src->data.rename_io.name && !dst->data.rename_io.name) {
            return NMO_ERR_NOMEM;
        }
        break;
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK:
        NMO_RETURN_IF_ERROR(edit_op_clone_handle_ref_slots_or_dispose(
            dst,
            (edit_plan_handle_ref_clone_slot_t[]){
                {
                    &dst->data.add_link.from_io_ref,
                    &src->data.add_link.from_io_ref,
                },
                {
                    &dst->data.add_link.to_io_ref,
                    &src->data.add_link.to_io_ref,
                },
            },
            2u));
        break;
    case NMO_EDIT_OP_ADD_PARAMETER:
        dst->data.add_parameter.name =
            edit_plan_strdup(src->data.add_parameter.name);
        if (src->data.add_parameter.name && !dst->data.add_parameter.name) {
            return NMO_ERR_NOMEM;
        }
        break;
    case NMO_EDIT_OP_CONNECT_PARAMETER:
        NMO_RETURN_IF_ERROR(edit_op_clone_handle_ref_slots_or_dispose(
            dst,
            (edit_plan_handle_ref_clone_slot_t[]){
                {
                    &dst->data.connect_parameter.target_parameter_ref,
                    &src->data.connect_parameter.target_parameter_ref,
                },
            },
            1u));
        break;
    case NMO_EDIT_OP_ADD_OPERATION:
        NMO_RETURN_IF_ERROR(edit_op_clone_handle_ref_slots_or_dispose(
            dst,
            (edit_plan_handle_ref_clone_slot_t[]){
                {
                    &dst->data.add_operation.in1_parameter_ref,
                    &src->data.add_operation.in1_parameter_ref,
                },
                {
                    &dst->data.add_operation.in2_parameter_ref,
                    &src->data.add_operation.in2_parameter_ref,
                },
                {
                    &dst->data.add_operation.out_parameter_ref,
                    &src->data.add_operation.out_parameter_ref,
                },
            },
            3u));
        break;
    case NMO_EDIT_OP_REWIRE_OPERATION:
        NMO_RETURN_IF_ERROR(edit_op_clone_handle_ref_slots_or_dispose(
            dst,
            (edit_plan_handle_ref_clone_slot_t[]){
                {
                    &dst->data.rewire_operation.in1_parameter_ref,
                    &src->data.rewire_operation.in1_parameter_ref,
                },
                {
                    &dst->data.rewire_operation.in2_parameter_ref,
                    &src->data.rewire_operation.in2_parameter_ref,
                },
                {
                    &dst->data.rewire_operation.out_parameter_ref,
                    &src->data.rewire_operation.out_parameter_ref,
                },
            },
            3u));
        break;
    case NMO_EDIT_OP_SET_DATA_CELL:
        dst->data.data_cell.value =
            edit_plan_strdup(src->data.data_cell.value);
        if (src->data.data_cell.value && !dst->data.data_cell.value) {
            return NMO_ERR_NOMEM;
        }
        break;
    case NMO_EDIT_OP_FOLD:
        dst->data.fold.desc = src->data.fold.desc;
        dst->data.fold.desc.name =
            edit_plan_strdup(src->data.fold.desc.name);
        if (src->data.fold.desc.name && !dst->data.fold.desc.name) {
            return NMO_ERR_NOMEM;
        }
        dst->data.fold.node_ids = NULL;
        dst->data.fold.input_maps = NULL;
        dst->data.fold.output_maps = NULL;
        dst->data.fold.parameter_maps = NULL;
        NMO_RETURN_IF_ERROR(edit_plan_dup_object_ids(
            src->data.fold.node_ids,
            src->data.fold.desc.node_count,
            &dst->data.fold.node_ids));
        dst->data.fold.desc.node_ids = dst->data.fold.node_ids;
        NMO_RETURN_IF_ERROR(edit_plan_dup_fold_maps(
            src->data.fold.input_maps,
            src->data.fold.desc.input_map_count,
            &dst->data.fold.input_maps));
        dst->data.fold.desc.input_maps = dst->data.fold.input_maps;
        NMO_RETURN_IF_ERROR(edit_plan_dup_fold_maps(
            src->data.fold.output_maps,
            src->data.fold.desc.output_map_count,
            &dst->data.fold.output_maps));
        dst->data.fold.desc.output_maps = dst->data.fold.output_maps;
        NMO_RETURN_IF_ERROR(edit_plan_dup_fold_maps(
            src->data.fold.parameter_maps,
            src->data.fold.desc.parameter_map_count,
            &dst->data.fold.parameter_maps));
        dst->data.fold.desc.parameter_maps = dst->data.fold.parameter_maps;
        break;
    case NMO_EDIT_OP_REPLACE_BB:
        dst->data.replace_bb.desc = src->data.replace_bb.desc;
        dst->data.replace_bb.desc.name =
            edit_plan_strdup(src->data.replace_bb.desc.name);
        if (src->data.replace_bb.desc.name &&
            !dst->data.replace_bb.desc.name) {
            return NMO_ERR_NOMEM;
        }
        break;
    default:
        break;
    }
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_create(nmo_edit_plan_t **out_plan)
{
    if (out_plan == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_plan = (nmo_edit_plan_t *)calloc(1, sizeof(nmo_edit_plan_t));
    return *out_plan != NULL ? NMO_OK : NMO_ERR_NOMEM;
}

nmo_status_t nmo_edit_plan_clone(
    const nmo_edit_plan_t *plan,
    nmo_edit_plan_t **out_plan)
{
    if (plan == NULL || out_plan == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_plan = NULL;
    nmo_edit_plan_t *clone = NULL;
    NMO_RETURN_IF_ERROR(nmo_edit_plan_create(&clone));
    nmo_status_t rc = edit_plan_reserve(clone, plan->count);
    if (rc != NMO_OK) {
        nmo_edit_plan_destroy(clone);
        return rc;
    }
    for (size_t i = 0; i < plan->count; ++i) {
        rc = edit_op_copy(&clone->ops[i], &plan->ops[i]);
        if (rc != NMO_OK) {
            for (size_t j = 0; j < i; ++j) {
                edit_op_dispose(&clone->ops[j]);
            }
            nmo_edit_plan_destroy(clone);
            return rc;
        }
        clone->count++;
    }
    if (plan->has_probe_selector_analysis) {
        rc = edit_plan_probe_analysis_copy(&clone->probe_selector_analysis,
                                           &plan->probe_selector_analysis);
        if (rc != NMO_OK) {
            nmo_edit_plan_destroy(clone);
            return rc;
        }
        clone->has_probe_selector_analysis = true;
    }
    *out_plan = clone;
    return NMO_OK;
}

void nmo_edit_plan_destroy(nmo_edit_plan_t *plan)
{
    if (plan == NULL) {
        return;
    }
    for (size_t i = 0; i < plan->count; i++) {
        edit_op_dispose(&plan->ops[i]);
    }
    nmo_probe_analysis_dispose(&plan->probe_selector_analysis);
    free(plan->ops);
    free(plan);
}

size_t nmo_edit_plan_count(const nmo_edit_plan_t *plan)
{
    return plan != NULL ? plan->count : 0u;
}

const nmo_edit_op_t *nmo_edit_plan_get(const nmo_edit_plan_t *plan, size_t index)
{
    if (plan == NULL || index >= plan->count) {
        return NULL;
    }
    return &plan->ops[index];
}

nmo_status_t nmo_edit_plan_set_probe_selector_analysis(
    nmo_edit_plan_t *plan,
    const nmo_probe_selector_result_t *analysis)
{
    if (plan == NULL || analysis == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t rc = edit_plan_probe_analysis_copy(
        &plan->probe_selector_analysis, analysis);
    if (rc != NMO_OK) {
        plan->has_probe_selector_analysis = false;
        return rc;
    }
    plan->has_probe_selector_analysis = true;
    return NMO_OK;
}

const nmo_probe_selector_result_t *
nmo_edit_plan_get_probe_selector_analysis(const nmo_edit_plan_t *plan)
{
    return plan != NULL && plan->has_probe_selector_analysis
        ? &plan->probe_selector_analysis
        : NULL;
}

static nmo_status_t edit_plan_add_set_parameter_value_core(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    edit_plan_handle_ref_t parameter_ref,
    const char *value_str,
    const nmo_parameter_write_options_t *options)
{
    nmo_edit_op_t *op = NULL;
    if (plan == NULL || value_str == NULL ||
        (parameter_id == 0u && !parameter_ref.has_ref)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_op(
        plan,
        NMO_EDIT_OP_SET_PARAMETER_VALUE,
        parameter_id,
        parameter_ref.has_ref,
        &op));
    op->data.set_value.value = edit_plan_strdup(value_str);
    if (op->data.set_value.value == NULL) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    edit_plan_handle_ref_clone_slot_t refs[] = {
        {&op->data.set_value.parameter_ref, &parameter_ref},
    };
    nmo_status_t st = edit_op_clone_handle_ref_slots_or_dispose(
        op, refs, sizeof(refs) / sizeof(refs[0]));
    if (st != NMO_OK) {
        return st;
    }
    st = edit_plan_copy_parameter_write_options(
        &op->data.set_value.options,
        &op->data.set_value.has_options,
        options);
    if (st != NMO_OK) {
        edit_op_dispose(op);
        return st;
    }
    plan->count++;
    return NMO_OK;
}

static nmo_status_t edit_plan_add_set_parameter_bytes_core(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    edit_plan_handle_ref_t parameter_ref,
    const uint8_t *bytes,
    size_t byte_count,
    const nmo_parameter_write_options_t *options)
{
    nmo_edit_op_t *op = NULL;
    if (plan == NULL || (bytes == NULL && byte_count > 0u) ||
        (parameter_id == 0u && !parameter_ref.has_ref)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_op(
        plan,
        NMO_EDIT_OP_SET_PARAMETER_BYTES,
        parameter_id,
        parameter_ref.has_ref,
        &op));
    nmo_status_t st = edit_plan_copy_bytes(
        bytes, byte_count, &op->data.set_bytes.bytes);
    if (st != NMO_OK) {
        edit_op_dispose(op);
        return st;
    }
    op->data.set_bytes.byte_count = byte_count;
    edit_plan_handle_ref_clone_slot_t refs[] = {
        {&op->data.set_bytes.parameter_ref, &parameter_ref},
    };
    st = edit_op_clone_handle_ref_slots_or_dispose(
        op, refs, sizeof(refs) / sizeof(refs[0]));
    if (st != NMO_OK) {
        return st;
    }
    st = edit_plan_copy_parameter_write_options(
        &op->data.set_bytes.options,
        &op->data.set_bytes.has_options,
        options);
    if (st != NMO_OK) {
        edit_op_dispose(op);
        return st;
    }
    plan->count++;
    return NMO_OK;
}

static nmo_status_t edit_plan_add_behavior_link_core(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t from_io_id,
    edit_plan_handle_ref_t from_ref,
    nmo_object_id_t to_io_id,
    edit_plan_handle_ref_t to_ref,
    uint32_t activation_delay)
{
    nmo_edit_op_t *op = NULL;
    if (parent_behavior_id == 0u ||
        (from_io_id == 0u && !from_ref.has_ref) ||
        (to_io_id == 0u && !to_ref.has_ref)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_ADD_BEHAVIOR_LINK, parent_behavior_id, &op));
    op->data.add_link.parent_behavior_id = parent_behavior_id;
    op->data.add_link.from_io_id = from_io_id;
    op->data.add_link.to_io_id = to_io_id;
    op->data.add_link.activation_delay = activation_delay;
    edit_plan_handle_ref_clone_slot_t refs[] = {
        {&op->data.add_link.from_io_ref, &from_ref},
        {&op->data.add_link.to_io_ref, &to_ref},
    };
    nmo_status_t st = edit_op_clone_handle_ref_slots_or_dispose(
        op, refs, sizeof(refs) / sizeof(refs[0]));
    if (st != NMO_OK) {
        return st;
    }
    plan->count++;
    return NMO_OK;
}

static nmo_status_t edit_plan_add_connect_parameter_core(
    nmo_edit_plan_t *plan,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id,
    edit_plan_handle_ref_t target_ref)
{
    nmo_edit_op_t *op = NULL;
    if (source_parameter_id == 0u ||
        (target_parameter_id == 0u && !target_ref.has_ref)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_object_id_t primary_id =
        target_ref.has_ref ? source_parameter_id : target_parameter_id;
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_CONNECT_PARAMETER, primary_id, &op));
    op->data.connect_parameter.source_parameter_id = source_parameter_id;
    op->data.connect_parameter.target_parameter_id = target_parameter_id;
    edit_plan_handle_ref_clone_slot_t refs[] = {
        {&op->data.connect_parameter.target_parameter_ref, &target_ref},
    };
    nmo_status_t st = edit_op_clone_handle_ref_slots_or_dispose(
        op, refs, sizeof(refs) / sizeof(refs[0]));
    if (st != NMO_OK) {
        return st;
    }
    plan->count++;
    return NMO_OK;
}

static nmo_status_t edit_plan_add_operation_core(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t operation_guid,
    nmo_object_id_t in1_parameter_id,
    edit_plan_handle_ref_t in1_ref,
    nmo_object_id_t in2_parameter_id,
    edit_plan_handle_ref_t in2_ref,
    nmo_object_id_t out_parameter_id,
    edit_plan_handle_ref_t out_ref)
{
    nmo_edit_op_t *op = NULL;
    if (nmo_guid_is_null(operation_guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_ADD_OPERATION, parent_behavior_id, &op));
    op->data.add_operation.parent_behavior_id = parent_behavior_id;
    op->data.add_operation.operation_guid = operation_guid;
    op->data.add_operation.in1_parameter_id = in1_parameter_id;
    op->data.add_operation.in2_parameter_id = in2_parameter_id;
    op->data.add_operation.out_parameter_id = out_parameter_id;
    edit_plan_handle_ref_clone_slot_t refs[] = {
        {&op->data.add_operation.in1_parameter_ref, &in1_ref},
        {&op->data.add_operation.in2_parameter_ref, &in2_ref},
        {&op->data.add_operation.out_parameter_ref, &out_ref},
    };
    nmo_status_t st = edit_op_clone_handle_ref_slots_or_dispose(
        op, refs, sizeof(refs) / sizeof(refs[0]));
    if (st != NMO_OK) {
        return st;
    }
    plan->count++;
    return NMO_OK;
}

static nmo_status_t edit_plan_add_rewire_operation_core(
    nmo_edit_plan_t *plan,
    nmo_object_id_t operation_id,
    uint32_t slot_flags,
    bool require_slot_flags,
    nmo_object_id_t in1_parameter_id,
    edit_plan_handle_ref_t in1_ref,
    nmo_object_id_t in2_parameter_id,
    edit_plan_handle_ref_t in2_ref,
    nmo_object_id_t out_parameter_id,
    edit_plan_handle_ref_t out_ref)
{
    nmo_edit_op_t *op = NULL;
    if (operation_id == 0u || (require_slot_flags && slot_flags == 0u)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_REWIRE_OPERATION, operation_id, &op));
    op->data.rewire_operation.operation_id = operation_id;
    op->data.rewire_operation.slot_flags = slot_flags;
    op->data.rewire_operation.in1_parameter_id = in1_parameter_id;
    op->data.rewire_operation.in2_parameter_id = in2_parameter_id;
    op->data.rewire_operation.out_parameter_id = out_parameter_id;
    edit_plan_handle_ref_clone_slot_t refs[] = {
        {&op->data.rewire_operation.in1_parameter_ref, &in1_ref},
        {&op->data.rewire_operation.in2_parameter_ref, &in2_ref},
        {&op->data.rewire_operation.out_parameter_ref, &out_ref},
    };
    nmo_status_t st = edit_op_clone_handle_ref_slots_or_dispose(
        op, refs, sizeof(refs) / sizeof(refs[0]));
    if (st != NMO_OK) {
        return st;
    }
    if (in1_ref.has_ref) {
        op->data.rewire_operation.slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_IN1;
    }
    if (in2_ref.has_ref) {
        op->data.rewire_operation.slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_IN2;
    }
    if (out_ref.has_ref) {
        op->data.rewire_operation.slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_OUT;
    }
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_set_parameter_value(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    const nmo_edit_handle_ref_t *parameter_ref,
    const char *value_str,
    const nmo_parameter_write_options_t *options)
{
    return edit_plan_add_set_parameter_value_core(
        plan,
        parameter_id,
        edit_plan_ref_or_none(parameter_ref),
        value_str,
        options);
}

nmo_status_t nmo_edit_plan_add_set_parameter_bytes(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    const nmo_edit_handle_ref_t *parameter_ref,
    const uint8_t *bytes,
    size_t byte_count,
    const nmo_parameter_write_options_t *options)
{
    return edit_plan_add_set_parameter_bytes_core(
        plan,
        parameter_id,
        edit_plan_ref_or_none(parameter_ref),
        bytes,
        byte_count,
        options);
}

nmo_status_t nmo_edit_plan_add_node(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t bb_guid,
    const char *name)
{
    return nmo_edit_plan_add_node_ex(
        plan, parent_behavior_id, bb_guid, name, NULL);
}

nmo_status_t nmo_edit_plan_add_node_ex(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t bb_guid,
    const char *name,
    const nmo_add_node_options_t *options)
{
    nmo_edit_op_t *op = NULL;
    if (plan == NULL || parent_behavior_id == 0 || nmo_guid_is_null(bb_guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_ADD_NODE, parent_behavior_id, &op));
    op->data.add_node.parent_behavior_id = parent_behavior_id;
    op->data.add_node.bb_guid = bb_guid;
    if (name != NULL) {
        op->data.add_node.name = edit_plan_strdup(name);
        if (op->data.add_node.name == NULL) {
            return NMO_ERR_NOMEM;
        }
    }
    if (options != NULL) {
        nmo_status_t st = edit_plan_add_node_options_clone(
            &op->data.add_node.options, options);
        if (st != NMO_OK) {
            edit_op_dispose(op);
            return st;
        }
        op->data.add_node.has_options = true;
    }
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_remove_node(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t node_id,
    uint32_t delete_flags)
{
    nmo_edit_op_t *op = NULL;
    if (node_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_REMOVE_NODE, parent_behavior_id, &op));
    op->data.remove_node.parent_behavior_id = parent_behavior_id;
    op->data.remove_node.node_id = node_id;
    op->data.remove_node.delete_flags = delete_flags;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_io(
    nmo_edit_plan_t *plan,
    nmo_object_id_t behavior_id,
    nmo_script_edit_io_kind_t kind,
    const char *name)
{
    nmo_edit_op_t *op = NULL;
    if (name == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_ADD_IO, behavior_id, &op));
    op->data.add_io.behavior_id = behavior_id;
    op->data.add_io.kind = kind;
    op->data.add_io.name = edit_plan_strdup(name);
    if (op->data.add_io.name == NULL) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_rename_io(
    nmo_edit_plan_t *plan,
    nmo_object_id_t io_id,
    const char *name)
{
    nmo_edit_op_t *op = NULL;
    if (name == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_RENAME_IO, io_id, &op));
    op->data.rename_io.io_id = io_id;
    op->data.rename_io.name = edit_plan_strdup(name);
    if (op->data.rename_io.name == NULL) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_remove_io(
    nmo_edit_plan_t *plan,
    nmo_object_id_t io_id,
    bool detach_links)
{
    nmo_edit_op_t *op = NULL;
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_REMOVE_IO, io_id, &op));
    op->data.remove_io.io_id = io_id;
    op->data.remove_io.detach_links = detach_links;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_behavior_link(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t from_io_id,
    const nmo_edit_handle_ref_t *from_io_ref,
    nmo_object_id_t to_io_id,
    const nmo_edit_handle_ref_t *to_io_ref,
    uint32_t activation_delay)
{
    return edit_plan_add_behavior_link_core(
        plan,
        parent_behavior_id,
        from_io_id,
        edit_plan_ref_or_none(from_io_ref),
        to_io_id,
        edit_plan_ref_or_none(to_io_ref),
        activation_delay);
}

nmo_status_t nmo_edit_plan_add_rewire_behavior_link(
    nmo_edit_plan_t *plan,
    nmo_object_id_t link_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id)
{
    nmo_edit_op_t *op = NULL;
    if (link_id == 0 || (from_io_id == 0 && to_io_id == 0)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK, link_id, &op));
    op->data.rewire_link.link_id = link_id;
    op->data.rewire_link.from_io_id = from_io_id;
    op->data.rewire_link.to_io_id = to_io_id;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_set_behavior_link_delay(
    nmo_edit_plan_t *plan,
    nmo_object_id_t link_id,
    uint32_t activation_delay)
{
    nmo_edit_op_t *op = NULL;
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY, link_id, &op));
    op->data.set_link_delay.link_id = link_id;
    op->data.set_link_delay.activation_delay = activation_delay;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_remove_behavior_link(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t link_id)
{
    nmo_edit_op_t *op = NULL;
    if (link_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK, parent_behavior_id, &op));
    op->data.remove_link.parent_behavior_id = parent_behavior_id;
    op->data.remove_link.link_id = link_id;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_parameter(
    nmo_edit_plan_t *plan,
    nmo_object_id_t owner_behavior_id,
    nmo_script_edit_parameter_kind_t kind,
    nmo_guid_t type_guid,
    const char *name)
{
    nmo_edit_op_t *op = NULL;
    if (nmo_guid_is_null(type_guid) || name == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_ADD_PARAMETER, owner_behavior_id, &op));
    op->data.add_parameter.owner_behavior_id = owner_behavior_id;
    op->data.add_parameter.kind = kind;
    op->data.add_parameter.type_guid = type_guid;
    op->data.add_parameter.name = edit_plan_strdup(name);
    if (op->data.add_parameter.name == NULL) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_connect_parameter(
    nmo_edit_plan_t *plan,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id,
    const nmo_edit_handle_ref_t *target_parameter_ref)
{
    return edit_plan_add_connect_parameter_core(
        plan,
        source_parameter_id,
        target_parameter_id,
        edit_plan_ref_or_none(target_parameter_ref));
}

nmo_status_t nmo_edit_plan_add_disconnect_parameter(
    nmo_edit_plan_t *plan,
    nmo_object_id_t target_parameter_id)
{
    nmo_edit_op_t *op = NULL;
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_DISCONNECT_PARAMETER, target_parameter_id, &op));
    op->data.disconnect_parameter.target_parameter_id = target_parameter_id;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_remove_parameter(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    bool detach)
{
    nmo_edit_op_t *op = NULL;
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_REMOVE_PARAMETER, parameter_id, &op));
    op->data.remove_parameter.parameter_id = parameter_id;
    op->data.remove_parameter.detach = detach;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_operation(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t operation_guid,
    nmo_object_id_t in1_parameter_id,
    const nmo_edit_handle_ref_t *in1_parameter_ref,
    nmo_object_id_t in2_parameter_id,
    const nmo_edit_handle_ref_t *in2_parameter_ref,
    nmo_object_id_t out_parameter_id,
    const nmo_edit_handle_ref_t *out_parameter_ref)
{
    return edit_plan_add_operation_core(
        plan,
        parent_behavior_id,
        operation_guid,
        in1_parameter_id,
        edit_plan_ref_or_none(in1_parameter_ref),
        in2_parameter_id,
        edit_plan_ref_or_none(in2_parameter_ref),
        out_parameter_id,
        edit_plan_ref_or_none(out_parameter_ref));
}

nmo_status_t nmo_edit_plan_add_rewire_operation(
    nmo_edit_plan_t *plan,
    nmo_object_id_t operation_id,
    uint32_t slot_flags,
    nmo_object_id_t in1_parameter_id,
    const nmo_edit_handle_ref_t *in1_parameter_ref,
    nmo_object_id_t in2_parameter_id,
    const nmo_edit_handle_ref_t *in2_parameter_ref,
    nmo_object_id_t out_parameter_id,
    const nmo_edit_handle_ref_t *out_parameter_ref)
{
    return edit_plan_add_rewire_operation_core(
        plan,
        operation_id,
        slot_flags,
        false,
        in1_parameter_id,
        edit_plan_ref_or_none(in1_parameter_ref),
        in2_parameter_id,
        edit_plan_ref_or_none(in2_parameter_ref),
        out_parameter_id,
        edit_plan_ref_or_none(out_parameter_ref));
}

nmo_status_t nmo_edit_plan_add_remove_operation(
    nmo_edit_plan_t *plan,
    nmo_object_id_t operation_id)
{
    nmo_edit_op_t *op = NULL;
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_REMOVE_OPERATION, operation_id, &op));
    op->data.remove_operation.operation_id = operation_id;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_interface_policy(
    nmo_edit_plan_t *plan,
    nmo_object_id_t behavior_id,
    nmo_script_edit_interface_mode_t mode)
{
    nmo_edit_op_t *op = NULL;
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_INTERFACE_POLICY, behavior_id, &op));
    op->data.interface_policy.behavior_id = behavior_id;
    op->data.interface_policy.mode = mode;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_data_cell(
    nmo_edit_plan_t *plan,
    nmo_object_id_t dataarray_id,
    uint32_t row,
    uint32_t col,
    const char *value)
{
    nmo_edit_op_t *op = NULL;
    if (value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_SET_DATA_CELL, dataarray_id, &op));
    op->data.data_cell.dataarray_id = dataarray_id;
    op->data.data_cell.row = row;
    op->data.data_cell.col = col;
    op->data.data_cell.value = edit_plan_strdup(value);
    if (op->data.data_cell.value == NULL) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_fold(
    nmo_edit_plan_t *plan,
    const nmo_behavior_fold_desc_t *desc)
{
    nmo_edit_op_t *op = NULL;
    if (desc == NULL || desc->parent_id == 0 || desc->node_count == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_FOLD, desc->parent_id, &op));
    op->data.fold.desc = *desc;
    op->data.fold.desc.name = edit_plan_strdup(desc->name);
    if (desc->name && !op->data.fold.desc.name) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    nmo_status_t rc = edit_plan_dup_object_ids(
        desc->node_ids, desc->node_count, &op->data.fold.node_ids);
    if (rc != NMO_OK) {
        edit_op_dispose(op);
        return rc;
    }
    op->data.fold.desc.node_ids = op->data.fold.node_ids;
    rc = edit_plan_dup_fold_maps(
        desc->input_maps, desc->input_map_count, &op->data.fold.input_maps);
    if (rc != NMO_OK) {
        edit_op_dispose(op);
        return rc;
    }
    op->data.fold.desc.input_maps = op->data.fold.input_maps;
    rc = edit_plan_dup_fold_maps(
        desc->output_maps, desc->output_map_count, &op->data.fold.output_maps);
    if (rc != NMO_OK) {
        edit_op_dispose(op);
        return rc;
    }
    op->data.fold.desc.output_maps = op->data.fold.output_maps;
    rc = edit_plan_dup_fold_maps(
        desc->parameter_maps,
        desc->parameter_map_count,
        &op->data.fold.parameter_maps);
    if (rc != NMO_OK) {
        edit_op_dispose(op);
        return rc;
    }
    op->data.fold.desc.parameter_maps = op->data.fold.parameter_maps;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_replace_bb(
    nmo_edit_plan_t *plan,
    const nmo_behavior_replace_bb_desc_t *desc)
{
    nmo_edit_op_t *op = NULL;
    if (desc == NULL || desc->behavior_id == 0 ||
        nmo_guid_is_null(desc->block_guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_REPLACE_BB, desc->behavior_id, &op));
    op->data.replace_bb.desc = *desc;
    op->data.replace_bb.desc.name = edit_plan_strdup(desc->name);
    if (desc->name && !op->data.replace_bb.desc.name) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    plan->count++;
    return NMO_OK;
}

nmo_edit_executor_options_t nmo_edit_executor_options_default(void)
{
    nmo_edit_executor_options_t options = {0};
    options.validation_flags =
        NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY |
        NMO_SCRIPT_EDIT_VALIDATE_REFERENCES |
        NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX |
        NMO_SCRIPT_EDIT_VALIDATE_INTERFACE;
    return options;
}

nmo_status_t nmo_edit_report_init(nmo_edit_report_t *report)
{
    if (report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memset(report, 0, sizeof(*report));
    return NMO_OK;
}

void nmo_edit_report_dispose(nmo_edit_report_t *report)
{
    if (report == NULL) {
        return;
    }
    for (size_t i = 0; i < report->operation_count; ++i) {
        if (report->operations != NULL) {
            for (size_t j = 0; j < report->operations[i].handle_count; ++j) {
                free((void *)report->operations[i].handles[j].name);
            }
            free((void *)report->operations[i].diagnostic_code);
            free((void *)report->operations[i].diagnostic_message);
            free(report->operations[i].handles);
        }
    }
    free(report->operations);
    free(report->changed_objects);
    free(report->created_objects);
    free(report->deleted_objects);
    free(report->semantic_risks);
    free(report->output_path);
    nmo_probe_analysis_dispose(&report->probe_selector_analysis);
    memset(report, 0, sizeof(*report));
}

nmo_status_t nmo_edit_report_set_output_path(
    nmo_edit_report_t *report,
    const char *output_path)
{
    if (report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    char *copy = edit_plan_strdup(output_path);
    if (output_path != NULL && copy == NULL) {
        return NMO_ERR_NOMEM;
    }
    free(report->output_path);
    report->output_path = copy;
    return NMO_OK;
}

static nmo_status_t edit_report_ensure_operations(
    nmo_edit_report_t *report,
    size_t needed)
{
    if (report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (needed <= report->operation_count) {
        return NMO_OK;
    }
    nmo_edit_operation_result_t *ops =
        (nmo_edit_operation_result_t *)realloc(
            report->operations, needed * sizeof(*ops));
    if (ops == NULL) {
        return NMO_ERR_NOMEM;
    }
    memset(ops + report->operation_count, 0,
           (needed - report->operation_count) * sizeof(*ops));
    report->operations = ops;
    report->operation_count = needed;
    return NMO_OK;
}

static nmo_status_t edit_report_prepare(nmo_edit_report_t *report,
                                        const nmo_edit_plan_t *plan,
                                        bool dry_run)
{
    if (report == NULL || plan == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_edit_report_dispose(report);
    report->dry_run = dry_run;
    report->operation_count = plan->count;
    if (plan->has_probe_selector_analysis) {
        nmo_status_t rc = edit_plan_probe_analysis_copy(
            &report->probe_selector_analysis,
            &plan->probe_selector_analysis);
        if (rc != NMO_OK) {
            nmo_edit_report_dispose(report);
            return rc;
        }
        report->has_probe_selector_analysis = true;
    }
    if (plan->count == 0) {
        return NMO_OK;
    }
    report->operations =
        (nmo_edit_operation_result_t *)calloc(plan->count, sizeof(*report->operations));
    if (report->operations == NULL) {
        nmo_edit_report_dispose(report);
        return NMO_ERR_NOMEM;
    }
    return NMO_OK;
}

static bool edit_report_has_changed_object(
    const nmo_edit_report_t *report,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    for (size_t i = 0; i < report->changed_object_count; i++) {
        const char *existing_role = report->changed_objects[i].role;
        bool same_role = existing_role == role ||
            (existing_role != NULL && role != NULL &&
             strcmp(existing_role, role) == 0);
        if (report->changed_objects[i].id == id &&
            report->changed_objects[i].cause == cause &&
            same_role) {
            return true;
        }
    }
    return false;
}

static nmo_status_t edit_report_add_impact(
    nmo_edit_object_impact_t **items,
    size_t *count,
    size_t *capacity,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    if (items == NULL || count == NULL || capacity == NULL || id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (*count == *capacity) {
        size_t next_capacity = *capacity == 0u ? 8u : *capacity * 2u;
        nmo_edit_object_impact_t *next =
            (nmo_edit_object_impact_t *)realloc(
                *items, next_capacity * sizeof(*next));
        if (next == NULL) {
            return NMO_ERR_NOMEM;
        }
        *items = next;
        *capacity = next_capacity;
    }
    (*items)[*count] = (nmo_edit_object_impact_t){
        .id = id,
        .cause = cause,
        .role = role,
    };
    ++(*count);
    return NMO_OK;
}

static nmo_edit_object_impact_t *edit_report_find_impact(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    for (size_t i = 0; items != NULL && i < count; ++i) {
        const char *existing_role = items[i].role;
        bool same_role = existing_role == role ||
            (existing_role != NULL && role != NULL &&
             strcmp(existing_role, role) == 0);
        if (items[i].id == id && items[i].cause == cause && same_role) {
            return &items[i];
        }
    }
    return NULL;
}

static void edit_report_set_control_link_after(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    uint32_t activation_delay)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL) {
        return;
    }
    impact->has_control_link_after = true;
    impact->after_from_io_id = from_io_id;
    impact->after_to_io_id = to_io_id;
    impact->after_activation_delay = activation_delay;
}

static void edit_report_set_control_link_before(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id,
    uint32_t activation_delay)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL) {
        return;
    }
    impact->has_control_link_before = true;
    impact->before_from_io_id = from_io_id;
    impact->before_to_io_id = to_io_id;
    impact->before_activation_delay = activation_delay;
}

static void edit_report_set_parameter_edge_before(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL) {
        return;
    }
    impact->has_parameter_edge_before = true;
    impact->before_source_parameter_id = source_parameter_id;
    impact->before_target_parameter_id = target_parameter_id;
}

static void edit_report_set_parameter_edge_after(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL) {
        return;
    }
    impact->has_parameter_edge_after = true;
    impact->after_source_parameter_id = source_parameter_id;
    impact->after_target_parameter_id = target_parameter_id;
}

static void edit_report_set_operation_slot_before(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    const nmo_parameteroperation_state_t *state)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL || state == NULL) {
        return;
    }
    impact->has_operation_slot_before = true;
    impact->before_operation_guid = state->operation_guid;
    impact->before_has_in1_parameter = state->has_in1 != 0u;
    impact->before_in1_parameter_id =
        state->has_in1 ? nmo_parameteroperation_in1_id(state) : 0u;
    impact->before_has_in2_parameter = state->has_in2 != 0u;
    impact->before_in2_parameter_id =
        state->has_in2 ? nmo_parameteroperation_in2_id(state) : 0u;
    impact->before_has_out_parameter = state->has_out != 0u;
    impact->before_out_parameter_id =
        state->has_out ? nmo_parameteroperation_out_id(state) : 0u;
}

static void edit_report_set_operation_slot_after(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    const nmo_parameteroperation_state_t *state)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL || state == NULL) {
        return;
    }
    impact->has_operation_slot_after = true;
    impact->after_operation_guid = state->operation_guid;
    impact->after_has_in1_parameter = state->has_in1 != 0u;
    impact->after_in1_parameter_id =
        state->has_in1 ? nmo_parameteroperation_in1_id(state) : 0u;
    impact->after_has_in2_parameter = state->has_in2 != 0u;
    impact->after_in2_parameter_id =
        state->has_in2 ? nmo_parameteroperation_in2_id(state) : 0u;
    impact->after_has_out_parameter = state->has_out != 0u;
    impact->after_out_parameter_id =
        state->has_out ? nmo_parameteroperation_out_id(state) : 0u;
}

static void edit_plan_format_data_cell_value(
    const nmo_dataarray_cell_t *cell,
    uint32_t type,
    char *buffer,
    size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0u) {
        return;
    }
    buffer[0] = '\0';
    if (cell == NULL) {
        return;
    }
    switch ((CK_ARRAYTYPE)type) {
    case CKARRAYTYPE_INT:
        snprintf(buffer, buffer_size, "%d", cell->int_value);
        break;
    case CKARRAYTYPE_FLOAT:
        snprintf(buffer, buffer_size, "%.9g", (double)cell->float_value);
        break;
    case CKARRAYTYPE_STRING:
        snprintf(buffer, buffer_size, "%s",
                 cell->string_value ? cell->string_value : "");
        break;
    case CKARRAYTYPE_OBJECT:
        snprintf(buffer, buffer_size, "%u",
                 nmo_ref_serialized_id(&cell->object_ref));
        break;
    case CKARRAYTYPE_PARAMETER:
        snprintf(buffer, buffer_size, "%u",
                 nmo_ref_serialized_id(&cell->parameter.ref));
        break;
    default:
        break;
    }
}

static const nmo_behavior_state_t *edit_plan_get_behavior_state(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t behavior_id)
{
    if (tx == NULL || behavior_id == 0u) {
        return NULL;
    }
    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    nmo_object_t *object = repo
        ? nmo_object_repository_find_by_id(repo, behavior_id)
        : NULL;
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
        return NULL;
    }
    return (const nmo_behavior_state_t *)nmo_object_get_state(object);
}

static const nmo_dataarray_cell_t *edit_plan_get_data_cell(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t dataarray_id,
    uint32_t row,
    uint32_t col,
    uint32_t *out_type)
{
    if (out_type != NULL) {
        *out_type = 0u;
    }
    if (tx == NULL || dataarray_id == 0u) {
        return NULL;
    }
    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    nmo_object_t *object = repo
        ? nmo_object_repository_find_by_id(repo, dataarray_id)
        : NULL;
    if (object == NULL ||
        nmo_object_get_class_id(object) != NMO_CID_DATAARRAY) {
        return NULL;
    }
    const nmo_dataarray_state_t *state =
        (const nmo_dataarray_state_t *)nmo_object_get_state(object);
    if (state == NULL || row >= state->row_count ||
        col >= state->column_count || state->rows == NULL ||
        state->rows[row].cells == NULL || state->column_formats == NULL) {
        return NULL;
    }
    if (out_type != NULL) {
        *out_type = (uint32_t)state->column_formats[col].type;
    }
    return &state->rows[row].cells[col];
}

static void edit_report_set_interface_before(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    const nmo_behavior_state_t *state)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL || state == NULL) {
        return;
    }
    impact->has_interface_before = true;
    impact->before_interface_behavior_id = id;
    impact->before_has_interface = state->has_interface;
    impact->before_has_interface_chunk = state->interface_chunk != NULL;
    impact->before_has_interface_data = state->interface_data != NULL;
    impact->before_interface_ids_are_runtime =
        state->interface_ids_are_runtime;
    impact->before_interface_version =
        state->interface_data ? state->interface_data->version : 0u;
    impact->before_interface_sub_count =
        state->interface_data ? (uint32_t)state->interface_data->sub_count : 0u;
}

static void edit_report_set_interface_after(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    const nmo_behavior_state_t *state)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL || state == NULL) {
        return;
    }
    impact->has_interface_after = true;
    impact->after_interface_behavior_id = id;
    impact->after_has_interface = state->has_interface;
    impact->after_has_interface_chunk = state->interface_chunk != NULL;
    impact->after_has_interface_data = state->interface_data != NULL;
    impact->after_interface_ids_are_runtime =
        state->interface_ids_are_runtime;
    impact->after_interface_version =
        state->interface_data ? state->interface_data->version : 0u;
    impact->after_interface_sub_count =
        state->interface_data ? (uint32_t)state->interface_data->sub_count : 0u;
}

static void edit_report_set_data_cell_before(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    uint32_t row,
    uint32_t col,
    uint32_t type,
    const nmo_dataarray_cell_t *cell)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL || cell == NULL) {
        return;
    }
    impact->has_data_cell_before = true;
    impact->before_data_cell_row = row;
    impact->before_data_cell_col = col;
    impact->before_data_cell_type = type;
    edit_plan_format_data_cell_value(
        cell, type, impact->before_data_cell_value,
        sizeof(impact->before_data_cell_value));
}

static void edit_report_set_data_cell_after(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    uint32_t row,
    uint32_t col,
    uint32_t type,
    const nmo_dataarray_cell_t *cell)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL || cell == NULL) {
        return;
    }
    impact->has_data_cell_after = true;
    impact->after_data_cell_row = row;
    impact->after_data_cell_col = col;
    impact->after_data_cell_type = type;
    edit_plan_format_data_cell_value(
        cell, type, impact->after_data_cell_value,
        sizeof(impact->after_data_cell_value));
}

static void edit_report_set_manager_entry_after(
    nmo_edit_object_impact_t *items,
    size_t count,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role,
    nmo_guid_t manager_guid,
    nmo_manager_entry_schema_t schema,
    const char *key,
    const char *category,
    nmo_guid_t type_guid,
    uint32_t entry_index,
    uint32_t entry_value,
    uint32_t compatible_class_id,
    uint32_t flags,
    bool created,
    bool manager_chunk_changed)
{
    nmo_edit_object_impact_t *impact =
        edit_report_find_impact(items, count, id, cause, role);
    if (impact == NULL) {
        return;
    }
    impact->has_manager_entry_after = true;
    impact->after_manager_guid = manager_guid;
    impact->after_manager_entry_schema = schema;
    if (key != NULL) {
        snprintf(impact->after_manager_entry_key,
                 sizeof(impact->after_manager_entry_key),
                 "%s",
                 key);
    }
    if (category != NULL) {
        snprintf(impact->after_manager_entry_category,
                 sizeof(impact->after_manager_entry_category),
                 "%s",
                 category);
    }
    impact->after_manager_entry_type_guid = type_guid;
    impact->after_manager_entry_index = entry_index;
    impact->after_manager_entry_value = entry_value;
    impact->after_manager_entry_compatible_class_id = compatible_class_id;
    impact->after_manager_entry_flags = flags;
    impact->after_manager_entry_created = created;
    impact->after_manager_chunk_changed = manager_chunk_changed;
}

static nmo_status_t edit_report_note_manager_entry_after(
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    const char *key,
    uint32_t entry_index,
    bool created,
    bool manager_chunk_changed)
{
    if (report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t rc = nmo_edit_report_add_changed_object(
        report, NMO_EDIT_MANAGER_ENTRY_IMPACT_ID, cause, "manager_entry");
    if (rc != NMO_OK) {
        return rc;
    }
    edit_report_set_manager_entry_after(
        report->changed_objects,
        report->changed_object_count,
        NMO_EDIT_MANAGER_ENTRY_IMPACT_ID,
        cause,
        "manager_entry",
        NMO_MANAGER_GUID_MESSAGE,
        NMO_MANAGER_ENTRY_SCHEMA_MESSAGE,
        key,
        NULL,
        (nmo_guid_t){0},
        entry_index,
        entry_index,
        0u,
        0u,
        created,
        manager_chunk_changed);
    return NMO_OK;
}

nmo_status_t nmo_edit_report_add_changed_object(
    nmo_edit_report_t *report,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    if (report == NULL || id == 0 ||
        edit_report_has_changed_object(report, id, cause, role)) {
        return report && id ? NMO_OK : NMO_ERR_INVALID_ARGUMENT;
    }
    size_t capacity = report->changed_object_count;
    if (report->changed_objects != NULL) {
        capacity = report->changed_object_count;
        while (capacity > 0 &&
               report->changed_objects[capacity - 1].id == 0) {
            capacity--;
        }
    }
    capacity = report->changed_object_count;
    return edit_report_add_impact(
        &report->changed_objects,
        &report->changed_object_count,
        &capacity,
        id,
        cause,
        role);
}

nmo_status_t nmo_edit_report_add_created_object(
    nmo_edit_report_t *report,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    return edit_report_add_impact(
        &report->created_objects,
        &report->created_object_count,
        &report->created_object_capacity,
        id,
        cause,
        role);
}

nmo_status_t nmo_edit_report_add_deleted_object(
    nmo_edit_report_t *report,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    return edit_report_add_impact(
        &report->deleted_objects,
        &report->deleted_object_count,
        &report->deleted_object_capacity,
        id,
        cause,
        role);
}

nmo_status_t nmo_edit_report_add_operation_handle(
    nmo_edit_report_t *report,
    size_t operation_index,
    const char *name,
    nmo_object_id_t id)
{
    if (report == NULL || id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_report_ensure_operations(
        report, operation_index + 1u));
    nmo_edit_operation_result_t *op = &report->operations[operation_index];
    nmo_edit_operation_handle_t *next =
        (nmo_edit_operation_handle_t *)realloc(
            op->handles,
            (op->handle_count + 1u) * sizeof(*next));
    if (next == NULL) {
        return NMO_ERR_NOMEM;
    }
    op->handles = next;
    const char *name_copy = edit_plan_strdup(name);
    if (name && !name_copy) {
        return NMO_ERR_NOMEM;
    }
    op->handles[op->handle_count++] =
        (nmo_edit_operation_handle_t){.name = name_copy, .id = id};
    return NMO_OK;
}

static nmo_status_t edit_report_set_operation_diagnostic(
    nmo_edit_report_t *report,
    size_t operation_index,
    const char *code,
    const char *message)
{
    if (report == NULL || (code == NULL && message == NULL)) {
        return NMO_OK;
    }
    NMO_RETURN_IF_ERROR(edit_report_ensure_operations(
        report, operation_index + 1u));
    nmo_edit_operation_result_t *op = &report->operations[operation_index];
    char *code_copy = edit_plan_strdup(code);
    if (code && !code_copy) {
        return NMO_ERR_NOMEM;
    }
    char *message_copy = edit_plan_strdup(message);
    if (message && !message_copy) {
        free(code_copy);
        return NMO_ERR_NOMEM;
    }
    free((void *)op->diagnostic_code);
    free((void *)op->diagnostic_message);
    op->diagnostic_code = code_copy;
    op->diagnostic_message = message_copy;
    return NMO_OK;
}

static nmo_status_t edit_report_note_created_objects(
    nmo_edit_report_t *report,
    const nmo_object_id_t *ids,
    size_t count,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    if (report == NULL || ids == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_created_object(
            report, ids[i], cause, role));
    }
    return NMO_OK;
}

static nmo_status_t edit_report_note_operation_slot_parameters(
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t in1_parameter_id,
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id)
{
    if (report == NULL) {
        return NMO_OK;
    }
    if (in1_parameter_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            in1_parameter_id,
            cause,
            "operation_slot_parameter"));
    }
    if (in2_parameter_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            in2_parameter_id,
            cause,
            "operation_slot_parameter"));
    }
    if (out_parameter_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            out_parameter_id,
            cause,
            "operation_slot_parameter"));
    }
    return NMO_OK;
}

static nmo_status_t edit_report_note_control_link_endpoints(
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id)
{
    if (report == NULL) {
        return NMO_OK;
    }
    if (from_io_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            from_io_id,
            cause,
            "control_link_endpoint"));
    }
    if (to_io_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            to_io_id,
            cause,
            "control_link_endpoint"));
    }
    return NMO_OK;
}

static nmo_status_t edit_report_note_io_detach_impacts(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t io_id)
{
    if (tx == NULL || report == NULL || io_id == 0u) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    if (repo == NULL) {
        return NMO_OK;
    }

    const size_t object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0u; i < object_count; ++i) {
        nmo_object_t *behavior_obj = nmo_object_repository_get_by_index(repo, i);
        nmo_behavior_state_t *behavior_state = behavior_obj &&
                nmo_object_get_class_id(behavior_obj) == NMO_CID_BEHAVIOR
            ? (nmo_behavior_state_t *)nmo_object_get_state(behavior_obj)
            : NULL;
        for (size_t j = 0u; behavior_state != NULL &&
                            j < behavior_state->sub_behavior_links.count; ++j) {
            nmo_object_id_t link_id = nmo_behavior_ref_array_get_id(
                &behavior_state->sub_behavior_links, j);
            if (link_id == 0) continue;
            nmo_object_t *link_obj =
                nmo_object_repository_find_by_id(repo, link_id);
            const nmo_behaviorlink_state_t *link_state = link_obj
                ? (const nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj)
                : NULL;
            if (link_state == NULL ||
                (nmo_behaviorlink_in_io_id(link_state) != io_id &&
                 nmo_behaviorlink_out_io_id(link_state) != io_id)) {
                continue;
            }
            NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
                report,
                link_id,
                cause,
                "detached_control_link"));
            NMO_RETURN_IF_ERROR(edit_report_note_control_link_endpoints(
                report,
                cause,
                nmo_behaviorlink_in_io_id(link_state),
                nmo_behaviorlink_out_io_id(link_state)));
        }
    }

    return NMO_OK;
}

static nmo_status_t edit_report_note_parameter_edge_source(
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t source_parameter_id)
{
    if (report == NULL || source_parameter_id == 0u) {
        return NMO_OK;
    }
    return nmo_edit_report_add_changed_object(
        report,
        source_parameter_id,
        cause,
        "parameter_edge_source");
}

static nmo_object_id_t edit_plan_get_parameterin_source(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t target_parameter_id)
{
    if (tx == NULL || target_parameter_id == 0u) {
        return 0u;
    }
    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    nmo_object_t *object = repo
        ? nmo_object_repository_find_by_id(repo, target_parameter_id)
        : NULL;
    nmo_parameterin_state_t *state = object
        ? (nmo_parameterin_state_t *)nmo_object_get_state(object)
        : NULL;
    return nmo_parameterin_source_id(state);
}

static void edit_plan_get_behavior_link_endpoints(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t link_id,
    nmo_object_id_t *out_from_io_id,
    nmo_object_id_t *out_to_io_id,
    uint32_t *out_activation_delay)
{
    if (out_from_io_id != NULL) {
        *out_from_io_id = 0u;
    }
    if (out_to_io_id != NULL) {
        *out_to_io_id = 0u;
    }
    if (out_activation_delay != NULL) {
        *out_activation_delay = 0u;
    }
    if (tx == NULL || link_id == 0u) {
        return;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    nmo_object_t *object = repo
        ? nmo_object_repository_find_by_id(repo, link_id)
        : NULL;
    nmo_behaviorlink_state_t *state = object
        ? (nmo_behaviorlink_state_t *)nmo_object_get_state(object)
        : NULL;
    if (state == NULL) {
        return;
    }
    if (out_from_io_id != NULL) {
        *out_from_io_id = nmo_behaviorlink_in_io_id(state);
    }
    if (out_to_io_id != NULL) {
        *out_to_io_id = nmo_behaviorlink_out_io_id(state);
    }
    if (out_activation_delay != NULL) {
        *out_activation_delay =
            state->initial_activation_delay > 0
                ? (uint32_t)state->initial_activation_delay
                : 0u;
    }
}

static void edit_plan_get_parameter_operation_slots(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t operation_id,
    nmo_object_id_t *out_in1_parameter_id,
    nmo_object_id_t *out_in2_parameter_id,
    nmo_object_id_t *out_out_parameter_id)
{
    if (out_in1_parameter_id != NULL) {
        *out_in1_parameter_id = 0u;
    }
    if (out_in2_parameter_id != NULL) {
        *out_in2_parameter_id = 0u;
    }
    if (out_out_parameter_id != NULL) {
        *out_out_parameter_id = 0u;
    }
    if (tx == NULL || operation_id == 0u) {
        return;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    nmo_object_t *object = repo
        ? nmo_object_repository_find_by_id(repo, operation_id)
        : NULL;
    nmo_parameteroperation_state_t *state = object
        ? (nmo_parameteroperation_state_t *)nmo_object_get_state(object)
        : NULL;
    if (state == NULL) {
        return;
    }
    if (out_in1_parameter_id != NULL && state->has_in1) {
        *out_in1_parameter_id = nmo_parameteroperation_in1_id(state);
    }
    if (out_in2_parameter_id != NULL && state->has_in2) {
        *out_in2_parameter_id = nmo_parameteroperation_in2_id(state);
    }
    if (out_out_parameter_id != NULL && state->has_out) {
        *out_out_parameter_id = nmo_parameteroperation_out_id(state);
    }
}

static const nmo_parameteroperation_state_t *edit_plan_get_operation_state(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t operation_id)
{
    if (tx == NULL || operation_id == 0u) {
        return NULL;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    nmo_object_t *object = repo
        ? nmo_object_repository_find_by_id(repo, operation_id)
        : NULL;
    if (object == NULL ||
        nmo_object_get_class_id(object) != NMO_CID_PARAMETEROPERATION) {
        return NULL;
    }
    return (const nmo_parameteroperation_state_t *)nmo_object_get_state(object);
}

static nmo_status_t edit_report_note_parameter_detach_impacts(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t parameter_id)
{
    if (tx == NULL || report == NULL || parameter_id == 0u) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    if (repo == NULL) {
        return NMO_OK;
    }

    const size_t object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        if (object == NULL) {
            continue;
        }

        if (nmo_object_get_class_id(object) == NMO_CID_PARAMETERIN) {
            const nmo_parameterin_state_t *state =
                (const nmo_parameterin_state_t *)nmo_object_get_state(object);
            if (nmo_parameterin_source_id(state) == parameter_id) {
                NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
                    report,
                    nmo_object_get_id(object),
                    cause,
                    "parameter_edge_target"));
            }
        } else if (nmo_object_get_class_id(object) == NMO_CID_PARAMETEROPERATION) {
            const nmo_parameteroperation_state_t *state =
                (const nmo_parameteroperation_state_t *)nmo_object_get_state(object);
            if (state == NULL) {
                continue;
            }

            if ((state->has_in1 && nmo_parameteroperation_in1_id(state) == parameter_id) ||
                (state->has_in2 && nmo_parameteroperation_in2_id(state) == parameter_id) ||
                (state->has_out && nmo_parameteroperation_out_id(state) == parameter_id)) {
                NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
                    report,
                    nmo_object_get_id(object),
                    cause,
                    "operation_slot_owner"));
            }
        }
    }

    return NMO_OK;
}

static nmo_status_t edit_report_note_behavior_owned_deleted_objects(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t behavior_id)
{
    if (tx == NULL || report == NULL || behavior_id == 0u) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    nmo_object_t *object = repo
        ? nmo_object_repository_find_by_id(repo, behavior_id)
        : NULL;
    nmo_behavior_state_t *state = object
        ? (nmo_behavior_state_t *)nmo_object_get_state(object)
        : NULL;
    if (state == NULL) {
        return NMO_OK;
    }

    const nmo_object_id_t target_parameter_id =
        nmo_behavior_target_parameter_id(state);
    if (target_parameter_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
            report,
            target_parameter_id,
            cause,
            "target_parameter"));
    }

    const struct {
        const nmo_array_t *array;
        const char *role;
    } owned_arrays[] = {
        { &state->inputs, "owned_io" },
        { &state->outputs, "owned_io" },
        { &state->in_parameters, "owned_parameter" },
        { &state->out_parameters, "owned_parameter" },
        { &state->local_parameters, "owned_parameter" },
        { &state->operations, "owned_operation" },
        { &state->sub_behavior_links, "owned_link" },
    };

    for (size_t i = 0u; i < sizeof(owned_arrays) / sizeof(owned_arrays[0]); ++i) {
        const nmo_array_t *array = owned_arrays[i].array;
        for (size_t j = 0u; array != NULL && j < array->count; ++j) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(array, j);
            if (id == 0) continue;
            NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
                report,
                id,
                cause,
                owned_arrays[i].role));
        }
    }

    for (size_t i = 0u; i < state->sub_behaviors.count; ++i) {
        nmo_object_id_t sub_id = nmo_behavior_ref_array_get_id(
            &state->sub_behaviors, i);
        if (sub_id == 0) continue;
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
            report,
            sub_id,
            cause,
            "owned_node"));
        NMO_RETURN_IF_ERROR(edit_report_note_behavior_owned_deleted_objects(
            tx,
            report,
            cause,
            sub_id));
    }

    return NMO_OK;
}

static nmo_status_t edit_report_note_behavior_io_detach_impacts(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t behavior_id)
{
    if (tx == NULL || report == NULL || behavior_id == 0u) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    nmo_object_t *object = repo
        ? nmo_object_repository_find_by_id(repo, behavior_id)
        : NULL;
    nmo_behavior_state_t *state = object
        ? (nmo_behavior_state_t *)nmo_object_get_state(object)
        : NULL;
    if (state == NULL) {
        return NMO_OK;
    }

    const nmo_array_t *io_arrays[] = {
        &state->inputs,
        &state->outputs,
    };
    for (size_t i = 0u; i < sizeof(io_arrays) / sizeof(io_arrays[0]); ++i) {
        const nmo_array_t *array = io_arrays[i];
        for (size_t j = 0u; array != NULL && j < array->count; ++j) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(array, j);
            if (id == 0) continue;
            NMO_RETURN_IF_ERROR(edit_report_note_io_detach_impacts(
                tx, report, cause, id));
        }
    }

    return NMO_OK;
}

static nmo_status_t edit_report_note_behavior_parameter_detach_impacts(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t behavior_id)
{
    if (tx == NULL || report == NULL || behavior_id == 0u) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    nmo_object_t *object = repo
        ? nmo_object_repository_find_by_id(repo, behavior_id)
        : NULL;
    nmo_behavior_state_t *state = object
        ? (nmo_behavior_state_t *)nmo_object_get_state(object)
        : NULL;
    if (state == NULL) {
        return NMO_OK;
    }

    const nmo_array_t *param_arrays[] = {
        &state->in_parameters,
        &state->out_parameters,
        &state->local_parameters,
    };
    for (size_t i = 0u; i < sizeof(param_arrays) / sizeof(param_arrays[0]); ++i) {
        const nmo_array_t *array = param_arrays[i];
        for (size_t j = 0u; array != NULL && j < array->count; ++j) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(array, j);
            if (id == 0) continue;
            nmo_object_id_t source_id =
                edit_plan_get_parameterin_source(tx, id);
            NMO_RETURN_IF_ERROR(edit_report_note_parameter_edge_source(
                report, cause, source_id));
            NMO_RETURN_IF_ERROR(edit_report_note_parameter_detach_impacts(
                tx, report, cause, id));
        }
    }

    const nmo_object_id_t target_parameter_id =
        nmo_behavior_target_parameter_id(state);
    if (target_parameter_id != 0u) {
        nmo_object_id_t source_id =
            edit_plan_get_parameterin_source(tx, target_parameter_id);
        NMO_RETURN_IF_ERROR(edit_report_note_parameter_edge_source(
            report, cause, source_id));
        NMO_RETURN_IF_ERROR(edit_report_note_parameter_detach_impacts(
            tx, report, cause, target_parameter_id));
    }

    return NMO_OK;
}

nmo_status_t nmo_edit_report_merge_semantic_risks(
    nmo_edit_report_t *report,
    const nmo_behavior_semantic_risk_t *risks,
    size_t risk_count)
{
    if (report == NULL || (risk_count > 0u && risks == NULL)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (risk_count == 0u) {
        return NMO_OK;
    }
    for (size_t i = 0; i < risk_count; ++i) {
        bool exists = false;
        for (size_t j = 0; j < report->semantic_risk_count; ++j) {
            const nmo_behavior_semantic_risk_t *existing =
                &report->semantic_risks[j];
            if (existing->severity == risks[i].severity &&
                existing->object_id == risks[i].object_id &&
                ((existing->code == NULL && risks[i].code == NULL) ||
                 (existing->code != NULL && risks[i].code != NULL &&
                  strcmp(existing->code, risks[i].code) == 0))) {
                exists = true;
                break;
            }
        }
        if (exists) {
            continue;
        }
        if (report->semantic_risk_count + 1u >
            report->semantic_risk_capacity) {
            size_t new_capacity = report->semantic_risk_capacity == 0u
                ? 8u
                : report->semantic_risk_capacity * 2u;
            nmo_behavior_semantic_risk_t *next =
                (nmo_behavior_semantic_risk_t *)realloc(
                    report->semantic_risks,
                    new_capacity * sizeof(*next));
            if (next == NULL) {
                return NMO_ERR_NOMEM;
            }
            report->semantic_risks = next;
            report->semantic_risk_capacity = new_capacity;
        }
        report->semantic_risks[report->semantic_risk_count++] = risks[i];
    }
    return NMO_OK;
}

static nmo_status_t edit_report_note_fold_impact(
    nmo_edit_report_t *report,
    const nmo_behavior_fold_report_t *fold_report,
    nmo_object_id_t parent_id)
{
    if (report == NULL || fold_report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
        report, parent_id, NMO_EDIT_OP_FOLD, "parent"));
    NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
        report, fold_report->anchor_id, NMO_EDIT_OP_FOLD, "anchor"));

    for (size_t i = 0; i < fold_report->selected_node_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            fold_report->selected_nodes[i],
            NMO_EDIT_OP_FOLD,
            "selected_node"));
    }
    for (size_t i = 0; i < fold_report->nodes_to_delete_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
            report,
            fold_report->nodes_to_delete[i],
            NMO_EDIT_OP_FOLD,
            "folded_node"));
    }
    for (size_t i = 0; i < fold_report->control_links_to_delete_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
            report,
            fold_report->control_links_to_delete[i].link_id,
            NMO_EDIT_OP_FOLD,
            "folded_control_link"));
    }

    const nmo_behavior_boundary_t *boundary = &fold_report->boundary;
    for (size_t i = 0; i < boundary->control_in_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            boundary->control_in[i].link_id,
            NMO_EDIT_OP_FOLD,
            "boundary_control_link"));
    }
    for (size_t i = 0; i < boundary->control_out_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            boundary->control_out[i].link_id,
            NMO_EDIT_OP_FOLD,
            "boundary_control_link"));
    }
    for (size_t i = 0; i < boundary->parameter_in_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            boundary->parameter_in[i].source_parameter_id,
            NMO_EDIT_OP_FOLD,
            "boundary_parameter_source"));
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            boundary->parameter_in[i].target_parameter_id,
            NMO_EDIT_OP_FOLD,
            "boundary_parameter_target"));
    }
    for (size_t i = 0; i < boundary->parameter_out_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            boundary->parameter_out[i].source_parameter_id,
            NMO_EDIT_OP_FOLD,
            "boundary_parameter_source"));
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            boundary->parameter_out[i].target_parameter_id,
            NMO_EDIT_OP_FOLD,
            "boundary_parameter_target"));
    }
    return NMO_OK;
}

static nmo_status_t edit_report_resolve_operation_handle(
    const nmo_edit_report_t *report,
    size_t operation_index,
    const char *handle_name,
    nmo_object_id_t *out_id);

static void edit_executor_set_diagnostic(
    const char **out_diagnostic_code,
    const char **out_diagnostic_message,
    const char *diagnostic_code,
    const char *diagnostic_message)
{
    if (out_diagnostic_code != NULL) {
        *out_diagnostic_code = diagnostic_code;
    }
    if (out_diagnostic_message != NULL) {
        *out_diagnostic_message = diagnostic_message;
    }
}

typedef struct edit_executor_handle_slot {
    const edit_plan_handle_ref_t *ref;
    nmo_object_id_t *id;
    const char *diagnostic_code;
    const char *diagnostic_message;
    bool resolve_input_parameter_source;
    bool source_requires_ref;
} edit_executor_handle_slot_t;

static nmo_status_t edit_executor_resolve_handle_ref(
    const nmo_edit_report_t *report,
    edit_plan_handle_ref_t ref,
    nmo_object_id_t *in_out_id,
    const char *diagnostic_code,
    const char *diagnostic_message,
    const char **out_diagnostic_code,
    const char **out_diagnostic_message)
{
    if (!ref.has_ref) {
        return NMO_OK;
    }
    nmo_status_t rc = edit_report_resolve_operation_handle(
        report, ref.operation_index, ref.handle_name, in_out_id);
    if (rc != NMO_OK) {
        edit_executor_set_diagnostic(
            out_diagnostic_code,
            out_diagnostic_message,
            diagnostic_code,
            diagnostic_message);
    }
    return rc;
}

static nmo_status_t edit_executor_resolve_input_parameter_source(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t *parameter_id)
{
    if (tx == NULL || parameter_id == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    nmo_object_t *parameter_obj = repo != NULL
        ? nmo_object_repository_find_by_id(repo, *parameter_id)
        : NULL;
    if (parameter_obj != NULL &&
        nmo_object_get_class_id(parameter_obj) == NMO_CID_PARAMETERIN) {
        return nmo_script_edit_ensure_input_parameter_source(
            tx, *parameter_id, parameter_id);
    }
    return NMO_OK;
}

static nmo_status_t edit_executor_resolve_handle_slots(
    nmo_script_edit_tx_t *tx,
    const nmo_edit_report_t *report,
    const edit_executor_handle_slot_t *slots,
    size_t slot_count,
    const char **out_diagnostic_code,
    const char **out_diagnostic_message)
{
    if (slot_count == 0u) {
        return NMO_OK;
    }
    if (slots == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < slot_count; ++i) {
        const edit_executor_handle_slot_t *slot = &slots[i];
        if (slot->id == NULL) {
            return NMO_ERR_INVALID_ARGUMENT;
        }

        bool has_ref = slot->ref != NULL && slot->ref->has_ref;
        if (has_ref) {
            nmo_status_t rc = edit_executor_resolve_handle_ref(
                report,
                *slot->ref,
                slot->id,
                slot->diagnostic_code,
                slot->diagnostic_message,
                out_diagnostic_code,
                out_diagnostic_message);
            if (rc != NMO_OK) {
                return rc;
            }
        }

        if (slot->resolve_input_parameter_source &&
            (has_ref || !slot->source_requires_ref)) {
            nmo_status_t rc =
                edit_executor_resolve_input_parameter_source(tx, slot->id);
            if (rc != NMO_OK) {
                return rc;
            }
        }
    }
    return NMO_OK;
}

static nmo_status_t edit_executor_apply_op(
    nmo_script_edit_tx_t *tx,
    const nmo_edit_op_t *op,
    nmo_object_id_t *out_result_id,
    bool dry_run,
    nmo_edit_report_t *report,
    const char **out_diagnostic_code,
    const char **out_diagnostic_message)
{
    nmo_workspace_edit_t *edit = NULL;
    if (tx == NULL || op == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (out_result_id != NULL) {
        *out_result_id = 0;
    }
    edit = nmo_script_edit_workspace_edit(tx);
    if (edit == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    switch (op->kind) {
    case NMO_EDIT_OP_SET_PARAMETER_VALUE: {
        nmo_object_id_t parameter_id = op->primary_id;
        edit_executor_handle_slot_t parameter_slot = {
            .ref = &op->data.set_value.parameter_ref,
            .id = &parameter_id,
            .diagnostic_code = "handle_not_found",
            .diagnostic_message =
                "Referenced edit operation handle was not found",
            .resolve_input_parameter_source = true,
            .source_requires_ref = false,
        };
        NMO_RETURN_IF_ERROR(edit_executor_resolve_handle_slots(
            tx,
            report,
            &parameter_slot,
            1u,
            out_diagnostic_code,
            out_diagnostic_message));
        if (out_result_id != NULL) {
            *out_result_id = parameter_id;
        }
        nmo_status_t write_rc = nmo_object_edit_set_parameter_value_ex(
            edit,
            parameter_id,
            op->data.set_value.value,
            op->data.set_value.has_options ? &op->data.set_value.options : NULL);
        if (write_rc == NMO_OK) {
            nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                                     NMO_WORKSPACE_EDIT_REFERENCES);
        }
        return write_rc;
    }
    case NMO_EDIT_OP_SET_PARAMETER_BYTES:
    {
        nmo_object_id_t parameter_id = op->primary_id;
        edit_executor_handle_slot_t parameter_slot = {
            .ref = &op->data.set_bytes.parameter_ref,
            .id = &parameter_id,
            .diagnostic_code = "handle_not_found",
            .diagnostic_message =
                "Referenced edit operation parameter handle was not found",
            .resolve_input_parameter_source = true,
            .source_requires_ref = true,
        };
        NMO_RETURN_IF_ERROR(edit_executor_resolve_handle_slots(
            tx,
            report,
            &parameter_slot,
            1u,
            out_diagnostic_code,
            out_diagnostic_message));
        if (out_result_id != NULL) {
            *out_result_id = parameter_id;
        }
        return nmo_object_edit_set_parameter_bytes_ex(
            edit,
            parameter_id,
            op->data.set_bytes.bytes,
            op->data.set_bytes.byte_count,
            op->data.set_bytes.has_options ? &op->data.set_bytes.options : NULL);
    }
    case NMO_EDIT_OP_ADD_NODE:
        return nmo_script_edit_add_node_ex(
            tx,
            op->data.add_node.parent_behavior_id,
            op->data.add_node.bb_guid,
            op->data.add_node.name,
            op->data.add_node.has_options
                ? &(nmo_script_edit_add_node_options_t){
                      .manager_entry =
                          op->data.add_node.options.manager_entry,
                  }
                : NULL,
            out_result_id);
    case NMO_EDIT_OP_REMOVE_NODE:
    {
        NMO_RETURN_IF_ERROR(edit_report_note_behavior_owned_deleted_objects(
            tx,
            report,
            NMO_EDIT_OP_REMOVE_NODE,
            op->data.remove_node.node_id));
        NMO_RETURN_IF_ERROR(edit_report_note_behavior_io_detach_impacts(
            tx,
            report,
            NMO_EDIT_OP_REMOVE_NODE,
            op->data.remove_node.node_id));
        NMO_RETURN_IF_ERROR(edit_report_note_behavior_parameter_detach_impacts(
            tx,
            report,
            NMO_EDIT_OP_REMOVE_NODE,
            op->data.remove_node.node_id));
        return nmo_script_edit_remove_node(
            tx,
            op->data.remove_node.parent_behavior_id,
            op->data.remove_node.node_id,
            op->data.remove_node.delete_flags);
    }
    case NMO_EDIT_OP_ADD_IO:
        return nmo_script_edit_add_io(
            tx,
            op->data.add_io.behavior_id,
            op->data.add_io.kind,
            op->data.add_io.name,
            out_result_id);
    case NMO_EDIT_OP_RENAME_IO:
        return nmo_script_edit_rename_io(
            tx,
            op->data.rename_io.io_id,
            op->data.rename_io.name);
    case NMO_EDIT_OP_REMOVE_IO:
    {
        if (op->data.remove_io.detach_links) {
            NMO_RETURN_IF_ERROR(edit_report_note_io_detach_impacts(
                tx,
                report,
                NMO_EDIT_OP_REMOVE_IO,
                op->data.remove_io.io_id));
        }
        return nmo_script_edit_remove_io(
            tx,
            op->data.remove_io.io_id,
            op->data.remove_io.detach_links);
    }
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK: {
        nmo_object_id_t from_io_id = op->data.add_link.from_io_id;
        nmo_object_id_t to_io_id = op->data.add_link.to_io_id;
        edit_executor_handle_slot_t slots[] = {
            {
                .ref = &op->data.add_link.from_io_ref,
                .id = &from_io_id,
                .diagnostic_code = "handle_not_found",
                .diagnostic_message =
                    "Referenced edit operation output IO handle was not found",
            },
            {
                .ref = &op->data.add_link.to_io_ref,
                .id = &to_io_id,
                .diagnostic_code = "handle_not_found",
                .diagnostic_message =
                    "Referenced edit operation input IO handle was not found",
            },
        };
        NMO_RETURN_IF_ERROR(edit_executor_resolve_handle_slots(
            tx,
            report,
            slots,
            sizeof(slots) / sizeof(slots[0]),
            out_diagnostic_code,
            out_diagnostic_message));
        nmo_status_t rc = nmo_script_edit_add_behavior_link(
            tx,
            op->data.add_link.parent_behavior_id,
            from_io_id,
            to_io_id,
            op->data.add_link.activation_delay,
            out_result_id);
        if (rc != NMO_OK) {
            return rc;
        }
        return edit_report_note_control_link_endpoints(
            report,
            NMO_EDIT_OP_ADD_BEHAVIOR_LINK,
            from_io_id,
            to_io_id);
    }
    case NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK: {
        nmo_object_id_t before_from_io_id = 0u;
        nmo_object_id_t before_to_io_id = 0u;
        uint32_t before_activation_delay = 0u;
        edit_plan_get_behavior_link_endpoints(
            tx,
            op->data.rewire_link.link_id,
            &before_from_io_id,
            &before_to_io_id,
            &before_activation_delay);
        nmo_status_t rc = nmo_script_edit_rewire_behavior_link(
            tx,
            op->data.rewire_link.link_id,
            op->data.rewire_link.from_io_id,
            op->data.rewire_link.to_io_id);
        if (rc != NMO_OK) {
            return rc;
        }
        nmo_object_id_t after_from_io_id = 0u;
        nmo_object_id_t after_to_io_id = 0u;
        uint32_t after_activation_delay = 0u;
        edit_plan_get_behavior_link_endpoints(
            tx,
            op->data.rewire_link.link_id,
            &after_from_io_id,
            &after_to_io_id,
            &after_activation_delay);
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            op->data.rewire_link.link_id,
            NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK,
            "primary"));
        edit_report_set_control_link_before(
            report->changed_objects,
            report->changed_object_count,
            op->data.rewire_link.link_id,
            NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK,
            "primary",
            before_from_io_id,
            before_to_io_id,
            before_activation_delay);
        edit_report_set_control_link_after(
            report->changed_objects,
            report->changed_object_count,
            op->data.rewire_link.link_id,
            NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK,
            "primary",
            after_from_io_id,
            after_to_io_id,
            after_activation_delay);
        return edit_report_note_control_link_endpoints(
            report,
            NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK,
            op->data.rewire_link.from_io_id,
            op->data.rewire_link.to_io_id);
    }
    case NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY: {
        nmo_object_id_t before_from_io_id = 0u;
        nmo_object_id_t before_to_io_id = 0u;
        uint32_t before_activation_delay = 0u;
        edit_plan_get_behavior_link_endpoints(
            tx,
            op->data.set_link_delay.link_id,
            &before_from_io_id,
            &before_to_io_id,
            &before_activation_delay);
        nmo_status_t rc = nmo_script_edit_set_behavior_link_delay(
            tx,
            op->data.set_link_delay.link_id,
            op->data.set_link_delay.activation_delay);
        if (rc != NMO_OK) {
            return rc;
        }
        nmo_object_id_t after_from_io_id = 0u;
        nmo_object_id_t after_to_io_id = 0u;
        uint32_t after_activation_delay = 0u;
        edit_plan_get_behavior_link_endpoints(
            tx,
            op->data.set_link_delay.link_id,
            &after_from_io_id,
            &after_to_io_id,
            &after_activation_delay);
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            op->data.set_link_delay.link_id,
            NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY,
            "primary"));
        edit_report_set_control_link_before(
            report->changed_objects,
            report->changed_object_count,
            op->data.set_link_delay.link_id,
            NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY,
            "primary",
            before_from_io_id,
            before_to_io_id,
            before_activation_delay);
        edit_report_set_control_link_after(
            report->changed_objects,
            report->changed_object_count,
            op->data.set_link_delay.link_id,
            NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY,
            "primary",
            after_from_io_id,
            after_to_io_id,
            after_activation_delay);
        return NMO_OK;
    }
    case NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK:
    {
        nmo_object_id_t from_io_id = 0u;
        nmo_object_id_t to_io_id = 0u;
        uint32_t activation_delay = 0u;
        edit_plan_get_behavior_link_endpoints(
            tx,
            op->data.remove_link.link_id,
            &from_io_id,
            &to_io_id,
            &activation_delay);
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
            report,
            op->data.remove_link.link_id,
            NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK,
            "primary"));
        edit_report_set_control_link_before(
            report->deleted_objects,
            report->deleted_object_count,
            op->data.remove_link.link_id,
            NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK,
            "primary",
            from_io_id,
            to_io_id,
            activation_delay);
        nmo_status_t rc = nmo_script_edit_remove_behavior_link(
            tx,
            op->data.remove_link.parent_behavior_id,
            op->data.remove_link.link_id);
        if (rc != NMO_OK) {
            return rc;
        }
        return edit_report_note_control_link_endpoints(
            report,
            NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK,
            from_io_id,
            to_io_id);
    }
    case NMO_EDIT_OP_ADD_PARAMETER:
        return nmo_script_edit_add_parameter(
            tx,
            op->data.add_parameter.owner_behavior_id,
            op->data.add_parameter.kind,
            op->data.add_parameter.type_guid,
            op->data.add_parameter.name,
            out_result_id);
    case NMO_EDIT_OP_CONNECT_PARAMETER: {
        nmo_object_id_t target_parameter_id =
            op->data.connect_parameter.target_parameter_id;
        edit_executor_handle_slot_t target_slot = {
            .ref = &op->data.connect_parameter.target_parameter_ref,
            .id = &target_parameter_id,
            .diagnostic_code = "handle_not_found",
            .diagnostic_message =
                "Referenced edit operation parameter handle was not found",
        };
        NMO_RETURN_IF_ERROR(edit_executor_resolve_handle_slots(
            tx,
            report,
            &target_slot,
            1u,
            out_diagnostic_code,
            out_diagnostic_message));
        nmo_object_id_t before_source_parameter_id =
            edit_plan_get_parameterin_source(tx, target_parameter_id);
        nmo_status_t rc = nmo_script_edit_connect_parameter(
            tx,
            op->data.connect_parameter.source_parameter_id,
            target_parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
        if (out_result_id != NULL) {
            *out_result_id = target_parameter_id;
        }
        nmo_object_id_t after_source_parameter_id =
            edit_plan_get_parameterin_source(tx, target_parameter_id);
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            target_parameter_id,
            NMO_EDIT_OP_CONNECT_PARAMETER,
            "primary"));
        edit_report_set_parameter_edge_before(
            report->changed_objects,
            report->changed_object_count,
            target_parameter_id,
            NMO_EDIT_OP_CONNECT_PARAMETER,
            "primary",
            before_source_parameter_id,
            target_parameter_id);
        edit_report_set_parameter_edge_after(
            report->changed_objects,
            report->changed_object_count,
            target_parameter_id,
            NMO_EDIT_OP_CONNECT_PARAMETER,
            "primary",
            after_source_parameter_id,
            target_parameter_id);
        return edit_report_note_parameter_edge_source(
            report,
            NMO_EDIT_OP_CONNECT_PARAMETER,
            op->data.connect_parameter.source_parameter_id);
    }
    case NMO_EDIT_OP_DISCONNECT_PARAMETER: {
        nmo_object_id_t old_source_parameter_id =
            edit_plan_get_parameterin_source(
                tx,
                op->data.disconnect_parameter.target_parameter_id);
        nmo_status_t rc = nmo_script_edit_disconnect_parameter(
            tx,
            op->data.disconnect_parameter.target_parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
        nmo_object_id_t after_source_parameter_id =
            edit_plan_get_parameterin_source(
                tx,
                op->data.disconnect_parameter.target_parameter_id);
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            op->data.disconnect_parameter.target_parameter_id,
            NMO_EDIT_OP_DISCONNECT_PARAMETER,
            "primary"));
        edit_report_set_parameter_edge_before(
            report->changed_objects,
            report->changed_object_count,
            op->data.disconnect_parameter.target_parameter_id,
            NMO_EDIT_OP_DISCONNECT_PARAMETER,
            "primary",
            old_source_parameter_id,
            op->data.disconnect_parameter.target_parameter_id);
        edit_report_set_parameter_edge_after(
            report->changed_objects,
            report->changed_object_count,
            op->data.disconnect_parameter.target_parameter_id,
            NMO_EDIT_OP_DISCONNECT_PARAMETER,
            "primary",
            after_source_parameter_id,
            op->data.disconnect_parameter.target_parameter_id);
        return edit_report_note_parameter_edge_source(
            report,
            NMO_EDIT_OP_DISCONNECT_PARAMETER,
            old_source_parameter_id);
    }
    case NMO_EDIT_OP_REMOVE_PARAMETER:
    {
        nmo_object_id_t old_source_parameter_id =
            edit_plan_get_parameterin_source(
                tx,
                op->data.remove_parameter.parameter_id);
        NMO_RETURN_IF_ERROR(edit_report_note_parameter_detach_impacts(
            tx,
            report,
            NMO_EDIT_OP_REMOVE_PARAMETER,
            op->data.remove_parameter.parameter_id));
        nmo_status_t rc = nmo_script_edit_remove_parameter(
            tx,
            op->data.remove_parameter.parameter_id,
            op->data.remove_parameter.detach);
        if (rc != NMO_OK) {
            return rc;
        }
        return edit_report_note_parameter_edge_source(
            report,
            NMO_EDIT_OP_REMOVE_PARAMETER,
            old_source_parameter_id);
    }
    case NMO_EDIT_OP_ADD_OPERATION:
    {
        nmo_object_id_t in1_parameter_id =
            op->data.add_operation.in1_parameter_id;
        nmo_object_id_t in2_parameter_id =
            op->data.add_operation.in2_parameter_id;
        nmo_object_id_t out_parameter_id =
            op->data.add_operation.out_parameter_id;
        edit_executor_handle_slot_t slots[] = {
            {
                .ref = &op->data.add_operation.in1_parameter_ref,
                .id = &in1_parameter_id,
                .diagnostic_code = "handle_not_found",
                .diagnostic_message =
                    "Referenced edit operation input parameter handle was not found",
            },
            {
                .ref = &op->data.add_operation.in2_parameter_ref,
                .id = &in2_parameter_id,
                .diagnostic_code = "handle_not_found",
                .diagnostic_message =
                    "Referenced edit operation input parameter handle was not found",
            },
            {
                .ref = &op->data.add_operation.out_parameter_ref,
                .id = &out_parameter_id,
                .diagnostic_code = "handle_not_found",
                .diagnostic_message =
                    "Referenced edit operation output parameter handle was not found",
            },
        };
        NMO_RETURN_IF_ERROR(edit_executor_resolve_handle_slots(
            tx,
            report,
            slots,
            sizeof(slots) / sizeof(slots[0]),
            out_diagnostic_code,
            out_diagnostic_message));
        nmo_status_t rc = nmo_script_edit_add_operation(
            tx,
            op->data.add_operation.parent_behavior_id,
            op->data.add_operation.operation_guid,
            in1_parameter_id,
            in2_parameter_id,
            out_parameter_id,
            out_result_id);
        if (rc != NMO_OK) {
            return rc;
        }
        return edit_report_note_operation_slot_parameters(
            report,
            NMO_EDIT_OP_ADD_OPERATION,
            in1_parameter_id,
            in2_parameter_id,
            out_parameter_id);
    }
    case NMO_EDIT_OP_REWIRE_OPERATION:
    {
        const nmo_parameteroperation_state_t *before_state =
            edit_plan_get_operation_state(
                tx,
                op->data.rewire_operation.operation_id);
        nmo_parameteroperation_state_t before_state_copy;
        const nmo_parameteroperation_state_t *before_snapshot = NULL;
        if (before_state != NULL) {
            before_state_copy = *before_state;
            before_snapshot = &before_state_copy;
        }
        nmo_object_id_t in1_parameter_id =
            op->data.rewire_operation.in1_parameter_id;
        nmo_object_id_t in2_parameter_id =
            op->data.rewire_operation.in2_parameter_id;
        nmo_object_id_t out_parameter_id =
            op->data.rewire_operation.out_parameter_id;
        edit_executor_handle_slot_t slots[] = {
            {
                .ref = &op->data.rewire_operation.in1_parameter_ref,
                .id = &in1_parameter_id,
                .diagnostic_code = "missing_in1_handle",
                .diagnostic_message =
                    "Failed to resolve in1 parameter handle",
            },
            {
                .ref = &op->data.rewire_operation.in2_parameter_ref,
                .id = &in2_parameter_id,
                .diagnostic_code = "missing_in2_handle",
                .diagnostic_message =
                    "Failed to resolve in2 parameter handle",
            },
            {
                .ref = &op->data.rewire_operation.out_parameter_ref,
                .id = &out_parameter_id,
                .diagnostic_code = "missing_out_handle",
                .diagnostic_message =
                    "Failed to resolve out parameter handle",
            },
        };
        NMO_RETURN_IF_ERROR(edit_executor_resolve_handle_slots(
            tx,
            report,
            slots,
            sizeof(slots) / sizeof(slots[0]),
            out_diagnostic_code,
            out_diagnostic_message));
        nmo_status_t rc = nmo_script_edit_rewire_operation(
            tx,
            op->data.rewire_operation.operation_id,
            op->data.rewire_operation.slot_flags,
            in1_parameter_id,
            in2_parameter_id,
            out_parameter_id);
        if (rc != NMO_OK || report == NULL) {
            return rc;
        }
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            op->data.rewire_operation.operation_id,
            NMO_EDIT_OP_REWIRE_OPERATION,
            "primary"));
        edit_report_set_operation_slot_before(
            report->changed_objects,
            report->changed_object_count,
            op->data.rewire_operation.operation_id,
            NMO_EDIT_OP_REWIRE_OPERATION,
            "primary",
            before_snapshot);
        edit_report_set_operation_slot_after(
            report->changed_objects,
            report->changed_object_count,
            op->data.rewire_operation.operation_id,
            NMO_EDIT_OP_REWIRE_OPERATION,
            "primary",
            edit_plan_get_operation_state(
                tx,
                op->data.rewire_operation.operation_id));
        return edit_report_note_operation_slot_parameters(
            report,
            NMO_EDIT_OP_REWIRE_OPERATION,
            (op->data.rewire_operation.slot_flags &
             NMO_SCRIPT_EDIT_OP_SLOT_IN1) != 0u ? in1_parameter_id : 0u,
            (op->data.rewire_operation.slot_flags &
             NMO_SCRIPT_EDIT_OP_SLOT_IN2) != 0u ? in2_parameter_id : 0u,
            (op->data.rewire_operation.slot_flags &
             NMO_SCRIPT_EDIT_OP_SLOT_OUT) != 0u ? out_parameter_id : 0u);
    }
    case NMO_EDIT_OP_REMOVE_OPERATION: {
        const nmo_parameteroperation_state_t *before_state =
            edit_plan_get_operation_state(
                tx,
                op->data.remove_operation.operation_id);
        nmo_parameteroperation_state_t before_state_copy;
        const nmo_parameteroperation_state_t *before_snapshot = NULL;
        if (before_state != NULL) {
            before_state_copy = *before_state;
            before_snapshot = &before_state_copy;
        }
        nmo_object_id_t in1_parameter_id = 0u;
        nmo_object_id_t in2_parameter_id = 0u;
        nmo_object_id_t out_parameter_id = 0u;
        edit_plan_get_parameter_operation_slots(
            tx,
            op->data.remove_operation.operation_id,
            &in1_parameter_id,
            &in2_parameter_id,
            &out_parameter_id);
        nmo_status_t rc = nmo_script_edit_remove_operation(
            tx,
            op->data.remove_operation.operation_id);
        if (rc != NMO_OK) {
            return rc;
        }
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
            report,
            op->data.remove_operation.operation_id,
            NMO_EDIT_OP_REMOVE_OPERATION,
            "primary"));
        edit_report_set_operation_slot_before(
            report->deleted_objects,
            report->deleted_object_count,
            op->data.remove_operation.operation_id,
            NMO_EDIT_OP_REMOVE_OPERATION,
            "primary",
            before_snapshot);
        return edit_report_note_operation_slot_parameters(
            report,
            NMO_EDIT_OP_REMOVE_OPERATION,
            in1_parameter_id,
            in2_parameter_id,
            out_parameter_id);
    }
    case NMO_EDIT_OP_INTERFACE_POLICY:
    {
        const nmo_behavior_state_t *before_state =
            edit_plan_get_behavior_state(
                tx,
                op->data.interface_policy.behavior_id);
        nmo_behavior_state_t before_state_copy;
        const nmo_behavior_state_t *before_snapshot = NULL;
        if (before_state != NULL) {
            before_state_copy = *before_state;
            before_snapshot = &before_state_copy;
        }
        nmo_status_t rc = nmo_script_edit_apply_interface_policy(
            tx,
            op->data.interface_policy.behavior_id,
            op->data.interface_policy.mode);
        if (rc != NMO_OK || report == NULL) {
            return rc;
        }
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            op->data.interface_policy.behavior_id,
            NMO_EDIT_OP_INTERFACE_POLICY,
            "primary"));
        edit_report_set_interface_before(
            report->changed_objects,
            report->changed_object_count,
            op->data.interface_policy.behavior_id,
            NMO_EDIT_OP_INTERFACE_POLICY,
            "primary",
            before_snapshot);
        edit_report_set_interface_after(
            report->changed_objects,
            report->changed_object_count,
            op->data.interface_policy.behavior_id,
            NMO_EDIT_OP_INTERFACE_POLICY,
            "primary",
            edit_plan_get_behavior_state(
                tx,
                op->data.interface_policy.behavior_id));
        return NMO_OK;
    }
    case NMO_EDIT_OP_SET_DATA_CELL:
    {
        uint32_t before_type = 0u;
        const nmo_dataarray_cell_t *before_cell =
            edit_plan_get_data_cell(
                tx,
                op->data.data_cell.dataarray_id,
                op->data.data_cell.row,
                op->data.data_cell.col,
                &before_type);
        nmo_dataarray_cell_t before_cell_copy;
        const nmo_dataarray_cell_t *before_snapshot = NULL;
        if (before_cell != NULL) {
            before_cell_copy = *before_cell;
            before_snapshot = &before_cell_copy;
        }
        nmo_status_t rc = nmo_object_edit_set_dataarray_cell(
            edit,
            op->data.data_cell.dataarray_id,
            op->data.data_cell.row,
            op->data.data_cell.col,
            op->data.data_cell.value);
        if (rc != NMO_OK) {
            return rc;
        }
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            op->data.data_cell.dataarray_id,
            NMO_EDIT_OP_SET_DATA_CELL,
            "data_cell"));
        edit_report_set_data_cell_before(
            report->changed_objects,
            report->changed_object_count,
            op->data.data_cell.dataarray_id,
            NMO_EDIT_OP_SET_DATA_CELL,
            "data_cell",
            op->data.data_cell.row,
            op->data.data_cell.col,
            before_type,
            before_snapshot);
        uint32_t after_type = 0u;
        const nmo_dataarray_cell_t *after_cell =
            edit_plan_get_data_cell(
                tx,
                op->data.data_cell.dataarray_id,
                op->data.data_cell.row,
                op->data.data_cell.col,
                &after_type);
        edit_report_set_data_cell_after(
            report->changed_objects,
            report->changed_object_count,
            op->data.data_cell.dataarray_id,
            NMO_EDIT_OP_SET_DATA_CELL,
            "data_cell",
            op->data.data_cell.row,
            op->data.data_cell.col,
            after_type,
            after_cell);
        return NMO_OK;
    }
    case NMO_EDIT_OP_REPLACE_BB: {
        nmo_behavior_replace_report_t replace_report = {0};
        nmo_status_t rc = nmo_behavior_edit_replace_bb_in_edit(
            nmo_script_edit_workspace(tx),
            edit,
            &op->data.replace_bb.desc,
            &replace_report);
        if (out_diagnostic_code != NULL) {
            *out_diagnostic_code = replace_report.diagnostic_code;
        }
        if (out_diagnostic_message != NULL) {
            *out_diagnostic_message = replace_report.diagnostic_message;
        }
        if (rc == NMO_OK && out_result_id != NULL) {
            *out_result_id = op->data.replace_bb.desc.behavior_id;
        }
        if (rc == NMO_OK && report != NULL) {
            rc = nmo_edit_report_merge_semantic_risks(
                report,
                replace_report.semantic_risks,
                replace_report.semantic_risk_count);
        }
        free(replace_report.semantic_risks);
        return rc;
    }
    case NMO_EDIT_OP_FOLD: {
        nmo_behavior_fold_report_t fold_report = {0};
        nmo_status_t rc = dry_run
            ? nmo_behavior_edit_fold_analyze(
                  nmo_script_edit_workspace(tx),
                  &op->data.fold.desc,
                  &fold_report)
            : nmo_behavior_edit_fold_in_script_tx(
                  tx,
                  &op->data.fold.desc,
                  &fold_report);
        if (out_diagnostic_code != NULL) {
            *out_diagnostic_code = fold_report.diagnostic_code;
        }
        if (out_diagnostic_message != NULL) {
            *out_diagnostic_message = fold_report.diagnostic_message;
        }
        if (rc == NMO_OK && out_result_id != NULL) {
            *out_result_id = fold_report.anchor_id != 0u
                ? fold_report.anchor_id
                : op->data.fold.desc.anchor_id;
        }
        if (rc == NMO_OK && report != NULL) {
            rc = nmo_edit_report_merge_semantic_risks(
                report,
                fold_report.semantic_risks,
                fold_report.semantic_risk_count);
            if (rc == NMO_OK) {
                rc = edit_report_note_fold_impact(
                    report, &fold_report, op->data.fold.desc.parent_id);
            }
        }
        nmo_behavior_edit_fold_report_free(&fold_report);
        return rc;
    }
    default:
        return NMO_ERR_NOT_SUPPORTED;
    }
}

static const char *edit_op_result_handle_name(nmo_edit_op_kind_t kind)
{
    switch (kind) {
    case NMO_EDIT_OP_ADD_NODE:
        return "node";
    case NMO_EDIT_OP_ADD_IO:
        return "io";
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK:
        return "link";
    case NMO_EDIT_OP_ADD_PARAMETER:
        return "parameter";
    case NMO_EDIT_OP_ADD_OPERATION:
        return "operation";
    default:
        return "object";
    }
}

static bool edit_op_creates_result(nmo_edit_op_kind_t kind)
{
    return kind == NMO_EDIT_OP_ADD_NODE ||
           kind == NMO_EDIT_OP_ADD_IO ||
           kind == NMO_EDIT_OP_ADD_BEHAVIOR_LINK ||
           kind == NMO_EDIT_OP_ADD_PARAMETER ||
           kind == NMO_EDIT_OP_ADD_OPERATION;
}

static nmo_status_t edit_report_add_named_handle(
    nmo_edit_report_t *report,
    size_t operation_index,
    const char *prefix,
    nmo_object_repository_t *repo,
    nmo_object_id_t id)
{
    if (id == 0u) {
        return NMO_OK;
    }
    nmo_object_t *object = repo != NULL
        ? nmo_object_repository_find_by_id(repo, id)
        : NULL;
    const char *name = object != NULL ? nmo_object_get_name(object) : NULL;
    char handle_name[160];
    if (name != NULL && name[0] != '\0') {
        snprintf(handle_name, sizeof(handle_name), "%s:%s", prefix, name);
    } else {
        snprintf(handle_name, sizeof(handle_name), "%s", prefix);
    }
    return nmo_edit_report_add_operation_handle(
        report, operation_index, handle_name, id);
}

static nmo_status_t edit_report_add_array_handles(
    nmo_edit_report_t *report,
    size_t operation_index,
    const char *prefix,
    nmo_object_repository_t *repo,
    const nmo_array_t *array)
{
    if (array == NULL || array->count == 0u) {
        return NMO_OK;
    }
    if (array->element_size != sizeof(nmo_behavior_ref_t) ||
        array->data == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < array->count; ++i) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(array, i);
        if (id == 0) continue;
        NMO_RETURN_IF_ERROR(edit_report_add_named_handle(
            report, operation_index, prefix, repo, id));
    }
    return NMO_OK;
}

static nmo_status_t edit_report_add_input_parameter_handles(
    nmo_edit_report_t *report,
    size_t operation_index,
    nmo_object_repository_t *repo,
    const nmo_array_t *array)
{
    if (array == NULL || array->count == 0u) {
        return NMO_OK;
    }
    if (array->element_size != sizeof(nmo_behavior_ref_t) ||
        array->data == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < array->count; ++i) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(array, i);
        if (id == 0) continue;
        NMO_RETURN_IF_ERROR(edit_report_add_named_handle(
            report, operation_index, "input_param", repo, id));
        nmo_object_t *param_obj =
            repo != NULL ? nmo_object_repository_find_by_id(repo, id) : NULL;
        nmo_parameterin_state_t *param_state = param_obj != NULL
            ? (nmo_parameterin_state_t *)nmo_object_get_state(param_obj)
            : NULL;
        const nmo_object_id_t source_id =
            nmo_parameterin_source_id(param_state);
        if (source_id != 0u) {
            NMO_RETURN_IF_ERROR(edit_report_add_named_handle(
                report,
                operation_index,
                "input_param_source",
                repo,
                source_id));
        }
    }
    return NMO_OK;
}

static nmo_status_t edit_report_add_node_child_handles(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    size_t operation_index,
    nmo_object_id_t node_id)
{
    if (tx == NULL || report == NULL || node_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_workspace_t *workspace = nmo_script_edit_workspace(tx);
    nmo_object_repository_t *repo =
        workspace != NULL ? nmo_workspace_internal_repository(workspace) : NULL;
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    nmo_object_t *node_obj = nmo_object_repository_find_by_id(repo, node_id);
    nmo_behavior_state_t *state = node_obj != NULL
        ? (nmo_behavior_state_t *)nmo_object_get_state(node_obj)
        : NULL;
    if (state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    const nmo_object_id_t target_parameter_id =
        nmo_behavior_target_parameter_id(state);
    if (target_parameter_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_operation_handle(
            report, operation_index, "target", target_parameter_id));
    }
    NMO_RETURN_IF_ERROR(edit_report_add_array_handles(
        report, operation_index, "input", repo, &state->inputs));
    NMO_RETURN_IF_ERROR(edit_report_add_array_handles(
        report, operation_index, "output", repo, &state->outputs));
    NMO_RETURN_IF_ERROR(edit_report_add_input_parameter_handles(
        report, operation_index, repo, &state->in_parameters));
    NMO_RETURN_IF_ERROR(edit_report_add_array_handles(
        report, operation_index, "output_param", repo, &state->out_parameters));
    NMO_RETURN_IF_ERROR(edit_report_add_array_handles(
        report, operation_index, "local_param", repo, &state->local_parameters));
    return NMO_OK;
}

static nmo_status_t edit_report_resolve_operation_handle(
    const nmo_edit_report_t *report,
    size_t operation_index,
    const char *handle_name,
    nmo_object_id_t *out_id)
{
    if (report == NULL || handle_name == NULL || out_id == NULL ||
        operation_index >= report->operation_count) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    const nmo_edit_operation_result_t *operation =
        &report->operations[operation_index];
    for (size_t i = 0; i < operation->handle_count; ++i) {
        if (operation->handles[i].name != NULL &&
            strcmp(operation->handles[i].name, handle_name) == 0) {
            *out_id = operation->handles[i].id;
            return NMO_OK;
        }
    }
    return NMO_ERR_NOT_FOUND;
}

static nmo_object_id_t edit_op_deleted_id(const nmo_edit_op_t *op)
{
    switch (op->kind) {
    case NMO_EDIT_OP_REMOVE_NODE:
        return op->data.remove_node.node_id;
    case NMO_EDIT_OP_REMOVE_IO:
        return op->data.remove_io.io_id;
    case NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK:
        return op->data.remove_link.link_id;
    case NMO_EDIT_OP_REMOVE_PARAMETER:
        return op->data.remove_parameter.parameter_id;
    case NMO_EDIT_OP_REMOVE_OPERATION:
        return op->data.remove_operation.operation_id;
    default:
        return 0;
    }
}

static nmo_object_id_t edit_op_changed_id(const nmo_edit_op_t *op)
{
    switch (op->kind) {
    case NMO_EDIT_OP_ADD_NODE:
        return op->data.add_node.parent_behavior_id;
    case NMO_EDIT_OP_REMOVE_NODE:
        return op->data.remove_node.parent_behavior_id;
    case NMO_EDIT_OP_ADD_IO:
        return op->data.add_io.behavior_id;
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK:
        return op->data.add_link.parent_behavior_id;
    case NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK:
        return op->data.remove_link.parent_behavior_id;
    case NMO_EDIT_OP_ADD_PARAMETER:
        return op->data.add_parameter.owner_behavior_id;
    case NMO_EDIT_OP_CONNECT_PARAMETER:
        return op->data.connect_parameter.target_parameter_id;
    case NMO_EDIT_OP_ADD_OPERATION:
        return op->data.add_operation.parent_behavior_id;
    case NMO_EDIT_OP_INTERFACE_POLICY:
        return op->data.interface_policy.behavior_id;
    default:
        return op->primary_id;
    }
}

static nmo_status_t edit_executor_validate(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    uint32_t validation_flags)
{
    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY) != 0u) {
        report->validation.roundtrip_status =
            nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY);
        if (report->validation.roundtrip_status != NMO_OK) {
            report->validation.final_status = report->validation.roundtrip_status;
            return report->validation.final_status;
        }
    }
    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_REFERENCES) != 0u) {
        report->validation.reference_status =
            nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_REFERENCES);
        if (report->validation.reference_status != NMO_OK) {
            report->validation.final_status = report->validation.reference_status;
            return report->validation.final_status;
        }
    }
    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX) != 0u) {
        report->validation.behavior_index_status =
            nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX);
        if (report->validation.behavior_index_status != NMO_OK) {
            report->validation.final_status =
                report->validation.behavior_index_status;
            return report->validation.final_status;
        }
    }
    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_INTERFACE) != 0u) {
        report->validation.interface_status =
            nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_INTERFACE);
        if (report->validation.interface_status != NMO_OK) {
            report->validation.final_status = report->validation.interface_status;
            return report->validation.final_status;
        }
    }
    report->validation.final_status = NMO_OK;
    return NMO_OK;
}

static nmo_status_t edit_executor_validate_semantics(
    nmo_script_edit_tx_t *tx,
    const nmo_edit_plan_t *plan,
    nmo_edit_report_t *report,
    bool allow_rewrite_analysis_failure)
{
    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    nmo_status_t rc = nmo_semantic_validate_edit_plan(
        nmo_script_edit_workspace(tx), plan, &risks, &risk_count);
    if (rc != NMO_OK) {
        nmo_semantic_risks_free(risks);
        if (allow_rewrite_analysis_failure &&
            (rc == NMO_ERR_INVALID_STATE || rc == NMO_ERR_NOT_FOUND ||
             rc == NMO_ERR_VALIDATION_FAILED)) {
            return NMO_OK;
        }
        return rc;
    }
    rc = nmo_edit_report_merge_semantic_risks(report, risks, risk_count);
    nmo_semantic_risks_free(risks);
    return rc;
}

static bool edit_plan_contains_rewrite_op(const nmo_edit_plan_t *plan)
{
    if (plan == NULL) {
        return false;
    }
    for (size_t i = 0; i < plan->count; ++i) {
        if (plan->ops[i].kind == NMO_EDIT_OP_FOLD ||
            plan->ops[i].kind == NMO_EDIT_OP_REPLACE_BB) {
            return true;
        }
    }
    return false;
}

nmo_status_t nmo_edit_executor_execute(
    nmo_workspace_t *workspace,
    const nmo_edit_plan_t *plan,
    const nmo_edit_executor_options_t *options,
    nmo_edit_report_t *report)
{
    if (workspace == NULL || plan == NULL || report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = nmo_script_edit_begin(workspace, "edit plan", &tx);
    if (rc != NMO_OK) {
        report->status = rc;
        return rc;
    }

    nmo_edit_executor_options_t effective =
        options != NULL ? *options : nmo_edit_executor_options_default();
    rc = nmo_edit_executor_execute_transaction(tx, plan, &effective, report);
    if (rc != NMO_OK) {
        nmo_script_edit_rollback(tx);
        return rc;
    }

    if (effective.dry_run) {
        nmo_script_edit_rollback(tx);
        report->ok = true;
        report->status = NMO_OK;
        return NMO_OK;
    }

    rc = nmo_script_edit_commit(tx);
    report->ok = rc == NMO_OK;
    report->status = rc;
    return rc;
}

nmo_status_t nmo_edit_executor_execute_transaction(
    nmo_script_edit_tx_t *tx,
    const nmo_edit_plan_t *plan,
    const nmo_edit_executor_options_t *options,
    nmo_edit_report_t *report)
{
    if (tx == NULL || plan == NULL || report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_edit_executor_options_t effective =
        options != NULL ? *options : nmo_edit_executor_options_default();
    nmo_status_t rc = NMO_OK;
    NMO_RETURN_IF_ERROR(edit_report_prepare(report, plan, effective.dry_run));

    const bool has_rewrite_ops = edit_plan_contains_rewrite_op(plan);
    rc = edit_executor_validate_semantics(tx, plan, report, has_rewrite_ops);
    if (rc != NMO_OK) {
        report->ok = false;
        report->status = rc;
        return rc;
    }

    for (size_t i = 0; i < plan->count; i++) {
        const nmo_edit_op_t *op = &plan->ops[i];
        const nmo_script_edit_report_t *tx_report_before =
            nmo_script_edit_report(tx);
        size_t created_start = tx_report_before
            ? tx_report_before->created_object_id_count
            : 0u;
        size_t changed_start = tx_report_before
            ? tx_report_before->changed_object_id_count
            : 0u;
        edit_plan_manager_snapshot_t manager_before = {0};
        edit_plan_manager_snapshot_t manager_after = {0};
        rc = edit_plan_read_manager_snapshot(
            nmo_script_edit_workspace(tx), &manager_before);
        if (rc != NMO_OK) {
            report->ok = false;
            report->status = rc;
            return rc;
        }
        nmo_object_id_t result_id = 0;
        const char *diagnostic_code = NULL;
        const char *diagnostic_message = NULL;
        nmo_status_t op_rc = edit_executor_apply_op(
            tx,
            op,
            &result_id,
            effective.dry_run,
            report,
            &diagnostic_code,
            &diagnostic_message);
        const nmo_script_edit_report_t *tx_report_after =
            nmo_script_edit_report(tx);
        report->operations[i] = (nmo_edit_operation_result_t){
            .kind = op->kind,
            .primary_id = op->primary_id,
            .result_id = result_id,
            .status = op_rc,
        };
        nmo_status_t diagnostic_rc = edit_report_set_operation_diagnostic(
            report, i, diagnostic_code, diagnostic_message);
        if (diagnostic_rc != NMO_OK) {
            edit_plan_manager_snapshot_dispose(&manager_before);
            report->ok = false;
            report->status = diagnostic_rc;
            return diagnostic_rc;
        }
        if (op_rc != NMO_OK) {
            edit_plan_manager_snapshot_dispose(&manager_before);
            report->ok = false;
            report->status = op_rc;
            return op_rc;
        }
        rc = edit_plan_read_manager_snapshot(
            nmo_script_edit_workspace(tx), &manager_after);
        if (rc != NMO_OK) {
            edit_plan_manager_snapshot_dispose(&manager_before);
            report->ok = false;
            report->status = rc;
            return rc;
        }
        const char *created_manager_key = NULL;
        uint32_t created_manager_index = 0u;
        bool created_manager_entry = edit_plan_find_created_message_entry(
            &manager_before,
            &manager_after,
            &created_manager_key,
            &created_manager_index);
        nmo_guid_t created_attribute_type_guid = {0};
        const char *created_attribute_category = NULL;
        uint32_t created_attribute_index = 0u;
        uint32_t created_attribute_compatible_class_id = 0u;
        uint32_t created_attribute_flags = 0u;
        bool created_attribute_entry = edit_plan_find_created_attribute_entry(
            &manager_before,
            &manager_after,
            &created_manager_key,
            &created_attribute_category,
            &created_attribute_type_guid,
            &created_attribute_index,
            &created_attribute_compatible_class_id,
            &created_attribute_flags);
        const nmo_manager_entry_options_t *manager_entry_options =
            edit_plan_op_manager_entry(op);
        if (!created_attribute_entry && manager_entry_options != NULL &&
            manager_entry_options->policy ==
                NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING &&
            manager_entry_options->schema == NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE &&
            manager_entry_options->key != NULL &&
            manager_entry_options->key[0] != '\0' &&
            manager_entry_options->create.enabled) {
            created_attribute_entry = true;
            created_manager_key = manager_entry_options->key;
            created_attribute_category =
                manager_entry_options->create.category;
            created_attribute_type_guid =
                manager_entry_options->create.attribute_type_guid;
            created_attribute_index =
                edit_plan_get_parameter_manager_value(tx, result_id);
            created_attribute_compatible_class_id =
                manager_entry_options->create.compatible_class_id;
            created_attribute_flags = manager_entry_options->create.flags;
        }
        if (created_attribute_entry) {
            created_manager_index = created_attribute_index;
        }
        if (result_id != 0u && edit_op_creates_result(op->kind)) {
            nmo_status_t handle_rc = nmo_edit_report_add_operation_handle(
                report,
                i,
                edit_op_result_handle_name(op->kind),
                result_id);
            if (handle_rc != NMO_OK) {
                edit_plan_manager_snapshot_dispose(&manager_after);
                edit_plan_manager_snapshot_dispose(&manager_before);
                report->ok = false;
                report->status = handle_rc;
                return handle_rc;
            }
            if (op->kind == NMO_EDIT_OP_ADD_NODE) {
                handle_rc = edit_report_add_node_child_handles(
                    tx, report, i, result_id);
                if (handle_rc != NMO_OK) {
                    edit_plan_manager_snapshot_dispose(&manager_after);
                    edit_plan_manager_snapshot_dispose(&manager_before);
                    report->ok = false;
                    report->status = handle_rc;
                    return handle_rc;
                }
            }
        }
        bool noted_created_objects = false;
        if (tx_report_after &&
            tx_report_after->created_object_ids &&
            tx_report_after->created_object_id_count > created_start) {
            nmo_status_t report_rc = edit_report_note_created_objects(
                report,
                tx_report_after->created_object_ids + created_start,
                tx_report_after->created_object_id_count - created_start,
                op->kind,
                "created");
            if (report_rc != NMO_OK) {
                edit_plan_manager_snapshot_dispose(&manager_after);
                edit_plan_manager_snapshot_dispose(&manager_before);
                report->ok = false;
                report->status = report_rc;
                return report_rc;
            }
            noted_created_objects = true;
        }
        if (edit_op_creates_result(op->kind) && !noted_created_objects) {
            nmo_status_t report_rc = nmo_edit_report_add_created_object(
                report, result_id, op->kind, "created");
            if (report_rc != NMO_OK) {
                edit_plan_manager_snapshot_dispose(&manager_after);
                edit_plan_manager_snapshot_dispose(&manager_before);
                report->ok = false;
                report->status = report_rc;
                return report_rc;
            }
        }
        if (op->kind == NMO_EDIT_OP_ADD_BEHAVIOR_LINK && result_id != 0u) {
            nmo_object_id_t after_from_io_id = 0u;
            nmo_object_id_t after_to_io_id = 0u;
            uint32_t after_activation_delay = 0u;
            edit_plan_get_behavior_link_endpoints(
                tx,
                result_id,
                &after_from_io_id,
                &after_to_io_id,
                &after_activation_delay);
            edit_report_set_control_link_after(
                report->created_objects,
                report->created_object_count,
                result_id,
                op->kind,
                "created",
                after_from_io_id,
                after_to_io_id,
                after_activation_delay);
        }
        if (op->kind == NMO_EDIT_OP_ADD_OPERATION && result_id != 0u) {
            edit_report_set_operation_slot_after(
                report->created_objects,
                report->created_object_count,
                result_id,
                op->kind,
                "created",
                edit_plan_get_operation_state(tx, result_id));
        }
        if (tx_report_after &&
            tx_report_after->changed_object_ids &&
            tx_report_after->changed_object_id_count > changed_start) {
            nmo_object_id_t primary_changed_id = edit_op_changed_id(op);
            if (primary_changed_id == 0u && result_id != 0u) {
                primary_changed_id = result_id;
            }
            const nmo_object_id_t *changed_ids =
                tx_report_after->changed_object_ids + changed_start;
            size_t changed_count =
                tx_report_after->changed_object_id_count - changed_start;
            for (size_t changed_i = 0; changed_i < changed_count; ++changed_i) {
                if (changed_ids[changed_i] == primary_changed_id) {
                    continue;
                }
                const char *changed_role =
                    changed_ids[changed_i] == NMO_EDIT_MANAGER_ENTRY_IMPACT_ID
                        ? "manager_entry"
                        : "changed";
                nmo_status_t report_rc = nmo_edit_report_add_changed_object(
                    report, changed_ids[changed_i], op->kind, changed_role);
                if (report_rc != NMO_OK) {
                    edit_plan_manager_snapshot_dispose(&manager_after);
                    edit_plan_manager_snapshot_dispose(&manager_before);
                    report->ok = false;
                    report->status = report_rc;
                    return report_rc;
                }
                if (changed_ids[changed_i] == NMO_EDIT_MANAGER_ENTRY_IMPACT_ID) {
                    const char *key = (created_manager_entry ||
                                       created_attribute_entry)
                        ? created_manager_key
                        : NULL;
                    uint32_t index = (created_manager_entry ||
                                      created_attribute_entry)
                        ? created_manager_index
                        : 0u;
                    edit_report_set_manager_entry_after(
                        report->changed_objects,
                        report->changed_object_count,
                        changed_ids[changed_i],
                        op->kind,
                        changed_role,
                        created_attribute_entry ? NMO_MANAGER_GUID_ATTRIBUTE
                                                : NMO_MANAGER_GUID_MESSAGE,
                        created_attribute_entry
                            ? NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE
                            : NMO_MANAGER_ENTRY_SCHEMA_MESSAGE,
                        key,
                        created_attribute_entry ? created_attribute_category : NULL,
                        created_attribute_entry ? created_attribute_type_guid
                                                : (nmo_guid_t){0},
                        index,
                        index,
                        created_attribute_entry
                            ? created_attribute_compatible_class_id
                            : 0u,
                        created_attribute_entry ? created_attribute_flags : 0u,
                        created_manager_entry || created_attribute_entry,
                        created_manager_entry || created_attribute_entry);
                }
            }
        }
        if (created_manager_entry || created_attribute_entry) {
            nmo_status_t manager_report_rc =
                edit_report_note_manager_entry_after(
                    report,
                    op->kind,
                    created_manager_key,
                    created_manager_index,
                    true,
                    true);
            if (manager_report_rc != NMO_OK) {
                edit_plan_manager_snapshot_dispose(&manager_after);
                edit_plan_manager_snapshot_dispose(&manager_before);
                report->ok = false;
                report->status = manager_report_rc;
                return manager_report_rc;
            }
            if (created_attribute_entry) {
                edit_report_set_manager_entry_after(
                    report->changed_objects,
                    report->changed_object_count,
                    NMO_EDIT_MANAGER_ENTRY_IMPACT_ID,
                    op->kind,
                    "manager_entry",
                    NMO_MANAGER_GUID_ATTRIBUTE,
                    NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE,
                    created_manager_key,
                    created_attribute_category,
                    created_attribute_type_guid,
                    created_attribute_index,
                    created_attribute_index,
                    created_attribute_compatible_class_id,
                    created_attribute_flags,
                    true,
                    true);
            }
        }
        nmo_object_id_t deleted_id = edit_op_deleted_id(op);
        if (deleted_id != 0u) {
            if (edit_report_find_impact(
                    report->deleted_objects,
                    report->deleted_object_count,
                    deleted_id,
                    op->kind,
                    "primary") == NULL) {
                (void)nmo_edit_report_add_deleted_object(
                    report, deleted_id, op->kind, "primary");
            }
        }
        nmo_object_id_t changed_id = edit_op_changed_id(op);
        if (changed_id == 0u && result_id != 0u) {
            changed_id = result_id;
        }
        (void)nmo_edit_report_add_changed_object(
            report, changed_id, op->kind, "primary");
        edit_plan_manager_snapshot_dispose(&manager_after);
        edit_plan_manager_snapshot_dispose(&manager_before);
    }

    rc = edit_executor_validate_semantics(
        tx, plan, report, has_rewrite_ops);
    if (rc != NMO_OK) {
        report->ok = false;
        report->status = rc;
        return rc;
    }

    rc = edit_executor_validate(tx, report, effective.validation_flags);
    if (rc != NMO_OK) {
        report->ok = false;
        report->status = rc;
        return rc;
    }

    report->ok = true;
    report->status = NMO_OK;
    return NMO_OK;
}
