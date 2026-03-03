#include "nmo_tool_session.h"
#include "app/nmo_session_util.h"

bool nmo_tool_open_session(const char *path,
                           nmo_context_t **out_ctx,
                           nmo_session_t **out_session,
                           char *errbuf,
                           size_t errbuf_size) {
    return nmo_session_open_file_with_context(path, out_ctx, out_session, errbuf, errbuf_size);
}

void nmo_tool_close_session(nmo_context_t *ctx, nmo_session_t *session) {
    nmo_session_close_with_context(ctx, session);
}
