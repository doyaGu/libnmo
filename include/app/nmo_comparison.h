/**
 * @file nmo_comparison.h
 * @brief DOM comparison API for round-trip testing (Phase 2.4)
 *
 * Provides structured comparison between two loaded sessions to verify
 * data integrity after save/load cycles.
 */

#ifndef NMO_APP_COMPARISON_H
#define NMO_APP_COMPARISON_H

#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_session nmo_session_t;

/* ============================================================================
 * Comparison Result Types
 * ============================================================================ */

/**
 * @brief Comparison flags for controlling comparison behavior
 */
typedef enum nmo_compare_flags {
    NMO_COMPARE_DEFAULT         = 0,
    NMO_COMPARE_STRUCTURE       = 0x0001,  /**< Compare object tree structure */
    NMO_COMPARE_IDS             = 0x0002,  /**< Compare object ID mappings */
    NMO_COMPARE_NAMES           = 0x0004,  /**< Compare object names */
    NMO_COMPARE_CLASS_IDS       = 0x0008,  /**< Compare object class IDs */
    NMO_COMPARE_CHUNKS          = 0x0010,  /**< Compare serialized chunk data */
    NMO_COMPARE_SHADOW          = 0x0020,  /**< Compare shadow blob data */
    NMO_COMPARE_MANAGERS        = 0x0040,  /**< Compare manager data */
    NMO_COMPARE_FILE_INFO       = 0x0080,  /**< Compare file metadata */
    NMO_COMPARE_STRICT          = 0x00FF,  /**< All comparisons enabled */
    NMO_COMPARE_IGNORE_ORDER    = 0x0100,  /**< Ignore object order differences */
    NMO_COMPARE_VERBOSE         = 0x0200,  /**< Generate detailed diff report */
} nmo_compare_flags_t;

/**
 * @brief Comparison difference types
 */
typedef enum nmo_diff_type {
    NMO_DIFF_NONE = 0,
    NMO_DIFF_OBJECT_COUNT,          /**< Different number of objects */
    NMO_DIFF_MANAGER_COUNT,         /**< Different number of managers */
    NMO_DIFF_OBJECT_MISSING,        /**< Object exists in one but not other */
    NMO_DIFF_OBJECT_ORDER,          /**< Object order differs between sessions */
    NMO_DIFF_OBJECT_ID,             /**< Object ID mismatch */
    NMO_DIFF_OBJECT_NAME,           /**< Object name mismatch */
    NMO_DIFF_OBJECT_CLASS_ID,       /**< Object class ID mismatch */
    NMO_DIFF_OBJECT_REFERENCE_FLAG, /**< Object reference-only flag mismatch */
    NMO_DIFF_OBJECT_CHUNK_SIZE,     /**< Chunk data size mismatch */
    NMO_DIFF_OBJECT_CHUNK_DATA,     /**< Chunk data content mismatch */
    NMO_DIFF_MANAGER_MISSING,       /**< Manager exists in one session but not the other */
    NMO_DIFF_MANAGER_GUID,          /**< Manager GUID mismatch */
    NMO_DIFF_MANAGER_CHUNK_SIZE,    /**< Manager chunk size mismatch */
    NMO_DIFF_MANAGER_CHUNK_DATA,    /**< Manager chunk data mismatch */
    NMO_DIFF_FILE_VERSION,          /**< File version mismatch */
    NMO_DIFF_CK_VERSION,            /**< CK version mismatch */
    NMO_DIFF_SHADOW_DATA,           /**< Shadow blob mismatch */
} nmo_diff_type_t;

/**
 * @brief Maximum number of differences to track
 */
#define NMO_MAX_DIFFS 64

/**
 * @brief Maximum length of diff context string
 */
#define NMO_DIFF_CONTEXT_MAX 512

/**
 * @brief Single difference entry
 */
typedef struct nmo_diff_entry {
    nmo_diff_type_t type;           /**< Type of difference */
    uint32_t object_id;             /**< Related object ID (if applicable) */
    char context[NMO_DIFF_CONTEXT_MAX]; /**< Human-readable context */
    union {
        struct {
            uint32_t expected;
            uint32_t actual;
        } count;                    /**< For count mismatches */
        struct {
            size_t expected_size;
            size_t actual_size;
        } size;                     /**< For size mismatches */
    } data;
} nmo_diff_entry_t;

/**
 * @brief Comparison result structure
 */
typedef struct nmo_comparison_result {
    int match;                      /**< 1 if sessions match, 0 otherwise */
    
    /* Summary statistics */
    uint32_t objects_compared;      /**< Number of objects compared */
    uint32_t objects_matched;       /**< Number of objects that matched */
    uint32_t managers_compared;     /**< Number of managers compared */
    uint32_t managers_matched;      /**< Number of managers that matched */
    
    /* Difference tracking */
    int diff_count;                 /**< Number of differences found */
    nmo_diff_entry_t diffs[NMO_MAX_DIFFS]; /**< Array of differences */
    int diff_overflow;              /**< 1 if more diffs than NMO_MAX_DIFFS */
    
    /* Detailed report (if NMO_COMPARE_VERBOSE) */
    char report[4096];              /**< Human-readable diff report */
} nmo_comparison_result_t;

/* ============================================================================
 * Comparison API
 * ============================================================================ */

/**
 * @brief Initialize comparison result structure
 *
 * @param result Result structure to initialize
 */
NMO_API void nmo_comparison_result_init(nmo_comparison_result_t *result);

/**
 * @brief Compare two sessions for equality
 *
 * Performs a structured comparison between two loaded sessions.
 * Use after a round-trip (load -> save -> load) to verify data integrity.
 *
 * @param session1 First session (typically the original)
 * @param session2 Second session (typically after round-trip)
 * @param flags Comparison flags controlling what to compare
 * @param result Output: comparison result (caller must initialize)
 * @return NMO_OK on success (even if sessions differ), error code on failure
 */
NMO_API int nmo_session_compare(const nmo_session_t *session1,
                                const nmo_session_t *session2,
                                nmo_compare_flags_t flags,
                                nmo_comparison_result_t *result);

/**
 * @brief Compare file info between sessions
 *
 * @param session1 First session
 * @param session2 Second session
 * @param result Output: comparison result
 * @return 1 if file info matches, 0 otherwise
 */
NMO_API int nmo_session_compare_file_info(const nmo_session_t *session1,
                                          const nmo_session_t *session2,
                                          nmo_comparison_result_t *result);

/**
 * @brief Compare object repositories between sessions
 *
 * @param session1 First session
 * @param session2 Second session
 * @param flags Comparison flags
 * @param result Output: comparison result
 * @return 1 if objects match, 0 otherwise
 */
NMO_API int nmo_session_compare_objects(const nmo_session_t *session1,
                                        const nmo_session_t *session2,
                                        nmo_compare_flags_t flags,
                                        nmo_comparison_result_t *result);

/**
 * @brief Generate human-readable diff report
 *
 * Populates the report field of the comparison result with a
 * formatted summary of all differences found.
 *
 * @param result Comparison result to generate report for
 */
NMO_API void nmo_comparison_result_format_report(nmo_comparison_result_t *result);

/**
 * @brief Add a difference to the result
 *
 * Internal helper for adding difference entries.
 *
 * @param result Result to add difference to
 * @param type Type of difference
 * @param object_id Related object ID (0 if not applicable)
 * @param context Human-readable context string
 */
NMO_API void nmo_comparison_add_diff(nmo_comparison_result_t *result,
                                     nmo_diff_type_t type,
                                     uint32_t object_id,
                                     const char *context);

#ifdef __cplusplus
}
#endif

#endif /* NMO_APP_COMPARISON_H */
