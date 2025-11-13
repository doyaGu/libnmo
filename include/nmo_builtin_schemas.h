/**
 * @file nmo_builtin_schemas.h
 * @brief Built-in schema definitions
 */

#ifndef NMO_BUILTIN_SCHEMAS_H
#define NMO_BUILTIN_SCHEMAS_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Built-in schema IDs
 */
typedef enum nmo_builtin_schema_id {
    NMO_SCHEMA_OBJECT_DESCRIPTOR  = 0x0001,
    NMO_SCHEMA_CHUNK              = 0x0002,
    NMO_SCHEMA_GENERIC_OBJECT     = 0x0003,
    NMO_SCHEMA_PROPERTY_CONTAINER = 0x0004,
    NMO_SCHEMA_REFERENCE          = 0x0005,
} nmo_builtin_schema_id_t;

/**
 * Initialize built-in schemas
 * @param registry Schema registry to populate
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_builtin_schemas_init(void *registry);

/**
 * Get object descriptor schema
 * @return Object descriptor schema or NULL
 */
NMO_API void *nmo_builtin_schemas_get_object_descriptor(void);

/**
 * Get chunk schema
 * @return Chunk schema or NULL
 */
NMO_API void *nmo_builtin_schemas_get_chunk(void);

/**
 * Get generic object schema
 * @return Generic object schema or NULL
 */
NMO_API void *nmo_builtin_schemas_get_generic_object(void);

/**
 * Get property container schema
 * @return Property container schema or NULL
 */
NMO_API void *nmo_builtin_schemas_get_property_container(void);

/**
 * Get reference schema
 * @return Reference schema or NULL
 */
NMO_API void *nmo_builtin_schemas_get_reference(void);

/**
 * Create schema by ID
 * @param schema_id Built-in schema ID
 * @return Schema instance or NULL if not found
 */
NMO_API void *nmo_builtin_schemas_create(uint32_t schema_id);

/**
 * Check if schema ID is built-in
 * @param schema_id Schema ID to check
 * @return 1 if built-in, 0 otherwise
 */
NMO_API int nmo_builtin_schemas_is_builtin(uint32_t schema_id);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BUILTIN_SCHEMAS_H */
