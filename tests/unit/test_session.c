/**
 * @file test_session.c
 * @brief Unit tests for session management
 */

#include "test_framework.h"
#include "nmo.h"

/**
 * Test session creation and destruction
 */
TEST(session, create) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    
    nmo_session_t* session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * Test getting context from session
 */
TEST(session, get_context) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    
    nmo_session_t* session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    
    nmo_context_t* retrieved_ctx = nmo_session_get_context(session);
    ASSERT_EQ(retrieved_ctx, ctx);
    
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(session, create);
    REGISTER_TEST(session, get_context);
TEST_MAIN_END()
