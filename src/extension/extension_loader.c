/**
 * @file extension_loader.c
 * @brief Extension loader implementation
 */

#include "extension/nmo_extension_loader.h"
#include "core/nmo_shared_library.h"
#include <string.h>

/* ============================================================================
 * Library Loading
 * ============================================================================ */

nmo_status_t nmo_extension_loader_open(
    nmo_allocator_t *allocator,
    const char *path,
    const char *symbol_name,
    nmo_shared_library_t **out_library,
    nmo_extension_query_fn *out_query)
{
    if (path == NULL || out_library == NULL || out_query == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
            "path, out_library, and out_query are required");
    }

    *out_library = NULL;
    *out_query = NULL;

    /* Open the shared library */
    nmo_shared_library_t *library = NULL;
    nmo_status_t status = nmo_shared_library_open(allocator, path, &library);
    if (status != NMO_OK) {
        /* Error already set by nmo_shared_library_open */
        return status;
    }

    /* Use default symbol name if not specified */
    const char *symbol = symbol_name ? symbol_name : NMO_EXTENSION_QUERY_SYMBOL;

    /* Resolve the query function */
    void *sym = NULL;
    status = nmo_shared_library_get_symbol(library, symbol, &sym);
    if (status != NMO_OK) {
        nmo_shared_library_close(library);
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
            "Symbol '%s' not found in %s", symbol, path);
    }

    *out_library = library;
    /* Use a union to safely convert void* to function pointer (POSIX-compatible) */
    union {
        void *ptr;
        nmo_extension_query_fn fn;
    } cast;
    cast.ptr = sym;
    *out_query = cast.fn;

    NMO_RETURN_OK();
}

void nmo_extension_loader_close(nmo_shared_library_t *library)
{
    if (library != NULL) {
        nmo_shared_library_close(library);
    }
}

nmo_status_t nmo_extension_loader_query(
    nmo_extension_query_fn query_fn,
    const nmo_extension_plugin_t **out_plugins,
    size_t *out_count)
{
    if (query_fn == NULL || out_plugins == NULL || out_count == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
            "query_fn, out_plugins, and out_count are required");
    }

    *out_plugins = NULL;
    *out_count = 0;

    /* Call the query function */
    nmo_status_t status = query_fn(out_plugins, out_count);
    if (status != NMO_OK) {
        return status;
    }

    /* Validate returned plugins */
    for (size_t i = 0; i < *out_count; i++) {
        const nmo_extension_plugin_t *plugin = &(*out_plugins)[i];

        if (!nmo_extension_plugin_is_compatible(plugin)) {
            NMO_RETURN_ERROR(NMO_ERR_UNSUPPORTED_VERSION, NMO_SEVERITY_ERROR,
                "Plugin %zu ABI version mismatch (got %u, expected %u)",
                i, plugin->abi_version, NMO_EXTENSION_ABI_VERSION);
        }
    }

    NMO_RETURN_OK();
}
