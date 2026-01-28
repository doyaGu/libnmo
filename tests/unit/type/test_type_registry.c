#include "type/type_system.h"
#include "object/nmo_param_meta.h"  // For CKPGUID macro
#include "test_framework.h"
#include <string.h>

/* Test GUIDs - Using Virtools compatible values (struct initialization for C17 compatibility) */
static const nmo_guid_t GUID_INT = {0x5a5716fd, 0x44e276d7};         // CKPGUID_INT from Virtools
static const nmo_guid_t GUID_FLOAT = {0x47884c3f, 0x432c2c20};       // CKPGUID_FLOAT
static const nmo_guid_t GUID_VECTOR3 = {0x48824eae, 0x2fe47960};     // CKPGUID_VECTOR
static const nmo_guid_t GUID_3DENTITY = {0x5b8a05d5, 0x31ea28d4};    // CKPGUID_3DENTITY
static const nmo_guid_t GUID_CHARACTER = {0x35985c64, 0x51af1372};   // CKPGUID_CHARACTER

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
    
    nmo_result_t result = nmo_type_registry_register(registry, &int_type);
    ASSERT_EQ(NMO_OK, result.code);
    
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

TEST(type_registry, register_duplicate_guid_fails) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    
    nmo_type_descriptor_t type1 = {0};
    type1.guid = GUID_INT;
    type1.name = "int";
    type1.category = NMO_TYPE_CATEGORY_SCALAR;
    type1.size = 4;
    
    nmo_result_t result1 = nmo_type_registry_register(registry, &type1);
    ASSERT_EQ(NMO_OK, result1.code);
    
    /* Try to register same GUID again */
    nmo_type_descriptor_t type2 = {0};
    type2.guid = GUID_INT;
    type2.name = "int32";
    type2.category = NMO_TYPE_CATEGORY_SCALAR;
    type2.size = 4;
    
    nmo_result_t result2 = nmo_type_registry_register(registry, &type2);
    ASSERT_NE(NMO_OK, result2.code);
    
    /* Original type should still be there */
    const nmo_type_descriptor_t *found = nmo_type_registry_find_by_guid(registry, GUID_INT);
    ASSERT_NE(NULL, found);
    ASSERT_STR_EQ("int", found->name);
    
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
    int_type.creator_plugin = NULL;  /* Builtin */
    nmo_type_registry_register(registry, &int_type);
    
    nmo_type_descriptor_t float_type = {0};
    float_type.guid = GUID_FLOAT;
    float_type.name = "float";
    float_type.size = 4;
    float_type.creator_plugin = NULL;  /* Builtin */
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
    REGISTER_TEST(type_registry, register_duplicate_guid_fails);
    REGISTER_TEST(type_registry, find_by_guid);
    REGISTER_TEST(type_registry, find_by_name);
    REGISTER_TEST(type_registry, get_by_id);
    REGISTER_TEST(type_registry, slot_recycling);
    REGISTER_TEST(type_registry, compatibility_same_type);
    REGISTER_TEST(type_registry, compatibility_inheritance);
    REGISTER_TEST(type_registry, derivation_depth);
    REGISTER_TEST(type_registry, statistics);
TEST_MAIN_END()
