#include "core/nmo_parse.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

static const char *skip_spaces(const char *p)
{
    while (p != NULL && *p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static bool at_end_after_spaces(const char *p)
{
    p = skip_spaces(p);
    return p != NULL && *p == '\0';
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_base_is_valid(int base)
{
    return base == 0 || (base >= 2 && base <= 36);
}

static bool char_is_any(char c, const char *choices)
{
    if (choices == NULL) {
        return false;
    }
    for (const char *p = choices; *p != '\0'; p++) {
        if (c == *p) {
            return true;
        }
    }
    return false;
}

static nmo_status_t parse_f32_token(
    const char *text,
    const char **out_end,
    float *out_value)
{
    if (text == NULL || out_end == NULL || out_value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const char *p = skip_spaces(text);
    if (*p == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    errno = 0;
    char *end = NULL;
    float value = strtof(p, &end);
    if (end == p || errno == ERANGE) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_value = value;
    *out_end = end;
    return NMO_OK;
}

nmo_status_t nmo_parse_object_id(
    const char *text,
    nmo_object_id_t *out_id)
{
    if (text == NULL || out_id == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const char *p = skip_spaces(text);
    if (*p == '#') {
        p++;
    }
    return nmo_parse_u32_range(p, 0, UINT32_MAX, out_id);
}

nmo_status_t nmo_parse_i32_range(
    const char *text,
    int32_t min_value,
    int32_t max_value,
    int32_t *out_value)
{
    return nmo_parse_i32_range_base(text, 10, min_value, max_value, out_value);
}

nmo_status_t nmo_parse_i32_range_base(
    const char *text,
    int base,
    int32_t min_value,
    int32_t max_value,
    int32_t *out_value)
{
    if (text == NULL || out_value == NULL || min_value > max_value) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (!parse_base_is_valid(base)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const char *p = skip_spaces(text);
    if (*p == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    errno = 0;
    char *end = NULL;
    long value = strtol(p, &end, base);
    if (end == p || errno == ERANGE || !at_end_after_spaces(end)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (value < (long)min_value || value > (long)max_value) {
        return NMO_ERR_OUT_OF_BOUNDS;
    }

    *out_value = (int32_t)value;
    return NMO_OK;
}

nmo_status_t nmo_parse_u32_range(
    const char *text,
    uint32_t min_value,
    uint32_t max_value,
    uint32_t *out_value)
{
    return nmo_parse_u32_range_base(text, 10, min_value, max_value, out_value);
}

nmo_status_t nmo_parse_u32_range_base(
    const char *text,
    int base,
    uint32_t min_value,
    uint32_t max_value,
    uint32_t *out_value)
{
    if (text == NULL || out_value == NULL || min_value > max_value) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (!parse_base_is_valid(base)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const char *p = skip_spaces(text);
    if (*p == '\0' || *p == '-') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(p, &end, base);
    if (end == p || errno == ERANGE || !at_end_after_spaces(end) || value > UINT32_MAX) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (value < (unsigned long)min_value || value > (unsigned long)max_value) {
        return NMO_ERR_OUT_OF_BOUNDS;
    }

    *out_value = (uint32_t)value;
    return NMO_OK;
}

nmo_status_t nmo_parse_size_range_base(
    const char *text,
    int base,
    size_t min_value,
    size_t max_value,
    size_t *out_value)
{
    if (text == NULL || out_value == NULL || min_value > max_value) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (!parse_base_is_valid(base)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const char *p = skip_spaces(text);
    if (*p == '\0' || *p == '-') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(p, &end, base);
    if (end == p || errno == ERANGE || !at_end_after_spaces(end) ||
        value > (unsigned long long)SIZE_MAX) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (value < (unsigned long long)min_value || value > (unsigned long long)max_value) {
        return NMO_ERR_OUT_OF_BOUNDS;
    }

    *out_value = (size_t)value;
    return NMO_OK;
}

nmo_status_t nmo_parse_i64_range_base(
    const char *text,
    int base,
    int64_t min_value,
    int64_t max_value,
    int64_t *out_value)
{
    if (text == NULL || out_value == NULL || min_value > max_value) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (!parse_base_is_valid(base)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const char *p = skip_spaces(text);
    if (*p == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    errno = 0;
    char *end = NULL;
    long long value = strtoll(p, &end, base);
    if (end == p || errno == ERANGE || !at_end_after_spaces(end)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (value < (long long)min_value || value > (long long)max_value) {
        return NMO_ERR_OUT_OF_BOUNDS;
    }

    *out_value = (int64_t)value;
    return NMO_OK;
}

nmo_status_t nmo_parse_u64_range_base(
    const char *text,
    int base,
    uint64_t min_value,
    uint64_t max_value,
    uint64_t *out_value)
{
    if (text == NULL || out_value == NULL || min_value > max_value) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (!parse_base_is_valid(base)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const char *p = skip_spaces(text);
    if (*p == '\0' || *p == '-') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(p, &end, base);
    if (end == p || errno == ERANGE || !at_end_after_spaces(end)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (value < (unsigned long long)min_value ||
        value > (unsigned long long)max_value) {
        return NMO_ERR_OUT_OF_BOUNDS;
    }

    *out_value = (uint64_t)value;
    return NMO_OK;
}

nmo_status_t nmo_parse_f32(
    const char *text,
    float *out_value)
{
    if (text == NULL || out_value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const char *p = skip_spaces(text);
    if (*p == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    errno = 0;
    char *end = NULL;
    float value = strtof(p, &end);
    if (end == p || errno == ERANGE || !at_end_after_spaces(end)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_value = value;
    return NMO_OK;
}

nmo_status_t nmo_parse_f64(
    const char *text,
    double *out_value)
{
    if (text == NULL || out_value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const char *p = skip_spaces(text);
    if (*p == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    errno = 0;
    char *end = NULL;
    double value = strtod(p, &end);
    if (end == p || errno == ERANGE || !at_end_after_spaces(end)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_value = value;
    return NMO_OK;
}

nmo_status_t nmo_parse_f32_tuple(
    const char *text,
    float *out_values,
    size_t expected_count)
{
    if (text == NULL || out_values == NULL || expected_count == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const char *p = skip_spaces(text);
    for (size_t i = 0; i < expected_count; i++) {
        if (*p == '\0') {
            return NMO_ERR_INVALID_ARGUMENT;
        }

        const char *end = NULL;
        float value = 0.0f;
        if (parse_f32_token(p, &end, &value) != NMO_OK) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        out_values[i] = value;

        p = skip_spaces(end);
        if (i + 1u < expected_count) {
            if (*p != ',') {
                return NMO_ERR_INVALID_ARGUMENT;
            }
            p = skip_spaces(p + 1);
        } else if (*p != '\0') {
            return NMO_ERR_INVALID_ARGUMENT;
        }
    }

    return NMO_OK;
}

nmo_status_t nmo_parse_f32_parenthesized_tuple(
    const char *text,
    const char *separators,
    float *out_values,
    size_t expected_count)
{
    if (text == NULL || separators == NULL || *separators == '\0' ||
        out_values == NULL || expected_count == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const char *p = skip_spaces(text);
    if (*p != '(') {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    p = skip_spaces(p + 1);

    for (size_t i = 0; i < expected_count; i++) {
        const char *end = NULL;
        float value = 0.0f;
        if (parse_f32_token(p, &end, &value) != NMO_OK) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        out_values[i] = value;

        p = skip_spaces(end);
        if (i + 1u < expected_count) {
            if (!char_is_any(*p, separators)) {
                return NMO_ERR_INVALID_ARGUMENT;
            }
            p = skip_spaces(p + 1);
        }
    }

    if (*p != ')') {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return at_end_after_spaces(p + 1) ? NMO_OK : NMO_ERR_INVALID_ARGUMENT;
}

nmo_status_t nmo_parse_hex_color(
    const char *text,
    uint32_t *out_color)
{
    if (text == NULL || out_color == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const char *p = skip_spaces(text);
    if (*p == '#') {
        p++;
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }

    uint32_t value = 0;
    size_t digits = 0;
    while (*p != '\0' && !isspace((unsigned char)*p)) {
        int hv = hex_value(*p);
        if (hv < 0 || digits >= 8) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        value = (value << 4) | (uint32_t)hv;
        digits++;
        p++;
    }

    if ((digits != 6 && digits != 8) || !at_end_after_spaces(p)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_color = value;
    return NMO_OK;
}

nmo_status_t nmo_parse_hex_bytes(
    const char *text,
    uint8_t *out_bytes,
    size_t out_capacity,
    size_t *out_count)
{
    if (text == NULL || out_count == NULL || (out_capacity > 0 && out_bytes == NULL)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t count = 0;
    int high = -1;
    for (const char *p = text; *p != '\0'; p++) {
        if (isspace((unsigned char)*p)) {
            continue;
        }

        int hv = hex_value(*p);
        if (hv < 0) {
            return NMO_ERR_INVALID_ARGUMENT;
        }

        if (high < 0) {
            high = hv;
            continue;
        }

        if (count >= out_capacity) {
            *out_count = count;
            return NMO_ERR_OUT_OF_BOUNDS;
        }
        out_bytes[count++] = (uint8_t)((high << 4) | hv);
        high = -1;
    }

    if (high >= 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_count = count;
    return NMO_OK;
}
