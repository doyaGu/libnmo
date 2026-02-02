/**
 * @file test_public_api_smoke.c
 * @brief Smoke tests for the main public header (nmo.h)
 */

#include "test_framework.h"
#include "nmo.h"

TEST(public_api_smoke, version) {
    const char *ver = nmo_version();
    ASSERT_NOT_NULL(ver);
    ASSERT_TRUE(ver[0] != '\0');

    uint32_t ver_int = nmo_version_int();
    ASSERT_NE(0u, ver_int);
}

TEST(public_api_smoke, context_create_release) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(public_api_smoke, version);
    REGISTER_TEST(public_api_smoke, context_create_release);
TEST_MAIN_END()
