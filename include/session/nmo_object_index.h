/**
 * @file nmo_object_index.h
 * @brief Object indexing system for fast lookups (Phase 5)
 * 
 * Provides efficient indexing of objects by class ID, name, and GUID.
 * Based on CKFile::m_IndexByClassId and related indexing structures.
 */

#ifndef NMO_OBJECT_INDEX_H
#define NMO_OBJECT_INDEX_H

#include "nmo_types.h"
#include "format/nmo_object.h"
#include "session/nmo_object_repository.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Object index structure (opaque)
 */
typedef struct nmo_object_index nmo_object_index_t;

/**
 * @brief Index build flags
 */
typedef enum nmo_index_build_flags {
    NMO_INDEX_BUILD_CLASS       = 0x0001,  /* Build class ID index */
    NMO_INDEX_BUILD_NAME        = 0x0002,  /* Build name index */
    NMO_INDEX_BUILD_GUID        = 0x0004,  /* Build GUID index */
    NMO_INDEX_BUILD_ALL         = 0x0007,  /* Build all indexes */
} nmo_index_build_flags_t;

/**
 * @brief Index statistics
 */
typedef struct nmo_index_stats {
    size_t total_objects;           /* Total objects indexed */
    size_t class_index_entries;     /* Number of class ID entries */
    size_t name_index_entries;      /* Number of name entries */
    size_t guid_index_entries;      /* Number of GUID entries */
    size_t memory_usage;            /* Approximate memory usage (bytes) */
} nmo_index_stats_t;

/* ==================== Lifecycle ==================== */

/**
 * Create object index
 * 
 * @param repo Object repository to index
 * @param arena Arena for memory allocation
 * @return New index or NULL on error
 * 
 * Reference: CKFile constructor initializes m_IndexByClassId
 */
NMO_API nmo_object_index_t *nmo_object_index_create(
    nmo_object_repository_t *repo,
    nmo_arena_t *arena
);

/**
 * Destroy object index
 * 
 * @param index Index to destroy
 */
NMO_API void nmo_object_index_destroy(nmo_object_index_t *index);

/* ==================== Index Building ==================== */

/**
 * Build indexes for all objects in repository
 * 
 * @param index Object index
 * @param flags Index types to build (combination of nmo_index_build_flags_t)
 * @return NMO_OK on success
 * 
 * Reference: CKFile::SaveObjectAsReference builds m_IndexByClassId
 * (reference/src/CKFile.cpp:828)
 */
NMO_API int nmo_object_index_build(
    nmo_object_index_t *index,
    uint32_t flags
);

/**
 * Rebuild all indexes (clear and rebuild)
 * 
 * @param index Object index
 * @param flags Index types to rebuild
 * @return NMO_OK on success
 */
NMO_API int nmo_object_index_rebuild(
    nmo_object_index_t *index,
    uint32_t flags
);

/**
 * Add single object to indexes (incremental update)
 * 
 * @param index Object index
 * @param object Object to add
 * @param flags Index types to update
 * @return NMO_OK on success
 */
NMO_API int nmo_object_index_add_object(
    nmo_object_index_t *index,
    nmo_object_t *object,
    uint32_t flags
);

/**
 * Remove object from indexes
 * 
 * @param index Object index
 * @param object_id Object ID to remove
 * @param flags Index types to update
 * @return NMO_OK on success
 */
NMO_API int nmo_object_index_remove_object(
    nmo_object_index_t *index,
    nmo_object_id_t object_id,
    uint32_t flags
);

/**
 * Clear all indexes
 * 
 * @param index Object index
 * @param flags Index types to clear
 * @return NMO_OK on success
 */
NMO_API int nmo_object_index_clear(
    nmo_object_index_t *index,
    uint32_t flags
);

/* ==================== Class ID Lookup ==================== */

/**
 * Get all objects of a specific class
 * 
 * @param index Object index
 * @param class_id Class ID to search for
 * @param out_count Output: number of objects found
 * @return Array of object pointers (valid until next rebuild), or NULL if none found
 * 
 * Time complexity: O(1) average case with index, O(n) without index
 * 
 * Reference: CKFile::m_IndexByClassId usage
 */
NMO_API nmo_object_t **nmo_object_index_get_by_class(
    const nmo_object_index_t *index,
    nmo_class_id_t class_id,
    size_t *out_count
);

/**
 * Count objects of a specific class
 * 
 * @param index Object index
 * @param class_id Class ID
 * @return Number of objects with this class ID
 */
NMO_API size_t nmo_object_index_count_by_class(
    const nmo_object_index_t *index,
    nmo_class_id_t class_id
);

/* ==================== Name Lookup ==================== */

/**
 * Find object by name (exact match)
 * 
 * @param index Object index
 * @param name Object name
 * @param class_id Optional class filter (0 = any class)
 * @return First matching object, or NULL if not found
 * 
 * Time complexity: O(1) average case with index
 * 
 * Note: If multiple objects have the same name, returns the first one found.
 * Use nmo_object_index_get_by_name_all() to get all matches.
 */
NMO_API nmo_object_t *nmo_object_index_find_by_name(
    const nmo_object_index_t *index,
    const char *name,
    nmo_class_id_t class_id
);

/**
 * Get all objects with a specific name
 * 
 * @param index Object index
 * @param name Object name
 * @param class_id Optional class filter (0 = any class)
 * @param out_count Output: number of objects found
 * @return Array of object pointers, or NULL if none found
 */
NMO_API nmo_object_t **nmo_object_index_get_by_name_all(
    const nmo_object_index_t *index,
    const char *name,
    nmo_class_id_t class_id,
    size_t *out_count
);

/**
 * Find object by name (case-insensitive)
 * 
 * @param index Object index
 * @param name Object name
 * @param class_id Optional class filter (0 = any class)
 * @return First matching object, or NULL if not found
 * 
 * Note: This is slower than exact match as it falls back to linear search
 */
NMO_API nmo_object_t *nmo_object_index_find_by_name_fuzzy(
    const nmo_object_index_t *index,
    const char *name,
    nmo_class_id_t class_id
);

/* ==================== GUID Lookup ==================== */

/**
 * Find object by type GUID
 * 
 * @param index Object index
 * @param guid Type GUID to search for
 * @return Object with matching GUID, or NULL if not found
 * 
 * Time complexity: O(1) average case with index, O(n) without index
 */
NMO_API nmo_object_t *nmo_object_index_find_by_guid(
    const nmo_object_index_t *index,
    nmo_guid_t guid
);

/**
 * Get all objects with a specific type GUID
 * 
 * @param index Object index
 * @param guid Type GUID
 * @param out_count Output: number of objects found
 * @return Array of object pointers, or NULL if none found
 */
NMO_API nmo_object_t **nmo_object_index_get_by_guid_all(
    const nmo_object_index_t *index,
    nmo_guid_t guid,
    size_t *out_count
);

/* ==================== Statistics ==================== */

/**
 * Get index statistics
 * 
 * @param index Object index
 * @param stats Output: statistics structure
 * @return NMO_OK on success
 */
NMO_API int nmo_object_index_get_stats(
    const nmo_object_index_t *index,
    nmo_index_stats_t *stats
);

/**
 * Check if class index is built
 * 
 * @param index Object index
 * @return 1 if built, 0 otherwise
 */
NMO_API int nmo_object_index_has_class_index(const nmo_object_index_t *index);

/**
 * Check if name index is built
 * 
 * @param index Object index
 * @return 1 if built, 0 otherwise
 */
NMO_API int nmo_object_index_has_name_index(const nmo_object_index_t *index);

/**
 * Check if GUID index is built
 * 
 * @param index Object index
 * @return 1 if built, 0 otherwise
 */
NMO_API int nmo_object_index_has_guid_index(const nmo_object_index_t *index);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_INDEX_H */
