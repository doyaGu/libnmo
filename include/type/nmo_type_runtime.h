/**
 * @file nmo_type_runtime.h
 * @brief Aggregated type runtime view (type registry + operation registry)
 */

#ifndef NMO_TYPE_RUNTIME_H
#define NMO_TYPE_RUNTIME_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations to keep the header lightweight. */
typedef struct nmo_type_registry nmo_type_registry_t;
typedef struct nmo_operation_registry nmo_operation_registry_t;

/*
 * The aggregate runtime object is public for advanced C coordination only.
 * It is not the intended long-lived binding-facing contract.
 */
#define NMO_TYPE_RUNTIME_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_TYPE_RUNTIME_AGGREGATE_API_TIER NMO_API_TIER_ADVANCED_C

/**
 * @brief Aggregated runtime for type-related registries.
 *
 * Ownership is external (typically nmo_context_t). This struct is a borrowed
 * view used to reduce parameter fan-out across upper layers. Ordinary
 * consumers should prefer stable metadata/query facades such as
 * nmo_type_view_*() and scalar type-query helpers instead of holding this
 * aggregate directly.
 */
typedef struct nmo_type_runtime {
    nmo_type_registry_t *types;
    nmo_operation_registry_t *ops;

    /* Optional debug/version snapshots updated by finalize(). */
    uint32_t types_finalized_version;
    uint32_t ops_finalized_version;
} nmo_type_runtime_t;

/**
 * @brief Finalize runtime caches for stable read/query usage.
 *
 * Calls:
 * - nmo_type_registry_finalize(rt->types)
 * - nmo_operation_registry_finalize(rt->ops, rt->types)
 *
 * @param rt Runtime aggregate
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_type_runtime_finalize(nmo_type_runtime_t *rt);

#ifdef __cplusplus
}
#endif

#endif /* NMO_TYPE_RUNTIME_H */
