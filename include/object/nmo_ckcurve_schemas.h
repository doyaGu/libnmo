/**
 * @file nmo_ckcurve_schemas.h
 * @brief CKCurve and CKCurvePoint schema definitions
 */

#ifndef NMO_CKCURVE_SCHEMAS_H
#define NMO_CKCURVE_SCHEMAS_H

#include "object/nmo_ck3dentity_schemas.h"
#include "core/nmo_math.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;

/**
 * @brief Curve point subchunk entry
 */
typedef struct nmo_ckcurve_point_subchunk {
    nmo_object_id_t point_id;
    nmo_chunk_t *chunk;
} nmo_ckcurve_point_subchunk_t;

/**
 * @brief CKCurve state
 */
typedef struct nmo_ckcurve_state {
    nmo_ck3dentity_state_t base;

    uint8_t has_curve_data;
    uint32_t control_point_count;
    nmo_object_id_t *control_point_ids;

    float fitting_coeff;
    uint32_t step_count;
    uint32_t opened;

    uint32_t sub_point_count;
    nmo_ckcurve_point_subchunk_t *sub_points;
} nmo_ckcurve_state_t;

/**
 * @brief CKCurvePoint state
 */
typedef struct nmo_ckcurvepoint_state {
    nmo_ck3dentity_state_t base;

    uint8_t has_default_data;
    nmo_object_id_t curve_id;
    int32_t use_tcb;
    int32_t linear;
    float tension;
    float continuity;
    float bias;
    nmo_vector_t tangent_in;
    nmo_vector_t tangent_out;

    uint8_t has_reserved_vector;
    nmo_vector_t reserved_vector;
} nmo_ckcurvepoint_state_t;

NMO_API nmo_result_t nmo_register_ckcurve_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_ckcurve_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckcurve_state_t *out_state);

NMO_API nmo_result_t nmo_ckcurve_serialize(
    const nmo_ckcurve_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_ckcurvepoint_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckcurvepoint_state_t *out_state);

NMO_API nmo_result_t nmo_ckcurvepoint_serialize(
    const nmo_ckcurvepoint_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKCURVE_SCHEMAS_H */
