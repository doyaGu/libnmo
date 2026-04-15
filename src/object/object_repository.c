/**
 * @file object_repository.c
 * @brief Object repository implementation with generic hash tables
 */

#include "object/nmo_object_repository.h"
#include "object/nmo_object_index.h"
#include "format/nmo_object.h"
#include "core/nmo_indexed_map.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_hash.h"
#include "core/nmo_container_lifecycle.h"
#include "core/nmo_error.h"
#include <ctype.h>
#include <string.h>

#define INITIAL_CAPACITY 64

// Forward declaration for private helper
static nmo_object_id_t nmo_object_repository_allocate_id(nmo_object_repository_t *repo);

/**
 * Object repository structure
 */
typedef struct nmo_object_repository {
    nmo_allocator_t base_allocator;
    nmo_allocator_t allocator;
    nmo_allocator_debug_t allocator_debug;

    /* ID indexed map (provides both hash lookup and iteration) */
    nmo_indexed_map_t *id_map; /* nmo_object_id_t -> nmo_object_t* */

    /* Name hash table (for name lookup only) */
    nmo_hash_table_t *name_table; /* const char* -> nmo_object_t* */

    /* File ID hash table (for original CK_ID lookup) */
    nmo_hash_table_t *file_id_table; /* nmo_object_id_t -> nmo_object_t* */

    /* Runtime ID allocator */
    nmo_object_id_t next_runtime_id;

    /* Optional attached index for incremental maintenance */
    nmo_object_index_t *attached_index;

    /* Scratch arrays for query results (caller must not free).
     * OWNERSHIP: repo allocator, valid until next query or destroy.
     */
    nmo_object_t **scratch_all;
    size_t scratch_all_capacity;
    nmo_object_t **scratch_class;
    size_t scratch_class_capacity;
} nmo_object_repository_t;

static void nmo_object_repository_dispose_object_ptr(void *element, void *user_data) {
    (void)user_data;
    if (element == NULL) {
        return;
    }

    nmo_object_t *obj = *(nmo_object_t **)element;
    if (obj != NULL) {
        nmo_object_destroy(obj);
    }
}

static void nmo_object_repository_set_owning_lifecycle(nmo_object_repository_t *repo) {
    if (repo == NULL || repo->id_map == NULL) {
        return;
    }

    nmo_container_lifecycle_t value_lifecycle = NMO_CONTAINER_LIFECYCLE_INIT;
    value_lifecycle.dispose = nmo_object_repository_dispose_object_ptr;
    nmo_indexed_map_set_lifecycle(repo->id_map, NULL, &value_lifecycle);
}

static int nmo_object_repository_remove_without_dispose(
    nmo_object_repository_t *repo,
    nmo_object_id_t id
) {
    if (repo == NULL || repo->id_map == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Temporarily disable value disposal so rollback keeps caller ownership. */
    nmo_container_lifecycle_t non_owning = NMO_CONTAINER_LIFECYCLE_INIT;
    nmo_indexed_map_set_lifecycle(repo->id_map, NULL, &non_owning);
    nmo_status_t remove_result = nmo_indexed_map_remove(repo->id_map, &id);
    nmo_object_repository_set_owning_lifecycle(repo);
    return remove_result;
}

static int nmo_object_repository_scratch_reserve(
    nmo_object_repository_t *repo,
    nmo_object_t ***scratch,
    size_t *capacity,
    size_t required
) {
    if (repo == NULL || scratch == NULL || capacity == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (required <= *capacity) {
        return NMO_OK;
    }

    if (required > SIZE_MAX / sizeof(nmo_object_t *)) {
        return NMO_ERR_NOMEM;
    }

    size_t new_capacity = required;
    nmo_object_t **new_buf = (nmo_object_t **)nmo_alloc(
        &repo->allocator,
        new_capacity * sizeof(nmo_object_t *),
        _Alignof(nmo_object_t *)
    );
    if (new_buf == NULL) {
        return NMO_ERR_NOMEM;
    }

    if (*scratch != NULL && *capacity > 0) {
        size_t copy_count = (*capacity < new_capacity) ? *capacity : new_capacity;
        memcpy(new_buf, *scratch, copy_count * sizeof(nmo_object_t *));
        nmo_free(&repo->allocator, *scratch);
    }

    *scratch = new_buf;
    *capacity = new_capacity;
    return NMO_OK;
}

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

static void nmo_object_repository_unlink_name(
    nmo_object_repository_t *repo,
    nmo_object_t *obj
) {
    if (repo == NULL || obj == NULL || obj->name == NULL || obj->name[0] == '\0') {
        return;
    }

    nmo_object_t *mapped = NULL;
    if (nmo_hash_table_get(repo->name_table, &obj->name, &mapped) != NMO_OK ||
        mapped != obj) {
        return;
    }

    nmo_object_t *replacement = NULL;
    size_t total_count = nmo_indexed_map_get_count(repo->id_map);
    for (size_t i = 0; i < total_count; i++) {
        nmo_object_t *candidate = NULL;
        if (nmo_indexed_map_get_value_at(repo->id_map, i, &candidate) &&
            candidate != obj &&
            candidate->name != NULL &&
            strcmp(candidate->name, obj->name) == 0) {
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

/**
 * Create object repository
 */
nmo_object_repository_t *nmo_object_repository_create(const nmo_allocator_t *allocator) {
    nmo_allocator_t snapshot = (allocator != NULL) ? *allocator : nmo_allocator_default();

    nmo_object_repository_t *repo = (nmo_object_repository_t *)nmo_alloc(
        &snapshot,
        sizeof(nmo_object_repository_t),
        _Alignof(nmo_object_repository_t)
    );
    if (repo == NULL) {
        return NULL;
    }

    memset(repo, 0, sizeof(*repo));
    repo->base_allocator = snapshot;
    repo->allocator = nmo_allocator_debug_init(
        &repo->allocator_debug,
        snapshot,
        "object_repository",
        "repo_allocator");

    /* Create ID indexed map */
    repo->id_map = nmo_indexed_map_create(
        NULL,
        &repo->allocator,
        sizeof(nmo_object_id_t),
        sizeof(nmo_object_t *),
        INITIAL_CAPACITY,
        nmo_map_hash_uint32,
        NULL
    );

    if (repo->id_map == NULL) {
        nmo_free(&repo->base_allocator, repo);
        return NULL;
    }

    nmo_object_repository_set_owning_lifecycle(repo);

    /* Create name hash table */
    repo->name_table = nmo_hash_table_create(
        &repo->allocator,
        sizeof(const char *),
        sizeof(nmo_object_t *),
        INITIAL_CAPACITY,
        nmo_hash_string,
        nmo_compare_string
    );

    if (repo->name_table == NULL) {
        nmo_indexed_map_destroy(repo->id_map);
        nmo_free(&repo->base_allocator, repo);
        return NULL;
    }

    /* Create file ID hash table */
    repo->file_id_table = nmo_hash_table_create(
        &repo->allocator,
        sizeof(nmo_object_id_t),
        sizeof(nmo_object_t *),
        INITIAL_CAPACITY,
        nmo_hash_uint32,
        NULL
    );

    if (repo->file_id_table == NULL) {
        nmo_hash_table_destroy(repo->name_table);
        nmo_indexed_map_destroy(repo->id_map);
        nmo_free(&repo->base_allocator, repo);
        return NULL;
    }

    repo->next_runtime_id = 1; /* Start from 1 (0 is invalid) */
    return repo;
}

/**
 * Destroy object repository
 */
void nmo_object_repository_destroy(nmo_object_repository_t *repo) {
    if (repo != NULL) {
        nmo_indexed_map_destroy(repo->id_map);
        nmo_hash_table_destroy(repo->name_table);
        nmo_hash_table_destroy(repo->file_id_table);
        nmo_free(&repo->allocator, repo->scratch_all);
        nmo_free(&repo->allocator, repo->scratch_class);
        nmo_free(&repo->base_allocator, repo);
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
int nmo_object_repository_add(nmo_object_repository_t *repo, nmo_object_t **obj_ref) {
    if (repo == NULL || obj_ref == NULL || *obj_ref == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_t *obj = *obj_ref;

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
    nmo_status_t insert_result = nmo_indexed_map_insert(repo->id_map, &obj->id, &obj);
    if (insert_result != NMO_OK) {
        return insert_result;
    }

    /* Add to name table if object has a name */
    if (obj->name != NULL && obj->name[0] != '\0') {
        nmo_status_t name_result = nmo_hash_table_insert(repo->name_table, &obj->name, &obj);
        if (name_result != NMO_OK) {
            /* Rollback ID insertion */
            nmo_status_t rollback_result = nmo_object_repository_remove_without_dispose(repo, obj->id);
            if (rollback_result != NMO_OK) {
                return rollback_result;
            }
            return name_result;
        }
    }

    /* Add to file ID table if object has a file ID */
    if (obj->file_id != 0) {
        nmo_hash_table_insert(repo->file_id_table, &obj->file_id, &obj);
    }

    int result = nmo_object_repository_notify_add(repo, obj);
    if (result != NMO_OK) {
        /* Keep structures consistent if index update fails */
        if (obj->name != NULL && obj->name[0] != '\0') {
            nmo_hash_table_remove(repo->name_table, &obj->name);
        }
        nmo_status_t rollback_result = nmo_object_repository_remove_without_dispose(repo, obj->id);
        if (rollback_result != NMO_OK) {
            return rollback_result;
        }
        return result;
    }

    *obj_ref = NULL;
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
    if (nmo_indexed_map_get(repo->id_map, &id, &obj) == NMO_OK) {
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
    if (nmo_hash_table_get(repo->name_table, &name, &obj) == NMO_OK) {
        return obj;
    }

    return NULL;
}

/**
 * Find object by file ID (original CK_ID from file)
 */
nmo_object_t *nmo_object_repository_find_by_file_id(const nmo_object_repository_t *repo,
                                                     nmo_object_id_t file_id) {
    if (repo == NULL || file_id == 0) {
        return NULL;
    }

    nmo_object_t *obj = NULL;
    if (nmo_hash_table_get(repo->file_id_table, &file_id, &obj) == NMO_OK) {
        return obj;
    }

    return NULL;
}

/**
 * Remove object
 */
int nmo_object_repository_take(
    nmo_object_repository_t *repo,
    nmo_object_id_t id,
    nmo_object_t **out_object
) {
    if (repo == NULL || out_object == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_object = NULL;

    /* Get object before removing */
    nmo_object_t *obj = NULL;
    if (nmo_indexed_map_get(repo->id_map, &id, &obj) != NMO_OK || obj == NULL) {
        return NMO_ERR_INVALID_ARGUMENT; /* Not found */
    }

    int result = nmo_object_repository_notify_remove(repo, id);
    if (result != NMO_OK) {
        return result;
    }

    nmo_object_repository_unlink_name(repo, obj);

    /* Remove from file ID table */
    if (obj->file_id != 0) {
        nmo_hash_table_remove(repo->file_id_table, &obj->file_id);
    }

    result = nmo_object_repository_remove_without_dispose(repo, id);
    if (result != NMO_OK) {
        return result;
    }

    *out_object = obj;
    return NMO_OK;
}

int nmo_object_repository_remove(nmo_object_repository_t *repo, nmo_object_id_t id) {
    nmo_object_t *obj = NULL;
    int result = nmo_object_repository_take(repo, id, &obj);
    if (result != NMO_OK) {
        return result;
    }

    nmo_object_destroy(obj);
    return NMO_OK;
}

/**
 * Rename an object already in the repository.
 *
 * Removes the old name-table entry first (while the key pointer is still
 * valid), then delegates to nmo_object_set_name(), and finally re-inserts
 * with the new name.  This avoids the use-after-free that would occur if
 * nmo_object_set_name() were called directly on a repository-owned object.
 */
int nmo_object_repository_rename(nmo_object_repository_t *repo,
                                 nmo_object_id_t id,
                                 const char *new_name) {
    if (repo == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_t *obj = NULL;
    if (nmo_indexed_map_get(repo->id_map, &id, &obj) != NMO_OK || obj == NULL) {
        return NMO_ERR_NOT_FOUND;
    }

    /* 1. Remove old name from table (key still valid at this point) */
    if (obj->name != NULL && obj->name[0] != '\0') {
        nmo_object_t *mapped = NULL;
        if (nmo_hash_table_get(repo->name_table, &obj->name, &mapped) == NMO_OK
            && mapped == obj) {
            nmo_hash_table_remove(repo->name_table, &obj->name);
        }
    }

    /* 2. Update the object's name (frees old, allocates new) */
    int set_result = nmo_object_set_name(obj, new_name);
    if (set_result != NMO_OK) {
        return set_result;
    }

    /* 3. Re-insert with new name if non-empty */
    if (new_name != NULL && new_name[0] != '\0') {
        nmo_status_t ins = nmo_hash_table_insert(repo->name_table, &obj->name, &obj);
        if (ins != NMO_OK) {
            return ins;
        }
    }

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

    if (nmo_object_repository_scratch_reserve(
            repo,
            &repo->scratch_all,
            &repo->scratch_all_capacity,
            obj_count
        ) != NMO_OK) {
        *count = 0;
        return NULL;
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

    if (repo->attached_index != NULL) {
        int result = nmo_object_index_clear(repo->attached_index, NMO_INDEX_BUILD_ALL);
        if (result != NMO_OK) {
            return result;
        }
    }

    nmo_indexed_map_clear(repo->id_map);
    nmo_hash_table_clear(repo->name_table);
    nmo_hash_table_clear(repo->file_id_table);
    repo->next_runtime_id = 1;

    return NMO_OK;
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

    if (nmo_object_repository_scratch_reserve(
            repo,
            &repo->scratch_class,
            &repo->scratch_class_capacity,
            match_count
        ) != NMO_OK) {
        *out_count = 0;
        return NULL;
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

/* ============================================================================
 * Filtered iteration
 * ============================================================================ */

/**
 * Iterative wildcard matcher supporting '*' and '?' — O(n*m) worst case.
 * Uses backtracking single-pass algorithm (no recursion).
 */
static bool wildcard_match_impl(const char *pattern, const char *text, bool icase) {
    if (!pattern || !*pattern) return true;
    if (!text) text = "";

    const char *star = NULL;
    const char *ss = text;
    while (*text) {
        char pc = icase ? (char)tolower((unsigned char)*pattern) : *pattern;
        char tc = icase ? (char)tolower((unsigned char)*text) : *text;
        if (pc == '?' || pc == tc) {
            pattern++;
            text++;
        } else if (*pattern == '*') {
            star = pattern++;
            ss = text;
        } else if (star) {
            pattern = star + 1;
            text = ++ss;
        } else {
            return false;
        }
    }
    while (*pattern == '*') pattern++;
    return !*pattern;
}

static bool wildcard_match(const char *pattern, const char *text) {
    return wildcard_match_impl(pattern, text, false);
}

static bool wildcard_match_ci(const char *pattern, const char *text) {
    return wildcard_match_impl(pattern, text, true);
}

/**
 * Case-insensitive substring search.
 */
static bool substr_ci(const char *haystack, const char *needle) {
    if (!haystack || !needle) return false;
    size_t nlen = strlen(needle);
    if (nlen == 0) return true;
    size_t hlen = strlen(haystack);
    if (nlen > hlen) return false;
    for (size_t i = 0; i <= hlen - nlen; ++i) {
        bool match = true;
        for (size_t j = 0; j < nlen; ++j) {
            if (tolower((unsigned char)haystack[i + j]) !=
                tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

nmo_status_t nmo_object_repository_iter_filtered(
    nmo_object_repository_t *repo,
    const nmo_object_filter_t *filter,
    nmo_object_visitor_fn visitor,
    void *user_data)
{
    if (!repo) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t count = nmo_indexed_map_get_count(repo->id_map);

    for (size_t i = 0; i < count; ++i) {
        nmo_object_t *obj = NULL;
        if (!nmo_indexed_map_get_value_at(repo->id_map, i, &obj) || !obj) {
            continue;
        }

        if (filter) {
            /* Class filter */
            if (filter->class_id != 0) {
                if (obj->class_id != filter->class_id) continue;
            }

            /* Name pattern (wildcard) */
            if (filter->name_pattern) {
                const char *name = nmo_object_get_name(obj);
                if (filter->case_insensitive) {
                    if (!wildcard_match_ci(filter->name_pattern, name ? name : ""))
                        continue;
                } else {
                    if (!wildcard_match(filter->name_pattern, name ? name : ""))
                        continue;
                }
            }

            /* Name substring */
            if (filter->name_substring) {
                const char *name = nmo_object_get_name(obj);
                if (filter->case_insensitive) {
                    if (!substr_ci(name, filter->name_substring))
                        continue;
                } else {
                    if (!name || !strstr(name, filter->name_substring))
                        continue;
                }
            }
        }

        if (visitor) {
            if (!visitor(obj, user_data)) break;
        }
    }

    return NMO_OK;
}
