/**
 * @file ckgrid_schemas.c
 * @brief CKGrid schema implementation
 */

#include "object/builtin/nmo_grid_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_object_struct_guids.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_system.h"
#include "type/nmo_param_guids.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"
#include "object/nmo_object_repository.h"
#include <string.h>

static void grid_layer_dispose(void *element, void *user_data)
{
    (void)user_data;
    nmo_grid_layer_t *layer = (nmo_grid_layer_t *)element;
    if (layer && layer->chunk) {
        nmo_chunk_destroy(layer->chunk);
        layer->chunk = NULL;
    }
}

static void grid_layers_set_lifecycle(nmo_array_t *layers)
{
    nmo_container_lifecycle_t lifecycle = NMO_CONTAINER_LIFECYCLE_INIT;
    lifecycle.dispose = grid_layer_dispose;
    nmo_array_set_lifecycle(layers, &lifecycle);
}

static void nmo_grid_dispose_state_arrays(nmo_grid_state_t *state);
static nmo_status_t nmo_grid_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

/* RCKGrid::RCKGrid scales the new grid to (1, 10, 1). The attribute named
   "Grid" it also sets needs the attribute manager. */
static void nmo_grid_set_defaults(void *instance)
{
    nmo_grid_state_t *state = instance;
    state->base.world_matrix[0] = 1.0f;
    state->base.world_matrix[5] = 10.0f;
    state->base.world_matrix[10] = 1.0f;
    state->base.world_matrix[15] = 1.0f;
    state->has_grid_data = 1;
}

static const nmo_object_state_member_t nmo_grid_layer_members[] = {
    NMO_STATE_VALUE(nmo_grid_layer_t, ref),
    NMO_STATE_CHUNK(nmo_grid_layer_t, chunk)
};

static const nmo_object_state_layout_t nmo_grid_layer_layout = {
    .size = sizeof(nmo_grid_layer_t),
    .members = nmo_grid_layer_members,
    .member_count = sizeof(nmo_grid_layer_members) /
        sizeof(nmo_grid_layer_members[0]),
};

static const nmo_object_state_member_t nmo_grid_members[] = {
    NMO_STATE_VALUE(nmo_grid_state_t, width),
    NMO_STATE_VALUE(nmo_grid_state_t, length),
    NMO_STATE_VALUE(nmo_grid_state_t, reserved_value),
    NMO_STATE_VALUE(nmo_grid_state_t, priority),
    NMO_STATE_VALUE(nmo_grid_state_t, orientation_mode),
    NMO_STATE_VALUE(nmo_grid_state_t, has_grid_data),
    NMO_STATE_VALUE(nmo_grid_state_t, has_file_flag),
    NMO_STATE_VALUE(nmo_grid_state_t, file_flag),
    NMO_STATE_RECORDS(nmo_grid_state_t, layers, nmo_grid_layer_layout)
};

static const nmo_object_state_layout_t nmo_grid_layout = {
    .size = sizeof(nmo_grid_state_t),
    .base_vtable = &nmo_3dentity_vtable,
    .members = nmo_grid_members,
    .member_count = sizeof(nmo_grid_members) / sizeof(nmo_grid_members[0]),
    .set_defaults = nmo_grid_set_defaults,
    .validate = nmo_grid_validate,
};

NMO_DEFINE_OBJECT_LAYOUT_LIFECYCLE(grid, nmo_grid_layout)
NMO_DEFINE_OBJECT_LAYOUT_COPY(grid, nmo_grid_layout)

static void nmo_grid_dispose_state_arrays(nmo_grid_state_t *state)
{
    if (state == NULL) return;
    nmo_array_dispose(&state->layers);
    nmo_3dentity_vtable.destroy(&state->base, NULL, NULL);
}

static nmo_status_t nmo_grid_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_grid_state_t *out_state = (nmo_grid_state_t *)instance;
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_grid_deserialize");
    }

    nmo_status_t result = nmo_3dentity_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    out_state->has_grid_data = 0;
    out_state->has_file_flag = 0;
    out_state->file_flag = 0;

    const bool is_file = nmo_object_deserialize_is_file(chunk, context);

    size_t section_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_GRIDDATA, &section_dwords);
    if (result == NMO_ERR_NOT_FOUND) {
        NMO_RETURN_OK();
    }
    if (result != NMO_OK) return result;
    const size_t section_end =
        nmo_chunk_get_position(chunk) + section_dwords;
    const size_t minimum_header_dwords =
        is_file ? 7u : 6u;
    if (section_dwords < minimum_header_dwords) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }
    out_state->has_grid_data = 1;

    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->width));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->length));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(
        chunk, &out_state->reserved_value));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->priority));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->orientation_mode));

    if (is_file) {
        int32_t file_flag = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &file_flag));
        out_state->has_file_flag = 1;
        out_state->file_flag = file_flag;
    }

    size_t count = 0;
    NMO_RETURN_IF_ERROR(nmo_chunk_read_object_sequence_start(chunk, &count));
    if (count > SIZE_MAX / sizeof(nmo_grid_layer_t)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    const size_t minimum_dwords_per_layer =
        is_file ? 1u : 2u;
    if (count >
        nmo_chunk_identifier_remaining_dwords(chunk) /
            minimum_dwords_per_layer) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }

    nmo_array_t layers;
    NMO_RETURN_IF_ERROR(nmo_array_init(
        &layers, sizeof(nmo_grid_layer_t), count, &out_state->layers.allocator));
    grid_layers_set_lifecycle(&layers);
    nmo_grid_layer_t *items = NULL;
    result = nmo_array_extend(&layers, count, (void **)&items);
    if (result != NMO_OK) {
        nmo_array_dispose(&layers);
        return result;
    }
    for (size_t i = 0; i < count; ++i) {
        result = nmo_ref_read(chunk, &items[i].ref);
        if (result != NMO_OK) {
            nmo_array_dispose(&layers);
            return result;
        }
        nmo_ref_check_class(
            &items[i].ref,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_LAYER);
    }
    if (!is_file) {
        for (size_t i = 0; i < count; ++i) {
            result = nmo_chunk_read_sub_chunk(chunk, &items[i].chunk);
            if (result != NMO_OK) {
                nmo_array_dispose(&layers);
                return result;
            }
        }
    }
    const size_t position = nmo_chunk_get_position(chunk);
    if (position > section_end) {
        nmo_array_dispose(&layers);
        return NMO_ERR_TRUNCATED_CHUNK;
    }
    nmo_array_dispose(&out_state->layers);
    out_state->layers = layers;

    NMO_RETURN_OK();
}

nmo_status_t nmo_grid_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_grid_state_t *out_state = (nmo_grid_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_grid_state_t decoded;
    nmo_status_t result = nmo_grid_create(&decoded, type, context);
    if (result != NMO_OK) return result;

    nmo_beobject_state_t *decoded_base = &decoded.base.base.base;
    const nmo_beobject_state_t *old_base = &out_state->base.base.base;
    if (old_base->scripts.allocator.alloc != NULL) {
        decoded_base->scripts.allocator = old_base->scripts.allocator;
    }
    if (old_base->attributes.allocator.alloc != NULL) {
        decoded_base->attributes.allocator = old_base->attributes.allocator;
    }
    if (old_base->legacy_attributes.allocator.alloc != NULL) {
        decoded_base->legacy_attributes.allocator =
            old_base->legacy_attributes.allocator;
    }
    if (out_state->layers.allocator.alloc != NULL) {
        decoded.layers.allocator = out_state->layers.allocator;
    }

    result = nmo_grid_deserialize_internal(&decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_grid_dispose_state_arrays(&decoded);
        return result;
    }

    nmo_grid_dispose_state_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
}

static nmo_status_t nmo_grid_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_grid_state_t *in_state = (const nmo_grid_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_grid_serialize");
    }

    nmo_status_t result = nmo_3dentity_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    const bool is_file = nmo_object_serialize_is_file(out_chunk, context);
    if (!is_file) {
        uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
        if ((save_flags & CK_STATESAVE_GRIDONLY) == 0) {
            return NMO_OK;
        }
    }

    if (!in_state->has_grid_data) {
        return NMO_OK;
    }

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_GRIDDATA);
    if (result != NMO_OK) return result;

    NMO_RETURN_IF_ERROR(nmo_chunk_write_int(out_chunk, in_state->width));
    NMO_RETURN_IF_ERROR(nmo_chunk_write_int(out_chunk, in_state->length));
    NMO_RETURN_IF_ERROR(nmo_chunk_write_int(
        out_chunk, in_state->reserved_value));
    NMO_RETURN_IF_ERROR(nmo_chunk_write_int(out_chunk, in_state->priority));
    NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(out_chunk, in_state->orientation_mode));

    if (is_file) {
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(out_chunk, in_state->has_file_flag ? in_state->file_flag : 1));
    }

    result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->layers.count);
    if (result != NMO_OK) return result;

    const nmo_grid_layer_t *layers = NMO_ARRAY_DATA(nmo_grid_layer_t, &in_state->layers);
    for (size_t i = 0; i < in_state->layers.count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_ref_write_sequence_item(out_chunk, &layers[i].ref));
    }

    if (!is_file) {
        for (size_t i = 0; i < in_state->layers.count; ++i) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_sub_chunk(out_chunk, layers[i].chunk));
        }
    }

    NMO_RETURN_OK();
}

NMO_DEFINE_OBJECT_STAGED_SERIALIZE_VALIDATED(nmo_grid)

static const nmo_type_field_t nmo_grid_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_grid_state_t, base),
                    sizeof(nmo_3dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_grid_state_t, width, CKPGUID_INT),
    NMO_FIELD(nmo_grid_state_t, length, CKPGUID_INT),
    NMO_FIELD(nmo_grid_state_t, reserved_value, CKPGUID_INT),
    NMO_FIELD(nmo_grid_state_t, priority, CKPGUID_INT),
    NMO_FIELD(nmo_grid_state_t, orientation_mode, CKPGUID_UINT32),
    NMO_FIELD(nmo_grid_state_t, has_grid_data, CKPGUID_UINT8),
    NMO_FIELD(nmo_grid_state_t, has_file_flag, CKPGUID_UINT8),
    NMO_FIELD(nmo_grid_state_t, file_flag, CKPGUID_INT),
    NMO_FIELD_ARRAY(nmo_grid_state_t, layers, NMO_GUID_STRUCT_CKGRIDLAYER)
};

static nmo_status_t nmo_grid_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_grid_state_t *s = instance;
    if (s == NULL) return NMO_ERR_INVALID_ARGUMENT;
    NMO_RETURN_IF_ERROR(nmo_3dentity_vtable.validate(
        &s->base, NULL, context));
    NMO_VALIDATE_COUNT(s->layers.data, s->layers.count, "layers");
    if (s->layers.count > (size_t)INT32_MAX) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (s->layers.element_size != sizeof(nmo_grid_layer_t)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    NMO_RETURN_OK();
}

NMO_DEFINE_OBJECT_PREPARE_VIA_VALIDATE(nmo_grid)

nmo_status_t nmo_grid_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_grid_remap_dependencies");
    }

    nmo_grid_state_t *state = (nmo_grid_state_t *)instance;
    nmo_status_t result = nmo_3dentity_remap_dependencies(&state->base, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    if (state->layers.count > 0 && state->layers.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Grid layers missing");
    }
    return nmo_grid_validate(state, NULL, NULL);
}

static nmo_status_t nmo_grid_enumerate_refs(
    const void *instance,
    const nmo_type_descriptor_t *type,
    nmo_type_ref_visitor_fn visitor,
    void *user_data)
{
    (void)type;
    const nmo_grid_state_t *state = instance;
    if (!state || !visitor) return NMO_OK;
    NMO_RETURN_IF_ERROR(nmo_grid_validate(state, NULL, NULL));
    const nmo_grid_layer_t *layers = NMO_ARRAY_DATA(
        nmo_grid_layer_t, &state->layers);
    for (size_t i = 0; i < state->layers.count; ++i) {
        if (layers[i].ref.state != NMO_REF_RESOLVED ||
            layers[i].ref.id == NMO_OBJECT_ID_NONE) {
            continue;
        }
        if (!visitor(user_data, layers[i].ref.id, 0, "layers", (uint32_t)i)) {
            break;
        }
    }
    return NMO_OK;
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

static const nmo_object_serialize_pass_t nmo_grid_compare_passes[] = {
    {
        .class_id = NMO_CID_GRID,
        .data_version = 7,
        .chunk_options = NMO_CHUNK_OPTION_FILE,
        .serialize_flags = NMO_SERIALIZE_FLAG_FILE_MODE,
        .use_context = 1,
    },
    {
        .class_id = NMO_CID_GRID,
        .data_version = 7,
        .save_flags = CK_STATESAVE_GRIDONLY,
        .use_context = 1,
    },
};

static bool nmo_grid_equals(const void *a, const void *b)
{
    return nmo_object_serialized_state_equals(
        a, b, nmo_grid_serialize,
        nmo_grid_compare_passes,
        sizeof(nmo_grid_compare_passes) /
            sizeof(nmo_grid_compare_passes[0]),
        4096);
}

static uint32_t nmo_grid_hash(const void *instance)
{
    return nmo_object_serialized_state_hash(
        instance, nmo_grid_serialize,
        nmo_grid_compare_passes,
        sizeof(nmo_grid_compare_passes) /
            sizeof(nmo_grid_compare_passes[0]),
        4096);
}

nmo_type_vtable_t nmo_grid_vtable = {
    .prepare_dependencies = nmo_grid_prepare_dependencies,
    .remap_dependencies = nmo_grid_remap_dependencies,
    .pre_delete = nmo_object_pre_delete_checked,
    .post_delete = nmo_object_post_delete_noop,
    NMO_OBJECT_VTABLE_EX(
        nmo_grid_create,
        nmo_grid_destroy,
        nmo_grid_serialize,
        nmo_grid_deserialize,
        nmo_grid_copy,
        nmo_grid_validate,
        nmo_grid_equals,
        nmo_grid_hash,
        nmo_grid_enumerate_refs)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_grid_type,
    CKPGUID_GRID,
    "CKGrid",
    NMO_CID_GRID,
    CKPGUID_3DENTITY,
    nmo_grid_state_t,
    &nmo_grid_vtable,
    nmo_grid_fields)
