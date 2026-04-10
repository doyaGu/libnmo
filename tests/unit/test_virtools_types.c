/**
 * @file test_virtools_types.c
 * @brief Tests for Virtools CK2 parameter types and operation types registration
 */

#include "../test_framework.h"
#include "app/nmo_context.h"
#include "type/nmo_type_system.h"
#include "core/nmo_guid.h"

/* === Operation type lookups via type_registry === */

TEST(virtools_types, operation_addition)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_TRUE(ctx != NULL);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);
    ASSERT_TRUE(reg != NULL);

    /* CKOGUID_ADDITION = (0x33CC6B49, 0x3589282B) */
    nmo_guid_t guid = nmo_guid_create(0x33CC6B49, 0x3589282B);
    const char *name = nmo_type_registry_guid_to_name(reg, guid);
    ASSERT_TRUE(name != NULL);
    ASSERT_STR_EQ(name, "Addition");

    nmo_context_release(ctx);
}

TEST(virtools_types, operation_equal)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    nmo_guid_t guid = nmo_guid_create(0x6C62476B, 0x7A922973);
    const char *name = nmo_type_registry_guid_to_name(reg, guid);
    ASSERT_TRUE(name != NULL);
    ASSERT_STR_EQ(name, "Equal");

    nmo_context_release(ctx);
}

TEST(virtools_types, operation_get_position)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    /* CKOGUID_GET_POSITION = (0x4BC87AEA, 0x6B5B643E) */
    nmo_guid_t guid = nmo_guid_create(0x4BC87AEA, 0x6B5B643E);
    const char *name = nmo_type_registry_guid_to_name(reg, guid);
    ASSERT_TRUE(name != NULL);
    ASSERT_STR_EQ(name, "Get Position");

    nmo_context_release(ctx);
}

TEST(virtools_types, operation_unknown)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    nmo_guid_t guid = nmo_guid_create(0xDEADDEAD, 0xBEEFBEEF);
    const char *name = nmo_type_registry_guid_to_name(reg, guid);
    ASSERT_TRUE(name == NULL);

    nmo_context_release(ctx);
}

/* === Parameter type lookups (enriched from Virtools data) === */

TEST(virtools_types, param_type_float_exists)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    /* CKPGUID_FLOAT = (0x47884c3f, 0x432c2c20) — registered by builtin */
    nmo_guid_t guid = nmo_guid_create(0x47884c3f, 0x432c2c20);
    const char *name = nmo_type_registry_guid_to_name(reg, guid);
    ASSERT_TRUE(name != NULL);

    nmo_context_release(ctx);
}

TEST(virtools_types, param_type_direction_enum)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    /* Direction = (0x0286652D, 0x5EA709C2) — enum type from Virtools data */
    nmo_guid_t guid = nmo_guid_create(0x0286652D, 0x5EA709C2);
    const char *name = nmo_type_registry_guid_to_name(reg, guid);
    ASSERT_TRUE(name != NULL);
    ASSERT_STR_EQ(name, "Direction");

    nmo_context_release(ctx);
}

TEST(virtools_types, operation_category_flag)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    nmo_guid_t guid = nmo_guid_create(0x33CC6B49, 0x3589282B); /* Addition */
    nmo_type_id_t tid = nmo_type_registry_guid_to_type_id(reg, guid);
    ASSERT_TRUE(tid != NMO_TYPE_ID_INVALID);

    const nmo_type_descriptor_t *desc = nmo_type_registry_get_by_id(reg, tid);
    ASSERT_TRUE(desc != NULL);
    ASSERT_TRUE((desc->category & NMO_TYPE_CATEGORY_OPERATION) != 0);

    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(virtools_types, operation_addition);
    REGISTER_TEST(virtools_types, operation_equal);
    REGISTER_TEST(virtools_types, operation_get_position);
    REGISTER_TEST(virtools_types, operation_unknown);
    REGISTER_TEST(virtools_types, param_type_float_exists);
    REGISTER_TEST(virtools_types, param_type_direction_enum);
    REGISTER_TEST(virtools_types, operation_category_flag);
TEST_MAIN_END()
