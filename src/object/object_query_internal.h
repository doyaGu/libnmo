/**
 * @file object_query_internal.h
 * @brief Internal declarations shared by object query translation units.
 */

#ifndef NMO_OBJECT_QUERY_INTERNAL_H
#define NMO_OBJECT_QUERY_INTERNAL_H

#include "object/nmo_object_query.h"
#include "object/nmo_object_repository.h"
#include "core/nmo_array.h"
#include "core/nmo_allocator.h"
#include "core/nmo_hash_table.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>

typedef struct query_meta {
    nmo_object_id_t object_id;
    nmo_class_id_t class_id;
    size_t repository_index;
    nmo_object_t *object;
    char *folded_name;
} query_meta_t;

typedef struct query_id_entry {
    uint32_t key;
    size_t meta_index;
} query_id_entry_t;

typedef struct query_name_entry {
    const char *key;
    size_t meta_index;
} query_name_entry_t;

struct nmo_object_query_index {
    nmo_object_repository_t *repository;
    const nmo_type_registry_t *registry;
    nmo_allocator_t allocator;

    query_meta_t *metas;
    size_t meta_count;
    size_t meta_capacity;

    nmo_hash_table_t *id_to_meta;
    nmo_array_t class_entries;        /* query_id_entry_t */
    nmo_array_t derived_entries;      /* query_id_entry_t */
    nmo_array_t name_entries;         /* query_name_entry_t */
    nmo_array_t folded_name_entries;  /* query_name_entry_t */

    nmo_array_t trigram_entries;      /* query_id_entry_t */
    bool text_built;

    uint32_t *visit_marks;
    uint32_t visit_generation;

    bool eager_dirty;
    bool text_dirty;
    bool observer_attached;
};

typedef enum query_candidate_kind {
    QUERY_CANDIDATE_ALL = 0,
    QUERY_CANDIDATE_NONE,
    QUERY_CANDIDATE_SINGLE,
    QUERY_CANDIDATE_ID_ENTRIES,
    QUERY_CANDIDATE_NAME_ENTRIES
} query_candidate_kind_t;

typedef struct query_candidate {
    query_candidate_kind_t kind;
    size_t count;
    size_t single_meta_index;
    const query_id_entry_t *id_entries;
    const query_name_entry_t *name_entries;
} query_candidate_t;

static inline char query_fold_ascii(char c)
{
    return (char)tolower((unsigned char)c);
}

static inline query_candidate_t query_all_candidate(void)
{
    return (query_candidate_t){ .kind = QUERY_CANDIDATE_ALL };
}

static inline query_candidate_t query_none_candidate(void)
{
    return (query_candidate_t){ .kind = QUERY_CANDIDATE_NONE };
}

static inline query_candidate_t query_id_entries_candidate(
    const query_id_entry_t *entries,
    size_t count)
{
    return (query_candidate_t){
        .kind = count == 0 ? QUERY_CANDIDATE_NONE : QUERY_CANDIDATE_ID_ENTRIES,
        .count = count,
        .id_entries = entries
    };
}

static inline query_candidate_t query_name_entries_candidate(
    const query_name_entry_t *entries,
    size_t count)
{
    return (query_candidate_t){
        .kind = count == 0 ? QUERY_CANDIDATE_NONE : QUERY_CANDIDATE_NAME_ENTRIES,
        .count = count,
        .name_entries = entries
    };
}

static inline query_candidate_t query_single_candidate(size_t meta_index)
{
    return (query_candidate_t){
        .kind = QUERY_CANDIDATE_SINGLE,
        .count = 1,
        .single_meta_index = meta_index
    };
}

nmo_status_t query_plan_candidate(
    nmo_object_query_index_t *index,
    const nmo_type_registry_t *registry,
    const nmo_object_query_t *query,
    query_candidate_t *out_candidate);

#endif /* NMO_OBJECT_QUERY_INTERNAL_H */
