#include "app/nmo_chunk_index.h"

#include "app/nmo_session.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_object.h"

#include <stdlib.h>

static int cmp_chunk_ptr_index(const void *a, const void *b) {
    const nmo_chunk_ptr_index_t *pa = (const nmo_chunk_ptr_index_t *)a;
    const nmo_chunk_ptr_index_t *pb = (const nmo_chunk_ptr_index_t *)b;

    if (pa->ptr < pb->ptr) {
        return -1;
    }
    if (pa->ptr > pb->ptr) {
        return 1;
    }
    return 0;
}

static bool chunk_entries_push(nmo_chunk_index_entry_t **entries,
                               size_t *count,
                               size_t *capacity,
                               const nmo_chunk_index_entry_t *item) {
    if (!entries || !count || !capacity || !item) {
        return false;
    }

    if (*count == *capacity) {
        size_t new_cap = (*capacity == 0) ? 128 : (*capacity * 2);
        nmo_chunk_index_entry_t *new_entries = (nmo_chunk_index_entry_t *)realloc(
            *entries, new_cap * sizeof(nmo_chunk_index_entry_t));
        if (!new_entries) {
            return false;
        }
        *entries = new_entries;
        *capacity = new_cap;
    }

    (*entries)[*count] = *item;
    (*count)++;
    return true;
}

static bool collect_chunk_entries_recursive(nmo_chunk_index_entry_t **entries,
                                            size_t *count,
                                            size_t *capacity,
                                            nmo_chunk_t *chunk,
                                            nmo_object_id_t owner_object_id,
                                            const char *owner_object_name,
                                            nmo_class_id_t owner_class_id,
                                            int64_t parent_index,
                                            uint32_t depth) {
    if (!chunk) {
        return true;
    }

    nmo_chunk_index_entry_t e = {
        .chunk = chunk,
        .owner_object_id = owner_object_id,
        .owner_object_name = owner_object_name,
        .owner_class_id = owner_class_id,
        .parent_index = parent_index,
        .depth = depth,
    };

    int64_t this_index = (int64_t)(*count);
    if (!chunk_entries_push(entries, count, capacity, &e)) {
        return false;
    }

    uint32_t sub_count = nmo_chunk_get_sub_chunk_count(chunk);
    for (uint32_t i = 0; i < sub_count; ++i) {
        nmo_chunk_t *sub = nmo_chunk_get_sub_chunk(chunk, i);
        if (!collect_chunk_entries_recursive(entries, count, capacity,
                                             sub, owner_object_id, owner_object_name,
                                             owner_class_id, this_index, depth + 1)) {
            return false;
        }
    }

    return true;
}

bool nmo_chunk_index_collect_entries(nmo_session_t *session,
                                     nmo_chunk_index_entry_t **out_entries,
                                     size_t *out_count,
                                     size_t *out_object_count) {
    if (!session || !out_entries || !out_count) {
        return false;
    }

    *out_entries = NULL;
    *out_count = 0;
    if (out_object_count) {
        *out_object_count = 0;
    }

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        return false;
    }

    nmo_chunk_index_entry_t *entries = NULL;
    size_t count = 0;
    size_t capacity = 0;

    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *obj = objects[i];
        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        if (!chunk) {
            continue;
        }

        nmo_object_id_t owner_id = nmo_object_get_id(obj);
        const char *owner_name = nmo_object_get_name(obj);
        nmo_class_id_t owner_class_id = nmo_object_get_class_id(obj);

        if (!collect_chunk_entries_recursive(&entries, &count, &capacity,
                                             chunk,
                                             owner_id,
                                             (owner_name && owner_name[0]) ? owner_name : NULL,
                                             owner_class_id,
                                             -1,
                                             0)) {
            free(entries);
            return false;
        }
    }

    *out_entries = entries;
    *out_count = count;
    if (out_object_count) {
        *out_object_count = object_count;
    }
    return true;
}

bool nmo_chunk_index_build_map(const nmo_chunk_index_entry_t *entries,
                               size_t entry_count,
                               nmo_chunk_ptr_index_t **out_map,
                               size_t *out_map_count) {
    if (!out_map || !out_map_count) {
        return false;
    }
    *out_map = NULL;
    *out_map_count = 0;

    if (!entries || entry_count == 0) {
        return true;
    }

    nmo_chunk_ptr_index_t *map = (nmo_chunk_ptr_index_t *)malloc(
        entry_count * sizeof(nmo_chunk_ptr_index_t));
    if (!map) {
        return false;
    }

    for (size_t i = 0; i < entry_count; ++i) {
        map[i].ptr = entries[i].chunk;
        map[i].index = (uint32_t)i;
    }

    qsort(map, entry_count, sizeof(*map), cmp_chunk_ptr_index);
    *out_map = map;
    *out_map_count = entry_count;
    return true;
}

bool nmo_chunk_index_lookup(const nmo_chunk_ptr_index_t *map,
                            size_t map_count,
                            const nmo_chunk_t *chunk,
                            uint32_t *out_index) {
    if (!out_index) {
        return false;
    }
    *out_index = 0;

    if (!map || map_count == 0 || !chunk) {
        return false;
    }

    nmo_chunk_ptr_index_t key = {.ptr = chunk, .index = 0};
    nmo_chunk_ptr_index_t *found = (nmo_chunk_ptr_index_t *)bsearch(
        &key, map, map_count, sizeof(*map), cmp_chunk_ptr_index);
    if (!found) {
        return false;
    }
    *out_index = found->index;
    return true;
}

void nmo_chunk_index_free_entries(nmo_chunk_index_entry_t *entries) {
    free(entries);
}

void nmo_chunk_index_free_map(nmo_chunk_ptr_index_t *map) {
    free(map);
}
