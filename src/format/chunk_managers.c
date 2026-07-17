// chunk_managers.c - Manager sequence operations
// Implements: start_manager_sequence, write/read_manager_int, start_manager_read_sequence

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include <string.h>

// =============================================================================
// Manager Sequences
// =============================================================================

nmo_status_t nmo_chunk_start_manager_sequence(nmo_chunk_t *chunk,
                                              nmo_guid_t manager_guid,
                                              size_t count) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");
    if (count > UINT32_MAX) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT(
            "Manager sequence count does not fit the 32-bit format");
    }

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (!state) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR, "Failed to get parser state");
    }
    if (state->current_pos > UINT32_MAX) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT(
            "Manager sequence position does not fit the 32-bit format");
    }
    nmo_status_t result = nmo_chunk_check_size(
        chunk, 3u * sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    const size_t managers_count_before = chunk->managers.count;
    const uint32_t options_before = chunk->chunk_options;
    chunk->chunk_options |= NMO_CHUNK_OPTION_MAN;

    // Track sequence start in manager list (CK2 AddEntries)
    uint32_t sentinel = 0xFFFFFFFFu;
    nmo_status_t list_result = nmo_arena_array_append(&chunk->managers, &sentinel);
    if (list_result != NMO_OK) {
        chunk->managers.count = managers_count_before;
        chunk->chunk_options = options_before;
        return list_result;
    }

    uint32_t pos = (uint32_t) state->current_pos;
    list_result = nmo_arena_array_append(&chunk->managers, &pos);
    if (list_result != NMO_OK) {
        chunk->managers.count = managers_count_before;
        chunk->chunk_options = options_before;
        return list_result;
    }

    // Write count then manager GUID
    result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    if (result == NMO_OK) {
        result = nmo_chunk_write_guid(chunk, manager_guid);
    }
    if (result != NMO_OK) {
        chunk->managers.count = managers_count_before;
        chunk->chunk_options = options_before;
    }
    return result;
}

nmo_status_t nmo_chunk_write_manager_int(nmo_chunk_t *chunk,
                                         nmo_guid_t manager_guid,
                                         uint32_t value) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    /* CK2 behavior: CheckSize(12) BEFORE checking/creating m_Managers */
    nmo_status_t result = nmo_chunk_check_size(chunk, 3 * sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (!state) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR, "Failed to get parser state");
    }
    if (state->current_pos > UINT32_MAX) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT(
            "Manager entry position does not fit the 32-bit format");
    }

    /* CK2 behavior: AddEntry(CurrentPos) - track position of GUID start */
    const size_t managers_count_before = chunk->managers.count;
    const uint32_t options_before = chunk->chunk_options;
    chunk->chunk_options |= NMO_CHUNK_OPTION_MAN;
    uint32_t pos = (uint32_t) state->current_pos;
    nmo_status_t list_result = nmo_arena_array_append(&chunk->managers, &pos);
    if (list_result != NMO_OK) {
        chunk->managers.count = managers_count_before;
        chunk->chunk_options = options_before;
        return list_result;
    }

    /* Write manager GUID and value (CK2 order: d1, d2, value) */
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    data[state->current_pos++] = manager_guid.d1;
    data[state->current_pos++] = manager_guid.d2;
    data[state->current_pos++] = value;

    /* Update data_size */
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_read_manager_int(nmo_chunk_t *chunk,
                                        nmo_guid_t *out_manager_guid,
                                        uint32_t *out_value) {
    NMO_CHUNK_CHECK_ARGS2(chunk, out_manager_guid, out_value, "Invalid arguments");

    NMO_CHUNK_CHECK_BOUNDS_MSG(chunk, 3, "Insufficient data for manager int");

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    out_manager_guid->d1 = data[state->current_pos++];
    out_manager_guid->d2 = data[state->current_pos++];
    *out_value = data[state->current_pos++];

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_start_manager_read_sequence(nmo_chunk_t *chunk,
                                                   nmo_guid_t *out_manager_guid,
                                                   size_t *out_count) {
    NMO_CHUNK_CHECK_ARGS2(chunk, out_manager_guid, out_count, "Invalid arguments");
    out_manager_guid->d1 = 0;
    out_manager_guid->d2 = 0;
    *out_count = 0;

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (!state) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR, "Failed to get parser state");
    }
    size_t start_pos = state->current_pos;

    // Read count then manager GUID
    uint32_t count_u32 = 0;
    nmo_guid_t manager_guid = {0, 0};
    nmo_status_t result = nmo_chunk_read_dword(chunk, &count_u32);
    if (result != NMO_OK) {
        state->current_pos = start_pos;
        return result;
    }

    result = nmo_chunk_read_guid(chunk, &manager_guid);
    if (result != NMO_OK) {
        state->current_pos = start_pos;
        return result;
    }

    if (!nmo_chunk_has_read_capacity(chunk, (size_t)count_u32)) {
        state->current_pos = start_pos;
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "Manager sequence count exceeds remaining DWORDs");
    }

    *out_manager_guid = manager_guid;
    *out_count = (size_t)count_u32;
    NMO_RETURN_OK();
}
