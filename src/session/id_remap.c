/**
 * @file id_remap.c
 * @brief ID remapping compatibility layer implementation
 */

#include "session/nmo_id_remap.h"
#include "session/nmo_load_session.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include <string.h>
#include <stdalign.h>

/* Forward declaration for load session internal function */
extern int nmo_load_session_get_mappings(const nmo_load_session_t *session,
                                         nmo_object_id_t **file_indices,
                                         nmo_object_id_t **runtime_ids,
                                         size_t *count);

/**
 * @brief ID remap plan structure
 */
typedef struct nmo_id_remap_plan {
    nmo_id_remap_t *remap;   /**< Underlying remap table (runtime -> file) */
    nmo_arena_t *arena;      /**< Arena for allocations */
    size_t objects_remapped; /**< Number of objects remapped */
    int owns_arena;          /**< Whether we own the arena (for cleanup) */
} nmo_id_remap_plan_t;

/* ============================================================================
 * Load-time ID Remapping (file object index -> runtime ID)
 * ============================================================================ */

nmo_id_remap_table_t *nmo_build_remap_table(nmo_load_session_t *session) {
    if (session == NULL) {
        return NULL;
    }

    /* Get mappings from load session */
    nmo_object_id_t *file_indices = NULL;
    nmo_object_id_t *runtime_ids = NULL;
    size_t count = 0;

    int result = nmo_load_session_get_mappings(session, &file_indices, &runtime_ids, &count);
    if (result != NMO_OK || count == 0) {
        return NULL;
    }

    /* Create arena for remap table */
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    if (arena == NULL) {
        return NULL;
    }

    /* Create remap table */
    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    if (remap == NULL) {
        nmo_arena_destroy(arena);
        return NULL;
    }

    /* Add all mappings (file object index -> runtime ID) */
    size_t failed_count = 0;
    for (size_t i = 0; i < count; i++) {
        nmo_status_t add_result = nmo_id_remap_add(remap, file_indices[i], runtime_ids[i]);
        if (add_result != NMO_OK) {
            failed_count++;
        }
    }

    /* If all mappings failed, clean up and return NULL */
    if (failed_count == count && count > 0) {
        nmo_arena_destroy(arena);
        return NULL;
    }

    return remap;
}

int nmo_id_remap_lookup(const nmo_id_remap_table_t *table,
                        nmo_object_id_t old_id,
                        nmo_object_id_t *new_id) {
    if (table == NULL || new_id == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_status_t result = nmo_id_remap_lookup_id((nmo_id_remap_t *) table, old_id, new_id);
    return result;
}

size_t nmo_id_remap_table_get_count(const nmo_id_remap_table_t *table) {
    if (table == NULL) {
        return 0;
    }

    return nmo_id_remap_get_count((nmo_id_remap_t *) table);
}

void nmo_id_remap_table_destroy(nmo_id_remap_table_t *table) {
    if (table == NULL) {
        return;
    }

    /* Destroy the arena that was created by nmo_build_remap_table.
     * NOTE: This assumes the table owns the arena. If callers need to share
     * arenas, they should use nmo_id_remap_create() directly and manage
     * the arena lifetime themselves. */
    nmo_arena_t *arena = table->arena;
    if (arena != NULL) {
        nmo_arena_destroy(arena);
    }
}

/* ============================================================================
 * Save-time ID Remapping (runtime ID -> file object index)
 * ============================================================================ */

nmo_id_remap_plan_t *nmo_id_remap_plan_create(nmo_object_repository_t *repo,
                                            nmo_object_t **objects_to_save,
                                            size_t object_count) {
    if (repo == NULL || objects_to_save == NULL || object_count == 0) {
        return NULL;
    }
    (void)repo;

    /* Create arena for plan */
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    if (arena == NULL) {
        return NULL;
    }

    /* Allocate plan structure */
    nmo_id_remap_plan_t *plan = (nmo_id_remap_plan_t *) nmo_arena_alloc(
        arena, sizeof(nmo_id_remap_plan_t), alignof(nmo_id_remap_plan_t));
    if (plan == NULL) {
        nmo_arena_destroy(arena);
        return NULL;
    }

    plan->arena = arena;
    plan->objects_remapped = 0;
    plan->owns_arena = 1;  /* We own the arena */

    /* Create remap table */
    plan->remap = nmo_id_remap_create(arena);
    if (plan->remap == NULL) {
        nmo_arena_destroy(arena);
        return NULL;
    }

    nmo_object_id_t *used_ids = (nmo_object_id_t *) nmo_arena_alloc(
        arena, sizeof(nmo_object_id_t) * object_count, alignof(nmo_object_id_t));
    if (used_ids == NULL) {
        nmo_arena_destroy(arena);
        return NULL;
    }
    size_t used_count = 0;

    for (size_t i = 0; i < object_count; i++) {
        nmo_object_t *obj = objects_to_save[i];
        if (obj == NULL) {
            continue;
        }

        if (obj->file_id != 0) {
            for (size_t u = 0; u < used_count; u++) {
                if (used_ids[u] == obj->file_id) {
                    nmo_arena_destroy(arena);
                    return NULL;
                }
            }
            used_ids[used_count++] = obj->file_id;
        }
    }

    nmo_object_id_t next_file_id = 1;

    /* Build mapping: runtime ID -> preserved file ID when available */
    for (size_t i = 0; i < object_count; i++) {
        nmo_object_t *obj = objects_to_save[i];
        if (obj == NULL) {
            continue;
        }

        nmo_object_id_t runtime_id = obj->id;
        nmo_object_id_t file_id = obj->file_id;

        if (obj->file_id == 0) {
            for (;;) {
                int used = 0;
                for (size_t u = 0; u < used_count; u++) {
                    if (used_ids[u] == next_file_id) {
                        used = 1;
                        break;
                    }
                }

                if (!used) {
                    file_id = next_file_id;
                    used_ids[used_count++] = file_id;
                    next_file_id++;
                    break;
                }

                next_file_id++;
            }
        }

        nmo_status_t result = nmo_id_remap_add(plan->remap, runtime_id, file_id);
        if (result == NMO_OK) {
            plan->objects_remapped++;
        }
    }

    return plan;
}

nmo_id_remap_table_t *nmo_id_remap_plan_get_table(const nmo_id_remap_plan_t *plan) {
    return plan ? plan->remap : NULL;
}

size_t nmo_id_remap_plan_get_remapped_count(const nmo_id_remap_plan_t *plan) {
    return plan ? plan->objects_remapped : 0;
}

void nmo_id_remap_plan_destroy(nmo_id_remap_plan_t *plan) {
    if (plan != NULL && plan->owns_arena) {
        /* Arena destruction will free everything */
        nmo_arena_destroy(plan->arena);
    }
}
