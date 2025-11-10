/**
 * @file test_session.c
 * @brief Unit tests for session management
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(session, create_session) {
    nmo_context_desc ctx_desc = {
        .allocator = NULL,
        .logger = nmo_logger_stderr(),
        .thread_pool_size = 4,
    };

    nmo_context *ctx = nmo_context_create(&ctx_desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(session, get_context) {
    nmo_context_desc ctx_desc = {
        .allocator = NULL,
        .logger = nmo_logger_stderr(),
        .thread_pool_size = 4,
    };

    nmo_context *ctx = nmo_context_create(&ctx_desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_context *retrieved_ctx = nmo_session_get_context(session);
    ASSERT_EQ(retrieved_ctx, ctx);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

int main(void) {
    return 0;
}
