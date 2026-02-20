/**
 * @file nmo_object_system.h
 * @brief High-level object lifecycle orchestration (create/deserialize/serialize)
 *
 * This module centralizes object lifecycle logic that would otherwise be duplicated
 * across the load parser and save pipeline.
 *
 * Layering: Session -> Object -> Type -> Format -> IO -> Core
 */

#ifndef NMO_OBJECT_SYSTEM_H
#define NMO_OBJECT_SYSTEM_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations (keep header lightweight) */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_allocator nmo_allocator_t;
typedef struct nmo_logger nmo_logger_t;

typedef struct nmo_object nmo_object_t;
typedef struct nmo_object_repository nmo_object_repository_t;

typedef struct nmo_type_registry nmo_type_registry_t;

typedef struct nmo_shadow_storage nmo_shadow_storage_t;
typedef struct nmo_load_session nmo_load_session_t;
typedef struct nmo_id_sanitizer nmo_id_sanitizer_t;
typedef struct nmo_reference_resolver nmo_reference_resolver_t;

typedef struct nmo_object_desc nmo_object_desc_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_chunk_file_context nmo_chunk_file_context_t;

typedef struct nmo_object_data nmo_object_data_t;
typedef struct nmo_manager_data nmo_manager_data_t;
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
 * @brief Create and register objects from Header1 object descriptors.
 *
 * - Creates objects with the provided allocator.
 * - Adds them to the repository (repository takes ownership).
 * - Registers file_index -> runtime_id mapping in the load session.
 * - Optionally updates the session ID sanitizer.
 *
 * On failure, this function rolls back objects created during this call.
 * OWNERSHIP:
 * - object_allocator: used for object-owned allocations
 * - scratch_arena: owns out_created_objects array
 * - repo: takes ownership of created objects
 *
 * @param object_allocator Allocator used for object-owned allocations (NULL for default)
 * @param scratch_arena Arena used only for the returned created_objects array
 * @param repo Target repository (takes ownership of objects)
 * @param id_sanitizer Optional sanitizer for file_id <-> runtime_id mapping
 * @param load_session Load-session mapping (required)
 * @param descs Header1 descriptors array
 * @param desc_count Number of descriptors
 * @param logger Optional logger for progress/errors
 * @param out_created_objects Output array (length desc_count) mapping file object index -> object
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_object_system_create_objects_from_header1(
    const nmo_allocator_t *object_allocator,
    nmo_arena_t *scratch_arena,
    nmo_object_repository_t *repo,
    nmo_id_sanitizer_t *id_sanitizer,
    nmo_load_session_t *load_session,
    const nmo_object_desc_t *descs,
    size_t desc_count,
    nmo_logger_t *logger,
    nmo_object_t ***out_created_objects);

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

/**
 * @brief Prepare loaded objects for manager dispatch.
 *
 * Performs the load-time object steps that must happen before manager chunks are
 * dispatched:
 * - Create objects from Header1 descriptors and register file_index -> runtime_id
 * - Attach Data-section object chunks onto the created objects
 * - Build and apply the ID remap table to all loaded object chunks and manager chunks
 *
 * This intentionally does NOT deserialize objects, because manager load-data
 * hooks run between remap and object deserialization.
 * OWNERSHIP:
 * - object_allocator: used for object-owned allocations
 * - scratch_arena: owns temporary rollback buffers
 *
 * @param object_allocator Allocator used for object-owned allocations (NULL for default)
 * @param scratch_arena Arena used for temporary allocations/rollback tracking
 * @param repo Target repository
 * @param id_sanitizer Optional sanitizer for file_id <-> runtime_id mapping
 * @param load_session Load session (required)
 * @param descs Header1 descriptors array
 * @param desc_count Number of descriptors
 * @param object_data Data-section object entries (can be NULL)
 * @param object_data_count Number of object_data entries
 * @param manager_data Data-section manager entries (can be NULL)
 * @param manager_data_count Number of manager_data entries
 * @param logger Optional logger
 * @param out_remap_errors Optional output count of remap errors
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_object_system_prepare_loaded_objects(
    const nmo_allocator_t *object_allocator,
    nmo_arena_t *scratch_arena,
    nmo_object_repository_t *repo,
    nmo_id_sanitizer_t *id_sanitizer,
    nmo_load_session_t *load_session,
    const nmo_object_desc_t *descs,
    size_t desc_count,
    const nmo_object_data_t *object_data,
    size_t object_data_count,
    nmo_manager_data_t *manager_data,
    size_t manager_data_count,
    nmo_logger_t *logger,
    size_t *out_remap_errors);

/**
 * @brief Deserialize only the objects registered in the load session.
 *
 * This is the load-pipeline counterpart to nmo_object_system_deserialize_repository(),
 * but scoped to objects that were part of the current file load.
 * OWNERSHIP:
 * - arena: owns deserialize context scratch data
 * - shadow_storage: captures tails/bytes it owns
 *
 * @param repo Repository containing the objects
 * @param type_rt Type runtime for schema dispatch and operation hooks
 * @param arena Arena used by deserialize context / schema allocations
 * @param logger Optional logger
 * @param shadow_storage Optional shadow storage for capturing unconsumed chunk tails
 * @param deser_flags Flags forwarded to nmo_deserialize_context_create()
 * @param reference_resolver Optional resolver for registering discovered references
 * @param load_session Load session providing file_index -> runtime_id mapping
 * @param file_object_count Header1 object table size (max file index + 1)
 * @param out_stats Optional stats output
 * @return NMO_OK (errors are reported in stats; fatal errors return code)
 */
NMO_API nmo_status_t nmo_object_system_deserialize_loaded_objects(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    nmo_arena_t *arena,
    nmo_logger_t *logger,
    nmo_shadow_storage_t *shadow_storage,
    uint32_t deser_flags,
    nmo_reference_resolver_t *reference_resolver,
    const nmo_load_session_t *load_session,
    size_t file_object_count,
    nmo_object_system_deserialize_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_SYSTEM_H */
