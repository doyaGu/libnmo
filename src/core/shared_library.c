#include "core/nmo_shared_library.h"

#include "core/nmo_allocator.h"
#include "core/nmo_error.h"

#include <string.h>
#include <stdio.h>
#include <stdalign.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

struct nmo_shared_library {
    nmo_allocator_t allocator;
    char *path;
#ifdef _WIN32
    HMODULE handle;
#else
    void *handle;
#endif
};

static nmo_result_t nmo_shared_library_make_error(
    nmo_error_code_t code,
    const char *context,
    const char *detail)
{
    char message[512];

    if (detail != NULL && detail[0] != '\0') {
        (void)snprintf(message, sizeof(message), "%s: %s", context, detail);
    } else {
        (void)snprintf(message, sizeof(message), "%s", context);
    }

    nmo_error_t *err = NMO_ERROR(NULL, code, NMO_SEVERITY_ERROR, message);
    if (err == NULL) {
        nmo_result_t result = { code, NULL };
        return result;
    }

    return nmo_result_error(err);
}

#ifdef _WIN32
static void nmo_shared_library_get_system_error(char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return;
    }

    DWORD error_code = GetLastError();
    DWORD written = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        buffer,
        (DWORD)buffer_size,
        NULL);

    if (written == 0) {
        (void)snprintf(buffer, buffer_size, "Windows error %lu", (unsigned long)error_code);
        return;
    }

    /* Trim trailing whitespace/newlines */
    while (written > 0 && (buffer[written - 1] == '\r' || buffer[written - 1] == '\n' || buffer[written - 1] == ' ')) {
        buffer[written - 1] = '\0';
        written--;
    }
}
#else
static void nmo_shared_library_get_system_error(char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return;
    }

    const char *err = dlerror();
    if (err == NULL) {
        (void)snprintf(buffer, buffer_size, "dynamic loader error (unknown)");
        return;
    }

    (void)snprintf(buffer, buffer_size, "%s", err);
}
#endif

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

nmo_result_t nmo_shared_library_open(
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

#ifdef _WIN32
    HMODULE handle = LoadLibraryA(path);
    if (handle == NULL) {
        char error_buffer[256] = {0};
        nmo_shared_library_get_system_error(error_buffer, sizeof(error_buffer));
        nmo_shared_library_release(library);
        return nmo_shared_library_make_error(NMO_ERR_CANT_OPEN_FILE, "LoadLibrary failed", error_buffer);
    }
    library->handle = handle;
#else
    dlerror(); /* Clear any prior error */
    void *handle = dlopen(path, RTLD_NOW);
    if (handle == NULL) {
        char error_buffer[256] = {0};
        nmo_shared_library_get_system_error(error_buffer, sizeof(error_buffer));
        nmo_shared_library_release(library);
        return nmo_shared_library_make_error(NMO_ERR_CANT_OPEN_FILE, "dlopen failed", error_buffer);
    }
    library->handle = handle;
#endif

    *out_library = library;
    return nmo_result_ok();
}

void nmo_shared_library_close(nmo_shared_library_t *library) {
    if (library == NULL) {
        return;
    }

    if (library->handle != NULL) {
#ifdef _WIN32
        (void)FreeLibrary(library->handle);
#else
        (void)dlclose(library->handle);
#endif
        library->handle = NULL;
    }

    nmo_shared_library_release(library);
}

nmo_result_t nmo_shared_library_get_symbol(
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

#ifdef _WIN32
    FARPROC proc = GetProcAddress(library->handle, symbol_name);
    if (proc == NULL) {
        char error_buffer[256] = {0};
        nmo_shared_library_get_system_error(error_buffer, sizeof(error_buffer));
        return nmo_shared_library_make_error(NMO_ERR_NOT_FOUND, "GetProcAddress failed", error_buffer);
    }
    union {
        FARPROC fp;
        void *ptr;
    } caster;
    caster.fp = proc;
    *out_symbol = caster.ptr;
#else
    dlerror();
    void *symbol = dlsym(library->handle, symbol_name);
    const char *dl_err = dlerror();
    if (dl_err != NULL) {
        char error_buffer[256] = {0};
        (void)snprintf(error_buffer, sizeof(error_buffer), "%s", dl_err);
        return nmo_shared_library_make_error(NMO_ERR_NOT_FOUND, "dlsym failed", error_buffer);
    }
    *out_symbol = symbol;
#endif

    return nmo_result_ok();
}

const char *nmo_shared_library_get_path(const nmo_shared_library_t *library) {
    if (library == NULL) {
        return NULL;
    }
    return library->path;
}
