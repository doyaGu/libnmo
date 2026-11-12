/**
 * @file id_mapping.c
 * @brief File-index to runtime-ID mapping implementation
 */

#include "session/nmo_id_mapping.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_hash.h"
#include "core/nmo_error.h"
#include <string.h>
#include <stdalign.h>

/* File indices count the objects of a file from 0, so they index an array;
 * one past this goes to the hash table instead. */
#define NMO_ID_MAPPING_DENSE_LIMIT (1u << 24)

struct nmo_id_mapping {
    nmo_object_repository_t *repo;
    nmo_object_id_t saved_id_max;
    nmo_object_id_t id_base;
    nmo_object_id_t *dense;        /* file index -> runtime id, 0 for none */
    size_t dense_capacity;
    size_t dense_count;
    nmo_hash_table_t *id_mappings; /* the other file indices */
    int active;
    nmo_arena_t *arena;
};

nmo_id_mapping_t *nmo_id_mapping_create(
    nmo_object_repository_t *repo,
    nmo_object_id_t max_saved_id)
{
    if (repo == NULL) return NULL;

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    if (arena == NULL) return NULL;

    nmo_id_mapping_t *m = (nmo_id_mapping_t *)nmo_arena_alloc(
        arena, sizeof(nmo_id_mapping_t), sizeof(void *));
    if (m == NULL) { nmo_arena_destroy(arena); return NULL; }
    memset(m, 0, sizeof(*m));
    m->arena = arena;

    m->id_mappings = nmo_hash_table_create(
        NULL, sizeof(nmo_object_id_t), sizeof(nmo_object_id_t),
        64, nmo_hash_uint32, NULL);
    if (m->id_mappings == NULL) { nmo_arena_destroy(arena); return NULL; }

    m->repo = repo;
    m->saved_id_max = max_saved_id;
    m->active = 1;

    size_t existing_count = nmo_object_repository_get_count(repo);
    if (existing_count > 0) {
        nmo_object_id_t max_id = 0;
        for (size_t i = 0; i < existing_count; i++) {
            nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
            if (object != NULL && object->id > max_id) {
                max_id = object->id;
            }
        }
        m->id_base = max_id + 1;
    } else {
        m->id_base = 1;
    }

    return m;
}

nmo_status_t nmo_id_mapping_register(nmo_id_mapping_t *mapping,
                                     nmo_object_t *obj,
                                     nmo_object_id_t file_index)
{
    if (mapping == NULL || obj == NULL || !mapping->active)
        return NMO_ERR_INVALID_ARGUMENT;
    if (file_index >= NMO_ID_MAPPING_DENSE_LIMIT || obj->id == NMO_OBJECT_ID_NONE) {
        if (nmo_hash_table_contains(mapping->id_mappings, &file_index) ||
            (file_index < mapping->dense_capacity && mapping->dense[file_index] != 0))
            return NMO_ERR_INVALID_STATE;
        return nmo_hash_table_insert(mapping->id_mappings, &file_index, &obj->id);
    }

    if (file_index >= mapping->dense_capacity) {
        size_t capacity = mapping->dense_capacity > 0 ? mapping->dense_capacity * 2u : 256u;
        if (capacity <= file_index) capacity = (size_t)file_index + 1u;
        if (capacity > NMO_ID_MAPPING_DENSE_LIMIT) capacity = NMO_ID_MAPPING_DENSE_LIMIT;
        nmo_allocator_t allocator = nmo_allocator_default();
        nmo_object_id_t *grown = (nmo_object_id_t *)nmo_alloc(
            &allocator, capacity * sizeof(nmo_object_id_t), alignof(nmo_object_id_t));
        if (grown == NULL) return NMO_ERR_NOMEM;
        if (mapping->dense_capacity > 0) {
            memcpy(grown, mapping->dense, mapping->dense_capacity * sizeof(nmo_object_id_t));
        }
        memset(grown + mapping->dense_capacity, 0,
               (capacity - mapping->dense_capacity) * sizeof(nmo_object_id_t));
        nmo_free(&allocator, mapping->dense);
        mapping->dense = grown;
        mapping->dense_capacity = capacity;
    }
    if (mapping->dense[file_index] != 0 ||
        (nmo_hash_table_get_count(mapping->id_mappings) > 0 &&
         nmo_hash_table_contains(mapping->id_mappings, &file_index)))
        return NMO_ERR_INVALID_STATE;
    mapping->dense[file_index] = obj->id;
    mapping->dense_count++;
    return NMO_OK;
}

nmo_status_t nmo_id_mapping_end(nmo_id_mapping_t *mapping) {
    if (mapping == NULL) return NMO_ERR_INVALID_ARGUMENT;
    mapping->active = 0;
    return NMO_OK;
}

nmo_status_t nmo_id_mapping_get_runtime_id(const nmo_id_mapping_t *mapping,
                                           nmo_object_id_t file_index,
                                           nmo_object_id_t *out_runtime_id)
{
    if (mapping == NULL || out_runtime_id == NULL)
        return NMO_ERR_INVALID_ARGUMENT;
    if (file_index < mapping->dense_capacity && mapping->dense[file_index] != 0) {
        *out_runtime_id = mapping->dense[file_index];
        return NMO_OK;
    }
    nmo_object_id_t runtime_id = 0;
    nmo_status_t result = nmo_hash_table_get(mapping->id_mappings, &file_index, &runtime_id);
    if (result != NMO_OK) return result;
    *out_runtime_id = runtime_id;
    return NMO_OK;
}

nmo_object_id_t nmo_id_mapping_get_id_base(const nmo_id_mapping_t *mapping) {
    return mapping ? mapping->id_base : 0;
}

nmo_object_id_t nmo_id_mapping_get_max_saved_id(const nmo_id_mapping_t *mapping) {
    return mapping ? mapping->saved_id_max : 0;
}

typedef struct {
    nmo_object_id_t *file_ids;
    nmo_object_id_t *runtime_ids;
    size_t index;
} mapping_collector_t;

static int collect_mapping(const void *key, void *value, void *user_data) {
    mapping_collector_t *c = (mapping_collector_t *)user_data;
    c->file_ids[c->index] = *(const nmo_object_id_t *)key;
    c->runtime_ids[c->index] = *(nmo_object_id_t *)value;
    c->index++;
    return 1;
}

nmo_status_t nmo_id_mapping_get_all(const nmo_id_mapping_t *mapping,
                                    nmo_object_id_t **file_ids,
                                    nmo_object_id_t **runtime_ids,
                                    size_t *count)
{
    if (mapping == NULL || file_ids == NULL || runtime_ids == NULL || count == NULL)
        return NMO_ERR_INVALID_ARGUMENT;

    size_t n = mapping->dense_count + nmo_hash_table_get_count(mapping->id_mappings);
    if (n == 0) { *file_ids = NULL; *runtime_ids = NULL; *count = 0; return NMO_OK; }

    nmo_object_id_t *fids = (nmo_object_id_t *)nmo_arena_alloc(
        mapping->arena, n * sizeof(nmo_object_id_t), alignof(nmo_object_id_t));
    nmo_object_id_t *rids = (nmo_object_id_t *)nmo_arena_alloc(
        mapping->arena, n * sizeof(nmo_object_id_t), alignof(nmo_object_id_t));
    if (fids == NULL || rids == NULL) return NMO_ERR_NOMEM;

    mapping_collector_t collector = { fids, rids, 0 };
    for (size_t i = 0; i < mapping->dense_capacity; ++i) {
        if (mapping->dense[i] != 0) {
            fids[collector.index] = (nmo_object_id_t)i;
            rids[collector.index] = mapping->dense[i];
            collector.index++;
        }
    }
    nmo_hash_table_iterate(mapping->id_mappings, collect_mapping, &collector);

    *file_ids = fids;
    *runtime_ids = rids;
    *count = n;
    return NMO_OK;
}

void nmo_id_mapping_destroy(nmo_id_mapping_t *mapping) {
    if (mapping != NULL) {
        nmo_allocator_t allocator = nmo_allocator_default();
        nmo_free(&allocator, mapping->dense);
        nmo_hash_table_destroy(mapping->id_mappings);
        nmo_arena_destroy(mapping->arena);
    }
}
