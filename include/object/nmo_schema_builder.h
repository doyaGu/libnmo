/**
 * @file nmo_schema_builder.h
 * @brief Dynamic schema builder API for libnmo
 *
 * This file provides functions for building schemas at runtime,
 * adding fields, defining types, and registering custom schemas.
 */

#ifndef NMO_SCHEMA_BUILDER_H
#define NMO_SCHEMA_BUILDER_H

#include "nmo_schema.h"
#include "core/nmo_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_schema_builder nmo_schema_builder_t;

/**
 * @brief Create a schema builder
 * @param arena Arena for allocations
 * @return Builder instance or NULL on failure
 */
nmo_schema_builder_t *nmo_schema_builder_create(nmo_arena_t *arena);

/**
 * @brief Begin defining a new struct type
 * @param builder Builder instance
 * @param name Type name
 * @param guid Type GUID
 * @return Result
 */
nmo_result_t nmo_schema_builder_begin_struct(nmo_schema_builder_t *builder,
                                            const char *name,
                                            nmo_guid_t guid);

/**
 * @brief Add a field to the current struct
 * @param builder Builder instance
 * @param name Field name
 * @param type_guid Field type GUID
 * @param offset Field offset in struct
 * @param annotations Field annotations
 * @return Result
 */
nmo_result_t nmo_schema_builder_add_field(nmo_schema_builder_t *builder,
                                         const char *name,
                                         nmo_guid_t type_guid,
                                         size_t offset,
                                         uint32_t annotations);

/**
 * @brief Finish defining the current struct
 * @param builder Builder instance
 * @param type Output type descriptor
 * @return Result
 */
nmo_result_t nmo_schema_builder_end_struct(nmo_schema_builder_t *builder,
                                          nmo_schema_type_t **type);

/**
 * @brief Begin defining an enum type
 * @param builder Builder instance
 * @param name Enum name
 * @param guid Type GUID
 * @return Result
 */
nmo_result_t nmo_schema_builder_begin_enum(nmo_schema_builder_t *builder,
                                          const char *name,
                                          nmo_guid_t guid);

/**
 * @brief Add an enum value
 * @param builder Builder instance
 * @param name Value name
 * @param value Value constant
 * @return Result
 */
nmo_result_t nmo_schema_builder_add_enum_value(nmo_schema_builder_t *builder,
                                              const char *name,
                                              int32_t value);

/**
 * @brief Finish defining the current enum
 * @param builder Builder instance
 * @param type Output type descriptor
 * @return Result
 */
nmo_result_t nmo_schema_builder_end_enum(nmo_schema_builder_t *builder,
                                        nmo_schema_type_t **type);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCHEMA_BUILDER_H */
