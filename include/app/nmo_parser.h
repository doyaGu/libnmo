/**
 * @file nmo_parser.h
 * @brief Load pipeline API (Phase 9)
 */

#ifndef NMO_PARSER_H
#define NMO_PARSER_H

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

#ifdef __cplusplus
}
#endif

#endif /* NMO_PARSER_H */
