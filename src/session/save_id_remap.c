/**
 * @file save_id_remap.c
 * @brief Save-time runtime ID to file ID remap planning.
 */

#include "session/nmo_save_id_remap.h"

#include "format/nmo_object.h"
#include "core/nmo_arena.h"

#include <stdalign.h>

typedef struct nmo_save_id_remap_plan {
    nmo_id_remap_t *remap;
    nmo_arena_t *arena;
    size_t objects_remapped;
    int owns_arena;
} nmo_save_id_remap_plan_t;

nmo_save_id_remap_plan_t *nmo_save_id_remap_plan_create(
    nmo_object_repository_t *repo,
    nmo_object_t **objects_to_save,
    size_t object_count)
{
    if (repo == NULL || objects_to_save == NULL || object_count == 0) {
        return NULL;
    }
    (void)repo;

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    if (arena == NULL) {
        return NULL;
    }

    nmo_save_id_remap_plan_t *plan = (nmo_save_id_remap_plan_t *)nmo_arena_alloc(
        arena, sizeof(nmo_save_id_remap_plan_t), alignof(nmo_save_id_remap_plan_t));
    if (plan == NULL) {
        nmo_arena_destroy(arena);
        return NULL;
    }

    plan->arena = arena;
    plan->objects_remapped = 0;
    plan->owns_arena = 1;

    plan->remap = nmo_id_remap_create(arena);
    if (plan->remap == NULL) {
        nmo_arena_destroy(arena);
        return NULL;
    }

    nmo_object_id_t *used_ids = (nmo_object_id_t *)nmo_arena_alloc(
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

nmo_id_remap_t *nmo_save_id_remap_plan_get_table(
    const nmo_save_id_remap_plan_t *plan)
{
    return plan ? plan->remap : NULL;
}

size_t nmo_save_id_remap_plan_get_remapped_count(
    const nmo_save_id_remap_plan_t *plan)
{
    return plan ? plan->objects_remapped : 0;
}

void nmo_save_id_remap_plan_destroy(nmo_save_id_remap_plan_t *plan)
{
    if (plan != NULL && plan->owns_arena) {
        nmo_arena_destroy(plan->arena);
    }
}
