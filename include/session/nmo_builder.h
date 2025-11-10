/**
 * @file nmo_builder.h
 * @brief Builder pipeline for saving NMO files
 */

#ifndef NMO_BUILDER_H
#define NMO_BUILDER_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Builder context
 */
typedef struct nmo_builder nmo_builder_t;

/**
 * @brief Build stage
 */
typedef enum {
    NMO_BUILD_STAGE_INIT = 0,
    NMO_BUILD_STAGE_HEADER,
    NMO_BUILD_STAGE_HEADER1,
    NMO_BUILD_STAGE_OBJECTS,
    NMO_BUILD_STAGE_COMPLETED,
} nmo_build_stage_t;

/**
 * Create builder
 * @param output_path Output file path
 * @return Builder or NULL on error
 */
NMO_API nmo_builder_t* nmo_builder_create(const char* output_path);

/**
 * Destroy builder
 * @param builder Builder to destroy
 */
NMO_API void nmo_builder_destroy(nmo_builder_t* builder);

/**
 * Start building
 * @param builder Builder
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_builder_start(nmo_builder_t* builder);

/**
 * Build next stage
 * @param builder Builder
 * @return Current build stage or NMO_BUILD_STAGE_COMPLETED
 */
NMO_API nmo_build_stage_t nmo_builder_build_next_stage(nmo_builder_t* builder);

/**
 * Get current build stage
 * @param builder Builder
 * @return Current build stage
 */
NMO_API nmo_build_stage_t nmo_builder_get_current_stage(const nmo_builder_t* builder);

/**
 * Add object to build
 * @param builder Builder
 * @param object_id Object ID
 * @param manager_id Manager ID
 * @param data Object data
 * @param size Data size
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_builder_add_object(
    nmo_builder_t* builder, uint32_t object_id, uint32_t manager_id, const void* data, size_t size);

/**
 * Finish building
 * @param builder Builder
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_builder_finish(nmo_builder_t* builder);

/**
 * Get builder error
 * @param builder Builder
 * @return Error message or NULL if no error
 */
NMO_API const char* nmo_builder_get_error(const nmo_builder_t* builder);

/**
 * Get object count
 * @param builder Builder
 * @return Number of objects added
 */
NMO_API uint32_t nmo_builder_get_object_count(const nmo_builder_t* builder);

/**
 * Check if building is complete
 * @param builder Builder
 * @return 1 if complete, 0 otherwise
 */
NMO_API int nmo_builder_is_complete(const nmo_builder_t* builder);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BUILDER_H */
