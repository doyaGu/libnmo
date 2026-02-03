/**
 * @file nmo_ckrendercontext_schemas.h
 * @brief CKRenderContext schema definitions
 */

#ifndef NMO_CKRENDERCONTEXT_SCHEMAS_H
#define NMO_CKRENDERCONTEXT_SCHEMAS_H

#include "object/nmo_ckobject_schemas.h"
#include "object/nmo_object_type_common.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor_t nmo_type_descriptor_t;

/**
 * @brief CKRenderContext state
 */
typedef struct nmo_ckrendercontext_state {
    nmo_ckobject_state_t base;
} nmo_ckrendercontext_state_t;

NMO_API nmo_status_t nmo_ckrendercontext_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_ckrendercontext_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_ckrendercontext_vtable, nmo_register_ckrendercontext_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKRENDERCONTEXT_SCHEMAS_H */
