/**
 * @file nmo_type_virtools.h
 * @brief Register CK2 parameter types and operation types from Virtools data
 */

#ifndef NMO_TYPE_VIRTOOLS_H
#define NMO_TYPE_VIRTOOLS_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_type_registry nmo_type_registry_t;

/**
 * @brief Register Virtools CK2 parameter types and operation types.
 *
 * Bulk-registers parameter types and operation types exported from the
 * Virtools runtime. Types already registered by the core are skipped.
 *
 * Called during context initialization.
 *
 * @param registry  Type registry
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_register_virtools_types(nmo_type_registry_t *registry);

#ifdef __cplusplus
}
#endif

#endif /* NMO_TYPE_VIRTOOLS_H */
