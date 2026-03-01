/**
 * @file nmo_rendercontext_schemas.h
 * @brief CKRenderContext schema definitions
 */

#ifndef NMO_CKRENDERCONTEXT_SCHEMAS_H
#define NMO_CKRENDERCONTEXT_SCHEMAS_H

#include "object/builtin/nmo_object_schemas.h"
#include "object/nmo_object_type_common.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/**
 * @brief CKRenderContext state
 */
typedef struct nmo_rendercontext_state {
    nmo_object_state_t base;
} nmo_rendercontext_state_t;

NMO_API nmo_status_t nmo_rendercontext_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_rendercontext_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_rendercontext_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository);

NMO_DECLARE_OBJECT_SCHEMA(nmo_rendercontext_vtable, nmo_register_rendercontext_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKRENDERCONTEXT_SCHEMAS_H */
