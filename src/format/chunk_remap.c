// chunk_remap.c - Object ID remapping
// Implements: remap_object_ids

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include "format/nmo_id_remap.h"

#include <stdlib.h>
#include <string.h>

#define LIST_SEQUENCE_MARKER 0xFFFFFFFFu

// =============================================================================
// Internal Helpers
// =============================================================================

static int remap_single_id(nmo_object_id_t *id_ref, const nmo_id_remap_t *remap) {
    if (!id_ref) return 0;

    nmo_object_id_t old_id = *id_ref;
    nmo_object_id_t new_id;

    nmo_status_t result = nmo_id_remap_lookup_id(remap, old_id, &new_id);
    if (result == NMO_OK) {
        if (new_id != 0 && new_id != old_id) {
            *id_ref = new_id;
            return 1;
        }
    }

    return 0;
}

static nmo_status_t remap_chunk_data_recursive(uint32_t *chunk_data,
                                               size_t data_size,
                                               uint32_t *ids,
                                               size_t id_count,
                                               const nmo_id_remap_t *remap,
                                               int *remapped_count) {
    if (!chunk_data || !remap || !remapped_count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    int local_count = 0;

    // Process object IDs using the ids list
    if (ids && id_count > 0) {
        size_t i = 0;
        while (i < id_count) {
            uint32_t entry = ids[i];

            if (entry != LIST_SEQUENCE_MARKER) {
                // Single object ID at this offset
                if ((size_t) entry < data_size) {
                    nmo_object_id_t *id_ref = (nmo_object_id_t *) &chunk_data[entry];
                    local_count += remap_single_id(id_ref, remap);
                }
                i++;
            } else {
                // Sequence of object IDs
                i++;
                if (i >= id_count) {
                    break;
                }

                uint32_t sequence_header_offset = ids[i];
                if ((size_t) sequence_header_offset < data_size) {
                    // Sequence format: [count, id1, id2, ...]
                    uint32_t count = chunk_data[sequence_header_offset];
                    size_t sequence_start = (size_t) sequence_header_offset + 1u;

                    if (count > 0 && sequence_start + (size_t) count <= data_size) {
                        for (uint32_t k = 0; k < count; k++) {
                            nmo_object_id_t *id_ref =
                                (nmo_object_id_t *) &chunk_data[sequence_start + k];
                            local_count += remap_single_id(id_ref, remap);
                        }
                    }
                }
                i++;
            }
        }
    }

    *remapped_count += local_count;
    NMO_RETURN_OK();
}

static nmo_status_t remap_embedded_subchunk_recursive(uint32_t *parent_data,
                                                       size_t parent_dwords,
                                                       size_t header_pos,
                                                       uint32_t parent_chunk_version,
                                                       const nmo_id_remap_t *remap,
                                                       int *remapped_count) {
    if (!parent_data || !remap || !remapped_count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    size_t header_bytes =
        sizeof(uint32_t) + /* size */
        sizeof(uint32_t) + /* class_id */
        sizeof(uint32_t) + /* version_info */
        sizeof(uint32_t) + /* data_size */
        sizeof(uint32_t) + /* file_flag */
        sizeof(uint32_t) + /* id_count */
        sizeof(uint32_t);  /* chunk_count */
    if (parent_chunk_version > NMO_CHUNK_VERSION1) {
        header_bytes += sizeof(uint32_t); /* manager_count */
    }
    const size_t header_dwords = header_bytes / sizeof(uint32_t);
    /* Layout written by nmo_chunk_write_sub_chunk:
     * [size][class_id][version_info][data_size][file_flag][id_count][chunk_count][manager_count?]
     * followed by: data[data_size], ids[id_count], chunk_refs[chunk_count], managers[manager_count]
     */
    if (header_pos + header_dwords > parent_dwords) {
        NMO_RETURN_ERROR(NMO_ERR_EOF, NMO_SEVERITY_ERROR, "Sub-chunk header out of bounds");
    }

    uint32_t payload_dwords = parent_data[header_pos];
    size_t total_dwords = 1u + (size_t)payload_dwords;
    if (header_pos + total_dwords > parent_dwords) {
        NMO_RETURN_ERROR(NMO_ERR_EOF, NMO_SEVERITY_ERROR, "Sub-chunk payload out of bounds");
    }

    uint32_t version_info = parent_data[header_pos + 2];
    uint32_t child_chunk_version = (version_info >> 16) & 0xFFFFu;

    uint32_t data_size = parent_data[header_pos + 3];
    uint32_t id_count = parent_data[header_pos + 5];
    uint32_t chunk_ref_count = parent_data[header_pos + 6];
    uint32_t manager_count = 0;
    if (parent_chunk_version > NMO_CHUNK_VERSION1) {
        manager_count = parent_data[header_pos + 7];
    }

    size_t data_start = header_pos + header_dwords;
    size_t ids_start = data_start + (size_t)data_size;
    size_t refs_start = ids_start + (size_t)id_count;
    size_t managers_start = refs_start + (size_t)chunk_ref_count;

    if (managers_start + (size_t)manager_count > header_pos + total_dwords) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Sub-chunk layout mismatch");
    }

    if (data_size > 0 && id_count > 0) {
        nmo_status_t result = remap_chunk_data_recursive(&parent_data[data_start],
                                                         (size_t)data_size,
                                                         &parent_data[ids_start],
                                                         (size_t)id_count,
                                                         remap,
                                                         remapped_count);
        NMO_RETURN_IF_ERROR(result);
    }

    if (chunk_ref_count > 0) {
        size_t data_end = data_start + (size_t)data_size;
        if (data_end > parent_dwords) {
            NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Sub-chunk data bounds invalid");
        }

        for (size_t i = 0; i < (size_t)chunk_ref_count; ++i) {
            uint32_t entry = parent_data[refs_start + i];

            if (entry == LIST_SEQUENCE_MARKER) {
                if (i + 1 >= (size_t)chunk_ref_count) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Malformed sub-chunk sequence marker");
                }

                uint32_t seq_pos = parent_data[refs_start + (++i)];
                if (seq_pos >= data_size) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Sub-chunk sequence offset out of bounds");
                }

                size_t seq_abs = data_start + (size_t)seq_pos;
                if (seq_abs + 1 > data_end) {
                    NMO_RETURN_ERROR(NMO_ERR_EOF, NMO_SEVERITY_ERROR, "Sub-chunk sequence header out of bounds");
                }

                uint32_t seq_count = parent_data[seq_abs];
                size_t cursor = seq_abs + 1;

                for (uint32_t s = 0; s < seq_count; ++s) {
                    if (cursor >= data_end) {
                        NMO_RETURN_ERROR(NMO_ERR_EOF, NMO_SEVERITY_ERROR, "Sub-chunk sequence truncated");
                    }

                    uint32_t payload_dwords = parent_data[cursor];
                    size_t total_dwords = 1u + (size_t)payload_dwords;
                    if (cursor + total_dwords > data_end) {
                        NMO_RETURN_ERROR(NMO_ERR_EOF, NMO_SEVERITY_ERROR, "Sub-chunk sequence payload out of bounds");
                    }

                    nmo_status_t result = remap_embedded_subchunk_recursive(parent_data,
                                                                            parent_dwords,
                                                                            cursor,
                                                                            child_chunk_version,
                                                                            remap,
                                                                            remapped_count);
                    NMO_RETURN_IF_ERROR(result);
                    cursor += total_dwords;
                }

                continue;
            }

            if (entry >= data_size) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Sub-chunk reference out of bounds");
            }

            size_t child_header_pos = data_start + (size_t)entry;
            nmo_status_t result = remap_embedded_subchunk_recursive(parent_data,
                                                                    parent_dwords,
                                                                    child_header_pos,
                                                                    child_chunk_version,
                                                                    remap,
                                                                    remapped_count);
            NMO_RETURN_IF_ERROR(result);
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t remap_object_ids_recursive(nmo_chunk_t *chunk,
                                               const nmo_id_remap_t *remap,
                                               int *remapped_count) {
    if (!chunk || !remap || !remapped_count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    int local_count = 0;
    nmo_status_t result = NMO_OK;

    if (chunk->chunk_version < NMO_CHUNK_VERSION1) {
        if (chunk->data.count > 0) {
            uint32_t *chunk_data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
            size_t data_size = chunk->data.count;

            static const uint32_t obj_marker[3] = {0xE32BC4C9u, 0x134212E3u, 0xFCBAE9DCu};
            static const uint32_t seq_marker[5] =
                {0xE192BD47u, 0x13246628u, 0x13EAB3CEu, 0x7891AEFCu, 0x13984562u};

            for (size_t pos = 3; pos < data_size; ++pos) {
                if (chunk_data[pos - 3] == obj_marker[0] &&
                    chunk_data[pos - 2] == obj_marker[1] &&
                    chunk_data[pos - 1] == obj_marker[2]) {
                    nmo_object_id_t *id_ref = (nmo_object_id_t *) &chunk_data[pos];
                    local_count += remap_single_id(id_ref, remap);
                }
            }

            if (data_size > 5) {
                for (size_t pos = 2; pos + 2 < data_size; ++pos) {
                    if (chunk_data[pos - 2] == seq_marker[0] &&
                        chunk_data[pos - 1] == seq_marker[1] &&
                        chunk_data[pos] == seq_marker[2] &&
                        chunk_data[pos + 1] == seq_marker[3] &&
                        chunk_data[pos + 2] == seq_marker[4]) {
                        if (pos + 3 >= data_size) {
                            continue;
                        }

                        uint32_t count = chunk_data[pos + 3];
                        size_t first_entry = pos + 4;
                        size_t last_entry = first_entry + (size_t) count;
                        if (count > 0 && last_entry <= data_size) {
                            for (size_t cursor = first_entry; cursor < last_entry; ++cursor) {
                                nmo_object_id_t *id_ref =
                                    (nmo_object_id_t *) &chunk_data[cursor];
                                local_count += remap_single_id(id_ref, remap);
                            }
                        }
                    }
                }
            }
        }
    } else {
        // Remap IDs in this chunk's data buffer
        if (chunk->data.count > 0 && chunk->ids.count > 0) {
            uint32_t *chunk_data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
            uint32_t *chunk_ids = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->ids);
            result = remap_chunk_data_recursive(chunk_data, chunk->data.count,
                                                chunk_ids, chunk->ids.count,
                                                remap, &local_count);
            NMO_RETURN_IF_ERROR(result);
        }
    }

    // Recursively process embedded sub-chunks in the serialized data stream
    if (chunk->chunk_refs.count > 0 && chunk->data.count > 0) {
        uint32_t *chunk_data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
        uint32_t *chunk_refs = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->chunk_refs);
        size_t data_size = chunk->data.count;
        for (size_t i = 0; i < chunk->chunk_refs.count; ++i) {
            uint32_t entry = chunk_refs[i];

            if (entry == LIST_SEQUENCE_MARKER) {
                if (i + 1 >= chunk->chunk_refs.count) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Malformed sub-chunk sequence marker");
                }

                uint32_t seq_pos = chunk_refs[++i];
                if (seq_pos >= data_size) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Sub-chunk sequence offset out of bounds");
                }

                size_t seq_abs = (size_t)seq_pos;
                if (seq_abs + 1 > data_size) {
                    NMO_RETURN_ERROR(NMO_ERR_EOF, NMO_SEVERITY_ERROR, "Sub-chunk sequence header out of bounds");
                }

                uint32_t seq_count = chunk_data[seq_abs];
                size_t cursor = seq_abs + 1;

                for (uint32_t s = 0; s < seq_count; ++s) {
                    if (cursor >= data_size) {
                        NMO_RETURN_ERROR(NMO_ERR_EOF, NMO_SEVERITY_ERROR, "Sub-chunk sequence truncated");
                    }

                    uint32_t payload_dwords = chunk_data[cursor];
                    size_t total_dwords = 1u + (size_t)payload_dwords;
                    if (cursor + total_dwords > data_size) {
                        NMO_RETURN_ERROR(NMO_ERR_EOF, NMO_SEVERITY_ERROR, "Sub-chunk sequence payload out of bounds");
                    }

                    result = remap_embedded_subchunk_recursive(chunk_data,
                                                               chunk->data.count,
                                                               cursor,
                                                               chunk->chunk_version,
                                                               remap,
                                                               &local_count);
                    NMO_RETURN_IF_ERROR(result);
                    cursor += total_dwords;
                }

                continue;
            }

            if (entry >= data_size) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Sub-chunk reference out of bounds");
            }

            result = remap_embedded_subchunk_recursive(chunk_data,
                                                       chunk->data.count,
                                                       (size_t)entry,
                                                       chunk->chunk_version,
                                                       remap,
                                                       &local_count);
            NMO_RETURN_IF_ERROR(result);
        }
    }

    *remapped_count += local_count;
    NMO_RETURN_OK();
}

// =============================================================================
// ID Remapping
// =============================================================================

nmo_status_t nmo_chunk_remap_object_ids(nmo_chunk_t *chunk,
                                        const nmo_id_remap_t *remap) {
    if (!chunk || !remap) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    if (chunk->arena == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Chunk arena unavailable for remap backup");
    }

    void *data_backup = NULL;
    void *ids_backup = NULL;
    void *refs_backup = NULL;
    void *mgrs_backup = NULL;
    size_t data_bytes = 0;
    size_t ids_bytes = 0;
    size_t refs_bytes = 0;
    size_t mgrs_bytes = 0;

    if (chunk->data.count > 0 && chunk->data.data != NULL) {
        data_bytes = chunk->data.count * chunk->data.element_size;
        data_backup = nmo_arena_alloc(chunk->arena, data_bytes, 8);
        if (data_backup == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate chunk data backup");
        }
        memcpy(data_backup, chunk->data.data, data_bytes);
    }

    if (chunk->ids.count > 0 && chunk->ids.data != NULL) {
        ids_bytes = chunk->ids.count * chunk->ids.element_size;
        ids_backup = nmo_arena_alloc(chunk->arena, ids_bytes, 8);
        if (ids_backup == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate chunk ids backup");
        }
        memcpy(ids_backup, chunk->ids.data, ids_bytes);
    }

    if (chunk->chunk_refs.count > 0 && chunk->chunk_refs.data != NULL) {
        refs_bytes = chunk->chunk_refs.count * chunk->chunk_refs.element_size;
        refs_backup = nmo_arena_alloc(chunk->arena, refs_bytes, 8);
        if (refs_backup == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate chunk refs backup");
        }
        memcpy(refs_backup, chunk->chunk_refs.data, refs_bytes);
    }

    if (chunk->managers.count > 0 && chunk->managers.data != NULL) {
        mgrs_bytes = chunk->managers.count * chunk->managers.element_size;
        mgrs_backup = nmo_arena_alloc(chunk->arena, mgrs_bytes, 8);
        if (mgrs_backup == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate chunk managers backup");
        }
        memcpy(mgrs_backup, chunk->managers.data, mgrs_bytes);
    }

    int remapped_count = 0;
    nmo_status_t result = remap_object_ids_recursive(chunk, remap, &remapped_count);
    if (result != NMO_OK) {
        if (data_backup != NULL && chunk->data.data != NULL) {
            memcpy(chunk->data.data, data_backup, data_bytes);
        }
        if (ids_backup != NULL && chunk->ids.data != NULL) {
            memcpy(chunk->ids.data, ids_backup, ids_bytes);
        }
        if (refs_backup != NULL && chunk->chunk_refs.data != NULL) {
            memcpy(chunk->chunk_refs.data, refs_backup, refs_bytes);
        }
        if (mgrs_backup != NULL && chunk->managers.data != NULL) {
            memcpy(chunk->managers.data, mgrs_backup, mgrs_bytes);
        }
        nmo_last_error_clear();
        result = NMO_OK;
    }

    return result;
}
