/**
 * @file nmo_dsl_json.h
 * @brief Serialize DSL values to JSON via yyjson.
 */

#ifndef NMO_DSL_JSON_H
#define NMO_DSL_JSON_H

#include "nmo_types.h"
#include "dsl/nmo_dsl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct yyjson_mut_doc yyjson_mut_doc;
typedef struct yyjson_mut_val yyjson_mut_val;

/**
 * @brief Serialize a DSL value to a JSON value.
 *
 * Handles all DSL value types: null, bool, int, uint, real, string,
 * byref/object/type (formatted as string), and sequence (recursive array).
 *
 * When @p parent is a JSON object and @p key is non-NULL, the result is
 * added as a keyed member.  When @p parent is a JSON array and @p key is
 * NULL, the result is appended.  When both @p parent and @p key are NULL
 * the caller receives a standalone value.
 *
 * @param value  DSL value to serialize (NULL emits json null)
 * @param doc    JSON document for allocation (required)
 * @param parent Parent object or array (may be NULL)
 * @param key    Key name when parent is an object (may be NULL)
 * @return The created JSON value, or NULL on error
 */
NMO_API yyjson_mut_val *nmo_dsl_value_to_json(
    const nmo_dsl_value_t *value,
    yyjson_mut_doc *doc,
    yyjson_mut_val *parent,
    const char *key);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DSL_JSON_H */
