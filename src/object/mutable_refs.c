#include "mutable_refs_internal.h"

#include "core/nmo_array.h"
#include "format/nmo_chunk.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_patchmesh_schemas.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "core/nmo_guid.h"

#include <stdint.h>
#include <string.h>

typedef enum nmo_mutable_ref_remove_action {
    NMO_MUTABLE_REF_COMPACT = 0,
    NMO_MUTABLE_REF_CLEAR,
} nmo_mutable_ref_remove_action_t;

typedef enum nmo_mutable_ref_storage {
    NMO_MUTABLE_REF_RAW_ARRAY = 0,
    NMO_MUTABLE_REF_ARRAY,
} nmo_mutable_ref_storage_t;

typedef struct nmo_mutable_ref_lane {
    nmo_mutable_ref_storage_t storage;
    size_t count_offset;
    size_t items_offset;
    size_t array_offset;
    size_t item_size;
    size_t ref_offset;
    size_t chunk_offset;
    nmo_class_id_t expected_class_id;
    bool allow_uninitialized_empty_array;
    bool remap_owned;
    bool claims_reflected_remove;
    nmo_mutable_ref_remove_action_t remove_action;
} nmo_mutable_ref_lane_t;

typedef struct nmo_mutable_ref_adapter {
    nmo_guid_t type_guid;
    const nmo_mutable_ref_lane_t *lanes;
    size_t lane_count;
} nmo_mutable_ref_adapter_t;

#define NMO_NO_CHUNK_OFFSET SIZE_MAX
#define NMO_NO_ARRAY_OFFSET SIZE_MAX

static const nmo_mutable_ref_lane_t keyedanimation_lanes[] = {
    {
        .storage = NMO_MUTABLE_REF_RAW_ARRAY,
        .count_offset = offsetof(nmo_keyedanimation_state_t, animation_count),
        .items_offset = offsetof(nmo_keyedanimation_state_t, animation_ids),
        .array_offset = NMO_NO_ARRAY_OFFSET,
        .item_size = sizeof(nmo_ref_t),
        .ref_offset = 0u,
        .chunk_offset = NMO_NO_CHUNK_OFFSET,
        .expected_class_id = NMO_CID_OBJECTANIMATION,
        .remap_owned = false,
        .claims_reflected_remove = true,
        .remove_action = NMO_MUTABLE_REF_COMPACT,
    },
    {
        .storage = NMO_MUTABLE_REF_RAW_ARRAY,
        .count_offset = offsetof(nmo_keyedanimation_state_t, subanim_count),
        .items_offset = offsetof(nmo_keyedanimation_state_t, subanims),
        .array_offset = NMO_NO_ARRAY_OFFSET,
        .item_size = sizeof(nmo_keyedanimation_subanim_t),
        .ref_offset = offsetof(nmo_keyedanimation_subanim_t, ref),
        .chunk_offset = offsetof(nmo_keyedanimation_subanim_t, chunk),
        .expected_class_id = NMO_CID_OBJECTANIMATION,
        .remap_owned = true,
        .claims_reflected_remove = false,
        .remove_action = NMO_MUTABLE_REF_COMPACT,
    },
};

static const nmo_mutable_ref_lane_t curve_lanes[] = {
    {
        .storage = NMO_MUTABLE_REF_RAW_ARRAY,
        .count_offset = offsetof(nmo_curve_state_t, control_point_count),
        .items_offset = offsetof(nmo_curve_state_t, control_point_ids),
        .array_offset = NMO_NO_ARRAY_OFFSET,
        .item_size = sizeof(nmo_ref_t),
        .ref_offset = 0u,
        .chunk_offset = NMO_NO_CHUNK_OFFSET,
        .expected_class_id = NMO_CID_CURVEPOINT,
        .remap_owned = false,
        .claims_reflected_remove = true,
        .remove_action = NMO_MUTABLE_REF_COMPACT,
    },
    {
        .storage = NMO_MUTABLE_REF_RAW_ARRAY,
        .count_offset = offsetof(nmo_curve_state_t, sub_point_count),
        .items_offset = offsetof(nmo_curve_state_t, sub_points),
        .array_offset = NMO_NO_ARRAY_OFFSET,
        .item_size = sizeof(nmo_curve_point_subchunk_t),
        .ref_offset = offsetof(nmo_curve_point_subchunk_t, ref),
        .chunk_offset = offsetof(nmo_curve_point_subchunk_t, chunk),
        .expected_class_id = NMO_CID_CURVEPOINT,
        .remap_owned = true,
        .claims_reflected_remove = false,
        .remove_action = NMO_MUTABLE_REF_COMPACT,
    },
};

static const nmo_mutable_ref_lane_t patchmesh_lanes[] = {
    {
        .storage = NMO_MUTABLE_REF_RAW_ARRAY,
        .count_offset = offsetof(nmo_patchmesh_state_t, patch_count),
        .items_offset = offsetof(nmo_patchmesh_state_t, patches),
        .array_offset = NMO_NO_ARRAY_OFFSET,
        .item_size = sizeof(nmo_patchmesh_patch_record_t),
        .ref_offset = offsetof(nmo_patchmesh_patch_record_t, material),
        .chunk_offset = NMO_NO_CHUNK_OFFSET,
        .expected_class_id = NMO_CID_MATERIAL,
        .remap_owned = true,
        .claims_reflected_remove = false,
        .remove_action = NMO_MUTABLE_REF_CLEAR,
    },
    {
        .storage = NMO_MUTABLE_REF_RAW_ARRAY,
        .count_offset = offsetof(nmo_patchmesh_state_t, channel_count),
        .items_offset = offsetof(nmo_patchmesh_state_t, channels),
        .array_offset = NMO_NO_ARRAY_OFFSET,
        .item_size = sizeof(nmo_patchmesh_channel_t),
        .ref_offset = offsetof(nmo_patchmesh_channel_t, material),
        .chunk_offset = NMO_NO_CHUNK_OFFSET,
        .expected_class_id = NMO_CID_MATERIAL,
        .remap_owned = true,
        .claims_reflected_remove = false,
        .remove_action = NMO_MUTABLE_REF_CLEAR,
    },
};

static const nmo_mutable_ref_lane_t mesh_lanes[] = {
    {
        .storage = NMO_MUTABLE_REF_RAW_ARRAY,
        .count_offset = offsetof(nmo_mesh_state_t, material_group_count),
        .items_offset = offsetof(nmo_mesh_state_t, material_groups),
        .array_offset = NMO_NO_ARRAY_OFFSET,
        .item_size = sizeof(nmo_material_group_t),
        .ref_offset = offsetof(nmo_material_group_t, material),
        .chunk_offset = NMO_NO_CHUNK_OFFSET,
        .expected_class_id = NMO_CID_MATERIAL,
        .remap_owned = true,
        .claims_reflected_remove = false,
        .remove_action = NMO_MUTABLE_REF_CLEAR,
    },
    {
        .storage = NMO_MUTABLE_REF_RAW_ARRAY,
        .count_offset = offsetof(nmo_mesh_state_t, material_channel_count),
        .items_offset = offsetof(nmo_mesh_state_t, material_channels),
        .array_offset = NMO_NO_ARRAY_OFFSET,
        .item_size = sizeof(nmo_material_channel_t),
        .ref_offset = offsetof(nmo_material_channel_t, material),
        .chunk_offset = NMO_NO_CHUNK_OFFSET,
        .expected_class_id = NMO_CID_MATERIAL,
        .remap_owned = true,
        .claims_reflected_remove = false,
        .remove_action = NMO_MUTABLE_REF_CLEAR,
    },
};

static const nmo_mutable_ref_lane_t character_lanes[] = {
    {
        .storage = NMO_MUTABLE_REF_ARRAY,
        .array_offset = offsetof(nmo_character_state_t, body_parts),
        .item_size = sizeof(nmo_character_part_t),
        .ref_offset = offsetof(nmo_character_part_t, ref),
        .chunk_offset = NMO_NO_CHUNK_OFFSET,
        .expected_class_id = NMO_CID_BODYPART,
        .allow_uninitialized_empty_array = true,
        .remap_owned = true,
        .remove_action = NMO_MUTABLE_REF_COMPACT,
    },
};

static const nmo_mutable_ref_lane_t scene_lanes[] = {
    {
        .storage = NMO_MUTABLE_REF_ARRAY,
        .array_offset = offsetof(nmo_scene_state_t, object_descs),
        .item_size = sizeof(nmo_scene_object_desc_t),
        .ref_offset = offsetof(nmo_scene_object_desc_t, ref),
        .chunk_offset = NMO_NO_CHUNK_OFFSET,
        .expected_class_id = NMO_CID_SCENEOBJECT,
        .remap_owned = true,
        .remove_action = NMO_MUTABLE_REF_COMPACT,
    },
};

static const nmo_mutable_ref_adapter_t mutable_ref_adapters[] = {
    {
        .type_guid = CKPGUID_KEYEDANIMATION_INIT,
        .lanes = keyedanimation_lanes,
        .lane_count = sizeof(keyedanimation_lanes) /
                      sizeof(keyedanimation_lanes[0]),
    },
    {
        .type_guid = CKPGUID_CURVE_INIT,
        .lanes = curve_lanes,
        .lane_count = sizeof(curve_lanes) / sizeof(curve_lanes[0]),
    },
    {
        .type_guid = CKPGUID_PATCHMESH_INIT,
        .lanes = patchmesh_lanes,
        .lane_count = sizeof(patchmesh_lanes) / sizeof(patchmesh_lanes[0]),
    },
    {
        .type_guid = CKPGUID_MESH_INIT,
        .lanes = mesh_lanes,
        .lane_count = sizeof(mesh_lanes) / sizeof(mesh_lanes[0]),
    },
    {
        .type_guid = CKPGUID_CHARACTER_INIT,
        .lanes = character_lanes,
        .lane_count = sizeof(character_lanes) / sizeof(character_lanes[0]),
    },
    {
        .type_guid = CKPGUID_SCENE_INIT,
        .lanes = scene_lanes,
        .lane_count = sizeof(scene_lanes) / sizeof(scene_lanes[0]),
    },
};

static const nmo_mutable_ref_adapter_t *find_mutable_ref_adapter(
    nmo_guid_t type_guid)
{
    for (size_t i = 0u;
         i < sizeof(mutable_ref_adapters) / sizeof(mutable_ref_adapters[0]);
         ++i) {
        if (nmo_guid_equals(mutable_ref_adapters[i].type_guid, type_guid)) {
            return &mutable_ref_adapters[i];
        }
    }
    return NULL;
}

bool nmo_mutable_refs_claims_remove_field(
    const nmo_type_descriptor_t *type,
    const nmo_type_field_t *field)
{
    if (type == NULL || field == NULL) return false;
    const nmo_mutable_ref_adapter_t *adapter =
        find_mutable_ref_adapter(type->guid);
    if (adapter == NULL) return false;
    for (size_t i = 0u; i < adapter->lane_count; ++i) {
        const nmo_mutable_ref_lane_t *lane = &adapter->lanes[i];
        if (lane->claims_reflected_remove &&
            lane->storage == NMO_MUTABLE_REF_RAW_ARRAY &&
            lane->items_offset == field->offset) {
            return true;
        }
    }
    return false;
}

static nmo_status_t validate_lane(
    const void *instance,
    const nmo_mutable_ref_lane_t *lane)
{
    const uint8_t *bytes = (const uint8_t *)instance;
    if (lane->storage == NMO_MUTABLE_REF_ARRAY) {
        const nmo_array_t *array = (const nmo_array_t *)(
            bytes + lane->array_offset);
        const bool uninitialized_empty =
            lane->allow_uninitialized_empty_array && array->count == 0u &&
            array->element_size == 0u;
        if ((!uninitialized_empty && array->element_size != lane->item_size) ||
            (array->count > 0u && array->data == NULL)) {
            return NMO_ERR_VALIDATION_FAILED;
        }
        return NMO_OK;
    }
    const uint32_t count = *(const uint32_t *)(bytes + lane->count_offset);
    const void *items = NULL;
    memcpy(&items, bytes + lane->items_offset, sizeof(items));
    if (count > 0u && items == NULL) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    return NMO_OK;
}

static nmo_status_t validate_hierarchy(
    const nmo_type_descriptor_t *derived_type,
    const void *root_instance)
{
    const nmo_type_descriptor_ext_t *layout = derived_type->ext;
    const bool has_layout = layout != NULL && layout->hierarchy != NULL &&
                            layout->hierarchy_depth > 0u;
    const size_t level_count = has_layout ? layout->hierarchy_depth : 1u;
    for (size_t level = level_count; level > 0u; --level) {
        const size_t index = level - 1u;
        const nmo_type_descriptor_t *current =
            has_layout ? layout->hierarchy[index] : derived_type;
        const uint32_t offset = has_layout && layout->state_offsets != NULL
            ? layout->state_offsets[index]
            : 0u;
        const uint8_t *current_instance =
            (const uint8_t *)root_instance + offset;
        if (current == NULL) continue;
        const nmo_mutable_ref_adapter_t *adapter =
            find_mutable_ref_adapter(current->guid);
        if (adapter == NULL) continue;
        for (size_t lane_index = 0u;
             lane_index < adapter->lane_count;
             ++lane_index) {
            nmo_status_t result = validate_lane(
                current_instance, &adapter->lanes[lane_index]);
            if (result != NMO_OK) return result;
        }
    }
    return NMO_OK;
}

static nmo_status_t mutate_lane(
    void *instance,
    const nmo_mutable_ref_lane_t *lane,
    const nmo_mutable_ref_request_t *request,
    size_t *change_count)
{
    uint8_t *bytes = (uint8_t *)instance;
    nmo_array_t *array = NULL;
    uint32_t *raw_count = NULL;
    size_t count = 0u;
    uint8_t *items = NULL;
    if (lane->storage == NMO_MUTABLE_REF_ARRAY) {
        array = (nmo_array_t *)(bytes + lane->array_offset);
        count = array->count;
        items = (uint8_t *)array->data;
    } else {
        raw_count = (uint32_t *)(bytes + lane->count_offset);
        count = *raw_count;
        memcpy(&items, bytes + lane->items_offset, sizeof(items));
    }

    if (request->operation == NMO_MUTABLE_REF_REMAP) {
        if (!lane->remap_owned) return NMO_OK;
        for (size_t i = 0u; i < count; ++i) {
            nmo_ref_t *ref = (nmo_ref_t *)(
                items + i * lane->item_size + lane->ref_offset);
            if (ref->state != NMO_REF_RESOLVED) continue;
            nmo_object_id_t replacement = NMO_OBJECT_ID_NONE;
            if (request->resolve(
                    request->context, ref, lane->expected_class_id,
                    &replacement)) {
                ref->id = replacement;
                if (change_count != NULL) ++*change_count;
            }
        }
        return NMO_OK;
    }

    if (lane->remove_action == NMO_MUTABLE_REF_CLEAR) {
        for (size_t i = 0u; i < count; ++i) {
            nmo_ref_t *ref = (nmo_ref_t *)(
                items + i * lane->item_size + lane->ref_offset);
            if (ref->state == NMO_REF_NONE) continue;
            nmo_object_id_t ignored_replacement = NMO_OBJECT_ID_NONE;
            if (request->resolve(
                    request->context, ref, lane->expected_class_id,
                    &ignored_replacement)) {
                *ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
                if (change_count != NULL) ++*change_count;
            }
        }
        return NMO_OK;
    }

    for (size_t i = 0u; i < count;) {
        nmo_ref_t *ref = (nmo_ref_t *)(
            items + i * lane->item_size + lane->ref_offset);
        nmo_object_id_t ignored_replacement = NMO_OBJECT_ID_NONE;
        if (!request->resolve(
                request->context, ref, lane->expected_class_id,
                &ignored_replacement)) {
            ++i;
            continue;
        }

        if (array != NULL) {
            NMO_RETURN_IF_ERROR(nmo_array_remove(array, i, NULL));
            count = array->count;
            items = (uint8_t *)array->data;
            if (change_count != NULL) ++*change_count;
            continue;
        }

        if (lane->chunk_offset != NMO_NO_CHUNK_OFFSET) {
            nmo_chunk_t **chunk = (nmo_chunk_t **)(
                items + i * lane->item_size + lane->chunk_offset);
            if (*chunk != NULL) {
                nmo_chunk_destroy(*chunk);
                *chunk = NULL;
            }
        }
        const size_t remaining = count - i - 1u;
        if (remaining > 0u) {
            memmove(
                items + i * lane->item_size,
                items + (i + 1u) * lane->item_size,
                remaining * lane->item_size);
        }
        --count;
        *raw_count = (uint32_t)count;
        if (lane->chunk_offset != NMO_NO_CHUNK_OFFSET) {
            nmo_chunk_t **tail_chunk = (nmo_chunk_t **)(
                items + count * lane->item_size + lane->chunk_offset);
            *tail_chunk = NULL;
        }
        if (change_count != NULL) ++*change_count;
    }
    return NMO_OK;
}

nmo_status_t nmo_mutable_refs_apply(
    const nmo_type_descriptor_t *derived_type,
    void *root_instance,
    const nmo_mutable_ref_request_t *request,
    size_t *out_change_count)
{
    if (out_change_count != NULL) *out_change_count = 0u;
    if (derived_type == NULL || root_instance == NULL || request == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (request->operation < NMO_MUTABLE_REF_VALIDATE ||
        request->operation > NMO_MUTABLE_REF_REMOVE ||
        (request->operation != NMO_MUTABLE_REF_VALIDATE &&
         request->resolve == NULL)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    NMO_RETURN_IF_ERROR(validate_hierarchy(derived_type, root_instance));
    if (request->operation == NMO_MUTABLE_REF_VALIDATE) return NMO_OK;

    size_t change_count = 0u;
    const nmo_type_descriptor_ext_t *layout = derived_type->ext;
    const bool has_layout = layout != NULL && layout->hierarchy != NULL &&
                            layout->hierarchy_depth > 0u;
    const size_t level_count = has_layout ? layout->hierarchy_depth : 1u;
    for (size_t level = level_count; level > 0u; --level) {
        const size_t index = level - 1u;
        const nmo_type_descriptor_t *current =
            has_layout ? layout->hierarchy[index] : derived_type;
        const uint32_t offset = has_layout && layout->state_offsets != NULL
            ? layout->state_offsets[index]
            : 0u;
        uint8_t *current_instance = (uint8_t *)root_instance + offset;
        if (current == NULL) continue;
        const nmo_mutable_ref_adapter_t *adapter =
            find_mutable_ref_adapter(current->guid);
        if (adapter == NULL) continue;
        for (size_t lane_index = 0u;
             lane_index < adapter->lane_count;
             ++lane_index) {
            NMO_RETURN_IF_ERROR(mutate_lane(
                current_instance, &adapter->lanes[lane_index], request,
                &change_count));
        }
    }
    if (out_change_count != NULL) *out_change_count = change_count;
    return NMO_OK;
}
