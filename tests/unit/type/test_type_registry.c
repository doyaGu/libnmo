#include "type/nmo_type_system.h"
#include "test_framework.h"
#include "core/nmo_error.h"
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

/* Test GUIDs - Using Virtools compatible values (struct initialization for C17 compatibility) */
static const nmo_guid_t GUID_INT = {0x5a5716fd, 0x44e276d7};         // CKPGUID_INT from Virtools
static const nmo_guid_t GUID_FLOAT = {0x47884c3f, 0x432c2c20};       // CKPGUID_FLOAT
static const nmo_guid_t GUID_VECTOR3 = {0x48824eae, 0x2fe47960};     // CKPGUID_VECTOR
static const nmo_guid_t GUID_3DENTITY = {0x5b8a05d5, 0x31ea28d4};    // CKPGUID_3DENTITY
static const nmo_guid_t GUID_CHARACTER = {0x35985c64, 0x51af1372};   // CKPGUID_CHARACTER

static nmo_status_t dummy_manager_serialize(
    const void *instance,
    struct nmo_chunk *chunk,
    void *manager_context) {
    (void)instance;
    (void)chunk;
    (void)manager_context;
    NMO_RETURN_OK();
}

static nmo_status_t dummy_manager_deserialize(
    void *instance,
    struct nmo_chunk *chunk,
    void *manager_context) {
    (void)instance;
    (void)chunk;
    (void)manager_context;
    NMO_RETURN_OK();
}

/* ============================================================================
 * Test: Registry Creation
 * ============================================================================ */

TEST(type_registry, create_destroy) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    ASSERT_NE(NULL, registry->types.data);
    ASSERT_EQ(0, registry->types.count);
    ASSERT_GT(registry->types.capacity, 0);
    ASSERT_NE(NULL, registry->guid_map);
    ASSERT_NE(NULL, registry->name_map);
    ASSERT_FALSE(registry->derivation_masks_valid);
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Type Registration
 * ============================================================================ */

TEST(type_registry, register_simple_type) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    
    /* Create simple int type */
    nmo_type_descriptor_t int_type = {0};
    int_type.guid = GUID_INT;
    int_type.name = "int";
    int_type.category = NMO_TYPE_CATEGORY_SCALAR;
    int_type.size = 4;
    int_type.alignment = 4;
    
    nmo_status_t result = nmo_type_registry_register(registry, &int_type);
    ASSERT_EQ(NMO_OK, result);
    
    /* Verify registration */
    ASSERT_EQ(1, registry->types.count);
    
    const nmo_type_descriptor_t *found = nmo_type_registry_find_by_guid(registry, GUID_INT);
    ASSERT_NE(NULL, found);
    ASSERT_TRUE(nmo_guid_equals(found->guid, GUID_INT));
    ASSERT_STR_EQ("int", found->name);
    ASSERT_EQ(NMO_TYPE_CATEGORY_SCALAR, found->category);
    ASSERT_EQ(4, found->size);
    ASSERT_EQ(0, found->id);
    ASSERT_TRUE(found->valid);
    
    nmo_arena_destroy(arena);
}

TEST(type_registry, register_multiple_types) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    
    /* Register int */
    nmo_type_descriptor_t int_type = {0};
    int_type.guid = GUID_INT;
    int_type.name = "int";
    int_type.category = NMO_TYPE_CATEGORY_SCALAR;
    int_type.size = 4;
    int_type.alignment = 4;
    nmo_type_registry_register(registry, &int_type);
    
    /* Register float */
    nmo_type_descriptor_t float_type = {0};
    float_type.guid = GUID_FLOAT;
    float_type.name = "float";
    float_type.category = NMO_TYPE_CATEGORY_SCALAR;
    float_type.size = 4;
    float_type.alignment = 4;
    nmo_type_registry_register(registry, &float_type);
    
    /* Register Vector3 */
    nmo_type_descriptor_t vec3_type = {0};
    vec3_type.guid = GUID_VECTOR3;
    vec3_type.name = "Vector3";
    vec3_type.category = NMO_TYPE_CATEGORY_STRUCT;
    vec3_type.size = 12;
    vec3_type.alignment = 4;
    nmo_type_registry_register(registry, &vec3_type);
    
    ASSERT_EQ(3, registry->types.count);
    
    /* Verify all types can be found */
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, GUID_INT));
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, GUID_FLOAT));
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, GUID_VECTOR3));
    
    nmo_arena_destroy(arena);
}

TEST(type_registry, register_copies_name_and_fields) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);

    char name_buf[32];
    char desc_buf[32];
    char field_name_buf[32];
    snprintf(name_buf, sizeof(name_buf), "TempType");
    snprintf(desc_buf, sizeof(desc_buf), "TempDesc");
    snprintf(field_name_buf, sizeof(field_name_buf), "fieldA");

    int default_val = 42;
    nmo_type_field_t fields[1] = {0};
    fields[0].name = field_name_buf;
    fields[0].description = "FieldDesc";
    fields[0].type_guid = GUID_INT;
    fields[0].offset = 0;
    fields[0].size = sizeof(int);
    fields[0].flags = 0;
    fields[0].default_value = &default_val;

    nmo_type_descriptor_t type = {0};
    type.guid = GUID_FLOAT;
    type.name = name_buf;
    type.description = desc_buf;
    type.category = NMO_TYPE_CATEGORY_STRUCT;
    type.size = sizeof(int);
    type.alignment = alignof(int);
    type.fields = fields;
    type.field_count = 1;

    nmo_status_t result = nmo_type_registry_register(registry, &type);
    ASSERT_EQ(NMO_OK, result);

    name_buf[0] = 'X';
    desc_buf[0] = 'Y';
    field_name_buf[0] = 'Z';
    default_val = 7;

    const nmo_type_descriptor_t *found = nmo_type_registry_find_by_guid(registry, GUID_FLOAT);
    ASSERT_NE(NULL, found);
    ASSERT_STR_EQ("TempType", found->name);
    ASSERT_STR_EQ("TempDesc", found->description);
    ASSERT_EQ(1, found->field_count);
    ASSERT_STR_EQ("fieldA", found->fields[0].name);
    ASSERT_STR_EQ("FieldDesc", found->fields[0].description);
    ASSERT_EQ(42, *(const int *)found->fields[0].default_value);

    nmo_arena_destroy(arena);
}

TEST(type_registry, register_duplicate_guid_fails) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    
    nmo_type_descriptor_t type1 = {0};
    type1.guid = GUID_INT;
    type1.name = "int";
    type1.category = NMO_TYPE_CATEGORY_SCALAR;
    type1.size = 4;
    
    nmo_status_t result1 = nmo_type_registry_register(registry, &type1);
    ASSERT_EQ(NMO_OK, result1);
    
    /* Try to register same GUID again */
    nmo_type_descriptor_t type2 = {0};
    type2.guid = GUID_INT;
    type2.name = "int32";
    type2.category = NMO_TYPE_CATEGORY_SCALAR;
    type2.size = 4;
    
    nmo_status_t result2 = nmo_type_registry_register(registry, &type2);
    ASSERT_NE(NMO_OK, result2);
    
    /* Original type should still be there */
    const nmo_type_descriptor_t *found = nmo_type_registry_find_by_guid(registry, GUID_INT);
    ASSERT_NE(NULL, found);
    ASSERT_STR_EQ("int", found->name);
    
    nmo_arena_destroy(arena);
}

TEST(type_registry, compat_mask_supports_more_than_256_types) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024 * 1024);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);

    const size_t type_count = 300;
    nmo_guid_t prev_guid = (nmo_guid_t){0, 0};

    for (size_t i = 0; i < type_count; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Type%u", (unsigned int)i);

        nmo_type_descriptor_t type = {0};
        type.guid = (nmo_guid_t){0x60000000u + (uint32_t)i, 0x10000000u + (uint32_t)i};
        type.name = name;
        type.category = NMO_TYPE_CATEGORY_SCALAR;
        type.size = 4;
        type.alignment = 4;
        if (i > 0) {
            type.base_type = prev_guid;
        }

        nmo_status_t result = nmo_type_registry_register(registry, &type);
        ASSERT_EQ(NMO_OK, result);
        prev_guid = type.guid;
    }

    // Ensure inheritance across the old 256-bit boundary works.
    ASSERT_TRUE(nmo_type_is_derived_from(registry, (nmo_type_id_t)(type_count - 1), 0));
    ASSERT_TRUE(!nmo_type_is_derived_from(registry, 0, (nmo_type_id_t)(type_count - 1)));

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Type Lookup
 * ============================================================================ */

TEST(type_registry, find_by_guid) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    
    nmo_type_descriptor_t int_type = {0};
    int_type.guid = GUID_INT;
    int_type.name = "int";
    int_type.category = NMO_TYPE_CATEGORY_SCALAR;
    int_type.size = 4;
    nmo_type_registry_register(registry, &int_type);
    
    /* Find existing type */
    const nmo_type_descriptor_t *found = nmo_type_registry_find_by_guid(registry, GUID_INT);
    ASSERT_NE(NULL, found);
    ASSERT_TRUE(nmo_guid_equals(found->guid, GUID_INT));
    
    /* Find non-existing type */
    const nmo_type_descriptor_t *not_found = nmo_type_registry_find_by_guid(registry, GUID_FLOAT);
    ASSERT_EQ(NULL, not_found);
    
    nmo_arena_destroy(arena);
}

TEST(type_registry, find_by_name) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    
    nmo_type_descriptor_t int_type = {0};
    int_type.guid = GUID_INT;
    int_type.name = "int";
    int_type.category = NMO_TYPE_CATEGORY_SCALAR;
    int_type.size = 4;
    nmo_type_registry_register(registry, &int_type);
    
    /* Find existing type */
    const nmo_type_descriptor_t *found = nmo_type_registry_find_by_name(registry, "int");
    ASSERT_NE(NULL, found);
    ASSERT_STR_EQ("int", found->name);
    
    /* Find non-existing type */
    const nmo_type_descriptor_t *not_found = nmo_type_registry_find_by_name(registry, "float");
    ASSERT_EQ(NULL, not_found);
    
    nmo_arena_destroy(arena);
}

TEST(type_registry, get_by_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    
    nmo_type_descriptor_t int_type = {0};
    int_type.guid = GUID_INT;
    int_type.name = "int";
    int_type.category = NMO_TYPE_CATEGORY_SCALAR;
    int_type.size = 4;
    nmo_type_registry_register(registry, &int_type);
    
    /* Get by valid ID */
    const nmo_type_descriptor_t *found = nmo_type_registry_get_by_id(registry, 0);
    ASSERT_NE(NULL, found);
    ASSERT_TRUE(nmo_guid_equals(found->guid, GUID_INT));
    
    /* Get by invalid ID */
    ASSERT_EQ(NULL, nmo_type_registry_get_by_id(registry, 999));
    ASSERT_EQ(NULL, nmo_type_registry_get_by_id(registry, -1));
    
    nmo_arena_destroy(arena);
}

TEST(type_registry, find_by_class_id_direct) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);

    nmo_type_descriptor_t object_type = {0};
    object_type.guid = GUID_3DENTITY;
    object_type.name = "CK3dEntity";
    object_type.category = NMO_TYPE_CATEGORY_OBJECT_REF;
    object_type.size = 4;
    object_type.alignment = 4;
    object_type.class_id = 0x1001;
    object_type.valid = true;
    object_type.base_type = NMO_GUID_NULL;

    nmo_status_t result = nmo_type_registry_register(registry, &object_type);
    ASSERT_EQ(NMO_OK, result);

    const nmo_type_descriptor_t *found = nmo_type_registry_find_by_class_id(registry, 0x1001);
    ASSERT_NE(NULL, found);
    ASSERT_EQ(0x1001, found->class_id);

    const nmo_type_descriptor_t *inherited =
        nmo_type_registry_find_by_class_id_inherited(registry, 0x1001);
    ASSERT_NE(NULL, inherited);
    ASSERT_TRUE(nmo_guid_equals(found->guid, inherited->guid));

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

TEST(type_registry, unregister_removes_lookups) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);

    nmo_type_descriptor_t test_type = {0};
    test_type.guid = GUID_CHARACTER;
    test_type.name = "TestCharacter";
    test_type.category = NMO_TYPE_CATEGORY_STRUCT;
    test_type.size = 8;
    test_type.alignment = 4;
    test_type.class_id = 0x2002;
    test_type.valid = true;
    test_type.base_type = NMO_GUID_NULL;

    nmo_status_t result = nmo_type_registry_register(registry, &test_type);
    ASSERT_EQ(NMO_OK, result);

    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, GUID_CHARACTER));
    ASSERT_NE(NULL, nmo_type_registry_find_by_name(registry, "TestCharacter"));
    ASSERT_NE(NULL, nmo_type_registry_find_by_class_id(registry, 0x2002));

    result = nmo_type_registry_unregister(registry, GUID_CHARACTER);
    ASSERT_EQ(NMO_OK, result);

    ASSERT_EQ(NULL, nmo_type_registry_find_by_guid(registry, GUID_CHARACTER));
    ASSERT_EQ(NULL, nmo_type_registry_find_by_name(registry, "TestCharacter"));
    ASSERT_EQ(NULL, nmo_type_registry_find_by_class_id(registry, 0x2002));
    ASSERT_EQ(NMO_TYPE_ID_INVALID, nmo_type_registry_class_id_to_type_id(registry, 0x2002));

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

TEST(type_registry, unregister_removes_aliases) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);

    nmo_type_descriptor_t test_type = {0};
    test_type.guid = GUID_CHARACTER;
    test_type.name = "AliasType";
    test_type.category = NMO_TYPE_CATEGORY_STRUCT;
    test_type.size = 8;
    test_type.alignment = 4;

    nmo_status_t result = nmo_type_registry_register(registry, &test_type);
    ASSERT_EQ(NMO_OK, result);

    const nmo_type_descriptor_t *registered = nmo_type_registry_find_by_guid(registry, GUID_CHARACTER);
    ASSERT_NE(NULL, registered);

    result = nmo_type_registry_add_name_alias(registry, registered->id, "ALIAS");
    ASSERT_EQ(NMO_OK, result);

    ASSERT_NE(NULL, nmo_type_registry_find_by_name(registry, "ALIAS"));

    result = nmo_type_registry_unregister(registry, GUID_CHARACTER);
    ASSERT_EQ(NMO_OK, result);

    ASSERT_EQ(NULL, nmo_type_registry_find_by_name(registry, "ALIAS"));

    nmo_type_descriptor_t new_type = {0};
    new_type.guid = GUID_INT;
    new_type.name = "NewType";
    new_type.category = NMO_TYPE_CATEGORY_STRUCT;
    new_type.size = 4;
    new_type.alignment = 4;

    result = nmo_type_registry_register(registry, &new_type);
    ASSERT_EQ(NMO_OK, result);

    const nmo_type_descriptor_t *registered_new = nmo_type_registry_find_by_guid(registry, GUID_INT);
    ASSERT_NE(NULL, registered_new);

    result = nmo_type_registry_add_name_alias(registry, registered_new->id, "ALIAS");
    ASSERT_EQ(NMO_OK, result);

    ASSERT_NE(NULL, nmo_type_registry_find_by_name(registry, "ALIAS"));

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Slot Recycling
 * ============================================================================ */

TEST(type_registry, slot_recycling) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    
    /* Register type at slot 0 */
    nmo_type_descriptor_t int_type = {0};
    int_type.guid = GUID_INT;
    int_type.name = "int";
    int_type.category = NMO_TYPE_CATEGORY_SCALAR;
    int_type.size = 4;
    nmo_type_registry_register(registry, &int_type);
    
    const nmo_type_descriptor_t *type0 = nmo_type_registry_get_by_id(registry, 0);
    ASSERT_NE(NULL, type0);
    ASSERT_EQ(0, type0->id);
    
    /* Register type at slot 1 */
    nmo_type_descriptor_t float_type = {0};
    float_type.guid = GUID_FLOAT;
    float_type.name = "float";
    float_type.category = NMO_TYPE_CATEGORY_SCALAR;
    float_type.size = 4;
    nmo_type_registry_register(registry, &float_type);
    
    ASSERT_EQ(2, registry->types.count);
    
    /* Unregister slot 0 */
    nmo_type_registry_unregister(registry, GUID_INT);
    ASSERT_EQ(NULL, nmo_type_registry_find_by_guid(registry, GUID_INT));
    
    /* Register new type - should reuse slot 0 */
    nmo_type_descriptor_t vec3_type = {0};
    vec3_type.guid = GUID_VECTOR3;
    vec3_type.name = "Vector3";
    vec3_type.category = NMO_TYPE_CATEGORY_STRUCT;
    vec3_type.size = 12;
    nmo_type_registry_register(registry, &vec3_type);
    
    /* Verify slot 0 was reused */
    const nmo_type_descriptor_t *new_type = nmo_type_registry_find_by_guid(registry, GUID_VECTOR3);
    ASSERT_NE(NULL, new_type);
    ASSERT_EQ(0, new_type->id);  /* Should reuse slot 0 */
    
    /* Type count should be 2 (not 3) */
    ASSERT_EQ(2, registry->types.count);
    
    nmo_arena_destroy(arena);
}

TEST(type_registry, slot_recycling_clears_class_id_map) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);

    nmo_type_descriptor_t type_a = {0};
    type_a.guid = GUID_INT;
    type_a.name = "TypeA";
    type_a.category = NMO_TYPE_CATEGORY_OBJECT_REF;
    type_a.size = 4;
    type_a.alignment = 4;
    type_a.class_id = 0x1111;
    nmo_type_registry_register(registry, &type_a);

    nmo_type_registry_unregister(registry, GUID_INT);

    nmo_type_descriptor_t type_b = {0};
    type_b.guid = GUID_FLOAT;
    type_b.name = "TypeB";
    type_b.category = NMO_TYPE_CATEGORY_OBJECT_REF;
    type_b.size = 4;
    type_b.alignment = 4;
    type_b.class_id = 0x2222;
    nmo_type_registry_register(registry, &type_b);

    ASSERT_EQ(NULL, nmo_type_registry_find_by_class_id(registry, 0x1111));
    ASSERT_EQ(NMO_TYPE_ID_INVALID, nmo_type_registry_class_id_to_type_id(registry, 0x1111));
    ASSERT_NE(NULL, nmo_type_registry_find_by_class_id(registry, 0x2222));

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

TEST(type_registry, unregister_clears_type_manager_mapping) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);

    nmo_guid_t manager_guid = {0xABCDEF01, 0x12345678};
    nmo_status_t result = nmo_type_registry_register_saver_manager(
        registry,
        manager_guid,
        "DummyManager",
        dummy_manager_serialize,
        dummy_manager_deserialize,
        NULL);
    ASSERT_EQ(NMO_OK, result);

    nmo_type_descriptor_t type_a = {0};
    type_a.guid = GUID_INT;
    type_a.name = "ManagedType";
    type_a.category = NMO_TYPE_CATEGORY_STRUCT;
    type_a.size = 4;
    type_a.alignment = 4;
    result = nmo_type_registry_register(registry, &type_a);
    ASSERT_EQ(NMO_OK, result);

    result = nmo_type_registry_set_type_manager(registry, GUID_INT, manager_guid);
    ASSERT_EQ(NMO_OK, result);

    ASSERT_NE(NULL, nmo_type_registry_get_type_manager(registry, GUID_INT));

    result = nmo_type_registry_unregister(registry, GUID_INT);
    ASSERT_EQ(NMO_OK, result);

    nmo_type_descriptor_t type_b = {0};
    type_b.guid = GUID_FLOAT;
    type_b.name = "NewType";
    type_b.category = NMO_TYPE_CATEGORY_STRUCT;
    type_b.size = 4;
    type_b.alignment = 4;
    result = nmo_type_registry_register(registry, &type_b);
    ASSERT_EQ(NMO_OK, result);

    ASSERT_EQ(NULL, nmo_type_registry_get_type_manager(registry, GUID_FLOAT));

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Type Compatibility
 * ============================================================================ */

TEST(type_registry, compatibility_same_type) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    
    nmo_type_descriptor_t int_type = {0};
    int_type.guid = GUID_INT;
    int_type.name = "int";
    int_type.category = NMO_TYPE_CATEGORY_SCALAR;
    int_type.size = 4;
    nmo_type_registry_register(registry, &int_type);
    
    /* Same type is always compatible */
    ASSERT_TRUE(nmo_type_is_compatible(registry, 0, 0));
    
    nmo_arena_destroy(arena);
}

TEST(type_registry, compatibility_inheritance) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    
    /* Register base type: Entity */
    nmo_type_descriptor_t entity_type = {0};
    entity_type.guid = GUID_3DENTITY;
    entity_type.name = "Entity";
    entity_type.category = NMO_TYPE_CATEGORY_STRUCT;
    entity_type.size = 16;
    nmo_type_registry_register(registry, &entity_type);
    
    /* Register derived type: Character (derives from Entity) */
    nmo_type_descriptor_t char_type = {0};
    char_type.guid = GUID_CHARACTER;
    char_type.name = "Character";
    char_type.category = NMO_TYPE_CATEGORY_STRUCT;
    char_type.size = 32;
    char_type.base_type = GUID_3DENTITY;  /* Inheritance */
    nmo_type_registry_register(registry, &char_type);
    
    const nmo_type_descriptor_t *entity = nmo_type_registry_find_by_guid(registry, GUID_3DENTITY);
    const nmo_type_descriptor_t *character = nmo_type_registry_find_by_guid(registry, GUID_CHARACTER);
    
    ASSERT_NE(NULL, entity);
    ASSERT_NE(NULL, character);
    
    /* Character (derived) should be compatible with Entity (base) */
    ASSERT_TRUE(nmo_type_is_compatible(registry, character->id, entity->id));
    
    /* Entity IS compatible with Character (symmetric check in Virtools) */
    ASSERT_TRUE(nmo_type_is_compatible(registry, entity->id, character->id));
    
    nmo_arena_destroy(arena);
}

TEST(type_registry, derivation_depth) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    
    /* Entity <- Character */
    nmo_type_descriptor_t entity_type = {0};
    entity_type.guid = GUID_3DENTITY;
    entity_type.name = "Entity";
    entity_type.size = 16;
    nmo_type_registry_register(registry, &entity_type);
    
    nmo_type_descriptor_t char_type = {0};
    char_type.guid = GUID_CHARACTER;
    char_type.name = "Character";
    char_type.size = 32;
    char_type.base_type = GUID_3DENTITY;
    nmo_type_registry_register(registry, &char_type);
    
    const nmo_type_descriptor_t *entity = nmo_type_registry_find_by_guid(registry, GUID_3DENTITY);
    const nmo_type_descriptor_t *character = nmo_type_registry_find_by_guid(registry, GUID_CHARACTER);
    
    /* Character -> Entity depth should be 1 */
    int32_t depth = nmo_type_get_derivation_depth(registry, character->id, entity->id);
    ASSERT_EQ(1, depth);
    
    /* Entity -> Character should be -1 (not derived) */
    depth = nmo_type_get_derivation_depth(registry, entity->id, character->id);
    ASSERT_EQ(-1, depth);
    
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Statistics
 * ============================================================================ */

TEST(type_registry, statistics) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    
    /* Register builtin types */
    nmo_type_descriptor_t int_type = {0};
    int_type.guid = GUID_INT;
    int_type.name = "int";
    int_type.size = 4;
    nmo_type_registry_register(registry, &int_type);
    
    nmo_type_descriptor_t float_type = {0};
    float_type.guid = GUID_FLOAT;
    float_type.name = "float";
    float_type.size = 4;
    nmo_type_registry_register(registry, &float_type);
    
    size_t total = 0, builtin = 0, plugin = 0;
    nmo_type_registry_get_stats(registry, &total, &builtin, &plugin);
    
    ASSERT_EQ(2, total);
    ASSERT_EQ(2, builtin);
    ASSERT_EQ(0, plugin);
    
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test Runner
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(type_registry, create_destroy);
    REGISTER_TEST(type_registry, register_simple_type);
    REGISTER_TEST(type_registry, register_multiple_types);
    REGISTER_TEST(type_registry, register_copies_name_and_fields);
    REGISTER_TEST(type_registry, register_duplicate_guid_fails);
    REGISTER_TEST(type_registry, compat_mask_supports_more_than_256_types);
    REGISTER_TEST(type_registry, find_by_guid);
    REGISTER_TEST(type_registry, find_by_name);
    REGISTER_TEST(type_registry, get_by_id);
    REGISTER_TEST(type_registry, find_by_class_id_direct);
    REGISTER_TEST(type_registry, unregister_removes_lookups);
    REGISTER_TEST(type_registry, unregister_removes_aliases);
    REGISTER_TEST(type_registry, slot_recycling);
    REGISTER_TEST(type_registry, slot_recycling_clears_class_id_map);
    REGISTER_TEST(type_registry, unregister_clears_type_manager_mapping);
    REGISTER_TEST(type_registry, compatibility_same_type);
    REGISTER_TEST(type_registry, compatibility_inheritance);
    REGISTER_TEST(type_registry, derivation_depth);
    REGISTER_TEST(type_registry, statistics);
TEST_MAIN_END()
