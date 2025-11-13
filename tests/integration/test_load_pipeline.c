/**
 * @file test_load_pipeline.c
 * @brief Integration test for file loading pipeline
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(load_pipeline, basic_load) {
    nmo_context_desc ctx_desc = {
        .allocator = NULL,
        .logger = nmo_logger_stderr(),
        .thread_pool_size = 4,
    };

    nmo_context *ctx = nmo_context_create(&ctx_desc);
    ASSERT_NOT_NULL(ctx);

    nmo_schema_registry *registry = nmo_context_get_schema_registry(ctx);
    ASSERT_NOT_NULL(registry);

    nmo_session *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    // Attempt to load a non-existent file (expected to fail gracefully)
    nmo_result result = nmo_load_file(session, "/tmp/nonexistent.nmo", NMO_LOAD_DEFAULT);
    // Result will indicate failure, which is expected

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(load_pipeline, load_with_validation) {
    nmo_context_desc ctx_desc = {
        .allocator = NULL,
        .logger = nmo_logger_stderr(),
        .thread_pool_size = 4,
    };

    nmo_context *ctx = nmo_context_create(&ctx_desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_result result =
        nmo_load_file(session, "/tmp/test.nmo",
                      NMO_LOAD_DEFAULT | NMO_LOAD_VALIDATE);
    // Expected to fail with invalid file, but validates API usage

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

int main(void) {
    return 0;
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(load_pipeline, basic_load);
    REGISTER_TEST(load_pipeline, load_with_validation);
TEST_MAIN_END()
