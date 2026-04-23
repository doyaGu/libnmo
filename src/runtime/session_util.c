#include "session/nmo_session_util.h"

#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "core/nmo_error.h"

#include <string.h>

static void nmo_set_error_text(char *errbuf, size_t errbuf_size, const char *msg) {
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

bool nmo_session_open_file_with_context(const char *path,
                                        nmo_context_t **out_ctx,
                                        nmo_session_t **out_session,
                                        char *errbuf,
                                        size_t errbuf_size) {
    if (!path || !out_ctx || !out_session) {
        nmo_set_error_text(errbuf, errbuf_size, "Invalid arguments");
        return false;
    }

    *out_ctx = NULL;
    *out_session = NULL;

    /* Create context — data_dir is resolved by context from NMO_DATA_DIR env,
     * or NULL if not set (graceful degradation without Virtools data). */
    nmo_context_t *ctx = nmo_context_create(NULL);
    if (!ctx) {
        nmo_set_error_text(errbuf, errbuf_size, "Failed to create libnmo context");
        return false;
    }

    nmo_session_t *session = nmo_session_load(ctx, path);
    if (!session) {
        const char *last = nmo_last_error_message();
        nmo_set_error_text(errbuf, errbuf_size, (last && last[0]) ? last : "Failed to load file");
        nmo_context_release(ctx);
        return false;
    }

    *out_ctx = ctx;
    *out_session = session;
    nmo_set_error_text(errbuf, errbuf_size, NULL);
    return true;
}

void nmo_session_close_with_context(nmo_context_t *ctx, nmo_session_t *session) {
    if (session) {
        nmo_session_destroy(session);
    }
    if (ctx) {
        nmo_context_release(ctx);
    }
}
