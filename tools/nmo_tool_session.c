#include "nmo_tool_session.h"

#include "session/nmo_context.h"
#include "session/nmo_session.h"
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
            snprintf(buffer, buffer_size, "%s\\..\\share\\libnmo\\data", exe_path);
            if (nmo_tool_dir_exists(buffer)) {
                return buffer;
            }
        }
    }
#endif

    return "data";
}

bool nmo_tool_open_session(const char *path,
                           nmo_context_t **out_ctx,
                           nmo_session_t **out_session,
                           char *errbuf,
                           size_t errbuf_size) {
    if (!path || !out_ctx || !out_session) {
        if (errbuf && errbuf_size > 0)
            snprintf(errbuf, errbuf_size, "Invalid arguments");
        return false;
    }

    *out_ctx = NULL;
    *out_session = NULL;

    /* Prefer NMO_DATA_DIR, then source-tree data, then packaged share data. */
    char data_dir_buffer[1024];
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.data_dir = nmo_tool_resolve_data_dir(data_dir_buffer, sizeof(data_dir_buffer));
    nmo_context_t *ctx = nmo_context_create(&desc);
    if (!ctx) {
        if (errbuf && errbuf_size > 0)
            snprintf(errbuf, errbuf_size, "Failed to create libnmo context");
        return false;
    }

    nmo_session_t *session = nmo_session_load(ctx, path);
    if (!session) {
        const char *last = nmo_last_error_message();
        if (errbuf && errbuf_size > 0)
            snprintf(errbuf, errbuf_size, "%s", (last && last[0]) ? last : "Failed to load file");
        nmo_context_release(ctx);
        return false;
    }

    *out_ctx = ctx;
    *out_session = session;
    return true;
}

bool nmo_tool_open_session_opts(const char *path,
                                const nmo_load_options_t *opts,
                                nmo_context_t **out_ctx,
                                nmo_session_t **out_session,
                                char *errbuf,
                                size_t errbuf_size) {
    if (!path || !out_ctx || !out_session) {
        if (errbuf && errbuf_size > 0)
            snprintf(errbuf, errbuf_size, "Invalid arguments");
        return false;
    }

    *out_ctx = NULL;
    *out_session = NULL;

    char data_dir_buffer[1024];
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.data_dir = nmo_tool_resolve_data_dir(data_dir_buffer, sizeof(data_dir_buffer));
    nmo_context_t *ctx = nmo_context_create(&desc);
    if (!ctx) {
        if (errbuf && errbuf_size > 0)
            snprintf(errbuf, errbuf_size, "Failed to create libnmo context");
        return false;
    }

    nmo_session_t *session = nmo_session_create(ctx);
    if (!session) {
        if (errbuf && errbuf_size > 0)
            snprintf(errbuf, errbuf_size, "Failed to create session");
        nmo_context_release(ctx);
        return false;
    }

    int result = nmo_session_load_file(session, path, opts, NULL);
    if (result != NMO_OK) {
        const char *last = nmo_last_error_message();
        if (errbuf && errbuf_size > 0)
            snprintf(errbuf, errbuf_size, "%s", (last && last[0]) ? last : "Failed to load file");
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return false;
    }

    *out_ctx = ctx;
    *out_session = session;
    return true;
}

void nmo_tool_close_session(nmo_context_t *ctx, nmo_session_t *session) {
    if (session)
        nmo_session_destroy(session);
    if (ctx)
        nmo_context_release(ctx);
}
