/**
 * @file nmo_string.h
 * @brief Dynamic UTF-8 string utilities mirroring Virtools XString behaviour.
 *
 * nmo_string_t provides an owning, growable string container that keeps the
 * contents null-terminated while tracking its logical length separately.
 * The API is intentionally rich to ease porting code that previously relied
 * on Virtools' XString helper (formatting, trimming, case conversions, etc.).
 */

#ifndef NMO_STRING_H
#define NMO_STRING_H

#include "nmo_types.h"
#include "core/nmo_allocator.h"
#include "core/nmo_error.h"

#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Non-owning string view (pointer + explicit length).
 */
typedef struct nmo_string_view {
    const char *data;
    size_t length;
} nmo_string_view_t;

/**
 * @brief Packed view literal helper.
 */
#define NMO_STRING_VIEW_LITERAL(literal) \
    (nmo_string_view_t){ (literal), sizeof(literal) - 1u }

/**
 * @brief Owning dynamic string.
 */
typedef struct nmo_string {
    char *data;               /**< Null-terminated buffer (may be NULL when empty). */
    size_t length;            /**< Logical string length in bytes (not counting the terminator). */
    size_t capacity;          /**< Capacity in bytes excluding space for the terminator. */
    nmo_allocator_t allocator;/**< Allocator used for heap operations. */
} nmo_string_t;

#define NMO_STRING_INITIALIZER { NULL, 0u, 0u, { NULL, NULL, NULL } }

/**
 * @brief Create a view from a C string literal or pointer.
 */
static inline nmo_string_view_t nmo_string_view_from_cstr(const char *cstr) {
    return (nmo_string_view_t){
        cstr,
        cstr ? strlen(cstr) : 0
    };
}

/**
 * @brief Create a view from explicit data/length (data may be NULL when length is 0).
 */
static inline nmo_string_view_t nmo_string_view_from_parts(const char *data, size_t length) {
    return (nmo_string_view_t){ data, data ? length : 0 };
}

/**
 * @brief Create a view referencing the entire contents of an nmo_string_t.
 */
static inline nmo_string_view_t nmo_string_view_from_string(const nmo_string_t *string) {
    if (string == NULL || string->data == NULL) {
        return (nmo_string_view_t){ NULL, 0u };
    }
    return (nmo_string_view_t){ string->data, string->length };
}

/* ------------------------------------------------------------------------- */
/* Initialization / lifetime                                                  */
/* ------------------------------------------------------------------------- */

NMO_API nmo_result_t nmo_string_init(nmo_string_t *string,
                                     const nmo_allocator_t *allocator);

NMO_API nmo_result_t nmo_string_init_cstr(nmo_string_t *string,
                                          const char *cstr,
                                          const nmo_allocator_t *allocator);

NMO_API nmo_result_t nmo_string_init_view(nmo_string_t *string,
                                          nmo_string_view_t view,
                                          const nmo_allocator_t *allocator);

NMO_API void nmo_string_dispose(nmo_string_t *string);

/* ------------------------------------------------------------------------- */
/* Capacity / metadata                                                        */
/* ------------------------------------------------------------------------- */

NMO_API size_t nmo_string_length(const nmo_string_t *string);
NMO_API size_t nmo_string_capacity(const nmo_string_t *string);
NMO_API int nmo_string_empty(const nmo_string_t *string);
NMO_API const char *nmo_string_c_str(const nmo_string_t *string);
NMO_API char *nmo_string_data(nmo_string_t *string);

NMO_API nmo_result_t nmo_string_reserve(nmo_string_t *string, size_t capacity);
NMO_API nmo_result_t nmo_string_shrink_to_fit(nmo_string_t *string);
NMO_API void nmo_string_clear(nmo_string_t *string);

/* ------------------------------------------------------------------------- */
/* Assignment / append                                                        */
/* ------------------------------------------------------------------------- */

NMO_API nmo_result_t nmo_string_assign(nmo_string_t *string, const char *cstr);
NMO_API nmo_result_t nmo_string_assign_len(nmo_string_t *string,
                                           const char *data,
                                           size_t length);
NMO_API nmo_result_t nmo_string_assign_view(nmo_string_t *string,
                                            nmo_string_view_t view);
NMO_API nmo_result_t nmo_string_copy(nmo_string_t *dest,
                                     const nmo_string_t *src);

NMO_API nmo_result_t nmo_string_append(nmo_string_t *string, const char *cstr);
NMO_API nmo_result_t nmo_string_append_len(nmo_string_t *string,
                                           const char *data,
                                           size_t length);
NMO_API nmo_result_t nmo_string_append_view(nmo_string_t *string,
                                            nmo_string_view_t view);
NMO_API nmo_result_t nmo_string_append_char(nmo_string_t *string, char ch);

/* ------------------------------------------------------------------------- */
/* Mutation helpers                                                           */
/* ------------------------------------------------------------------------- */

NMO_API nmo_result_t nmo_string_insert(nmo_string_t *string,
                                       size_t index,
                                       const char *data,
                                       size_t length);
NMO_API nmo_result_t nmo_string_erase(nmo_string_t *string,
                                      size_t index,
                                      size_t length);
NMO_API nmo_result_t nmo_string_replace(nmo_string_t *string,
                                        size_t index,
                                        size_t length,
                                        const char *data,
                                        size_t new_length);
NMO_API nmo_result_t nmo_string_replace_all(nmo_string_t *string,
                                            nmo_string_view_t needle,
                                            nmo_string_view_t replacement,
                                            size_t *out_count);

/* ------------------------------------------------------------------------- */
/* Search / comparison                                                        */
/* ------------------------------------------------------------------------- */

NMO_API size_t nmo_string_find(const nmo_string_t *string,
                               nmo_string_view_t needle,
                               size_t start);
NMO_API size_t nmo_string_find_char(const nmo_string_t *string,
                                    char ch,
                                    size_t start);
NMO_API size_t nmo_string_rfind_char(const nmo_string_t *string,
                                     char ch,
                                     size_t start);

NMO_API int nmo_string_contains(const nmo_string_t *string,
                                nmo_string_view_t needle);
NMO_API int nmo_string_starts_with(const nmo_string_t *string,
                                   nmo_string_view_t prefix);
NMO_API int nmo_string_istarts_with(const nmo_string_t *string,
                                    nmo_string_view_t prefix);
NMO_API int nmo_string_ends_with(const nmo_string_t *string,
                                 nmo_string_view_t suffix);
NMO_API int nmo_string_iends_with(const nmo_string_t *string,
                                  nmo_string_view_t suffix);

NMO_API int nmo_string_compare(const nmo_string_t *lhs,
                               const nmo_string_t *rhs);
NMO_API int nmo_string_compare_view(const nmo_string_t *lhs,
                                    nmo_string_view_t rhs);
NMO_API int nmo_string_icompare_view(const nmo_string_t *lhs,
                                     nmo_string_view_t rhs);
NMO_API int nmo_string_equals(const nmo_string_t *lhs,
                              const nmo_string_t *rhs);
NMO_API int nmo_string_equals_view(const nmo_string_t *lhs,
                                   nmo_string_view_t rhs);
NMO_API int nmo_string_iequals_view(const nmo_string_t *lhs,
                                    nmo_string_view_t rhs);
NMO_API int nmo_string_slice_view(const nmo_string_t *string,
                                  size_t start,
                                  size_t length,
                                  nmo_string_view_t *out_view);
NMO_API nmo_result_t nmo_string_substr(nmo_string_t *dest,
                                       const nmo_string_t *src,
                                       size_t start,
                                       size_t length);

/* ------------------------------------------------------------------------- */
/* Transformations                                                            */
/* ------------------------------------------------------------------------- */

NMO_API void nmo_string_to_upper(nmo_string_t *string);
NMO_API void nmo_string_to_lower(nmo_string_t *string);
NMO_API void nmo_string_trim_left(nmo_string_t *string);
NMO_API void nmo_string_trim_right(nmo_string_t *string);
NMO_API void nmo_string_trim(nmo_string_t *string);

/* ------------------------------------------------------------------------- */
/* Formatting                                                                 */
/* ------------------------------------------------------------------------- */

NMO_API nmo_result_t nmo_string_format(nmo_string_t *string,
                                       const char *fmt, ...);
NMO_API nmo_result_t nmo_string_formatv(nmo_string_t *string,
                                        const char *fmt,
                                        va_list args);
NMO_API nmo_result_t nmo_string_append_format(nmo_string_t *string,
                                              const char *fmt, ...);
NMO_API nmo_result_t nmo_string_append_formatv(nmo_string_t *string,
                                               const char *fmt,
                                               va_list args);

/* ------------------------------------------------------------------------- */
/* Numeric conversions                                                        */
/* ------------------------------------------------------------------------- */

NMO_API int nmo_string_to_int(const nmo_string_t *string, int *out_value);
NMO_API int nmo_string_to_uint32(const nmo_string_t *string, uint32_t *out_value);
NMO_API int nmo_string_to_float(const nmo_string_t *string, float *out_value);
NMO_API int nmo_string_to_double(const nmo_string_t *string, double *out_value);

NMO_API nmo_result_t nmo_string_from_int(nmo_string_t *string, int value);
NMO_API nmo_result_t nmo_string_from_uint32(nmo_string_t *string, uint32_t value);
NMO_API nmo_result_t nmo_string_from_float(nmo_string_t *string, float value);
NMO_API nmo_result_t nmo_string_from_double(nmo_string_t *string, double value);
NMO_API nmo_result_t nmo_string_pop_back(nmo_string_t *string, char *out_char);

#ifdef __cplusplus
}
#endif

#endif /* NMO_STRING_H */
