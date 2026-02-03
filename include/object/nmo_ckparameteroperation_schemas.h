/**
 * @file nmo_ckparameteroperation_schemas.h
 * @brief CKParameterOperation schema definitions
 */

#ifndef NMO_CKPARAMETEROPERATION_SCHEMAS_H
#define NMO_CKPARAMETEROPERATION_SCHEMAS_H

#include "object/nmo_ckobject_schemas.h"
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
typedef struct nmo_ckparameteroperation_state {
    nmo_ckobject_state_t base;
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
} nmo_ckparameteroperation_state_t;

NMO_API nmo_status_t nmo_ckparameteroperation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_ckparameteroperation_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_ckparameteroperation_vtable, nmo_register_ckparameteroperation_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPARAMETEROPERATION_SCHEMAS_H */
