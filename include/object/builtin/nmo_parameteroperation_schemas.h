/**
 * @file nmo_parameteroperation_schemas.h
 * @brief CKParameterOperation schema definitions
 */

#ifndef NMO_CKPARAMETEROPERATION_SCHEMAS_H
#define NMO_CKPARAMETEROPERATION_SCHEMAS_H

#include "object/builtin/nmo_object_schemas.h"
#include "object/nmo_object_type_common.h"
#include "nmo_types.h"
#include "core/nmo_guid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/**
 * @brief CKParameterOperation state
 */
typedef struct nmo_parameteroperation_state {
    nmo_object_state_t base;
    nmo_guid_t operation_guid;
    nmo_object_id_t owner_id;
    nmo_object_id_t in1_id;
    nmo_object_id_t in2_id;
    nmo_object_id_t out_id;
    uint8_t has_owner;
    uint8_t has_in1;
    uint8_t has_in2;
    uint8_t has_out;
    nmo_chunk_t *in1_chunk;
    nmo_chunk_t *in2_chunk;
    nmo_chunk_t *out_chunk;
} nmo_parameteroperation_state_t;

NMO_API nmo_status_t nmo_parameteroperation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_parameteroperation_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_parameteroperation_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_parameteroperation_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_parameteroperation_vtable, nmo_register_parameteroperation_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPARAMETEROPERATION_SCHEMAS_H */
