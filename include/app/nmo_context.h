/**
 * @file nmo_context.h
 * @brief Global context for NMO library (Phase 8.1)
 */

#ifndef NMO_CONTEXT_H
#define NMO_CONTEXT_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_allocator nmo_allocator;
typedef struct nmo_logger nmo_logger;
typedef struct nmo_schema_registry nmo_schema_registry;
typedef struct nmo_manager_registry nmo_manager_registry;

/**
 * @brief Global context structure
 *
 * Reference-counted global state that owns schema registry and manager registry.
 * Thread-safe for concurrent access.
 */
typedef struct nmo_context nmo_context;

/**
 * @brief Context descriptor for creation
 */
typedef struct {
    nmo_allocator* allocator;      /**< Memory allocator (NULL for default) */
    nmo_logger* logger;            /**< Logger (NULL for default) */
    int thread_pool_size;          /**< Thread pool size (0 for no threading) */
} nmo_context_desc;

/**
 * @brief Create context
 *
 * Creates a new context with the given configuration. The context is
 * reference-counted and starts with a reference count of 1.
 *
 * @param desc Context descriptor (NULL for defaults)
 * @return Context or NULL on error
 */
NMO_API nmo_context* nmo_context_create(const nmo_context_desc* desc);

/**
 * @brief Retain context
 *
 * Increments the reference count. Thread-safe.
 *
 * @param ctx Context to retain
 */
NMO_API void nmo_context_retain(nmo_context* ctx);

/**
 * @brief Release context
 *
 * Decrements the reference count. When count reaches 0, the context
 * is destroyed. Thread-safe.
 *
 * @param ctx Context to release
 */
NMO_API void nmo_context_release(nmo_context* ctx);

/**
 * @brief Get schema registry
 *
 * Thread-safe access to the schema registry.
 *
 * @param ctx Context
 * @return Schema registry or NULL
 */
NMO_API nmo_schema_registry* nmo_context_get_schema_registry(const nmo_context* ctx);

/**
 * @brief Get manager registry
 *
 * Thread-safe access to the manager registry.
 *
 * @param ctx Context
 * @return Manager registry or NULL
 */
NMO_API nmo_manager_registry* nmo_context_get_manager_registry(const nmo_context* ctx);

/**
 * @brief Get allocator
 *
 * @param ctx Context
 * @return Allocator
 */
NMO_API nmo_allocator* nmo_context_get_allocator(const nmo_context* ctx);

/**
 * @brief Get logger
 *
 * @param ctx Context
 * @return Logger
 */
NMO_API nmo_logger* nmo_context_get_logger(const nmo_context* ctx);

/**
 * @brief Get reference count (for debugging)
 *
 * @param ctx Context
 * @return Current reference count
 */
NMO_API int nmo_context_get_refcount(const nmo_context* ctx);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CONTEXT_H */
