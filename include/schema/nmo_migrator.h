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

/* Forward declarations */
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_chunk nmo_chunk_t;

/**
 * @brief Migrator context
 */
typedef struct nmo_migrator nmo_migrator_t;

/**
 * @brief Create migrator
 * @param registry Schema registry
 * @return Migrator or NULL on error
 */
NMO_API nmo_migrator_t *nmo_migrator_create(nmo_schema_registry_t *registry);

/**
 * @brief Destroy migrator
 * @param migrator Migrator to destroy
 */
NMO_API void nmo_migrator_destroy(nmo_migrator_t *migrator);

/**
 * @brief Migrate chunk to target version
 * @param migrator Migrator
 * @param chunk Chunk to migrate
 * @param target_version Target version
 * @return NMO_OK on success
 */
NMO_API int nmo_migrate_chunk(nmo_migrator_t *migrator, nmo_chunk_t *chunk, uint32_t target_version);

/**
 * @brief Check if migration is supported
 * @param migrator Migrator
 * @param from_version Source version
 * @param to_version Target version
 * @return 1 if supported, 0 otherwise
 */
NMO_API int nmo_migrator_can_migrate(nmo_migrator_t *migrator, uint32_t from_version, uint32_t to_version);

#ifdef __cplusplus
}
#endif

#endif /* NMO_MIGRATOR_H */
