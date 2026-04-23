/**
 * @file export_json_util_internal.h
 * @brief Reusable yyjson helpers for safe string/value emission.
 */

#ifndef NMO_EXPORT_JSON_UTIL_INTERNAL_H
#define NMO_EXPORT_JSON_UTIL_INTERNAL_H

#include "nmo_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct yyjson_mut_doc yyjson_mut_doc;
typedef struct yyjson_mut_val yyjson_mut_val;

NMO_API yyjson_mut_val *nmo_json_make_str_safe(yyjson_mut_doc *doc, const char *str);
NMO_API bool nmo_json_add_str_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                   const char *key, const char *str);
NMO_API bool nmo_json_add_str_safe_to_arr(yyjson_mut_doc *doc, yyjson_mut_val *arr,
                                          const char *str);
NMO_API bool nmo_json_add_int_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                   const char *key, int64_t val);
NMO_API bool nmo_json_add_uint_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                    const char *key, uint64_t val);
NMO_API bool nmo_json_add_real_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                    const char *key, double val);
NMO_API bool nmo_json_add_bool_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                    const char *key, bool val);
NMO_API bool nmo_json_add_null_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                    const char *key);
NMO_API bool nmo_json_add_val_safe(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                   const char *key, yyjson_mut_val *val);
NMO_API bool nmo_json_add_data_hex(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                   const void *bytes, size_t data_size,
                                   size_t max_bytes, bool uppercase);

#ifdef __cplusplus
}
#endif

#endif /* NMO_EXPORT_JSON_UTIL_INTERNAL_H */
