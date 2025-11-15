#ifndef NMO_SHARED_LIBRARY_H
#define NMO_SHARED_LIBRARY_H

/**
 * @file nmo_shared_library.h
 * @brief Cross-platform shared library loading helpers
 */

#include "nmo_types.h"
#include "core/nmo_allocator.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_shared_library nmo_shared_library_t;

/**
 * @brief Open a shared library at the provided path.
 *
 * @param allocator Optional allocator for persistent allocations (defaults to system allocator).
 * @param path UTF-8 path to the library.
 * @param out_library Output pointer that receives the opened library handle.
 * @return NMO_OK on success or error code on failure.
 */
NMO_API nmo_result_t nmo_shared_library_open(
    nmo_allocator_t *allocator,
    const char *path,
    nmo_shared_library_t **out_library);

/**
 * @brief Close a previously opened library and free associated resources.
 */
NMO_API void nmo_shared_library_close(nmo_shared_library_t *library);

/**
 * @brief Lookup a symbol (function or variable) from the library.
 *
 * @param library Library handle returned by @ref nmo_shared_library_open.
 * @param symbol_name Null-terminated symbol name.
 * @param out_symbol Output pointer storing the resolved symbol on success.
 * @return NMO_OK on success or error code on failure.
 */
NMO_API nmo_result_t nmo_shared_library_get_symbol(
    nmo_shared_library_t *library,
    const char *symbol_name,
    void **out_symbol);

/**
 * @brief Returns the path that was used to load the shared library.
 */
NMO_API const char *nmo_shared_library_get_path(const nmo_shared_library_t *library);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SHARED_LIBRARY_H */
