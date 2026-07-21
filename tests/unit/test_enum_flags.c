/**
 * @file test_enum_flags.c
 * @brief Unit tests for enum and flags type registration (Phase 6.2 Task 6.2.3)
 */

#include "test_framework.h"
#include "type/nmo_dynamic_types.h"
#include "type/nmo_type_system.h"
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
    if (g_type_registry) {
        nmo_type_registry_destroy(g_type_registry);
        g_type_registry = NULL;
    }
    if (g_arena) {
        nmo_arena_destroy(g_arena);
        g_arena = NULL;
    }
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
    nmo_status_t result = nmo_type_registry_register_enum(g_type_registry, &enum_def, &guid);
    ASSERT_EQ(NMO_OK, result);
    
    /* Lookup registered type */
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(g_type_registry, guid);
    ASSERT_NE(NULL, type_desc);
    ASSERT_EQ(NMO_TYPE_CATEGORY_ENUM, type_desc->category);
    ASSERT_STR_EQ("Color", type_desc->name);
    ASSERT_EQ(sizeof(int32_t), type_desc->size);
    
    teardown();
}

TEST(enum_flags, register_enum_metadata_mapping) {
    setup();

    nmo_enum_value_def_t values1[] = {
        { "A", 0, NULL },
        { "B", 1, NULL }
    };
    nmo_enum_type_def_t enum_def1 = {
        .name = "EnumOne",
        .values = values1,
        .value_count = 2,
        .default_value = 0
    };

    nmo_enum_value_def_t values2[] = {
        { "X", 10, NULL },
        { "Y", 20, NULL }
    };
    nmo_enum_type_def_t enum_def2 = {
        .name = "EnumTwo",
        .values = values2,
        .value_count = 2,
        .default_value = 10
    };

    nmo_guid_t guid1;
    nmo_guid_t guid2;
    nmo_status_t result = nmo_type_registry_register_enum(g_type_registry, &enum_def1, &guid1);
    ASSERT_EQ(NMO_OK, result);
    result = nmo_type_registry_register_enum(g_type_registry, &enum_def2, &guid2);
    ASSERT_EQ(NMO_OK, result);

    const nmo_type_descriptor_t *type1 = nmo_type_registry_find_by_guid(g_type_registry, guid1);
    const nmo_type_descriptor_t *type2 = nmo_type_registry_find_by_guid(g_type_registry, guid2);
    ASSERT_NE(NULL, type1);
    ASSERT_NE(NULL, type2);

    const nmo_specialized_metadata_t *meta1 =
        nmo_type_registry_get_metadata(g_type_registry, type1->id);
    const nmo_specialized_metadata_t *meta2 =
        nmo_type_registry_get_metadata(g_type_registry, type2->id);
    ASSERT_NE(NULL, meta1);
    ASSERT_NE(NULL, meta2);

    ASSERT_EQ(NMO_METADATA_TYPE_ENUM, meta1->metadata_type);
    ASSERT_EQ(NMO_METADATA_TYPE_ENUM, meta2->metadata_type);
    ASSERT_EQ(2, meta1->enum_meta.value_count);
    ASSERT_EQ(2, meta2->enum_meta.value_count);
    ASSERT_STR_EQ("A", meta1->enum_meta.values[0].name);
    ASSERT_STR_EQ("X", meta2->enum_meta.values[0].name);

    teardown();
}

TEST(enum_flags, unregister_enum_clears_metadata_mapping) {
    setup();

    nmo_enum_value_def_t values[] = {
        { "ONE", 1, NULL }
    };
    nmo_enum_type_def_t enum_def = {
        .name = "TempEnum",
        .values = values,
        .value_count = 1,
        .default_value = 1
    };

    nmo_guid_t guid;
    nmo_status_t result = nmo_type_registry_register_enum(g_type_registry, &enum_def, &guid);
    ASSERT_EQ(NMO_OK, result);

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(g_type_registry, guid);
    ASSERT_NE(NULL, type);
    nmo_type_id_t type_id = type->id;

    ASSERT_NE(NULL, nmo_type_registry_get_metadata(g_type_registry, type_id));

    result = nmo_type_registry_unregister(g_type_registry, guid);
    ASSERT_EQ(NMO_OK, result);

    ASSERT_EQ(NULL, nmo_type_registry_get_metadata(g_type_registry, type_id));

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
    nmo_status_t result = nmo_type_registry_register_enum(NULL, &enum_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
    
    /* Test NULL enum_def */
    result = nmo_type_registry_register_enum(g_type_registry, NULL, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
    
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
    nmo_status_t result = nmo_type_registry_register_enum(g_type_registry, &enum_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
    
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
    nmo_status_t result = nmo_type_registry_register_enum(g_type_registry, &enum_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
    
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
    nmo_status_t result = nmo_type_registry_register_enum(g_type_registry, &enum_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
    
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
    nmo_status_t result = nmo_type_registry_register_enum(g_type_registry, &enum_def, &guid1);
    ASSERT_EQ(NMO_OK, result);
    
    /* Register second time with same name - should fail */
    nmo_guid_t guid2;
    result = nmo_type_registry_register_enum(g_type_registry, &enum_def, &guid2);
    ASSERT_EQ(NMO_ERR_ALREADY_EXISTS, result);
    
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
    nmo_status_t result = nmo_type_registry_register_enum(g_type_registry, &enum_def, &guid);
    ASSERT_EQ(NMO_OK, result);
    
    /* Verify type registered successfully */
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(g_type_registry, guid);
    ASSERT_NE(NULL, type_desc);
    ASSERT_EQ(NMO_TYPE_CATEGORY_ENUM, type_desc->category);
    
    teardown();
}

TEST(enum_flags, change_enum_string_add_value_success) {
    setup();

    nmo_enum_value_def_t values[] = {
        { "RED", 0, NULL },
        { "GREEN", 1, NULL }
    };
    nmo_enum_type_def_t enum_def = {
        .name = "Color",
        .values = values,
        .value_count = 2,
        .default_value = 0
    };

    nmo_guid_t guid;
    nmo_status_t result = nmo_type_registry_register_enum(g_type_registry, &enum_def, &guid);
    ASSERT_EQ(NMO_OK, result);

    result = nmo_type_registry_change_enum_string(g_type_registry, guid, "RED=0,GREEN=1,BLUE=2");
    ASSERT_EQ(NMO_OK, result);

    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(g_type_registry, guid);
    ASSERT_NE(NULL, type_desc);

    const nmo_specialized_metadata_t *meta =
        nmo_type_registry_get_metadata(g_type_registry, type_desc->id);
    ASSERT_NE(NULL, meta);
    ASSERT_EQ(NMO_METADATA_TYPE_ENUM, meta->metadata_type);
    ASSERT_EQ(3, meta->enum_meta.value_count);

    bool found_blue = false;
    for (size_t i = 0; i < meta->enum_meta.value_count; i++) {
        if (strcmp(meta->enum_meta.values[i].name, "BLUE") == 0) {
            found_blue = true;
            ASSERT_EQ(2, meta->enum_meta.values[i].value);
        }
    }
    ASSERT_TRUE(found_blue);

    teardown();
}

TEST(enum_flags, change_enum_string_rejects_incompatible_changes) {
    setup();

    nmo_enum_value_def_t values[] = {
        { "A", 0, NULL },
        { "B", 1, NULL }
    };
    nmo_enum_type_def_t enum_def = {
        .name = "MyEnum",
        .values = values,
        .value_count = 2,
        .default_value = 0
    };

    nmo_guid_t guid;
    nmo_status_t result = nmo_type_registry_register_enum(g_type_registry, &enum_def, &guid);
    ASSERT_EQ(NMO_OK, result);

    /* Cannot remove existing value */
    result = nmo_type_registry_change_enum_string(g_type_registry, guid, "A=0");
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);

    /* Cannot change existing value's numeric mapping */
    result = nmo_type_registry_change_enum_string(g_type_registry, guid, "A=0,B=2");
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);

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
    nmo_status_t result = nmo_type_registry_register_flags(g_type_registry, &flags_def, &guid);
    ASSERT_EQ(NMO_OK, result);
    
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
    nmo_status_t result = nmo_type_registry_register_flags(NULL, &flags_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
    
    /* Test NULL flags_def */
    result = nmo_type_registry_register_flags(g_type_registry, NULL, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
    
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
    nmo_status_t result = nmo_type_registry_register_flags(g_type_registry, &flags_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
    
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
    nmo_status_t result = nmo_type_registry_register_flags(g_type_registry, &flags_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
    
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
    nmo_status_t result = nmo_type_registry_register_flags(g_type_registry, &flags_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
    
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
    nmo_status_t result = nmo_type_registry_register_flags(g_type_registry, &flags_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
    
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
    nmo_status_t result = nmo_type_registry_register_flags(g_type_registry, &flags_def, &guid);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
    
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
    nmo_status_t result = nmo_type_registry_register_flags(g_type_registry, &flags_def, &guid);
    ASSERT_EQ(NMO_OK, result);
    
    /* Verify registration successful */
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(g_type_registry, guid);
    ASSERT_NE(NULL, type_desc);
    ASSERT_EQ(NMO_TYPE_CATEGORY_FLAGS, type_desc->category);
    
    teardown();
}

TEST(enum_flags, change_flags_string_add_bit_success) {
    setup();

    nmo_flags_bit_def_t bits[] = {
        { "READ", 0x01, NULL },
        { "WRITE", 0x02, NULL }
    };
    nmo_flags_type_def_t flags_def = {
        .name = "Permissions",
        .bits = bits,
        .bit_count = 2,
        .default_value = 0
    };

    nmo_guid_t guid;
    nmo_status_t result = nmo_type_registry_register_flags(g_type_registry, &flags_def, &guid);
    ASSERT_EQ(NMO_OK, result);

    result = nmo_type_registry_change_flags_string(g_type_registry, guid, "READ=0x01,WRITE=0x02,EXECUTE=0x04");
    ASSERT_EQ(NMO_OK, result);

    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(g_type_registry, guid);
    ASSERT_NE(NULL, type_desc);

    const nmo_specialized_metadata_t *meta =
        nmo_type_registry_get_metadata(g_type_registry, type_desc->id);
    ASSERT_NE(NULL, meta);
    ASSERT_EQ(NMO_METADATA_TYPE_FLAGS, meta->metadata_type);
    ASSERT_EQ(3, meta->flags_meta.bit_count);

    bool found_execute = false;
    for (size_t i = 0; i < meta->flags_meta.bit_count; i++) {
        if (strcmp(meta->flags_meta.bits[i].name, "EXECUTE") == 0) {
            found_execute = true;
            ASSERT_EQ(0x04, meta->flags_meta.bits[i].mask);
        }
    }
    ASSERT_TRUE(found_execute);

    teardown();
}

TEST(enum_flags, change_flags_string_rejects_incompatible_changes) {
    setup();

    nmo_flags_bit_def_t bits[] = {
        { "BIT_A", 0x01, NULL },
        { "BIT_B", 0x02, NULL }
    };
    nmo_flags_type_def_t flags_def = {
        .name = "MyFlags",
        .bits = bits,
        .bit_count = 2,
        .default_value = 0
    };

    nmo_guid_t guid;
    nmo_status_t result = nmo_type_registry_register_flags(g_type_registry, &flags_def, &guid);
    ASSERT_EQ(NMO_OK, result);

    /* Cannot remove existing bit */
    result = nmo_type_registry_change_flags_string(g_type_registry, guid, "BIT_A=0x01");
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);

    /* Cannot change existing bit's mask */
    result = nmo_type_registry_change_flags_string(g_type_registry, guid, "BIT_A=0x01,BIT_B=0x04");
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);

    teardown();
}

/* ============================================================================
 * Test Main
 * ============================================================================ */

TEST_MAIN_BEGIN()
    /* Enum tests */
    REGISTER_TEST(enum_flags, register_enum_success);
    REGISTER_TEST(enum_flags, register_enum_metadata_mapping);
    REGISTER_TEST(enum_flags, unregister_enum_clears_metadata_mapping);
    REGISTER_TEST(enum_flags, register_enum_null_params);
    REGISTER_TEST(enum_flags, register_enum_empty_name);
    REGISTER_TEST(enum_flags, register_enum_no_values);
    REGISTER_TEST(enum_flags, register_enum_duplicate_names);
    REGISTER_TEST(enum_flags, register_enum_already_exists);
    REGISTER_TEST(enum_flags, register_enum_with_negative_values);
    REGISTER_TEST(enum_flags, change_enum_string_add_value_success);
    REGISTER_TEST(enum_flags, change_enum_string_rejects_incompatible_changes);
    
    /* Flags tests */
    REGISTER_TEST(enum_flags, register_flags_success);
    REGISTER_TEST(enum_flags, register_flags_null_params);
    REGISTER_TEST(enum_flags, register_flags_empty_name);
    REGISTER_TEST(enum_flags, register_flags_no_bits);
    REGISTER_TEST(enum_flags, register_flags_duplicate_names);
    REGISTER_TEST(enum_flags, register_flags_duplicate_masks);
    REGISTER_TEST(enum_flags, register_flags_invalid_mask);
    REGISTER_TEST(enum_flags, register_flags_with_default_value);
    REGISTER_TEST(enum_flags, change_flags_string_add_bit_success);
    REGISTER_TEST(enum_flags, change_flags_string_rejects_incompatible_changes);
TEST_MAIN_END()
