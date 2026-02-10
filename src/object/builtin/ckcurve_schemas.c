/**
 * @file ckcurve_schemas.c
 * @brief CKCurve and CKCurvePoint schema implementation
 */

#include "object/nmo_curve_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "type/nmo_reflection.h"
#include "object/nmo_param_guids.h"
#include "object/nmo_object_struct_guids.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(curve, nmo_curve_state_t)
NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(curvepoint, nmo_curvepoint_state_t)

static nmo_status_t read_object_sequence(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_object_id_t **out_ids,
    uint32_t *out_count)
{
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result != NMO_OK) return result;

    if (count == 0) {
        *out_ids = NULL;
        *out_count = 0;
        NMO_RETURN_OK();
    }

    *out_count = (uint32_t)count;
    *out_ids = (nmo_object_id_t *)nmo_arena_alloc(
        arena, sizeof(nmo_object_id_t) * (*out_count), _Alignof(nmo_object_id_t));
    if (!*out_ids) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate object ID array");
    }

    for (uint32_t i = 0; i < *out_count; ++i) {
        result = nmo_chunk_read_object_sequence_item(chunk, &(*out_ids)[i]);
        if (result != NMO_OK) {
            *out_count = i;
            break;
        }
    }

    NMO_RETURN_OK();
}

static const nmo_type_field_t nmo_curve_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_curve_state_t, base),
                    sizeof(nmo_3dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_curve_state_t, has_curve_data, CKPGUID_UINT8),
    NMO_FIELD(nmo_curve_state_t, control_point_count, CKPGUID_UINT32),
    NMO_FIELD_REF_ARRAY(nmo_curve_state_t, control_point_ids),
    NMO_FIELD(nmo_curve_state_t, fitting_coeff, CKPGUID_FLOAT),
    NMO_FIELD(nmo_curve_state_t, step_count, CKPGUID_UINT32),
    NMO_FIELD(nmo_curve_state_t, opened, CKPGUID_UINT32),
    NMO_FIELD(nmo_curve_state_t, sub_point_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY(nmo_curve_state_t, sub_points, NMO_GUID_STRUCT_CKCURVEPOINTSUBCHUNK)
};

static const nmo_type_field_t nmo_curvepoint_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_curvepoint_state_t, base),
                    sizeof(nmo_3dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_curvepoint_state_t, has_default_data, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_curvepoint_state_t, curve_id),
    NMO_FIELD(nmo_curvepoint_state_t, use_tcb, CKPGUID_INT),
    NMO_FIELD(nmo_curvepoint_state_t, linear, CKPGUID_INT),
    NMO_FIELD(nmo_curvepoint_state_t, tension, CKPGUID_FLOAT),
    NMO_FIELD(nmo_curvepoint_state_t, continuity, CKPGUID_FLOAT),
    NMO_FIELD(nmo_curvepoint_state_t, bias, CKPGUID_FLOAT),
    NMO_FIELD(nmo_curvepoint_state_t, tangent_in, CKPGUID_VECTOR),
    NMO_FIELD(nmo_curvepoint_state_t, tangent_out, CKPGUID_VECTOR),
    NMO_FIELD(nmo_curvepoint_state_t, has_reserved_vector, CKPGUID_UINT8),
    NMO_FIELD(nmo_curvepoint_state_t, reserved_vector, CKPGUID_VECTOR)
};

static nmo_status_t nmo_curve_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_curve_state_t *s = src;
    nmo_curve_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->control_point_ids,
                                              s->control_point_ids, sizeof(nmo_object_id_t), s->control_point_count));
    if (s->sub_point_count > 0) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->sub_points,
                                                  s->sub_points, sizeof(nmo_curve_point_subchunk_t),
                                                  s->sub_point_count));
        for (uint32_t i = 0; i < s->sub_point_count; ++i) {
            nmo_chunk_t *clone = NULL;
            NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &clone, s->sub_points[i].chunk));
            d->sub_points[i].chunk = clone;
        }
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_curve_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_curve_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->control_point_ids, s->control_point_count, "control_point_ids");
    NMO_VALIDATE_COUNT(s->sub_points, s->sub_point_count, "sub_points");
    NMO_RETURN_OK();
}

static nmo_status_t nmo_curvepoint_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    return nmo_object_default_copy(src, dst, type, arena);
}

static nmo_status_t nmo_curvepoint_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    curve,
    nmo_curve_state_t,
    nmo_curve_serialize,
    nmo_curve_deserialize,
    nmo_curve_fields,
    CKPGUID_CURVE,
    "CKCurve",
    NMO_CID_CURVE,
    CKPGUID_3DENTITY
)

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    curvepoint,
    nmo_curvepoint_state_t,
    nmo_curvepoint_serialize,
    nmo_curvepoint_deserialize,
    nmo_curvepoint_fields,
    CKPGUID_CURVEPOINT,
    "CKCurvePoint",
    NMO_CID_CURVEPOINT,
    CKPGUID_3DENTITY
)

static nmo_status_t write_object_sequence(
    nmo_chunk_t *chunk,
    const nmo_object_id_t *ids,
    uint32_t count)
{
    nmo_status_t result = nmo_chunk_write_object_sequence_start(chunk, count);
    if (result != NMO_OK) return result;

    for (uint32_t i = 0; i < count; ++i) {
        result = nmo_chunk_write_object_sequence_item(chunk, ids[i]);
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

    uint32_t data_version = nmo_chunk_get_data_version(chunk);

    if (data_version < 5) {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVECONTROLPOINT) == NMO_OK) {
            out_state->has_curve_data = 1;
            (void)read_object_sequence(chunk, arena,
                                       &out_state->control_point_ids,
                                       &out_state->control_point_count);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVEFITCOEFF) == NMO_OK) {
            (void)nmo_chunk_read_float(chunk, &out_state->fitting_coeff);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVESTEPS) == NMO_OK) {
            (void)nmo_chunk_read_dword(chunk, &out_state->step_count);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVEOPEN) == NMO_OK) {
            (void)nmo_chunk_read_dword(chunk, &out_state->opened);
        }
    } else {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVEONLY) == NMO_OK) {
            out_state->has_curve_data = 1;
            (void)read_object_sequence(chunk, arena,
                                       &out_state->control_point_ids,
                                       &out_state->control_point_count);
            (void)nmo_chunk_read_float(chunk, &out_state->fitting_coeff);
            (void)nmo_chunk_read_dword(chunk, &out_state->step_count);
            (void)nmo_chunk_read_dword(chunk, &out_state->opened);
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVESAVEPOINTS) == NMO_OK) {
        uint32_t count = 0;
        (void)nmo_chunk_read_dword(chunk, &count);
        if (count > 0) {
            out_state->sub_point_count = count;
            out_state->sub_points = (nmo_curve_point_subchunk_t *)nmo_arena_alloc(
                arena, sizeof(nmo_curve_point_subchunk_t) * count,
                _Alignof(nmo_curve_point_subchunk_t));
            if (!out_state->sub_points) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate curve subchunk array");
            }

            for (uint32_t i = 0; i < count; ++i) {
                (void)nmo_chunk_read_object_id(chunk, &out_state->sub_points[i].point_id);
                (void)nmo_chunk_read_sub_chunk(chunk, &out_state->sub_points[i].chunk);
            }
        }
    }

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

    if (in_state->has_curve_data) {
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
    }

    if (in_state->sub_point_count > 0 && in_state->sub_points) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CURVESAVEPOINTS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->sub_point_count);
        if (result != NMO_OK) return result;
        for (uint32_t i = 0; i < in_state->sub_point_count; ++i) {
            nmo_chunk_t *sub = in_state->sub_points[i].chunk;
            if (!sub) {
                sub = nmo_chunk_create(arena);
            }
            result = nmo_chunk_write_object_id(out_chunk, in_state->sub_points[i].point_id);
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

    if (data_version < 5) {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVEPOINTDEFAULTDATA) == NMO_OK) {
            out_state->has_default_data = 1;
            (void)nmo_chunk_read_object_id(chunk, &out_state->curve_id);
            (void)nmo_chunk_read_int(chunk, &out_state->use_tcb);
            (void)nmo_chunk_read_int(chunk, &out_state->linear);

            /* Legacy format includes position (3 floats) - consume it */
            float tmp = 0.0f;
            (void)nmo_chunk_read_float(chunk, &tmp);
            (void)nmo_chunk_read_float(chunk, &tmp);
            (void)nmo_chunk_read_float(chunk, &tmp);
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVEPOINTTCB) == NMO_OK) {
            (void)nmo_chunk_read_float(chunk, &out_state->tension);
            (void)nmo_chunk_read_float(chunk, &out_state->continuity);
            (void)nmo_chunk_read_float(chunk, &out_state->bias);
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVEPOINTCURVEPOS) == NMO_OK) {
            out_state->has_reserved_vector = 1;
            (void)nmo_chunk_read_vector3(chunk, &out_state->reserved_vector);
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVEPOINTTANGENTS) == NMO_OK) {
            (void)nmo_chunk_read_vector3(chunk, &out_state->tangent_in);
            (void)nmo_chunk_read_vector3(chunk, &out_state->tangent_out);
        }
    } else {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVEPOINTDEFAULTDATA) == NMO_OK) {
            out_state->has_default_data = 1;
            (void)nmo_chunk_read_object_id(chunk, &out_state->curve_id);
            (void)nmo_chunk_read_int(chunk, &out_state->use_tcb);
            (void)nmo_chunk_read_int(chunk, &out_state->linear);
            (void)nmo_chunk_read_float(chunk, &out_state->tension);
            (void)nmo_chunk_read_float(chunk, &out_state->continuity);
            (void)nmo_chunk_read_float(chunk, &out_state->bias);
            (void)nmo_chunk_read_vector3(chunk, &out_state->tangent_in);
            (void)nmo_chunk_read_vector3(chunk, &out_state->tangent_out);
        }
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

    if (in_state->has_default_data) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CURVEPOINTDEFAULTDATA);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->curve_id);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->use_tcb);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->linear);
        if (result != NMO_OK) return result;
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
    return nmo_curve_deserialize_internal(chunk, context, out_state);
}

nmo_status_t nmo_curve_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_curve_state_t *in_state = (const nmo_curve_state_t *)instance;
    return nmo_curve_serialize_internal(in_state, out_chunk, context);
}

nmo_status_t nmo_curvepoint_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_curvepoint_state_t *out_state = (nmo_curvepoint_state_t *)instance;
    return nmo_curvepoint_deserialize_internal(chunk, context, out_state);
}

nmo_status_t nmo_curvepoint_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_curvepoint_state_t *in_state = (const nmo_curvepoint_state_t *)instance;
    return nmo_curvepoint_serialize_internal(in_state, out_chunk, context);
}

