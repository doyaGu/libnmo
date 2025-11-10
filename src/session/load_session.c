/**
 * @file load_session.c
 * @brief Load session implementation for object ID remapping
 */

#include "session/nmo_load_session.h"
#include "session/nmo_object_repository.h"
#include "format/nmo_object.h"
#include <stdlib.h>
#include <string.h>

/**
 * ID mapping entry for hash table
 */
typedef struct {
    nmo_object_id file_id;
    nmo_object_id runtime_id;
    int occupied;
} id_mapping_entry;

/**
 * Load session structure
 */
struct nmo_load_session {
    nmo_object_repository* repo;
    nmo_object_id saved_id_max;
    nmo_object_id id_base;

    /* Hash table for file ID to runtime ID mapping */
    id_mapping_entry* mappings;
    size_t capacity;
    size_t count;

    int active;
};

/**
 * Hash function for IDs
 */
static inline size_t hash_id(nmo_object_id id, size_t capacity) {
    return (id * 2654435761U) % capacity;
}

/**
 * Find mapping slot
 */
static int find_mapping_slot(const nmo_load_session* session,
                              nmo_object_id file_id,
                              int* found) {
    size_t index = hash_id(file_id, session->capacity);
    size_t start = index;

    do {
        if (!session->mappings[index].occupied) {
            *found = 0;
            return (int)index;
        }
        if (session->mappings[index].file_id == file_id) {
            *found = 1;
            return (int)index;
        }
        index = (index + 1) % session->capacity;
    } while (index != start);

    *found = 0;
    return -1;
}

/**
 * Resize mapping table
 */
static int resize_mapping_table(nmo_load_session* session, size_t new_capacity) {
    id_mapping_entry* new_mappings = (id_mapping_entry*)calloc(new_capacity,
                                                                 sizeof(id_mapping_entry));
    if (new_mappings == NULL) {
        return NMO_ERR_NOMEM;
    }

    id_mapping_entry* old_mappings = session->mappings;
    size_t old_capacity = session->capacity;

    session->mappings = new_mappings;
    session->capacity = new_capacity;
    size_t old_count = session->count;
    session->count = 0;

    /* Rehash */
    for (size_t i = 0; i < old_capacity; i++) {
        if (old_mappings[i].occupied) {
            int found;
            int slot = find_mapping_slot(session, old_mappings[i].file_id, &found);
            if (slot >= 0) {
                session->mappings[slot].file_id = old_mappings[i].file_id;
                session->mappings[slot].runtime_id = old_mappings[i].runtime_id;
                session->mappings[slot].occupied = 1;
                session->count++;
            }
        }
    }

    free(old_mappings);
    return (session->count == old_count) ? NMO_OK : NMO_ERR_INVALID_STATE;
}

/**
 * Start load session
 */
nmo_load_session* nmo_load_session_start(nmo_object_repository* repo,
                                          nmo_object_id max_saved_id) {
    if (repo == NULL) {
        return NULL;
    }

    nmo_load_session* session = (nmo_load_session*)malloc(sizeof(nmo_load_session));
    if (session == NULL) {
        return NULL;
    }

    /* Initialize mapping table */
    size_t initial_capacity = (max_saved_id > 64) ? (max_saved_id * 2) : 64;
    session->mappings = (id_mapping_entry*)calloc(initial_capacity,
                                                    sizeof(id_mapping_entry));
    if (session->mappings == NULL) {
        free(session);
        return NULL;
    }

    session->repo = repo;
    session->saved_id_max = max_saved_id;
    session->capacity = initial_capacity;
    session->count = 0;
    session->active = 1;

    /* Allocate ID base
     * This ensures file IDs don't conflict with existing runtime IDs.
     * We allocate a range starting from current max + 1.
     */
    size_t existing_count = nmo_object_repository_get_count(repo);
    if (existing_count > 0) {
        /* Find max existing ID */
        size_t count;
        nmo_object** objects = nmo_object_repository_get_all(repo, &count);
        nmo_object_id max_id = 0;
        for (size_t i = 0; i < count; i++) {
            if (objects[i]->id > max_id) {
                max_id = objects[i]->id;
            }
        }
        session->id_base = max_id + 1;
    } else {
        session->id_base = 1;  /* Start from 1 (0 is invalid) */
    }

    return session;
}

/**
 * Register object with file ID
 */
int nmo_load_session_register(nmo_load_session* session,
                               nmo_object* obj,
                               nmo_object_id file_id) {
    if (session == NULL || obj == NULL || !session->active) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Resize if needed */
    if ((session->count + 1) * 10 > session->capacity * 7) {
        int result = resize_mapping_table(session, session->capacity * 2);
        if (result != NMO_OK) {
            return result;
        }
    }

    /* Add mapping */
    int found;
    int slot = find_mapping_slot(session, file_id, &found);
    if (slot < 0) {
        return NMO_ERR_NOMEM;
    }
    if (found) {
        return NMO_ERR_INVALID_STATE;  // Already registered
    }

    session->mappings[slot].file_id = file_id;
    session->mappings[slot].runtime_id = obj->id;
    session->mappings[slot].occupied = 1;
    session->count++;

    return NMO_OK;
}

/**
 * End load session
 */
int nmo_load_session_end(nmo_load_session* session) {
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    session->active = 0;
    return NMO_OK;
}

/**
 * Get object repository
 */
nmo_object_repository* nmo_load_session_get_repository(
    const nmo_load_session* session) {
    return session ? session->repo : NULL;
}

/**
 * Get ID base
 */
nmo_object_id nmo_load_session_get_id_base(const nmo_load_session* session) {
    return session ? session->id_base : 0;
}

/**
 * Get max saved ID
 */
nmo_object_id nmo_load_session_get_max_saved_id(const nmo_load_session* session) {
    return session ? session->saved_id_max : 0;
}

/**
 * Destroy load session
 */
void nmo_load_session_destroy(nmo_load_session* session) {
    if (session != NULL) {
        free(session->mappings);
        free(session);
    }
}

/**
 * Get runtime ID for file ID (internal helper for id_remap.c)
 */
int nmo_load_session_lookup_runtime_id(const nmo_load_session* session,
                                        nmo_object_id file_id,
                                        nmo_object_id* runtime_id) {
    if (session == NULL || runtime_id == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int found;
    int slot = find_mapping_slot(session, file_id, &found);
    if (slot >= 0 && found) {
        *runtime_id = session->mappings[slot].runtime_id;
        return NMO_OK;
    }

    return NMO_ERR_INVALID_ARGUMENT;  // Not found
}

/**
 * Get all mappings (internal helper for id_remap.c)
 */
int nmo_load_session_get_mappings(const nmo_load_session* session,
                                   nmo_object_id** file_ids,
                                   nmo_object_id** runtime_ids,
                                   size_t* count) {
    if (session == NULL || file_ids == NULL || runtime_ids == NULL || count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (session->count == 0) {
        *file_ids = NULL;
        *runtime_ids = NULL;
        *count = 0;
        return NMO_OK;
    }

    /* Allocate arrays */
    nmo_object_id* fids = (nmo_object_id*)malloc(session->count * sizeof(nmo_object_id));
    nmo_object_id* rids = (nmo_object_id*)malloc(session->count * sizeof(nmo_object_id));

    if (fids == NULL || rids == NULL) {
        free(fids);
        free(rids);
        return NMO_ERR_NOMEM;
    }

    /* Collect mappings */
    size_t idx = 0;
    for (size_t i = 0; i < session->capacity && idx < session->count; i++) {
        if (session->mappings[i].occupied) {
            fids[idx] = session->mappings[i].file_id;
            rids[idx] = session->mappings[i].runtime_id;
            idx++;
        }
    }

    *file_ids = fids;
    *runtime_ids = rids;
    *count = session->count;

    return NMO_OK;
}
