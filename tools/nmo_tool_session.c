#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "nmo_tool_session.h"

#include "core/nmo_error.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

static bool nmo_tool_dir_exists(const char *path) {
    if (!path || path[0] == '\0') {
        return false;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
#ifdef _WIN32
    return (st.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(st.st_mode);
#endif
}

static const char *nmo_tool_try_buffer_path(char *buffer,
                                            size_t buffer_size,
                                            const char *fmt,
                                            const char *base)
{
    if (buffer == NULL || buffer_size == 0u || fmt == NULL || base == NULL) {
        return NULL;
    }

    int written = snprintf(buffer, buffer_size, fmt, base);
    if (written < 0 || (size_t)written >= buffer_size) {
        buffer[0] = '\0';
        return NULL;
    }
    if (nmo_tool_dir_exists(buffer)) {
        return buffer;
    }
    return NULL;
}

static bool nmo_tool_get_executable_path(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size < 2u) {
        return false;
    }

#ifdef _WIN32
    DWORD len = GetModuleFileNameA(NULL, buffer, (DWORD)buffer_size);
    return len > 0u && (size_t)len < buffer_size;
#elif defined(__APPLE__)
    uint32_t size = buffer_size > UINT32_MAX
        ? UINT32_MAX
        : (uint32_t)buffer_size;
    return _NSGetExecutablePath(buffer, &size) == 0;
#elif defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", buffer, buffer_size - 1u);
    if (len <= 0 || (size_t)len >= buffer_size) {
        return false;
    }
    buffer[len] = '\0';
    return true;
#else
    (void)buffer;
    (void)buffer_size;
    return false;
#endif
}

static const char *nmo_tool_resolve_executable_data_dir(char *buffer,
                                                         size_t buffer_size)
{
    char exe_path[4096];
    if (!nmo_tool_get_executable_path(exe_path, sizeof(exe_path))) {
        return NULL;
    }

    char *slash = strrchr(exe_path, '/');
    char *backslash = strrchr(exe_path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) {
        slash = backslash;
    }
    if (slash == NULL) {
        return NULL;
    }
    *slash = '\0';

    if (nmo_tool_try_buffer_path(buffer, buffer_size,
                                 "%s/../share/libnmo/data",
                                 exe_path) != NULL) {
        return buffer;
    }
    if (nmo_tool_try_buffer_path(buffer, buffer_size,
                                 "%s/../data", exe_path) != NULL) {
        return buffer;
    }
    return nmo_tool_try_buffer_path(buffer, buffer_size,
                                    "%s/../../data", exe_path);
}

static const char *nmo_tool_resolve_data_dir(char *buffer, size_t buffer_size) {
    const char *env = getenv("NMO_DATA_DIR");
    if (env != NULL && env[0] != '\0') {
        return env;
    }

    if (nmo_tool_dir_exists("data")) {
        return "data";
    }

    const char *executable_data_dir =
        nmo_tool_resolve_executable_data_dir(buffer, buffer_size);
    if (executable_data_dir != NULL) {
        return executable_data_dir;
    }

    return "data";
}

bool nmo_tool_open_context(nmo_context_t **out_ctx,
                           char *errbuf,
                           size_t errbuf_size) {
    char data_dir_buffer[1024];
    nmo_context_desc_t desc;

    if (!out_ctx) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Invalid arguments");
        }
        return false;
    }

    *out_ctx = NULL;
    memset(&desc, 0, sizeof(desc));
    desc.data_dir = nmo_tool_resolve_data_dir(data_dir_buffer, sizeof(data_dir_buffer));
    *out_ctx = nmo_context_create(&desc);
    if (*out_ctx == NULL) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Failed to create libnmo context");
        }
        return false;
    }

    return true;
}

bool nmo_tool_open_document(const char *path,
                            nmo_context_t **out_ctx,
                            nmo_document_t **out_document,
                            nmo_workspace_t **out_workspace,
                            char *errbuf,
                            size_t errbuf_size)
{
    return nmo_tool_open_document_opts(
        path, NULL, out_ctx, out_document, out_workspace, errbuf, errbuf_size);
}

bool nmo_tool_open_document_opts(const char *path,
                                 const nmo_load_options_t *opts,
                                 nmo_context_t **out_ctx,
                                 nmo_document_t **out_document,
                                 nmo_workspace_t **out_workspace,
                                 char *errbuf,
                                 size_t errbuf_size)
{
    if (!path || !out_ctx || !out_document || !out_workspace) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Invalid arguments");
        }
        return false;
    }

    *out_ctx = NULL;
    *out_document = NULL;
    *out_workspace = NULL;

    nmo_context_t *ctx = NULL;
    if (!nmo_tool_open_context(&ctx, errbuf, errbuf_size)) {
        return false;
    }

    nmo_document_t *document = NULL;
    nmo_status_t status = nmo_document_load_file(ctx, path, opts, &document);
    if (status != NMO_OK) {
        const char *last = nmo_last_error_message();
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "%s",
                     (last && last[0]) ? last : "Failed to load file");
        }
        nmo_context_release(ctx);
        return false;
    }

    nmo_workspace_t *workspace = NULL;
    status = nmo_workspace_create(ctx, document, &workspace);
    if (status != NMO_OK) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Failed to create workspace");
        }
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return false;
    }

    *out_ctx = ctx;
    *out_document = document;
    *out_workspace = workspace;
    return true;
}

void nmo_tool_close_document(nmo_context_t *ctx,
                             nmo_document_t *document,
                             nmo_workspace_t *workspace)
{
    if (workspace) {
        nmo_workspace_destroy(workspace);
    }
    if (document) {
        nmo_document_destroy(document);
    }
    if (ctx) {
        nmo_context_release(ctx);
    }
}
