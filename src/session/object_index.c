/**
 * @file object_index.c
 * @brief Object indexing system implementation (Phase 5)
 * 
 * Provides efficient indexing of objects by class ID, name, and GUID.
 * Based on CKFile::m_IndexByClassId and related indexing structures.
 * 
 * Reference:
 * - reference/include/CKFile.h line 267: m_IndexByClassId
 * - reference/src/CKFile.cpp line 800-900: Index usage
 */

#include "session/nmo_object_index.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_hash.h"
#include "format/nmo_object.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ==================== Internal Structures ==================== */

/**
 * @brief Dynamic array of object pointers
 */
typedef struct object_array {
    nmo_object_t **objects;
    size_t count;
    size_t capacity;
} object_array_t;

/**
 * @brief Object index structure
 */
struct nmo_object_index {
    nmo_object_repository_t *repo;
    nmo_arena_t *arena;
    
    /* Index flags */
    uint32_t active_indexes;
    
    /* Class ID index: class_id → object_array_t */
    nmo_hash_table_t *class_index;
    
    /* Name index: name → object_array_t */
    nmo_hash_table_t *name_index;
    
    /* GUID index: guid → object_array_t */
    nmo_hash_table_t *guid_index;
    
    /* Cache for last query */
    nmo_object_t **last_query_result;
    size_t last_query_count;
    nmo_class_id_t last_query_class;
};

/* ==================== Helper Functions ==================== */

/**
 * Create object array
 */
static object_array_t *object_array_create(size_t initial_capacity) {
    object_array_t *arr = (object_array_t *)malloc(sizeof(object_array_t));
    if (arr == NULL) {
        return NULL;
    }
    
    arr->capacity = initial_capacity > 0 ? initial_capacity : 8;
    arr->objects = (nmo_object_t **)malloc(arr->capacity * sizeof(nmo_object_t *));
    if (arr->objects == NULL) {
        free(arr);
        return NULL;
    }
    
    arr->count = 0;
    return arr;
}

/**
 * Add object to array
 */
static int object_array_add(object_array_t *arr, nmo_object_t *obj) {
    if (arr->count >= arr->capacity) {
        size_t new_capacity = arr->capacity * 2;
        nmo_object_t **new_objects = (nmo_object_t **)realloc(
            arr->objects, 
            new_capacity * sizeof(nmo_object_t *)
        );
        if (new_objects == NULL) {
            return NMO_ERR_NOMEM;
        }
        arr->objects = new_objects;
        arr->capacity = new_capacity;
    }
    
    arr->objects[arr->count++] = obj;
    return NMO_OK;
}

/**
 * Remove object from array by ID
 */
static int object_array_remove(object_array_t *arr, nmo_object_id_t id) {
    for (size_t i = 0; i < arr->count; i++) {
        if (arr->objects[i]->id == id) {
            /* Shift remaining elements */
            memmove(&arr->objects[i], &arr->objects[i + 1],
                    (arr->count - i - 1) * sizeof(nmo_object_t *));
            arr->count--;
            return NMO_OK;
        }
    }
    return NMO_ERR_NOT_FOUND;
}

/**
 * Case-insensitive string comparison
 */
static int strcasecmp_portable(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) {
            return c1 - c2;
        }
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

/* ==================== Index Building ==================== */

/**
 * Build class ID index
 */
static int build_class_index(nmo_object_index_t *index) {
    if (index->class_index != NULL) {
        nmo_hash_table_destroy(index->class_index);
    }
    
    /* Create hash table: uint32_t → object_array_t* */
    index->class_index = nmo_hash_table_create(
        sizeof(nmo_class_id_t),
        sizeof(object_array_t *),
        64, /* Initial capacity */
        nmo_hash_uint32,
        NULL /* Default memcmp */
    );
    
    if (index->class_index == NULL) {
        return NMO_ERR_NOMEM;
    }
    
    /* Get all objects from repository */
    size_t obj_count;
    nmo_object_t **objects = nmo_object_repository_get_all(index->repo, &obj_count);
    
    /* Group objects by class ID */
    for (size_t i = 0; i < obj_count; i++) {
        nmo_object_t *obj = objects[i];
        object_array_t *arr = NULL;
        
        /* Check if class already has an array */
        if (!nmo_hash_table_get(index->class_index, &obj->class_id, &arr)) {
            /* Create new array for this class */
            arr = object_array_create(8);
            if (arr == NULL) {
                return NMO_ERR_NOMEM;
            }
            nmo_hash_table_insert(index->class_index, &obj->class_id, &arr);
        }
        
        /* Add object to array */
        int result = object_array_add(arr, obj);
        if (result != NMO_OK) {
            return result;
        }
    }
    
    index->active_indexes |= NMO_INDEX_BUILD_CLASS;
    return NMO_OK;
}

/**
 * Build name index
 */
static int build_name_index(nmo_object_index_t *index) {
    if (index->name_index != NULL) {
        nmo_hash_table_destroy(index->name_index);
    }
    
    /* Create hash table: string → object_array_t* */
    index->name_index = nmo_hash_table_create(
        sizeof(char *),
        sizeof(object_array_t *),
        64,
        nmo_hash_string,
        nmo_compare_string  /* Use string comparison */
    );
    
    if (index->name_index == NULL) {
        return NMO_ERR_NOMEM;
    }
    
    /* Get all objects from repository */
    size_t obj_count;
    nmo_object_t **objects = nmo_object_repository_get_all(index->repo, &obj_count);
    
    /* Group objects by name */
    for (size_t i = 0; i < obj_count; i++) {
        nmo_object_t *obj = objects[i];
        const char *name = nmo_object_get_name(obj);
        
        /* Skip objects without name */
        if (name == NULL || name[0] == '\0') {
            continue;
        }
        
        object_array_t *arr = NULL;
        
        /* Check if name already has an array */
        if (!nmo_hash_table_get(index->name_index, &name, &arr)) {
            /* Create new array for this name */
            arr = object_array_create(4); /* Most names are unique */
            if (arr == NULL) {
                return NMO_ERR_NOMEM;
            }
            nmo_hash_table_insert(index->name_index, &name, &arr);
        }
        
        /* Add object to array */
        int result = object_array_add(arr, obj);
        if (result != NMO_OK) {
            return result;
        }
    }
    
    index->active_indexes |= NMO_INDEX_BUILD_NAME;
    return NMO_OK;
}

/**
 * Build GUID index
 */
static int build_guid_index(nmo_object_index_t *index) {
    if (index->guid_index != NULL) {
        nmo_hash_table_destroy(index->guid_index);
    }
    
    /* Create hash table: nmo_guid_t → object_array_t* */
    index->guid_index = nmo_hash_table_create(
        sizeof(nmo_guid_t),
        sizeof(object_array_t *),
        64,
        NULL,  /* Use default hash for now, will add nmo_hash_guid later */
        NULL
    );
    
    if (index->guid_index == NULL) {
        return NMO_ERR_NOMEM;
    }
    
    /* Get all objects from repository */
    size_t obj_count;
    nmo_object_t **objects = nmo_object_repository_get_all(index->repo, &obj_count);
    
    /* Group objects by GUID */
    for (size_t i = 0; i < obj_count; i++) {
        nmo_object_t *obj = objects[i];
        
        /* Skip objects with null GUID */
        if (nmo_guid_is_null(obj->type_guid)) {
            continue;
        }
        
        object_array_t *arr = NULL;
        
        /* Check if GUID already has an array */
        if (!nmo_hash_table_get(index->guid_index, &obj->type_guid, &arr)) {
            /* Create new array for this GUID */
            arr = object_array_create(4);
            if (arr == NULL) {
                return NMO_ERR_NOMEM;
            }
            nmo_hash_table_insert(index->guid_index, &obj->type_guid, &arr);
        }
        
        /* Add object to array */
        int result = object_array_add(arr, obj);
        if (result != NMO_OK) {
            return result;
        }
    }
    
    index->active_indexes |= NMO_INDEX_BUILD_GUID;
    return NMO_OK;
}

/* ==================== Public API ==================== */

/**
 * Create object index
 */
nmo_object_index_t *nmo_object_index_create(
    nmo_object_repository_t *repo,
    nmo_arena_t *arena
) {
    if (repo == NULL || arena == NULL) {
        return NULL;
    }
    
    nmo_object_index_t *index = (nmo_object_index_t *)malloc(sizeof(nmo_object_index_t));
    if (index == NULL) {
        return NULL;
    }
    
    index->repo = repo;
    index->arena = arena;
    index->active_indexes = 0;
    index->class_index = NULL;
    index->name_index = NULL;
    index->guid_index = NULL;
    index->last_query_result = NULL;
    index->last_query_count = 0;
    index->last_query_class = 0;
    
    return index;
}

/**
 * Destroy object index
 */
void nmo_object_index_destroy(nmo_object_index_t *index) {
    if (index == NULL) {
        return;
    }
    
    /* Destroy class index */
    if (index->class_index != NULL) {
        /* Need to iterate and free object arrays */
        /* TODO: Implement hash table iterator */
        nmo_hash_table_destroy(index->class_index);
    }
    
    /* Destroy name index */
    if (index->name_index != NULL) {
        nmo_hash_table_destroy(index->name_index);
    }
    
    /* Destroy GUID index */
    if (index->guid_index != NULL) {
        nmo_hash_table_destroy(index->guid_index);
    }
    
    free(index->last_query_result);
    free(index);
}

/**
 * Build indexes
 */
int nmo_object_index_build(nmo_object_index_t *index, uint32_t flags) {
    if (index == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    int result = NMO_OK;
    
    if (flags & NMO_INDEX_BUILD_CLASS) {
        result = build_class_index(index);
        if (result != NMO_OK) {
            return result;
        }
    }
    
    if (flags & NMO_INDEX_BUILD_NAME) {
        result = build_name_index(index);
        if (result != NMO_OK) {
            return result;
        }
    }
    
    if (flags & NMO_INDEX_BUILD_GUID) {
        result = build_guid_index(index);
        if (result != NMO_OK) {
            return result;
        }
    }
    
    return NMO_OK;
}

/**
 * Rebuild indexes
 */
int nmo_object_index_rebuild(nmo_object_index_t *index, uint32_t flags) {
    if (index == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    /* Clear specified indexes */
    int result = nmo_object_index_clear(index, flags);
    if (result != NMO_OK) {
        return result;
    }
    
    /* Rebuild */
    return nmo_object_index_build(index, flags);
}

/**
 * Add object to indexes (incremental update)
 */
int nmo_object_index_add_object(
    nmo_object_index_t *index,
    nmo_object_t *object,
    uint32_t flags
) {
    if (index == NULL || object == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    int result = NMO_OK;
    
    /* Add to class index */
    if ((flags & NMO_INDEX_BUILD_CLASS) && index->class_index != NULL) {
        object_array_t *arr = NULL;
        
        if (!nmo_hash_table_get(index->class_index, &object->class_id, &arr)) {
            arr = object_array_create(8);
            if (arr == NULL) {
                return NMO_ERR_NOMEM;
            }
            nmo_hash_table_insert(index->class_index, &object->class_id, &arr);
        }
        
        result = object_array_add(arr, object);
        if (result != NMO_OK) {
            return result;
        }
    }
    
    /* Add to name index */
    if ((flags & NMO_INDEX_BUILD_NAME) && index->name_index != NULL) {
        const char *name = nmo_object_get_name(object);
        if (name != NULL && name[0] != '\0') {
            object_array_t *arr = NULL;
            
            if (!nmo_hash_table_get(index->name_index, &name, &arr)) {
                arr = object_array_create(4);
                if (arr == NULL) {
                    return NMO_ERR_NOMEM;
                }
                nmo_hash_table_insert(index->name_index, &name, &arr);
            }
            
            result = object_array_add(arr, object);
            if (result != NMO_OK) {
                return result;
            }
        }
    }
    
    /* Add to GUID index */
    if ((flags & NMO_INDEX_BUILD_GUID) && index->guid_index != NULL) {
        if (!nmo_guid_is_null(object->type_guid)) {
            object_array_t *arr = NULL;
            
            if (!nmo_hash_table_get(index->guid_index, &object->type_guid, &arr)) {
                arr = object_array_create(4);
                if (arr == NULL) {
                    return NMO_ERR_NOMEM;
                }
                nmo_hash_table_insert(index->guid_index, &object->type_guid, &arr);
            }
            
            result = object_array_add(arr, object);
            if (result != NMO_OK) {
                return result;
            }
        }
    }
    
    return NMO_OK;
}

/**
 * Remove object from indexes
 */
int nmo_object_index_remove_object(
    nmo_object_index_t *index,
    nmo_object_id_t object_id,
    uint32_t flags
) {
    if (index == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    /* Find the object first */
    nmo_object_t *object = nmo_object_repository_find_by_id(index->repo, object_id);
    if (object == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    
    /* Remove from class index */
    if ((flags & NMO_INDEX_BUILD_CLASS) && index->class_index != NULL) {
        object_array_t *arr = NULL;
        if (nmo_hash_table_get(index->class_index, &object->class_id, &arr)) {
            object_array_remove(arr, object_id);
        }
    }
    
    /* Remove from name index */
    if ((flags & NMO_INDEX_BUILD_NAME) && index->name_index != NULL) {
        const char *name = nmo_object_get_name(object);
        if (name != NULL && name[0] != '\0') {
            object_array_t *arr = NULL;
            if (nmo_hash_table_get(index->name_index, &name, &arr)) {
                object_array_remove(arr, object_id);
            }
        }
    }
    
    /* Remove from GUID index */
    if ((flags & NMO_INDEX_BUILD_GUID) && index->guid_index != NULL) {
        if (!nmo_guid_is_null(object->type_guid)) {
            object_array_t *arr = NULL;
            if (nmo_hash_table_get(index->guid_index, &object->type_guid, &arr)) {
                object_array_remove(arr, object_id);
            }
        }
    }
    
    return NMO_OK;
}

/**
 * Clear indexes
 */
int nmo_object_index_clear(nmo_object_index_t *index, uint32_t flags) {
    if (index == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    if ((flags & NMO_INDEX_BUILD_CLASS) && index->class_index != NULL) {
        nmo_hash_table_destroy(index->class_index);
        index->class_index = NULL;
        index->active_indexes &= ~NMO_INDEX_BUILD_CLASS;
    }
    
    if ((flags & NMO_INDEX_BUILD_NAME) && index->name_index != NULL) {
        nmo_hash_table_destroy(index->name_index);
        index->name_index = NULL;
        index->active_indexes &= ~NMO_INDEX_BUILD_NAME;
    }
    
    if ((flags & NMO_INDEX_BUILD_GUID) && index->guid_index != NULL) {
        nmo_hash_table_destroy(index->guid_index);
        index->guid_index = NULL;
        index->active_indexes &= ~NMO_INDEX_BUILD_GUID;
    }
    
    return NMO_OK;
}

uint32_t nmo_object_index_get_active_flags(const nmo_object_index_t *index) {
    if (index == NULL) {
        return 0;
    }
    return index->active_indexes;
}

/* ==================== Class ID Lookup ==================== */

/**
 * Get objects by class ID
 */
nmo_object_t **nmo_object_index_get_by_class(
    const nmo_object_index_t *index,
    nmo_class_id_t class_id,
    size_t *out_count
) {
    if (index == NULL || out_count == NULL) {
        return NULL;
    }
    
    *out_count = 0;
    
    /* Use index if available */
    if (index->class_index != NULL) {
        object_array_t *arr = NULL;
        if (nmo_hash_table_get(index->class_index, &class_id, &arr)) {
            *out_count = arr->count;
            return arr->objects;
        }
        return NULL;
    }
    
    /* Fall back to linear search */
    return nmo_object_repository_find_by_class(index->repo, class_id, out_count);
}

/**
 * Count objects by class ID
 */
size_t nmo_object_index_count_by_class(
    const nmo_object_index_t *index,
    nmo_class_id_t class_id
) {
    size_t count = 0;
    nmo_object_index_get_by_class(index, class_id, &count);
    return count;
}

/* ==================== Name Lookup ==================== */

/**
 * Find object by name (exact match)
 */
nmo_object_t *nmo_object_index_find_by_name(
    const nmo_object_index_t *index,
    const char *name,
    nmo_class_id_t class_id
) {
    if (index == NULL || name == NULL) {
        return NULL;
    }
    
    /* Use index if available */
    if (index->name_index != NULL) {
        object_array_t *arr = NULL;
        if (nmo_hash_table_get(index->name_index, &name, &arr)) {
            /* Filter by class if specified */
            if (class_id != 0) {
                for (size_t i = 0; i < arr->count; i++) {
                    if (arr->objects[i]->class_id == class_id) {
                        return arr->objects[i];
                    }
                }
                return NULL;
            }
            /* Return first match */
            return arr->count > 0 ? arr->objects[0] : NULL;
        }
        return NULL;
    }
    
    /* Fall back to repository search */
    return nmo_object_repository_find_by_name(index->repo, name);
}

/**
 * Get all objects with a specific name
 */
nmo_object_t **nmo_object_index_get_by_name_all(
    const nmo_object_index_t *index,
    const char *name,
    nmo_class_id_t class_id,
    size_t *out_count
) {
    if (index == NULL || name == NULL || out_count == NULL) {
        return NULL;
    }
    
    *out_count = 0;
    
    if (index->name_index != NULL) {
        object_array_t *arr = NULL;
        if (nmo_hash_table_get(index->name_index, &name, &arr)) {
            /* No class filter */
            if (class_id == 0) {
                *out_count = arr->count;
                return arr->objects;
            }
            
            /* Filter by class - need to build temporary array */
            /* This is a simplified implementation - could be optimized */
            size_t matching = 0;
            for (size_t i = 0; i < arr->count; i++) {
                if (arr->objects[i]->class_id == class_id) {
                    matching++;
                }
            }
            
            if (matching == 0) {
                return NULL;
            }
            
            /* Allocate result array (caller responsible for freeing) */
            nmo_object_t **result = (nmo_object_t **)malloc(matching * sizeof(nmo_object_t *));
            if (result == NULL) {
                return NULL;
            }
            
            size_t idx = 0;
            for (size_t i = 0; i < arr->count; i++) {
                if (arr->objects[i]->class_id == class_id) {
                    result[idx++] = arr->objects[i];
                }
            }
            
            *out_count = matching;
            return result;
        }
    }
    
    return NULL;
}

/**
 * Find object by name (case-insensitive)
 */
nmo_object_t *nmo_object_index_find_by_name_fuzzy(
    const nmo_object_index_t *index,
    const char *name,
    nmo_class_id_t class_id
) {
    if (index == NULL || name == NULL) {
        return NULL;
    }
    
    /* Linear search with case-insensitive comparison */
    size_t obj_count;
    nmo_object_t **objects = nmo_object_repository_get_all(index->repo, &obj_count);
    
    for (size_t i = 0; i < obj_count; i++) {
        const char *obj_name = nmo_object_get_name(objects[i]);
        if (obj_name != NULL && strcasecmp_portable(obj_name, name) == 0) {
            if (class_id == 0 || objects[i]->class_id == class_id) {
                return objects[i];
            }
        }
    }
    
    return NULL;
}

/* ==================== GUID Lookup ==================== */

/**
 * Find object by GUID
 */
nmo_object_t *nmo_object_index_find_by_guid(
    const nmo_object_index_t *index,
    nmo_guid_t guid
) {
    if (index == NULL) {
        return NULL;
    }
    
    if (nmo_guid_is_null(guid)) {
        return NULL;
    }
    
    /* Use index if available */
    if (index->guid_index != NULL) {
        object_array_t *arr = NULL;
        if (nmo_hash_table_get(index->guid_index, &guid, &arr)) {
            return arr->count > 0 ? arr->objects[0] : NULL;
        }
        return NULL;
    }
    
    /* Fall back to linear search */
    size_t obj_count;
    nmo_object_t **objects = nmo_object_repository_get_all(index->repo, &obj_count);
    
    for (size_t i = 0; i < obj_count; i++) {
        if (nmo_guid_equals(objects[i]->type_guid, guid)) {
            return objects[i];
        }
    }
    
    return NULL;
}

/**
 * Get all objects with a specific GUID
 */
nmo_object_t **nmo_object_index_get_by_guid_all(
    const nmo_object_index_t *index,
    nmo_guid_t guid,
    size_t *out_count
) {
    if (index == NULL || out_count == NULL) {
        return NULL;
    }
    
    *out_count = 0;
    
    if (nmo_guid_is_null(guid)) {
        return NULL;
    }
    
    if (index->guid_index != NULL) {
        object_array_t *arr = NULL;
        if (nmo_hash_table_get(index->guid_index, &guid, &arr)) {
            *out_count = arr->count;
            return arr->objects;
        }
    }
    
    return NULL;
}

/* ==================== Statistics ==================== */

/**
 * Get index statistics
 */
int nmo_object_index_get_stats(
    const nmo_object_index_t *index,
    nmo_index_stats_t *stats
) {
    if (index == NULL || stats == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    memset(stats, 0, sizeof(nmo_index_stats_t));
    
    stats->total_objects = nmo_object_repository_get_count(index->repo);
    
    if (index->class_index != NULL) {
        stats->class_index_entries = nmo_hash_table_size(index->class_index);
    }
    
    if (index->name_index != NULL) {
        stats->name_index_entries = nmo_hash_table_size(index->name_index);
    }
    
    if (index->guid_index != NULL) {
        stats->guid_index_entries = nmo_hash_table_size(index->guid_index);
    }
    
    /* Approximate memory usage */
    stats->memory_usage = sizeof(nmo_object_index_t);
    stats->memory_usage += stats->class_index_entries * (sizeof(nmo_class_id_t) + sizeof(object_array_t *));
    stats->memory_usage += stats->name_index_entries * (sizeof(char *) + sizeof(object_array_t *));
    stats->memory_usage += stats->guid_index_entries * (sizeof(nmo_guid_t) + sizeof(object_array_t *));
    
    return NMO_OK;
}

/**
 * Check if class index is built
 */
int nmo_object_index_has_class_index(const nmo_object_index_t *index) {
    return index != NULL && index->class_index != NULL;
}

/**
 * Check if name index is built
 */
int nmo_object_index_has_name_index(const nmo_object_index_t *index) {
    return index != NULL && index->name_index != NULL;
}

/**
 * Check if GUID index is built
 */
int nmo_object_index_has_guid_index(const nmo_object_index_t *index) {
    return index != NULL && index->guid_index != NULL;
}
