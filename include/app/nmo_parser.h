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
typedef struct nmo_session nmo_session_t;
typedef struct nmo_allocator nmo_allocator_t;

/* ============================================================================
 * Load-time limits
 * ============================================================================ */

/** Default maximum included filename length (bytes, excluding NUL). */
#define NMO_LOAD_DEFAULT_MAX_INCLUDED_NAME_LEN 4096u

/** Default maximum included file payload size (bytes). */
#define NMO_LOAD_DEFAULT_MAX_INCLUDED_FILE_SIZE (512u * 1024u * 1024u)

/**
 * @brief Load flags
 */
typedef enum nmo_load_flags {
    NMO_LOAD_DEFAULT            = 0,
    NMO_LOAD_DODIALOG           = 0x0001,
    NMO_LOAD_AUTOMATICMODE      = 0x0002,
    NMO_LOAD_CHECKDUPLICATES    = 0x0004,
    NMO_LOAD_AS_DYNAMIC_OBJECT  = 0x0008,
    NMO_LOAD_ONLYBEHAVIORS      = 0x0010,
    NMO_LOAD_CHECK_DEPENDENCIES = 0x0020,
    NMO_LOAD_PRESERVE_SHADOW    = 0x0080,

    /* Phase 5 flags */
    NMO_LOAD_SKIP_INDEX_BUILD       = 0x0100,  /* Skip object index building */
    NMO_LOAD_SKIP_REFERENCE_RESOLVE = 0x0200,  /* Skip reference resolution */
} nmo_load_flags_t;

/**
 * @brief Extended load options (Phase 2.1)
 *
 * Provides fine-grained control over the loading process.
 */
typedef struct nmo_load_options {
    nmo_allocator_t *allocator;     /**< Custom allocator (NULL for default) */
    nmo_load_flags_t flags;         /**< Standard load flags */

    /** Maximum included filename length (bytes, excluding NUL). */
    uint32_t max_included_name_len;

    /** Maximum included file payload size (bytes). */
    uint32_t max_included_file_size;
} nmo_load_options_t;

/**
 * @brief Initialize load options with defaults
 *
 * @return Default load options:
 *   - allocator: NULL (use default)
 *   - flags: NMO_LOAD_DEFAULT
 */
NMO_API nmo_load_options_t nmo_load_options_default(void);

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
NMO_API int nmo_load_file(nmo_session_t *session,
                          const char *path,
                          nmo_load_flags_t flags);

/**
 * @brief Load file with extended options (Phase 2.1)
 *
 * Extended version of nmo_load_file() that accepts load options
 * for fine-grained control over the loading process, including:
 * - CRC validation
 * - Shadow storage preservation
 * - Custom allocator
 *
 * @param session Session to load into
 * @param path File path
 * @param opts Load options (NULL for defaults)
 * @return NMO_OK on success
 */
NMO_API int nmo_load_file_ex(nmo_session_t *session,
                             const char *path,
                             const nmo_load_options_t *opts);

/**
 * @brief Save flags
 */
typedef enum nmo_save_flags {
    NMO_SAVE_DEFAULT          = 0,
    NMO_SAVE_AS_OBJECTS       = 0x0001, /**< Save as referenced objects */
    NMO_SAVE_COMPRESSED       = 0x0002, /**< Enable compression */
    NMO_SAVE_SEQUENTIAL_IDS   = 0x0004, /**< Use sequential file IDs */
    NMO_SAVE_INCLUDE_MANAGERS = 0x0008, /**< Include manager state */
    NMO_SAVE_VALIDATE_BEFORE  = 0x0010, /**< Validate before writing */
    NMO_SAVE_STRIP_INCLUDED_FILES = 0x0020, /**< Drop included payloads during save */
} nmo_save_flags_t;

/**
 * @brief Save file
 *
 * Implements the complete 14-phase save pipeline:
 * 1. Validate Session State
 * 2. Manager Pre-Save Hooks
 * 3. Build ID Remap Plan (runtime -> file IDs)
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
NMO_API int nmo_save_file(nmo_session_t *session,
                          const char *path,
                          nmo_save_flags_t flags);

#ifdef __cplusplus
}
#endif

#endif /* NMO_APP_PARSER_H */
