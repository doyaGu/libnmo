/**
 * @file builder.c
 * @brief Builder pipeline for saving NMO files implementation
 */

#include "session/nmo_builder.h"

/**
 * Create builder
 */
nmo_builder_t* nmo_builder_create(const char* output_path) {
    (void)output_path;
    return NULL;
}

/**
 * Destroy builder
 */
void nmo_builder_destroy(nmo_builder_t* builder) {
    (void)builder;
}

/**
 * Start building
 */
nmo_result_t nmo_builder_start(nmo_builder_t* builder) {
    (void)builder;
    return nmo_result_ok();
}

/**
 * Build next stage
 */
nmo_build_stage_t nmo_builder_build_next_stage(nmo_builder_t* builder) {
    (void)builder;
    return NMO_BUILD_STAGE_COMPLETED;
}

/**
 * Get current build stage
 */
nmo_build_stage_t nmo_builder_get_current_stage(const nmo_builder_t* builder) {
    (void)builder;
    return NMO_BUILD_STAGE_INIT;
}

/**
 * Add object to build
 */
nmo_result_t nmo_builder_add_object(
    nmo_builder_t* builder, uint32_t object_id, uint32_t manager_id, const void* data, size_t size) {
    (void)builder;
    (void)object_id;
    (void)manager_id;
    (void)data;
    (void)size;
    return nmo_result_ok();
}

/**
 * Finish building
 */
nmo_result_t nmo_builder_finish(nmo_builder_t* builder) {
    (void)builder;
    return nmo_result_ok();
}

/**
 * Get builder error
 */
const char* nmo_builder_get_error(const nmo_builder_t* builder) {
    (void)builder;
    return NULL;
}

/**
 * Get object count
 */
uint32_t nmo_builder_get_object_count(const nmo_builder_t* builder) {
    (void)builder;
    return 0;
}

/**
 * Check if building is complete
 */
int nmo_builder_is_complete(const nmo_builder_t* builder) {
    (void)builder;
    return 0;
}
