/**
 * @file nmo_load.h
 * @brief High-level file load API
 */

#ifndef NMO_LOAD_H
#define NMO_LOAD_H

#include "core/nmo_error.h"
#include "session/nmo_deserializer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_session nmo_session_t;

/*
 * High-level load workflow is part of the stable consumer API.
 */
#define NMO_LOAD_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_LOAD_WORKFLOW_API_TIER NMO_API_TIER_STABLE_CONSUMER

/**
 * @brief Load file (high-level wrapper)
 *
 * Implements the complete load pipeline with automatic IO selection
 * (mmap for uncompressed files, standard file IO otherwise).
 * Internally creates a deserializer and runs all phases.
 * Callers consume the workflow result through session state rather than any
 * CLI/reporting output contract.
 *
 * @param session Session to load into
 * @param path    File path
 * @param opts    Load options (NULL for defaults)
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_load_file(nmo_session_t *session,
                                   const char *path,
                                   const nmo_load_options_t *opts);

#ifdef __cplusplus
}
#endif

#endif /* NMO_LOAD_H */
