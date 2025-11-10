/**
 * @file nmo_context.h
 * @brief Global NMO context and initialization
 */

#ifndef NMO_CONTEXT_H
#define NMO_CONTEXT_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Global NMO context
 */
typedef struct nmo_context nmo_context_t;

/**
 * @brief Context configuration
 */
typedef struct {
    size_t max_object_size;  /* Maximum object size */
    size_t buffer_pool_size; /* Size of buffer pool */
    int enable_compression;  /* Enable compression */
    int enable_validation;   /* Enable schema validation */
} nmo_context_config_t;

/**
 * Initialize global NMO context
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_context_init(void);

/**
 * Initialize global NMO context with config
 * @param config Configuration
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_context_init_with_config(const nmo_context_config_t* config);

/**
 * Cleanup global NMO context
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_context_cleanup(void);

/**
 * Get global NMO context
 * @return Global context
 */
NMO_API nmo_context_t* nmo_context_get_global(void);

/**
 * Register schema registry with context
 * @param context Context
 * @param registry Schema registry
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_context_set_schema_registry(nmo_context_t* context, void* registry);

/**
 * Get schema registry from context
 * @param context Context
 * @return Schema registry
 */
NMO_API void* nmo_context_get_schema_registry(const nmo_context_t* context);

/**
 * Register manager registry with context
 * @param context Context
 * @param registry Manager registry
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_context_set_manager_registry(nmo_context_t* context, void* registry);

/**
 * Get manager registry from context
 * @param context Context
 * @return Manager registry
 */
NMO_API void* nmo_context_get_manager_registry(const nmo_context_t* context);

/**
 * Set context configuration
 * @param context Context
 * @param config Configuration
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_context_set_config(nmo_context_t* context, const nmo_context_config_t* config);

/**
 * Get context configuration
 * @param context Context
 * @param out_config Output configuration
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_context_get_config(const nmo_context_t* context, nmo_context_config_t* out_config);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CONTEXT_H */
