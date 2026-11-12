/**
 * @file test_id_sanitizer.c
 * @brief Unit tests for nmo_id_sanitize
 */

#include "session/nmo_session_pipeline.h"
#include "test_framework.h"

TEST(id_sanitizer, strips_mask_and_passthrough) {
    ASSERT_EQ(nmo_id_sanitize(NMO_OBJECT_REFERENCE_FLAG | 0x42u), 0x42u);
    ASSERT_EQ(nmo_id_sanitize(0x00001234u), 0x00001234u);
    ASSERT_EQ(nmo_id_sanitize(0u), 0u);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(id_sanitizer, strips_mask_and_passthrough);
TEST_MAIN_END()
