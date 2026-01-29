/**
 * @file nmo_migrator.h
 * @brief Schema migration API for libnmo
 *
 * This file provides functions for migrating data between schema versions,
 * handling field additions/removals, and maintaining backward compatibility.
 */

#ifndef NMO_MIGRATOR_H
#define NMO_MIGRATOR_H

#include "nmo_schema.h"
#include "core/nmo_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_migrator nmo_migrator_t;
typedef struct nmo_migration_path nmo_migration_path_t;

/**
 * @brief Migration strategy
 */
typedef enum nmo_migration_strategy {
    NMO_MIGRATE_STRICT = 0,      /**< Fail on any incompatibility */
    NMO_MIGRATE_LENIENT,         /**< Use defaults for missing fields */
    NMO_MIGRATE_CUSTOM           /**< Use custom migration handlers */
} nmo_migration_strategy_t;

/**
 * @brief Create a migrator instance
 * @param arena Arena for allocations
 * @param registry Schema registry
 * @return Migrator instance or NULL on failure
 */
nmo_migrator_t *nmo_migrator_create(nmo_arena_t *arena,
                                   nmo_schema_registry_t *registry);

/**
 * @brief Migrate data from one schema version to another
 * @param migrator Migrator instance
 * @param source_data Source data
 * @param source_type Source schema type
 * @param target_type Target schema type
 * @param target_data Output migrated data
 * @param strategy Migration strategy
 * @return Result
 */
nmo_result_t nmo_migrator_migrate(nmo_migrator_t *migrator,
                                 const void *source_data,
                                 const nmo_schema_type_t *source_type,
                                 const nmo_schema_type_t *target_type,
                                 void **target_data,
                                 nmo_migration_strategy_t strategy);

#ifdef __cplusplus
}
#endif

#endif /* NMO_MIGRATOR_H */
