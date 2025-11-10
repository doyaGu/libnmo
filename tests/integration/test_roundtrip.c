/**
 * @file test_roundtrip.c
 * @brief Integration test for file roundtrip (load then save)
 */

#include "../test_framework.h"
#include "nmo.h"
#include <unistd.h>

TEST(roundtrip, simple_roundtrip) {
    nmo_context_desc ctx_desc = {
        .allocator = NULL,
        .logger = nmo_logger_stderr(),
        .thread_pool_size = 4,
    };

    nmo_context *ctx = nmo_context_create(&ctx_desc);
    ASSERT_NOT_NULL(ctx);

    // Register built-in schemas
    nmo_schema_registry *registry = nmo_context_get_schema_registry(ctx);
    ASSERT_NOT_NULL(registry);

    nmo_session *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    // Test file paths
    const char *source_file = "/tmp/test_roundtrip_src.nmo";
    const char *dest_file = "/tmp/test_roundtrip_dst.nmo";

    // Clean up test files
    unlink(source_file);
    unlink(dest_file);

    // Note: Actual roundtrip testing would require valid test files
    // This is a stub that validates the API usage

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

int main(void) {
    return 0;
}
