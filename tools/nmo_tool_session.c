#include "nmo_tool_session.h"

#include <string.h>

static void set_err(char *errbuf, size_t errbuf_size, const char *msg) {
    if (!errbuf || errbuf_size == 0) {
        return;
    }
    if (!msg) {
        errbuf[0] = '\0';
        return;
    }
#if defined(_MSC_VER)
    strncpy_s(errbuf, errbuf_size, msg, _TRUNCATE);
#else
    strncpy(errbuf, msg, errbuf_size - 1);
    errbuf[errbuf_size - 1] = '\0';
#endif
}

bool nmo_tool_open_session(const char *path,
                           nmo_context_t **out_ctx,
                           nmo_session_t **out_session,
                           char *errbuf,
                           size_t errbuf_size) {
    if (!path || !out_ctx || !out_session) {
        set_err(errbuf, errbuf_size, "Invalid arguments");
        return false;
    }

    *out_ctx = NULL;
    *out_session = NULL;

    nmo_context_t *ctx = nmo_context_create(NULL);
    if (!ctx) {
        set_err(errbuf, errbuf_size, "Failed to create libnmo context");
        return false;
    }

    nmo_session_t *session = nmo_session_load(ctx, path);
    if (!session) {
        set_err(errbuf, errbuf_size, "Failed to load file");
        nmo_context_destroy(ctx);
        return false;
    }

    *out_ctx = ctx;
    *out_session = session;
    set_err(errbuf, errbuf_size, NULL);
    return true;
}

void nmo_tool_close_session(nmo_context_t *ctx, nmo_session_t *session) {
    if (session) {
        nmo_session_destroy(session);
    }
    if (ctx) {
        nmo_context_destroy(ctx);
    }
}
