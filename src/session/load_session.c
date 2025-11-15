/**
 * @file load_session.c
 * @brief Load session implementation for object ID remapping
 */

#include "session/nmo_load_session.h"
#include "session/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_hash.h"
#include "core/nmo_error.h"
#include <stdlib.h>
#include <string.h>

/**
 * Load session structure
 */
typedef struct nmo_load_session {
    nmo_object_repository_t *repo;
    nmo_object_id_t saved_id_max;
    nmo_object_id_t id_base;

    /* Hash table for file ID to runtime ID mapping */
    nmo_hash_table_t *id_mappings;

    int active;
} nmo_load_session_t;

/**
 * Start load session
 */
nmo_load_session_t *nmo_load_session_start(nmo_object_repository_t *repo,
                                           nmo_object_id_t max_saved_id) {
    if (repo == NULL) {
        return NULL;
    }

    nmo_arena_t *arena = nmo_object_repository_get_arena(repo);
    nmo_load_session_t *session = (nmo_load_session_t *) malloc(sizeof(nmo_load_session_t));
    if (session == NULL) {
        return NULL;
    }

    /* Initialize mapping table using generic hash table */
    size_t initial_capacity = (max_saved_id > 64) ? (max_saved_id * 2) : 64;
    session->id_mappings = nmo_hash_table_create(
        NULL,
        sizeof(nmo_object_id_t),    /* key: file_id */
        sizeof(nmo_object_id_t),    /* value: runtime_id */
        initial_capacity,
        nmo_hash_uint32,            /* hash function for uint32_t */
        NULL                        /* use default memcmp */
    );

    if (session->id_mappings == NULL) {
        free(session);
        return NULL;
    }

    session->repo = repo;
    session->saved_id_max = max_saved_id;
    session->active = 1;

    /* Allocate ID base
     * This ensures file IDs don't conflict with existing runtime IDs.
     * We allocate a range starting from current max + 1.
     */
    size_t existing_count = nmo_object_repository_get_count(repo);
    if (existing_count > 0) {
        /* Find max existing ID */
        size_t count;
        nmo_object_t **objects = nmo_object_repository_get_all(repo, &count);
        nmo_object_id_t max_id = 0;
        for (size_t i = 0; i < count; i++) {
            if (objects[i]->id > max_id) {
                max_id = objects[i]->id;
            }
        }
        session->id_base = max_id + 1;
    } else {
        session->id_base = 1; /* Start from 1 (0 is invalid) */
    }

    return session;
}

/**
 * Register object with file ID
 */
int nmo_load_session_register(nmo_load_session_t *session,
                              nmo_object_t *obj,
                              nmo_object_id_t file_id) {
    if (session == NULL || obj == NULL || !session->active) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Check if already registered */
    if (nmo_hash_table_contains(session->id_mappings, &file_id)) {
        return NMO_ERR_INVALID_STATE; // Already registered
    }

    /* Add mapping */
    int result = nmo_hash_table_insert(session->id_mappings, &file_id, &obj->id);
    if (result != NMO_OK) {
        return result;
    }

    return NMO_OK;
}

/**
 * End load session
 */
int nmo_load_session_end(nmo_load_session_t *session) {
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    session->active = 0;
    return NMO_OK;
}

/**
 * Get object repository
 */
nmo_object_repository_t *nmo_load_session_get_repository(
    const nmo_load_session_t *session) {
    return session ? session->repo : NULL;
}

/**
 * Get ID base
 */
nmo_object_id_t nmo_load_session_get_id_base(const nmo_load_session_t *session) {
    return session ? session->id_base : 0;
}

/**
 * Get max saved ID
 */
nmo_object_id_t nmo_load_session_get_max_saved_id(const nmo_load_session_t *session) {
    return session ? session->saved_id_max : 0;
}

/**
 * Destroy load session
 */
void nmo_load_session_destroy(nmo_load_session_t *session) {
    if (session != NULL) {
        nmo_hash_table_destroy(session->id_mappings);
        free(session);
    }
}

/**
 * Get runtime ID for file ID (internal helper for id_remap.c)
 */
int nmo_load_session_lookup_runtime_id(const nmo_load_session_t *session,
                                       nmo_object_id_t file_id,
                                       nmo_object_id_t *runtime_id) {
    if (session == NULL || runtime_id == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (nmo_hash_table_get(session->id_mappings, &file_id, runtime_id)) {
        return NMO_OK;
    }

    return NMO_ERR_INVALID_ARGUMENT; // Not found
}

/**
 * Iterator context for collecting mappings
 */
typedef struct {
    nmo_object_id_t *file_ids;
    nmo_object_id_t *runtime_ids;
    size_t index;
} mapping_collector_t;

/**
 * Iterator function to collect mappings
 */
static int collect_mapping(const void *key, void *value, void *user_data) {
    mapping_collector_t *collector = (mapping_collector_t *)user_data;
    collector->file_ids[collector->index] = *(const nmo_object_id_t *)key;
    collector->runtime_ids[collector->index] = *(nmo_object_id_t *)value;
    collector->index++;
    return 1; /* Continue iteration */
}

/**
 * Get all mappings (internal helper for id_remap.c)
 */
int nmo_load_session_get_mappings(const nmo_load_session_t *session,
                                  nmo_object_id_t **file_ids,
                                  nmo_object_id_t **runtime_ids,
                                  size_t *count) {
    if (session == NULL || file_ids == NULL || runtime_ids == NULL || count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t mapping_count = nmo_hash_table_get_count(session->id_mappings);
    if (mapping_count == 0) {
        *file_ids = NULL;
        *runtime_ids = NULL;
        *count = 0;
        return NMO_OK;
    }

    /* Allocate arrays */
    nmo_object_id_t *fids = (nmo_object_id_t *) malloc(mapping_count * sizeof(nmo_object_id_t));
    nmo_object_id_t *rids = (nmo_object_id_t *) malloc(mapping_count * sizeof(nmo_object_id_t));

    if (fids == NULL || rids == NULL) {
        free(fids);
        free(rids);
        return NMO_ERR_NOMEM;
    }

    /* Collect mappings using iterator */
    mapping_collector_t collector = {
        .file_ids = fids,
        .runtime_ids = rids,
        .index = 0
    };

    nmo_hash_table_iterate(session->id_mappings, collect_mapping, &collector);

    *file_ids = fids;
    *runtime_ids = rids;
    *count = mapping_count;

    return NMO_OK;
}
