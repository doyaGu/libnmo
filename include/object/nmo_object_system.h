/**
 * @file nmo_object_system.h
 * @brief Object-layer serialization/deserialization core.
 *
 * This module contains object-centric logic that does not require load-session
 * orchestration (file-index remap, session registration, etc.).
 */

#ifndef NMO_OBJECT_LAYER_SYSTEM_H
#define NMO_OBJECT_LAYER_SYSTEM_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_logger nmo_logger_t;
typedef struct nmo_object nmo_object_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_shadow_storage nmo_shadow_storage_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_chunk_file_context nmo_chunk_file_context_t;
typedef struct nmo_type_runtime nmo_type_runtime_t;

/**
 * @brief Stats produced by repository deserialization.
 */
typedef struct nmo_object_system_deserialize_stats {
    size_t deserialized;
    size_t skipped_null;
    size_t skipped_no_chunk;
    size_t skipped_empty_chunk;
    size_t no_schema;
    size_t errors;
} nmo_object_system_deserialize_stats_t;

/**
 * @brief Deserialize all objects in a repository using the type runtime.
 *
 * This function handles per-object lifecycle correctly:
 * - alloc_state (combined inherited state)
 * - vtable create() before deserialize()
 * - vtable destroy() on create/deserialize failure
 * - chunk start_read()/close() always paired
 * OWNERSHIP:
 * - arena: owns deserialize context scratch data
 * - shadow_storage: captures tails/bytes it owns
 *
 * @param repo Repository to iterate
 * @param type_rt Type runtime for schema dispatch and operation hooks
 * @param arena Arena used by deserialize context / schema allocations
 * @param logger Optional logger
 * @param shadow_storage Optional shadow storage for capturing unconsumed chunk tails
 * @param deser_flags Flags forwarded to nmo_deserialize_context_create()
 * @param out_stats Optional stats output
 * @return NMO_OK (errors are reported in stats; fatal errors return code)
 */
NMO_API nmo_status_t nmo_object_system_deserialize_repository(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    nmo_arena_t *arena,
    nmo_logger_t *logger,
    nmo_shadow_storage_t *shadow_storage,
    uint32_t deser_flags,
    nmo_object_system_deserialize_stats_t *out_stats);

/**
 * @brief Serialize an object's chunk using schema dispatch.
 *
 * Returns either:
 * - a reused existing chunk (if unmodified), or
 * - a newly generated chunk in the provided arena, or
 * - NULL on allocation/parameter errors.
 * OWNERSHIP:
 * - arena: owns any newly generated chunk
 *
 * @param file_ctx Optional file context for CKFile-style ID remap during write
 */
NMO_API nmo_chunk_t *nmo_object_system_serialize_object_chunk(
    nmo_object_t *obj,
    const nmo_type_runtime_t *type_rt,
    nmo_arena_t *arena,
    nmo_logger_t *logger,
    const nmo_shadow_storage_t *shadow_storage,
    const nmo_chunk_file_context_t *file_ctx);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_LAYER_SYSTEM_H */
