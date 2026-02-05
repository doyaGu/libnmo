/**
 * @file test_struct.c
 * @brief Test suite for struct type registration (Phase 6.2 Task 6.2.4)
 */

#include "test_framework.h"
#include "type/nmo_dynamic_types.h"
#include "type/nmo_type_system.h"
#include "type/nmo_builtin_type_guids.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include <stdalign.h>
#include <string.h>

/* ============================================================================
 * Test Fixtures
 * ============================================================================ */

static nmo_arena_t *test_arena = NULL;
static nmo_type_registry_t *test_registry = NULL;

static void setup(void) {
    test_arena = nmo_arena_create(NULL, 1024 * 1024); /* 1MB */
    ASSERT_NE(NULL, test_arena);
    
    test_registry = nmo_type_registry_create(test_arena);
    ASSERT_NE(NULL, test_registry);
    
    /* Register basic builtin types manually for testing */
    /* Type descriptors for basic types (int, float, CKBYTE) */
    
    /* int type */
    nmo_type_descriptor_t *int_type = (nmo_type_descriptor_t*)
        nmo_arena_alloc(test_arena, sizeof(nmo_type_descriptor_t), alignof(nmo_type_descriptor_t));
    memset(int_type, 0, sizeof(nmo_type_descriptor_t));
    int_type->guid = NMO_TYPE_GUID_INT;
    int_type->name = "int";
    int_type->size = 4;
    int_type->alignment = 4;
    int_type->category = NMO_TYPE_CATEGORY_SCALAR;
    int_type->flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_POD;
    int_type->valid = true;
    nmo_status_t result = nmo_type_registry_register(test_registry, int_type);
    ASSERT_EQ(NMO_OK, result);
    
    /* float type */
    nmo_type_descriptor_t *float_type = (nmo_type_descriptor_t*)
        nmo_arena_alloc(test_arena, sizeof(nmo_type_descriptor_t), alignof(nmo_type_descriptor_t));
    memset(float_type, 0, sizeof(nmo_type_descriptor_t));
    float_type->guid = NMO_TYPE_GUID_FLOAT;
    float_type->name = "float";
    float_type->size = 4;
    float_type->alignment = 4;
    float_type->category = NMO_TYPE_CATEGORY_SCALAR;
    float_type->flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_POD;
    float_type->valid = true;
    result = nmo_type_registry_register(test_registry, float_type);
    ASSERT_EQ(NMO_OK, result);
    
    /* CKBYTE type (uint8_t) */
    nmo_type_descriptor_t *byte_type = (nmo_type_descriptor_t*)
        nmo_arena_alloc(test_arena, sizeof(nmo_type_descriptor_t), alignof(nmo_type_descriptor_t));
    memset(byte_type, 0, sizeof(nmo_type_descriptor_t));
    byte_type->guid = (nmo_guid_t){0x6FED1D00, 0x00000020};  /* Made-up GUID for CKBYTE */
    byte_type->name = "CKBYTE";
    byte_type->size = 1;
    byte_type->alignment = 1;
    byte_type->category = NMO_TYPE_CATEGORY_SCALAR;
    byte_type->flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_POD;
    byte_type->valid = true;
    result = nmo_type_registry_register(test_registry, byte_type);
    ASSERT_EQ(NMO_OK, result);
}

static void teardown(void) {
    if (test_registry) {
        nmo_type_registry_destroy(test_registry);
        test_registry = NULL;
    }
    if (test_arena) {
        nmo_arena_destroy(test_arena);
        test_arena = NULL;
    }
}

/* ============================================================================
 * Layout Calculation Tests
 * ============================================================================ */

TEST(struct_registration, calculate_layout_basic) {
    setup();
    
    /* Simple struct: { int x; float y; } */
    nmo_struct_field_def_t fields[] = {
        { .name = "x", .type_name = "int" },
        { .name = "y", .type_name = "float" }
    };
    
    uint32_t total_size, alignment;
    nmo_status_t result = nmo_type_calculate_layout(
        test_registry, fields, 2, 0, false, &total_size, &alignment);
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(8, total_size);  /* int(4) + float(4) */
    ASSERT_EQ(4, alignment);   /* max(4, 4) */
    
    teardown();
}

TEST(struct_registration, calculate_layout_with_padding) {
    setup();
    
    /* Struct with padding: { char a; int b; char c; } */
    nmo_struct_field_def_t fields[] = {
        { .name = "a", .type_name = "CKBYTE" },   /* 1 byte + 3 padding */
        { .name = "b", .type_name = "int" },      /* 4 bytes */
        { .name = "c", .type_name = "CKBYTE" }    /* 1 byte + 3 padding at end */
    };
    
    uint32_t total_size, alignment;
    nmo_status_t result = nmo_type_calculate_layout(
        test_registry, fields, 3, 0, false, &total_size, &alignment);
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(12, total_size);  /* 1 + 3 pad + 4 + 1 + 3 pad = 12 */
    ASSERT_EQ(4, alignment);    /* Aligned to int */
    
    teardown();
}

TEST(struct_registration, calculate_layout_packed) {
    setup();
    
    /* Packed struct: { char a; int b; char c; } */
    nmo_struct_field_def_t fields[] = {
        { .name = "a", .type_name = "CKBYTE" },
        { .name = "b", .type_name = "int" },
        { .name = "c", .type_name = "CKBYTE" }
    };
    
    uint32_t total_size, alignment;
    nmo_status_t result = nmo_type_calculate_layout(
        test_registry, fields, 3, 0, true, &total_size, &alignment);
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(6, total_size);  /* 1 + 4 + 1 = 6 (no padding) */
    ASSERT_EQ(1, alignment);   /* Packed = byte alignment */
    
    teardown();
}

TEST(struct_registration, calculate_layout_custom_alignment) {
    setup();
    
    /* Struct with custom 16-byte alignment */
    nmo_struct_field_def_t fields[] = {
        { .name = "x", .type_name = "int" },
        { .name = "y", .type_name = "int" }
    };
    
    uint32_t total_size, alignment;
    nmo_status_t result = nmo_type_calculate_layout(
        test_registry, fields, 2, 16, false, &total_size, &alignment);
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(16, total_size);  /* 8 bytes + 8 bytes padding to reach 16 */
    ASSERT_EQ(16, alignment);
    
    teardown();
}

TEST(struct_registration, calculate_layout_null_params) {
    setup();
    
    nmo_struct_field_def_t fields[] = {
        { .name = "x", .type_name = "int" }
    };
    
    uint32_t total_size, alignment;
    
    /* NULL registry */
    nmo_status_t result = nmo_type_calculate_layout(
        NULL, fields, 1, 0, false, &total_size, &alignment);
    ASSERT_NE(NMO_OK, result);
    
    /* NULL fields */
    result = nmo_type_calculate_layout(
        test_registry, NULL, 1, 0, false, &total_size, &alignment);
    ASSERT_NE(NMO_OK, result);
    
    /* Zero field count */
    result = nmo_type_calculate_layout(
        test_registry, fields, 0, 0, false, &total_size, &alignment);
    ASSERT_NE(NMO_OK, result);
    
    teardown();
}

TEST(struct_registration, calculate_layout_unknown_field_type) {
    setup();
    
    /* Field with non-existent type */
    nmo_struct_field_def_t fields[] = {
        { .name = "x", .type_name = "NonExistentType" }
    };
    
    uint32_t total_size, alignment;
    nmo_status_t result = nmo_type_calculate_layout(
        test_registry, fields, 1, 0, false, &total_size, &alignment);
    
    ASSERT_NE(NMO_OK, result);
    
    teardown();
}

/* ============================================================================
 * Struct Registration Tests
 * ============================================================================ */

TEST(struct_registration, register_struct_simple) {
    setup();
    
    /* Register Point2D { int x; int y; } */
    nmo_struct_field_def_t fields[] = {
        { .name = "x", .type_name = "int" },
        { .name = "y", .type_name = "int" }
    };
    
    nmo_struct_type_def_t struct_def = {
        .name = "Point2D",
        .description = "2D Point",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 2,
        .alignment = 0,
        .packed = false
    };
    
    nmo_guid_t guid;
    nmo_status_t result = nmo_type_registry_register_struct(test_registry, &struct_def, &guid);
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT(!nmo_guid_is_null(guid));
    
    /* Verify type is registered */
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(test_registry, guid);
    ASSERT_NE(NULL, type_desc);
    ASSERT_STR_EQ("Point2D", type_desc->name);
    ASSERT_EQ(8, type_desc->size);
    ASSERT_EQ(4, type_desc->alignment);
    ASSERT_EQ(NMO_TYPE_CATEGORY_STRUCT, type_desc->category);
    
    teardown();
}

TEST(struct_registration, register_struct_metadata_mapping) {
    setup();

    nmo_struct_field_def_t fields[] = {
        { .name = "x", .type_name = "int" },
        { .name = "y", .type_name = "float" }
    };

    nmo_struct_type_def_t struct_def = {
        .name = "Point2D",
        .description = "2D point",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 2,
        .alignment = 0,
        .packed = false
    };

    nmo_guid_t guid;
    nmo_status_t result = nmo_type_registry_register_struct(test_registry, &struct_def, &guid);
    ASSERT_EQ(NMO_OK, result);

    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(test_registry, guid);
    ASSERT_NE(NULL, type_desc);

    const nmo_specialized_metadata_t *meta =
        nmo_type_registry_get_metadata(test_registry, type_desc->id);
    ASSERT_NE(NULL, meta);
    ASSERT_EQ(NMO_METADATA_TYPE_STRUCT, meta->metadata_type);
    ASSERT_EQ(2, meta->struct_meta.field_count);
    ASSERT_STR_EQ("x", meta->struct_meta.fields[0].name);
    ASSERT_STR_EQ("y", meta->struct_meta.fields[1].name);

    teardown();
}

TEST(struct_registration, register_struct_with_custom_guid) {
    setup();
    
    nmo_guid_t custom_guid = { 0x12345678, 0x9ABCDEF0 };
    
    nmo_struct_field_def_t fields[] = {
        { .name = "value", .type_name = "int" }
    };
    
    nmo_struct_type_def_t struct_def = {
        .name = "CustomGuidStruct",
        .guid = custom_guid,
        .fields = fields,
        .field_count = 1,
        .alignment = 0,
        .packed = false
    };
    
    nmo_guid_t out_guid;
    nmo_status_t result = nmo_type_registry_register_struct(test_registry, &struct_def, &out_guid);
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT(nmo_guid_equals(custom_guid, out_guid));
    
    teardown();
}

TEST(struct_registration, register_struct_packed) {
    setup();
    
    /* Packed struct: { char a; int b; char c; } = 6 bytes */
    nmo_struct_field_def_t fields[] = {
        { .name = "a", .type_name = "CKBYTE" },
        { .name = "b", .type_name = "int" },
        { .name = "c", .type_name = "CKBYTE" }
    };
    
    nmo_struct_type_def_t struct_def = {
        .name = "PackedStruct",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 3,
        .alignment = 0,
        .packed = true
    };
    
    nmo_guid_t guid;
    nmo_status_t result = nmo_type_registry_register_struct(test_registry, &struct_def, &guid);
    
    ASSERT_EQ(NMO_OK, result);
    
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(test_registry, guid);
    ASSERT_NE(NULL, type_desc);
    ASSERT_EQ(6, type_desc->size);  /* No padding */
    ASSERT_EQ(1, type_desc->alignment);
    
    teardown();
}

TEST(struct_registration, register_struct_null_params) {
    setup();
    
    nmo_struct_field_def_t fields[] = {
        { .name = "x", .type_name = "int" }
    };
    
    nmo_struct_type_def_t struct_def = {
        .name = "TestStruct",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 1
    };
    
    nmo_guid_t guid;
    
    /* NULL registry */
    nmo_status_t result = nmo_type_registry_register_struct(NULL, &struct_def, &guid);
    ASSERT_NE(NMO_OK, result);
    
    /* NULL struct_def */
    result = nmo_type_registry_register_struct(test_registry, NULL, &guid);
    ASSERT_NE(NMO_OK, result);
    
    teardown();
}

TEST(struct_registration, register_struct_empty_name) {
    setup();
    
    nmo_struct_field_def_t fields[] = {
        { .name = "x", .type_name = "int" }
    };
    
    nmo_struct_type_def_t struct_def = {
        .name = "",  /* Empty name */
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 1
    };
    
    nmo_guid_t guid;
    nmo_status_t result = nmo_type_registry_register_struct(test_registry, &struct_def, &guid);
    
    ASSERT_NE(NMO_OK, result);
    
    teardown();
}

TEST(struct_registration, register_struct_no_fields) {
    setup();
    
    nmo_struct_type_def_t struct_def = {
        .name = "EmptyStruct",
        .guid = NMO_NULL_GUID,
        .fields = NULL,
        .field_count = 0
    };
    
    nmo_guid_t guid;
    nmo_status_t result = nmo_type_registry_register_struct(test_registry, &struct_def, &guid);
    
    ASSERT_NE(NMO_OK, result);
    
    teardown();
}

TEST(struct_registration, register_struct_already_exists) {
    setup();
    
    nmo_struct_field_def_t fields[] = {
        { .name = "x", .type_name = "int" }
    };
    
    nmo_struct_type_def_t struct_def = {
        .name = "DuplicateStruct",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 1
    };
    
    nmo_guid_t guid1, guid2;
    
    /* First registration */
    nmo_status_t result = nmo_type_registry_register_struct(test_registry, &struct_def, &guid1);
    ASSERT_EQ(NMO_OK, result);
    
    /* Second registration (should fail) */
    result = nmo_type_registry_register_struct(test_registry, &struct_def, &guid2);
    ASSERT_NE(NMO_OK, result);
    
    teardown();
}

TEST(struct_registration, register_struct_unknown_field_type) {
    setup();
    
    nmo_struct_field_def_t fields[] = {
        { .name = "x", .type_name = "UnknownType" }
    };
    
    nmo_struct_type_def_t struct_def = {
        .name = "InvalidFieldStruct",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 1
    };
    
    nmo_guid_t guid;
    nmo_status_t result = nmo_type_registry_register_struct(test_registry, &struct_def, &guid);
    
    ASSERT_NE(NMO_OK, result);
    
    teardown();
}

TEST(struct_registration, register_struct_with_description) {
    setup();
    
    nmo_struct_field_def_t fields[] = {
        {
            .name = "radius",
            .type_name = "float",
            .description = "Circle radius in world units"
        }
    };
    
    nmo_struct_type_def_t struct_def = {
        .name = "Circle",
        .description = "2D Circle definition",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 1
    };
    
    nmo_guid_t guid;
    nmo_status_t result = nmo_type_registry_register_struct(test_registry, &struct_def, &guid);
    
    ASSERT_EQ(NMO_OK, result);
    
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(test_registry, guid);
    ASSERT_NE(NULL, type_desc);
    ASSERT_STR_EQ("2D Circle definition", type_desc->description);
    
    teardown();
}

/* ============================================================================
 * Size and Alignment Query Tests
 * ============================================================================ */

TEST(struct_registration, get_type_size) {
    setup();
    
    nmo_struct_field_def_t fields[] = {
        { .name = "x", .type_name = "int" },
        { .name = "y", .type_name = "int" },
        { .name = "z", .type_name = "int" }
    };
    
    nmo_struct_type_def_t struct_def = {
        .name = "Point3D",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 3
    };
    
    nmo_guid_t guid;
    nmo_status_t result = nmo_type_registry_register_struct(test_registry, &struct_def, &guid);
    ASSERT_EQ(NMO_OK, result);
    
    uint32_t size = nmo_type_get_size(test_registry, guid);
    ASSERT_EQ(12, size);  /* 3 * sizeof(int) */
    
    teardown();
}

TEST(struct_registration, get_type_alignment) {
    setup();
    
    nmo_struct_field_def_t fields[] = {
        { .name = "a", .type_name = "CKBYTE" },
        { .name = "b", .type_name = "int" }
    };
    
    nmo_struct_type_def_t struct_def = {
        .name = "MixedStruct",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 2
    };
    
    nmo_guid_t guid;
    nmo_status_t result = nmo_type_registry_register_struct(test_registry, &struct_def, &guid);
    ASSERT_EQ(NMO_OK, result);
    
    uint32_t alignment = nmo_type_get_alignment(test_registry, guid);
    ASSERT_EQ(4, alignment);  /* Aligned to int */
    
    teardown();
}

TEST(struct_registration, get_size_alignment_invalid) {
    setup();
    
    nmo_guid_t invalid_guid = { 0xFFFFFFFF, 0xFFFFFFFF };
    
    uint32_t size = nmo_type_get_size(test_registry, invalid_guid);
    ASSERT_EQ(0, size);
    
    uint32_t alignment = nmo_type_get_alignment(test_registry, invalid_guid);
    ASSERT_EQ(1, alignment);
    
    teardown();
}

/* ============================================================================
 * String-Based Struct Registration Tests (Phase 6.2.3)
 * ============================================================================ */

TEST(struct_string_registration, register_struct_string_basic) {
    setup();
    
    /* Register Vector3 struct: { float, float, float } */
    const char *field_types[] = { "float", "float", "float" };
    nmo_guid_t struct_guid = { 0x12345678, 0x87654321 };
    
    nmo_status_t result = nmo_type_registry_register_struct_string(
        test_registry,
        struct_guid,
        "Vector3",
        field_types,
        3
    );
    
    ASSERT_EQ(NMO_OK, result);
    
    /* Verify the struct was registered */
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(
        test_registry, struct_guid);
    ASSERT_NE(NULL, type_desc);
    ASSERT_STR_EQ("Vector3", type_desc->name);
    ASSERT_EQ(12, type_desc->size);  /* 3 * sizeof(float) */
    ASSERT_EQ(4, type_desc->alignment);  /* alignof(float) */
    ASSERT_EQ(NMO_TYPE_CATEGORY_STRUCT, type_desc->category);
    
    teardown();
}

TEST(struct_string_registration, register_struct_string_auto_guid) {
    setup();
    
    /* Register with NULL_GUID to auto-generate */
    const char *field_types[] = { "int", "int" };
    
    nmo_status_t result = nmo_type_registry_register_struct_string(
        test_registry,
        NMO_NULL_GUID,
        "Point2D",
        field_types,
        2
    );
    
    ASSERT_EQ(NMO_OK, result);
    
    /* Verify struct was registered with auto-generated GUID */
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_name(
        test_registry, "Point2D");
    ASSERT_NE(NULL, type_desc);
    ASSERT_STR_EQ("Point2D", type_desc->name);
    ASSERT_FALSE(nmo_guid_is_null(type_desc->guid));
    
    teardown();
}

TEST(struct_string_registration, register_struct_string_multiple_fields) {
    setup();
    
    /* Register struct with different field types */
    const char *field_types[] = { "int", "float", "CKBYTE", "int" };
    
    nmo_status_t result = nmo_type_registry_register_struct_string(
        test_registry,
        NMO_NULL_GUID,
        "MixedStruct",
        field_types,
        4
    );
    
    ASSERT_EQ(NMO_OK, result);
    
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_name(
        test_registry, "MixedStruct");
    ASSERT_NE(NULL, type_desc);
    /* Layout: int(4) + float(4) + CKBYTE(1) + padding(3) + int(4) = 16 bytes */
    ASSERT_EQ(16, type_desc->size);
    ASSERT_EQ(4, type_desc->alignment);
    
    teardown();
}

TEST(struct_string_registration, register_struct_string_null_registry) {
    /* Don't call setup - test NULL registry */
    
    const char *field_types[] = { "int" };
    
    nmo_status_t result = nmo_type_registry_register_struct_string(
        NULL,
        NMO_NULL_GUID,
        "TestStruct",
        field_types,
        1
    );
    
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
}

TEST(struct_string_registration, register_struct_string_null_name) {
    setup();
    
    const char *field_types[] = { "int" };
    
    nmo_status_t result = nmo_type_registry_register_struct_string(
        test_registry,
        NMO_NULL_GUID,
        NULL,
        field_types,
        1
    );
    
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
    
    teardown();
}

TEST(struct_string_registration, register_struct_string_null_field_types) {
    setup();
    
    nmo_status_t result = nmo_type_registry_register_struct_string(
        test_registry,
        NMO_NULL_GUID,
        "TestStruct",
        NULL,
        1
    );
    
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
    
    teardown();
}

TEST(struct_string_registration, register_struct_string_zero_fields) {
    setup();
    
    const char *field_types[] = { "int" };
    
    nmo_status_t result = nmo_type_registry_register_struct_string(
        test_registry,
        NMO_NULL_GUID,
        "EmptyStruct",
        field_types,
        0
    );
    
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
    
    teardown();
}

TEST(struct_string_registration, register_struct_string_invalid_field_type) {
    setup();
    
    /* Try to register struct with non-existent field type */
    const char *field_types[] = { "int", "NonExistentType", "float" };
    
    nmo_status_t result = nmo_type_registry_register_struct_string(
        test_registry,
        NMO_NULL_GUID,
        "BadStruct",
        field_types,
        3
    );
    
    ASSERT_EQ(NMO_ERR_NOT_FOUND, result);
    
    teardown();
}

TEST(struct_string_registration, register_struct_string_already_exists) {
    setup();
    
    const char *field_types[] = { "int", "int" };
    nmo_guid_t struct_guid = { 0xAABBCCDD, 0xEEFF0011 };
    
    /* Register first time */
    nmo_status_t result = nmo_type_registry_register_struct_string(
        test_registry,
        struct_guid,
        "DuplicateStruct",
        field_types,
        2
    );
    ASSERT_EQ(NMO_OK, result);
    
    /* Try to register again with same GUID */
    result = nmo_type_registry_register_struct_string(
        test_registry,
        struct_guid,
        "DuplicateStruct2",
        field_types,
        2
    );
    
    ASSERT_EQ(NMO_ERR_ALREADY_EXISTS, result);
    
    teardown();
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

TEST_MAIN_BEGIN()
    /* Layout calculation tests */
    REGISTER_TEST(struct_registration, calculate_layout_basic);
    REGISTER_TEST(struct_registration, calculate_layout_with_padding);
    REGISTER_TEST(struct_registration, calculate_layout_packed);
    REGISTER_TEST(struct_registration, calculate_layout_custom_alignment);
    REGISTER_TEST(struct_registration, calculate_layout_null_params);
    REGISTER_TEST(struct_registration, calculate_layout_unknown_field_type);
    
    /* Struct registration tests */
    REGISTER_TEST(struct_registration, register_struct_simple);
    REGISTER_TEST(struct_registration, register_struct_metadata_mapping);
    REGISTER_TEST(struct_registration, register_struct_with_custom_guid);
    REGISTER_TEST(struct_registration, register_struct_packed);
    REGISTER_TEST(struct_registration, register_struct_null_params);
    REGISTER_TEST(struct_registration, register_struct_empty_name);
    REGISTER_TEST(struct_registration, register_struct_no_fields);
    REGISTER_TEST(struct_registration, register_struct_already_exists);
    REGISTER_TEST(struct_registration, register_struct_unknown_field_type);
    REGISTER_TEST(struct_registration, register_struct_with_description);
    
    /* Size and alignment query tests */
    REGISTER_TEST(struct_registration, get_type_size);
    REGISTER_TEST(struct_registration, get_type_alignment);
    REGISTER_TEST(struct_registration, get_size_alignment_invalid);
    
    /* String-based struct registration tests (Phase 6.2.3) */
    REGISTER_TEST(struct_string_registration, register_struct_string_basic);
    REGISTER_TEST(struct_string_registration, register_struct_string_auto_guid);
    REGISTER_TEST(struct_string_registration, register_struct_string_multiple_fields);
    REGISTER_TEST(struct_string_registration, register_struct_string_null_registry);
    REGISTER_TEST(struct_string_registration, register_struct_string_null_name);
    REGISTER_TEST(struct_string_registration, register_struct_string_null_field_types);
    REGISTER_TEST(struct_string_registration, register_struct_string_zero_fields);
    REGISTER_TEST(struct_string_registration, register_struct_string_invalid_field_type);
    REGISTER_TEST(struct_string_registration, register_struct_string_already_exists);
TEST_MAIN_END()
