/**
 * @file nmo_extension_diagnostics.h
 * @brief Extension diagnostics and dependency resolution
 *
 * Provides diagnostic utilities for querying extension information
 * and resolving dependencies.
 */

#ifndef NMO_EXTENSION_DIAGNOSTICS_H
#define NMO_EXTENSION_DIAGNOSTICS_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "extension/nmo_extension_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct nmo_extension_registry nmo_extension_registry_t;

/* ============================================================================
 * Dependency Resolution
 * ============================================================================ */

/**
 * @brief Dependency resolution result
 */
typedef struct nmo_extension_dependency_result {
    /** Dependency GUID */
    nmo_guid_t guid;

    /** Required minimum version */
    uint32_t required_version;

    /** Actual version found (0 if not found) */
    uint32_t found_version;

    /** Whether the dependency is satisfied */
    int satisfied;
} nmo_extension_dependency_result_t;

/**
 * @brief Check if a dependency is satisfied
 *
 * Looks up the dependency in the registry and checks version requirements.
 *
 * @param registry Extension registry to search
 * @param category Required category
 * @param guid Required GUID
 * @param min_version Minimum required version (0 = any)
 * @param out_result Receives resolution result (optional)
 * @return 1 if satisfied, 0 if not
 */
NMO_API int nmo_extension_check_dependency(
    const nmo_extension_registry_t *registry,
    nmo_plugin_category_t category,
    nmo_guid_t guid,
    uint32_t min_version,
    nmo_extension_dependency_result_t *out_result);

/**
 * @brief Check multiple dependencies
 *
 * @param registry Extension registry to search
 * @param categories Array of required categories
 * @param guids Array of required GUIDs
 * @param min_versions Array of minimum versions (0 = any)
 * @param count Number of dependencies
 * @param out_results Array to receive results (optional, must have count elements)
 * @return Number of unsatisfied dependencies (0 = all satisfied)
 */
NMO_API size_t nmo_extension_check_dependencies(
    const nmo_extension_registry_t *registry,
    const nmo_plugin_category_t *categories,
    const nmo_guid_t *guids,
    const uint32_t *min_versions,
    size_t count,
    nmo_extension_dependency_result_t *out_results);

#ifdef __cplusplus
}
#endif

#endif /* NMO_EXTENSION_DIAGNOSTICS_H */
