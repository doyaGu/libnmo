#include "core/nmo_shared_library.h"

#include "core/nmo_allocator.h"
#include "core/nmo_error.h"
#include "../platform/platform.h"

#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

struct nmo_shared_library {
    nmo_allocator_t allocator;
    char *path;
    void *handle;
};

static nmo_status_t nmo_shared_library_make_error(
    nmo_error_code_t code,
    const char *context,
    const char *detail)
{
    if (detail != NULL && detail[0] != '\0') {
        NMO_RETURN_ERROR(code, NMO_SEVERITY_ERROR, "%s: %s", context, detail);
    }

    NMO_RETURN_ERROR(code, NMO_SEVERITY_ERROR, "%s", context);
}

/* The error with the platform's message, which goes */
static nmo_status_t nmo_shared_library_system_error(nmo_error_code_t code, const char *context, char *message)
{
    nmo_status_t status = nmo_shared_library_make_error(code, context, message);
    free(message);
    return status;
}

static void nmo_shared_library_release(nmo_shared_library_t *library) {
    if (library == NULL) {
        return;
    }

    if (library->path != NULL) {
        nmo_free(&library->allocator, library->path);
        library->path = NULL;
    }

    nmo_free(&library->allocator, library);
}

nmo_status_t nmo_shared_library_open(
    nmo_allocator_t *allocator,
    const char *path,
    nmo_shared_library_t **out_library)
{
    if (out_library == NULL) {
        return nmo_shared_library_make_error(NMO_ERR_INVALID_ARGUMENT, "out_library must not be NULL", NULL);
    }

    *out_library = NULL;

    if (path == NULL) {
        return nmo_shared_library_make_error(NMO_ERR_INVALID_ARGUMENT, "path must not be NULL", NULL);
    }

    nmo_allocator_t fallback_allocator;
    if (allocator == NULL) {
        fallback_allocator = nmo_allocator_default();
        allocator = &fallback_allocator;
    }

    nmo_shared_library_t *library = nmo_alloc(allocator, sizeof(nmo_shared_library_t), alignof(nmo_shared_library_t));
    if (library == NULL) {
        return nmo_shared_library_make_error(NMO_ERR_NOMEM, "Failed to allocate shared library wrapper", NULL);
    }

    library->allocator = *allocator;
    library->path = NULL;
    library->handle = NULL;

    size_t path_len = strlen(path);
    library->path = nmo_alloc(&library->allocator, path_len + 1, 1);
    if (library->path == NULL) {
        nmo_shared_library_release(library);
        return nmo_shared_library_make_error(NMO_ERR_NOMEM, "Failed to copy library path", NULL);
    }

    memcpy(library->path, path, path_len + 1);

    char *message = NULL;
    library->handle = nmo_platform_library_open(path, &message);
    if (library->handle == NULL) {
        nmo_shared_library_release(library);
        return nmo_shared_library_system_error(NMO_ERR_CANT_OPEN_FILE, "Failed to load the library", message);
    }

    *out_library = library;
    NMO_RETURN_OK();
}

void nmo_shared_library_close(nmo_shared_library_t *library) {
    if (library == NULL) {
        return;
    }

    if (library->handle != NULL) {
        nmo_platform_library_close(library->handle);
        library->handle = NULL;
    }

    nmo_shared_library_release(library);
}

nmo_status_t nmo_shared_library_get_symbol(
    nmo_shared_library_t *library,
    const char *symbol_name,
    void **out_symbol)
{
    if (out_symbol == NULL) {
        return nmo_shared_library_make_error(NMO_ERR_INVALID_ARGUMENT, "out_symbol must not be NULL", NULL);
    }

    *out_symbol = NULL;

    if (library == NULL || library->handle == NULL) {
        return nmo_shared_library_make_error(NMO_ERR_INVALID_STATE, "Library handle is not initialized", NULL);
    }

    if (symbol_name == NULL) {
        return nmo_shared_library_make_error(NMO_ERR_INVALID_ARGUMENT, "symbol_name must not be NULL", NULL);
    }

    char *message = NULL;
    if (nmo_platform_library_symbol(library->handle, symbol_name, out_symbol, &message) != 0) {
        return nmo_shared_library_system_error(NMO_ERR_NOT_FOUND, "Failed to find the symbol", message);
    }

    NMO_RETURN_OK();
}

const char *nmo_shared_library_get_path(const nmo_shared_library_t *library) {
    if (library == NULL) {
        return NULL;
    }
    return library->path;
}
