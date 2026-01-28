/**
 * @file nmo_chunk_list.h
 * @brief Internal helpers for chunk list management (arena-backed)
 */

#ifndef NMO_CHUNK_LIST_H
#define NMO_CHUNK_LIST_H

#include "core/nmo_arena_array.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline nmo_result_t nmo_chunk_list_ensure_space(
    nmo_arena_t *arena,
    void **data,
    size_t *count,
    size_t *capacity,
    size_t element_size,
    size_t additional)
{
    if (!arena || !data || !count || !capacity || element_size == 0) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid list arguments"));
    }

    nmo_arena_array_t array = {
        .data = *data,
        .count = *count,
        .capacity = *capacity,
        .element_size = element_size,
        .arena = arena,
        .lifecycle = {0}
    };

    nmo_result_t result = nmo_arena_array_ensure_space(&array, additional);
    if (result.code != NMO_OK) {
        return result;
    }

    *data = array.data;
    *capacity = array.capacity;
    return nmo_result_ok();
}

static inline nmo_result_t nmo_chunk_list_append_u32(
    nmo_arena_t *arena,
    uint32_t **data,
    size_t *count,
    size_t *capacity,
    uint32_t value)
{
    nmo_result_t result = nmo_chunk_list_ensure_space(arena,
                                                      (void **) data,
                                                      count,
                                                      capacity,
                                                      sizeof(uint32_t),
                                                      1);
    if (result.code != NMO_OK) {
        return result;
    }

    (*data)[(*count)++] = value;
    return nmo_result_ok();
}

static inline nmo_result_t nmo_chunk_list_append_ptr(
    nmo_arena_t *arena,
    void ***data,
    size_t *count,
    size_t *capacity,
    void *value)
{
    nmo_result_t result = nmo_chunk_list_ensure_space(arena,
                                                      (void **) data,
                                                      count,
                                                      capacity,
                                                      sizeof(void *),
                                                      1);
    if (result.code != NMO_OK) {
        return result;
    }

    (*data)[(*count)++] = value;
    return nmo_result_ok();
}

#ifdef __cplusplus
}
#endif

#endif /* NMO_CHUNK_LIST_H */