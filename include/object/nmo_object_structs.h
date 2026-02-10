/**
 * @file nmo_object_structs.h
 * @brief Registration for CK/VX struct types used in object schemas
 */

#ifndef NMO_OBJECT_STRUCTS_H
#define NMO_OBJECT_STRUCTS_H

#include "core/nmo_error.h"
#include "object/nmo_object_struct_defs.h"
#include "object/nmo_object_struct_guids.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_type_registry nmo_type_registry_t;

/**
 * @brief Register struct/union types referenced by object schemas
 *
 * Must be called before using reflection on object schema fields.
 */
NMO_API nmo_status_t nmo_register_object_structs(nmo_type_registry_t *registry);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_STRUCTS_H */
