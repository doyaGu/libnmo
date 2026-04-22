#ifndef NMO_TOOL_SESSION_H
#define NMO_TOOL_SESSION_H

#include "nmo.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a libnmo context using the same data-dir resolution as tool
 * session loading.
 */
bool nmo_tool_open_context(nmo_context_t **out_ctx,
                           char *errbuf,
                           size_t errbuf_size);

/**
 * @brief Create a libnmo context and load a session from a file.
 *
 * This helper does not print; it returns false and optionally fills errbuf.
 *
 * @param path Input file path.
 * @param out_ctx Receives context on success.
 * @param out_session Receives session on success.
 * @param errbuf Optional error buffer.
 * @param errbuf_size Size of errbuf.
 * @return true on success, false on failure.
 */
bool nmo_tool_open_session(const char *path,
                          nmo_context_t **out_ctx,
                          nmo_session_t **out_session,
                          char *errbuf,
                          size_t errbuf_size);

typedef struct nmo_load_options nmo_load_options_t;

/**
 * @brief Like nmo_tool_open_session, but with explicit load options.
 */
bool nmo_tool_open_session_opts(const char *path,
                                const nmo_load_options_t *opts,
                                nmo_context_t **out_ctx,
                                nmo_session_t **out_session,
                                char *errbuf,
                                size_t errbuf_size);

/**
 * @brief Destroy session/context created by nmo_tool_open_session.
 */
void nmo_tool_close_session(nmo_context_t *ctx, nmo_session_t *session);

#ifdef __cplusplus
}
#endif

#endif /* NMO_TOOL_SESSION_H */
