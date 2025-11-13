/**
 * @file nmo_finish_loading.h
 * @brief FinishLoading phase API (Phase 5.1)
 * 
 * ARCHITECTURE NOTE: This API belongs in the APP layer.
 * FinishLoading is a high-level orchestration function that coordinates
 * session, context, and lower layer operations.
 * 
 * Defines the finish loading phase that executes after initial object parsing.
 * This phase handles reference resolution, index building, and final processing.
 * 
 * Reference: CKFile::FinishLoading
 */

#ifndef NMO_APP_FINISH_LOADING_H
#define NMO_APP_FINISH_LOADING_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_session nmo_session_t;

/**
 * @brief Finish loading flags
 * 
 * Control which operations are performed during finish loading
 */
typedef enum nmo_finish_load_flags {
    /* Reference resolution */
    NMO_FINISH_LOAD_RESOLVE_REFERENCES  = 0x0001,  /* Resolve object references */
    NMO_FINISH_LOAD_STRICT_REFERENCES   = 0x0002,  /* Fail on unresolved references */
    
    /* Index building */
    NMO_FINISH_LOAD_BUILD_INDEXES       = 0x0004,  /* Build object indexes */
    NMO_FINISH_LOAD_INDEX_CLASS         = 0x0008,  /* Build class ID index */
    NMO_FINISH_LOAD_INDEX_NAME          = 0x0010,  /* Build name index */
    NMO_FINISH_LOAD_INDEX_GUID          = 0x0020,  /* Build GUID index */
    
    /* Manager processing */
    NMO_FINISH_LOAD_MANAGER_POSTLOAD    = 0x0040,  /* Invoke manager post-load hooks */
    NMO_FINISH_LOAD_STRICT_MANAGERS     = 0x0080,  /* Fail on manager errors */
    
    /* Statistics */
    NMO_FINISH_LOAD_GATHER_STATS        = 0x0100,  /* Gather and log statistics */
    
    /* Preset combinations */
    NMO_FINISH_LOAD_DEFAULT = 
        NMO_FINISH_LOAD_RESOLVE_REFERENCES |
        NMO_FINISH_LOAD_BUILD_INDEXES |
        NMO_FINISH_LOAD_MANAGER_POSTLOAD |
        NMO_FINISH_LOAD_GATHER_STATS,
    
    NMO_FINISH_LOAD_MINIMAL = 0,  /* No finish loading operations */
    
    NMO_FINISH_LOAD_FULL =
        NMO_FINISH_LOAD_DEFAULT |
        NMO_FINISH_LOAD_INDEX_CLASS |
        NMO_FINISH_LOAD_INDEX_NAME |
        NMO_FINISH_LOAD_INDEX_GUID,
} nmo_finish_load_flags_t;

/**
 * @brief Execute finish loading phase
 * 
 * Completes the file loading process by:
 * - Resolving object references
 * - Building object indexes for fast lookup
 * - Invoking manager post-load hooks
 * - Gathering statistics
 * 
 * This function should be called after the initial file parsing and object
 * creation phases are complete (after Phase 15 in nmo_load_file).
 * 
 * @param session Session containing loaded objects
 * @param flags Combination of nmo_finish_load_flags_t controlling operations
 * @return NMO_OK on success, error code otherwise
 * 
 * Example:
 * @code
 * // After calling nmo_load_file()
 * result = nmo_session_finish_loading(session, NMO_FINISH_LOAD_DEFAULT);
 * if (result != NMO_OK) {
 *     // Handle error
 * }
 * 
 * // Now indexes are available for fast queries
 * nmo_object_t *obj = nmo_session_find_by_name(session, "MyObject", 0);
 * @endcode
 * 
 * Reference: CKFile::FinishLoading() in reference/src/CKFile.cpp:1550-1583
 */
NMO_API int nmo_session_finish_loading(nmo_session_t *session, uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif /* NMO_APP_FINISH_LOADING_H */
