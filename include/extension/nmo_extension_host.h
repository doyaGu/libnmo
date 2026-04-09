/**
 * @file nmo_extension_host.h
 * @brief Host-side implementation of the extension API
 *
 * Provides the host implementation of the extension host API table.
 * This is internal to libnmo and not used by extensions directly.
 */

#ifndef NMO_EXTENSION_HOST_H
#define NMO_EXTENSION_HOST_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "extension/nmo_extension_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct nmo_extension_registry nmo_extension_registry_t;
typedef struct nmo_extension_host_context nmo_extension_host_context_t;

/* ============================================================================
 * Host Context
 *
 * Internal context passed as host_user to extension callbacks.
 * Contains references needed for registration operations.
 * ============================================================================ */

/**
 * @brief Host context for extension callbacks
 *
 * This is the opaque host_user passed to extension init() callbacks.
 * It contains all the state needed to perform registrations.
 */
typedef struct nmo_extension_host_context {
    /** Parent extension registry */
    nmo_extension_registry_t *registry;

    /** Arena for this plugin's allocations */
    nmo_arena_t *plugin_arena;

    /** Plugin GUID being initialized */
    nmo_guid_t plugin_guid;

    /** Array of registered manager IDs (for rollback) */
    uint32_t *manager_ids;
    size_t manager_id_count;
    size_t manager_id_capacity;

    /** Array of registered type GUIDs (for rollback) */
    nmo_guid_t *type_guids;
    size_t type_guid_count;
    size_t type_guid_capacity;
} nmo_extension_host_context_t;

/* ============================================================================
 * Host API Table
 * ============================================================================ */

/**
 * @brief Get the host API table
 *
 * Returns a pointer to the static host API table. The table is valid
 * for the lifetime of the process.
 *
 * @return Pointer to the host API table
 * @ownership static
 */
NMO_API const nmo_extension_host_t *nmo_extension_host_get_api(void);

/* ============================================================================
 * Host Context Management
 * ============================================================================ */

/**
 * @brief Initialize a host context for a plugin
 *
 * @param ctx Host context to initialize
 * @param registry Parent extension registry
 * @param plugin_arena Arena for this plugin's allocations
 * @param plugin_guid GUID of the plugin being initialized
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_extension_host_context_init(
    nmo_extension_host_context_t *ctx,
    nmo_extension_registry_t *registry,
    nmo_arena_t *plugin_arena,
    nmo_guid_t plugin_guid);

/**
 * @brief Clean up a host context
 *
 * Does not roll back registrations - use rollback functions for that.
 *
 * @param ctx Host context to clean up
 */
NMO_API void nmo_extension_host_context_cleanup(nmo_extension_host_context_t *ctx);

/**
 * @brief Roll back all registrations made through this context
 *
 * Unregisters all types and managers registered during init().
 *
 * @param ctx Host context
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_extension_host_context_rollback(nmo_extension_host_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* NMO_EXTENSION_HOST_H */
