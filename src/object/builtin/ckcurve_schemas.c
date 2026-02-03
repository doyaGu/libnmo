/**
 * @file ckcurve_schemas.c
 * @brief CKCurve and CKCurvePoint schema implementation
 */

#include "object/nmo_ckcurve_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_schema_interface.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckcurve, nmo_ckcurve_state_t)
NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckcurvepoint, nmo_ckcurvepoint_state_t)

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

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ckcurve,
    nmo_ckcurve_state_t,
    nmo_ckcurve_serialize,
    nmo_ckcurve_deserialize,
    NMO_GUID_CKCURVE,
    "CKCurve",
    NMO_CID_CURVE,
    NMO_GUID_CK3DENTITY
)

NMO_DEFINE_OBJECT_SCHEMA(
    ckcurvepoint,
    nmo_ckcurvepoint_state_t,
    nmo_ckcurvepoint_serialize,
    nmo_ckcurvepoint_deserialize,
    NMO_GUID_CKCURVEPOINT,
    "CKCurvePoint",
    NMO_CID_CURVEPOINT,
    NMO_GUID_CK3DENTITY
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

static nmo_status_t nmo_ckcurve_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_ckcurve_state_t *out_state)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckcurve_deserialize");
    }

    NMO_RETURN_IF_ERROR(nmo_ckcurve_create(out_state, NULL, context));

    nmo_status_t result = nmo_ck3dentity_deserialize(&out_state->base, chunk, NULL, context);
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
            out_state->sub_points = (nmo_ckcurve_point_subchunk_t *)nmo_arena_alloc(
                arena, sizeof(nmo_ckcurve_point_subchunk_t) * count,
                _Alignof(nmo_ckcurve_point_subchunk_t));
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

static nmo_status_t nmo_ckcurve_serialize_internal(
    const nmo_ckcurve_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckcurve_serialize");
    }

    nmo_status_t result = nmo_ck3dentity_serialize(&in_state->base, out_chunk, NULL, context);
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

static nmo_status_t nmo_ckcurvepoint_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_ckcurvepoint_state_t *out_state)
{
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckcurvepoint_deserialize");
    }

    NMO_RETURN_IF_ERROR(nmo_ckcurvepoint_create(out_state, NULL, context));

    nmo_status_t result = nmo_ck3dentity_deserialize(&out_state->base, chunk, NULL, context);
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

static nmo_status_t nmo_ckcurvepoint_serialize_internal(
    const nmo_ckcurvepoint_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckcurvepoint_serialize");
    }

    nmo_status_t result = nmo_ck3dentity_serialize(&in_state->base, out_chunk, NULL, context);
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

nmo_status_t nmo_ckcurve_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckcurve_state_t *out_state = (nmo_ckcurve_state_t *)instance;
    return nmo_ckcurve_deserialize_internal(chunk, context, out_state);
}

nmo_status_t nmo_ckcurve_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckcurve_state_t *in_state = (const nmo_ckcurve_state_t *)instance;
    return nmo_ckcurve_serialize_internal(in_state, out_chunk, context);
}

nmo_status_t nmo_ckcurvepoint_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckcurvepoint_state_t *out_state = (nmo_ckcurvepoint_state_t *)instance;
    return nmo_ckcurvepoint_deserialize_internal(chunk, context, out_state);
}

nmo_status_t nmo_ckcurvepoint_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckcurvepoint_state_t *in_state = (const nmo_ckcurvepoint_state_t *)instance;
    return nmo_ckcurvepoint_serialize_internal(in_state, out_chunk, context);
}
