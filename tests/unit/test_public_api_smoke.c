/**
 * @file test_public_api_smoke.c
 * @brief Smoke tests for the main public header (nmo.h)
 */

#include "test_framework.h"
#include "nmo.h"
#include "object/nmo_object_query.h"
#include "session/nmo_session_edit.h"

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

TEST(public_api_smoke, preferred_edit_and_query_headers_are_directly_usable) {
    nmo_session_edit_t *edit = NULL;
    nmo_object_query_t query = {0};
    query.name_mode = NMO_OBJECT_QUERY_NAME_EXACT;

    ASSERT_NULL(edit);
    ASSERT_EQ(NMO_OBJECT_QUERY_NAME_EXACT, query.name_mode);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(public_api_smoke, version);
    REGISTER_TEST(public_api_smoke, context_create_release);
    REGISTER_TEST(public_api_smoke, preferred_edit_and_query_headers_are_directly_usable);
TEST_MAIN_END()
