/**
 * @file nmo_save.h
 * @brief High-level file save API
 */

#ifndef NMO_SAVE_H
#define NMO_SAVE_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "session/nmo_serializer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_session nmo_session_t;

/**
 * @brief Save file (high-level wrapper)
 *
 * Two-phase commit: Layout & Serialize to memory, then Pack & Commit
 * atomically to disk.  When @p opts is NULL compression settings are
 * inherited from the session's original file (round-trip safe).
 *
 * @param session Session to save from
 * @param path    Output file path
 * @param opts    Save options (NULL for round-trip-safe defaults)
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_save_file(nmo_session_t *session,
                                   const char *path,
                                   const nmo_save_options_t *opts);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SAVE_H */
