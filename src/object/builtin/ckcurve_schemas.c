/**
 * @file ckcurve_schemas.c
 * @brief CKCurve and CKCurvePoint schema implementation
 */

#include "object/nmo_ckcurve_schemas.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

#define CK_STATESAVE_CURVEFITCOEFF         0x00400000u
#define CK_STATESAVE_CURVECONTROLPOINT     0x00800000u
#define CK_STATESAVE_CURVESTEPS            0x01000000u
#define CK_STATESAVE_CURVEOPEN             0x02000000u
#define CK_STATESAVE_CURVEPOINTDEFAULTDATA 0x10000000u
#define CK_STATESAVE_CURVEPOINTTCB         0x20000000u
#define CK_STATESAVE_CURVEPOINTTANGENTS    0x40000000u
#define CK_STATESAVE_CURVEPOINTCURVEPOS    0x80000000u
#define CK_STATESAVE_CURVESAVEPOINTS       0xFF000000u
#define CK_STATESAVE_CURVEONLY             0xFFC00000u

static nmo_result_t read_object_sequence(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_object_id_t **out_ids,
    uint32_t *out_count)
{
    size_t count = 0;
    nmo_result_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result.code != NMO_OK) return result;

    if (count == 0) {
        *out_ids = NULL;
        *out_count = 0;
        return nmo_result_ok();
    }

    *out_count = (uint32_t)count;
    *out_ids = (nmo_object_id_t *)nmo_arena_alloc(
        arena, sizeof(nmo_object_id_t) * (*out_count), _Alignof(nmo_object_id_t));
    if (!*out_ids) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
            NMO_SEVERITY_ERROR, "Failed to allocate object ID array"));
    }

    for (uint32_t i = 0; i < *out_count; ++i) {
        result = nmo_chunk_read_object_sequence_item(chunk, &(*out_ids)[i]);
        if (result.code != NMO_OK) {
            *out_count = i;
            break;
        }
    }

    return nmo_result_ok();
}

static nmo_result_t write_object_sequence(
    nmo_chunk_t *chunk,
    const nmo_object_id_t *ids,
    uint32_t count)
{
    nmo_result_t result = nmo_chunk_write_object_sequence_start(chunk, count);
    if (result.code != NMO_OK) return result;

    for (uint32_t i = 0; i < count; ++i) {
        result = nmo_chunk_write_object_sequence_item(chunk, ids[i]);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckcurve_deserialize_internal(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckcurve_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckcurve_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = nmo_ck3dentity_deserialize(chunk, arena, &out_state->base);
    if (result.code != NMO_OK) return result;

    uint32_t data_version = nmo_chunk_get_data_version(chunk);

    if (data_version < 5) {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVECONTROLPOINT).code == NMO_OK) {
            out_state->has_curve_data = 1;
            (void)read_object_sequence(chunk, arena,
                                       &out_state->control_point_ids,
                                       &out_state->control_point_count);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVEFITCOEFF).code == NMO_OK) {
            (void)nmo_chunk_read_float(chunk, &out_state->fitting_coeff);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVESTEPS).code == NMO_OK) {
            (void)nmo_chunk_read_dword(chunk, &out_state->step_count);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVEOPEN).code == NMO_OK) {
            (void)nmo_chunk_read_dword(chunk, &out_state->opened);
        }
    } else {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVEONLY).code == NMO_OK) {
            out_state->has_curve_data = 1;
            (void)read_object_sequence(chunk, arena,
                                       &out_state->control_point_ids,
                                       &out_state->control_point_count);
            (void)nmo_chunk_read_float(chunk, &out_state->fitting_coeff);
            (void)nmo_chunk_read_dword(chunk, &out_state->step_count);
            (void)nmo_chunk_read_dword(chunk, &out_state->opened);
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVESAVEPOINTS).code == NMO_OK) {
        uint32_t count = 0;
        (void)nmo_chunk_read_dword(chunk, &count);
        if (count > 0) {
            out_state->sub_point_count = count;
            out_state->sub_points = (nmo_ckcurve_point_subchunk_t *)nmo_arena_alloc(
                arena, sizeof(nmo_ckcurve_point_subchunk_t) * count,
                _Alignof(nmo_ckcurve_point_subchunk_t));
            if (!out_state->sub_points) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate curve subchunk array"));
            }

            for (uint32_t i = 0; i < count; ++i) {
                (void)nmo_chunk_read_object_id(chunk, &out_state->sub_points[i].point_id);
                (void)nmo_chunk_read_sub_chunk(chunk, &out_state->sub_points[i].chunk);
            }
        }
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckcurve_serialize_internal(
    const nmo_ckcurve_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckcurve_serialize"));
    }

    nmo_result_t result = nmo_ck3dentity_serialize(&in_state->base, out_chunk, arena);
    if (result.code != NMO_OK) return result;

    if (in_state->has_curve_data) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CURVEONLY);
        if (result.code != NMO_OK) return result;
        result = write_object_sequence(out_chunk,
                           in_state->control_point_ids,
                           in_state->control_point_count);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->fitting_coeff);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->step_count);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->opened);
        if (result.code != NMO_OK) return result;
    }

    if (in_state->sub_point_count > 0 && in_state->sub_points) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CURVESAVEPOINTS);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->sub_point_count);
        if (result.code != NMO_OK) return result;
        for (uint32_t i = 0; i < in_state->sub_point_count; ++i) {
            nmo_chunk_t *sub = in_state->sub_points[i].chunk;
            if (!sub) {
                sub = nmo_chunk_create(arena);
            }
            result = nmo_chunk_write_object_id(out_chunk, in_state->sub_points[i].point_id);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_sub_chunk(out_chunk, sub);
            if (result.code != NMO_OK) return result;
        }
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckcurvepoint_deserialize_internal(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckcurvepoint_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckcurvepoint_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = nmo_ck3dentity_deserialize(chunk, arena, &out_state->base);
    if (result.code != NMO_OK) return result;

    uint32_t data_version = nmo_chunk_get_data_version(chunk);

    if (data_version < 5) {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVEPOINTDEFAULTDATA).code == NMO_OK) {
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

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVEPOINTTCB).code == NMO_OK) {
            (void)nmo_chunk_read_float(chunk, &out_state->tension);
            (void)nmo_chunk_read_float(chunk, &out_state->continuity);
            (void)nmo_chunk_read_float(chunk, &out_state->bias);
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVEPOINTCURVEPOS).code == NMO_OK) {
            out_state->has_reserved_vector = 1;
            (void)nmo_chunk_read_vector3(chunk, &out_state->reserved_vector);
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVEPOINTTANGENTS).code == NMO_OK) {
            (void)nmo_chunk_read_vector3(chunk, &out_state->tangent_in);
            (void)nmo_chunk_read_vector3(chunk, &out_state->tangent_out);
        }
    } else {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CURVEPOINTDEFAULTDATA).code == NMO_OK) {
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

    return nmo_result_ok();
}

static nmo_result_t nmo_ckcurvepoint_serialize_internal(
    const nmo_ckcurvepoint_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckcurvepoint_serialize"));
    }

    nmo_result_t result = nmo_ck3dentity_serialize(&in_state->base, out_chunk, arena);
    if (result.code != NMO_OK) return result;

    if (in_state->has_default_data) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CURVEPOINTDEFAULTDATA);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->curve_id);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->use_tcb);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->linear);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->tension);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->continuity);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->bias);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &in_state->tangent_in);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &in_state->tangent_out);
        if (result.code != NMO_OK) return result;
    }

    if (in_state->has_reserved_vector) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CURVEPOINTCURVEPOS);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &in_state->reserved_vector);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckcurve_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type;
    return nmo_ckcurve_deserialize_internal(chunk, arena, (nmo_ckcurve_state_t *)out_ptr);
}

static nmo_result_t nmo_ckcurve_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr,
    nmo_arena_t *arena)
{
    (void)type;
    return nmo_ckcurve_serialize_internal((const nmo_ckcurve_state_t *)in_ptr, chunk, arena);
}

static nmo_result_t nmo_ckcurvepoint_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type;
    return nmo_ckcurvepoint_deserialize_internal(chunk, arena, (nmo_ckcurvepoint_state_t *)out_ptr);
}

static nmo_result_t nmo_ckcurvepoint_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr,
    nmo_arena_t *arena)
{
    (void)type;
    return nmo_ckcurvepoint_serialize_internal((const nmo_ckcurvepoint_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_ckcurve_vtable = {
    .read = nmo_ckcurve_vtable_read,
    .write = nmo_ckcurve_vtable_write,
    .validate = NULL
};

static const nmo_schema_vtable_t nmo_ckcurvepoint_vtable = {
    .read = nmo_ckcurvepoint_vtable_read,
    .write = nmo_ckcurvepoint_vtable_write,
    .validate = NULL
};

nmo_result_t nmo_register_ckcurve_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckcurve_schemas"));
    }

    const nmo_schema_type_t *uint32_type = nmo_schema_registry_find_by_name(registry, "u32");
    const nmo_schema_type_t *float_type = nmo_schema_registry_find_by_name(registry, "f32");
    if (!uint32_type || !float_type) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "Required types not found in registry"));
    }

    nmo_schema_builder_t curve_builder = nmo_builder_struct(arena, "CKCurveState",
                                                            sizeof(nmo_ckcurve_state_t),
                                                            alignof(nmo_ckcurve_state_t));
    nmo_builder_add_field_ex(&curve_builder, "control_point_count", uint32_type,
                            offsetof(nmo_ckcurve_state_t, control_point_count), 0);
    nmo_builder_add_field_ex(&curve_builder, "fitting_coeff", float_type,
                            offsetof(nmo_ckcurve_state_t, fitting_coeff), 0);
    nmo_builder_add_field_ex(&curve_builder, "step_count", uint32_type,
                            offsetof(nmo_ckcurve_state_t, step_count), 0);
    nmo_builder_add_field_ex(&curve_builder, "opened", uint32_type,
                            offsetof(nmo_ckcurve_state_t, opened), 0);
    nmo_builder_set_vtable(&curve_builder, &nmo_ckcurve_vtable);
    nmo_result_t result = nmo_builder_build(&curve_builder, registry);
    if (result.code != NMO_OK) return result;

    nmo_schema_builder_t point_builder = nmo_builder_struct(arena, "CKCurvePointState",
                                                            sizeof(nmo_ckcurvepoint_state_t),
                                                            alignof(nmo_ckcurvepoint_state_t));
    nmo_builder_add_field_ex(&point_builder, "curve_id", uint32_type,
                            offsetof(nmo_ckcurvepoint_state_t, curve_id), 0);
    nmo_builder_add_field_ex(&point_builder, "tension", float_type,
                            offsetof(nmo_ckcurvepoint_state_t, tension), 0);
    nmo_builder_add_field_ex(&point_builder, "continuity", float_type,
                            offsetof(nmo_ckcurvepoint_state_t, continuity), 0);
    nmo_builder_add_field_ex(&point_builder, "bias", float_type,
                            offsetof(nmo_ckcurvepoint_state_t, bias), 0);
    nmo_builder_set_vtable(&point_builder, &nmo_ckcurvepoint_vtable);

    return nmo_builder_build(&point_builder, registry);
}

nmo_result_t nmo_ckcurve_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckcurve_state_t *out_state)
{
    return nmo_ckcurve_deserialize_internal(chunk, arena, out_state);
}

nmo_result_t nmo_ckcurve_serialize(
    const nmo_ckcurve_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    return nmo_ckcurve_serialize_internal(in_state, out_chunk, arena);
}

nmo_result_t nmo_ckcurvepoint_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckcurvepoint_state_t *out_state)
{
    return nmo_ckcurvepoint_deserialize_internal(chunk, arena, out_state);
}

nmo_result_t nmo_ckcurvepoint_serialize(
    const nmo_ckcurvepoint_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    return nmo_ckcurvepoint_serialize_internal(in_state, out_chunk, arena);
}
