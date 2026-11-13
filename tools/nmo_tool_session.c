#include "nmo_tool_session.h"

#include "nmo_tool_common.h"

#include "core/nmo_error.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/platform/platform.h"

static bool nmo_tool_dir_exists(const char *path) {
    return path && path[0] != '\0' && nmo_platform_is_directory(path);
}

/* "<base><suffix>" when that directory exists, else NULL. malloc'd. */
static char *nmo_tool_existing_dir_dup(const char *base, const char *suffix)
{
    char *path = nmo_tool_strdup_fmt("%s%s", base, suffix);
    if (path != NULL && !nmo_tool_dir_exists(path)) {
        free(path);
        path = NULL;
    }
    return path;
}

/* malloc'd data directory located relative to the executable, or NULL. */
static char *nmo_tool_resolve_executable_data_dir_dup(void)
{
    char *exe_path = nmo_platform_executable_path();
    if (exe_path == NULL) {
        return NULL;
    }

    char *slash = strrchr(exe_path, '/');
    char *backslash = strrchr(exe_path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) {
        slash = backslash;
    }
    if (slash == NULL) {
        free(exe_path);
        return NULL;
    }
    *slash = '\0';

    char *dir = nmo_tool_existing_dir_dup(exe_path, "/../share/libnmo/data");
    if (dir == NULL) {
        dir = nmo_tool_existing_dir_dup(exe_path, "/../data");
    }
    if (dir == NULL) {
        dir = nmo_tool_existing_dir_dup(exe_path, "/../../data");
    }
    free(exe_path);
    return dir;
}

/*
 * Data directory for the context: $NMO_DATA_DIR, ./data, a directory next to
 * the executable, or "data". *owned receives the malloc'd string to release
 * once the context has copied it (NULL when the result is static).
 */
static const char *nmo_tool_resolve_data_dir(char **owned) {
    *owned = NULL;

    const char *env = getenv("NMO_DATA_DIR");
    if (env != NULL && env[0] != '\0') {
        return env;
    }

    if (nmo_tool_dir_exists("data")) {
        return "data";
    }

    *owned = nmo_tool_resolve_executable_data_dir_dup();
    if (*owned != NULL) {
        return *owned;
    }

    return "data";
}

static void nmo_tool_set_error(char **out_error, const char *message)
{
    if (out_error != NULL) {
        *out_error = nmo_tool_strdup(message);
    }
}

bool nmo_tool_open_context(nmo_context_t **out_ctx, char **out_error) {
    nmo_context_desc_t desc;

    if (!out_ctx) {
        nmo_tool_set_error(out_error, "Invalid arguments");
        return false;
    }

    *out_ctx = NULL;
    memset(&desc, 0, sizeof(desc));
    char *owned_data_dir = NULL;
    desc.data_dir = nmo_tool_resolve_data_dir(&owned_data_dir);
    *out_ctx = nmo_context_create(&desc); /* copies data_dir */
    free(owned_data_dir);
    if (*out_ctx == NULL) {
        nmo_tool_set_error(out_error, "Failed to create libnmo context");
        return false;
    }

    return true;
}

bool nmo_tool_open_document(const char *path,
                            nmo_context_t **out_ctx,
                            nmo_document_t **out_document,
                            nmo_workspace_t **out_workspace,
                            char **out_error)
{
    return nmo_tool_open_document_opts(
        path, NULL, out_ctx, out_document, out_workspace, out_error);
}

bool nmo_tool_open_document_opts(const char *path,
                                 const nmo_load_options_t *opts,
                                 nmo_context_t **out_ctx,
                                 nmo_document_t **out_document,
                                 nmo_workspace_t **out_workspace,
                                 char **out_error)
{
    if (!path || !out_ctx || !out_document || !out_workspace) {
        nmo_tool_set_error(out_error, "Invalid arguments");
        return false;
    }

    *out_ctx = NULL;
    *out_document = NULL;
    *out_workspace = NULL;

    nmo_context_t *ctx = NULL;
    if (!nmo_tool_open_context(&ctx, out_error)) {
        return false;
    }

    nmo_document_t *document = NULL;
    nmo_status_t status = nmo_document_load_file(ctx, path, opts, &document);
    if (status != NMO_OK) {
        const char *last = nmo_last_error_message();
        nmo_tool_set_error(out_error,
                           (last && last[0]) ? last : "Failed to load file");
        nmo_context_release(ctx);
        return false;
    }

    nmo_workspace_t *workspace = NULL;
    status = nmo_workspace_create(ctx, document, &workspace);
    if (status != NMO_OK) {
        nmo_tool_set_error(out_error, "Failed to create workspace");
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
