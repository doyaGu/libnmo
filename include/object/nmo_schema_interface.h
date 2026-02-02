/**
 * @file nmo_schema_interface.h
 * @brief Unified serialization interface for CKObject class hierarchy
 * 
 * Defines the standard function signatures for object serialization that
 * are compatible with the type system vtable. All CKObject-derived schemas
 * implement these signatures directly, eliminating wrapper layers.
 * 
 * Design:
 * - Unified signatures compatible with nmo_type_vtable_t
 * - Context parameter carries arena and any additional state
 * - Direct function calls replace accessor function indirection
 * 
 * @see type/type_system.h for nmo_type_vtable_t definition
 */

#ifndef NMO_SCHEMA_INTERFACE_H
#define NMO_SCHEMA_INTERFACE_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_type_descriptor_t nmo_type_descriptor_t;

/** Magic value for detecting nmo_serialize_context_t. */
#define NMO_SERIALIZE_CONTEXT_MAGIC 0x4E4D4F53u /* "NMOS" */

/* ============================================================================
 * Serialization Context
 * 
 * Passed to serialize/deserialize functions via the void* context parameter.
 * Contains everything needed for object serialization operations.
 * ============================================================================ */

/**
 * @brief Serialization context for object schema operations
 * 
 * This context is passed through the type system vtable and provides
 * access to the arena allocator and any additional state needed.
 */
typedef struct nmo_serialize_context {
    uint32_t magic;              /**< Must be NMO_SERIALIZE_CONTEXT_MAGIC */
    nmo_arena_t *arena;           /**< Arena for allocations */
    void *repository;             /**< Object repository for reference resolution */
    uint32_t flags;               /**< Operation flags */
} nmo_serialize_context_t;

/* Context flags */
#define NMO_SERIALIZE_FLAG_NONE       0x0000
#define NMO_SERIALIZE_FLAG_FILE_MODE  0x0001  /**< File serialization (vs runtime) */
#define NMO_SERIALIZE_FLAG_VALIDATE   0x0002  /**< Validate data during operation */

/* ============================================================================
 * Unified Function Signatures
 * 
 * These signatures match nmo_type_vtable_t serialize/deserialize members.
 * All schema implementations use these signatures directly.
 * ============================================================================ */

/**
 * @brief Object serialize function signature
 * 
 * Serializes object state to a chunk. Compatible with nmo_type_serialize_fn.
 * 
 * @param instance Pointer to object state structure
 * @param chunk Target chunk for serialization
 * @param type Type descriptor (may be NULL for legacy calls)
 * @param context Serialization context (nmo_serialize_context_t*)
 * @return nmo_ok() on success, error on failure
 */
typedef nmo_result_t (*nmo_object_serialize_fn)(
    const void *instance,
    struct nmo_chunk *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

/**
 * @brief Object deserialize function signature
 * 
 * Deserializes object state from a chunk. Compatible with nmo_type_deserialize_fn.
 * 
 * @param instance Pointer to object state structure to fill
 * @param chunk Source chunk containing serialized data
 * @param type Type descriptor (may be NULL for legacy calls)
 * @param context Serialization context (nmo_serialize_context_t*)
 * @return nmo_ok() on success, error on failure
 */
typedef nmo_result_t (*nmo_object_deserialize_fn)(
    void *instance,
    struct nmo_chunk *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

/**
 * @brief Finish loading function signature
 * 
 * Called after deserialization to resolve references and initialize runtime state.
 * Equivalent to CKObject::PostLoad() in Virtools SDK.
 * 
 * @param instance Pointer to object state structure
 * @param context Serialization context with repository for reference resolution
 * @return nmo_ok() on success, error on failure
 */
typedef nmo_result_t (*nmo_object_finish_loading_fn)(
    void *instance,
    void *context);

/* ============================================================================
 * Context Helper Functions
 * ============================================================================ */

/**
 * @brief Create serialization context
 * 
 * @param arena Arena for allocations
 * @param repository Object repository (may be NULL)
 * @param flags Operation flags
 * @return Initialized context
 */
static inline nmo_serialize_context_t nmo_serialize_context_create(
    nmo_arena_t *arena,
    void *repository,
    uint32_t flags)
{
    nmo_serialize_context_t ctx = {
        .magic = NMO_SERIALIZE_CONTEXT_MAGIC,
        .arena = arena,
        .repository = repository,
        .flags = flags
    };
    return ctx;
}

static inline const nmo_serialize_context_t *nmo_serialize_context_try(void *context)
{
    if (!context) return NULL;

    const nmo_serialize_context_t *ctx = (const nmo_serialize_context_t *)context;
    if (ctx->magic != NMO_SERIALIZE_CONTEXT_MAGIC) return NULL;
    return ctx;
}

/**
 * @brief Extract arena from serialization context
 *
 * @param context Context pointer (must be nmo_serialize_context_t*)
 * @return Arena pointer, or NULL if context is NULL/invalid
 */
static inline nmo_arena_t* nmo_serialize_context_get_arena(void *context)
{
    const nmo_serialize_context_t *ctx = nmo_serialize_context_try(context);
    return ctx ? ctx->arena : NULL;
}

static inline void *nmo_serialize_context_get_repository(void *context)
{
    const nmo_serialize_context_t *ctx = nmo_serialize_context_try(context);
    return ctx ? ctx->repository : NULL;
}

static inline uint32_t nmo_serialize_context_get_flags(void *context)
{
    const nmo_serialize_context_t *ctx = nmo_serialize_context_try(context);
    return ctx ? ctx->flags : NMO_SERIALIZE_FLAG_NONE;
}

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCHEMA_INTERFACE_H */
