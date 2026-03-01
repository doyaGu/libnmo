/**
 * @file nmo_deserialize_context.h
 * @brief Deserialization Context for Object Loading
 * 
 * This module provides the context passed to schema deserialize functions.
 * State is now unified in nmo_object_t with pre-allocated state memory,
 * managed through the type system.
 * 
 * Key design:
 * - State is pre-allocated via nmo_object_alloc_state() using type hierarchy
 * - deserialize() receives instance pointer directly into state memory
 * - Type-safe state access via nmo_object_get_state() / nmo_object_get_ancestor_state()
 * 
 * Usage in schema deserialize:
 * ```c
 * nmo_status_t my_deserialize(void *instance, nmo_chunk_t *chunk,
 *                              const nmo_type_descriptor_t *type, void *context)
 * {
 *     nmo_deserialize_context_t *ctx = nmo_deserialize_context_get(context);
 *     my_state_t *state = (my_state_t *)instance;
 *     
 *     // Read data into state (already allocated)
 *     NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &state->field));
 *     return NMO_OK;
 * }
 * ```
 */

#ifndef NMO_DESERIALIZE_CONTEXT_H
#define NMO_DESERIALIZE_CONTEXT_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "object/nmo_serialize_context.h"
#include "format/nmo_object.h"
#include "type/nmo_type_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_type_descriptor nmo_type_descriptor_t;
typedef struct nmo_type_registry nmo_type_registry_t;
typedef struct nmo_operation_registry nmo_operation_registry_t;
typedef struct nmo_object nmo_object_t;

/** Magic value for detecting nmo_deserialize_context_t */
#define NMO_DESERIALIZE_CONTEXT_MAGIC 0x4E4D4F44u  /* "NMOD" */

/* ============================================================================
 * Deserialize Context
 * ============================================================================ */

/**
 * @brief Deserialization context for object loading
 * 
 * Passed to deserialize functions, provides access to:
 * - Object being deserialized
 * - Arena for any allocations needed
 * - Object repository for reference resolution
 * - Type registry for hierarchy lookups
 */
typedef struct nmo_deserialize_context {
    /** Magic for type detection (NMO_DESERIALIZE_CONTEXT_MAGIC) */
    uint32_t magic;
    
    /** Flags */
    uint32_t flags;
    
    /** Arena for allocations */
    nmo_arena_t *arena;
    
    /** Object repository for reference resolution (nmo_object_repository_t*) */
    void *repository;
    
    /** Object being deserialized */
    nmo_object_t *object;
    
    /** Aggregated runtime view (type + operation registries) */
    const nmo_type_runtime_t *type_runtime;

    /** Type registry for hierarchy lookups (cached alias of type_runtime->types) */
    const nmo_type_registry_t *type_registry;

    /** Operation registry (cached alias of type_runtime->ops) */
    nmo_operation_registry_t *operation_registry;
    
    /** Current chunk version (for compatibility decisions) */
    uint32_t chunk_version;
    
    /** Load session for ID remapping */
    void *load_session;
    
    /** Callback to capture unconsumed bytes */
    void *raw_chunk_callback;
} nmo_deserialize_context_t;

/** Deserialize context flags */
#define NMO_DESER_FLAG_NONE           0x0000
#define NMO_DESER_FLAG_FILE_MODE      0x0001  /**< Loading from file */
#define NMO_DESER_FLAG_VALIDATE       0x0002  /**< Validate during load */
#define NMO_DESER_FLAG_PRESERVE_RAW   0x0004  /**< Store unconsumed bytes */

/* ============================================================================
 * Context Creation and Access
 * ============================================================================ */

/**
 * @brief Create deserialization context
 * 
 * @param arena Arena for allocations
 * @param repository Repository pointer (nmo_object_repository_t*)
 * @param type_runtime Type runtime aggregate
 * @param flags Context flags
 * @return Initialized context
 */
static inline nmo_deserialize_context_t nmo_deserialize_context_create(
    nmo_arena_t *arena,
    void *repository,
    const nmo_type_runtime_t *type_runtime,
    uint32_t flags)
{
    nmo_deserialize_context_t ctx = {
        .magic = NMO_DESERIALIZE_CONTEXT_MAGIC,
        .flags = flags,
        .arena = arena,
        .repository = repository,
        .object = NULL,
        .type_runtime = type_runtime,
        .type_registry = type_runtime ? type_runtime->types : NULL,
        .operation_registry = type_runtime ? type_runtime->ops : NULL,
        .chunk_version = 0,
        .load_session = NULL,
        .raw_chunk_callback = NULL
    };
    return ctx;
}

/**
 * @brief Try to get deserialize context from void pointer
 * 
 * Checks magic value to detect if pointer is a deserialize context.
 * Returns NULL if not a valid deserialize context.
 * 
 * @param context Void pointer that may be deserialize context
 * @return Typed context pointer or NULL
 */
static inline nmo_deserialize_context_t *nmo_deserialize_context_get(void *context)
{
    if (context == NULL) return NULL;
    
    nmo_deserialize_context_t *ctx = (nmo_deserialize_context_t *)context;
    if (ctx->magic != NMO_DESERIALIZE_CONTEXT_MAGIC) {
        return NULL;  /* Not a deserialize context (maybe serialize_context) */
    }
    return ctx;
}

/**
 * @brief Get arena from deserialize context
 */
static inline nmo_arena_t *nmo_deserialize_context_get_arena(void *context)
{
    nmo_deserialize_context_t *ctx = nmo_deserialize_context_get(context);
    return ctx != NULL ? ctx->arena : NULL;
}

/**
 * @brief Get repository from deserialize context
 */
static inline void *nmo_deserialize_context_get_repository(void *context)
{
    nmo_deserialize_context_t *ctx = nmo_deserialize_context_get(context);
    return ctx != NULL ? ctx->repository : NULL;
}

/**
 * @brief Get type registry from deserialize context
 */
static inline const nmo_type_registry_t *nmo_deserialize_context_get_type_registry(void *context)
{
    nmo_deserialize_context_t *ctx = nmo_deserialize_context_get(context);
    return ctx != NULL ? ctx->type_registry : NULL;
}

/**
 * @brief Set object being deserialized
 * 
 * Called by parser before invoking deserialize function.
 */
static inline void nmo_deserialize_context_set_object(
    nmo_deserialize_context_t *ctx,
    nmo_object_t *object)
{
    if (ctx != NULL) {
        ctx->object = object;
        /* Prefer per-object allocation arena when available. */
        if (object != NULL) {
            nmo_arena_t *obj_arena = nmo_object_get_storage_arena(object);
            if (obj_arena != NULL) {
                ctx->arena = obj_arena;
            }
        }
    }
}

/**
 * @brief Get object being deserialized
 */
static inline nmo_object_t *nmo_deserialize_context_get_object(
    nmo_deserialize_context_t *ctx)
{
    return ctx != NULL ? ctx->object : NULL;
}

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Store unconsumed chunk bytes
 * 
 * Called at end of deserialize if chunk has remaining bytes.
 * These are stored in shadow storage for round-trip preservation.
 * 
 * @param ctx Deserialize context
 * @param chunk Chunk being deserialized (to get remaining bytes)
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_deserialize_store_remaining(
    nmo_deserialize_context_t *ctx,
    nmo_chunk_t *chunk);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DESERIALIZE_CONTEXT_H */
