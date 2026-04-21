/**
 * @file nmo_object_import.h
 * @brief JSON-to-object import via the reflection system
 *
 * Inverse of the nmo_object_summary JSON snapshot export. Reads semantic
 * field snapshots and writes them into live objects through reflection.
 * This importer accepts the snapshot protocol produced by `object export`;
 * legacy hand-written flat maps and preview-only summaries are rejected.
 *
 * Import outcomes are exposed through the structured nmo_import_result_t
 * result object. Rendering or CLI-specific reporting policy should remain
 * outside this API.
 */

#ifndef NMO_OBJECT_IMPORT_H
#define NMO_OBJECT_IMPORT_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"

#define NMO_OBJECT_IMPORT_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_OBJECT_IMPORT_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_session nmo_session_t;
typedef struct nmo_type_registry nmo_type_registry_t;

/** @brief Import creates missing objects when ID is not found. */
#define NMO_IMPORT_CREATE_MISSING  0x0001
/** @brief Parse and validate only; do not write any data. */
#define NMO_IMPORT_DRY_RUN         0x0002

/**
 * @brief Import statistics returned after a call to nmo_object_import_json.
 */
typedef struct nmo_import_result {
    size_t objects_updated;
    size_t objects_created;
    size_t fields_written;
    size_t fields_skipped;
    size_t errors;
} nmo_import_result_t;

/**
 * @brief Import object field values from a JSON document.
 *
 * The JSON must follow the semantic snapshot schema produced by
 * `nmo object export -f json`:
 * @code
 * {
 *   "objects": [
 *     {
 *       "id": 42,
 *       "class_name": "CK3dObject",
 *       "name": "Ball_01",
 *       "fields": [
 *         {
 *           "name": "position",
 *           "kind": "struct",
 *           "type_guid": "{...}",
 *           "value": {
 *             "fields": [
 *               { "name": "x", "kind": "scalar", "type_guid": "{...}", "value": 1.0 }
 *             ]
 *           }
 *         },
 *         {
 *           "name": "vertices",
 *           "kind": "array",
 *           "type_guid": "{...}",
 *           "value": null,
 *           "count": 3,
 *           "items": [
 *             { "kind": "struct", "fields": [ ... ] },
 *             { "kind": "struct", "fields": [ ... ] }
 *           ],
 *           "raw_hex": "..."
 *         }
 *       ]
 *     }
 *   ]
 * }
 * @endcode
 *
 * Array `items` are complete semantic import data. `raw_hex` is used only for
 * fixed-size data that cannot be represented reliably through typed JSON; when
 * semantic `items` are present they take precedence over `raw_hex`.
 *
 * Legacy flat field maps and preview-only `{name,value_str}` export bridges
 * are invalid input.
 *
 * @param session    Active session containing objects
 * @param registry   Type registry for field resolution
 * @param arena      Arena allocator for temporary and pointer allocations
 * @param json_data  JSON string
 * @param json_size  Length of json_data in bytes (0 to use strlen)
 * @param flags      Combination of NMO_IMPORT_* flags
 * @param result     Output statistics (may be NULL)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_status_t nmo_object_import_json(
    nmo_session_t *session,
    const nmo_type_registry_t *registry,
    nmo_arena_t *arena,
    const char *json_data,
    size_t json_size,
    uint32_t flags,
    nmo_import_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_IMPORT_H */
