/**
 * @file test_virtools_types.c
 * @brief Tests for Virtools data loading via context data_dir
 */

#include "../test_framework.h"
#include "app/nmo_context.h"
#include "type/nmo_type_system.h"
#include "core/nmo_guid.h"

#include <string.h>

static nmo_context_t *create_ctx_with_data(void) {
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.data_dir = "data";
    return nmo_context_create(&desc);
}

TEST(vt, operation_addition) {
    nmo_context_t *ctx = create_ctx_with_data();
    ASSERT_TRUE(ctx != NULL);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);
    nmo_guid_t guid = nmo_guid_create(0x33CC6B49, 0x3589282B);
    const char *name = nmo_type_registry_guid_to_name(reg, guid);
    ASSERT_TRUE(name != NULL);
    ASSERT_STR_EQ(name, "Addition");
    nmo_context_release(ctx);
}

TEST(vt, operation_equal) {
    nmo_context_t *ctx = create_ctx_with_data();
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);
    nmo_guid_t guid = nmo_guid_create(0x6C62476B, 0x7A922973);
    ASSERT_TRUE(nmo_type_registry_guid_to_name(reg, guid) != NULL);
    nmo_context_release(ctx);
}

TEST(vt, operation_get_position) {
    nmo_context_t *ctx = create_ctx_with_data();
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);
    nmo_guid_t guid = nmo_guid_create(0x4BC87AEA, 0x6B5B643E);
    const char *name = nmo_type_registry_guid_to_name(reg, guid);
    ASSERT_TRUE(name != NULL);
    ASSERT_STR_EQ(name, "Get Position");
    nmo_context_release(ctx);
}

TEST(vt, operation_unknown) {
    nmo_context_t *ctx = create_ctx_with_data();
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);
    nmo_guid_t guid = nmo_guid_create(0xDEADDEAD, 0xBEEFBEEF);
    ASSERT_TRUE(nmo_type_registry_guid_to_name(reg, guid) == NULL);
    nmo_context_release(ctx);
}

TEST(vt, param_type_direction) {
    nmo_context_t *ctx = create_ctx_with_data();
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);
    nmo_guid_t guid = nmo_guid_create(0x0286652D, 0x5EA709C2);
    const char *name = nmo_type_registry_guid_to_name(reg, guid);
    ASSERT_TRUE(name != NULL);
    ASSERT_STR_EQ(name, "Direction");
    nmo_context_release(ctx);
}

TEST(vt, operation_has_category_flag) {
    nmo_context_t *ctx = create_ctx_with_data();
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);
    nmo_guid_t guid = nmo_guid_create(0x33CC6B49, 0x3589282B);
    nmo_type_id_t tid = nmo_type_registry_guid_to_type_id(reg, guid);
    ASSERT_TRUE(tid != NMO_TYPE_ID_INVALID);
    const nmo_type_descriptor_t *desc = nmo_type_registry_get_by_id(reg, tid);
    ASSERT_TRUE(desc != NULL);
    ASSERT_TRUE((desc->category & NMO_TYPE_CATEGORY_OPERATION) != 0);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(vt, operation_addition);
    REGISTER_TEST(vt, operation_equal);
    REGISTER_TEST(vt, operation_get_position);
    REGISTER_TEST(vt, operation_unknown);
    REGISTER_TEST(vt, param_type_direction);
    REGISTER_TEST(vt, operation_has_category_flag);
TEST_MAIN_END()
