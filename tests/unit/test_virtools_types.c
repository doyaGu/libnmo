/**
 * @file test_virtools_types.c
 * @brief Tests for Virtools data loading from JSON
 */

#include "../test_framework.h"
#include "app/nmo_context.h"
#include "app/nmo_virtools_loader.h"
#include "type/nmo_type_system.h"
#include "core/nmo_guid.h"

static nmo_context_t *g_ctx;
static nmo_type_registry_t *g_reg;

static void setup(void) {
    g_ctx = nmo_context_create(NULL);
    g_reg = nmo_context_get_type_registry(g_ctx);
    /* Load Virtools data from JSON — tests run with working dir = project root */
    nmo_virtools_load_param_types(g_reg, "data/virtools_parameter_types.json");
    nmo_virtools_load_operations(g_reg, "data/virtools_operation_types.json");
}

static void teardown(void) {
    nmo_context_release(g_ctx);
    g_ctx = NULL;
    g_reg = NULL;
}

TEST(vt, operation_addition) {
    setup();
    nmo_guid_t guid = nmo_guid_create(0x33CC6B49, 0x3589282B);
    const char *name = nmo_type_registry_guid_to_name(g_reg, guid);
    ASSERT_TRUE(name != NULL);
    ASSERT_STR_EQ(name, "Addition");
    teardown();
}

TEST(vt, operation_equal) {
    setup();
    nmo_guid_t guid = nmo_guid_create(0x6C62476B, 0x7A922973);
    ASSERT_TRUE(nmo_type_registry_guid_to_name(g_reg, guid) != NULL);
    teardown();
}

TEST(vt, operation_get_position) {
    setup();
    nmo_guid_t guid = nmo_guid_create(0x4BC87AEA, 0x6B5B643E);
    const char *name = nmo_type_registry_guid_to_name(g_reg, guid);
    ASSERT_TRUE(name != NULL);
    ASSERT_STR_EQ(name, "Get Position");
    teardown();
}

TEST(vt, operation_unknown) {
    setup();
    nmo_guid_t guid = nmo_guid_create(0xDEADDEAD, 0xBEEFBEEF);
    ASSERT_TRUE(nmo_type_registry_guid_to_name(g_reg, guid) == NULL);
    teardown();
}

TEST(vt, param_type_direction) {
    setup();
    nmo_guid_t guid = nmo_guid_create(0x0286652D, 0x5EA709C2);
    const char *name = nmo_type_registry_guid_to_name(g_reg, guid);
    ASSERT_TRUE(name != NULL);
    ASSERT_STR_EQ(name, "Direction");
    teardown();
}

TEST(vt, operation_has_category_flag) {
    setup();
    nmo_guid_t guid = nmo_guid_create(0x33CC6B49, 0x3589282B);
    nmo_type_id_t tid = nmo_type_registry_guid_to_type_id(g_reg, guid);
    ASSERT_TRUE(tid != NMO_TYPE_ID_INVALID);
    const nmo_type_descriptor_t *desc = nmo_type_registry_get_by_id(g_reg, tid);
    ASSERT_TRUE(desc != NULL);
    ASSERT_TRUE((desc->category & NMO_TYPE_CATEGORY_OPERATION) != 0);
    teardown();
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(vt, operation_addition);
    REGISTER_TEST(vt, operation_equal);
    REGISTER_TEST(vt, operation_get_position);
    REGISTER_TEST(vt, operation_unknown);
    REGISTER_TEST(vt, param_type_direction);
    REGISTER_TEST(vt, operation_has_category_flag);
TEST_MAIN_END()
