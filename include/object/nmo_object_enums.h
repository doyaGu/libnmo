/**
 * @file nmo_object_enums.h
 * @brief Registration of CK2/VxMath enums and flags
 */

#ifndef NMO_OBJECT_ENUMS_H
#define NMO_OBJECT_ENUMS_H

#include "nmo_types.h"
#include "object/nmo_object_enums_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_type_registry nmo_type_registry_t;

/**
 * @brief Register CK2/VxMath enum and flags types
 * @param registry Type registry
 * @return NMO_OK on success, error code otherwise
 */
NMO_API nmo_status_t nmo_register_object_enums(nmo_type_registry_t *registry);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_ENUMS_H */
