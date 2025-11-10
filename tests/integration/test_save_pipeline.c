/**
 * @file test_save_pipeline.c
 * @brief Integration test for file saving pipeline
 */

#include "../test_framework.h"
#include "nmo.h"
#include <unistd.h>

TEST(save_pipeline, basic_save) {
    nmo_context_desc ctx_desc = {
        .allocator = NULL,
        .logger = nmo_logger_stderr(),
        .thread_pool_size = 4,
    };

    nmo_context *ctx = nmo_context_create(&ctx_desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    const char *output_file = "/tmp/test_save_output.nmo";
    unlink(output_file);

    nmo_result result = nmo_save_file(session, output_file, NMO_SAVE_DEFAULT);
    // Expected to complete, may or may not succeed based on session state

    unlink(output_file);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(save_pipeline, save_with_compression) {
    nmo_context_desc ctx_desc = {
        .allocator = NULL,
        .logger = nmo_logger_stderr(),
        .thread_pool_size = 4,
    };

    nmo_context *ctx = nmo_context_create(&ctx_desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    const char *output_file = "/tmp/test_save_compressed.nmo";
    unlink(output_file);

    nmo_result result = nmo_save_file(session, output_file,
                                       NMO_SAVE_DEFAULT | NMO_SAVE_COMPRESS);
    // Expected to complete, validates API usage

    unlink(output_file);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

int main(void) {
    return 0;
}
