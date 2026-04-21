/**
 * @file nmo_object_diff.h
 * @brief Semantic object diff engine
 *
 * Computes structured field-level diffs between objects in two loaded sessions.
 * Matching is topology-aware and class-local:
 * anchors + similarity flooding + Hungarian assignment.
 * Object reference IDs are resolved to "ClassName/ObjectName" paths since IDs
 * are session-local indices and NOT stable across saves.
 *
 * For stable binding-facing summary consumption, prefer
 * nmo_diff_result_collect_stats() from nmo_report_result.h over re-parsing
 * formatted field strings.
 *
 * Architecture: App Layer (depends on Session, Object, Type, Format, Core)
 */

#ifndef NMO_APP_OBJECT_DIFF_H
#define NMO_APP_OBJECT_DIFF_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include <stdbool.h>
#include <stddef.h>

#define NMO_OBJECT_DIFF_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_OBJECT_DIFF_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_context nmo_context_t;
typedef struct nmo_session nmo_session_t;
typedef struct nmo_object nmo_object_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_type_registry nmo_type_registry_t;

/* ============================================================================
 * Configuration
 * ============================================================================ */

/**
 * @brief Diff configuration
 */
typedef struct nmo_diff_config {
    uint32_t max_objects;       /**< Max changed objects to report (0=unlimited) */
    uint32_t max_fields;        /**< Max field diffs per object (0=unlimited) */
    float    min_similarity;    /**< Min similarity to consider a match (default 0.0) */
    float    rename_similarity; /**< Min similarity to classify as renamed (default 0.85) */
    uint32_t flags;             /**< Reserved for future use */
} nmo_diff_config_t;

/**
 * @brief Return default diff configuration
 */
static inline nmo_diff_config_t nmo_diff_config_default(void) {
    nmo_diff_config_t c;
    c.max_objects = 0;
    c.max_fields = 0;
    c.min_similarity = 0.0f;
    c.rename_similarity = 0.85f;
    c.flags = 0;
    return c;
}

/* ============================================================================
 * Diff Result Structures
 * ============================================================================ */

/** Maximum formatted value length */
#define NMO_DIFF_VALUE_MAX 256

/**
 * @brief A single field difference between two matched objects
 */
typedef struct nmo_field_diff {
    const char *field_name;                 /**< Field name (arena-owned) */
    char before[NMO_DIFF_VALUE_MAX];        /**< Formatted old value */
    char after[NMO_DIFF_VALUE_MAX];         /**< Formatted new value */
} nmo_field_diff_t;

/**
 * @brief A matched object pair with field-level differences
 */
typedef struct nmo_object_diff {
    const nmo_object_t *obj1;               /**< Object from session 1 */
    const nmo_object_t *obj2;               /**< Object from session 2 */
    nmo_field_diff_t *field_diffs;          /**< Array of field differences (arena-owned) */
    size_t field_diff_count;                /**< Number of field differences */
    size_t field_diff_total;                /**< Total field diffs (may exceed reported if truncated) */
    float similarity;                       /**< Content similarity 0.0..1.0 */
} nmo_object_diff_t;

/**
 * @brief A matched pair classified as rename
 */
typedef struct nmo_rename_diff {
    const nmo_object_t *obj1;               /**< Object from session 1 */
    const nmo_object_t *obj2;               /**< Object from session 2 */
    const char *before_name;                /**< Arena-owned copy (can be empty) */
    const char *after_name;                 /**< Arena-owned copy (can be empty) */
    float similarity;                       /**< Match similarity 0.0..1.0 */
} nmo_rename_diff_t;

/**
 * @brief Full diff result containing all matched/added/removed objects
 */
typedef struct nmo_diff_result {
    /* Changed objects (matched pairs with at least one field diff) */
    nmo_object_diff_t *changed;
    size_t changed_count;

    /* Renamed objects (matched pairs with high similarity and different names) */
    nmo_rename_diff_t *renamed;
    size_t renamed_count;

    /* Objects only in session 1 (removed) */
    const nmo_object_t **removed;
    size_t removed_count;

    /* Objects only in session 2 (added) */
    const nmo_object_t **added;
    size_t added_count;

    /* Statistics */
    size_t identical_count;                 /**< Matched pairs with zero diffs */
    size_t total_objects1;                  /**< Total objects in session 1 */
    size_t total_objects2;                  /**< Total objects in session 2 */

    /* Internal - do not access directly */
    nmo_arena_t *arena_;                    /**< Owns all dynamic allocations */
} nmo_diff_result_t;

/* ============================================================================
 * Core API
 * ============================================================================ */

/**
 * @brief Compute semantic diff between objects in two sessions
 *
 * Matching algorithm:
 * 1. Build per-class graph nodes and strong anchors
 * 2. Run topology-aware similarity flooding on unmatched candidates
 * 3. Solve class-local N:M assignment via Hungarian algorithm
 * 4. Classify rename pairs and build field-level diffs
 *
 * Field comparison resolves object reference IDs to "ClassName/ObjectName".
 *
 * @param ctx1    Context for session 1
 * @param ses1    Session 1
 * @param ctx2    Context for session 2
 * @param ses2    Session 2
 * @param config  Configuration (NULL for defaults)
 * @param result  Output result (caller-owned, cleaned up via nmo_diff_result_destroy)
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_diff_objects(
    nmo_context_t *ctx1, nmo_session_t *ses1,
    nmo_context_t *ctx2, nmo_session_t *ses2,
    const nmo_diff_config_t *config,
    nmo_diff_result_t *result);

/**
 * @brief Destroy diff result and free all allocations
 * @param result Result to destroy (safe to call with NULL or zeroed result)
 */
NMO_API void nmo_diff_result_destroy(nmo_diff_result_t *result);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Format object identity as "ClassName/ObjectName"
 *
 * @param buf       Output buffer
 * @param buf_size  Buffer size
 * @param ctx       Context (for class name resolution)
 * @param obj       Object
 */
NMO_API void nmo_object_format_path(
    char *buf, size_t buf_size,
    nmo_context_t *ctx,
    const nmo_object_t *obj);

/**
 * @brief Format an object reference ID as "ClassName/ObjectName"
 *
 * Returns "(null)" for id 0, "(unknown #id)" if not resolved.
 *
 * @param buf       Output buffer
 * @param buf_size  Buffer size
 * @param id        Object reference ID
 * @param repo      Object repository to resolve the ID
 * @param ctx       Context (for class name resolution)
 */
NMO_API void nmo_object_format_ref(
    char *buf, size_t buf_size,
    nmo_object_id_t id,
    const nmo_object_repository_t *repo,
    nmo_context_t *ctx);

/**
 * @brief Compare two object references by resolved identity
 *
 * IDs are session-local indices and NOT stable across saves.
 * Two references are equal if they point to objects with the same
 * class_id and name.
 *
 * @param id1   Reference ID from session 1
 * @param id2   Reference ID from session 2
 * @param repo1 Repository for session 1
 * @param repo2 Repository for session 2
 * @return true if references point to equivalent objects
 */
NMO_API bool nmo_object_ref_equal(
    nmo_object_id_t id1, nmo_object_id_t id2,
    const nmo_object_repository_t *repo1,
    const nmo_object_repository_t *repo2);

/**
 * @brief Compute content similarity between two objects
 *
 * Returns the ratio of matching fields to total comparable fields.
 * Requires both objects to have the same class_id and reflection data.
 * Falls back to chunk data comparison when no reflection is available.
 *
 * @return Similarity score in [0.0, 1.0]
 */
NMO_API float nmo_object_similarity(
    const nmo_object_t *obj1, const nmo_object_t *obj2,
    const nmo_type_registry_t *reg1, const nmo_type_registry_t *reg2,
    const nmo_object_repository_t *repo1, const nmo_object_repository_t *repo2);

#ifdef __cplusplus
}
#endif

#endif /* NMO_APP_OBJECT_DIFF_H */
