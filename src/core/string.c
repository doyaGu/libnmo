/**
 * @file string.c
 * @brief Dynamic string implementation mirroring key XString features.
 */

#include "core/nmo_string.h"
#include "core/nmo_parse.h"
#include "core/nmo_utils.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>

#define NMO_STRING_DEFAULT_CAPACITY 16u

static const char *nmo_string_empty_cstr(void) {
    return "";
}

static int nmo_string_validate(nmo_string_t *string) {
    return string != NULL;
}

static nmo_status_t nmo_string_ensure_capacity(nmo_string_t *string, size_t needed) {
    if (!nmo_string_validate(string)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "string must not be NULL");
    }

    if (needed <= string->capacity) {
        if (string->data != NULL) {
            string->data[string->length] = '\0';
        }
        NMO_RETURN_OK();
    }

    size_t target = string->capacity ? string->capacity : NMO_STRING_DEFAULT_CAPACITY;
    while (target < needed) {
        if (target > SIZE_MAX / 2u) {
            target = needed;
            break;
        }
        target *= 2u;
    }

    if (target >= SIZE_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Requested string capacity is too large");
    }

    size_t bytes = target + 1u;
    char *buffer = (char *)nmo_alloc(&string->allocator, bytes, alignof(char));
    if (buffer == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate string buffer");
    }

    if (string->data != NULL && string->length > 0) {
        memcpy(buffer, string->data, string->length);
    }

    buffer[string->length] = '\0';

    if (string->data != NULL) {
        nmo_free(&string->allocator, string->data);
    }

    string->data = buffer;
    string->capacity = target;

    NMO_RETURN_OK();
}

static nmo_status_t nmo_string_prepare_write(nmo_string_t *string, size_t total_length) {
    if (!nmo_string_validate(string)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "string must not be NULL");
    }

    nmo_status_t reserve = nmo_string_ensure_capacity(string, total_length);
    if (reserve != NMO_OK) {
        return reserve;
    }

    if (string->data == NULL) {
        size_t bytes = (string->capacity ? string->capacity : 1u) + 1u;
        string->data = (char *)nmo_alloc(&string->allocator, bytes, alignof(char));
        if (string->data == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate string buffer");
        }
        string->capacity = bytes - 1u;
        string->length = 0;
        string->data[0] = '\0';
    }

    NMO_RETURN_OK();
}

static int nmo_string_source_aliases(
    const nmo_string_t *string,
    const char *data,
    size_t length)
{
    if (string == NULL || string->data == NULL || data == NULL || length == 0) {
        return 0;
    }

    uintptr_t buffer_address = (uintptr_t)(const void *)string->data;
    uintptr_t source_address = (uintptr_t)(const void *)data;
    return source_address >= buffer_address &&
           source_address - buffer_address <= string->capacity;
}

static nmo_status_t nmo_string_snapshot_source(
    nmo_string_t *string,
    const char **data,
    size_t length,
    char **out_snapshot)
{
    *out_snapshot = NULL;
    if (!nmo_string_source_aliases(string, *data, length)) {
        return NMO_OK;
    }

    char *snapshot = (char *)nmo_alloc(&string->allocator, length, alignof(char));
    if (snapshot == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to snapshot aliased string input");
    }

    memcpy(snapshot, *data, length);
    *data = snapshot;
    *out_snapshot = snapshot;
    return NMO_OK;
}

static inline size_t nmo_string_min_size(size_t a, size_t b) {
    return (a < b) ? a : b;
}

static inline int nmo_string_is_whitespace(char ch) {
    return isspace((unsigned char)ch);
}

static inline int nmo_string_case_cmp_char(char a, char b) {
    return tolower((unsigned char)a) - tolower((unsigned char)b);
}

/* ------------------------------------------------------------------------- */
/* Initialization / lifetime                                                  */
/* ------------------------------------------------------------------------- */

nmo_status_t nmo_string_init(nmo_string_t *string, const nmo_allocator_t *allocator) {
    if (string == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "string must not be NULL");
    }

    memset(string, 0, sizeof(*string));
    string->allocator = allocator ? *allocator : nmo_allocator_default();
    NMO_RETURN_OK();
}

nmo_status_t nmo_string_init_cstr(nmo_string_t *string,
                                  const char *cstr,
                                  const nmo_allocator_t *allocator) {
    nmo_status_t init = nmo_string_init(string, allocator);
    if (init != NMO_OK) {
        return init;
    }
    return nmo_string_assign(string, cstr);
}

nmo_status_t nmo_string_init_view(nmo_string_t *string,
                                  nmo_string_view_t view,
                                  const nmo_allocator_t *allocator) {
    nmo_status_t init = nmo_string_init(string, allocator);
    if (init != NMO_OK) {
        return init;
    }
    return nmo_string_assign_view(string, view);
}

void nmo_string_dispose(nmo_string_t *string) {
    if (string == NULL) {
        return;
    }

    if (string->data != NULL) {
        nmo_free(&string->allocator, string->data);
    }

    memset(string, 0, sizeof(*string));
}

/* ------------------------------------------------------------------------- */
/* Capacity / metadata                                                        */
/* ------------------------------------------------------------------------- */

size_t nmo_string_length(const nmo_string_t *string) {
    return (string != NULL) ? string->length : 0u;
}

size_t nmo_string_capacity(const nmo_string_t *string) {
    return (string != NULL) ? string->capacity : 0u;
}

int nmo_string_empty(const nmo_string_t *string) {
    return nmo_string_length(string) == 0u;
}

const char *nmo_string_c_str(const nmo_string_t *string) {
    if (string == NULL || string->data == NULL) {
        return nmo_string_empty_cstr();
    }
    return string->data;
}

char *nmo_string_data(nmo_string_t *string) {
    if (string == NULL) {
        return NULL;
    }
    return string->data;
}

nmo_status_t nmo_string_reserve(nmo_string_t *string, size_t capacity) {
    return nmo_string_ensure_capacity(string, capacity);
}

nmo_status_t nmo_string_shrink_to_fit(nmo_string_t *string) {
    if (!nmo_string_validate(string)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "string must not be NULL");
    }

    if (string->length == 0u) {
        if (string->data != NULL) {
            nmo_free(&string->allocator, string->data);
            string->data = NULL;
        }
        string->capacity = 0u;
        NMO_RETURN_OK();
    }

    if (string->length == string->capacity) {
        NMO_RETURN_OK();
    }

    size_t bytes = string->length + 1u;
    char *buffer = (char *)nmo_alloc(&string->allocator, bytes, alignof(char));
    if (buffer == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate shrink buffer");
    }

    memcpy(buffer, string->data, string->length);
    buffer[string->length] = '\0';

    if (string->data != NULL) {
        nmo_free(&string->allocator, string->data);
    }

    string->data = buffer;
    string->capacity = string->length;
    NMO_RETURN_OK();
}

void nmo_string_clear(nmo_string_t *string) {
    if (!nmo_string_validate(string)) {
        return;
    }
    string->length = 0u;
    if (string->data != NULL) {
        string->data[0] = '\0';
    }
}

/* ------------------------------------------------------------------------- */
/* Assignment / append                                                        */
/* ------------------------------------------------------------------------- */

nmo_status_t nmo_string_assign(nmo_string_t *string, const char *cstr) {
    if (cstr == NULL) {
        nmo_string_clear(string);
        NMO_RETURN_OK();
    }
    return nmo_string_assign_len(string, cstr, strlen(cstr));
}

nmo_status_t nmo_string_assign_len(nmo_string_t *string,
                                   const char *data,
                                   size_t length) {
    if (!nmo_string_validate(string)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "string must not be NULL");
    }
    if (length > 0 && data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "data must not be NULL when length > 0");
    }

    char *snapshot = NULL;
    nmo_status_t snapshot_result = nmo_string_snapshot_source(
        string, &data, length, &snapshot);
    if (snapshot_result != NMO_OK) {
        return snapshot_result;
    }

    nmo_status_t prep = nmo_string_prepare_write(string, length);
    if (prep != NMO_OK) {
        nmo_free(&string->allocator, snapshot);
        return prep;
    }

    if (length > 0) {
        memcpy(string->data, data, length);
    }

    string->length = length;
    string->data[length] = '\0';
    nmo_free(&string->allocator, snapshot);
    NMO_RETURN_OK();
}

nmo_status_t nmo_string_assign_view(nmo_string_t *string,
                                    nmo_string_view_t view) {
    if (view.length == 0) {
        return nmo_string_assign_len(string, NULL, 0);
    }
    return nmo_string_assign_len(string, view.data, view.length);
}

nmo_status_t nmo_string_copy(nmo_string_t *dest,
                             const nmo_string_t *src) {
    if (dest == src) {
        NMO_RETURN_OK();
    }
    if (src == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "source string must not be NULL");
    }
    return nmo_string_assign_len(dest, src->data, src->length);
}

nmo_status_t nmo_string_append(nmo_string_t *string, const char *cstr) {
    if (cstr == NULL) {
        NMO_RETURN_OK();
    }
    return nmo_string_append_len(string, cstr, strlen(cstr));
}

nmo_status_t nmo_string_append_len(nmo_string_t *string,
                                   const char *data,
                                   size_t length) {
    if (!nmo_string_validate(string)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "string must not be NULL");
    }
    if (length == 0) {
        NMO_RETURN_OK();
    }
    if (data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "data must not be NULL when length > 0");
    }
    size_t new_length = 0;
    if (!nmo_safe_add_size(nmo_string_length(string), length, &new_length)) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "string length overflow");
    }

    char *snapshot = NULL;
    nmo_status_t snapshot_result = nmo_string_snapshot_source(
        string, &data, length, &snapshot);
    if (snapshot_result != NMO_OK) {
        return snapshot_result;
    }

    nmo_status_t prep = nmo_string_prepare_write(string, new_length);
    if (prep != NMO_OK) {
        nmo_free(&string->allocator, snapshot);
        return prep;
    }

    memcpy(string->data + string->length, data, length);
    string->length = new_length;
    string->data[string->length] = '\0';
    nmo_free(&string->allocator, snapshot);
    NMO_RETURN_OK();
}

nmo_status_t nmo_string_append_view(nmo_string_t *string,
                                    nmo_string_view_t view) {
    return nmo_string_append_len(string, view.data, view.length);
}

nmo_status_t nmo_string_append_char(nmo_string_t *string, char ch) {
    size_t new_length = 0;
    if (!nmo_safe_add_size(nmo_string_length(string), 1u, &new_length)) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "string length overflow");
    }
    nmo_status_t prep = nmo_string_prepare_write(string, new_length);
    if (prep != NMO_OK) {
        return prep;
    }

    string->data[string->length++] = ch;
    string->data[string->length] = '\0';
    NMO_RETURN_OK();
}

/* ------------------------------------------------------------------------- */
/* Mutation helpers                                                           */
/* ------------------------------------------------------------------------- */

nmo_status_t nmo_string_insert(nmo_string_t *string,
                               size_t index,
                               const char *data,
                               size_t length) {
    if (!nmo_string_validate(string)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "string must not be NULL");
    }
    if (index > string->length) {
        NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR, "Insertion index out of bounds");
    }
    if (length == 0) {
        NMO_RETURN_OK();
    }
    if (data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "data must not be NULL when length > 0");
    }

    size_t new_length = 0;
    if (!nmo_safe_add_size(string->length, length, &new_length)) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "string length overflow");
    }

    char *snapshot = NULL;
    nmo_status_t snapshot_result = nmo_string_snapshot_source(
        string, &data, length, &snapshot);
    if (snapshot_result != NMO_OK) {
        return snapshot_result;
    }

    nmo_status_t prep = nmo_string_prepare_write(string, new_length);
    if (prep != NMO_OK) {
        nmo_free(&string->allocator, snapshot);
        return prep;
    }

    char *dst = string->data + index;
    memmove(dst + length, dst, string->length - index + 1u);
    memcpy(dst, data, length);
    string->length = new_length;
    nmo_free(&string->allocator, snapshot);
    NMO_RETURN_OK();
}

nmo_status_t nmo_string_erase(nmo_string_t *string,
                              size_t index,
                              size_t length) {
    if (!nmo_string_validate(string)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "string must not be NULL");
    }
    if (index > string->length) {
        NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR, "Erase index out of bounds");
    }
    if (length == 0) {
        NMO_RETURN_OK();
    }
    size_t available = string->length - index;
    size_t erase_count = length > available ? available : length;
    if (erase_count == 0) {
        NMO_RETURN_OK();
    }

    char *dst = string->data + index;
    memmove(dst, dst + erase_count, available - erase_count + 1u);
    string->length -= erase_count;
    NMO_RETURN_OK();
}

nmo_status_t nmo_string_replace(nmo_string_t *string,
                                size_t index,
                                size_t length,
                                const char *data,
                                size_t new_length) {
    if (!nmo_string_validate(string)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "string must not be NULL");
    }
    if (index > string->length) {
        NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR, "Replace index out of bounds");
    }
    if (new_length > 0 && data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "data must not be NULL when new_length > 0");
    }

    size_t current_span = nmo_string_min_size(length, string->length - index);
    size_t base_length = string->length - current_span;
    size_t updated_length = 0;
    if (!nmo_safe_add_size(base_length, new_length, &updated_length)) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "string length overflow");
    }

    char *snapshot = NULL;
    nmo_status_t snapshot_result = nmo_string_snapshot_source(
        string, &data, new_length, &snapshot);
    if (snapshot_result != NMO_OK) {
        return snapshot_result;
    }

    nmo_status_t prep = nmo_string_prepare_write(string, updated_length);
    if (prep != NMO_OK) {
        nmo_free(&string->allocator, snapshot);
        return prep;
    }

    char *base = string->data;
    size_t tail = string->length - (index + current_span);

    if (new_length != current_span) {
        memmove(base + index + new_length,
                base + index + current_span,
                tail + 1u);
    }

    if (new_length > 0) {
        memcpy(base + index, data, new_length);
    }

    string->length = updated_length;
    string->data[string->length] = '\0';
    nmo_free(&string->allocator, snapshot);
    NMO_RETURN_OK();
}

nmo_status_t nmo_string_replace_all(nmo_string_t *string,
                                    nmo_string_view_t needle,
                                    nmo_string_view_t replacement,
                                    size_t *out_count) {
    if (needle.length == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "needle must not be empty");
    }
    if (!nmo_string_validate(string)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "string must not be NULL");
    }
    if (needle.data == NULL || (replacement.length > 0 && replacement.data == NULL)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "string view data must not be NULL");
    }

    char *needle_snapshot = NULL;
    char *replacement_snapshot = NULL;
    nmo_status_t snapshot_result = nmo_string_snapshot_source(
        string, &needle.data, needle.length, &needle_snapshot);
    if (snapshot_result != NMO_OK) {
        return snapshot_result;
    }
    snapshot_result = nmo_string_snapshot_source(
        string, &replacement.data, replacement.length, &replacement_snapshot);
    if (snapshot_result != NMO_OK) {
        nmo_free(&string->allocator, needle_snapshot);
        return snapshot_result;
    }

    size_t pos = 0;
    size_t replaced = 0;
    while (1) {
        size_t found = nmo_string_find(string, needle, pos);
        if (found == SIZE_MAX) {
            break;
        }
        nmo_status_t repl = nmo_string_replace(string,
                                               found,
                                               needle.length,
                                               replacement.data,
                                               replacement.length);
        if (repl != NMO_OK) {
            nmo_free(&string->allocator, replacement_snapshot);
            nmo_free(&string->allocator, needle_snapshot);
            return repl;
        }
        pos = found + replacement.length;
        ++replaced;
    }

    if (out_count != NULL) {
        *out_count = replaced;
    }
    nmo_free(&string->allocator, replacement_snapshot);
    nmo_free(&string->allocator, needle_snapshot);
    NMO_RETURN_OK();
}

/* ------------------------------------------------------------------------- */
/* Search / comparison                                                        */
/* ------------------------------------------------------------------------- */

size_t nmo_string_find(const nmo_string_t *string,
                       nmo_string_view_t needle,
                       size_t start) {
    if (string == NULL || string->data == NULL) {
        return SIZE_MAX;
    }
    if (needle.length == 0) {
        return (start <= string->length) ? start : SIZE_MAX;
    }
    if (needle.data == NULL) {
        return SIZE_MAX;
    }
    if (start > string->length) {
        return SIZE_MAX;
    }
    if (needle.length > string->length) {
        return SIZE_MAX;
    }
    size_t limit = string->length - needle.length;
    for (size_t i = start; i <= limit; ++i) {
        if (memcmp(string->data + i, needle.data, needle.length) == 0) {
            return i;
        }
    }
    return SIZE_MAX;
}

size_t nmo_string_find_char(const nmo_string_t *string,
                            char ch,
                            size_t start) {
    if (string == NULL || string->data == NULL) {
        return SIZE_MAX;
    }
    if (start >= string->length) {
        if (start == string->length && ch == '\0') {
            return string->length;
        }
        return SIZE_MAX;
    }

    for (size_t i = start; i < string->length; ++i) {
        if (string->data[i] == ch) {
            return i;
        }
    }

    return SIZE_MAX;
}

size_t nmo_string_rfind_char(const nmo_string_t *string,
                             char ch,
                             size_t start) {
    if (string == NULL || string->data == NULL || string->length == 0) {
        return SIZE_MAX;
    }

    size_t begin = string->length ? string->length - 1u : 0u;
    size_t i = (start >= string->length) ? begin : start;

    while (1) {
        if (string->data[i] == ch) {
            return i;
        }
        if (i == 0) {
            break;
        }
        --i;
    }

    return SIZE_MAX;
}

int nmo_string_contains(const nmo_string_t *string,
                        nmo_string_view_t needle) {
    return nmo_string_find(string, needle, 0u) != SIZE_MAX;
}

static int nmo_string_starts_with_impl(const nmo_string_t *string,
                                       nmo_string_view_t prefix,
                                       int ignore_case) {
    if (string == NULL || prefix.length > string->length || prefix.data == NULL) {
        return 0;
    }

    for (size_t i = 0; i < prefix.length; ++i) {
        char a = string->data[i];
        char b = prefix.data[i];
        if (ignore_case) {
            if (tolower((unsigned char)a) != tolower((unsigned char)b)) {
                return 0;
            }
        } else if (a != b) {
            return 0;
        }
    }
    return 1;
}

int nmo_string_starts_with(const nmo_string_t *string,
                           nmo_string_view_t prefix) {
    return nmo_string_starts_with_impl(string, prefix, 0);
}

int nmo_string_istarts_with(const nmo_string_t *string,
                            nmo_string_view_t prefix) {
    return nmo_string_starts_with_impl(string, prefix, 1);
}

static int nmo_string_ends_with_impl(const nmo_string_t *string,
                                     nmo_string_view_t suffix,
                                     int ignore_case) {
    if (string == NULL || suffix.length > string->length || suffix.data == NULL) {
        return 0;
    }
    size_t offset = string->length - suffix.length;
    for (size_t i = 0; i < suffix.length; ++i) {
        char a = string->data[offset + i];
        char b = suffix.data[i];
        if (ignore_case) {
            if (tolower((unsigned char)a) != tolower((unsigned char)b)) {
                return 0;
            }
        } else if (a != b) {
            return 0;
        }
    }
    return 1;
}

int nmo_string_ends_with(const nmo_string_t *string,
                         nmo_string_view_t suffix) {
    return nmo_string_ends_with_impl(string, suffix, 0);
}

int nmo_string_iends_with(const nmo_string_t *string,
                          nmo_string_view_t suffix) {
    return nmo_string_ends_with_impl(string, suffix, 1);
}

int nmo_string_compare(const nmo_string_t *lhs,
                       const nmo_string_t *rhs) {
    if (lhs == rhs) {
        return 0;
    }
    if (lhs == NULL) {
        return rhs && rhs->length ? -1 : 0;
    }
    if (rhs == NULL) {
        return lhs->length ? 1 : 0;
    }
    size_t min_len = nmo_string_min_size(lhs->length, rhs->length);
    if (min_len > 0) {
        int cmp = memcmp(lhs->data, rhs->data, min_len);
        if (cmp != 0) {
            return cmp;
        }
    }
    if (lhs->length == rhs->length) {
        return 0;
    }
    return (lhs->length < rhs->length) ? -1 : 1;
}

int nmo_string_compare_view(const nmo_string_t *lhs,
                            nmo_string_view_t rhs) {
    if (lhs == NULL) {
        return (rhs.length == 0) ? 0 : -1;
    }
    size_t min_len = nmo_string_min_size(lhs->length, rhs.length);
    if (rhs.data == NULL && rhs.length > 0) {
        return 1;
    }
    if (min_len > 0) {
        int cmp = memcmp(lhs->data, rhs.data, min_len);
        if (cmp != 0) {
            return cmp;
        }
    }
    if (lhs->length == rhs.length) {
        return 0;
    }
    return (lhs->length < rhs.length) ? -1 : 1;
}

int nmo_string_icompare_view(const nmo_string_t *lhs,
                             nmo_string_view_t rhs) {
    if (lhs == NULL) {
        return (rhs.length == 0) ? 0 : -1;
    }
    if (rhs.data == NULL && rhs.length > 0) {
        return 1;
    }
    size_t min_len = nmo_string_min_size(lhs->length, rhs.length);
    if (min_len > 0) {
        for (size_t i = 0; i < min_len; ++i) {
            int diff = nmo_string_case_cmp_char(lhs->data[i], rhs.data[i]);
            if (diff != 0) {
                return diff;
            }
        }
    }
    if (lhs->length == rhs.length) {
        return 0;
    }
    return (lhs->length < rhs.length) ? -1 : 1;
}

int nmo_string_equals(const nmo_string_t *lhs,
                      const nmo_string_t *rhs) {
    return nmo_string_compare(lhs, rhs) == 0;
}

int nmo_string_equals_view(const nmo_string_t *lhs,
                           nmo_string_view_t rhs) {
    return nmo_string_compare_view(lhs, rhs) == 0;
}

int nmo_string_iequals_view(const nmo_string_t *lhs,
                            nmo_string_view_t rhs) {
    return nmo_string_icompare_view(lhs, rhs) == 0;
}

int nmo_string_slice_view(const nmo_string_t *string,
                          size_t start,
                          size_t length,
                          nmo_string_view_t *out_view) {
    if (out_view != NULL) {
        out_view->data = NULL;
        out_view->length = 0;
    }
    if (string == NULL || out_view == NULL) {
        return 0;
    }
    if (start > string->length) {
        return 0;
    }
    size_t remaining = string->length - start;
    size_t actual = length > remaining ? remaining : length;
    out_view->data = string->data ? string->data + start : NULL;
    out_view->length = actual;
    return 1;
}

nmo_status_t nmo_string_substr(nmo_string_t *dest,
                               const nmo_string_t *src,
                               size_t start,
                               size_t length) {
    if (dest == NULL || src == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid substring arguments");
    }
    nmo_string_view_t view;
    if (!nmo_string_slice_view(src, start, length, &view)) {
        NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR, "Substring range out of bounds");
    }
    if (view.length == 0) {
        nmo_string_clear(dest);
        NMO_RETURN_OK();
    }
    return nmo_string_assign_len(dest, view.data, view.length);
}

/* ------------------------------------------------------------------------- */
/* Transformations                                                            */
/* ------------------------------------------------------------------------- */

void nmo_string_to_upper(nmo_string_t *string) {
    if (!nmo_string_validate(string) || string->data == NULL) {
        return;
    }
    for (size_t i = 0; i < string->length; ++i) {
        string->data[i] = (char)toupper((unsigned char)string->data[i]);
    }
}

void nmo_string_to_lower(nmo_string_t *string) {
    if (!nmo_string_validate(string) || string->data == NULL) {
        return;
    }
    for (size_t i = 0; i < string->length; ++i) {
        string->data[i] = (char)tolower((unsigned char)string->data[i]);
    }
}

void nmo_string_trim_left(nmo_string_t *string) {
    if (!nmo_string_validate(string) || string->data == NULL) {
        return;
    }
    size_t i = 0;
    while (i < string->length && nmo_string_is_whitespace(string->data[i])) {
        ++i;
    }
    if (i == 0) {
        return;
    }
    size_t remaining = string->length - i;
    memmove(string->data, string->data + i, remaining + 1u);
    string->length = remaining;
}

void nmo_string_trim_right(nmo_string_t *string) {
    if (!nmo_string_validate(string) || string->data == NULL || string->length == 0) {
        return;
    }
    size_t i = string->length;
    while (i > 0) {
        if (!nmo_string_is_whitespace(string->data[i - 1u])) {
            break;
        }
        --i;
    }
    if (i == string->length) {
        return;
    }
    string->length = i;
    string->data[i] = '\0';
}

void nmo_string_trim(nmo_string_t *string) {
    nmo_string_trim_right(string);
    nmo_string_trim_left(string);
}

/* ------------------------------------------------------------------------- */
/* Formatting                                                                 */
/* ------------------------------------------------------------------------- */

static nmo_status_t nmo_string_vformat_internal(nmo_string_t *string,
                                                int append,
                                                const char *fmt,
                                                va_list args) {
    if (!nmo_string_validate(string) || fmt == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid format arguments");
    }

    va_list copy;
    va_copy(copy, args);
#if defined(_MSC_VER)
    int needed = _vscprintf(fmt, copy);
#else
    int needed = vsnprintf(NULL, 0, fmt, copy);
#endif
    va_end(copy);

    if (needed < 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "vsnprintf failed");
    }

    size_t offset = append ? string->length : 0u;
    size_t total_length = offset + (size_t)needed;

    if (!append) {
        string->length = 0;
    }

    nmo_status_t prep = nmo_string_prepare_write(string, total_length);
    if (prep != NMO_OK) {
        return prep;
    }

    va_list copy2;
    va_copy(copy2, args);
    vsnprintf(string->data + offset, (string->capacity - offset) + 1u, fmt, copy2);
    va_end(copy2);

    string->length = total_length;
    string->data[string->length] = '\0';
    NMO_RETURN_OK();
}

nmo_status_t nmo_string_format(nmo_string_t *string,
                               const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    nmo_status_t result = nmo_string_vformat_internal(string, 0, fmt, args);
    va_end(args);
    return result;
}

nmo_status_t nmo_string_formatv(nmo_string_t *string,
                                const char *fmt,
                                va_list args) {
    return nmo_string_vformat_internal(string, 0, fmt, args);
}

nmo_status_t nmo_string_append_format(nmo_string_t *string,
                                      const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    nmo_status_t result = nmo_string_vformat_internal(string, 1, fmt, args);
    va_end(args);
    return result;
}

nmo_status_t nmo_string_append_formatv(nmo_string_t *string,
                                       const char *fmt,
                                       va_list args) {
    return nmo_string_vformat_internal(string, 1, fmt, args);
}

/* ------------------------------------------------------------------------- */
/* Numeric conversions                                                        */
/* ------------------------------------------------------------------------- */

static int nmo_string_parse_signed(const nmo_string_t *string,
                                   long long min_value,
                                   long long max_value,
                                   long long *out_value) {
    if (string == NULL || string->data == NULL || string->length == 0) {
        return 0;
    }

    int64_t value = 0;
    if (nmo_parse_i64_range_base(string->data, 10,
                                 (int64_t)min_value,
                                 (int64_t)max_value,
                                 &value) != NMO_OK) {
        return 0;
    }
    *out_value = (long long)value;
    return 1;
}

static int nmo_string_parse_unsigned(const nmo_string_t *string,
                                     unsigned long long max_value,
                                     unsigned long long *out_value) {
    if (string == NULL || string->data == NULL || string->length == 0) {
        return 0;
    }

    uint64_t value = 0;
    if (nmo_parse_u64_range_base(string->data, 10,
                                 0,
                                 (uint64_t)max_value,
                                 &value) != NMO_OK) {
        return 0;
    }
    *out_value = (unsigned long long)value;
    return 1;
}

static int nmo_string_parse_float(const nmo_string_t *string,
                                  double *out_value) {
    if (string == NULL || string->data == NULL || string->length == 0) {
        return 0;
    }

    double value = 0.0;
    if (nmo_parse_f64(string->data, &value) != NMO_OK) {
        return 0;
    }
    *out_value = value;
    return 1;
}

int nmo_string_to_int(const nmo_string_t *string, int *out_value) {
    long long value = 0;
    if (!nmo_string_parse_signed(string, INT_MIN, INT_MAX, &value)) {
        return 0;
    }
    if (out_value != NULL) {
        *out_value = (int)value;
    }
    return 1;
}

int nmo_string_to_uint32(const nmo_string_t *string, uint32_t *out_value) {
    unsigned long long value = 0;
    if (!nmo_string_parse_unsigned(string, UINT32_MAX, &value)) {
        return 0;
    }
    if (out_value != NULL) {
        *out_value = (uint32_t)value;
    }
    return 1;
}

int nmo_string_to_float(const nmo_string_t *string, float *out_value) {
    double value = 0.0;
    if (!nmo_string_parse_float(string, &value)) {
        return 0;
    }
    if (out_value != NULL) {
        *out_value = (float)value;
    }
    return 1;
}

int nmo_string_to_double(const nmo_string_t *string, double *out_value) {
    double value = 0.0;
    if (!nmo_string_parse_float(string, &value)) {
        return 0;
    }
    if (out_value != NULL) {
        *out_value = value;
    }
    return 1;
}

nmo_status_t nmo_string_from_int(nmo_string_t *string, int value) {
    return nmo_string_format(string, "%d", value);
}

nmo_status_t nmo_string_from_uint32(nmo_string_t *string, uint32_t value) {
    return nmo_string_format(string, "%u", value);
}

nmo_status_t nmo_string_from_float(nmo_string_t *string, float value) {
    return nmo_string_format(string, "%g", value);
}

nmo_status_t nmo_string_from_double(nmo_string_t *string, double value) {
    return nmo_string_format(string, "%g", value);
}

nmo_status_t nmo_string_pop_back(nmo_string_t *string, char *out_char) {
    if (string == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "string must not be NULL");
    }
    if (string->length == 0 || string->data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Cannot pop from empty string");
    }
    string->length -= 1u;
    char value = string->data[string->length];
    string->data[string->length] = '\0';
    if (out_char != NULL) {
        *out_char = value;
    }
    NMO_RETURN_OK();
}

/* ------------------------------------------------------------------------- */
/* Formatting / sanitization helpers                                         */
/* ------------------------------------------------------------------------- */

void nmo_format_hex(const void *data, size_t len, size_t max_bytes,
                     char *out_buf, size_t out_size) {
    if (!data || len == 0 || !out_buf || out_size == 0) {
        if (out_buf && out_size > 0)
            out_buf[0] = '\0';
        return;
    }

    const uint8_t *bytes = (const uint8_t *)data;
    size_t display_len = (max_bytes > 0 && len > max_bytes) ? max_bytes : len;
    size_t pos = 0;

    for (size_t i = 0; i < display_len && pos + 3 < out_size; ++i) {
        snprintf(out_buf + pos, out_size - pos, "%02x ", bytes[i]);
        pos += 3;
    }

    if (display_len < len && pos + 10 < out_size) {
        snprintf(out_buf + pos, out_size - pos, "... (%zu)", len);
    } else if (pos > 0 && out_buf[pos - 1] == ' ') {
        out_buf[pos - 1] = '\0';
    }
}

void nmo_sanitize_filename(char *dst, size_t dst_size,
                            const char *name, uint32_t id) {
    if (!dst || dst_size == 0) return;

    if (name && name[0]) {
        /* Limit name to 200 chars so that name + "_" + uint32 fits in 256 */
        char safe[201];
        size_t i = 0;
        for (; name[i] && i < sizeof(safe) - 1; ++i) {
            unsigned char ch = (unsigned char)name[i];
            if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' ||
                ch == '?' || ch == '"' || ch == '<' || ch == '>' ||
                ch == '|' || ch < 0x20 || ch == ' ') {
                safe[i] = '_';
            } else {
                safe[i] = (char)ch;
            }
        }
        safe[i] = '\0';
        snprintf(dst, dst_size, "%s_%u", safe, (unsigned)id);
    } else {
        snprintf(dst, dst_size, "unnamed_%u", (unsigned)id);
    }
}
