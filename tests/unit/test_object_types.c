/**
 * @file test_object_types.c
 * @brief Unit tests for rebuilt object type system (Phase 6.1)
 */

#include "test_framework.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_struct_defs.h"
#include "object/nmo_object_struct_guids.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_operations.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_system.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include <stdint.h>
#include <string.h>

static nmo_status_t register_test_object_types(nmo_type_registry_t *registry) {
    nmo_status_t result = nmo_register_builtin_types(registry);
    if (result != NMO_OK) {
        return result;
    }
    return nmo_register_object_types(registry);
}

static int is_parameter_class(nmo_type_registry_t *registry, nmo_class_id_t class_id) {
    if (!registry) {
        return 0;
    }

    if (nmo_type_registry_is_class_derived_from(registry, class_id, NMO_CID_PARAMETER)) {
        return 1;
    }

    switch (class_id) {
    case NMO_CID_PARAMETERIN:
    case NMO_CID_PARAMETEROUT:
    case NMO_CID_PARAMETEROPERATION:
        return 1;
    default:
        return 0;
    }
}

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
        registry, CKPGUID_OBJECT);
    ASSERT_NE(NULL, ckobject);
    ASSERT_EQ(0, strcmp(ckobject->name, "CKObject"));
    ASSERT_EQ(1, ckobject->class_id);  /* NMO_CID_OBJECT */

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* Test: Full registration */
TEST(object_types, register_all_types) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);

    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);

    nmo_last_error_clear();
     nmo_status_t result = register_test_object_types(registry);
    if (result != NMO_OK) {
        char chain[1024];
        nmo_last_error_chain_copy(chain, sizeof(chain));

        char msg[512];
        test_format_error(msg, sizeof(msg),
                          "nmo_register_object_types failed: %d (%s)\n  %s",
                          (int)result, nmo_error_string(result), chain);
        test_add_result(__func__, __func__, 0, msg, __FILE__, __LINE__);
        return;
    }

    /* Verify key types were registered */
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, CKPGUID_OBJECT));
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, CKPGUID_MESH));
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, CKPGUID_MATERIAL));
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, CKPGUID_3DENTITY));
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, CKPGUID_BEHAVIOR));

    const nmo_type_descriptor_t *portal_entry =
        nmo_type_registry_find_by_guid(
            registry, NMO_GUID_STRUCT_CKPLACEPORTALENTRY);
    ASSERT_NOT_NULL(portal_entry);
    ASSERT_EQ(sizeof(nmo_place_portal_entry_t), portal_entry->size);
    ASSERT_EQ(2u, portal_entry->field_count);
    ASSERT_EQ(sizeof(nmo_ref_t), portal_entry->fields[0].size);
    ASSERT_TRUE(nmo_field_is_ref(&portal_entry->fields[0]));
    ASSERT_TRUE(nmo_field_uses_ref_records(&portal_entry->fields[0]));
    ASSERT_FALSE(nmo_field_is_array(&portal_entry->fields[0]));
    ASSERT_EQ(sizeof(nmo_ref_t), portal_entry->fields[1].size);
    ASSERT_TRUE(nmo_field_is_ref(&portal_entry->fields[1]));
    ASSERT_TRUE(nmo_field_uses_ref_records(&portal_entry->fields[1]));
    ASSERT_FALSE(nmo_field_is_array(&portal_entry->fields[1]));

    const nmo_type_descriptor_t *skin_bone =
        nmo_type_registry_find_by_guid(
            registry, CKPGUID_CK3DENTITYSKINBONE);
    ASSERT_NOT_NULL(skin_bone);
    ASSERT_EQ(sizeof(nmo_3dentity_skin_bone_t), skin_bone->size);
    ASSERT_EQ(3u, skin_bone->field_count);
    ASSERT_EQ(sizeof(nmo_ref_t), skin_bone->fields[0].size);
    ASSERT_TRUE(nmo_field_is_ref(&skin_bone->fields[0]));
    ASSERT_TRUE(nmo_field_uses_ref_records(&skin_bone->fields[0]));
    ASSERT_FALSE(nmo_field_is_array(&skin_bone->fields[0]));

    const nmo_type_descriptor_t *entity3d =
        nmo_type_registry_find_by_guid(registry, CKPGUID_3DENTITY);
    ASSERT_NOT_NULL(entity3d);
    const nmo_type_field_t *parent = nmo_type_get_field_by_name(
        entity3d, "parent");
    ASSERT_NOT_NULL(parent);
    ASSERT_TRUE(nmo_field_uses_ref_records(parent));
    const nmo_type_field_t *skin = nmo_type_get_field_by_name(
        entity3d, "skin");
    ASSERT_NOT_NULL(skin);
    ASSERT_TRUE(nmo_guid_equals(skin->type_guid, CKPGUID_CK3DENTITYSKIN));
    ASSERT_TRUE((skin->flags & NMO_FIELD_POINTER) != 0u);

    const nmo_type_descriptor_t *character_subpart =
        nmo_type_registry_find_by_guid(
            registry, NMO_GUID_STRUCT_CKCHARACTERSUBPART);
    ASSERT_NOT_NULL(character_subpart);
    ASSERT_EQ(sizeof(nmo_character_subpart_t), character_subpart->size);
    ASSERT_EQ(sizeof(nmo_ref_t), character_subpart->fields[0].size);
    ASSERT_TRUE(nmo_field_uses_ref_records(
        &character_subpart->fields[0]));

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* Test: Class ID lookup */
TEST(object_types, lookup_by_class_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_status_t result = register_test_object_types(registry);
    ASSERT_EQ(NMO_OK, result);

    /* Lookup CKMesh (class 32) */
    const nmo_type_descriptor_t *mesh = nmo_get_object_type_by_class_id(registry, 32);
    ASSERT_NE(NULL, mesh);
    ASSERT_EQ(0, strcmp(mesh->name, "CKMesh"));
    ASSERT_EQ(32, mesh->class_id);

    /* Verify GUID matches canonical CKPGUID_* constant */
    ASSERT_TRUE(nmo_guid_equals(mesh->guid, CKPGUID_MESH));

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* Test: Inheritance check */
TEST(object_types, inheritance_check) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_status_t result = register_test_object_types(registry);
    ASSERT_EQ(NMO_OK, result);

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

    /* CKObjectAnimation and CKAnimation are sibling CKSceneObject classes. */
    const nmo_type_descriptor_t *object_animation =
        nmo_type_registry_find_by_class_id(registry, NMO_CID_OBJECTANIMATION);
    const nmo_type_descriptor_t *animation =
        nmo_type_registry_find_by_class_id(registry, NMO_CID_ANIMATION);
    const nmo_type_descriptor_t *scene_object =
        nmo_type_registry_find_by_class_id(registry, NMO_CID_SCENEOBJECT);
    ASSERT_NE(NULL, object_animation);
    ASSERT_NE(NULL, animation);
    ASSERT_NE(NULL, scene_object);
    ASSERT_TRUE(nmo_type_is_derived_from(
        registry, object_animation->id, scene_object->id));
    ASSERT_FALSE(nmo_type_is_derived_from(
        registry, object_animation->id, animation->id));

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* Test: Object type check */
TEST(object_types, is_object_type) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_status_t result = register_test_object_types(registry);
    ASSERT_EQ(NMO_OK, result);

    /* CKMesh is an object type */
    int group_result = nmo_is_object_type(registry, CKPGUID_MESH);
    ASSERT_EQ(1, group_result);

    /* CKObject itself is an object type */
    group_result = nmo_is_object_type(registry, CKPGUID_OBJECT);
    ASSERT_EQ(1, group_result);

    /* Non-existent type */
    nmo_guid_t fake_guid = {0x12345678, 0xABCDEF00};
    group_result = nmo_is_object_type(registry, fake_guid);
    ASSERT_EQ(0, group_result);

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* Test: Behavior/parameter group helpers */
TEST(object_types, class_group_checks) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_status_t result = register_test_object_types(registry);
    ASSERT_EQ(NMO_OK, result);

    int is_behavior = nmo_type_registry_is_class_derived_from(registry, NMO_CID_BEHAVIOR, NMO_CID_BEHAVIOR) ? 1 : 0;
    int is_parameter = is_parameter_class(registry, NMO_CID_PARAMETER);
    int is_parameter_in = is_parameter_class(registry, NMO_CID_PARAMETERIN);
    int mesh_is_behavior = nmo_type_registry_is_class_derived_from(registry, NMO_CID_MESH, NMO_CID_BEHAVIOR) ? 1 : 0;
    int mesh_is_parameter = is_parameter_class(registry, NMO_CID_MESH);

    ASSERT_EQ(1, is_behavior);
    ASSERT_EQ(1, is_parameter);
    ASSERT_EQ(1, is_parameter_in);
    ASSERT_EQ(0, mesh_is_behavior);
    ASSERT_EQ(0, mesh_is_parameter);

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* Test: 3D entity hierarchy */
TEST(object_types, 3d_entity_hierarchy) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_status_t result = register_test_object_types(registry);
    ASSERT_EQ(NMO_OK, result);

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

    /* Verify hierarchy: CKCamera -> CK3dEntity -> CKRenderObject -> CKBeObject -> CKSceneObject -> CKObject */
    ASSERT_TRUE(nmo_type_is_derived_from(registry, camera->id, entity3d->id));
    ASSERT_TRUE(nmo_type_is_derived_from(registry, entity3d->id, renderobject->id));
    ASSERT_TRUE(nmo_type_is_derived_from(registry, renderobject->id, beobject->id));
    ASSERT_TRUE(nmo_type_is_derived_from(registry, camera->id, ckobject->id));

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* Test: Resource types */
TEST(object_types, resource_types) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_status_t result = register_test_object_types(registry);
    ASSERT_EQ(NMO_OK, result);

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

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

TEST(object_types, default_validate_allows_null_type) {
    uint32_t dummy = 0;
    nmo_status_t result = nmo_object_default_validate(&dummy, NULL, NULL);
    ASSERT_EQ(NMO_OK, result);
}

TEST(object_types, default_validate_rejects_null_instance) {
    nmo_status_t result = nmo_object_default_validate(NULL, NULL, NULL);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);
}

TEST(object_types, copy_helpers_reject_invalid_sizes_atomically) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024);
    ASSERT_NOT_NULL(arena);
    uint32_t source[2] = {1, 2};
    void *sentinel = source;
    void *destination = sentinel;

    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_object_copy_bytes(
        NULL, &destination, source, sizeof(source)));
    ASSERT_EQ(sentinel, destination);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_object_copy_bytes(
        arena, NULL, source, sizeof(source)));

    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_object_copy_array(
        arena, &destination, source, SIZE_MAX, 2));
    ASSERT_EQ(sentinel, destination);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_object_copy_array(
        arena, &destination, source, 0, 2));
    ASSERT_EQ(sentinel, destination);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_object_copy_array(
        NULL, &destination, source, sizeof(source[0]), 2));
    ASSERT_EQ(sentinel, destination);

    ASSERT_EQ(NMO_OK, nmo_object_copy_array(
        arena, &destination, source, sizeof(source[0]), 2));
    ASSERT_NE(source, destination);
    ASSERT_EQ(0, memcmp(source, destination, sizeof(source)));

    destination = sentinel;
    ASSERT_EQ(NMO_OK, nmo_object_copy_bytes(NULL, &destination, NULL, 0));
    ASSERT_NULL(destination);
    ASSERT_EQ(NMO_OK, nmo_object_copy_array(
        NULL, &destination, NULL, 0, 0));
    ASSERT_NULL(destination);

    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(object_types, register_base_types);
    REGISTER_TEST(object_types, register_all_types);
    REGISTER_TEST(object_types, lookup_by_class_id);
    REGISTER_TEST(object_types, inheritance_check);
    REGISTER_TEST(object_types, is_object_type);
    REGISTER_TEST(object_types, class_group_checks);
    REGISTER_TEST(object_types, 3d_entity_hierarchy);
    REGISTER_TEST(object_types, resource_types);
    REGISTER_TEST(object_types, default_validate_allows_null_type);
    REGISTER_TEST(object_types, default_validate_rejects_null_instance);
    REGISTER_TEST(object_types, copy_helpers_reject_invalid_sizes_atomically);
TEST_MAIN_END()
