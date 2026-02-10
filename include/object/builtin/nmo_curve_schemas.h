/**
 * @file nmo_curve_schemas.h
 * @brief CKCurve and CKCurvePoint schema definitions
 */

#ifndef NMO_CKCURVE_SCHEMAS_H
#define NMO_CKCURVE_SCHEMAS_H

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/nmo_object_struct_defs.h"
#include "object/nmo_object_type_common.h"
#include "core/nmo_math.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/**
 * @brief CKCurve state
 */
typedef struct nmo_curve_state {
    nmo_3dentity_state_t base;

    uint8_t has_curve_data;
    uint32_t control_point_count;
    nmo_object_id_t *control_point_ids;

    float fitting_coeff;
    uint32_t step_count;
    uint32_t opened;

    uint32_t sub_point_count;
    nmo_curve_point_subchunk_t *sub_points;
} nmo_curve_state_t;

/**
 * @brief CKCurvePoint state
 */
typedef struct nmo_curvepoint_state {
    nmo_3dentity_state_t base;

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
} nmo_curvepoint_state_t;

NMO_API nmo_status_t nmo_curve_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_curve_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_curvepoint_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_curvepoint_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_curve_vtable, nmo_register_curve_type)
NMO_DECLARE_OBJECT_SCHEMA(nmo_curvepoint_vtable, nmo_register_curvepoint_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKCURVE_SCHEMAS_H */
