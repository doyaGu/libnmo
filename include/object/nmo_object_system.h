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
#include "format/nmo_id_remap.h"
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
 * @param out_status Optional detailed status. Set to NMO_OK for successful
 *                   serialization or intentional raw-chunk reuse.
 * @ownership arena
 */
NMO_API nmo_chunk_t *nmo_object_system_serialize_object_chunk(
    nmo_object_t *obj,
    const nmo_type_runtime_t *type_rt,
    nmo_arena_t *arena,
    nmo_arena_t *scratch,
    nmo_object_repository_t *repo,
    nmo_logger_t *logger,
    const nmo_shadow_storage_t *shadow_storage,
    const nmo_chunk_file_context_t *file_ctx,
    nmo_status_t *out_status);

/**
 * @brief Serialize the state of an object as the schema writes it.
 *
 * Unlike nmo_object_system_serialize_object_chunk() this never returns the
 * old chunk: it is NULL when the object has no state or no schema that can
 * write it. Object ids are the runtime ids (no file context).
 */
NMO_API nmo_chunk_t *nmo_object_system_serialize_object_state(
    nmo_object_t *obj,
    const nmo_type_runtime_t *type_rt,
    nmo_arena_t *arena,
    nmo_arena_t *scratch,
    nmo_object_repository_t *repo,
    nmo_logger_t *logger,
    nmo_status_t *out_status);

/** @brief Counts of nmo_object_system_capture_fidelity(). */
typedef struct nmo_object_system_fidelity_stats {
    size_t captured;      /**< objects with a digest */
    size_t with_residue;  /**< of those, objects whose chunk holds more than the schema writes */
    size_t skipped;       /**< objects without a chunk, state or writable schema */
} nmo_object_system_fidelity_stats_t;

/**
 * @brief Record, for every object of a freshly loaded repository, what its
 *        state serializes to (nmo_chunk_residue.h).
 *
 * Call it after the load is complete. The objects are numbered in repository
 * order, which is the order of the file their chunks' ids refer to.
 */
NMO_API nmo_status_t nmo_object_system_capture_fidelity(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    nmo_logger_t *logger,
    nmo_object_system_fidelity_stats_t *out_stats);

/** @brief What a save found out about an object (nmo_object_system_fidelity_inspect). */
typedef enum nmo_fidelity_outcome {
    NMO_FIDELITY_UNKNOWN = 0,   /**< nothing was captured; serialize the state */
    NMO_FIDELITY_UNCHANGED,     /**< the state is as loaded; reuse the original chunk */
    NMO_FIDELITY_CHANGED        /**< the state changed; serialize it and merge the residue */
} nmo_fidelity_outcome_t;

/**
 * @brief Compare the state of a loaded object with what was captured.
 *
 * @param[out] out_current  The state serialized now (runtime ids), to hand to
 *                          nmo_object_system_fidelity_commit(); NULL if unknown
 */
NMO_API nmo_fidelity_outcome_t nmo_object_system_fidelity_inspect(
    nmo_object_t *obj,
    const nmo_type_runtime_t *type_rt,
    nmo_arena_t *arena,
    nmo_arena_t *scratch,
    nmo_object_repository_t *repo,
    nmo_logger_t *logger,
    nmo_chunk_t **out_current);

/**
 * @brief The original chunk of an unchanged object, with its ids translated to
 *        the file being written.
 *
 * @param current       The object's state serialized now (from fidelity_inspect);
 *                      it tells where the ids are in the original chunk
 * @param load_to_file  Maps the index an object had in the loaded file to the
 *                      index it has in the file being written; NULL if they agree
 * @param file_index    Index of obj in the file being written
 * @param file_count    Number of objects of the file being written
 * @return The chunk (the original itself when no id moves), or NULL if an id
 *         refers to an object that is no longer there
 */
NMO_API nmo_chunk_t *nmo_object_system_fidelity_reuse(
    nmo_object_t *obj,
    const nmo_chunk_t *current,
    const nmo_id_remap_t *load_to_file,
    nmo_arena_t *arena,
    uint32_t file_index,
    uint32_t file_count);

/**
 * @brief Finish the chunk written for a changed object and remember its state.
 *
 * Carries the residue of the original chunk into chunk (in the id space of the
 * file being written), then records current as what the state serializes to.
 *
 * @param chunk         The chunk written for the object, ids already in file indices
 * @param original      The chunk the object had before
 * @param current       From nmo_object_system_fidelity_inspect()
 */
NMO_API nmo_status_t nmo_object_system_fidelity_commit(
    nmo_object_t *obj,
    nmo_chunk_t *chunk,
    const nmo_chunk_t *original,
    const nmo_chunk_t *current,
    const nmo_id_remap_t *load_to_file,
    nmo_arena_t *arena,
    uint32_t file_index,
    uint32_t file_count);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_LAYER_SYSTEM_H */
