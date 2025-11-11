/**
 * @file nmo_parser.h
 * @brief Load and Save pipeline API (Phase 9 & 10)
 */

#ifndef NMO_APP_PARSER_H
#define NMO_APP_PARSER_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_session nmo_session;

/**
 * @brief Load flags
 */
typedef enum {
    NMO_LOAD_DEFAULT            = 0,
    NMO_LOAD_DODIALOG           = 0x0001,
    NMO_LOAD_AUTOMATICMODE      = 0x0002,
    NMO_LOAD_CHECKDUPLICATES    = 0x0004,
    NMO_LOAD_AS_DYNAMIC_OBJECT  = 0x0008,
    NMO_LOAD_ONLYBEHAVIORS      = 0x0010,
    NMO_LOAD_CHECK_DEPENDENCIES = 0x0020,
} nmo_load_flags;

/**
 * @brief Load file
 *
 * Implements the complete 15-phase load pipeline:
 * 1. Open IO
 * 2. Parse File Header
 * 3. Read and Decompress Header1
 * 4. Parse Header1
 * 5. Start Load Session
 * 6. Check Plugin Dependencies
 * 7. Manager Pre-Load Hooks
 * 8. Read and Decompress Data Section
 * 9. Parse Manager Chunks
 * 10. Create Objects
 * 11. Parse Object Chunks
 * 12. Build ID Remap Table
 * 13. Remap IDs in All Chunks
 * 14. Deserialize Objects
 * 15. Manager Post-Load Hooks
 *
 * @param session Session to load into
 * @param path File path
 * @param flags Load flags
 * @return NMO_OK on success
 */
NMO_API int nmo_load_file(nmo_session* session,
                           const char* path,
                           nmo_load_flags flags);

/**
 * @brief Save flags
 */
typedef enum {
    NMO_SAVE_DEFAULT            = 0,
    NMO_SAVE_AS_OBJECTS         = 0x0001,  /**< Save as referenced objects */
    NMO_SAVE_COMPRESSED         = 0x0002,  /**< Enable compression */
    NMO_SAVE_SEQUENTIAL_IDS     = 0x0004,  /**< Use sequential file IDs */
    NMO_SAVE_INCLUDE_MANAGERS   = 0x0008,  /**< Include manager state */
    NMO_SAVE_VALIDATE_BEFORE    = 0x0010,  /**< Validate before writing */
} nmo_save_flags;

/**
 * @brief Save file
 *
 * Implements the complete 14-phase save pipeline:
 * 1. Validate Session State
 * 2. Manager Pre-Save Hooks
 * 3. Build ID Remap Plan (runtime → file IDs)
 * 4. Serialize Manager Chunks
 * 5. Serialize Object Chunks with ID Remapping
 * 6. Compress Data Section
 * 7. Build Object Descriptors for Header1
 * 8. Build Plugin Dependencies List
 * 9. Compress Header1
 * 10. Calculate File Sizes
 * 11. Build File Header
 * 12. Open Output IO
 * 13. Write File Header, Header1, Data Section
 * 14. Manager Post-Save Hooks
 *
 * @param session Session to save from
 * @param path File path
 * @param flags Save flags
 * @return NMO_OK on success
 */
NMO_API int nmo_save_file(nmo_session* session,
                           const char* path,
                           nmo_save_flags flags);

#ifdef __cplusplus
}
#endif

#endif /* NMO_APP_PARSER_H */
