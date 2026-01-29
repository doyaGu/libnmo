/**
 * @file test_enum_flags.c
 * @brief Unit tests for enum and flags type registration (Phase 6.2 Task 6.2.3)
 */

#include "test_framework.h"
#include "type/dynamic_types.h"
#include "type/type_system.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"
#include <string.h>

/* ============================================================================
 * Test Fixtures
 * ============================================================================ */

static nmo_arena_t *g_arena = NULL;
static nmo_type_registry_t *g_type_registry = NULL;

static void setup(void) {
    g_arena = nmo_arena_create(NULL, 1024 * 1024); /* 1 MB */
    ASSERT_NE(NULL, g_arena);
    
    g_type_registry = nmo_type_registry_create(g_arena);
    ASSERT_NE(NULL, g_type_registry);
}

static void teardown(void) {
    if (g_arena) {
        nmo_arena_destroy(g_arena);
        g_arena = NULL;
    }
    g_type_registry = NULL;
}

/* ============================================================================
 * Enum Registration Tests
 * ============================================================================ */

TEST(enum_flags, register_enum_success) {
    setup();
    
    /* Define enum values */
    nmo_enum_value_def_t values[] = {
        { "RED",   0, NULL },
        { "GREEN", 1, NULL },
        { "BLUE",  2, NULL }
    };
    
    /* Define enum type */
    nmo_enum_type_def_t enum_def = {
        .name = "Color",
        .values = values,
        .value_count = 3,
        .default_value = 0
    };
    
    /* Register enum */
    nmo_guid_t guid;
    nmo_result_t result = nmo_type_registry_register_enum(g_type_registry, &enum_def, &guid);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Lookup registered type */
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(g_type_registry, guid);
    ASSERT_NE(NULL, type_desc);
    ASSERT_EQ(NMO_TYPE_CATEGORY_ENUM, type_desc->category);
    ASSERT_STR_EQ("Color", type_desc->name);
    ASSERT_EQ(sizeof(int32_t), type_desc->size);
    
    teardown();
}

TEST(enum_flags, register_enum_null_params) {
    setup();
    
    nmo_enum_value_def_t values[] = {{ "A", 0, NULL }};
    nmo_enum_type_def_t enum_def = {
        .name = "TestEnum",
        .values = values,
        .value_count = 1,
        .default_value = 0
    };
    
    /* Test NULL registry */
    nmo_guid_t guid;
    nmo_result_t result = nmo_type_registry_register_enum(NULL, &enum_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    /* Test NULL enum_def */
    result = nmo_type_registry_register_enum(g_type_registry, NULL, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    teardown();
}

TEST(enum_flags, register_enum_empty_name) {
    setup();
    
    nmo_enum_value_def_t values[] = {{ "A", 0, NULL }};
    nmo_enum_type_def_t enum_def = {
        .name = "",  /* Empty name */
        .values = values,
        .value_count = 1,
        .default_value = 0
    };
    
    nmo_guid_t guid;
    nmo_result_t result = nmo_type_registry_register_enum(g_type_registry, &enum_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    teardown();
}

TEST(enum_flags, register_enum_no_values) {
    setup();
    
    nmo_enum_type_def_t enum_def = {
        .name = "EmptyEnum",
        .values = NULL,
        .value_count = 0,
        .default_value = 0
    };
    
    nmo_guid_t guid;
    nmo_result_t result = nmo_type_registry_register_enum(g_type_registry, &enum_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    teardown();
}

TEST(enum_flags, register_enum_duplicate_names) {
    setup();
    
    nmo_enum_value_def_t values[] = {
        { "SAME", 0, NULL },
        { "SAME", 1, NULL }  /* Duplicate name */
    };
    
    nmo_enum_type_def_t enum_def = {
        .name = "DupEnum",
        .values = values,
        .value_count = 2,
        .default_value = 0
    };
    
    nmo_guid_t guid;
    nmo_result_t result = nmo_type_registry_register_enum(g_type_registry, &enum_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    teardown();
}

TEST(enum_flags, register_enum_already_exists) {
    setup();
    
    nmo_enum_value_def_t values[] = {{ "VAL", 0, NULL }};
    nmo_enum_type_def_t enum_def = {
        .name = "MyEnum",
        .values = values,
        .value_count = 1,
        .default_value = 0
    };
    
    /* Register first time - should succeed */
    nmo_guid_t guid1;
    nmo_result_t result = nmo_type_registry_register_enum(g_type_registry, &enum_def, &guid1);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Register second time with same name - should fail */
    nmo_guid_t guid2;
    result = nmo_type_registry_register_enum(g_type_registry, &enum_def, &guid2);
    ASSERT_EQ(NMO_ERR_ALREADY_EXISTS, result.code);
    
    teardown();
}

TEST(enum_flags, register_enum_with_negative_values) {
    setup();
    
    nmo_enum_value_def_t values[] = {
        { "NEGATIVE", -1, NULL },
        { "ZERO",      0, NULL },
        { "POSITIVE",  1, NULL }
    };
    
    nmo_enum_type_def_t enum_def = {
        .name = "SignedEnum",
        .values = values,
        .value_count = 3,
        .default_value = 0
    };
    
    nmo_guid_t guid;
    nmo_result_t result = nmo_type_registry_register_enum(g_type_registry, &enum_def, &guid);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify type registered successfully */
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(g_type_registry, guid);
    ASSERT_NE(NULL, type_desc);
    ASSERT_EQ(NMO_TYPE_CATEGORY_ENUM, type_desc->category);
    
    teardown();
}

/* ============================================================================
 * Flags Registration Tests
 * ============================================================================ */

TEST(enum_flags, register_flags_success) {
    setup();
    
    /* Define flags bits */
    nmo_flags_bit_def_t bits[] = {
        { "READ",    0x01, NULL },  /* Bit 0 */
        { "WRITE",   0x02, NULL },  /* Bit 1 */
        { "EXECUTE", 0x04, NULL }   /* Bit 2 */
    };
    
    /* Define flags type */
    nmo_flags_type_def_t flags_def = {
        .name = "Permissions",
        .bits = bits,
        .bit_count = 3,
        .default_value = 0
    };
    
    /* Register flags */
    nmo_guid_t guid;
    nmo_result_t result = nmo_type_registry_register_flags(g_type_registry, &flags_def, &guid);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Lookup registered type */
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(g_type_registry, guid);
    ASSERT_NE(NULL, type_desc);
    ASSERT_EQ(NMO_TYPE_CATEGORY_FLAGS, type_desc->category);
    ASSERT_STR_EQ("Permissions", type_desc->name);
    ASSERT_EQ(sizeof(uint32_t), type_desc->size);
    
    teardown();
}

TEST(enum_flags, register_flags_null_params) {
    setup();
    
    nmo_flags_bit_def_t bits[] = {{ "BIT", 0x01, NULL }};
    nmo_flags_type_def_t flags_def = {
        .name = "TestFlags",
        .bits = bits,
        .bit_count = 1,
        .default_value = 0
    };
    
    /* Test NULL registry */
    nmo_guid_t guid;
    nmo_result_t result = nmo_type_registry_register_flags(NULL, &flags_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    /* Test NULL flags_def */
    result = nmo_type_registry_register_flags(g_type_registry, NULL, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    teardown();
}

TEST(enum_flags, register_flags_empty_name) {
    setup();
    
    nmo_flags_bit_def_t bits[] = {{ "BIT", 0x01, NULL }};
    nmo_flags_type_def_t flags_def = {
        .name = "",  /* Empty name */
        .bits = bits,
        .bit_count = 1,
        .default_value = 0
    };
    
    nmo_guid_t guid;
    nmo_result_t result = nmo_type_registry_register_flags(g_type_registry, &flags_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    teardown();
}

TEST(enum_flags, register_flags_no_bits) {
    setup();
    
    nmo_flags_type_def_t flags_def = {
        .name = "EmptyFlags",
        .bits = NULL,
        .bit_count = 0,
        .default_value = 0
    };
    
    nmo_guid_t guid;
    nmo_result_t result = nmo_type_registry_register_flags(g_type_registry, &flags_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    teardown();
}

TEST(enum_flags, register_flags_duplicate_names) {
    setup();
    
    nmo_flags_bit_def_t bits[] = {
        { "SAME", 0, NULL },
        { "SAME", 1, NULL }  /* Duplicate name */
    };
    
    nmo_flags_type_def_t flags_def = {
        .name = "DupFlags",
        .bits = bits,
        .bit_count = 2,
        .default_value = 0
    };
    
    nmo_guid_t guid;
    nmo_result_t result = nmo_type_registry_register_flags(g_type_registry, &flags_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    teardown();
}

TEST(enum_flags, register_flags_duplicate_masks) {
    setup();
    
    nmo_flags_bit_def_t bits[] = {
        { "BIT_A", 0x01, NULL },
        { "BIT_B", 0x01, NULL }  /* Duplicate mask */
    };
    
    nmo_flags_type_def_t flags_def = {
        .name = "DupMaskFlags",
        .bits = bits,
        .bit_count = 2,
        .default_value = 0
    };
    
    nmo_guid_t guid;
    nmo_result_t result = nmo_type_registry_register_flags(g_type_registry, &flags_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    teardown();
}

TEST(enum_flags, register_flags_invalid_mask) {
    setup();
    
    nmo_flags_bit_def_t bits[] = {
        { "INVALID", 0x03, NULL }  /* Not a power of 2 */
    };
    
    nmo_flags_type_def_t flags_def = {
        .name = "InvalidMaskFlags",
        .bits = bits,
        .bit_count = 1,
        .default_value = 0
    };
    
    nmo_guid_t guid;
    nmo_result_t result = nmo_type_registry_register_flags(g_type_registry, &flags_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    teardown();
}

TEST(enum_flags, register_flags_with_default_value) {
    setup();
    
    nmo_flags_bit_def_t bits[] = {
        { "FLAG_A", 0x01, NULL },  /* Bit 0 */
        { "FLAG_B", 0x02, NULL },  /* Bit 1 */
        { "FLAG_C", 0x04, NULL }   /* Bit 2 */
    };
    
    nmo_flags_type_def_t flags_def = {
        .name = "DefaultFlags",
        .bits = bits,
        .bit_count = 3,
        .default_value = 0x5  /* Bits 0 and 2 set */
    };
    
    nmo_guid_t guid;
    nmo_result_t result = nmo_type_registry_register_flags(g_type_registry, &flags_def, &guid);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify registration successful */
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(g_type_registry, guid);
    ASSERT_NE(NULL, type_desc);
    ASSERT_EQ(NMO_TYPE_CATEGORY_FLAGS, type_desc->category);
    
    teardown();
}

/* ============================================================================
 * Test Main
 * ============================================================================ */

TEST_MAIN_BEGIN()
    /* Enum tests */
    REGISTER_TEST(enum_flags, register_enum_success);
    REGISTER_TEST(enum_flags, register_enum_null_params);
    REGISTER_TEST(enum_flags, register_enum_empty_name);
    REGISTER_TEST(enum_flags, register_enum_no_values);
    REGISTER_TEST(enum_flags, register_enum_duplicate_names);
    REGISTER_TEST(enum_flags, register_enum_already_exists);
    REGISTER_TEST(enum_flags, register_enum_with_negative_values);
    
    /* Flags tests */
    REGISTER_TEST(enum_flags, register_flags_success);
    REGISTER_TEST(enum_flags, register_flags_null_params);
    REGISTER_TEST(enum_flags, register_flags_empty_name);
    REGISTER_TEST(enum_flags, register_flags_no_bits);
    REGISTER_TEST(enum_flags, register_flags_duplicate_names);
    REGISTER_TEST(enum_flags, register_flags_duplicate_masks);
    REGISTER_TEST(enum_flags, register_flags_invalid_mask);
    REGISTER_TEST(enum_flags, register_flags_with_default_value);
TEST_MAIN_END()
