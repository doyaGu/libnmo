/**
 * @file nmo_load.h
 * @brief High-level file load API
 */

#ifndef NMO_LOAD_H
#define NMO_LOAD_H

#include "session/nmo_deserializer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_session nmo_session_t;

/**
 * @brief Load file (high-level wrapper)
 *
 * Implements the complete load pipeline with automatic IO selection
 * (mmap for uncompressed files, standard file IO otherwise).
 * Internally creates a deserializer and runs all phases.
 *
 * @param session Session to load into
 * @param path    File path
 * @param opts    Load options (NULL for defaults)
 * @return NMO_OK on success
 */
NMO_API int nmo_load_file(nmo_session_t *session,
                          const char *path,
                          const nmo_load_options_t *opts);

#ifdef __cplusplus
}
#endif

#endif /* NMO_LOAD_H */
