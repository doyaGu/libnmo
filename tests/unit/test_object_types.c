/**
 * @file test_object_types.c
 * @brief Unit tests for rebuilt object type system (Phase 6.1)
 */

#include "test_framework.h"
#include "object/nmo_object_types.h"
#include "type/nmo_type_system.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include <string.h>

/* Test: Basic registration */
TEST(object_types, register_base_types) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);

    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);

    nmo_status_t result = nmo_register_base_object_types(registry);
    ASSERT_EQ(NMO_OK, result);

    /* Verify CKObject was registered */
    const nmo_type_descriptor_t *ckobject = nmo_type_registry_find_by_guid(
        registry, NMO_GUID_CKOBJECT);
    ASSERT_NE(NULL, ckobject);
    ASSERT_EQ(0, strcmp(ckobject->name, "CKObject"));
    ASSERT_EQ(1, ckobject->class_id);  /* NMO_CID_OBJECT */

    nmo_arena_destroy(arena);
}

/* Test: Full registration */
TEST(object_types, register_all_types) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);

    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);

    nmo_status_t result = nmo_register_object_types(registry);
    ASSERT_EQ(NMO_OK, result);

    /* Verify key types were registered */
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, NMO_GUID_CKOBJECT));
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, NMO_GUID_CKMESH));
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, NMO_GUID_CKMATERIAL));
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, NMO_GUID_CK3DENTITY));
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, NMO_GUID_CKBEHAVIOR));

    nmo_arena_destroy(arena);
}

/* Test: Class ID lookup */
TEST(object_types, lookup_by_class_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_register_object_types(registry);

    /* Lookup CKMesh (class 32) */
    const nmo_type_descriptor_t *mesh = nmo_get_object_type_by_class_id(registry, 32);
    ASSERT_NE(NULL, mesh);
    ASSERT_EQ(0, strcmp(mesh->name, "CKMesh"));
    ASSERT_EQ(32, mesh->class_id);

    /* Verify GUID encoding */
    ASSERT_EQ(0x564B4F42, mesh->guid.d1);
    ASSERT_EQ(32, mesh->guid.d2);

    nmo_arena_destroy(arena);
}

/* Test: Inheritance check */
TEST(object_types, inheritance_check) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_register_object_types(registry);

    /* CKSprite (28) should derive from CK2dEntity (27) */
    const nmo_type_descriptor_t *sprite = nmo_type_registry_find_by_class_id(registry, 28);
    const nmo_type_descriptor_t *entity2d = nmo_type_registry_find_by_class_id(registry, 27);
    ASSERT_NE(NULL, sprite);
    ASSERT_NE(NULL, entity2d);

    bool is_derived = nmo_type_is_derived_from(registry, sprite->id, entity2d->id);
    ASSERT_TRUE(is_derived);

    /* CKSprite should also derive from CKObject (1) */
    const nmo_type_descriptor_t *ckobject = nmo_type_registry_find_by_class_id(registry, 1);
    ASSERT_NE(NULL, ckobject);
    is_derived = nmo_type_is_derived_from(registry, sprite->id, ckobject->id);
    ASSERT_TRUE(is_derived);

    /* CKMesh (32) should NOT derive from CK3dEntity (33) */
    const nmo_type_descriptor_t *mesh = nmo_type_registry_find_by_class_id(registry, 32);
    const nmo_type_descriptor_t *entity3d = nmo_type_registry_find_by_class_id(registry, 33);
    ASSERT_NE(NULL, mesh);
    ASSERT_NE(NULL, entity3d);
    is_derived = nmo_type_is_derived_from(registry, mesh->id, entity3d->id);
    ASSERT_FALSE(is_derived);

    nmo_arena_destroy(arena);
}

/* Test: GUID to class ID conversion */
TEST(object_types, guid_to_class_id) {
    nmo_class_id_t class_id = nmo_object_guid_to_class_id(NMO_GUID_CKMESH);
    ASSERT_EQ(32, class_id);

    class_id = nmo_object_guid_to_class_id(NMO_GUID_CKOBJECT);
    ASSERT_EQ(1, class_id);

    /* Non-object GUID should return 0 */
    nmo_guid_t non_object_guid = {0x12345678, 0xABCDEF00};
    class_id = nmo_object_guid_to_class_id(non_object_guid);
    ASSERT_EQ(0, class_id);
}

/* Test: Object type check */
TEST(object_types, is_object_type) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_register_object_types(registry);

    /* CKMesh is an object type */
    int result = nmo_is_object_type(registry, NMO_GUID_CKMESH);
    ASSERT_EQ(1, result);

    /* CKObject itself is an object type */
    result = nmo_is_object_type(registry, NMO_GUID_CKOBJECT);
    ASSERT_EQ(1, result);

    /* Non-existent type */
    nmo_guid_t fake_guid = {0x12345678, 0xABCDEF00};
    result = nmo_is_object_type(registry, fake_guid);
    ASSERT_EQ(0, result);

    nmo_arena_destroy(arena);
}

/* Test: 3D entity hierarchy */
TEST(object_types, 3d_entity_hierarchy) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_register_object_types(registry);

    /* Get types */
    const nmo_type_descriptor_t *ckobject = nmo_type_registry_find_by_class_id(registry, 1);
    const nmo_type_descriptor_t *beobject = nmo_type_registry_find_by_class_id(registry, 19);
    const nmo_type_descriptor_t *renderobject = nmo_type_registry_find_by_class_id(registry, 47);
    const nmo_type_descriptor_t *entity3d = nmo_type_registry_find_by_class_id(registry, 33);
    const nmo_type_descriptor_t *camera = nmo_type_registry_find_by_class_id(registry, 34);

    ASSERT_NE(NULL, ckobject);
    ASSERT_NE(NULL, beobject);
    ASSERT_NE(NULL, renderobject);
    ASSERT_NE(NULL, entity3d);
    ASSERT_NE(NULL, camera);

    /* Verify hierarchy: CKCamera �?CK3dEntity �?CKRenderObject �?CKBeObject �?CKSceneObject �?CKObject */
    ASSERT_TRUE(nmo_type_is_derived_from(registry, camera->id, entity3d->id));
    ASSERT_TRUE(nmo_type_is_derived_from(registry, entity3d->id, renderobject->id));
    ASSERT_TRUE(nmo_type_is_derived_from(registry, renderobject->id, beobject->id));
    ASSERT_TRUE(nmo_type_is_derived_from(registry, camera->id, ckobject->id));

    nmo_arena_destroy(arena);
}

/* Test: Resource types */
TEST(object_types, resource_types) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_register_object_types(registry);

    /* Get resource types */
    const nmo_type_descriptor_t *material = nmo_type_registry_find_by_class_id(registry, 30);
    const nmo_type_descriptor_t *texture = nmo_type_registry_find_by_class_id(registry, 31);
    const nmo_type_descriptor_t *mesh = nmo_type_registry_find_by_class_id(registry, 32);

    ASSERT_NE(NULL, material);
    ASSERT_NE(NULL, texture);
    ASSERT_NE(NULL, mesh);

    /* All should derive from CKBeObject */
    const nmo_type_descriptor_t *beobject = nmo_type_registry_find_by_class_id(registry, 19);
    ASSERT_NE(NULL, beobject);
    ASSERT_TRUE(nmo_type_is_derived_from(registry, material->id, beobject->id));
    ASSERT_TRUE(nmo_type_is_derived_from(registry, texture->id, beobject->id));
    ASSERT_TRUE(nmo_type_is_derived_from(registry, mesh->id, beobject->id));

    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(object_types, register_base_types);
    REGISTER_TEST(object_types, register_all_types);
    REGISTER_TEST(object_types, lookup_by_class_id);
    REGISTER_TEST(object_types, inheritance_check);
    REGISTER_TEST(object_types, guid_to_class_id);
    REGISTER_TEST(object_types, is_object_type);
    REGISTER_TEST(object_types, 3d_entity_hierarchy);
    REGISTER_TEST(object_types, resource_types);
TEST_MAIN_END()
