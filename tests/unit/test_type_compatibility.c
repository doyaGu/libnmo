/**
 * @file test_type_compatibility.c
 * @brief Unit tests for type compatibility and conversion API (Phase 6.3)
 */

#include "test_framework.h"
#include "type/nmo_type_system.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"

/* ============================================================================
 * Test Fixtures
 * ============================================================================ */

static nmo_arena_t *arena = NULL;
static nmo_type_registry_t *registry = NULL;

/* Test type GUIDs */
static nmo_guid_t guid_base = {0x10000001, 0x00000000};
static nmo_guid_t guid_derived1 = {0x10000002, 0x00000000};
static nmo_guid_t guid_derived2 = {0x10000003, 0x00000000};
static nmo_guid_t guid_unrelated = {0x10000004, 0x00000000};

static nmo_status_t dummy_type_serialize(
    const void *instance,
    struct nmo_chunk *chunk,
    const nmo_type_descriptor_t *type,
    void *context
) {
    (void)instance;
    (void)chunk;
    (void)type;
    (void)context;
    NMO_RETURN_OK();
}

static nmo_status_t dummy_type_deserialize(
    void *instance,
    struct nmo_chunk *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)chunk;
    (void)type;
    (void)context;
    NMO_RETURN_OK();
}

static nmo_type_vtable_t dummy_type_vtable = {
    .serialize = dummy_type_serialize,
    .deserialize = dummy_type_deserialize
};

/* Test type IDs */
static nmo_type_id_t id_base = NMO_TYPE_ID_INVALID;
static nmo_type_id_t id_derived1 = NMO_TYPE_ID_INVALID;
static nmo_type_id_t id_derived2 = NMO_TYPE_ID_INVALID;
static nmo_type_id_t id_unrelated = NMO_TYPE_ID_INVALID;

static void setup(void) {
    arena = nmo_arena_create(NULL, 8192);
    ASSERT_NE(NULL, arena);
    
    registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register base type (no parent) */
    nmo_type_descriptor_t base_type = {0};
    base_type.guid = guid_base;
    base_type.name = "BaseType";
    base_type.size = 4;
    base_type.alignment = 4;
    base_type.category = NMO_TYPE_CATEGORY_SCALAR;
    base_type.valid = true;
    base_type.base_type = NMO_GUID_NULL;
    
    nmo_status_t res = nmo_type_registry_register(registry, &base_type);
    ASSERT_EQ(NMO_OK, res);
    id_base = nmo_type_registry_guid_to_type_id(registry, guid_base);
    ASSERT_NE(NMO_TYPE_ID_INVALID, id_base);
    
    /* Register derived1 (parent = base) */
    nmo_type_descriptor_t derived1_type = {0};
    derived1_type.guid = guid_derived1;
    derived1_type.name = "Derived1Type";
    derived1_type.size = 8;
    derived1_type.alignment = 4;
    derived1_type.category = NMO_TYPE_CATEGORY_SCALAR;
    derived1_type.valid = true;
    derived1_type.base_type = guid_base;
    
    res = nmo_type_registry_register(registry, &derived1_type);
    ASSERT_EQ(NMO_OK, res);
    id_derived1 = nmo_type_registry_guid_to_type_id(registry, guid_derived1);
    ASSERT_NE(NMO_TYPE_ID_INVALID, id_derived1);
    
    /* Register derived2 (parent = derived1, grandparent = base) */
    nmo_type_descriptor_t derived2_type = {0};
    derived2_type.guid = guid_derived2;
    derived2_type.name = "Derived2Type";
    derived2_type.size = 12;
    derived2_type.alignment = 4;
    derived2_type.category = NMO_TYPE_CATEGORY_SCALAR;
    derived2_type.valid = true;
    derived2_type.base_type = guid_derived1;
    
    res = nmo_type_registry_register(registry, &derived2_type);
    ASSERT_EQ(NMO_OK, res);
    id_derived2 = nmo_type_registry_guid_to_type_id(registry, guid_derived2);
    ASSERT_NE(NMO_TYPE_ID_INVALID, id_derived2);
    
    /* Register unrelated type (no parent) */
    nmo_type_descriptor_t unrelated_type = {0};
    unrelated_type.guid = guid_unrelated;
    unrelated_type.name = "UnrelatedType";
    unrelated_type.size = 4;
    unrelated_type.alignment = 4;
    unrelated_type.category = NMO_TYPE_CATEGORY_SCALAR;
    unrelated_type.valid = true;
    unrelated_type.base_type = NMO_GUID_NULL;
    
    res = nmo_type_registry_register(registry, &unrelated_type);
    ASSERT_EQ(NMO_OK, res);
    id_unrelated = nmo_type_registry_guid_to_type_id(registry, guid_unrelated);
    ASSERT_NE(NMO_TYPE_ID_INVALID, id_unrelated);
    
    /* Update derivation masks */
    nmo_type_registry_update_derivation_masks(registry);
}

static void teardown(void) {
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
    registry = NULL;
    arena = NULL;
}

/* ============================================================================
 * Task 6.3.1: Inheritance Checking Tests
 * ============================================================================ */

TEST(type_compatibility, is_derived_from_same_type) {
    setup();
    
    /* Same type is trivially derived from itself */
    ASSERT_TRUE(nmo_type_is_derived_from(registry, id_base, id_base));
    ASSERT_TRUE(nmo_type_is_derived_from(registry, id_derived1, id_derived1));
    
    teardown();
}

TEST(type_compatibility, is_derived_from_direct_parent) {
    setup();
    
    /* Derived1 is derived from Base */
    ASSERT_TRUE(nmo_type_is_derived_from(registry, id_derived1, id_base));
    
    /* But not vice versa */
    ASSERT_FALSE(nmo_type_is_derived_from(registry, id_base, id_derived1));
    
    teardown();
}

TEST(type_compatibility, is_derived_from_grandparent) {
    setup();
    
    /* Derived2 -> Derived1 -> Base (2 levels) */
    ASSERT_TRUE(nmo_type_is_derived_from(registry, id_derived2, id_base));
    ASSERT_TRUE(nmo_type_is_derived_from(registry, id_derived2, id_derived1));
    
    /* But not vice versa */
    ASSERT_FALSE(nmo_type_is_derived_from(registry, id_base, id_derived2));
    ASSERT_FALSE(nmo_type_is_derived_from(registry, id_derived1, id_derived2));
    
    teardown();
}

TEST(type_compatibility, is_derived_from_unrelated) {
    setup();
    
    /* Unrelated types are not derived from each other */
    ASSERT_FALSE(nmo_type_is_derived_from(registry, id_unrelated, id_base));
    ASSERT_FALSE(nmo_type_is_derived_from(registry, id_base, id_unrelated));
    ASSERT_FALSE(nmo_type_is_derived_from(registry, id_unrelated, id_derived1));
    
    teardown();
}

TEST(type_compatibility, is_derived_from_invalid_ids) {
    setup();
    
    /* Invalid IDs should return false */
    ASSERT_FALSE(nmo_type_is_derived_from(registry, NMO_TYPE_ID_INVALID, id_base));
    ASSERT_FALSE(nmo_type_is_derived_from(registry, id_base, NMO_TYPE_ID_INVALID));
    ASSERT_FALSE(nmo_type_is_derived_from(registry, NMO_TYPE_ID_INVALID, NMO_TYPE_ID_INVALID));
    
    teardown();
}

TEST(type_compatibility, get_inheritance_chain_base) {
    setup();
    
    nmo_type_id_t *chain = NULL;
    size_t count = 0;
    nmo_status_t res = nmo_type_get_inheritance_chain(registry, id_base, &chain, &count, arena);
    
    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, chain);
    ASSERT_EQ(1, count); /* Base has no parent, chain = [Base] */
    ASSERT_EQ(id_base, chain[0]);
    
    teardown();
}

TEST(type_compatibility, get_inheritance_chain_derived1) {
    setup();
    
    nmo_type_id_t *chain = NULL;
    size_t count = 0;
    nmo_status_t res = nmo_type_get_inheritance_chain(registry, id_derived1, &chain, &count, arena);
    
    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, chain);
    ASSERT_EQ(2, count); /* Chain = [Derived1, Base] */
    ASSERT_EQ(id_derived1, chain[0]); /* Most derived first */
    ASSERT_EQ(id_base, chain[1]);
    
    teardown();
}

TEST(type_compatibility, get_inheritance_chain_derived2) {
    setup();
    
    nmo_type_id_t *chain = NULL;
    size_t count = 0;
    nmo_status_t res = nmo_type_get_inheritance_chain(registry, id_derived2, &chain, &count, arena);
    
    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, chain);
    ASSERT_EQ(3, count); /* Chain = [Derived2, Derived1, Base] */
    ASSERT_EQ(id_derived2, chain[0]);
    ASSERT_EQ(id_derived1, chain[1]);
    ASSERT_EQ(id_base, chain[2]);
    
    teardown();
}

TEST(type_compatibility, get_derivation_depth_same_type) {
    setup();
    
    /* Same type has depth 0 */
    ASSERT_EQ(0, nmo_type_get_derivation_depth(registry, id_base, id_base));
    ASSERT_EQ(0, nmo_type_get_derivation_depth(registry, id_derived1, id_derived1));
    
    teardown();
}

TEST(type_compatibility, get_derivation_depth_direct_parent) {
    setup();
    
    /* Direct parent has depth 1 */
    ASSERT_EQ(1, nmo_type_get_derivation_depth(registry, id_derived1, id_base));
    ASSERT_EQ(1, nmo_type_get_derivation_depth(registry, id_derived2, id_derived1));
    
    teardown();
}

TEST(type_compatibility, get_derivation_depth_grandparent) {
    setup();
    
    /* Grandparent has depth 2 */
    ASSERT_EQ(2, nmo_type_get_derivation_depth(registry, id_derived2, id_base));
    
    teardown();
}

TEST(type_compatibility, get_derivation_depth_unrelated) {
    setup();
    
    /* Unrelated types return -1 */
    ASSERT_EQ(-1, nmo_type_get_derivation_depth(registry, id_unrelated, id_base));
    ASSERT_EQ(-1, nmo_type_get_derivation_depth(registry, id_base, id_derived1)); /* Wrong direction */
    
    teardown();
}

/* ============================================================================
 * Task 6.3.2: Compatibility Checking Tests
 * ============================================================================ */

TEST(type_compatibility, is_compatible_same_type) {
    setup();
    
    /* Same type is always compatible */
    ASSERT_TRUE(nmo_type_is_compatible(registry, id_base, id_base));
    ASSERT_TRUE(nmo_type_is_compatible(registry, id_derived1, id_derived1));
    
    teardown();
}

TEST(type_compatibility, is_compatible_derived_types) {
    setup();
    
    /* Derived types are compatible (symmetric) */
    ASSERT_TRUE(nmo_type_is_compatible(registry, id_derived1, id_base));
    ASSERT_TRUE(nmo_type_is_compatible(registry, id_base, id_derived1)); /* Symmetric! */
    
    ASSERT_TRUE(nmo_type_is_compatible(registry, id_derived2, id_base));
    ASSERT_TRUE(nmo_type_is_compatible(registry, id_base, id_derived2));
    
    ASSERT_TRUE(nmo_type_is_compatible(registry, id_derived2, id_derived1));
    ASSERT_TRUE(nmo_type_is_compatible(registry, id_derived1, id_derived2));
    
    teardown();
}

TEST(type_compatibility, is_compatible_unrelated) {
    setup();
    
    /* Unrelated types are not compatible */
    ASSERT_FALSE(nmo_type_is_compatible(registry, id_unrelated, id_base));
    ASSERT_FALSE(nmo_type_is_compatible(registry, id_base, id_unrelated));
    ASSERT_FALSE(nmo_type_is_compatible(registry, id_unrelated, id_derived1));
    
    teardown();
}

/* ============================================================================
 * Task 6.3.3: Type Conversion API Tests
 * ============================================================================ */

TEST(type_conversion, guid_to_type_id) {
    setup();
    
    /* Valid conversions */
    ASSERT_EQ(id_base, nmo_type_registry_guid_to_type_id(registry, guid_base));
    ASSERT_EQ(id_derived1, nmo_type_registry_guid_to_type_id(registry, guid_derived1));
    
    /* Invalid GUID */
    nmo_guid_t invalid_guid = {0xDEADBEEF, 0xCAFEBABE};
    ASSERT_EQ(NMO_TYPE_ID_INVALID, nmo_type_registry_guid_to_type_id(registry, invalid_guid));
    
    teardown();
}

TEST(type_conversion, type_id_to_guid) {
    setup();
    
    nmo_guid_t result_guid;
    nmo_status_t res;
    
    /* Valid conversions */
    res = nmo_type_registry_type_id_to_guid(registry, id_base, &result_guid);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_TRUE(nmo_guid_equals(guid_base, result_guid));
    
    res = nmo_type_registry_type_id_to_guid(registry, id_derived1, &result_guid);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_TRUE(nmo_guid_equals(guid_derived1, result_guid));
    
    /* Invalid ID */
    res = nmo_type_registry_type_id_to_guid(registry, NMO_TYPE_ID_INVALID, &result_guid);
    ASSERT_NE(NMO_OK, res);
    
    teardown();
}

TEST(type_conversion, guid_to_name) {
    setup();
    
    const char *name = nmo_type_registry_guid_to_name(registry, guid_base);
    ASSERT_NE(NULL, name);
    ASSERT_STR_EQ("BaseType", name);
    
    name = nmo_type_registry_guid_to_name(registry, guid_derived1);
    ASSERT_NE(NULL, name);
    ASSERT_STR_EQ("Derived1Type", name);
    
    /* Invalid GUID */
    nmo_guid_t invalid_guid = {0xDEADBEEF, 0xCAFEBABE};
    name = nmo_type_registry_guid_to_name(registry, invalid_guid);
    ASSERT_EQ(NULL, name);
    
    teardown();
}

TEST(type_conversion, name_to_guid) {
    setup();
    
    nmo_guid_t result_guid;
    nmo_status_t res;
    
    res = nmo_type_registry_name_to_guid(registry, "BaseType", &result_guid);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_TRUE(nmo_guid_equals(guid_base, result_guid));
    
    res = nmo_type_registry_name_to_guid(registry, "Derived1Type", &result_guid);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_TRUE(nmo_guid_equals(guid_derived1, result_guid));
    
    /* Invalid name */
    res = nmo_type_registry_name_to_guid(registry, "NonExistentType", &result_guid);
    ASSERT_NE(NMO_OK, res);
    
    teardown();
}

TEST(type_conversion, type_id_to_name) {
    setup();
    
    const char *name = nmo_type_registry_type_id_to_name(registry, id_base);
    ASSERT_NE(NULL, name);
    ASSERT_STR_EQ("BaseType", name);
    
    name = nmo_type_registry_type_id_to_name(registry, id_derived1);
    ASSERT_NE(NULL, name);
    ASSERT_STR_EQ("Derived1Type", name);
    
    /* Invalid ID */
    name = nmo_type_registry_type_id_to_name(registry, NMO_TYPE_ID_INVALID);
    ASSERT_EQ(NULL, name);
    
    teardown();
}

TEST(type_conversion, name_to_type_id) {
    setup();
    
    ASSERT_EQ(id_base, nmo_type_registry_name_to_type_id(registry, "BaseType"));
    ASSERT_EQ(id_derived1, nmo_type_registry_name_to_type_id(registry, "Derived1Type"));
    
    /* Invalid name */
    ASSERT_EQ(NMO_TYPE_ID_INVALID, nmo_type_registry_name_to_type_id(registry, "NonExistentType"));
    
    teardown();
}

TEST(type_conversion, class_id_conversions) {
    setup();
    
    /* Register type with ClassID */
    nmo_guid_t guid_with_classid = {0x20000001, 0x00000000};
    nmo_type_descriptor_t type_with_classid = {0};
    type_with_classid.guid = guid_with_classid;
    type_with_classid.name = "TypeWithClassID";
    type_with_classid.size = 4;
    type_with_classid.alignment = 4;
    type_with_classid.category = NMO_TYPE_CATEGORY_OBJECT_REF;
    type_with_classid.class_id = 12345; /* Virtools CK_CLASSID */
    type_with_classid.valid = true;
    type_with_classid.base_type = NMO_GUID_NULL;
    type_with_classid.vtable = &dummy_type_vtable;
    
    nmo_status_t res = nmo_type_registry_register(registry, &type_with_classid);
    ASSERT_EQ(NMO_OK, res);
    
    nmo_type_id_t id_with_classid = nmo_type_registry_guid_to_type_id(registry, guid_with_classid);
    ASSERT_NE(NMO_TYPE_ID_INVALID, id_with_classid);
    
    /* ClassID -> GUID */
    nmo_guid_t result_guid;
    res = nmo_type_registry_class_id_to_guid(registry, 12345, &result_guid);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_TRUE(nmo_guid_equals(guid_with_classid, result_guid));
    
    /* GUID -> ClassID */
    uint32_t result_classid;
    res = nmo_type_registry_guid_to_class_id(registry, guid_with_classid, &result_classid);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_EQ(12345, result_classid);
    
    /* Type ID -> ClassID */
    res = nmo_type_registry_type_id_to_class_id(registry, id_with_classid, &result_classid);
    ASSERT_EQ(NMO_OK, res);
    ASSERT_EQ(12345, result_classid);
    
    /* ClassID -> Type ID */
    ASSERT_EQ(id_with_classid, nmo_type_registry_class_id_to_type_id(registry, 12345));
    
    /* Type without ClassID should fail */
    res = nmo_type_registry_guid_to_class_id(registry, guid_base, &result_classid);
    ASSERT_NE(NMO_OK, res);
    
    teardown();
}

/* ============================================================================
 * Edge Cases & Error Handling
 * ============================================================================ */

TEST(type_compatibility, null_registry) {
    /* All functions should handle NULL registry gracefully */
    ASSERT_FALSE(nmo_type_is_derived_from(NULL, 0, 1));
    ASSERT_FALSE(nmo_type_is_compatible(NULL, 0, 1));
    ASSERT_EQ(-1, nmo_type_get_derivation_depth(NULL, 0, 1));
    ASSERT_EQ(NMO_TYPE_ID_INVALID, nmo_type_registry_guid_to_type_id(NULL, guid_base));
    ASSERT_EQ(NULL, nmo_type_registry_type_id_to_name(NULL, 0));
}

TEST(type_compatibility, lazy_mask_update) {
    setup();
    
    /* Force invalidation */
    registry->derivation_masks_valid = false;
    
    /* First call should trigger lazy update */
    ASSERT_TRUE(nmo_type_is_derived_from(registry, id_derived1, id_base));
    
    /* Should be valid now */
    ASSERT_TRUE(registry->derivation_masks_valid);
    
    teardown();
}

TEST(type_compatibility, derivation_out_of_order_registration) {
    nmo_arena_t *local_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NE(NULL, local_arena);

    nmo_type_registry_t *local_registry = nmo_type_registry_create(local_arena);
    ASSERT_NE(NULL, local_registry);

    nmo_guid_t guid_parent = {0x20000001, 0x00000000};
    nmo_guid_t guid_child = {0x20000002, 0x00000000};

    nmo_type_descriptor_t child_type = {0};
    child_type.guid = guid_child;
    child_type.name = "ChildType";
    child_type.size = 4;
    child_type.alignment = 4;
    child_type.category = NMO_TYPE_CATEGORY_SCALAR;
    child_type.valid = true;
    child_type.base_type = guid_parent;

    nmo_status_t res = nmo_type_registry_register(local_registry, &child_type);
    ASSERT_EQ(NMO_OK, res);

    nmo_type_descriptor_t parent_type = {0};
    parent_type.guid = guid_parent;
    parent_type.name = "ParentType";
    parent_type.size = 4;
    parent_type.alignment = 4;
    parent_type.category = NMO_TYPE_CATEGORY_SCALAR;
    parent_type.valid = true;
    parent_type.base_type = NMO_GUID_NULL;

    res = nmo_type_registry_register(local_registry, &parent_type);
    ASSERT_EQ(NMO_OK, res);

    nmo_type_id_t parent_id = nmo_type_registry_guid_to_type_id(local_registry, guid_parent);
    nmo_type_id_t child_id = nmo_type_registry_guid_to_type_id(local_registry, guid_child);
    ASSERT_NE(NMO_TYPE_ID_INVALID, parent_id);
    ASSERT_NE(NMO_TYPE_ID_INVALID, child_id);

    ASSERT_TRUE(nmo_type_is_derived_from(local_registry, child_id, parent_id));

    nmo_type_registry_destroy(local_registry);
    nmo_arena_destroy(local_arena);
}

/* ============================================================================
 * Test Main
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(type_compatibility, is_derived_from_same_type);
    REGISTER_TEST(type_compatibility, is_derived_from_direct_parent);
    REGISTER_TEST(type_compatibility, is_derived_from_grandparent);
    REGISTER_TEST(type_compatibility, is_derived_from_unrelated);
    REGISTER_TEST(type_compatibility, is_derived_from_invalid_ids);
    REGISTER_TEST(type_compatibility, get_inheritance_chain_base);
    REGISTER_TEST(type_compatibility, get_inheritance_chain_derived1);
    REGISTER_TEST(type_compatibility, get_inheritance_chain_derived2);
    REGISTER_TEST(type_compatibility, get_derivation_depth_same_type);
    REGISTER_TEST(type_compatibility, get_derivation_depth_direct_parent);
    REGISTER_TEST(type_compatibility, get_derivation_depth_grandparent);
    REGISTER_TEST(type_compatibility, get_derivation_depth_unrelated);
    REGISTER_TEST(type_compatibility, is_compatible_same_type);
    REGISTER_TEST(type_compatibility, is_compatible_derived_types);
    REGISTER_TEST(type_compatibility, is_compatible_unrelated);
    REGISTER_TEST(type_conversion, guid_to_type_id);
    REGISTER_TEST(type_conversion, type_id_to_guid);
    REGISTER_TEST(type_conversion, guid_to_name);
    REGISTER_TEST(type_conversion, name_to_guid);
    REGISTER_TEST(type_conversion, type_id_to_name);
    REGISTER_TEST(type_conversion, name_to_type_id);
    REGISTER_TEST(type_conversion, class_id_conversions);
    REGISTER_TEST(type_compatibility, null_registry);
    REGISTER_TEST(type_compatibility, lazy_mask_update);
    REGISTER_TEST(type_compatibility, derivation_out_of_order_registration);
TEST_MAIN_END()
