/**
 * @file nmo_chunk_index.h
 * @brief Chunk entry collection and flat-index helpers.
 */

#ifndef NMO_CHUNK_INDEX_H
#define NMO_CHUNK_INDEX_H

#include "nmo_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_session nmo_session_t;

typedef struct nmo_chunk_index_entry {
    nmo_chunk_t *chunk;
    nmo_object_id_t owner_object_id;
    const char *owner_object_name; /* Borrowed from object name in session */
    nmo_class_id_t owner_class_id;
    int64_t parent_index;          /* -1 for root */
    uint32_t depth;
} nmo_chunk_index_entry_t;

typedef struct nmo_chunk_ptr_index {
    const nmo_chunk_t *ptr;
    uint32_t index;
} nmo_chunk_ptr_index_t;

/**
 * @brief Collect all root chunks and sub-chunks from a session.
 */
NMO_API bool nmo_chunk_index_collect_entries(nmo_session_t *session,
                                             nmo_chunk_index_entry_t **out_entries,
                                             size_t *out_count,
                                             size_t *out_object_count);

/**
 * @brief Build sorted pointer->flat-index lookup table from collected entries.
 */
NMO_API bool nmo_chunk_index_build_map(const nmo_chunk_index_entry_t *entries,
                                       size_t entry_count,
                                       nmo_chunk_ptr_index_t **out_map,
                                       size_t *out_map_count);

/**
 * @brief Resolve a chunk pointer to its flat index.
 */
NMO_API bool nmo_chunk_index_lookup(const nmo_chunk_ptr_index_t *map,
                                    size_t map_count,
                                    const nmo_chunk_t *chunk,
                                    uint32_t *out_index);

NMO_API void nmo_chunk_index_free_entries(nmo_chunk_index_entry_t *entries);
NMO_API void nmo_chunk_index_free_map(nmo_chunk_ptr_index_t *map);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CHUNK_INDEX_H */
