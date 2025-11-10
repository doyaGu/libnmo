/**
 * @file nmo_migrator.h
 * @brief Schema migrator for handling schema version changes
 */

#ifndef NMO_MIGRATOR_H
#define NMO_MIGRATOR_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Migrator context
 */
typedef struct nmo_migrator nmo_migrator_t;

/**
 * @brief Migration function typedef
 */
typedef nmo_result_t (*nmo_migration_fn)(const void* old_data, size_t old_size,
                                          void* new_data, size_t* new_size);

/**
 * Create migrator
 * @param schema_registry Schema registry
 * @return Migrator or NULL on error
 */
NMO_API nmo_migrator_t* nmo_migrator_create(void* schema_registry);

/**
 * Destroy migrator
 * @param migrator Migrator to destroy
 */
NMO_API void nmo_migrator_destroy(nmo_migrator_t* migrator);

/**
 * Register migration function
 * @param migrator Migrator
 * @param from_version Source schema version
 * @param to_version Target schema version
 * @param migration_fn Migration function
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_migrator_register_migration(
    nmo_migrator_t* migrator, uint32_t from_version, uint32_t to_version,
    nmo_migration_fn migration_fn);

/**
 * Migrate data to target version
 * @param migrator Migrator
 * @param schema_id Schema ID
 * @param old_version Current data version
 * @param target_version Target version
 * @param old_data Old data
 * @param old_size Old data size
 * @param out_new_data Output new data (caller must free)
 * @param out_new_size Output new data size
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_migrator_migrate(
    nmo_migrator_t* migrator, uint32_t schema_id,
    uint32_t old_version, uint32_t target_version,
    const void* old_data, size_t old_size,
    void** out_new_data, size_t* out_new_size);

/**
 * Check if migration path exists
 * @param migrator Migrator
 * @param from_version Source version
 * @param to_version Target version
 * @return 1 if path exists, 0 otherwise
 */
NMO_API int nmo_migrator_has_migration_path(
    nmo_migrator_t* migrator, uint32_t from_version, uint32_t to_version);

/**
 * Get latest schema version
 * @param migrator Migrator
 * @param schema_id Schema ID
 * @return Latest version or 0 if not found
 */
NMO_API uint32_t nmo_migrator_get_latest_version(nmo_migrator_t* migrator, uint32_t schema_id);

#ifdef __cplusplus
}
#endif

#endif /* NMO_MIGRATOR_H */
