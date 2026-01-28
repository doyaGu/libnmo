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

static nmo_result_t remap_embedded_subchunk_recursive(uint32_t *parent_data,
                                                       size_t parent_dwords,
                                                       size_t header_pos,
                                                       const nmo_id_remap_t *remap,
                                                       int *remapped_count) {
    if (!parent_data || !remap || !remapped_count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    /* Layout written by nmo_chunk_write_sub_chunk:
     * [size][class_id][version_info][data_size][file_flag][id_count][chunk_count][manager_count]
     * followed by: data[data_size], ids[id_count], chunk_refs[chunk_count], managers[manager_count]
     */
    if (header_pos + 8 > parent_dwords) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR, "Sub-chunk header out of bounds"));
    }

    uint32_t payload_dwords = parent_data[header_pos];
    size_t total_dwords = 1u + (size_t)payload_dwords;
    if (header_pos + total_dwords > parent_dwords) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR, "Sub-chunk payload out of bounds"));
    }

    uint32_t data_size = parent_data[header_pos + 3];
    uint32_t id_count = parent_data[header_pos + 5];
    uint32_t chunk_ref_count = parent_data[header_pos + 6];
    uint32_t manager_count = parent_data[header_pos + 7];

    size_t data_start = header_pos + 8;
    size_t ids_start = data_start + (size_t)data_size;
    size_t refs_start = ids_start + (size_t)id_count;
    size_t managers_start = refs_start + (size_t)chunk_ref_count;

    if (managers_start + (size_t)manager_count > header_pos + total_dwords) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CORRUPT,
                                          NMO_SEVERITY_ERROR, "Sub-chunk layout mismatch"));
    }

    if (data_size > 0 && id_count > 0) {
        nmo_result_t result = remap_chunk_data_recursive(&parent_data[data_start],
                                                         (size_t)data_size,
                                                         &parent_data[ids_start],
                                                         (size_t)id_count,
                                                         remap,
                                                         remapped_count);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    if (chunk_ref_count > 0) {
        for (size_t i = 0; i < (size_t)chunk_ref_count; ++i) {
            size_t rel = (size_t)parent_data[refs_start + i];
            size_t child_header_pos = data_start + rel;
            nmo_result_t result = remap_embedded_subchunk_recursive(parent_data,
                                                                    parent_dwords,
                                                                    child_header_pos,
                                                                    remap,
                                                                    remapped_count);
            if (result.code != NMO_OK) {
                return result;
            }
        }
    }

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
    nmo_result_t result = nmo_result_ok();

    // Only process chunks with version >= CHUNK_VERSION1 (4)
    if (chunk->chunk_version < NMO_CHUNK_VERSION1) {
        return nmo_result_ok();
    }

    // Remap IDs in this chunk's data buffer
    if (chunk->data.count > 0 && chunk->ids.count > 0) {
        uint32_t *chunk_data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
        uint32_t *chunk_ids = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->ids);
        result = remap_chunk_data_recursive(chunk_data, chunk->data.count,
                                            chunk_ids, chunk->ids.count,
                                            remap, &local_count);
        if (result.code != NMO_OK) return result;
    }

    // Recursively process embedded sub-chunks in the serialized data stream
    if (chunk->chunk_refs.count > 0 && chunk->data.count > 0) {
        uint32_t *chunk_data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
        uint32_t *chunk_refs = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->chunk_refs);
        for (size_t i = 0; i < chunk->chunk_refs.count; ++i) {
            result = remap_embedded_subchunk_recursive(chunk_data,
                                                       chunk->data.count,
                                                       (size_t)chunk_refs[i],
                                                       remap,
                                                       &local_count);
            if (result.code != NMO_OK) {
                return result;
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
