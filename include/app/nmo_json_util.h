/**
 * @file nmo_json_util.h
 * @brief Reusable yyjson helpers for safe string/value emission.
 */

#ifndef NMO_JSON_UTIL_H
#define NMO_JSON_UTIL_H

#include "nmo_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations to avoid hard include dependency in this header. */
typedef struct yyjson_mut_doc yyjson_mut_doc;
typedef struct yyjson_mut_val yyjson_mut_val;

/**
 * @brief Create a JSON string value with UTF-8 sanitization (U+FFFD replacement).
 *
 * Returns `yyjson_mut_null(doc)` for NULL input strings.
 * @ownership borrowed
 */
NMO_API yyjson_mut_val *nmo_json_make_str_safe(yyjson_mut_doc *doc, const char *str);

/**
 * @brief Add a string to JSON object, sanitizing invalid UTF-8 bytes.
 */
NMO_API bool nmo_json_add_str_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                   const char *key, const char *str);

/**
 * @brief Add a string to JSON array, sanitizing invalid UTF-8 bytes.
 */
NMO_API bool nmo_json_add_str_safe_to_arr(yyjson_mut_doc *doc, yyjson_mut_val *arr,
                                          const char *str);

/**
 * @brief Add integer value with copied key.
 */
NMO_API bool nmo_json_add_int_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                   const char *key, int64_t val);

/**
 * @brief Add unsigned integer value with copied key.
 */
NMO_API bool nmo_json_add_uint_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                    const char *key, uint64_t val);

/**
 * @brief Add floating-point value with copied key.
 */
NMO_API bool nmo_json_add_real_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                    const char *key, double val);

/**
 * @brief Add boolean value with copied key.
 */
NMO_API bool nmo_json_add_bool_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                    const char *key, bool val);

/**
 * @brief Add null value with copied key.
 */
NMO_API bool nmo_json_add_null_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                    const char *key);

/**
 * @brief Add arbitrary JSON value with copied key.
 */
NMO_API bool nmo_json_add_val_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                   const char *key, yyjson_mut_val *val);

/**
 * @brief Add hex-encoded payload and truncation metadata to JSON object.
 */
NMO_API bool nmo_json_add_data_hex(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                   const void *bytes, size_t data_size,
                                   size_t max_bytes, bool uppercase);

#ifdef __cplusplus
}
#endif

#endif /* NMO_JSON_UTIL_H */
