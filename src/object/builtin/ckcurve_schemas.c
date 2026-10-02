/**
 * @file ckcurve_schemas.c
 * @brief CKCurve and CKCurvePoint schema implementation
 */

#include "object/builtin/nmo_curve_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "type/nmo_reflection.h"
#include "type/nmo_param_guids.h"
#include "object/nmo_object_struct_guids.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include <string.h>

static void nmo_curve_set_defaults(void *instance) {
    nmo_curve_state_t *state = instance;

    state->has_curve_data = 1;
    state->control_point_count = 0;
    state->control_point_ids = NULL;
    state->fitting_coeff = 0.0f;
    state->step_count = 100;
    state->opened = 1;
    state->sub_point_count = 0;
    state->sub_points = NULL;

    state->has_curveonly_chunk = 1;
    state->has_controlpoints_chunk = 0;
    state->has_fitting_chunk = 0;
    state->has_steps_chunk = 0;
    state->has_open_chunk = 0;
    state->has_savepoints_chunk = 0;
    state->savepoints_in_file = 0;
}

static void nmo_curvepoint_set_defaults(void *instance) {
    nmo_curvepoint_state_t *state = instance;

    state->has_default_data = 1;
    state->defaultdata_is_modern = 1;
    state->curve = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->tangent_mode = 0;
    state->linear = 0;
    state->tension = 0.0f;
    state->continuity = 0.0f;
    state->bias = 0.0f;
    state->tangent_in = (nmo_vector_t){0.0f, 0.0f, 0.0f};
    state->tangent_out = (nmo_vector_t){0.0f, 0.0f, 0.0f};
    state->has_reserved_vector = 0;
    state->reserved_vector = (nmo_vector_t){0.0f, 0.0f, 0.0f};
    state->has_tcb_chunk = 0;
    state->has_tangents_chunk = 0;
    state->has_legacy_position = 0;
    state->legacy_position = (nmo_vector_t){0.0f, 0.0f, 0.0f};
}

static nmo_status_t read_object_sequence(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    size_t trailing_dwords,
    nmo_ref_t **out_ids,
    uint32_t *out_count)
{
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result != NMO_OK) return result;

    if (count > UINT32_MAX || count > SIZE_MAX / sizeof(nmo_ref_t)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Curve control point count overflow");
    }
    const size_t remaining_dwords =
        nmo_chunk_identifier_remaining_dwords(chunk);
    if (trailing_dwords > remaining_dwords ||
        count > remaining_dwords - trailing_dwords) {
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "Curve control point count exceeds identifier payload");
    }
    if (count == 0) {
        *out_ids = NULL;
        *out_count = 0;
        NMO_RETURN_OK();
    }

    nmo_ref_t *ids = (nmo_ref_t *)nmo_arena_alloc(
        arena, sizeof(nmo_ref_t) * count, _Alignof(nmo_ref_t));
    if (!ids) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate curve control point array");
    }

    for (size_t i = 0; i < count; ++i) {
        result = nmo_ref_read(chunk, &ids[i]);
        if (result != NMO_OK) {
            return result;
        }
    }

    *out_ids = ids;
    *out_count = (uint32_t)count;

    NMO_RETURN_OK();
}

static void nmo_curve_check_point_refs(
    nmo_ref_t *refs,
    uint32_t count,
    void *context)
{
    const nmo_object_repository_t *repository =
        nmo_deserialize_context_get_repository(context);
    const nmo_type_registry_t *types =
        nmo_deserialize_context_get_type_registry(context);
    for (uint32_t i = 0; i < count; ++i) {
        nmo_ref_check_class(&refs[i], repository, types, NMO_CID_CURVEPOINT);
    }
}

static const nmo_type_field_t nmo_curve_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_curve_state_t, base),
                    sizeof(nmo_3dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_curve_state_t, has_curve_data, CKPGUID_UINT8),
    NMO_FIELD(nmo_curve_state_t, control_point_count, CKPGUID_UINT32),
    NMO_FIELD_REF_RECORD_ARRAY_COUNTED(
        nmo_curve_state_t, control_point_ids, control_point_count),
    NMO_FIELD(nmo_curve_state_t, fitting_coeff, CKPGUID_FLOAT),
    NMO_FIELD(nmo_curve_state_t, step_count, CKPGUID_UINT32),
    NMO_FIELD(nmo_curve_state_t, opened, CKPGUID_UINT32),
    NMO_FIELD(nmo_curve_state_t, sub_point_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_curve_state_t, sub_points, sub_point_count, 1, NMO_GUID_STRUCT_CKCURVEPOINTSUBCHUNK),
    NMO_FIELD(nmo_curve_state_t, has_curveonly_chunk, CKPGUID_UINT8),
    NMO_FIELD(nmo_curve_state_t, has_controlpoints_chunk, CKPGUID_UINT8),
    NMO_FIELD(nmo_curve_state_t, has_fitting_chunk, CKPGUID_UINT8),
    NMO_FIELD(nmo_curve_state_t, has_steps_chunk, CKPGUID_UINT8),
    NMO_FIELD(nmo_curve_state_t, has_open_chunk, CKPGUID_UINT8),
    NMO_FIELD(nmo_curve_state_t, has_savepoints_chunk, CKPGUID_UINT8),
    NMO_FIELD(nmo_curve_state_t, savepoints_in_file, CKPGUID_UINT8)
};

static const nmo_type_field_t nmo_curvepoint_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_curvepoint_state_t, base),
                    sizeof(nmo_3dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_curvepoint_state_t, has_default_data, CKPGUID_UINT8),
    NMO_FIELD_REF_VALUE(nmo_curvepoint_state_t, curve),
    NMO_FIELD(nmo_curvepoint_state_t, tangent_mode, CKPGUID_INT),
    NMO_FIELD(nmo_curvepoint_state_t, linear, CKPGUID_INT),
    NMO_FIELD(nmo_curvepoint_state_t, tension, CKPGUID_FLOAT),
    NMO_FIELD(nmo_curvepoint_state_t, continuity, CKPGUID_FLOAT),
    NMO_FIELD(nmo_curvepoint_state_t, bias, CKPGUID_FLOAT),
    NMO_FIELD(nmo_curvepoint_state_t, tangent_in, CKPGUID_VECTOR),
    NMO_FIELD(nmo_curvepoint_state_t, tangent_out, CKPGUID_VECTOR),
    NMO_FIELD(nmo_curvepoint_state_t, has_reserved_vector, CKPGUID_UINT8),
    NMO_FIELD(nmo_curvepoint_state_t, reserved_vector, CKPGUID_VECTOR),
    NMO_FIELD(nmo_curvepoint_state_t, defaultdata_is_modern, CKPGUID_UINT8),
    NMO_FIELD(nmo_curvepoint_state_t, has_tcb_chunk, CKPGUID_UINT8),
    NMO_FIELD(nmo_curvepoint_state_t, has_tangents_chunk, CKPGUID_UINT8),
    NMO_FIELD(nmo_curvepoint_state_t, has_legacy_position, CKPGUID_UINT8),
    NMO_FIELD(nmo_curvepoint_state_t, legacy_position, CKPGUID_VECTOR)
};

static nmo_status_t nmo_curve_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);
static nmo_status_t nmo_curvepoint_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static const nmo_object_state_member_t nmo_curve_sub_point_members[] = {
    NMO_STATE_VALUE(nmo_curve_point_subchunk_t, ref),
    NMO_STATE_CHUNK(nmo_curve_point_subchunk_t, chunk)
};

static const nmo_object_state_layout_t nmo_curve_sub_point_layout = {
    .size = sizeof(nmo_curve_point_subchunk_t),
    .members = nmo_curve_sub_point_members,
    .member_count = sizeof(nmo_curve_sub_point_members) /
        sizeof(nmo_curve_sub_point_members[0]),
};

static const nmo_object_state_member_t nmo_curve_members[] = {
    NMO_STATE_VALUE(nmo_curve_state_t, has_curve_data),
    NMO_STATE_VALUE(nmo_curve_state_t, control_point_count),
    NMO_STATE_COUNTED(nmo_curve_state_t, control_point_ids, control_point_count,
                      nmo_ref_t),
    NMO_STATE_VALUE(nmo_curve_state_t, fitting_coeff),
    NMO_STATE_VALUE(nmo_curve_state_t, step_count),
    NMO_STATE_VALUE(nmo_curve_state_t, opened),
    NMO_STATE_VALUE(nmo_curve_state_t, sub_point_count),
    NMO_STATE_COUNTED_RECORDS(nmo_curve_state_t, sub_points, sub_point_count,
                              nmo_curve_sub_point_layout),
    NMO_STATE_VALUE(nmo_curve_state_t, has_curveonly_chunk),
    NMO_STATE_VALUE(nmo_curve_state_t, has_controlpoints_chunk),
    NMO_STATE_VALUE(nmo_curve_state_t, has_fitting_chunk),
    NMO_STATE_VALUE(nmo_curve_state_t, has_steps_chunk),
    NMO_STATE_VALUE(nmo_curve_state_t, has_open_chunk),
    NMO_STATE_VALUE(nmo_curve_state_t, has_savepoints_chunk),
    NMO_STATE_VALUE(nmo_curve_state_t, savepoints_in_file)
};

static const nmo_object_state_layout_t nmo_curve_layout = {
    .size = sizeof(nmo_curve_state_t),
    .base_vtable = &nmo_3dentity_vtable,
    .members = nmo_curve_members,
    .member_count = sizeof(nmo_curve_members) / sizeof(nmo_curve_members[0]),
    .set_defaults = nmo_curve_set_defaults,
    .validate = nmo_curve_validate,
};

NMO_DEFINE_OBJECT_LAYOUT_LIFECYCLE(curve, nmo_curve_layout)
NMO_DEFINE_OBJECT_LAYOUT_COPY(curve, nmo_curve_layout)

static const nmo_object_state_member_t nmo_curvepoint_members[] = {
    NMO_STATE_VALUE(nmo_curvepoint_state_t, has_default_data),
    NMO_STATE_VALUE(nmo_curvepoint_state_t, defaultdata_is_modern),
    NMO_STATE_VALUE(nmo_curvepoint_state_t, curve),
    NMO_STATE_VALUE(nmo_curvepoint_state_t, tangent_mode),
    NMO_STATE_VALUE(nmo_curvepoint_state_t, linear),
    NMO_STATE_VALUE(nmo_curvepoint_state_t, tension),
    NMO_STATE_VALUE(nmo_curvepoint_state_t, continuity),
    NMO_STATE_VALUE(nmo_curvepoint_state_t, bias),
    NMO_STATE_VALUE(nmo_curvepoint_state_t, tangent_in),
    NMO_STATE_VALUE(nmo_curvepoint_state_t, tangent_out),
    NMO_STATE_VALUE(nmo_curvepoint_state_t, has_reserved_vector),
    NMO_STATE_VALUE(nmo_curvepoint_state_t, reserved_vector),
    NMO_STATE_VALUE(nmo_curvepoint_state_t, has_tcb_chunk),
    NMO_STATE_VALUE(nmo_curvepoint_state_t, has_tangents_chunk),
    NMO_STATE_VALUE(nmo_curvepoint_state_t, has_legacy_position),
    NMO_STATE_VALUE(nmo_curvepoint_state_t, legacy_position)
};

static const nmo_object_state_layout_t nmo_curvepoint_layout = {
    .size = sizeof(nmo_curvepoint_state_t),
    .base_vtable = &nmo_3dentity_vtable,
    .members = nmo_curvepoint_members,
    .member_count = sizeof(nmo_curvepoint_members) /
        sizeof(nmo_curvepoint_members[0]),
    .set_defaults = nmo_curvepoint_set_defaults,
    .validate = nmo_curvepoint_validate,
};

NMO_DEFINE_OBJECT_LAYOUT_LIFECYCLE(curvepoint, nmo_curvepoint_layout)
NMO_DEFINE_OBJECT_LAYOUT_COPY(curvepoint, nmo_curvepoint_layout)

static nmo_status_t nmo_curve_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_curve_state_t *s = instance;
    if (s == NULL) return NMO_ERR_INVALID_ARGUMENT;
    NMO_RETURN_IF_ERROR(nmo_3dentity_vtable.validate(
        &s->base, NULL, context));
    NMO_VALIDATE_COUNT(s->control_point_ids, s->control_point_count, "control_point_ids");
    NMO_VALIDATE_COUNT(s->sub_points, s->sub_point_count, "sub_points");
    if (s->control_point_count > (uint32_t)INT32_MAX) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    NMO_RETURN_OK();
}

NMO_DEFINE_OBJECT_VALIDATE_BASE(nmo_curvepoint, nmo_curvepoint_state_t, base, nmo_3dentity_vtable)

static nmo_status_t nmo_curve_enumerate_refs(
    const void *instance,
    const nmo_type_descriptor_t *type,
    nmo_type_ref_visitor_fn visitor,
    void *user_data)
{
    (void)type;
    const nmo_curve_state_t *state = instance;
    if (!state || !visitor) return NMO_OK;
    NMO_RETURN_IF_ERROR(nmo_curve_validate(state, NULL, NULL));

    for (uint32_t i = 0; i < state->control_point_count; ++i) {
        const nmo_object_id_t id = nmo_ref_runtime_id(
            &state->control_point_ids[i]);
        if (id != NMO_OBJECT_ID_NONE &&
            !visitor(user_data, id, 0, "control_point_ids", i)) {
            return NMO_OK;
        }
    }
    for (uint32_t i = 0; i < state->sub_point_count; ++i) {
        const nmo_object_id_t id = nmo_ref_runtime_id(
            &state->sub_points[i].ref);
        if (id != NMO_OBJECT_ID_NONE &&
            !visitor(user_data, id, 0, "sub_points.ref", i)) {
            return NMO_OK;
        }
    }
    return NMO_OK;
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_PREPARE_VIA_VALIDATE(nmo_curve)

nmo_status_t nmo_curve_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_curve_remap_dependencies");
    }

    nmo_curve_state_t *state = (nmo_curve_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_3dentity_remap_dependencies(&state->base, NULL, context));

    if (state->control_point_count > 0 && state->control_point_ids == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Curve control_point_ids missing");
    }

    if (state->sub_point_count > 0 && state->sub_points == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Curve sub_points missing");
    }

    /* Dependency resolution must not rewrite authored curve topology. */
    return nmo_curve_validate(state, NULL, NULL);
}

NMO_DEFINE_OBJECT_PREPARE_VIA_VALIDATE(nmo_curvepoint)

nmo_status_t nmo_curvepoint_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_curvepoint_remap_dependencies");
    }

    nmo_curvepoint_state_t *state = (nmo_curvepoint_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_3dentity_remap_dependencies(&state->base, NULL, context));

    /* Preserve optional-section flags and unresolved curve reference. */
    return nmo_curvepoint_validate(state, NULL, NULL);
}

static const nmo_object_serialize_pass_t nmo_curve_compare_pass = {
    .class_id = NMO_CID_CURVE,
    .data_version = 7,
    .serialize_flags = NMO_SERIALIZE_FLAG_FILE_MODE,
    .save_flags = UINT32_MAX,
    .use_context = 1,
};

static const nmo_object_serialize_pass_t nmo_curvepoint_compare_pass = {
    .class_id = NMO_CID_CURVEPOINT,
    .data_version = 7,
    .serialize_flags = NMO_SERIALIZE_FLAG_FILE_MODE,
    .save_flags = UINT32_MAX,
    .use_context = 1,
};

static bool nmo_curve_equals(const void *a, const void *b)
{
    return nmo_object_serialized_state_equals(
        a, b, nmo_curve_serialize, &nmo_curve_compare_pass, 1, 8192);
}

static uint32_t nmo_curve_hash(const void *instance)
{
    return nmo_object_serialized_state_hash(
        instance, nmo_curve_serialize,
        &nmo_curve_compare_pass, 1, 8192);
}

static bool nmo_curvepoint_equals(const void *a, const void *b)
{
    return nmo_object_serialized_state_equals(
        a, b, nmo_curvepoint_serialize,
        &nmo_curvepoint_compare_pass, 1, 8192);
}

static uint32_t nmo_curvepoint_hash(const void *instance)
{
    return nmo_object_serialized_state_hash(
        instance, nmo_curvepoint_serialize,
        &nmo_curvepoint_compare_pass, 1, 8192);
}

nmo_type_vtable_t nmo_curve_vtable = {
    .prepare_dependencies = nmo_curve_prepare_dependencies,
    .remap_dependencies = nmo_curve_remap_dependencies,
    .pre_delete = nmo_object_pre_delete_checked,
    .post_delete = nmo_object_post_delete_noop,
    NMO_OBJECT_VTABLE_EX(
        nmo_curve_create,
        nmo_curve_destroy,
        nmo_curve_serialize,
        nmo_curve_deserialize,
        nmo_curve_copy,
        nmo_curve_validate,
        nmo_curve_equals,
        nmo_curve_hash,
        nmo_curve_enumerate_refs)
};

nmo_type_vtable_t nmo_curvepoint_vtable = {
    .prepare_dependencies = nmo_curvepoint_prepare_dependencies,
    .remap_dependencies = nmo_curvepoint_remap_dependencies,
    .pre_delete = nmo_object_pre_delete_checked,
    .post_delete = nmo_object_post_delete_noop,
    NMO_OBJECT_VTABLE(
        nmo_curvepoint_create,
        nmo_curvepoint_destroy,
        nmo_curvepoint_serialize,
        nmo_curvepoint_deserialize,
        nmo_curvepoint_copy,
        nmo_curvepoint_validate,
        nmo_curvepoint_equals,
        nmo_curvepoint_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_curve_type,
    CKPGUID_CURVE,
    "CKCurve",
    NMO_CID_CURVE,
    CKPGUID_3DENTITY,
    nmo_curve_state_t,
    &nmo_curve_vtable,
    nmo_curve_fields)

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_curvepoint_type,
    CKPGUID_CURVEPOINT,
    "CKCurvePoint",
    NMO_CID_CURVEPOINT,
    CKPGUID_3DENTITY,
    nmo_curvepoint_state_t,
    &nmo_curvepoint_vtable,
    nmo_curvepoint_fields)

static nmo_status_t write_object_sequence(
    nmo_chunk_t *chunk,
    const nmo_ref_t *ids,
    uint32_t count)
{
    nmo_status_t result = nmo_chunk_write_object_sequence_start(chunk, count);
    if (result != NMO_OK) return result;

    for (uint32_t i = 0; i < count; ++i) {
        result = nmo_ref_write_sequence_item(chunk, &ids[i]);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_curve_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_curve_state_t *out_state)
{
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_curve_deserialize");
    }

    nmo_status_t result = nmo_3dentity_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    out_state->has_curve_data = 0;
    out_state->control_point_count = 0;
    out_state->control_point_ids = NULL;
    out_state->fitting_coeff = 0.0f;
    /* RCKCurve::Load assigns these only from a section that is present; the
       constructor values (100 steps, open) stay otherwise. */
    out_state->step_count = 100;
    out_state->opened = 1;
    out_state->sub_point_count = 0;
    out_state->sub_points = NULL;
    out_state->has_curveonly_chunk = 0;
    out_state->has_controlpoints_chunk = 0;
    out_state->has_fitting_chunk = 0;
    out_state->has_steps_chunk = 0;
    out_state->has_open_chunk = 0;
    out_state->has_savepoints_chunk = 0;
    out_state->savepoints_in_file = 0;

    uint32_t data_version = nmo_chunk_get_data_version(chunk);
    const bool is_file = nmo_object_deserialize_is_file(chunk, context);

    if (data_version < 5) {
        size_t control_points_section_dwords = 0;
        result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CURVECONTROLPOINT,
            &control_points_section_dwords);
        if (result == NMO_OK) {
            if (control_points_section_dwords < 1u) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            const size_t control_points_section_end =
                nmo_chunk_get_position(chunk) +
                control_points_section_dwords;
            nmo_ref_t *control_points = NULL;
            uint32_t control_point_count = 0;
            result = read_object_sequence(
                chunk, arena, 0, &control_points, &control_point_count);
            if (result != NMO_OK) return result;
            if (nmo_chunk_get_position(chunk) >
                control_points_section_end) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            nmo_curve_check_point_refs(
                control_points, control_point_count, context);
            out_state->has_curve_data = 1;
            out_state->has_controlpoints_chunk = 1;
            out_state->control_point_ids = control_points;
            out_state->control_point_count = control_point_count;
        } else if (result != NMO_ERR_NOT_FOUND) return result;
        size_t fitting_section_dwords = 0;
        result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CURVEFITCOEFF, &fitting_section_dwords);
        if (result == NMO_OK) {
            if (fitting_section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
            out_state->has_curve_data = 1;
            out_state->has_fitting_chunk = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->fitting_coeff));
        } else if (result != NMO_ERR_NOT_FOUND) return result;
        size_t steps_section_dwords = 0;
        result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CURVESTEPS, &steps_section_dwords);
        if (result == NMO_OK) {
            if (steps_section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
            out_state->has_curve_data = 1;
            out_state->has_steps_chunk = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->step_count));
        } else if (result != NMO_ERR_NOT_FOUND) return result;
        size_t open_section_dwords = 0;
        result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CURVEOPEN, &open_section_dwords);
        if (result == NMO_OK) {
            if (open_section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
            out_state->has_curve_data = 1;
            out_state->has_open_chunk = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->opened));
        } else if (result != NMO_ERR_NOT_FOUND) return result;
    } else {
        size_t curve_section_dwords = 0;
        result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CURVEONLY, &curve_section_dwords);
        if (result == NMO_OK) {
            if (curve_section_dwords < 4u) return NMO_ERR_TRUNCATED_CHUNK;
            const size_t curve_section_end =
                nmo_chunk_get_position(chunk) + curve_section_dwords;
            nmo_ref_t *control_points = NULL;
            uint32_t control_point_count = 0;
            result = read_object_sequence(
                chunk, arena, 3, &control_points, &control_point_count);
            if (result != NMO_OK) return result;
            nmo_curve_check_point_refs(
                control_points, control_point_count, context);
            out_state->has_curve_data = 1;
            out_state->has_curveonly_chunk = 1;
            out_state->control_point_ids = control_points;
            out_state->control_point_count = control_point_count;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->fitting_coeff));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->step_count));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->opened));
            if (nmo_chunk_get_position(chunk) > curve_section_end) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
        } else if (result != NMO_ERR_NOT_FOUND) return result;
    }

    size_t savepoints_section_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_CURVESAVEPOINTS, &savepoints_section_dwords);
    if (result == NMO_OK) {
        if (savepoints_section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t savepoints_section_end =
            nmo_chunk_get_position(chunk) + savepoints_section_dwords;
        out_state->has_savepoints_chunk = 1;
        if (is_file) {
            out_state->savepoints_in_file = 1;
        }
        uint32_t count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &count));
#if SIZE_MAX < UINT64_MAX
        if ((size_t)count > SIZE_MAX / sizeof(nmo_curve_point_subchunk_t)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Curve saved point count overflow");
        }
#endif
        if ((size_t)count > nmo_chunk_identifier_remaining_dwords(chunk) / 2u) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Curve saved point count exceeds identifier payload");
        }
        if (count > 0) {
            nmo_curve_point_subchunk_t *sub_points =
                (nmo_curve_point_subchunk_t *)nmo_arena_alloc(
                arena, sizeof(nmo_curve_point_subchunk_t) * count,
                _Alignof(nmo_curve_point_subchunk_t));
            if (!sub_points) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate curve subchunk array");
            }
            memset(sub_points, 0,
                   sizeof(nmo_curve_point_subchunk_t) * count);
            out_state->sub_points = sub_points;
            out_state->sub_point_count = count;

            for (uint32_t i = 0; i < count; ++i) {
                sub_points[i].ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
                NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &sub_points[i].ref));
                NMO_RETURN_IF_ERROR(nmo_chunk_read_sub_chunk(
                    chunk, &sub_points[i].chunk));
                nmo_curve_check_point_refs(&sub_points[i].ref, 1, context);
            }
        }
        if (nmo_chunk_get_position(chunk) > savepoints_section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    NMO_RETURN_OK();
}

static nmo_status_t nmo_curve_serialize_internal(
    const nmo_curve_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_curve_serialize");
    }

    nmo_status_t result = nmo_3dentity_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    const bool is_file = nmo_object_serialize_is_file(out_chunk, context);
    if (!is_file) {
        uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
        if ((save_flags & CK_STATESAVE_CURVEONLY) == 0) {
            return NMO_OK;
        }
    }

    if (in_state->control_point_count > 0 && in_state->control_point_ids == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing control point IDs for CKCurve");
    }

    if (in_state->has_curve_data) {
        const bool has_legacy_ids = in_state->has_controlpoints_chunk ||
            in_state->has_fitting_chunk || in_state->has_steps_chunk ||
            in_state->has_open_chunk;
        const bool modern_layout =
            nmo_chunk_get_data_version(out_chunk) >= 5u;

        if (modern_layout) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CURVEONLY);
            if (result != NMO_OK) return result;
            result = write_object_sequence(out_chunk,
                               in_state->control_point_ids,
                               in_state->control_point_count);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_float(out_chunk, in_state->fitting_coeff);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->step_count);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->opened);
            if (result != NMO_OK) return result;
        } else {
            const bool convert_modern_layout =
                in_state->has_curveonly_chunk || !has_legacy_ids;
            if (in_state->has_controlpoints_chunk ||
                convert_modern_layout) {
                result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CURVECONTROLPOINT);
                if (result != NMO_OK) return result;
                result = write_object_sequence(out_chunk,
                                   in_state->control_point_ids,
                                   in_state->control_point_count);
                if (result != NMO_OK) return result;
            }
            if (in_state->has_fitting_chunk || convert_modern_layout) {
                result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CURVEFITCOEFF);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_float(out_chunk, in_state->fitting_coeff);
                if (result != NMO_OK) return result;
            }
            if (in_state->has_steps_chunk || convert_modern_layout) {
                result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CURVESTEPS);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_dword(out_chunk, in_state->step_count);
                if (result != NMO_OK) return result;
            }
            if (in_state->has_open_chunk || convert_modern_layout) {
                result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CURVEOPEN);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_dword(out_chunk, in_state->opened);
                if (result != NMO_OK) return result;
            }
        }
    }

    if (in_state->sub_point_count > 0 && in_state->sub_points == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing curve sub-point data");
    }

    if (in_state->has_savepoints_chunk && (!is_file || in_state->savepoints_in_file)) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CURVESAVEPOINTS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->sub_point_count);
        if (result != NMO_OK) return result;
        for (uint32_t i = 0; i < in_state->sub_point_count; ++i) {
            nmo_chunk_t *sub = in_state->sub_points[i].chunk;
            if (!sub) {
                sub = nmo_chunk_create(arena);
            }
            result = nmo_ref_write(out_chunk, &in_state->sub_points[i].ref);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_sub_chunk(out_chunk, sub);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_curvepoint_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_curvepoint_state_t *out_state)
{
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_curvepoint_deserialize");
    }

    nmo_status_t result = nmo_3dentity_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    uint32_t data_version = nmo_chunk_get_data_version(chunk);

    out_state->has_default_data = 0;
    out_state->defaultdata_is_modern = (data_version >= 5);
    out_state->curve = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->tangent_mode = 0;
    out_state->linear = 0;
    out_state->tension = 0.0f;
    out_state->continuity = 0.0f;
    out_state->bias = 0.0f;
    out_state->tangent_in = (nmo_vector_t){0.0f, 0.0f, 0.0f};
    out_state->tangent_out = (nmo_vector_t){0.0f, 0.0f, 0.0f};
    out_state->has_reserved_vector = 0;
    out_state->reserved_vector = (nmo_vector_t){0.0f, 0.0f, 0.0f};
    out_state->has_tcb_chunk = 0;
    out_state->has_tangents_chunk = 0;
    out_state->has_legacy_position = 0;
    out_state->legacy_position = (nmo_vector_t){0.0f, 0.0f, 0.0f};

    if (data_version < 5) {
        size_t default_section_dwords = 0;
        result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CURVEPOINTDEFAULTDATA,
            &default_section_dwords);
        if (result == NMO_OK) {
            if (default_section_dwords < 6u) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            nmo_ref_t curve = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
            int32_t tangent_mode = 0;
            int32_t linear = 0;
            nmo_vector_t legacy_position = {0.0f, 0.0f, 0.0f};
            NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &curve));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &tangent_mode));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &linear));

            /* Legacy format includes position (3 floats) - consume it */
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &legacy_position.x));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &legacy_position.y));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &legacy_position.z));
            nmo_ref_check_class(
                &curve,
                (const nmo_object_repository_t *)
                    nmo_deserialize_context_get_repository(context),
                nmo_deserialize_context_get_type_registry(context),
                NMO_CID_CURVE);
            out_state->has_default_data = 1;
            out_state->defaultdata_is_modern = 0;
            out_state->curve = curve;
            out_state->tangent_mode = tangent_mode;
            out_state->linear = linear;
            out_state->legacy_position = legacy_position;
            out_state->has_legacy_position = 1;
            /* RCKCurvePoint::Load calls SetPosition with it after the entity
               data; nmo_curvepoint_get_position reports it, the entity matrix
               keeps what the file holds. */
        } else if (result != NMO_ERR_NOT_FOUND) return result;

        size_t tcb_section_dwords = 0;
        result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CURVEPOINTTCB, &tcb_section_dwords);
        if (result == NMO_OK) {
            if (tcb_section_dwords < 3u) return NMO_ERR_TRUNCATED_CHUNK;
            out_state->has_tcb_chunk = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->tension));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->continuity));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->bias));
        } else if (result != NMO_ERR_NOT_FOUND) return result;

        size_t curve_position_section_dwords = 0;
        result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CURVEPOINTCURVEPOS,
            &curve_position_section_dwords);
        if (result == NMO_OK) {
            if (curve_position_section_dwords < 3u) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            out_state->has_reserved_vector = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_vector3(chunk, &out_state->reserved_vector));
        } else if (result != NMO_ERR_NOT_FOUND) return result;

        size_t tangents_section_dwords = 0;
        result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CURVEPOINTTANGENTS,
            &tangents_section_dwords);
        if (result == NMO_OK) {
            if (tangents_section_dwords < 6u) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            out_state->has_tangents_chunk = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_vector3(chunk, &out_state->tangent_in));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_vector3(chunk, &out_state->tangent_out));
        } else if (result != NMO_ERR_NOT_FOUND) return result;
    } else {
        size_t default_section_dwords = 0;
        result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CURVEPOINTDEFAULTDATA,
            &default_section_dwords);
        if (result == NMO_OK) {
            if (default_section_dwords < 12u) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            nmo_ref_t curve = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
            int32_t tangent_mode = 0;
            int32_t linear = 0;
            float tension = 0.0f;
            float continuity = 0.0f;
            float bias = 0.0f;
            nmo_vector_t tangent_in = {0.0f, 0.0f, 0.0f};
            nmo_vector_t tangent_out = {0.0f, 0.0f, 0.0f};
            NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &curve));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &tangent_mode));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &linear));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &tension));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &continuity));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &bias));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_vector3(chunk, &tangent_in));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_vector3(chunk, &tangent_out));
            nmo_ref_check_class(
                &curve,
                (const nmo_object_repository_t *)
                    nmo_deserialize_context_get_repository(context),
                nmo_deserialize_context_get_type_registry(context),
                NMO_CID_CURVE);
            out_state->has_default_data = 1;
            out_state->defaultdata_is_modern = 1;
            out_state->curve = curve;
            out_state->tangent_mode = tangent_mode;
            out_state->linear = linear;
            out_state->tension = tension;
            out_state->continuity = continuity;
            out_state->bias = bias;
            out_state->tangent_in = tangent_in;
            out_state->tangent_out = tangent_out;
        } else if (result != NMO_ERR_NOT_FOUND) return result;

        size_t tcb_section_dwords = 0;
        result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CURVEPOINTTCB, &tcb_section_dwords);
        if (result == NMO_OK) {
            if (tcb_section_dwords < 3u) return NMO_ERR_TRUNCATED_CHUNK;
            out_state->has_tcb_chunk = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->tension));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->continuity));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->bias));
        } else if (result != NMO_ERR_NOT_FOUND) return result;

        size_t curve_position_section_dwords = 0;
        result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CURVEPOINTCURVEPOS,
            &curve_position_section_dwords);
        if (result == NMO_OK) {
            if (curve_position_section_dwords < 3u) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            out_state->has_reserved_vector = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_vector3(chunk, &out_state->reserved_vector));
        } else if (result != NMO_ERR_NOT_FOUND) return result;

        size_t tangents_section_dwords = 0;
        result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CURVEPOINTTANGENTS,
            &tangents_section_dwords);
        if (result == NMO_OK) {
            if (tangents_section_dwords < 6u) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            out_state->has_tangents_chunk = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_vector3(chunk, &out_state->tangent_in));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_vector3(chunk, &out_state->tangent_out));
        } else if (result != NMO_ERR_NOT_FOUND) return result;
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_curvepoint_serialize_internal(
    const nmo_curvepoint_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_curvepoint_serialize");
    }

    nmo_status_t result = nmo_3dentity_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    const bool is_file = nmo_object_serialize_is_file(out_chunk, context);
    if (!is_file) {
        uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
        if ((save_flags & CK_STATESAVE_CURVEONLY) == 0) {
            return NMO_OK;
        }
    }

    const bool modern_layout =
        nmo_chunk_get_data_version(out_chunk) >= 5u;
    if (in_state->has_default_data) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CURVEPOINTDEFAULTDATA);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->curve);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->tangent_mode);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->linear);
        if (result != NMO_OK) return result;

        if (modern_layout) {
            result = nmo_chunk_write_float(out_chunk, in_state->tension);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_float(out_chunk, in_state->continuity);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_float(out_chunk, in_state->bias);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_vector3(out_chunk, &in_state->tangent_in);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_vector3(out_chunk, &in_state->tangent_out);
            if (result != NMO_OK) return result;
        } else {
            /* The position the point has now. */
            nmo_vector_t pos;
            nmo_curvepoint_get_position(in_state, &pos);
            result = nmo_chunk_write_vector3(out_chunk, &pos);
            if (result != NMO_OK) return result;
        }
    }

    const bool convert_modern_default =
        !modern_layout && in_state->has_default_data &&
        in_state->defaultdata_is_modern;
    if (in_state->has_tcb_chunk || convert_modern_default) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CURVEPOINTTCB);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->tension);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->continuity);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->bias);
        if (result != NMO_OK) return result;
    }

    if (in_state->has_tangents_chunk || convert_modern_default) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CURVEPOINTTANGENTS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &in_state->tangent_in);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &in_state->tangent_out);
        if (result != NMO_OK) return result;
    }

    if (in_state->has_reserved_vector) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CURVEPOINTCURVEPOS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &in_state->reserved_vector);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_curve_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_curve_state_t *out_state = (nmo_curve_state_t *)instance;
    if (!out_state || !chunk) return NMO_ERR_INVALID_ARGUMENT;
    nmo_curve_state_t decoded;
    nmo_status_t result = nmo_curve_create(&decoded, NULL, context);
    if (result != NMO_OK) return result;
    result = nmo_curve_deserialize_internal(chunk, context, &decoded);
    if (result != NMO_OK) {
        nmo_curve_destroy(&decoded, NULL, context);
        return result;
    }
    nmo_curve_destroy(out_state, NULL, context);
    *out_state = decoded;
    return NMO_OK;
}

NMO_DEFINE_OBJECT_STAGED_SERIALIZE_STATE(nmo_curve, nmo_curve_state_t, nmo_curve_validate)

nmo_status_t nmo_curvepoint_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_curvepoint_state_t *out_state = (nmo_curvepoint_state_t *)instance;
    if (!out_state || !chunk) return NMO_ERR_INVALID_ARGUMENT;
    nmo_curvepoint_state_t decoded;
    nmo_status_t result = nmo_curvepoint_create(&decoded, NULL, context);
    if (result != NMO_OK) return result;
    result = nmo_curvepoint_deserialize_internal(chunk, context, &decoded);
    if (result != NMO_OK) {
        nmo_curvepoint_destroy(&decoded, NULL, context);
        return result;
    }
    nmo_curvepoint_destroy(out_state, NULL, context);
    *out_state = decoded;
    return NMO_OK;
}

NMO_DEFINE_OBJECT_STAGED_SERIALIZE_STATE(nmo_curvepoint, nmo_curvepoint_state_t, nmo_curvepoint_validate)
