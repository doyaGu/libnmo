/**
 * @file nmo_session_util.h
 * @brief Convenience helpers for context/session open-close pairs.
 */

#ifndef NMO_SESSION_UTIL_H
#define NMO_SESSION_UTIL_H

#include "nmo_types.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_context nmo_context_t;
typedef struct nmo_session nmo_session_t;

/**
 * @brief Create a context and load a session from a file path.
 *
 * This is a convenience helper for tools and applications that use the common
 * `context + session` lifetime pair.
 *
 * @param path Input file path.
 * @param out_ctx Receives context on success.
 * @param out_session Receives session on success.
 * @param errbuf Optional error buffer.
 * @param errbuf_size Size of @p errbuf.
 * @return true on success, false on failure.
 */
NMO_API bool nmo_session_open_file_with_context(const char *path,
                                                nmo_context_t **out_ctx,
                                                nmo_session_t **out_session,
                                                char *errbuf,
                                                size_t errbuf_size);

/**
 * @brief Destroy a session/context pair created by
 * nmo_session_open_file_with_context().
 */
NMO_API void nmo_session_close_with_context(nmo_context_t *ctx, nmo_session_t *session);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_UTIL_H */
