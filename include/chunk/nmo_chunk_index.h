#ifndef NMO_CHUNK_INDEX_OWNER_H
#define NMO_CHUNK_INDEX_OWNER_H

#include "nmo_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NMO_CHUNK_INDEX_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_CHUNK_INDEX_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_session nmo_session_t;

typedef struct nmo_chunk_index_entry {
    nmo_chunk_t *chunk;
    nmo_object_id_t owner_object_id;
    const char *owner_object_name;
    nmo_class_id_t owner_class_id;
    int64_t parent_index;
    uint32_t depth;
} nmo_chunk_index_entry_t;

typedef struct nmo_chunk_ptr_index {
    const nmo_chunk_t *ptr;
    uint32_t index;
} nmo_chunk_ptr_index_t;

NMO_API bool nmo_chunk_index_collect_entries(nmo_session_t *session,
                                             nmo_chunk_index_entry_t **out_entries,
                                             size_t *out_count,
                                             size_t *out_object_count);

NMO_API bool nmo_chunk_index_build_map(const nmo_chunk_index_entry_t *entries,
                                       size_t entry_count,
                                       nmo_chunk_ptr_index_t **out_map,
                                       size_t *out_map_count);

NMO_API bool nmo_chunk_index_lookup(const nmo_chunk_ptr_index_t *map,
                                    size_t map_count,
                                    const nmo_chunk_t *chunk,
                                    uint32_t *out_index);

NMO_API void nmo_chunk_index_free_entries(nmo_chunk_index_entry_t *entries);
NMO_API void nmo_chunk_index_free_map(nmo_chunk_ptr_index_t *map);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CHUNK_INDEX_OWNER_H */
