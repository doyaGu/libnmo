/**
 * @file nmo_extension_loader.h
 * @brief Shared library loading utilities for extensions
 *
 * Provides wrapper utilities for loading extension libraries.
 * Uses the Core layer nmo_shared_library_* functions internally.
 */

#ifndef NMO_EXTENSION_LOADER_H
#define NMO_EXTENSION_LOADER_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_allocator.h"
#include "extension/nmo_extension_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct nmo_shared_library nmo_shared_library_t;

/* ============================================================================
 * Library Loading
 * ============================================================================ */

/**
 * @brief Load an extension library and resolve the query function
 *
 * Opens the library and resolves the extension query symbol.
 *
 * @param allocator Allocator for the library handle (NULL for default)
 * @param path Path to the shared library
 * @param symbol_name Query function symbol (NULL for default)
 * @param out_library Receives the library handle on success
 * @param out_query Receives the query function pointer on success
 * @return NMO_OK on success
 *
 * Errors:
 * - NMO_ERR_INVALID_ARGUMENT: path, out_library, or out_query is NULL
 * - NMO_ERR_CANT_OPEN_FILE: Cannot open library
 * - NMO_ERR_NOT_FOUND: Symbol not found in library
 */
NMO_API nmo_status_t nmo_extension_loader_open(
    nmo_allocator_t *allocator,
    const char *path,
    const char *symbol_name,
    nmo_shared_library_t **out_library,
    nmo_extension_query_fn *out_query);

/**
 * @brief Close an extension library
 *
 * @param library Library handle to close
 */
NMO_API void nmo_extension_loader_close(nmo_shared_library_t *library);

/**
 * @brief Query plugins from a loaded library
 *
 * Calls the query function and validates the returned plugins.
 *
 * @param query_fn Query function from the library
 * @param out_plugins Receives pointer to plugin array
 * @param out_count Receives number of plugins
 * @return NMO_OK on success
 *
 * Errors:
 * - NMO_ERR_INVALID_ARGUMENT: query_fn or output pointers are NULL
 * - NMO_ERR_UNSUPPORTED_VERSION: Plugin ABI version mismatch
 */
NMO_API nmo_status_t nmo_extension_loader_query(
    nmo_extension_query_fn query_fn,
    const nmo_extension_plugin_t **out_plugins,
    size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* NMO_EXTENSION_LOADER_H */
