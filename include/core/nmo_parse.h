#ifndef NMO_PARSE_H
#define NMO_PARSE_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

NMO_API nmo_status_t nmo_parse_object_id(
    const char *text,
    nmo_object_id_t *out_id);

NMO_API nmo_status_t nmo_parse_i32_range(
    const char *text,
    int32_t min_value,
    int32_t max_value,
    int32_t *out_value);

NMO_API nmo_status_t nmo_parse_i32_range_base(
    const char *text,
    int base,
    int32_t min_value,
    int32_t max_value,
    int32_t *out_value);

NMO_API nmo_status_t nmo_parse_u32_range(
    const char *text,
    uint32_t min_value,
    uint32_t max_value,
    uint32_t *out_value);

NMO_API nmo_status_t nmo_parse_u32_range_base(
    const char *text,
    int base,
    uint32_t min_value,
    uint32_t max_value,
    uint32_t *out_value);

NMO_API nmo_status_t nmo_parse_size_range_base(
    const char *text,
    int base,
    size_t min_value,
    size_t max_value,
    size_t *out_value);

NMO_API nmo_status_t nmo_parse_i64_range_base(
    const char *text,
    int base,
    int64_t min_value,
    int64_t max_value,
    int64_t *out_value);

NMO_API nmo_status_t nmo_parse_u64_range_base(
    const char *text,
    int base,
    uint64_t min_value,
    uint64_t max_value,
    uint64_t *out_value);

NMO_API nmo_status_t nmo_parse_f32(
    const char *text,
    float *out_value);

NMO_API nmo_status_t nmo_parse_f64(
    const char *text,
    double *out_value);

NMO_API nmo_status_t nmo_parse_f32_tuple(
    const char *text,
    float *out_values,
    size_t expected_count);

NMO_API nmo_status_t nmo_parse_f32_parenthesized_tuple(
    const char *text,
    const char *separators,
    float *out_values,
    size_t expected_count);

NMO_API nmo_status_t nmo_parse_hex_color(
    const char *text,
    uint32_t *out_color);

NMO_API nmo_status_t nmo_parse_hex_bytes(
    const char *text,
    uint8_t *out_bytes,
    size_t out_capacity,
    size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PARSE_H */
