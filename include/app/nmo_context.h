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
typedef struct nmo_allocator nmo_allocator_t;
typedef struct nmo_logger nmo_logger_t;
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_manager_registry nmo_manager_registry_t;
typedef struct nmo_plugin_manager nmo_plugin_manager_t;
typedef struct nmo_arena nmo_arena_t;

/**
 * @brief Global context structure
 *
 * Reference-counted global state that owns schema registry and manager registry.
 * Thread-safe for concurrent access.
 */
typedef struct nmo_context nmo_context_t;

/**
 * @brief Context descriptor for creation
 */
typedef struct nmo_context_desc {
    nmo_allocator_t *allocator; /**< Memory allocator (NULL for default) */
    nmo_logger_t *logger;       /**< Logger (NULL for default) */
    int thread_pool_size;       /**< Thread pool size (0 for no threading) */
} nmo_context_desc_t;

/**
 * @brief Create context
 *
 * Creates a new context with the given configuration. The context is
 * reference-counted and starts with a reference count of 1.
 *
 * @param desc Context descriptor (NULL for defaults)
 * @return Context or NULL on error
 */
NMO_API nmo_context_t *nmo_context_create(const nmo_context_desc_t *desc);

/**
 * @brief Retain context
 *
 * Increments the reference count. Thread-safe across MSVC, GCC, and Clang via
 * the platform-specific atomic primitives used internally.
 *
 * @param ctx Context to retain
 */
NMO_API void nmo_context_retain(nmo_context_t *ctx);

/**
 * @brief Release context
 *
 * Decrements the reference count. When count reaches 0, the context
 * is destroyed. Uses the same cross-compiler atomic primitives as retain,
 * making it safe to call from multiple threads simultaneously.
 *
 * @param ctx Context to release
 */
NMO_API void nmo_context_release(nmo_context_t *ctx);

/**
 * @brief Destroy context (alias for release)
 *
 * Convenience function, equivalent to nmo_context_release().
 *
 * @param ctx Context to destroy
 */
static inline void nmo_context_destroy(nmo_context_t *ctx) {
    nmo_context_release(ctx);
}

/**
 * @brief Get schema registry
 *
 * Thread-safe access to the schema registry.
 *
 * @param ctx Context
 * @return Schema registry or NULL
 */
NMO_API nmo_schema_registry_t *nmo_context_get_schema_registry(const nmo_context_t *ctx);

/**
 * @brief Get manager registry
 *
 * Thread-safe access to the manager registry.
 *
 * @param ctx Context
 * @return Manager registry or NULL
 */
NMO_API nmo_manager_registry_t *nmo_context_get_manager_registry(const nmo_context_t *ctx);

/**
 * @brief Get plugin manager
 */
NMO_API nmo_plugin_manager_t *nmo_context_get_plugin_manager(const nmo_context_t *ctx);

/**
 * @brief Get allocator
 *
 * @param ctx Context
 * @return Allocator
 */
NMO_API nmo_allocator_t *nmo_context_get_allocator(const nmo_context_t *ctx);

/**
 * @brief Get logger
 *
 * @param ctx Context
 * @return Logger
 */
NMO_API nmo_logger_t *nmo_context_get_logger(const nmo_context_t *ctx);

/**
 * @brief Get the arena owned by the context
 *
 * @param ctx Context
 * @return Arena pointer or NULL
 */
NMO_API nmo_arena_t *nmo_context_get_arena(const nmo_context_t *ctx);

/**
 * @brief Get reference count (for debugging)
 *
 * @param ctx Context
 * @return Current reference count
 */
NMO_API int nmo_context_get_refcount(const nmo_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CONTEXT_H */
