#include "nmo_tool_session.h"

#include "core/nmo_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static bool nmo_tool_dir_exists(const char *path) {
    if (!path || path[0] == '\0') {
        return false;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return (st.st_mode & S_IFDIR) != 0;
}

static const char *nmo_tool_try_buffer_path(char *buffer,
                                            size_t buffer_size,
                                            const char *fmt,
                                            const char *base)
{
    if (buffer == NULL || buffer_size == 0u || fmt == NULL || base == NULL) {
        return NULL;
    }

    snprintf(buffer, buffer_size, fmt, base);
    if (nmo_tool_dir_exists(buffer)) {
        return buffer;
    }
    return NULL;
}

static const char *nmo_tool_resolve_data_dir(char *buffer, size_t buffer_size) {
    const char *env = getenv("NMO_DATA_DIR");
    if (env != NULL && env[0] != '\0') {
        return env;
    }

    if (nmo_tool_dir_exists("data")) {
        return "data";
    }

#ifdef _WIN32
    char exe_path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exe_path, (DWORD)sizeof(exe_path));
    if (len > 0 && len < sizeof(exe_path)) {
        char *slash = strrchr(exe_path, '\\');
        char *alt_slash = strrchr(exe_path, '/');
        if (alt_slash != NULL && (slash == NULL || alt_slash > slash)) {
            slash = alt_slash;
        }
        if (slash != NULL) {
            *slash = '\0';
            if (nmo_tool_try_buffer_path(buffer, buffer_size,
                                         "%s\\..\\share\\libnmo\\data",
                                         exe_path) != NULL) {
                return buffer;
            }
            if (nmo_tool_try_buffer_path(buffer, buffer_size,
                                         "%s\\..\\data",
                                         exe_path) != NULL) {
                return buffer;
            }
            if (nmo_tool_try_buffer_path(buffer, buffer_size,
                                         "%s\\..\\..\\data",
                                         exe_path) != NULL) {
                return buffer;
            }
        }
    }
#endif

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
