/**
 * @file nmo_context.h
 * @brief Global context for NMO library.
 */

#ifndef NMO_RUNTIME_CONTEXT_H
#define NMO_RUNTIME_CONTEXT_H

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

typedef struct nmo_allocator nmo_allocator_t;
typedef struct nmo_manager_registry nmo_manager_registry_t;
typedef struct nmo_extension_registry nmo_extension_registry_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_bb_registry nmo_behavior_registry_t;

typedef struct nmo_context nmo_context_t;

typedef struct nmo_context_desc {
    const nmo_allocator_t *allocator;
    const nmo_logger_t *logger;
    int thread_pool_size;
    const char *data_dir;
} nmo_context_desc_t;

NMO_API nmo_context_t *nmo_context_create(const nmo_context_desc_t *desc);
NMO_API void nmo_context_retain(nmo_context_t *ctx);
NMO_API void nmo_context_release(nmo_context_t *ctx);
NMO_API nmo_type_registry_t *nmo_context_get_type_registry(const nmo_context_t *ctx);
NMO_API nmo_operation_registry_t *nmo_context_get_operation_registry(const nmo_context_t *ctx);
NMO_API nmo_behavior_registry_t *nmo_context_get_bb_registry(const nmo_context_t *ctx);
NMO_API const nmo_type_runtime_t *nmo_context_get_type_runtime(const nmo_context_t *ctx);
NMO_API nmo_manager_registry_t *nmo_context_get_manager_registry(const nmo_context_t *ctx);
NMO_API nmo_extension_registry_t *nmo_context_get_extension_registry(const nmo_context_t *ctx);
NMO_API nmo_allocator_t *nmo_context_get_allocator(const nmo_context_t *ctx);
NMO_API nmo_logger_t *nmo_context_get_logger(const nmo_context_t *ctx);
NMO_API void nmo_context_set_logger(nmo_context_t *ctx, const nmo_logger_t *logger);
NMO_API void nmo_context_enable_logging(nmo_context_t *ctx, int enable);
NMO_API void nmo_context_set_log_level(nmo_context_t *ctx, nmo_log_level_t level);
NMO_API nmo_arena_t *nmo_context_get_arena(const nmo_context_t *ctx);
NMO_API int nmo_context_get_refcount(const nmo_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* NMO_RUNTIME_CONTEXT_H */
