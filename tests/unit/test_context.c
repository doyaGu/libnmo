/**
 * @file test_context.c
 * @brief Unit tests for application context
 */

#include "../test_framework.h"
#include "nmo.h"

/**
 * Test context creation and destruction
 */
TEST(context, create) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    
    nmo_context_release(ctx);
}

/**
 * Test getting allocator from context
 */
TEST(context, get_allocator) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    
    nmo_allocator_t* alloc = nmo_context_get_allocator(ctx);
    ASSERT_NOT_NULL(alloc);
    
    nmo_context_release(ctx);
}

/**
 * Test getting logger from context
 */
TEST(context, get_logger) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    
    nmo_logger_t* logger = nmo_context_get_logger(ctx);
    ASSERT_NOT_NULL(logger);
    
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(context, create);
    REGISTER_TEST(context, get_allocator);
    REGISTER_TEST(context, get_logger);
TEST_MAIN_END()
