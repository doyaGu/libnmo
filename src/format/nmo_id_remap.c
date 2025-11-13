/**
 * @file nmo_id_remap.c
 * @brief ID remapping implementation
 */

#include "format/nmo_id_remap.h"
#include "core/nmo_error.h"
#include <string.h>

#define INITIAL_CAPACITY 32

nmo_id_remap_t *nmo_id_remap_create(nmo_arena_t *arena) {
    if (!arena) return NULL;

    nmo_id_remap_t *remap = (nmo_id_remap_t *) nmo_arena_alloc(arena, sizeof(nmo_id_remap_t), 8);
    if (!remap) return NULL;

    remap->entries = (nmo_id_remap_entry_t *) nmo_arena_alloc(arena, sizeof(nmo_id_remap_entry_t) * INITIAL_CAPACITY, 8);
    if (!remap->entries) return NULL;

    remap->count = 0;
    remap->capacity = INITIAL_CAPACITY;
    remap->arena = arena;

    return remap;
}

void nmo_id_remap_destroy(nmo_id_remap_t *remap) {
    // Memory is managed by arena, nothing to do
    (void) remap;
}

nmo_result_t nmo_id_remap_add(nmo_id_remap_t *remap, nmo_object_id_t old_id, nmo_object_id_t new_id) {
    if (!remap) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid remap table"));
    }

    // Check if we need to expand
    if (remap->count >= remap->capacity) {
        size_t new_capacity = remap->capacity * 2;
        nmo_id_remap_entry_t *new_entries = (nmo_id_remap_entry_t *) nmo_arena_alloc(
            remap->arena, sizeof(nmo_id_remap_entry_t) * new_capacity, 8);
        if (!new_entries) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR, "Out of memory"));
        }

        memcpy(new_entries, remap->entries, sizeof(nmo_id_remap_entry_t) * remap->count);
        remap->entries = new_entries;
        remap->capacity = new_capacity;
    }

    // Add new entry
    remap->entries[remap->count].old_id = old_id;
    remap->entries[remap->count].new_id = new_id;
    remap->count++;

    return nmo_result_ok();
}

nmo_result_t nmo_id_remap_lookup_id(const nmo_id_remap_t *remap, nmo_object_id_t old_id, nmo_object_id_t *out_new_id) {
    if (!remap || !out_new_id) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    // Linear search (could be optimized with hash table if needed)
    for (size_t i = 0; i < remap->count; i++) {
        if (remap->entries[i].old_id == old_id) {
            *out_new_id = remap->entries[i].new_id;
            return nmo_result_ok();
        }
    }

    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND,
                                      NMO_SEVERITY_WARNING, "ID not found in remap table"));
}

size_t nmo_id_remap_get_count(const nmo_id_remap_t *remap) {
    return remap ? remap->count : 0;
}

void nmo_id_remap_clear(nmo_id_remap_t *remap) {
    if (remap) {
        remap->count = 0;
    }
}
