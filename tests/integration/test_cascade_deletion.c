/**
 * @file test_cascade_deletion.c
 * @brief Integration tests for cascade deletion functionality
 * 
 * Tests recursive unregistration of derived types.
 * Validates nmo_type_registry_unregister_derived() works correctly.
 * 
 * Reference: CKParameterManager.cpp lines 140-145
 * Phase 5.6 - Task T5.6.6
 */

#include "type/type_system.h"
#include "test_framework.h"
#include <string.h>

/* Test type GUIDs forming inheritance hierarchy */
static const nmo_guid_t GUID_BASE = {0x10000000, 0x00000001};
static const nmo_guid_t GUID_DERIVED1 = {0x20000000, 0x00000002};
static const nmo_guid_t GUID_DERIVED2 = {0x30000000, 0x00000003};
static const nmo_guid_t GUID_DERIVED1_1 = {0x40000000, 0x00000004};
static const nmo_guid_t GUID_DERIVED1_2 = {0x50000000, 0x00000005};
static const nmo_guid_t GUID_DERIVED2_1 = {0x60000000, 0x00000006};
static const nmo_guid_t GUID_INDEPENDENT = {0x70000000, 0x00000007};

/* ============================================================================
 * Test: Simple Two-Level Cascade
 * ============================================================================ */

TEST(cascade_deletion, two_level_hierarchy) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register base type */
    nmo_type_descriptor_t base_type = {0};
    base_type.guid = GUID_BASE;
    base_type.name = "BaseType";
    base_type.category = NMO_TYPE_CATEGORY_STRUCT;
    base_type.size = 16;
    base_type.alignment = 4;
    base_type.base_type = NMO_GUID_NULL;
    base_type.valid = true;
    
    nmo_result_t result = nmo_type_registry_register(registry, &base_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Register derived type */
    nmo_type_descriptor_t derived_type = {0};
    derived_type.guid = GUID_DERIVED1;
    derived_type.name = "DerivedType";
    derived_type.category = NMO_TYPE_CATEGORY_STRUCT;
    derived_type.size = 32;
    derived_type.alignment = 4;
    derived_type.base_type = GUID_BASE;
    derived_type.valid = true;
    
    result = nmo_type_registry_register(registry, &derived_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify both types exist */
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, GUID_BASE));
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, GUID_DERIVED1));
    
    /* Cascade delete derived types of base */
    result = nmo_type_registry_unregister_derived(registry, GUID_BASE);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Base type should still exist */
    const nmo_type_descriptor_t *base_check = 
        nmo_type_registry_find_by_guid(registry, GUID_BASE);
    ASSERT_NE(NULL, base_check);
    ASSERT_TRUE(base_check->valid);
    
    /* Derived type should be removed or invalid */
    const nmo_type_descriptor_t *derived_check = 
        nmo_type_registry_find_by_guid(registry, GUID_DERIVED1);
    if (derived_check != NULL) {
        ASSERT_FALSE(derived_check->valid);
    }
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Three-Level Deep Hierarchy
 * ============================================================================ */

TEST(cascade_deletion, three_level_hierarchy) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Create hierarchy: Base -> Derived1 -> Derived1_1 */
    nmo_type_descriptor_t types[3] = {
        {
            .guid = GUID_BASE,
            .name = "Base",
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .size = 16,
            .alignment = 4,
            .base_type = NMO_GUID_NULL,
            .valid = true
        },
        {
            .guid = GUID_DERIVED1,
            .name = "Derived1",
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .size = 32,
            .alignment = 4,
            .base_type = GUID_BASE,
            .valid = true
        },
        {
            .guid = GUID_DERIVED1_1,
            .name = "Derived1_1",
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .size = 48,
            .alignment = 4,
            .base_type = GUID_DERIVED1,
            .valid = true
        }
    };
    
    /* Register all types */
    for (int i = 0; i < 3; i++) {
        nmo_result_t result = nmo_type_registry_register(registry, &types[i]);
        ASSERT_EQ(NMO_OK, result.code);
    }
    
    /* Verify all exist */
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, GUID_BASE));
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, GUID_DERIVED1));
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, GUID_DERIVED1_1));
    
    /* Cascade delete from base (should remove entire chain) */
    nmo_result_t result = nmo_type_registry_unregister_derived(registry, GUID_BASE);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Base should still exist */
    const nmo_type_descriptor_t *base = 
        nmo_type_registry_find_by_guid(registry, GUID_BASE);
    ASSERT_NE(NULL, base);
    ASSERT_TRUE(base->valid);
    
    /* All derived types should be removed/invalid */
    const nmo_type_descriptor_t *d1 = 
        nmo_type_registry_find_by_guid(registry, GUID_DERIVED1);
    const nmo_type_descriptor_t *d1_1 = 
        nmo_type_registry_find_by_guid(registry, GUID_DERIVED1_1);
    
    if (d1 != NULL) ASSERT_FALSE(d1->valid);
    if (d1_1 != NULL) ASSERT_FALSE(d1_1->valid);
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Multiple Branches
 * ============================================================================ */

TEST(cascade_deletion, multiple_branches) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Create tree:
     *        Base
     *       /    \
     *  Derived1  Derived2
     *     /         \
     * Derived1_1  Derived2_1
     */
    nmo_type_descriptor_t types[] = {
        {
            .guid = GUID_BASE,
            .name = "Base",
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .size = 16,
            .alignment = 4,
            .base_type = NMO_GUID_NULL,
            .valid = true
        },
        {
            .guid = GUID_DERIVED1,
            .name = "Derived1",
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .size = 32,
            .alignment = 4,
            .base_type = GUID_BASE,
            .valid = true
        },
        {
            .guid = GUID_DERIVED2,
            .name = "Derived2",
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .size = 32,
            .alignment = 4,
            .base_type = GUID_BASE,
            .valid = true
        },
        {
            .guid = GUID_DERIVED1_1,
            .name = "Derived1_1",
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .size = 48,
            .alignment = 4,
            .base_type = GUID_DERIVED1,
            .valid = true
        },
        {
            .guid = GUID_DERIVED2_1,
            .name = "Derived2_1",
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .size = 48,
            .alignment = 4,
            .base_type = GUID_DERIVED2,
            .valid = true
        }
    };
    
    /* Register all types */
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        nmo_result_t result = nmo_type_registry_register(registry, &types[i]);
        ASSERT_EQ(NMO_OK, result.code);
    }
    
    /* Cascade delete all derived from base */
    nmo_result_t result = nmo_type_registry_unregister_derived(registry, GUID_BASE);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Base should remain valid */
    const nmo_type_descriptor_t *base = 
        nmo_type_registry_find_by_guid(registry, GUID_BASE);
    ASSERT_NE(NULL, base);
    ASSERT_TRUE(base->valid);
    
    /* All branches should be removed/invalid */
    const nmo_type_descriptor_t *checks[] = {
        nmo_type_registry_find_by_guid(registry, GUID_DERIVED1),
        nmo_type_registry_find_by_guid(registry, GUID_DERIVED2),
        nmo_type_registry_find_by_guid(registry, GUID_DERIVED1_1),
        nmo_type_registry_find_by_guid(registry, GUID_DERIVED2_1)
    };
    
    for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
        if (checks[i] != NULL) {
            ASSERT_FALSE(checks[i]->valid);
        }
    }
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Selective Branch Deletion
 * ============================================================================ */

TEST(cascade_deletion, selective_branch) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Same tree as before */
    nmo_type_descriptor_t types[] = {
        {
            .guid = GUID_BASE,
            .name = "Base",
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .size = 16,
            .alignment = 4,
            .base_type = NMO_GUID_NULL,
            .valid = true
        },
        {
            .guid = GUID_DERIVED1,
            .name = "Derived1",
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .size = 32,
            .alignment = 4,
            .base_type = GUID_BASE,
            .valid = true
        },
        {
            .guid = GUID_DERIVED2,
            .name = "Derived2",
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .size = 32,
            .alignment = 4,
            .base_type = GUID_BASE,
            .valid = true
        },
        {
            .guid = GUID_DERIVED1_1,
            .name = "Derived1_1",
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .size = 48,
            .alignment = 4,
            .base_type = GUID_DERIVED1,
            .valid = true
        },
        {
            .guid = GUID_DERIVED2_1,
            .name = "Derived2_1",
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .size = 48,
            .alignment = 4,
            .base_type = GUID_DERIVED2,
            .valid = true
        }
    };
    
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        nmo_result_t result = nmo_type_registry_register(registry, &types[i]);
        ASSERT_EQ(NMO_OK, result.code);
    }
    
    /* Delete only Derived1 branch */
    nmo_result_t result = nmo_type_registry_unregister_derived(registry, GUID_DERIVED1);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Base, Derived1, Derived2, Derived2_1 should remain valid */
    ASSERT_TRUE(nmo_type_registry_find_by_guid(registry, GUID_BASE)->valid);
    ASSERT_TRUE(nmo_type_registry_find_by_guid(registry, GUID_DERIVED1)->valid);
    ASSERT_TRUE(nmo_type_registry_find_by_guid(registry, GUID_DERIVED2)->valid);
    ASSERT_TRUE(nmo_type_registry_find_by_guid(registry, GUID_DERIVED2_1)->valid);
    
    /* Only Derived1_1 should be invalid */
    const nmo_type_descriptor_t *d1_1 = 
        nmo_type_registry_find_by_guid(registry, GUID_DERIVED1_1);
    if (d1_1 != NULL) {
        ASSERT_FALSE(d1_1->valid);
    }
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Independent Types Unaffected
 * ============================================================================ */

TEST(cascade_deletion, independent_types_preserved) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register hierarchy and independent type */
    nmo_type_descriptor_t types[] = {
        {
            .guid = GUID_BASE,
            .name = "Base",
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .size = 16,
            .alignment = 4,
            .base_type = NMO_GUID_NULL,
            .valid = true
        },
        {
            .guid = GUID_DERIVED1,
            .name = "Derived1",
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .size = 32,
            .alignment = 4,
            .base_type = GUID_BASE,
            .valid = true
        },
        {
            .guid = GUID_INDEPENDENT,
            .name = "Independent",
            .category = NMO_TYPE_CATEGORY_SCALAR,
            .size = 8,
            .alignment = 4,
            .base_type = NMO_GUID_NULL,
            .valid = true
        }
    };
    
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        nmo_result_t result = nmo_type_registry_register(registry, &types[i]);
        ASSERT_EQ(NMO_OK, result.code);
    }
    
    /* Cascade delete base's derived types */
    nmo_result_t result = nmo_type_registry_unregister_derived(registry, GUID_BASE);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Independent type should remain valid */
    const nmo_type_descriptor_t *independent = 
        nmo_type_registry_find_by_guid(registry, GUID_INDEPENDENT);
    ASSERT_NE(NULL, independent);
    ASSERT_TRUE(independent->valid);
    
    /* Base should remain valid */
    const nmo_type_descriptor_t *base = 
        nmo_type_registry_find_by_guid(registry, GUID_BASE);
    ASSERT_NE(NULL, base);
    ASSERT_TRUE(base->valid);
    
    /* Derived1 should be invalid */
    const nmo_type_descriptor_t *derived = 
        nmo_type_registry_find_by_guid(registry, GUID_DERIVED1);
    if (derived != NULL) {
        ASSERT_FALSE(derived->valid);
    }
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Empty Cascade (No Derived Types)
 * ============================================================================ */

TEST(cascade_deletion, no_derived_types) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register standalone type with no derived types */
    nmo_type_descriptor_t base_type = {0};
    base_type.guid = GUID_BASE;
    base_type.name = "StandaloneType";
    base_type.category = NMO_TYPE_CATEGORY_STRUCT;
    base_type.size = 16;
    base_type.alignment = 4;
    base_type.base_type = NMO_GUID_NULL;
    base_type.valid = true;
    
    nmo_result_t result = nmo_type_registry_register(registry, &base_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Cascade delete (should be no-op) */
    result = nmo_type_registry_unregister_derived(registry, GUID_BASE);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Base type should still be valid */
    const nmo_type_descriptor_t *check = 
        nmo_type_registry_find_by_guid(registry, GUID_BASE);
    ASSERT_NE(NULL, check);
    ASSERT_TRUE(check->valid);
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Invalid Arguments
 * ============================================================================ */

TEST(cascade_deletion, invalid_arguments) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* NULL registry */
    nmo_result_t result = nmo_type_registry_unregister_derived(NULL, GUID_BASE);
    ASSERT_NE(NMO_OK, result.code);
    
    /* Non-existent GUID (should succeed with no-op) */
    nmo_guid_t nonexistent = {0xFFFFFFFF, 0xFFFFFFFF};
    result = nmo_type_registry_unregister_derived(registry, nonexistent);
    ASSERT_EQ(NMO_OK, result.code);
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test Main
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(cascade_deletion, two_level_hierarchy);
    REGISTER_TEST(cascade_deletion, three_level_hierarchy);
    REGISTER_TEST(cascade_deletion, multiple_branches);
    REGISTER_TEST(cascade_deletion, selective_branch);
    REGISTER_TEST(cascade_deletion, independent_types_preserved);
    REGISTER_TEST(cascade_deletion, no_derived_types);
    REGISTER_TEST(cascade_deletion, invalid_arguments);
TEST_MAIN_END()
