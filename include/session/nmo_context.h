/**
 * @file nmo_context.h
 * @brief Global context for NMO library (Phase 8.1)
 */

#ifndef NMO_CONTEXT_H
#define NMO_CONTEXT_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_logger.h"
#include "type/nmo_type_runtime.h"

#define NMO_CONTEXT_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_MIXED_TIER
#define NMO_CONTEXT_LIFECYCLE_API_TIER NMO_API_TIER_STABLE_CONSUMER
#define NMO_CONTEXT_REGISTRY_ACCESS_API_TIER NMO_API_TIER_ADVANCED_C
#define NMO_CONTEXT_RUNTIME_VIEW_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_allocator nmo_allocator_t;
typedef struct nmo_manager_registry nmo_manager_registry_t;
typedef struct nmo_extension_registry nmo_extension_registry_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_bb_registry nmo_behavior_registry_t;

/**
 * @brief Global context structure
 *
 * Reference-counted global state that owns schema registry and manager registry.
 * retain/release is thread-safe; registry mutation remains caller-synchronized.
 */
typedef struct nmo_context nmo_context_t;

/**
 * @brief Context descriptor for creation
 */
typedef struct nmo_context_desc {
    const nmo_allocator_t *allocator; /**< Memory allocator config (NULL for default) */
    const nmo_logger_t *logger;       /**< Logger config (NULL for default) */
    int thread_pool_size;       /**< Thread pool size (0 for no threading) */
    const char *data_dir;       /**< Virtools data directory (NULL = auto-discover) */
} nmo_context_desc_t;

/**
 * @brief Create context
 *
 * Creates a new context with the given configuration. The context is
 * reference-counted and starts with a reference count of 1.
 *
 * @param desc Context descriptor (NULL for defaults)
 * @return Context or NULL on error
 * @ownership owned
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
 * @brief Get type registry (schema v2)
 *
 * @param ctx Context
 * @return Type registry, or NULL if not initialized
 * @ownership borrowed
 */
NMO_API nmo_type_registry_t *nmo_context_get_type_registry(const nmo_context_t *ctx);

/**
 * @brief Get operation registry
 *
 * @param ctx Context
 * @return Operation registry, or NULL if not initialized
 * @ownership borrowed
 */
NMO_API nmo_operation_registry_t *nmo_context_get_operation_registry(const nmo_context_t *ctx);

/**
 * @brief Get BB prototype registry
 *
 * @param ctx Context
 * @return BB registry, or NULL if not initialized
 * @ownership borrowed
 */
NMO_API nmo_behavior_registry_t *nmo_context_get_bb_registry(const nmo_context_t *ctx);

/**
 * @brief Get aggregated type runtime view
 *
 * @param ctx Context
 * @return Runtime view, or NULL if context is NULL
 * @ownership borrowed
 */
NMO_API const nmo_type_runtime_t *nmo_context_get_type_runtime(const nmo_context_t *ctx);

/**
 * @brief Get manager registry
 *
 * Borrowed pointer; caller-synchronized for registry operations.
 *
 * @param ctx Context
 * @return Manager registry or NULL
 * @ownership borrowed
 */
NMO_API nmo_manager_registry_t *nmo_context_get_manager_registry(const nmo_context_t *ctx);

/**
 * @brief Get extension registry
 *
 * Borrowed pointer; caller-synchronized for registry operations.
 *
 * @param ctx Context
 * @return Extension registry or NULL
 * @ownership borrowed
 */
NMO_API nmo_extension_registry_t *nmo_context_get_extension_registry(const nmo_context_t *ctx);

/**
 * @brief Get allocator
 *
 * @param ctx Context
 * @return Allocator
 * @ownership borrowed
 */
NMO_API nmo_allocator_t *nmo_context_get_allocator(const nmo_context_t *ctx);

/**
 * @brief Get logger
 *
 * @param ctx Context
 * @return Logger
 * @ownership borrowed
 */
NMO_API nmo_logger_t *nmo_context_get_logger(const nmo_context_t *ctx);

/**
 * @brief Replace the context logger
 *
 * Copies the provided logger configuration into the context. Pass NULL to
 * restore the default (null) logger.
 *
 * @param ctx Context
 * @param logger Logger configuration to copy (NULL for default)
 */
NMO_API void nmo_context_set_logger(nmo_context_t *ctx, const nmo_logger_t *logger);

/**
 * @brief Enable or disable default logging to stderr
 *
 * Convenience wrapper that switches between the built-in stderr logger and the
 * null logger.
 *
 * @param ctx Context
 * @param enable Non-zero to enable logging, zero to disable
 */
NMO_API void nmo_context_enable_logging(nmo_context_t *ctx, int enable);

/**
 * @brief Set minimum log level for the current logger
 *
 * If the logger is not owned by the context it will be copied before adjusting.
 *
 * @param ctx Context
 * @param level Minimum log level
 */
NMO_API void nmo_context_set_log_level(nmo_context_t *ctx, nmo_log_level_t level);

/**
 * @brief Get the arena owned by the context
 *
 * @param ctx Context
 * @return Arena pointer or NULL
 * @ownership borrowed
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
