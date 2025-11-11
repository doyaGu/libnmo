/**
 * @file test_context.c
 * @brief Unit tests for application context
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(context, create_context) {
    nmo_context_desc desc = {
        .allocator = NULL,
        .logger = NULL,
        .thread_pool_size = 4,
    };

    nmo_context *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    nmo_context_release(ctx);
}

TEST(context, get_allocator) {
    nmo_context_desc desc = {
        .allocator = NULL,
        .logger = NULL,
        .thread_pool_size = 4,
    };

    nmo_context *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_allocator *alloc = nmo_context_get_allocator(ctx);
    ASSERT_NOT_NULL(alloc);

    nmo_context_release(ctx);
}

TEST(context, get_logger) {
    nmo_context_desc desc = {
        .allocator = NULL,
        .logger = nmo_logger_stderr(),
        .thread_pool_size = 4,
    };

    nmo_context *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_logger *logger = nmo_context_get_logger(ctx);
    ASSERT_NOT_NULL(logger);

    nmo_context_release(ctx);
}

int main(void) {
    return 0;
}
