#include "session/nmo_runtime_kernel.h"

#include "format/nmo_id_remap.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_grid_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_patchmesh_schemas.h"
#include "object/builtin/nmo_place_schemas.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_param_guids.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_query.h"
#include "type/nmo_type_runtime.h"
#include "type/nmo_type_system.h"
#include "core/nmo_array.h"
#include "../runtime/runtime_internal.h"

#include <string.h>

/* 鈹€鈹€ ID remap lookup 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€ */

static bool runtime_lookup_mapping(
    const nmo_id_remap_t *remap,
    nmo_object_id_t old_id,
    nmo_object_id_t *out_new_id)
{
    if (remap == NULL || out_new_id == NULL || old_id == NMO_OBJECT_ID_NONE) {
        return false;
    }
    return nmo_id_remap_lookup_id(remap, old_id, out_new_id) == NMO_OK;
}

/* 鈹€鈹€ Ref-field remap callback 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€ */

typedef struct runtime_ref_remap_ctx {
    const nmo_id_remap_t *remap;
    const nmo_type_descriptor_t *type;
    void *instance;
    nmo_status_t status;
} runtime_ref_remap_ctx_t;

static bool runtime_remap_ref_field(
    void *user_data,
    const nmo_type_field_t *field,
    const void *field_ptr)
{
    (void)field_ptr;

    runtime_ref_remap_ctx_t *ctx = (runtime_ref_remap_ctx_t *)user_data;
    if (ctx == NULL || field == NULL || ctx->instance == NULL) {
        if (ctx != NULL) ctx->status = NMO_ERR_INVALID_ARGUMENT;
        return false;
    }

    if (!nmo_field_is_ref(field)) {
        return true;
    }

    if (!nmo_field_is_array(field)) {
        if (field->size == sizeof(nmo_ref_t)) {
            nmo_ref_t *ref = (nmo_ref_t *)nmo_field_get_ptr(
                ctx->instance, field);
            if (ref != NULL && ref->state == NMO_REF_RESOLVED) {
                nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
                if (runtime_lookup_mapping(ctx->remap, ref->id, &mapped)) {
                    ref->id = mapped;
                }
            }
            return true;
        }
        if (field->size == sizeof(nmo_object_id_t)) {
            nmo_object_id_t *id_ptr = (nmo_object_id_t *)nmo_field_get_ptr(ctx->instance, field);
            if (id_ptr != NULL && *id_ptr != NMO_OBJECT_ID_NONE) {
                nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
                if (runtime_lookup_mapping(ctx->remap, *id_ptr, &mapped)) {
                    *id_ptr = mapped;
                }
            }
        }
        return true;
    }

    if (field->size == sizeof(nmo_array_t)) {
        nmo_array_t *arr = (nmo_array_t *)nmo_field_get_ptr(ctx->instance, field);
        if (arr == NULL) {
            ctx->status = NMO_ERR_INVALID_ARGUMENT;
            return false;
        }
        if (nmo_field_uses_ref_records(field)) {
            if (((arr->element_size != 0 || arr->count > 0) &&
                 arr->element_size != sizeof(nmo_ref_t)) ||
                (arr->count > 0 && arr->data == NULL)) {
                ctx->status = NMO_ERR_VALIDATION_FAILED;
                return false;
            }
            if (arr->count == 0) return true;
            nmo_ref_t *refs = (nmo_ref_t *)arr->data;
            for (size_t i = 0; i < arr->count; ++i) {
                nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
                if (refs[i].state == NMO_REF_RESOLVED &&
                    runtime_lookup_mapping(ctx->remap, refs[i].id, &mapped)) {
                    refs[i].id = mapped;
                }
            }
            return true;
        }
        if (((arr->element_size != 0 || arr->count > 0) &&
             arr->element_size != sizeof(nmo_object_id_t)) ||
            (arr->count > 0 && arr->data == NULL)) {
            ctx->status = NMO_ERR_VALIDATION_FAILED;
            return false;
        }
        if (arr->count == 0) return true;

        nmo_object_id_t *ids = (nmo_object_id_t *)arr->data;
        for (size_t i = 0; i < arr->count; i++) {
            nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
            if (runtime_lookup_mapping(ctx->remap, ids[i], &mapped)) {
                ids[i] = mapped;
            }
        }
        return true;
    }

    if (field->size == sizeof(void *)) {
        if (nmo_field_uses_ref_records(field)) {
            nmo_ref_t **refs_ptr = (nmo_ref_t **)nmo_field_get_ptr(
                ctx->instance, field);
            if (refs_ptr == NULL) {
                ctx->status = NMO_ERR_INVALID_ARGUMENT;
                return false;
            }
            uint32_t count = 0;
            if (nmo_field_resolve_count(
                    ctx->type, field, ctx->instance, &count) != NMO_OK) {
                ctx->status = NMO_ERR_VALIDATION_FAILED;
                return false;
            }
            if (count > 0 && *refs_ptr == NULL) {
                ctx->status = NMO_ERR_VALIDATION_FAILED;
                return false;
            }
            if (count == 0) return true;
            for (uint32_t i = 0; i < count; ++i) {
                nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
                if ((*refs_ptr)[i].state == NMO_REF_RESOLVED &&
                    runtime_lookup_mapping(
                        ctx->remap, (*refs_ptr)[i].id, &mapped)) {
                    (*refs_ptr)[i].id = mapped;
                }
            }
            return true;
        }
        nmo_object_id_t **ids_ptr = (nmo_object_id_t **)nmo_field_get_ptr(ctx->instance, field);
        if (ids_ptr == NULL) {
            ctx->status = NMO_ERR_INVALID_ARGUMENT;
            return false;
        }

        uint32_t count = 0;
        if (nmo_field_resolve_count(ctx->type, field, ctx->instance, &count) != NMO_OK) {
            ctx->status = NMO_ERR_VALIDATION_FAILED;
            return false;
        }
        if (count > 0 && *ids_ptr == NULL) {
            ctx->status = NMO_ERR_VALIDATION_FAILED;
            return false;
        }
        if (count == 0) return true;

        nmo_object_id_t *ids = *ids_ptr;
        for (uint32_t i = 0; i < count; i++) {
            nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
            if (runtime_lookup_mapping(ctx->remap, ids[i], &mapped)) {
                ids[i] = mapped;
            }
        }
    }

    return true;
}

/* 鈹€鈹€ Base-instance resolution 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€ */

static nmo_status_t runtime_remap_scene_objects(
    nmo_scene_state_t *state,
    const nmo_id_remap_t *remap)
{
    if (state == NULL) return NMO_OK;
    if (state->object_descs.element_size != sizeof(nmo_scene_object_desc_t) ||
        (state->object_descs.count > 0 &&
         state->object_descs.data == NULL)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    nmo_scene_object_desc_t *descs = NMO_ARRAY_DATA(
        nmo_scene_object_desc_t, &state->object_descs);
    for (size_t i = 0; i < state->object_descs.count; ++i) {
        nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
        if (descs[i].ref.state == NMO_REF_RESOLVED &&
            runtime_lookup_mapping(remap, descs[i].ref.id, &mapped)) {
            descs[i].ref.id = mapped;
        }
    }
    return NMO_OK;
}

static nmo_status_t runtime_remap_behavior_refs(
    nmo_behavior_state_t *state,
    const nmo_id_remap_t *remap)
{
    if (state == NULL) return NMO_OK;
    nmo_array_t *arrays[] = {
        &state->sub_behaviors,
        &state->sub_behavior_links,
        &state->operations,
        &state->in_parameters,
        &state->out_parameters,
        &state->local_parameters,
        &state->inputs,
        &state->outputs,
    };
    const size_t array_count = sizeof(arrays) / sizeof(arrays[0]);
    for (size_t i = 0; i < array_count; ++i) {
        if ((arrays[i]->count > 0 && arrays[i]->data == NULL) ||
            ((arrays[i]->element_size != 0 || arrays[i]->count > 0) &&
             arrays[i]->element_size != sizeof(nmo_behavior_ref_t))) {
            return NMO_ERR_VALIDATION_FAILED;
        }
    }
    for (size_t i = 0; i < array_count; ++i) {
        nmo_behavior_ref_t *refs = NMO_ARRAY_DATA(
            nmo_behavior_ref_t, arrays[i]);
        for (size_t j = 0; j < arrays[i]->count; ++j) {
            nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
            if (refs[j].ref.state == NMO_REF_RESOLVED &&
                runtime_lookup_mapping(remap, refs[j].ref.id, &mapped)) {
                refs[j].ref.id = mapped;
            }
        }
    }
    return NMO_OK;
}

static nmo_status_t runtime_remap_beobject_attributes(
    nmo_beobject_state_t *state,
    const nmo_id_remap_t *remap)
{
    if (state == NULL) return NMO_OK;
    if ((state->attributes.count > 0 && state->attributes.data == NULL) ||
        (state->attributes.element_size != 0 &&
         state->attributes.element_size != sizeof(nmo_beobject_attribute_t)) ||
        (state->attributes.count > 0 &&
         state->attributes.element_size != sizeof(nmo_beobject_attribute_t)) ||
        (state->legacy_attributes.count > 0 &&
         state->legacy_attributes.data == NULL) ||
        (state->legacy_attributes.element_size != 0 &&
         state->legacy_attributes.element_size !=
             sizeof(nmo_beobject_legacy_attribute_t)) ||
        (state->legacy_attributes.count > 0 &&
         state->legacy_attributes.element_size !=
             sizeof(nmo_beobject_legacy_attribute_t))) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    nmo_beobject_attribute_t *attributes = NMO_ARRAY_DATA(
        nmo_beobject_attribute_t, &state->attributes);
    for (size_t i = 0; i < state->attributes.count; ++i) {
        nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
        if (attributes[i].parameter.state == NMO_REF_RESOLVED &&
            runtime_lookup_mapping(
                remap, attributes[i].parameter.id, &mapped)) {
            attributes[i].parameter.id = mapped;
        }
    }

    nmo_beobject_legacy_attribute_t *legacy_attributes = NMO_ARRAY_DATA(
        nmo_beobject_legacy_attribute_t, &state->legacy_attributes);
    for (size_t i = 0; i < state->legacy_attributes.count; ++i) {
        nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
        if (legacy_attributes[i].parameter.state == NMO_REF_RESOLVED &&
            runtime_lookup_mapping(
                remap, legacy_attributes[i].parameter.id, &mapped)) {
            legacy_attributes[i].parameter.id = mapped;
        }
    }
    return NMO_OK;
}

static nmo_status_t runtime_remap_grid_layers(
    nmo_grid_state_t *state,
    const nmo_id_remap_t *remap)
{
    if (state == NULL) return NMO_OK;
    if (state->layers.element_size != sizeof(nmo_grid_layer_t) ||
        (state->layers.count > 0 && state->layers.data == NULL)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    nmo_grid_layer_t *layers = NMO_ARRAY_DATA(
        nmo_grid_layer_t, &state->layers);
    for (size_t i = 0; i < state->layers.count; ++i) {
        nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
        if (layers[i].ref.state == NMO_REF_RESOLVED &&
            runtime_lookup_mapping(remap, layers[i].ref.id, &mapped)) {
            layers[i].ref.id = mapped;
        }
    }
    return NMO_OK;
}

static nmo_status_t runtime_remap_character_parts(
    nmo_character_state_t *state,
    const nmo_id_remap_t *remap)
{
    if (state == NULL) return NMO_OK;
    if ((state->body_parts.element_size != 0 &&
         state->body_parts.element_size != sizeof(nmo_character_part_t)) ||
        (state->body_parts.count > 0 &&
         (state->body_parts.data == NULL ||
          state->body_parts.element_size != sizeof(nmo_character_part_t)))) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    nmo_character_part_t *parts = NMO_ARRAY_DATA(
        nmo_character_part_t, &state->body_parts);
    for (size_t i = 0; i < state->body_parts.count; ++i) {
        nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
        if (parts[i].ref.state == NMO_REF_RESOLVED &&
            runtime_lookup_mapping(remap, parts[i].ref.id, &mapped)) {
            parts[i].ref.id = mapped;
        }
    }
    return NMO_OK;
}

static nmo_status_t runtime_remap_patchmesh_refs(
    nmo_patchmesh_state_t *state,
    const nmo_id_remap_t *remap)
{
    if (state == NULL) return NMO_OK;
    if ((state->patch_count > 0 && state->patches == NULL) ||
        (state->channel_count > 0 && state->channels == NULL)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    for (uint32_t i = 0; i < state->patch_count; ++i) {
        nmo_ref_t *ref = &state->patches[i].material;
        nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
        if (ref->state == NMO_REF_RESOLVED &&
            runtime_lookup_mapping(remap, ref->id, &mapped)) {
            ref->id = mapped;
        }
    }
    for (uint32_t i = 0; i < state->channel_count; ++i) {
        nmo_ref_t *ref = &state->channels[i].material;
        nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
        if (ref->state == NMO_REF_RESOLVED &&
            runtime_lookup_mapping(remap, ref->id, &mapped)) {
            ref->id = mapped;
        }
    }
    return NMO_OK;
}

static nmo_status_t runtime_remap_mesh_refs(
    nmo_mesh_state_t *state,
    const nmo_id_remap_t *remap)
{
    if (!state) return NMO_OK;
    if ((state->material_group_count > 0 && !state->material_groups) ||
        (state->material_channel_count > 0 && !state->material_channels)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    for (uint32_t i = 0; i < state->material_group_count; ++i) {
        nmo_ref_t *ref = &state->material_groups[i].material;
        nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
        if (ref->state == NMO_REF_RESOLVED &&
            runtime_lookup_mapping(remap, ref->id, &mapped)) {
            ref->id = mapped;
        }
    }
    for (uint32_t i = 0; i < state->material_channel_count; ++i) {
        nmo_ref_t *ref = &state->material_channels[i].material;
        nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
        if (ref->state == NMO_REF_RESOLVED &&
            runtime_lookup_mapping(remap, ref->id, &mapped)) {
            ref->id = mapped;
        }
    }
    return NMO_OK;
}

static nmo_status_t runtime_remap_keyedanimation_refs(
    nmo_keyedanimation_state_t *state,
    const nmo_id_remap_t *remap)
{
    if (!state) return NMO_OK;
    if ((state->animation_count > 0 && !state->animation_ids) ||
        (state->subanim_count > 0 && !state->subanims)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    for (uint32_t i = 0; i < state->subanim_count; ++i) {
        nmo_ref_t *ref = &state->subanims[i].ref;
        nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
        if (ref->state == NMO_REF_RESOLVED &&
            runtime_lookup_mapping(remap, ref->id, &mapped)) {
            ref->id = mapped;
        }
    }
    return NMO_OK;
}

static nmo_status_t runtime_remap_curve_refs(
    nmo_curve_state_t *state,
    const nmo_id_remap_t *remap)
{
    if (!state) return NMO_OK;
    if ((state->control_point_count > 0 && !state->control_point_ids) ||
        (state->sub_point_count > 0 && !state->sub_points)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    for (uint32_t i = 0; i < state->sub_point_count; ++i) {
        nmo_ref_t *ref = &state->sub_points[i].ref;
        nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
        if (ref->state == NMO_REF_RESOLVED &&
            runtime_lookup_mapping(remap, ref->id, &mapped)) {
            ref->id = mapped;
        }
    }
    return NMO_OK;
}

static nmo_status_t runtime_remap_place_refs(
    nmo_place_state_t *state,
    const nmo_id_remap_t *remap)
{
    if (state == NULL) return NMO_OK;
    if (state->portals.element_size != sizeof(nmo_place_portal_entry_t) ||
        (state->portals.count > 0 && state->portals.data == NULL)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    nmo_place_portal_entry_t *entries = NMO_ARRAY_DATA(
        nmo_place_portal_entry_t, &state->portals);
    for (size_t i = 0; i < state->portals.count; ++i) {
        nmo_ref_t *refs[] = {&entries[i].place, &entries[i].portal};
        for (size_t j = 0; j < 2; ++j) {
            nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
            if (refs[j]->state == NMO_REF_RESOLVED &&
                runtime_lookup_mapping(remap, refs[j]->id, &mapped)) {
                refs[j]->id = mapped;
            }
        }
    }
    return NMO_OK;
}

static nmo_status_t runtime_remap_3dentity_skin_refs(
    nmo_3dentity_state_t *state,
    const nmo_id_remap_t *remap)
{
    if (state == NULL || state->skin == NULL) return NMO_OK;
    nmo_3dentity_skin_t *skin = state->skin;
    if (skin->bone_count > 0 && skin->bones == NULL) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    for (uint32_t i = 0; i < skin->bone_count; ++i) {
        nmo_ref_t *ref = &skin->bones[i].bone;
        nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
        if (ref->state == NMO_REF_RESOLVED &&
            runtime_lookup_mapping(remap, ref->id, &mapped)) {
            ref->id = mapped;
        }
    }
    return NMO_OK;
}

static nmo_status_t runtime_validate_dataarray_ref_storage(
    const nmo_dataarray_state_t *state)
{
    if (state == NULL) return NMO_OK;
    if ((state->column_count > 0 && state->column_formats == NULL) ||
        (state->row_count > 0 && state->rows == NULL)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    for (uint32_t column = 0; column < state->column_count; ++column) {
        switch (state->column_formats[column].type) {
        case CKARRAYTYPE_INT:
        case CKARRAYTYPE_FLOAT:
        case CKARRAYTYPE_STRING:
        case CKARRAYTYPE_OBJECT:
        case CKARRAYTYPE_PARAMETER:
            break;
        default:
            return NMO_ERR_VALIDATION_FAILED;
        }
    }
    for (uint32_t row = 0; row < state->row_count; ++row) {
        if (state->rows[row].column_count != state->column_count ||
            (state->rows[row].column_count > 0 &&
             state->rows[row].cells == NULL)) {
            return NMO_ERR_VALIDATION_FAILED;
        }
    }
    return NMO_OK;
}

static nmo_status_t runtime_remap_dataarray_refs(
    nmo_dataarray_state_t *state,
    const nmo_id_remap_t *remap)
{
    if (state == NULL) return NMO_OK;
    NMO_RETURN_IF_ERROR(runtime_validate_dataarray_ref_storage(state));

    for (uint32_t row_index = 0; row_index < state->row_count; ++row_index) {
        nmo_dataarray_row_t *row = &state->rows[row_index];
        for (uint32_t column_index = 0;
             column_index < state->column_count;
             ++column_index) {
            nmo_ref_t *ref = NULL;
            switch (state->column_formats[column_index].type) {
            case CKARRAYTYPE_OBJECT:
                ref = &row->cells[column_index].object_ref;
                break;
            case CKARRAYTYPE_PARAMETER:
                ref = &row->cells[column_index].parameter.ref;
                break;
            case CKARRAYTYPE_INT:
            case CKARRAYTYPE_FLOAT:
            case CKARRAYTYPE_STRING:
                continue;
            default:
                return NMO_ERR_VALIDATION_FAILED;
            }

            nmo_object_id_t mapped = NMO_OBJECT_ID_NONE;
            if (ref->state == NMO_REF_RESOLVED &&
                runtime_lookup_mapping(remap, ref->id, &mapped)) {
                ref->id = mapped;
            }
        }
    }
    return NMO_OK;
}

/* 鈹€鈹€ Public API 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€ */

nmo_status_t nmo_runtime_remap_copy_refs(
    const nmo_type_runtime_t *type_rt,
    const nmo_type_descriptor_t *type,
    void *instance,
    const nmo_id_remap_t *remap)
{
    if (type_rt == NULL || type_rt->types == NULL || type == NULL || instance == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (remap == NULL || nmo_id_remap_get_count(remap) == 0) {
        return NMO_OK;
    }

    const nmo_type_descriptor_ext_t *layout = type->ext;
    const bool has_layout = layout != NULL && layout->hierarchy != NULL &&
                            layout->hierarchy_depth > 0;
    const size_t level_count = has_layout ? layout->hierarchy_depth : 1u;

    for (size_t level = level_count; level > 0; --level) {
        const size_t index = level - 1u;
        const nmo_type_descriptor_t *current =
            has_layout ? layout->hierarchy[index] : type;
        const uint32_t offset = has_layout && layout->state_offsets != NULL
            ? layout->state_offsets[index]
            : 0u;
        void *current_instance = (uint8_t *)instance + offset;
        if (current == NULL) continue;
        runtime_ref_remap_ctx_t remap_ctx = {
            .remap = remap,
            .type = current,
            .instance = current_instance,
            .status = NMO_OK,
        };

        nmo_status_t remap_result = nmo_type_foreach_ref_field(
            current,
            current_instance,
            runtime_remap_ref_field,
            &remap_ctx);
        if (remap_result != NMO_OK) {
            return remap_result;
        }
        if (remap_ctx.status != NMO_OK) {
            return remap_ctx.status;
        }

        if (nmo_guid_equals(current->guid, CKPGUID_SCENE)) {
            NMO_RETURN_IF_ERROR(runtime_remap_scene_objects(
                (nmo_scene_state_t *)current_instance, remap));
        }
        if (nmo_guid_equals(current->guid, CKPGUID_BEHAVIOR)) {
            NMO_RETURN_IF_ERROR(runtime_remap_behavior_refs(
                (nmo_behavior_state_t *)current_instance, remap));
        }
        if (nmo_guid_equals(current->guid, CKPGUID_BEOBJECT)) {
            NMO_RETURN_IF_ERROR(runtime_remap_beobject_attributes(
                (nmo_beobject_state_t *)current_instance, remap));
        }
        if (nmo_guid_equals(current->guid, CKPGUID_GRID)) {
            NMO_RETURN_IF_ERROR(runtime_remap_grid_layers(
                (nmo_grid_state_t *)current_instance, remap));
        }
        if (nmo_guid_equals(current->guid, CKPGUID_CHARACTER)) {
            NMO_RETURN_IF_ERROR(runtime_remap_character_parts(
                (nmo_character_state_t *)current_instance, remap));
        }
        if (nmo_guid_equals(current->guid, CKPGUID_PATCHMESH)) {
            NMO_RETURN_IF_ERROR(runtime_remap_patchmesh_refs(
                (nmo_patchmesh_state_t *)current_instance, remap));
        }
        if (nmo_guid_equals(current->guid, CKPGUID_MESH)) {
            NMO_RETURN_IF_ERROR(runtime_remap_mesh_refs(
                (nmo_mesh_state_t *)current_instance, remap));
        }
        if (nmo_guid_equals(current->guid, CKPGUID_KEYEDANIMATION)) {
            NMO_RETURN_IF_ERROR(runtime_remap_keyedanimation_refs(
                (nmo_keyedanimation_state_t *)current_instance, remap));
        }
        if (nmo_guid_equals(current->guid, CKPGUID_CURVE)) {
            NMO_RETURN_IF_ERROR(runtime_remap_curve_refs(
                (nmo_curve_state_t *)current_instance, remap));
        }
        if (nmo_guid_equals(current->guid, CKPGUID_PLACE)) {
            NMO_RETURN_IF_ERROR(runtime_remap_place_refs(
                (nmo_place_state_t *)current_instance, remap));
        }
        if (nmo_guid_equals(current->guid, CKPGUID_3DENTITY)) {
            NMO_RETURN_IF_ERROR(runtime_remap_3dentity_skin_refs(
                (nmo_3dentity_state_t *)current_instance, remap));
        }
        if (nmo_guid_equals(current->guid, CKPGUID_DATAARRAY)) {
            NMO_RETURN_IF_ERROR(runtime_remap_dataarray_refs(
                (nmo_dataarray_state_t *)current_instance, remap));
        }

    }

    return NMO_OK;
}

nmo_status_t nmo_runtime_remap_all_refs(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    uint32_t request_flags)
{
    if (repo == NULL || type_rt == NULL || type_rt->types == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; i++) {
        nmo_object_t *obj = nmo_object_repository_get_by_index(repo, i);
        if (obj == NULL || obj->state == NULL) {
            continue;
        }

        const nmo_type_descriptor_t *type = runtime_find_type_for_object(type_rt, obj);
        if (type == NULL || type->vtable == NULL || type->vtable->remap_dependencies == NULL) {
            continue;
        }

        nmo_status_t hook_result = type->vtable->remap_dependencies(obj->state, type, repo);
        if (hook_result != NMO_OK && (request_flags & NMO_RUNTIME_REQUEST_STRICT)) {
            return hook_result;
        }
    }

    return NMO_OK;
}

static bool normalize_id_is_invalid(
    nmo_object_repository_t *repo,
    nmo_object_id_t id)
{
    return id == NMO_OBJECT_ID_INVALID ||
           (id != NMO_OBJECT_ID_NONE &&
            nmo_object_repository_find_by_id(repo, id) == NULL);
}

static bool normalize_id_has_wrong_class(
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *types,
    nmo_object_id_t id,
    nmo_class_id_t expected_class_id)
{
    if (id == NMO_OBJECT_ID_NONE || expected_class_id == 0 || types == NULL) {
        return false;
    }
    const nmo_object_t *target = nmo_object_repository_find_by_id(repo, id);
    return target != NULL && !nmo_type_query_object_is_derived_from_class(
        types, target, expected_class_id);
}

static nmo_class_id_t normalize_expected_class_for_field(const char *name)
{
    if (name == NULL) return 0;
    if (strstr(name, "mesh") != NULL) return NMO_CID_MESH;
    if (strstr(name, "animation") != NULL || strstr(name, "anim") != NULL) {
        return NMO_CID_OBJECTANIMATION;
    }
    if (strstr(name, "material") != NULL) return NMO_CID_MATERIAL;
    if (strstr(name, "texture") != NULL) return NMO_CID_TEXTURE;
    if (strcmp(name, "destination_ids") == 0) {
        return NMO_CID_PARAMETERIN;
    }
    if (strstr(name, "parameter") != NULL ||
        strcmp(name, "source_id") == 0 || strcmp(name, "in1_id") == 0 ||
        strcmp(name, "in2_id") == 0 || strcmp(name, "out_id") == 0) {
        return NMO_CID_PARAMETER;
    }
    if (strstr(name, "script") != NULL) return NMO_CID_BEHAVIOR;
    if (strcmp(name, "in_io") == 0 || strcmp(name, "out_io") == 0) {
        return NMO_CID_BEHAVIORIO;
    }
    if (strcmp(name, "grid_id") == 0 || strcmp(name, "grid") == 0) {
        return NMO_CID_GRID;
    }
    if (strcmp(name, "camera_id") == 0 || strcmp(name, "camera") == 0 ||
        strcmp(name, "starting_camera_id") == 0 ||
        strcmp(name, "starting_camera") == 0) return NMO_CID_CAMERA;
    if (strcmp(name, "level_id") == 0 || strcmp(name, "level") == 0) {
        return NMO_CID_LEVEL;
    }
    if (strstr(name, "scene") != NULL) return NMO_CID_SCENE;
    if (strcmp(name, "target") == 0) return NMO_CID_3DENTITY;
    if (strcmp(name, "entity") == 0) return NMO_CID_3DENTITY;
    if (strcmp(name, "root_entity") == 0) return NMO_CID_3DENTITY;
    if (strcmp(name, "character") == 0) return NMO_CID_CHARACTER;
    if (strcmp(name, "curve_id") == 0 || strcmp(name, "curve") == 0) {
        return NMO_CID_CURVE;
    }
    if (strcmp(name, "control_point_ids") == 0 ||
        strcmp(name, "sub_points.ref") == 0) {
        return NMO_CID_CURVEPOINT;
    }
    if (strcmp(name, "place_id") == 0 || strcmp(name, "place") == 0) {
        return NMO_CID_PLACE;
    }
    if (strcmp(name, "attached_object") == 0) return NMO_CID_3DENTITY;
    if (strcmp(name, "sprite_ref_id") == 0 ||
        strcmp(name, "sprite_ref") == 0) return NMO_CID_SPRITE;
    if (strstr(name, "body_part") != NULL) return NMO_CID_BODYPART;
    return 0;
}

static nmo_class_id_t normalize_expected_class_for_typed_field(
    const nmo_type_descriptor_t *type,
    const nmo_type_field_t *field)
{
    if (type != NULL && field != NULL && field->name != NULL &&
        nmo_guid_equals(type->guid, CKPGUID_BEHAVIOR)) {
        if (strcmp(field->name, "owner") == 0) {
            return NMO_CID_SCENEOBJECT;
        }
        if (strcmp(field->name, "target_parameter") == 0) {
            return NMO_CID_PARAMETERIN;
        }
    }
    if (type != NULL && field != NULL && field->name != NULL &&
        strcmp(field->name, "owner") == 0 &&
        (nmo_guid_equals(type->guid, CKPGUID_PARAMETERIN) ||
         nmo_guid_equals(type->guid, CKPGUID_PARAMETEROUT) ||
         nmo_guid_equals(type->guid, CKPGUID_PARAMETERLOCAL))) {
        return NMO_CID_BEHAVIOR;
    }
    if (type != NULL && field != NULL && field->name != NULL &&
        nmo_guid_equals(type->guid, CKPGUID_PARAMETEROPERATION)) {
        if (strcmp(field->name, "owner") == 0) return NMO_CID_BEHAVIOR;
        if (strcmp(field->name, "in1") == 0 ||
            strcmp(field->name, "in2") == 0 ||
            strcmp(field->name, "out") == 0) return 0;
    }
    if (type != NULL && field != NULL && field->name != NULL &&
        strcmp(field->name, "parent") == 0) {
        if (nmo_guid_equals(type->guid, CKPGUID_2DENTITY)) {
            return NMO_CID_2DENTITY;
        }
        if (nmo_guid_equals(type->guid, CKPGUID_3DENTITY)) {
            return NMO_CID_3DENTITY;
        }
    }
    return normalize_expected_class_for_field(field ? field->name : NULL);
}

static bool normalize_is_parameter_family_slot(
    const nmo_type_descriptor_t *type,
    const nmo_type_field_t *field)
{
    if (type == NULL || field == NULL || field->name == NULL) return false;
    if (nmo_guid_equals(type->guid, CKPGUID_PARAMETERIN) &&
        strcmp(field->name, "source") == 0) {
        return true;
    }
    return nmo_guid_equals(type->guid, CKPGUID_PARAMETEROPERATION) &&
           (strcmp(field->name, "in1") == 0 ||
            strcmp(field->name, "in2") == 0 ||
            strcmp(field->name, "out") == 0);
}

static bool normalize_is_parameter_object(
    const nmo_type_registry_t *types,
    const nmo_object_t *object,
    bool allow_operation)
{
    const nmo_class_id_t classes[] = {
        NMO_CID_PARAMETER,
        NMO_CID_PARAMETERIN,
        NMO_CID_PARAMETEROUT,
        NMO_CID_PARAMETERLOCAL,
        NMO_CID_PARAMETEROPERATION,
    };
    size_t count = sizeof(classes) / sizeof(classes[0]);
    if (!allow_operation) --count;
    for (size_t i = 0; i < count; ++i) {
        if (nmo_type_query_object_is_derived_from_class(
                types, object, classes[i])) {
            return true;
        }
    }
    return false;
}

static bool normalize_id_is_invalid_attribute_parameter(
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *types,
    nmo_object_id_t id)
{
    if (normalize_id_is_invalid(repo, id)) return true;
    const nmo_object_t *target = nmo_object_repository_find_by_id(repo, id);
    return target != NULL &&
           !normalize_is_parameter_object(types, target, false);
}

static bool normalize_id_is_invalid_for_typed_field(
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *types,
    const nmo_type_descriptor_t *type,
    const nmo_type_field_t *field,
    nmo_object_id_t id)
{
    if (normalize_is_parameter_family_slot(type, field)) {
        if (normalize_id_is_invalid(repo, id)) return true;
        const nmo_object_t *target =
            nmo_object_repository_find_by_id(repo, id);
        return target != NULL &&
               !normalize_is_parameter_object(types, target, true);
    }
    return normalize_id_is_invalid(repo, id) ||
        normalize_id_has_wrong_class(
            repo, types, id,
            normalize_expected_class_for_typed_field(type, field));
}

static nmo_status_t normalize_beobject_attributes(
    nmo_beobject_state_t *state,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *types,
    size_t *changes)
{
    if (!state) return NMO_OK;
    size_t count = state->attributes.count;
    if ((state->attributes.element_size != 0 &&
         state->attributes.element_size != sizeof(nmo_beobject_attribute_t)) ||
        (count > 0 &&
         state->attributes.element_size != sizeof(nmo_beobject_attribute_t)) ||
        (count > 0 && state->attributes.data == NULL)) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    for (size_t i = 0; i < count;) {
        nmo_beobject_attribute_t *attributes = NMO_ARRAY_DATA(
            nmo_beobject_attribute_t, &state->attributes);
        const nmo_object_id_t id = nmo_ref_runtime_id(
            &attributes[i].parameter);
        if (id != NMO_OBJECT_ID_NONE &&
            !normalize_id_is_invalid_attribute_parameter(repo, types, id)) {
            ++i;
            continue;
        }
        NMO_RETURN_IF_ERROR(nmo_array_remove(&state->attributes, i, NULL));
        --count;
        (*changes)++;
    }

    count = state->legacy_attributes.count;
    if ((state->legacy_attributes.element_size != 0 &&
         state->legacy_attributes.element_size !=
             sizeof(nmo_beobject_legacy_attribute_t)) ||
        (count > 0 &&
         (state->legacy_attributes.data == NULL ||
          state->legacy_attributes.element_size !=
              sizeof(nmo_beobject_legacy_attribute_t)))) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    for (size_t i = 0; i < count;) {
        nmo_beobject_legacy_attribute_t *legacy_attributes = NMO_ARRAY_DATA(
            nmo_beobject_legacy_attribute_t, &state->legacy_attributes);
        const nmo_object_id_t id = nmo_ref_runtime_id(
            &legacy_attributes[i].parameter);
        if (id != NMO_OBJECT_ID_NONE &&
            !normalize_id_is_invalid_attribute_parameter(repo, types, id)) {
            ++i;
            continue;
        }
        NMO_RETURN_IF_ERROR(nmo_array_remove(
            &state->legacy_attributes, i, NULL));
        --count;
        (*changes)++;
    }
    return NMO_OK;
}

static nmo_status_t normalize_grid_layers(
    nmo_grid_state_t *state,
    nmo_object_repository_t *repo,
    size_t *changes)
{
    if (!state) return NMO_OK;
    if (state->layers.element_size != sizeof(nmo_grid_layer_t) ||
        (state->layers.count > 0 && !state->layers.data)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    for (size_t i = 0; i < state->layers.count;) {
        nmo_grid_layer_t *layers = NMO_ARRAY_DATA(nmo_grid_layer_t, &state->layers);
        const nmo_ref_t *ref = &layers[i].ref;
        bool invalid = ref->state != NMO_REF_RESOLVED ||
                       normalize_id_is_invalid(repo, ref->id);
        if (!invalid) {
            ++i;
            continue;
        }
        NMO_RETURN_IF_ERROR(nmo_array_remove(&state->layers, i, NULL));
        (*changes)++;
    }
    return NMO_OK;
}

static nmo_status_t normalize_character_parts(
    nmo_character_state_t *state,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *types,
    size_t *changes)
{
    if (state == NULL) return NMO_OK;
    if ((state->body_parts.element_size != 0 &&
         state->body_parts.element_size != sizeof(nmo_character_part_t)) ||
        (state->body_parts.count > 0 &&
         (state->body_parts.data == NULL ||
          state->body_parts.element_size != sizeof(nmo_character_part_t)))) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    for (size_t i = 0; i < state->body_parts.count;) {
        nmo_character_part_t *parts = NMO_ARRAY_DATA(
            nmo_character_part_t, &state->body_parts);
        const nmo_object_id_t id = nmo_ref_runtime_id(&parts[i].ref);
        if (parts[i].ref.state == NMO_REF_RESOLVED &&
            !normalize_id_is_invalid(repo, id) &&
            !normalize_id_has_wrong_class(
                repo, types, id, NMO_CID_BODYPART)) {
            ++i;
            continue;
        }
        NMO_RETURN_IF_ERROR(nmo_array_remove(
            &state->body_parts, i, NULL));
        (*changes)++;
    }
    return NMO_OK;
}

static nmo_status_t normalize_scene_objects(
    nmo_scene_state_t *state,
    nmo_object_repository_t *repo,
    size_t *changes)
{
    if (state == NULL) return NMO_OK;
    if (state->object_descs.element_size != sizeof(nmo_scene_object_desc_t) ||
        (state->object_descs.count > 0 && state->object_descs.data == NULL)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    for (size_t i = 0; i < state->object_descs.count;) {
        nmo_scene_object_desc_t *descs = NMO_ARRAY_DATA(
            nmo_scene_object_desc_t, &state->object_descs);
        const nmo_object_id_t id = nmo_ref_runtime_id(&descs[i].ref);
        if (descs[i].ref.state == NMO_REF_RESOLVED &&
            !normalize_id_is_invalid(repo, id)) {
            ++i;
            continue;
        }
        NMO_RETURN_IF_ERROR(nmo_array_remove(
            &state->object_descs, i, NULL));
        (*changes)++;
    }
    return NMO_OK;
}

static nmo_status_t normalize_patchmesh_patches(
    nmo_patchmesh_state_t *state,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *types,
    size_t *changes)
{
    if (!state) return NMO_OK;
    if ((state->patch_count > 0 && !state->patches) ||
        (state->channel_count > 0 && !state->channels)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    uint32_t count = state->patch_count;
    for (uint32_t i = 0; i < count;) {
        const nmo_ref_t *ref = &state->patches[i].material;
        const nmo_object_id_t id = nmo_ref_runtime_id(ref);
        if (ref->state == NMO_REF_RESOLVED &&
            !normalize_id_is_invalid(repo, id) &&
            !normalize_id_has_wrong_class(
                repo, types, id, NMO_CID_MATERIAL)) {
            ++i;
            continue;
        }
        uint32_t remaining = count - i - 1;
        if (remaining > 0) {
            memmove(&state->patches[i], &state->patches[i + 1],
                    (size_t)remaining * sizeof(*state->patches));
        }
        state->patch_count = --count;
        (*changes)++;
    }
    for (uint32_t i = 0; i < state->channel_count; ++i) {
        nmo_ref_t *ref = &state->channels[i].material;
        const nmo_object_id_t id = nmo_ref_runtime_id(ref);
        if (ref->state != NMO_REF_NONE &&
            (ref->state != NMO_REF_RESOLVED ||
             normalize_id_is_invalid(repo, id) ||
             normalize_id_has_wrong_class(
                 repo, types, id, NMO_CID_MATERIAL))) {
            *ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
            (*changes)++;
        }
    }
    return NMO_OK;
}

static nmo_status_t normalize_mesh_materials(
    nmo_mesh_state_t *state,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *types,
    size_t *changes)
{
    if (!state) return NMO_OK;
    if ((state->material_group_count > 0 && !state->material_groups) ||
        (state->material_channel_count > 0 && !state->material_channels) ||
        (state->face_count > 0 && !state->faces)) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    uint32_t count = state->material_group_count;
    for (uint32_t i = 0; i < count;) {
        const nmo_ref_t *ref = &state->material_groups[i].material;
        const nmo_object_id_t id = nmo_ref_runtime_id(ref);
        if (ref->state == NMO_REF_RESOLVED &&
            !normalize_id_is_invalid(repo, id) &&
            !normalize_id_has_wrong_class(
                repo, types, id, NMO_CID_MATERIAL)) {
            ++i;
            continue;
        }
        uint32_t remaining = count - i - 1u;
        if (remaining > 0) {
            memmove(&state->material_groups[i],
                    &state->material_groups[i + 1u],
                    (size_t)remaining * sizeof(*state->material_groups));
        }
        state->material_group_count = --count;
        for (uint32_t face = 0; face < state->face_count; ++face) {
            uint16_t *index = &state->faces[face].material_group_idx;
            if (*index == i) {
                *index = 0;
            } else if (*index > i) {
                --*index;
            }
        }
        (*changes)++;
    }

    for (uint32_t i = 0; i < state->material_channel_count; ++i) {
        nmo_ref_t *ref = &state->material_channels[i].material;
        const nmo_object_id_t id = nmo_ref_runtime_id(ref);
        if (ref->state != NMO_REF_NONE &&
            (ref->state != NMO_REF_RESOLVED ||
             normalize_id_is_invalid(repo, id) ||
             normalize_id_has_wrong_class(
                 repo, types, id, NMO_CID_MATERIAL))) {
            *ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
            (*changes)++;
        }
    }
    return NMO_OK;
}

static nmo_status_t normalize_keyed_animation(
    nmo_keyedanimation_state_t *state,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *types,
    size_t *changes)
{
    if (!state) return NMO_OK;
    if (state->animation_count > 0 && !state->animation_ids) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (state->subanim_count > 0 && !state->subanims) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    uint32_t count = state->animation_count;
    for (uint32_t i = 0; i < count;) {
        const nmo_ref_t *ref = &state->animation_ids[i];
        const nmo_object_id_t id = nmo_ref_runtime_id(ref);
        if (ref->state == NMO_REF_RESOLVED &&
            !normalize_id_is_invalid(repo, id) &&
            !normalize_id_has_wrong_class(
                repo, types, id, NMO_CID_OBJECTANIMATION)) {
            ++i;
            continue;
        }
        uint32_t remaining = count - i - 1;
        if (remaining > 0) {
            memmove(&state->animation_ids[i], &state->animation_ids[i + 1],
                    (size_t)remaining * sizeof(*state->animation_ids));
        }
        state->animation_count = --count;
        (*changes)++;
    }

    count = state->subanim_count;
    for (uint32_t i = 0; i < count;) {
        const nmo_ref_t *ref = &state->subanims[i].ref;
        const nmo_object_id_t id = nmo_ref_runtime_id(ref);
        if (ref->state == NMO_REF_RESOLVED &&
            !normalize_id_is_invalid(repo, id) &&
            !normalize_id_has_wrong_class(
                repo, types, id, NMO_CID_OBJECTANIMATION)) {
            ++i;
            continue;
        }
        if (state->subanims[i].chunk != NULL) {
            nmo_chunk_destroy(state->subanims[i].chunk);
            state->subanims[i].chunk = NULL;
        }
        const uint32_t remaining = count - i - 1;
        if (remaining > 0) {
            memmove(&state->subanims[i], &state->subanims[i + 1],
                    (size_t)remaining * sizeof(*state->subanims));
        }
        state->subanim_count = --count;
        state->subanims[count].chunk = NULL;
        (*changes)++;
    }
    return NMO_OK;
}

static nmo_status_t normalize_curve_sub_points(
    nmo_curve_state_t *state,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *types,
    size_t *changes)
{
    if (!state) return NMO_OK;
    if ((state->control_point_count > 0 && !state->control_point_ids) ||
        (state->sub_point_count > 0 && !state->sub_points)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    uint32_t count = state->sub_point_count;
    for (uint32_t i = 0; i < count;) {
        const nmo_ref_t *ref = &state->sub_points[i].ref;
        const nmo_object_id_t id = nmo_ref_runtime_id(ref);
        if (ref->state == NMO_REF_RESOLVED &&
            !normalize_id_is_invalid(repo, id) &&
            !normalize_id_has_wrong_class(
                repo, types, id, NMO_CID_CURVEPOINT)) {
            ++i;
            continue;
        }
        if (state->sub_points[i].chunk != NULL) {
            nmo_chunk_destroy(state->sub_points[i].chunk);
            state->sub_points[i].chunk = NULL;
        }
        const uint32_t remaining = count - i - 1u;
        if (remaining > 0) {
            memmove(&state->sub_points[i], &state->sub_points[i + 1u],
                    (size_t)remaining * sizeof(*state->sub_points));
        }
        state->sub_point_count = --count;
        state->sub_points[count].chunk = NULL;
        (*changes)++;
    }
    return NMO_OK;
}

static nmo_status_t normalize_place_portals(
    nmo_place_state_t *state,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *types,
    size_t *changes)
{
    if (state == NULL) return NMO_OK;
    if (state->portals.element_size != sizeof(nmo_place_portal_entry_t) ||
        (state->portals.count > 0 && state->portals.data == NULL)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    for (size_t i = 0; i < state->portals.count;) {
        nmo_place_portal_entry_t *entries = NMO_ARRAY_DATA(
            nmo_place_portal_entry_t, &state->portals);
        const nmo_object_id_t place_id = nmo_ref_runtime_id(
            &entries[i].place);
        const nmo_object_id_t portal_id = nmo_ref_runtime_id(
            &entries[i].portal);
        const bool valid =
            entries[i].place.state == NMO_REF_RESOLVED &&
            entries[i].portal.state == NMO_REF_RESOLVED &&
            !normalize_id_is_invalid(repo, place_id) &&
            !normalize_id_is_invalid(repo, portal_id) &&
            !normalize_id_has_wrong_class(
                repo, types, place_id, NMO_CID_PLACE) &&
            !normalize_id_has_wrong_class(
                repo, types, portal_id, NMO_CID_3DENTITY);
        if (valid) {
            ++i;
            continue;
        }
        NMO_RETURN_IF_ERROR(nmo_array_remove(&state->portals, i, NULL));
        (*changes)++;
    }
    return NMO_OK;
}

static nmo_status_t normalize_3dentity_skin_bones(
    nmo_3dentity_state_t *state,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *types,
    size_t *changes)
{
    if (state == NULL || state->skin == NULL) return NMO_OK;
    nmo_3dentity_skin_t *skin = state->skin;
    if (skin->bone_count > 0 && skin->bones == NULL) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    for (uint32_t i = 0; i < skin->bone_count; ++i) {
        nmo_ref_t *ref = &skin->bones[i].bone;
        const nmo_object_id_t id = nmo_ref_runtime_id(ref);
        if (ref->state == NMO_REF_NONE) continue;
        if (ref->state == NMO_REF_RESOLVED &&
            !normalize_id_is_invalid(repo, id) &&
            !normalize_id_has_wrong_class(
                repo, types, id, NMO_CID_3DENTITY)) {
            continue;
        }
        *ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        (*changes)++;
    }
    return NMO_OK;
}

static nmo_status_t normalize_dataarray_cells(
    nmo_dataarray_state_t *state,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *types,
    size_t *changes)
{
    if (state == NULL) return NMO_OK;
    NMO_RETURN_IF_ERROR(runtime_validate_dataarray_ref_storage(state));

    for (uint32_t row_index = 0; row_index < state->row_count; ++row_index) {
        nmo_dataarray_row_t *row = &state->rows[row_index];
        for (uint32_t column_index = 0;
             column_index < state->column_count;
             ++column_index) {
            const CK_ARRAYTYPE column_type =
                state->column_formats[column_index].type;
            nmo_ref_t *ref = NULL;
            bool valid = true;
            if (column_type == CKARRAYTYPE_OBJECT) {
                ref = &row->cells[column_index].object_ref;
                valid = ref->state == NMO_REF_NONE ||
                    (ref->state == NMO_REF_RESOLVED &&
                     !normalize_id_is_invalid(repo, ref->id));
            } else if (column_type == CKARRAYTYPE_PARAMETER) {
                ref = &row->cells[column_index].parameter.ref;
                if (ref->state != NMO_REF_NONE) {
                    const nmo_object_t *target =
                        ref->state == NMO_REF_RESOLVED
                            ? nmo_object_repository_find_by_id(repo, ref->id)
                            : NULL;
                    valid = target != NULL &&
                        normalize_is_parameter_object(types, target, true);
                }
            } else if (column_type != CKARRAYTYPE_INT &&
                       column_type != CKARRAYTYPE_FLOAT &&
                       column_type != CKARRAYTYPE_STRING) {
                return NMO_ERR_VALIDATION_FAILED;
            }
            if (ref != NULL && !valid) {
                *ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
                ++*changes;
            }
        }
    }
    return NMO_OK;
}

typedef struct normalize_ref_ctx {
    nmo_object_repository_t *repo;
    const nmo_type_registry_t *types;
    const nmo_type_descriptor_t *type;
    void *instance;
    size_t *changes;
} normalize_ref_ctx_t;

static bool normalize_ref_field(
    void *user_data,
    const nmo_type_field_t *field,
    const void *field_ptr)
{
    (void)field_ptr;
    normalize_ref_ctx_t *ctx = (normalize_ref_ctx_t *)user_data;
    if (!ctx || !field || !nmo_field_is_ref(field)) return true;

    if (!nmo_field_is_array(field) && field->size == sizeof(nmo_ref_t)) {
        nmo_ref_t *ref = (nmo_ref_t *)nmo_field_get_ptr(
            ctx->instance, field);
        const nmo_object_id_t id = nmo_ref_runtime_id(ref);
        if (ref != NULL && ref->state != NMO_REF_NONE &&
            (id == NMO_OBJECT_ID_NONE ||
             normalize_id_is_invalid_for_typed_field(
                ctx->repo, ctx->types, ctx->type, field, id))) {
            *ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
            (*ctx->changes)++;
        }
        return true;
    }

    if (!nmo_field_is_array(field) && field->size == sizeof(nmo_object_id_t)) {
        nmo_object_id_t *id = (nmo_object_id_t *)nmo_field_get_ptr(
            ctx->instance, field);
        if (id && normalize_id_is_invalid_for_typed_field(
                ctx->repo, ctx->types, ctx->type, field, *id)) {
            *id = NMO_OBJECT_ID_NONE;
            (*ctx->changes)++;
        }
        return true;
    }

    if (field->size == sizeof(nmo_array_t)) {
        nmo_array_t *array = (nmo_array_t *)nmo_field_get_ptr(ctx->instance, field);
        if (!array) return true;
        if (nmo_field_uses_ref_records(field)) {
            if (array->element_size != sizeof(nmo_ref_t)) return true;
            for (size_t i = 0; i < array->count;) {
                nmo_ref_t *refs = NMO_ARRAY_DATA(nmo_ref_t, array);
                const nmo_object_id_t id = nmo_ref_runtime_id(&refs[i]);
                if (refs[i].state == NMO_REF_RESOLVED &&
                    !normalize_id_is_invalid_for_typed_field(
                        ctx->repo, ctx->types, ctx->type, field, id)) {
                    ++i;
                    continue;
                }
                if (nmo_array_remove(array, i, NULL) != NMO_OK) return false;
                (*ctx->changes)++;
            }
            return true;
        }
        if (array->element_size != sizeof(nmo_object_id_t)) return true;
        for (size_t i = 0; i < array->count;) {
            nmo_object_id_t *ids = NMO_ARRAY_DATA(nmo_object_id_t, array);
            if (!normalize_id_is_invalid_for_typed_field(
                    ctx->repo, ctx->types, ctx->type, field, ids[i])) {
                ++i;
                continue;
            }
            if (nmo_array_remove(array, i, NULL) != NMO_OK) return false;
            (*ctx->changes)++;
        }
        return true;
    }

    if (field->size == sizeof(void *)) {
        if (nmo_field_uses_ref_records(field)) {
            nmo_ref_t **values = (nmo_ref_t **)nmo_field_get_ptr(
                ctx->instance, field);
            uint32_t count = 0;
            if (!values || !*values || nmo_field_resolve_count(
                    ctx->type, field, ctx->instance, &count) != NMO_OK) {
                return true;
            }
            uint32_t kept = 0;
            for (uint32_t i = 0; i < count; ++i) {
                const nmo_object_id_t id = nmo_ref_runtime_id(&(*values)[i]);
                if ((*values)[i].state != NMO_REF_RESOLVED ||
                    normalize_id_is_invalid_for_typed_field(
                        ctx->repo, ctx->types, ctx->type, field, id)) {
                    (*ctx->changes)++;
                    continue;
                }
                (*values)[kept++] = (*values)[i];
            }
            if (kept != count && field->count_field_name) {
                const nmo_type_field_t *count_field = nmo_type_get_field_by_name(
                    ctx->type, field->count_field_name);
                if (count_field && count_field->size == sizeof(uint32_t)) {
                    uint32_t *count_ptr = (uint32_t *)nmo_field_get_ptr(
                        ctx->instance, count_field);
                    if (count_ptr) *count_ptr = kept;
                }
            }
            return true;
        }
        nmo_object_id_t **values = (nmo_object_id_t **)nmo_field_get_ptr(
            ctx->instance, field);
        uint32_t count = 0;
        if (!values || !*values || nmo_field_resolve_count(
                ctx->type, field, ctx->instance, &count) != NMO_OK) {
            return true;
        }
        uint32_t kept = 0;
        for (uint32_t i = 0; i < count; ++i) {
            if (normalize_id_is_invalid_for_typed_field(
                    ctx->repo, ctx->types, ctx->type, field, (*values)[i])) {
                (*ctx->changes)++;
                continue;
            }
            (*values)[kept++] = (*values)[i];
        }
        if (kept != count && field->count_field_name) {
            const nmo_type_field_t *count_field = nmo_type_get_field_by_name(
                ctx->type, field->count_field_name);
            if (count_field && count_field->size == sizeof(uint32_t)) {
                uint32_t *count_ptr = (uint32_t *)nmo_field_get_ptr(
                    ctx->instance, count_field);
                if (count_ptr) *count_ptr = kept;
            }
        }
    }
    return true;
}

nmo_status_t nmo_runtime_normalize_invalid_refs(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    size_t *out_change_count)
{
    if (!repo || !type_rt || !type_rt->types) return NMO_ERR_INVALID_ARGUMENT;
    size_t changed = 0;
    size_t count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < count; ++i) {
        nmo_object_t *obj = nmo_object_repository_get_by_index(repo, i);
        if (!obj || !obj->state) continue;
        nmo_behavior_state_t *state = (nmo_behavior_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                type_rt->types, obj, CKPGUID_BEHAVIOR);
        if (state) {
            size_t object_changes = 0;
            NMO_RETURN_IF_ERROR(nmo_behavior_normalize_references(
                state, repo, &object_changes));
            changed += object_changes;
        }

        nmo_beobject_state_t *beobject = (nmo_beobject_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                type_rt->types, obj, CKPGUID_BEOBJECT);
        NMO_RETURN_IF_ERROR(normalize_beobject_attributes(
            beobject, repo, type_rt->types, &changed));
        nmo_character_state_t *character = (nmo_character_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                type_rt->types, obj, CKPGUID_CHARACTER);
        NMO_RETURN_IF_ERROR(normalize_character_parts(
            character, repo, type_rt->types, &changed));
        nmo_grid_state_t *grid = (nmo_grid_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                type_rt->types, obj, CKPGUID_GRID);
        NMO_RETURN_IF_ERROR(normalize_grid_layers(grid, repo, &changed));
        nmo_scene_state_t *scene = (nmo_scene_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                type_rt->types, obj, CKPGUID_SCENE);
        NMO_RETURN_IF_ERROR(normalize_scene_objects(
            scene, repo, &changed));
        nmo_patchmesh_state_t *patchmesh = (nmo_patchmesh_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                type_rt->types, obj, CKPGUID_PATCHMESH);
        NMO_RETURN_IF_ERROR(normalize_patchmesh_patches(
            patchmesh, repo, type_rt->types, &changed));
        nmo_mesh_state_t *mesh = (nmo_mesh_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                type_rt->types, obj, CKPGUID_MESH);
        NMO_RETURN_IF_ERROR(normalize_mesh_materials(
            mesh, repo, type_rt->types, &changed));
        nmo_keyedanimation_state_t *keyed = (nmo_keyedanimation_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                type_rt->types, obj, CKPGUID_KEYEDANIMATION);
        NMO_RETURN_IF_ERROR(normalize_keyed_animation(
            keyed, repo, type_rt->types, &changed));
        nmo_curve_state_t *curve = (nmo_curve_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                type_rt->types, obj, CKPGUID_CURVE);
        NMO_RETURN_IF_ERROR(normalize_curve_sub_points(
            curve, repo, type_rt->types, &changed));
        nmo_place_state_t *place = (nmo_place_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                type_rt->types, obj, CKPGUID_PLACE);
        NMO_RETURN_IF_ERROR(normalize_place_portals(
            place, repo, type_rt->types, &changed));
        nmo_3dentity_state_t *entity3d = (nmo_3dentity_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                type_rt->types, obj, CKPGUID_3DENTITY);
        NMO_RETURN_IF_ERROR(normalize_3dentity_skin_bones(
            entity3d, repo, type_rt->types, &changed));
        nmo_dataarray_state_t *dataarray = (nmo_dataarray_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(
                type_rt->types, obj, CKPGUID_DATAARRAY);
        NMO_RETURN_IF_ERROR(normalize_dataarray_cells(
            dataarray, repo, type_rt->types, &changed));

        const nmo_type_descriptor_t *derived =
            runtime_find_type_for_object(type_rt, obj);
        if (derived == NULL) continue;
        const nmo_type_descriptor_ext_t *layout = derived->ext;
        const bool has_layout = layout != NULL && layout->hierarchy != NULL &&
                                layout->hierarchy_depth > 0;
        const size_t level_count = has_layout ? layout->hierarchy_depth : 1u;
        for (size_t level = level_count; level > 0; --level) {
            const size_t index = level - 1u;
            const nmo_type_descriptor_t *current =
                has_layout ? layout->hierarchy[index] : derived;
            const uint32_t offset = has_layout && layout->state_offsets != NULL
                ? layout->state_offsets[index]
                : 0u;
            void *current_instance = (uint8_t *)obj->state + offset;
            if (current == NULL) continue;
            normalize_ref_ctx_t normalize_ctx = {
                .repo = repo, .types = type_rt->types,
                .type = current, .instance = current_instance,
                .changes = &changed,
            };
            NMO_RETURN_IF_ERROR(nmo_type_foreach_ref_field(
                current, current_instance, normalize_ref_field, &normalize_ctx));
        }
    }
    if (out_change_count) *out_change_count = changed;
    return NMO_OK;
}
