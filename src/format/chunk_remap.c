// chunk_remap.c - Object ID remapping
// Implements: remap_object_ids

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include "format/nmo_id_remap.h"

// =============================================================================
// Internal Helpers
// =============================================================================

static int remap_single_id(nmo_object_id_t *id_ref, const nmo_id_remap_t *remap) {
    if (!id_ref || *id_ref == 0) return 0;

    nmo_object_id_t old_id = *id_ref;
    nmo_object_id_t new_id;

    nmo_result_t result = nmo_id_remap_lookup_id(remap, old_id, &new_id);
    if (result.code == NMO_OK) {
        if (new_id != 0 && new_id != old_id) {
            *id_ref = new_id;
            return 1;
        }
    }

    return 0;
}

static nmo_result_t remap_chunk_data_recursive(uint32_t *chunk_data,
                                               size_t data_size,
                                               uint32_t *ids,
                                               size_t id_count,
                                               const nmo_id_remap_t *remap,
                                               int *remapped_count) {
    if (!chunk_data || !remap || !remapped_count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    int local_count = 0;

    // Process object IDs using the ids list
    if (ids && id_count > 0) {
        size_t i = 0;
        while (i < id_count) {
            int id_offset = (int) ids[i];

            if (id_offset >= 0) {
                // Single object ID at this offset
                if ((size_t) id_offset < data_size) {
                    nmo_object_id_t *id_ref = (nmo_object_id_t *) &chunk_data[id_offset];
                    local_count += remap_single_id(id_ref, remap);
                }
                i++;
            } else {
                // Sequence of object IDs
                i++;
                if (i >= id_count) break;

                int sequence_header_offset = (int) ids[i];
                if (sequence_header_offset >= 0 &&
                    (size_t) sequence_header_offset < data_size) {
                    // Sequence format: [count, id1, id2, ...]
                    int count = (int) chunk_data[sequence_header_offset];
                    size_t sequence_start = sequence_header_offset + 1;

                    if (count > 0 && sequence_start + count <= data_size) {
                        for (int k = 0; k < count; k++) {
                            nmo_object_id_t *id_ref = (nmo_object_id_t *) &chunk_data[sequence_start + k];
                            local_count += remap_single_id(id_ref, remap);
                        }
                    }
                }
                i++;
            }
        }
    }

    *remapped_count += local_count;
    return nmo_result_ok();
}

static nmo_result_t remap_object_ids_recursive(nmo_chunk_t *chunk,
                                               const nmo_id_remap_t *remap,
                                               int *remapped_count) {
    if (!chunk || !remap || !remapped_count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    int local_count = 0;

    // Only process chunks with version >= CHUNK_VERSION1 (4)
    if (chunk->chunk_version < NMO_CHUNK_VERSION1) {
        return nmo_result_ok();
    }

    // Remap IDs in this chunk's data buffer
    nmo_result_t result = remap_chunk_data_recursive(chunk->data, chunk->data_size,
                                                     chunk->ids, chunk->id_count,
                                                     remap, &local_count);
    if (result.code != NMO_OK) return result;

    // Recursively process sub-chunks
    if (chunk->chunks && chunk->chunk_count > 0) {
        for (size_t i = 0; i < chunk->chunk_count; i++) {
            if (chunk->chunks[i]) {
                int sub_remapped = 0;
                result = remap_object_ids_recursive(chunk->chunks[i], remap, &sub_remapped);
                if (result.code != NMO_OK) return result;
                local_count += sub_remapped;
            }
        }
    }

    *remapped_count += local_count;
    return nmo_result_ok();
}

// =============================================================================
// ID Remapping
// =============================================================================

nmo_result_t nmo_chunk_remap_object_ids(nmo_chunk_t *chunk,
                                        const nmo_id_remap_t *remap) {
    if (!chunk || !remap) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    int remapped_count = 0;
    return remap_object_ids_recursive(chunk, remap, &remapped_count);
}
