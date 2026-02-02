/**
 * @file object_repository.c
 * @brief Object repository implementation with generic hash tables
 */

#include "session/nmo_object_repository.h"
#include "session/nmo_object_index.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include "core/nmo_indexed_map.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_hash.h"
#include "core/nmo_error.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 64

// Forward declaration for private helper
static nmo_object_id_t nmo_object_repository_allocate_id(nmo_object_repository_t *repo);

/**
 * Object repository structure
 */
typedef struct nmo_object_repository {
    nmo_arena_t *arena;

    /* ID indexed map (provides both hash lookup and iteration) */
    nmo_indexed_map_t *id_map; /* nmo_object_id_t -> nmo_object_t* */

    /* Name hash table (for name lookup only) */
    nmo_hash_table_t *name_table; /* const char* -> nmo_object_t* */

    /* Runtime ID allocator */
    nmo_object_id_t next_runtime_id;

    /* Optional attached index for incremental maintenance */
    nmo_object_index_t *attached_index;

    /* Scratch arrays for query results (caller must not free) */
    nmo_object_t **scratch_all;
    size_t scratch_all_capacity;
    nmo_object_t **scratch_class;
    size_t scratch_class_capacity;
} nmo_object_repository_t;

static uint32_t nmo_object_repository_get_active_index_flags(
    const nmo_object_repository_t *repo
) {
    if (repo == NULL || repo->attached_index == NULL) {
        return 0;
    }
    return nmo_object_index_get_active_flags(repo->attached_index);
}

static int nmo_object_repository_notify_add(
    nmo_object_repository_t *repo,
    nmo_object_t *obj
) {
    uint32_t flags = nmo_object_repository_get_active_index_flags(repo);
    if (flags == 0) {
        return NMO_OK;
    }
    return nmo_object_index_add_object(repo->attached_index, obj, flags);
}

static int nmo_object_repository_notify_remove(
    nmo_object_repository_t *repo,
    nmo_object_id_t id
) {
    uint32_t flags = nmo_object_repository_get_active_index_flags(repo);
    if (flags == 0) {
        return NMO_OK;
    }
    return nmo_object_index_remove_object(repo->attached_index, id, flags);
}

/**
 * Create object repository
 */
nmo_object_repository_t *nmo_object_repository_create(nmo_arena_t *arena) {
    if (arena == NULL) {
        return NULL;
    }

    nmo_object_repository_t *repo = (nmo_object_repository_t *) malloc(sizeof(nmo_object_repository_t));
    if (repo == NULL) {
        return NULL;
    }

    repo->arena = arena;

    /* Create ID indexed map */
    repo->id_map = nmo_indexed_map_create(
        arena,
        NULL,
        sizeof(nmo_object_id_t),
        sizeof(nmo_object_t *),
        INITIAL_CAPACITY,
        nmo_map_hash_uint32,
        NULL
    );

    if (repo->id_map == NULL) {
        free(repo);
        return NULL;
    }

    /* Create name hash table */
    repo->name_table = nmo_hash_table_create(
        NULL,
        sizeof(const char *),
        sizeof(nmo_object_t *),
        INITIAL_CAPACITY,
        nmo_hash_string,
        nmo_compare_string
    );

    if (repo->name_table == NULL) {
        nmo_indexed_map_destroy(repo->id_map);
        free(repo);
        return NULL;
    }

    repo->next_runtime_id = 1; /* Start from 1 (0 is invalid) */
    repo->attached_index = NULL;
    repo->scratch_all = NULL;
    repo->scratch_all_capacity = 0;
    repo->scratch_class = NULL;
    repo->scratch_class_capacity = 0;

    return repo;
}

/**
 * Destroy object repository
 */
void nmo_object_repository_destroy(nmo_object_repository_t *repo) {
    if (repo != NULL) {
        /* Note: Objects are owned by arena, don't free them here */
        nmo_indexed_map_destroy(repo->id_map);
        nmo_hash_table_destroy(repo->name_table);
        free(repo->scratch_all);
        free(repo->scratch_class);
        free(repo);
    }
}

void nmo_object_repository_set_index(
    nmo_object_repository_t *repo,
    nmo_object_index_t *index
) {
    if (repo == NULL) {
        return;
    }
    repo->attached_index = index;
}

/**
 * Add object
 */
int nmo_object_repository_add(nmo_object_repository_t *repo, nmo_object_t *obj) {
    if (repo == NULL || obj == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Allocate a new runtime ID if the object doesn't have one */
    if (obj->id == NMO_OBJECT_ID_NONE) {
        obj->id = nmo_object_repository_allocate_id(repo);
        if (obj->id == NMO_OBJECT_ID_NONE) {
            return NMO_ERR_NOMEM; // Should not happen unless repo is NULL
        }
    }

    /* Check if ID already exists */
    if (nmo_indexed_map_contains(repo->id_map, &obj->id)) {
        return NMO_ERR_INVALID_STATE;
    }

    /* Add to ID map */
    nmo_result_t insert_result = nmo_indexed_map_insert(repo->id_map, &obj->id, &obj);
    if (nmo_result_is_error(insert_result)) {
        return insert_result.code;
    }

    /* Add to name table if object has a name */
    if (obj->name != NULL && obj->name[0] != '\0') {
        nmo_result_t name_result = nmo_hash_table_insert(repo->name_table, &obj->name, &obj);
        if (nmo_result_is_error(name_result)) {
            /* Rollback ID insertion */
            nmo_indexed_map_remove(repo->id_map, &obj->id);
            return name_result.code;
        }
    }

    int result = nmo_object_repository_notify_add(repo, obj);
    if (result != NMO_OK) {
        /* Keep structures consistent if index update fails */
        if (obj->name != NULL && obj->name[0] != '\0') {
            nmo_hash_table_remove(repo->name_table, &obj->name);
        }
        nmo_indexed_map_remove(repo->id_map, &obj->id);
        return result;
    }

    return NMO_OK;
}

/**
 * Find object by ID
 */
nmo_object_t *nmo_object_repository_find_by_id(const nmo_object_repository_t *repo,
                                               nmo_object_id_t id) {
    if (repo == NULL) {
        return NULL;
    }

    nmo_object_t *obj = NULL;
    if (nmo_result_is_ok(nmo_indexed_map_get(repo->id_map, &id, &obj))) {
        return obj;
    }

    return NULL;
}

/**
 * Find object by name
 */
nmo_object_t *nmo_object_repository_find_by_name(const nmo_object_repository_t *repo,
                                                 const char *name) {
    if (repo == NULL || name == NULL) {
        return NULL;
    }

    nmo_object_t *obj = NULL;
    if (nmo_result_is_ok(nmo_hash_table_get(repo->name_table, &name, &obj))) {
        return obj;
    }

    return NULL;
}

/**
 * Remove object
 */
int nmo_object_repository_remove(nmo_object_repository_t *repo, nmo_object_id_t id) {
    if (repo == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Get object before removing */
    nmo_object_t *obj = NULL;
    if (nmo_result_is_error(nmo_indexed_map_get(repo->id_map, &id, &obj))) {
        return NMO_ERR_INVALID_ARGUMENT; /* Not found */
    }

    int result = nmo_object_repository_notify_remove(repo, id);
    if (result != NMO_OK) {
        return result;
    }

    /* Remove from name table if has name */
    if (obj != NULL && obj->name != NULL && obj->name[0] != '\0') {
        nmo_object_t *mapped = NULL;
        if (nmo_result_is_ok(nmo_hash_table_get(repo->name_table, &obj->name, &mapped))
            && mapped == obj) {
            nmo_object_t *replacement = NULL;
            size_t total_count = nmo_indexed_map_get_count(repo->id_map);
            for (size_t i = 0; i < total_count; i++) {
                nmo_object_t *candidate = NULL;
                if (nmo_indexed_map_get_value_at(repo->id_map, i, &candidate)
                    && candidate != obj
                    && candidate->name != NULL
                    && strcmp(candidate->name, obj->name) == 0) {
                    replacement = candidate;
                    break;
                }
            }
            if (replacement != NULL) {
                nmo_hash_table_insert(repo->name_table, &replacement->name, &replacement);
            } else {
                nmo_hash_table_remove(repo->name_table, &obj->name);
            }
        }
    }

    /* Remove from ID map */
    nmo_indexed_map_remove(repo->id_map, &id);

    return NMO_OK;
}

/**
 * Check if object exists
 */
int nmo_object_repository_contains(const nmo_object_repository_t *repo, nmo_object_id_t id) {
    if (repo == NULL) {
        return 0;
    }

    return nmo_indexed_map_contains(repo->id_map, &id);
}

/**
 * Get object count
 */
size_t nmo_object_repository_get_count(const nmo_object_repository_t *repo) {
    if (repo == NULL) {
        return 0;
    }

    return nmo_indexed_map_get_count(repo->id_map);
}

/**
 * Get object at index
 */
nmo_object_t *nmo_object_repository_get_at(const nmo_object_repository_t *repo, size_t index) {
    if (repo == NULL) {
        return NULL;
    }

    nmo_object_t *obj = NULL;
    if (nmo_indexed_map_get_value_at(repo->id_map, index, &obj)) {
        return obj;
    }

    return NULL;
}

/**
 * Get all objects
 */
nmo_object_t **nmo_object_repository_get_all(nmo_object_repository_t *repo, size_t *count) {
    if (repo == NULL || count == NULL) {
        if (count != NULL) *count = 0;
        return NULL;
    }

    size_t obj_count = nmo_indexed_map_get_count(repo->id_map);
    if (obj_count == 0) {
        *count = 0;
        return NULL;
    }

    if (repo->scratch_all_capacity < obj_count) {
        /* Check for multiplication overflow */
        if (obj_count > SIZE_MAX / sizeof(nmo_object_t *)) {
            *count = 0;
            return NULL;
        }
        nmo_object_t **new_objects = (nmo_object_t **)realloc(
            repo->scratch_all,
            obj_count * sizeof(nmo_object_t *));
        if (new_objects == NULL) {
            *count = 0;
            return NULL;
        }
        repo->scratch_all = new_objects;
        repo->scratch_all_capacity = obj_count;
    }

    /* Fill array */
    for (size_t i = 0; i < obj_count; i++) {
        nmo_object_t *obj = NULL;
        if (nmo_indexed_map_get_value_at(repo->id_map, i, &obj)) {
            repo->scratch_all[i] = obj;
        } else {
            repo->scratch_all[i] = NULL;
        }
    }

    *count = obj_count;
    return repo->scratch_all;
}

/**
 * Allocate new runtime ID
 */
static nmo_object_id_t nmo_object_repository_allocate_id(nmo_object_repository_t *repo) {
    if (repo == NULL) {
        return NMO_OBJECT_ID_NONE;
    }

    nmo_object_id_t start = repo->next_runtime_id;
    if (start == NMO_OBJECT_ID_NONE) {
        start = 1;
    }

    nmo_object_id_t id = start;
    for (;;) {
        if (!nmo_indexed_map_contains(repo->id_map, &id)) {
            nmo_object_id_t next = id + 1;
            if (next == NMO_OBJECT_ID_NONE) {
                next = 1;
            }
            repo->next_runtime_id = next;
            return id;
        }

        id++;
        if (id == NMO_OBJECT_ID_NONE) {
            id = 1;
        }
        if (id == start) {
            return NMO_OBJECT_ID_NONE;
        }
    }
}

/**
 * Clear repository
 */
int nmo_object_repository_clear(nmo_object_repository_t *repo) {
    if (repo == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_indexed_map_clear(repo->id_map);
    nmo_hash_table_clear(repo->name_table);
    repo->next_runtime_id = 1;

    if (repo->attached_index != NULL) {
        int result = nmo_object_index_clear(repo->attached_index, NMO_INDEX_BUILD_ALL);
        if (result != NMO_OK) {
            return result;
        }
    }

    return NMO_OK;
}

/**
 * Get arena
 */
nmo_arena_t *nmo_object_repository_get_arena(const nmo_object_repository_t *repo) {
    return repo ? repo->arena : NULL;
}

/**
 * Find objects by class
 */
nmo_object_t **nmo_object_repository_find_by_class(nmo_object_repository_t *repo,
                                                           nmo_class_id_t class_id,
                                                           size_t *out_count) {
    if (repo == NULL || out_count == NULL) {
        if (out_count != NULL) *out_count = 0;
        return NULL;
    }

    size_t total_count = nmo_indexed_map_get_count(repo->id_map);
    if (total_count == 0) {
        *out_count = 0;
        return NULL;
    }

    // First pass: count matching objects
    size_t match_count = 0;
    for (size_t i = 0; i < total_count; i++) {
        nmo_object_t *obj = NULL;
        if (nmo_indexed_map_get_value_at(repo->id_map, i, &obj) && obj->class_id == class_id) {
            match_count++;
        }
    }

    if (match_count == 0) {
        *out_count = 0;
        return NULL;
    }

    if (repo->scratch_class_capacity < match_count) {
        /* Check for multiplication overflow */
        if (match_count > SIZE_MAX / sizeof(nmo_object_t *)) {
            *out_count = 0;
            return NULL;
        }
        nmo_object_t **new_objects = (nmo_object_t **)realloc(
            repo->scratch_class,
            match_count * sizeof(nmo_object_t *));
        if (new_objects == NULL) {
            *out_count = 0;
            return NULL;
        }
        repo->scratch_class = new_objects;
        repo->scratch_class_capacity = match_count;
    }

    // Second pass: fill the array
    size_t current_match = 0;
    for (size_t i = 0; i < total_count; i++) {
        nmo_object_t *obj = NULL;
        if (nmo_indexed_map_get_value_at(repo->id_map, i, &obj) && obj->class_id == class_id) {
            repo->scratch_class[current_match++] = obj;
        }
    }

    *out_count = match_count;
    return repo->scratch_class;
}

/**
 * Get object by index (alias for get_at to match header declaration)
 */
nmo_object_t *nmo_object_repository_get_by_index(const nmo_object_repository_t *repo, size_t index) {
    return nmo_object_repository_get_at(repo, index);
}
