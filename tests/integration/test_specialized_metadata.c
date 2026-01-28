/**
 * @file test_specialized_metadata.c
 * @brief Integration tests for specialized metadata functionality
 * 
 * Tests enum, struct, and flags metadata registration and retrieval.
 * Validates specialized metadata arrays (enums, structs, flags) work correctly.
 * 
 * Reference: CKParameterManager.cpp lines 298-304
 * Phase 5.6 - Task T5.6.6
 */

#include "type/type_system.h"
#include "test_framework.h"
#include <string.h>

/* Test type GUIDs */
static const nmo_guid_t GUID_ENUM_COLOR = {0xE1E1E1E1, 0xC0C0C0C0};
static const nmo_guid_t GUID_STRUCT_POINT = {0xE2E2E2E2, 0xC1C1C1C1};
static const nmo_guid_t GUID_FLAGS_PERMS = {0xE3E3E3E3, 0xC2C2C2C2};
static const nmo_guid_t GUID_INT = {0x5a5716fd, 0x44e276d7};

/* ============================================================================
 * Test: Enum Metadata Registration and Retrieval
 * ============================================================================ */

TEST(specialized_metadata, enum_registration) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register enum type */
    nmo_type_descriptor_t enum_type = {0};
    enum_type.guid = GUID_ENUM_COLOR;
    enum_type.name = "ColorEnum";
    enum_type.category = NMO_TYPE_CATEGORY_ENUM;
    enum_type.size = 4;
    enum_type.alignment = 4;
    enum_type.valid = true;
    
    nmo_result_t result = nmo_type_registry_register(registry, &enum_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Get registered type to obtain type_id */
    const nmo_type_descriptor_t *registered_type = 
        nmo_type_registry_find_by_guid(registry, GUID_ENUM_COLOR);
    ASSERT_NE(NULL, registered_type);
    nmo_type_id_t type_id = registered_type->id;
    
    /* Create enum metadata */
    nmo_enum_descriptor_t enum_values[] = {
        {.name = "Red", .value = 0, .description = "Red color", .flags = 0},
        {.name = "Green", .value = 1, .description = "Green color", .flags = 0},
        {.name = "Blue", .value = 2, .description = "Blue color", .flags = 0},
        {.name = "Alpha", .value = 3, .description = "Alpha channel", .flags = 0}
    };
    
    nmo_specialized_metadata_t metadata = {0};
    metadata.type_id = type_id;
    metadata.metadata_type = NMO_METADATA_TYPE_ENUM;
    metadata.enum_meta.values = enum_values;
    metadata.enum_meta.value_count = 4;
    
    /* Register metadata */
    result = nmo_type_registry_register_metadata(registry, &metadata);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Retrieve metadata */
    const nmo_specialized_metadata_t *retrieved = 
        nmo_type_registry_get_metadata(registry, type_id);
    ASSERT_NE(NULL, retrieved);
    ASSERT_EQ(NMO_METADATA_TYPE_ENUM, retrieved->metadata_type);
    ASSERT_EQ(4, retrieved->enum_meta.value_count);
    ASSERT_NE(NULL, retrieved->enum_meta.values);
    
    /* Verify enum values */
    ASSERT_STR_EQ("Red", retrieved->enum_meta.values[0].name);
    ASSERT_EQ(0, retrieved->enum_meta.values[0].value);
    ASSERT_STR_EQ("Blue", retrieved->enum_meta.values[2].name);
    ASSERT_EQ(2, retrieved->enum_meta.values[2].value);
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Struct Metadata Registration and Retrieval
 * ============================================================================ */

TEST(specialized_metadata, struct_registration) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register struct type */
    nmo_type_descriptor_t struct_type = {0};
    struct_type.guid = GUID_STRUCT_POINT;
    struct_type.name = "Point2D";
    struct_type.category = NMO_TYPE_CATEGORY_STRUCT;
    struct_type.size = 8;
    struct_type.alignment = 4;
    struct_type.valid = true;
    
    nmo_result_t result = nmo_type_registry_register(registry, &struct_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    const nmo_type_descriptor_t *registered_type = 
        nmo_type_registry_find_by_guid(registry, GUID_STRUCT_POINT);
    ASSERT_NE(NULL, registered_type);
    nmo_type_id_t type_id = registered_type->id;
    
    /* Create struct metadata */
    nmo_struct_descriptor_t fields[] = {
        {
            .name = "x",
            .type_guid = GUID_INT,
            .offset = 0,
            .size = 4,
            .array_count = 0,
            .flags = 0,
            .description = "X coordinate"
        },
        {
            .name = "y",
            .type_guid = GUID_INT,
            .offset = 4,
            .size = 4,
            .array_count = 0,
            .flags = 0,
            .description = "Y coordinate"
        }
    };
    
    nmo_specialized_metadata_t metadata = {0};
    metadata.type_id = type_id;
    metadata.metadata_type = NMO_METADATA_TYPE_STRUCT;
    metadata.struct_meta.fields = fields;
    metadata.struct_meta.field_count = 2;
    
    /* Register metadata */
    result = nmo_type_registry_register_metadata(registry, &metadata);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Retrieve metadata */
    const nmo_specialized_metadata_t *retrieved = 
        nmo_type_registry_get_metadata(registry, type_id);
    ASSERT_NE(NULL, retrieved);
    ASSERT_EQ(NMO_METADATA_TYPE_STRUCT, retrieved->metadata_type);
    ASSERT_EQ(2, retrieved->struct_meta.field_count);
    ASSERT_NE(NULL, retrieved->struct_meta.fields);
    
    /* Verify struct fields */
    ASSERT_STR_EQ("x", retrieved->struct_meta.fields[0].name);
    ASSERT_EQ(0, retrieved->struct_meta.fields[0].offset);
    ASSERT_EQ(4, retrieved->struct_meta.fields[0].size);
    ASSERT_STR_EQ("y", retrieved->struct_meta.fields[1].name);
    ASSERT_EQ(4, retrieved->struct_meta.fields[1].offset);
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Flags Metadata Registration and Retrieval
 * ============================================================================ */

TEST(specialized_metadata, flags_registration) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register flags type */
    nmo_type_descriptor_t flags_type = {0};
    flags_type.guid = GUID_FLAGS_PERMS;
    flags_type.name = "Permissions";
    flags_type.category = NMO_TYPE_CATEGORY_FLAGS;
    flags_type.size = 4;
    flags_type.alignment = 4;
    flags_type.valid = true;
    
    nmo_result_t result = nmo_type_registry_register(registry, &flags_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    const nmo_type_descriptor_t *registered_type = 
        nmo_type_registry_find_by_guid(registry, GUID_FLAGS_PERMS);
    ASSERT_NE(NULL, registered_type);
    nmo_type_id_t type_id = registered_type->id;
    
    /* Create flags metadata */
    nmo_flags_descriptor_t bits[] = {
        {.name = "Read", .mask = 0x01, .description = "Read permission", .flags = 0},
        {.name = "Write", .mask = 0x02, .description = "Write permission", .flags = 0},
        {.name = "Execute", .mask = 0x04, .description = "Execute permission", .flags = 0},
        {.name = "Delete", .mask = 0x08, .description = "Delete permission", .flags = 0}
    };
    
    nmo_specialized_metadata_t metadata = {0};
    metadata.type_id = type_id;
    metadata.metadata_type = NMO_METADATA_TYPE_FLAGS;
    metadata.flags_meta.bits = bits;
    metadata.flags_meta.bit_count = 4;
    
    /* Register metadata */
    result = nmo_type_registry_register_metadata(registry, &metadata);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Retrieve metadata */
    const nmo_specialized_metadata_t *retrieved = 
        nmo_type_registry_get_metadata(registry, type_id);
    ASSERT_NE(NULL, retrieved);
    ASSERT_EQ(NMO_METADATA_TYPE_FLAGS, retrieved->metadata_type);
    ASSERT_EQ(4, retrieved->flags_meta.bit_count);
    ASSERT_NE(NULL, retrieved->flags_meta.bits);
    
    /* Verify flags bits */
    ASSERT_STR_EQ("Read", retrieved->flags_meta.bits[0].name);
    ASSERT_EQ(0x01, retrieved->flags_meta.bits[0].mask);
    ASSERT_STR_EQ("Execute", retrieved->flags_meta.bits[2].name);
    ASSERT_EQ(0x04, retrieved->flags_meta.bits[2].mask);
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Multiple Metadata Types
 * ============================================================================ */

TEST(specialized_metadata, multiple_types) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register all three types */
    nmo_type_descriptor_t types[] = {
        {
            .guid = GUID_ENUM_COLOR,
            .name = "Color",
            .category = NMO_TYPE_CATEGORY_ENUM,
            .size = 4,
            .alignment = 4,
            .valid = true
        },
        {
            .guid = GUID_STRUCT_POINT,
            .name = "Point",
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .size = 8,
            .alignment = 4,
            .valid = true
        },
        {
            .guid = GUID_FLAGS_PERMS,
            .name = "Flags",
            .category = NMO_TYPE_CATEGORY_FLAGS,
            .size = 4,
            .alignment = 4,
            .valid = true
        }
    };
    
    nmo_type_id_t type_ids[3];
    for (int i = 0; i < 3; i++) {
        nmo_result_t result = nmo_type_registry_register(registry, &types[i]);
        ASSERT_EQ(NMO_OK, result.code);
        
        const nmo_type_descriptor_t *reg = 
            nmo_type_registry_find_by_guid(registry, types[i].guid);
        ASSERT_NE(NULL, reg);
        type_ids[i] = reg->id;
    }
    
    /* Register metadata for each */
    nmo_enum_descriptor_t enum_vals[] = {
        {.name = "Val1", .value = 0, .description = NULL, .flags = 0}
    };
    nmo_struct_descriptor_t struct_fields[] = {
        {.name = "field1", .type_guid = GUID_INT, .offset = 0, .size = 4, 
         .array_count = 0, .flags = 0, .description = NULL}
    };
    nmo_flags_descriptor_t flag_bits[] = {
        {.name = "Bit1", .mask = 0x01, .description = NULL, .flags = 0}
    };
    
    nmo_specialized_metadata_t metadatas[] = {
        {
            .type_id = type_ids[0],
            .metadata_type = NMO_METADATA_TYPE_ENUM,
            .enum_meta = {.values = enum_vals, .value_count = 1}
        },
        {
            .type_id = type_ids[1],
            .metadata_type = NMO_METADATA_TYPE_STRUCT,
            .struct_meta = {.fields = struct_fields, .field_count = 1}
        },
        {
            .type_id = type_ids[2],
            .metadata_type = NMO_METADATA_TYPE_FLAGS,
            .flags_meta = {.bits = flag_bits, .bit_count = 1}
        }
    };
    
    for (int i = 0; i < 3; i++) {
        nmo_result_t result = nmo_type_registry_register_metadata(registry, &metadatas[i]);
        ASSERT_EQ(NMO_OK, result.code);
    }
    
    /* Verify all metadata is retrievable */
    for (int i = 0; i < 3; i++) {
        const nmo_specialized_metadata_t *meta = 
            nmo_type_registry_get_metadata(registry, type_ids[i]);
        ASSERT_NE(NULL, meta);
        ASSERT_EQ(metadatas[i].metadata_type, meta->metadata_type);
    }
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Metadata Unregistration
 * ============================================================================ */

TEST(specialized_metadata, unregistration) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register type and metadata */
    nmo_type_descriptor_t enum_type = {0};
    enum_type.guid = GUID_ENUM_COLOR;
    enum_type.name = "TestEnum";
    enum_type.category = NMO_TYPE_CATEGORY_ENUM;
    enum_type.size = 4;
    enum_type.alignment = 4;
    enum_type.valid = true;
    
    nmo_result_t result = nmo_type_registry_register(registry, &enum_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    const nmo_type_descriptor_t *reg_type = 
        nmo_type_registry_find_by_guid(registry, GUID_ENUM_COLOR);
    ASSERT_NE(NULL, reg_type);
    nmo_type_id_t type_id = reg_type->id;
    
    nmo_enum_descriptor_t vals[] = {
        {.name = "Val", .value = 0, .description = NULL, .flags = 0}
    };
    
    nmo_specialized_metadata_t metadata = {0};
    metadata.type_id = type_id;
    metadata.metadata_type = NMO_METADATA_TYPE_ENUM;
    metadata.enum_meta.values = vals;
    metadata.enum_meta.value_count = 1;
    
    result = nmo_type_registry_register_metadata(registry, &metadata);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify metadata exists */
    ASSERT_NE(NULL, nmo_type_registry_get_metadata(registry, type_id));
    
    /* Unregister metadata */
    nmo_type_registry_unregister_metadata(registry, type_id);
    
    /* Verify metadata is removed */
    ASSERT_EQ(NULL, nmo_type_registry_get_metadata(registry, type_id));
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Type Descriptor's specialized_index Field
 * ============================================================================ */

TEST(specialized_metadata, specialized_index_field) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register enum type */
    nmo_type_descriptor_t enum_type = {0};
    enum_type.guid = GUID_ENUM_COLOR;
    enum_type.name = "TestEnum";
    enum_type.category = NMO_TYPE_CATEGORY_ENUM;
    enum_type.size = 4;
    enum_type.alignment = 4;
    enum_type.valid = true;
    
    nmo_result_t result = nmo_type_registry_register(registry, &enum_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    const nmo_type_descriptor_t *reg_type = 
        nmo_type_registry_find_by_guid(registry, GUID_ENUM_COLOR);
    ASSERT_NE(NULL, reg_type);
    nmo_type_id_t type_id = reg_type->id;
    
    /* Initially specialized_index should be 0 */
    ASSERT_EQ(0, reg_type->specialized_index);
    
    /* Register metadata */
    nmo_enum_descriptor_t vals[] = {
        {.name = "Val", .value = 0, .description = NULL, .flags = 0}
    };
    
    nmo_specialized_metadata_t metadata = {0};
    metadata.type_id = type_id;
    metadata.metadata_type = NMO_METADATA_TYPE_ENUM;
    metadata.enum_meta.values = vals;
    metadata.enum_meta.value_count = 1;
    
    result = nmo_type_registry_register_metadata(registry, &metadata);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Check that specialized_index is now set */
    reg_type = nmo_type_registry_find_by_guid(registry, GUID_ENUM_COLOR);
    ASSERT_NE(NULL, reg_type);
    ASSERT_GT(reg_type->specialized_index, 0);  /* Should be non-zero now */
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Invalid Arguments
 * ============================================================================ */

TEST(specialized_metadata, invalid_arguments) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    nmo_specialized_metadata_t metadata = {0};
    metadata.type_id = 0;
    metadata.metadata_type = NMO_METADATA_TYPE_ENUM;
    
    /* NULL registry */
    nmo_result_t result = nmo_type_registry_register_metadata(NULL, &metadata);
    ASSERT_NE(NMO_OK, result.code);
    
    /* NULL metadata */
    result = nmo_type_registry_register_metadata(registry, NULL);
    ASSERT_NE(NMO_OK, result.code);
    
    /* NULL registry for get */
    const nmo_specialized_metadata_t *retrieved = 
        nmo_type_registry_get_metadata(NULL, 0);
    ASSERT_EQ(NULL, retrieved);
    
    /* Non-existent type_id */
    retrieved = nmo_type_registry_get_metadata(registry, 9999);
    ASSERT_EQ(NULL, retrieved);
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test Main
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(specialized_metadata, enum_registration);
    REGISTER_TEST(specialized_metadata, struct_registration);
    REGISTER_TEST(specialized_metadata, flags_registration);
    REGISTER_TEST(specialized_metadata, multiple_types);
    REGISTER_TEST(specialized_metadata, unregistration);
    REGISTER_TEST(specialized_metadata, specialized_index_field);
    REGISTER_TEST(specialized_metadata, invalid_arguments);
TEST_MAIN_END()
