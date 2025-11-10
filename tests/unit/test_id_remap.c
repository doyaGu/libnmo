/**
 * @file test_id_remap.c
 * @brief Unit tests for ID remapping
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(id_remap, create_remap_context) {
    nmo_id_remap_ctx *ctx = nmo_id_remap_ctx_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_id_remap_ctx_destroy(ctx);
}

TEST(id_remap, add_mapping) {
    nmo_id_remap_ctx *ctx = nmo_id_remap_ctx_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_id_remap_ctx_add_mapping(ctx, 1, 100);
    nmo_id_remap_ctx_destroy(ctx);
}

TEST(id_remap, get_mapping) {
    nmo_id_remap_ctx *ctx = nmo_id_remap_ctx_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_id_remap_ctx_add_mapping(ctx, 1, 100);
    uint32_t mapped = nmo_id_remap_ctx_get_mapping(ctx, 1);
    ASSERT_EQ(mapped, 100);

    nmo_id_remap_ctx_destroy(ctx);
}

int main(void) {
    return 0;
}
