/**
 * @file test_chunk_remap.c
 * @brief Unit tests for chunk remapping
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(chunk_remap, create_remap_context) {
    nmo_chunk_remap_ctx *ctx = nmo_chunk_remap_ctx_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_chunk_remap_ctx_destroy(ctx);
}

TEST(chunk_remap, add_mapping) {
    nmo_chunk_remap_ctx *ctx = nmo_chunk_remap_ctx_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_chunk_remap_ctx_add_mapping(ctx, 1, 10);
    nmo_chunk_remap_ctx_destroy(ctx);
}

TEST(chunk_remap, get_mapping) {
    nmo_chunk_remap_ctx *ctx = nmo_chunk_remap_ctx_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_chunk_remap_ctx_add_mapping(ctx, 1, 10);
    uint32_t mapped = nmo_chunk_remap_ctx_get_mapping(ctx, 1);
    ASSERT_EQ(mapped, 10);

    nmo_chunk_remap_ctx_destroy(ctx);
}

int main(void) {
    return 0;
}
