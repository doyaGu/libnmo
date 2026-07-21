/**
 * @file test_string_registration.c
 * @brief Unit tests for string-based type registration API (Phase 6.2, Task 6.2.2)
 * 
 * Tests nmo_type_registry_register_enum_string() and nmo_type_registry_register_flags_string()
 */

#include "test_framework.h"
#include "type/nmo_dynamic_types.h"
#include "type/nmo_type_system.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena.h"
#include <string.h>

/* ============================================================================
 * Test Fixtures
 * ============================================================================ */

static nmo_type_registry_t *g_type_registry = NULL;
static nmo_arena_t *g_type_arena = NULL;

static void setup(void) {
    g_type_arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, g_type_arena);
    g_type_registry = nmo_type_registry_create(g_type_arena);
    ASSERT_NE(NULL, g_type_registry);
}

static void teardown(void) {
    if (g_type_registry) {
        nmo_type_registry_destroy(g_type_registry);
        g_type_registry = NULL;
    }
    if (g_type_arena) {
        nmo_arena_destroy(g_type_arena);
        g_type_arena = NULL;
    }
}

/* ============================================================================
 * register_enum_string() Tests
 * ============================================================================ */

TEST(string_registration, register_enum_string_basic) {
    setup();
    
    nmo_guid_t guid = {0x12345678, 0x9ABCDEF0};
    nmo_status_t result = nmo_type_registry_register_enum_string(
        g_type_registry,
        guid,
        "TestEnum",
        "RED=0, GREEN=1, BLUE=2"
    );
    
    ASSERT_EQ(NMO_OK, result);
    
    /* Verify enum was registered */
    const nmo_type_descriptor_t *desc = nmo_type_registry_find_by_guid(g_type_registry, guid);
    ASSERT_NE(NULL, desc);
    ASSERT_STR_EQ("TestEnum", desc->name);
    ASSERT_EQ(NMO_TYPE_CATEGORY_ENUM, desc->category);
    
    /* Verify specialized metadata */
    const nmo_specialized_metadata_t *meta = nmo_type_registry_get_metadata(g_type_registry, desc->id);
    ASSERT_NE(NULL, meta);
    ASSERT_EQ(NMO_METADATA_TYPE_ENUM, meta->metadata_type);
    ASSERT_EQ(3, meta->enum_meta.value_count);
    
    /* Check values */
    ASSERT_STR_EQ("RED", meta->enum_meta.values[0].name);
    ASSERT_EQ(0, meta->enum_meta.values[0].value);
    ASSERT_STR_EQ("GREEN", meta->enum_meta.values[1].name);
    ASSERT_EQ(1, meta->enum_meta.values[1].value);
    ASSERT_STR_EQ("BLUE", meta->enum_meta.values[2].name);
    ASSERT_EQ(2, meta->enum_meta.values[2].value);
    
    teardown();
}

TEST(string_registration, register_enum_string_with_hex_values) {
    setup();
    
    nmo_guid_t guid = {0x11111111, 0x22222222};
    nmo_status_t result = nmo_type_registry_register_enum_string(
        g_type_registry,
        guid,
        "HexEnum",
        "FLAG_A=0x01, FLAG_B=0x10, FLAG_C=0xFF"
    );
    
    ASSERT_EQ(NMO_OK, result);
    
    const nmo_type_descriptor_t *desc = nmo_type_registry_find_by_guid(g_type_registry, guid);
    ASSERT_NE(NULL, desc);
    
    const nmo_specialized_metadata_t *meta = nmo_type_registry_get_metadata(g_type_registry, desc->id);
    ASSERT_EQ(3, meta->enum_meta.value_count);
    ASSERT_EQ(0x01, meta->enum_meta.values[0].value);
    ASSERT_EQ(0x10, meta->enum_meta.values[1].value);
    ASSERT_EQ(0xFF, meta->enum_meta.values[2].value);
    
    teardown();
}

TEST(string_registration, register_enum_string_auto_guid) {
    setup();
    
    nmo_guid_t null_guid = NMO_NULL_GUID;
    nmo_status_t result = nmo_type_registry_register_enum_string(
        g_type_registry,
        null_guid,
        "AutoGuidEnum",
        "VALUE1=0, VALUE2=1"
    );
    
    ASSERT_EQ(NMO_OK, result);
    
    /* Find by name since GUID was auto-generated */
    const nmo_type_descriptor_t *desc = nmo_type_registry_find_by_name(g_type_registry, "AutoGuidEnum");
    ASSERT_NE(NULL, desc);
    ASSERT_STR_EQ("AutoGuidEnum", desc->name);
    
    /* Verify GUID is not null */
    ASSERT_EQ(false, nmo_guid_is_null(desc->guid));
    
    teardown();
}

TEST(string_registration, register_enum_string_null_registry) {
    nmo_guid_t guid = {0x1, 0x2};
    nmo_status_t result = nmo_type_registry_register_enum_string(
        NULL,
        guid,
        "Test",
        "A=0"
    );
    
    ASSERT_NE(NMO_OK, result);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
}

TEST(string_registration, register_enum_string_null_name) {
    setup();
    
    nmo_guid_t guid = {0x1, 0x2};
    nmo_status_t result = nmo_type_registry_register_enum_string(
        g_type_registry,
        guid,
        NULL,
        "A=0"
    );
    
    ASSERT_NE(NMO_OK, result);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
    
    teardown();
}

TEST(string_registration, register_enum_string_null_data) {
    setup();
    
    nmo_guid_t guid = {0x1, 0x2};
    nmo_status_t result = nmo_type_registry_register_enum_string(
        g_type_registry,
        guid,
        "Test",
        NULL
    );
    
    ASSERT_NE(NMO_OK, result);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
    
    teardown();
}

TEST(string_registration, register_enum_string_invalid_syntax) {
    setup();
    
    nmo_guid_t guid = {0x1, 0x2};
    nmo_status_t result = nmo_type_registry_register_enum_string(
        g_type_registry,
        guid,
        "Test",
        "INVALID SYNTAX"
    );
    
    ASSERT_NE(NMO_OK, result);
    
    teardown();
}

TEST(string_registration, register_enum_string_with_whitespace) {
    setup();
    
    nmo_guid_t guid = {0xAABBCCDD, 0xEEFFFFFF};
    nmo_status_t result = nmo_type_registry_register_enum_string(
        g_type_registry,
        guid,
        "WhitespaceEnum",
        "  A = 0 ,  B = 1 ,  C = 2  "
    );
    
    ASSERT_EQ(NMO_OK, result);
    
    const nmo_type_descriptor_t *desc = nmo_type_registry_find_by_guid(g_type_registry, guid);
    ASSERT_NE(NULL, desc);
    
    const nmo_specialized_metadata_t *meta = nmo_type_registry_get_metadata(g_type_registry, desc->id);
    ASSERT_EQ(3, meta->enum_meta.value_count);
    ASSERT_STR_EQ("A", meta->enum_meta.values[0].name);
    ASSERT_STR_EQ("B", meta->enum_meta.values[1].name);
    ASSERT_STR_EQ("C", meta->enum_meta.values[2].name);
    
    teardown();
}

/* ============================================================================
 * register_flags_string() Tests
 * ============================================================================ */

TEST(string_registration, register_flags_string_basic) {
    setup();
    
    nmo_guid_t guid = {0x98765432, 0x11223344};
    nmo_status_t result = nmo_type_registry_register_flags_string(
        g_type_registry,
        guid,
        "TestFlags",
        "READ=0x01, WRITE=0x02, EXECUTE=0x04"
    );
    
    ASSERT_EQ(NMO_OK, result);
    
    const nmo_type_descriptor_t *desc = nmo_type_registry_find_by_guid(g_type_registry, guid);
    ASSERT_NE(NULL, desc);
    ASSERT_STR_EQ("TestFlags", desc->name);
    ASSERT_EQ(NMO_TYPE_CATEGORY_FLAGS, desc->category);
    
    const nmo_specialized_metadata_t *meta = nmo_type_registry_get_metadata(g_type_registry, desc->id);
    ASSERT_NE(NULL, meta);
    ASSERT_EQ(NMO_METADATA_TYPE_FLAGS, meta->metadata_type);
    ASSERT_EQ(3, meta->flags_meta.bit_count);
    
    /* Check bits */
    ASSERT_STR_EQ("READ", meta->flags_meta.bits[0].name);
    ASSERT_EQ(0x01, meta->flags_meta.bits[0].mask);
    ASSERT_STR_EQ("WRITE", meta->flags_meta.bits[1].name);
    ASSERT_EQ(0x02, meta->flags_meta.bits[1].mask);
    ASSERT_STR_EQ("EXECUTE", meta->flags_meta.bits[2].name);
    ASSERT_EQ(0x04, meta->flags_meta.bits[2].mask);
    
    teardown();
}

TEST(string_registration, register_flags_string_powers_of_two) {
    setup();
    
    nmo_guid_t guid = {0x55555555, 0x66666666};
    nmo_status_t result = nmo_type_registry_register_flags_string(
        g_type_registry,
        guid,
        "PowerFlags",
        "BIT0=0x01, BIT1=0x02, BIT2=0x04, BIT3=0x08, BIT4=0x10"
    );
    
    ASSERT_EQ(NMO_OK, result);
    
    const nmo_type_descriptor_t *desc = nmo_type_registry_find_by_guid(g_type_registry, guid);
    ASSERT_NE(NULL, desc);
    
    const nmo_specialized_metadata_t *meta = nmo_type_registry_get_metadata(g_type_registry, desc->id);
    ASSERT_NE(NULL, meta);
    ASSERT_EQ(5, meta->flags_meta.bit_count);
    
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ((uint64_t)(1 << i), meta->flags_meta.bits[i].mask);
    }
    
    teardown();
}

TEST(string_registration, register_flags_string_auto_guid) {
    setup();
    
    nmo_guid_t null_guid = NMO_NULL_GUID;
    nmo_status_t result = nmo_type_registry_register_flags_string(
        g_type_registry,
        null_guid,
        "AutoFlags",
        "FLAG_A=0x01, FLAG_B=0x02"
    );
    
    ASSERT_EQ(NMO_OK, result);
    
    const nmo_type_descriptor_t *desc = nmo_type_registry_find_by_name(g_type_registry, "AutoFlags");
    ASSERT_NE(NULL, desc);
    ASSERT_EQ(false, nmo_guid_is_null(desc->guid));
    
    teardown();
}

TEST(string_registration, register_flags_string_null_params) {
    setup();
    
    nmo_guid_t guid = {0x1, 0x2};
    
    /* NULL registry */
    nmo_status_t result1 = nmo_type_registry_register_flags_string(NULL, guid, "Test", "A=0x01");
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result1);
    
    /* NULL name */
    nmo_status_t result2 = nmo_type_registry_register_flags_string(g_type_registry, guid, NULL, "A=0x01");
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result2);
    
    /* NULL data */
    nmo_status_t result3 = nmo_type_registry_register_flags_string(g_type_registry, guid, "Test", NULL);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result3);
    
    teardown();
}

TEST(string_registration, register_flags_string_invalid_mask) {
    setup();
    
    nmo_guid_t guid = {0x1, 0x2};
    /* 0x03 is not a power of 2 - should fail validation */
    nmo_status_t result = nmo_type_registry_register_flags_string(
        g_type_registry,
        guid,
        "InvalidFlags",
        "INVALID=0x03"
    );
    
    ASSERT_NE(NMO_OK, result);
    
    teardown();
}

/* ============================================================================
 * change_*_string() Tests
 * ============================================================================ */

TEST(string_registration, change_enum_string_not_found) {
    setup();
    
    nmo_guid_t guid = {0x1, 0x2};
    nmo_status_t result = nmo_type_registry_change_enum_string(
        g_type_registry,
        guid,
        "NEW=0"
    );

    /* Type does not exist yet */
    ASSERT_EQ(NMO_ERR_NOT_FOUND, result);
    
    teardown();
}

TEST(string_registration, change_enum_string_success) {
    setup();
    
    nmo_guid_t guid = {0x10, 0x20};
    nmo_status_t result = nmo_type_registry_register_enum_string(
        g_type_registry,
        guid,
        "MyEnum",
        "A=0,B=1"
    );
    ASSERT_EQ(NMO_OK, result);

    result = nmo_type_registry_change_enum_string(
        g_type_registry,
        guid,
        "A=0,B=1,C=2"
    );
    ASSERT_EQ(NMO_OK, result);

    /* Removing existing values must fail */
    result = nmo_type_registry_change_enum_string(
        g_type_registry,
        guid,
        "A=0"
    );
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
    
    teardown();
}

TEST(string_registration, change_flags_string_not_found) {
    setup();

    nmo_guid_t guid = {0x1, 0x2};
    nmo_status_t result = nmo_type_registry_change_flags_string(
        g_type_registry,
        guid,
        "NEW=0x01"
    );

    /* Type does not exist yet */
    ASSERT_EQ(NMO_ERR_NOT_FOUND, result);

    teardown();
}

TEST(string_registration, change_flags_string_success) {
    setup();

    nmo_guid_t guid = {0x11, 0x22};
    nmo_status_t result = nmo_type_registry_register_flags_string(
        g_type_registry,
        guid,
        "MyFlags",
        "READ=0x01,WRITE=0x02"
    );
    ASSERT_EQ(NMO_OK, result);

    result = nmo_type_registry_change_flags_string(
        g_type_registry,
        guid,
        "READ=0x01,WRITE=0x02,EXECUTE=0x04"
    );
    ASSERT_EQ(NMO_OK, result);

    /* Removing existing bits must fail */
    result = nmo_type_registry_change_flags_string(
        g_type_registry,
        guid,
        "READ=0x01"
    );
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);

    teardown();
}

/* ============================================================================
 * Test Runner
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(string_registration, register_enum_string_basic);
    REGISTER_TEST(string_registration, register_enum_string_with_hex_values);
    REGISTER_TEST(string_registration, register_enum_string_auto_guid);
    REGISTER_TEST(string_registration, register_enum_string_null_registry);
    REGISTER_TEST(string_registration, register_enum_string_null_name);
    REGISTER_TEST(string_registration, register_enum_string_null_data);
    REGISTER_TEST(string_registration, register_enum_string_invalid_syntax);
    REGISTER_TEST(string_registration, register_enum_string_with_whitespace);
    
    REGISTER_TEST(string_registration, register_flags_string_basic);
    REGISTER_TEST(string_registration, register_flags_string_powers_of_two);
    REGISTER_TEST(string_registration, register_flags_string_auto_guid);
    REGISTER_TEST(string_registration, register_flags_string_null_params);
    REGISTER_TEST(string_registration, register_flags_string_invalid_mask);
    
    REGISTER_TEST(string_registration, change_enum_string_not_found);
    REGISTER_TEST(string_registration, change_enum_string_success);
    REGISTER_TEST(string_registration, change_flags_string_not_found);
    REGISTER_TEST(string_registration, change_flags_string_success);
TEST_MAIN_END()
