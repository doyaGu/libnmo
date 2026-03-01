/**
 * @file nmo_parser.h
 * @brief Load pipeline API
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
 * Implements the complete load pipeline with automatic IO selection
 * (mmap for uncompressed files, standard file IO otherwise).
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

#endif /* NMO_APP_PARSER_H */
