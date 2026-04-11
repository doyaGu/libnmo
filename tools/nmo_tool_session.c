#include "nmo_tool_session.h"

#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "core/nmo_error.h"

#include <string.h>

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

    /* CLI creates context with data_dir="data" for Virtools data auto-loading.
     * This is relative to CWD. For deployed tools, set NMO_DATA_DIR env var. */
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.data_dir = "data";
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

void nmo_tool_close_session(nmo_context_t *ctx, nmo_session_t *session) {
    if (session)
        nmo_session_destroy(session);
    if (ctx)
        nmo_context_release(ctx);
}
