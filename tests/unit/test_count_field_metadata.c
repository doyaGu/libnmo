#include "test_framework.h"
#include "nmo.h"
#include "type/nmo_type_system.h"
#include "type/nmo_reflection.h"
#include "type/nmo_operations.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_struct_guids.h"

#include <stdalign.h>
#include <string.h>

#define TEST_GUID_ELEM      {0xDEAD0001, 0x00000000}
#define TEST_GUID_ELEM_INIT TEST_GUID_ELEM

typedef struct test_ptr_array_struct {
    uint32_t item_count;
    void *items;
} test_ptr_array_struct_t;

typedef struct raw_pointer_array_struct {
    uint32_t item_count;
    uint32_t *items;
} raw_pointer_array_struct_t;

typedef struct multiplied_array_struct {
    uint32_t face_count;
    uint16_t *indices;
} multiplied_array_struct_t;

static const nmo_type_field_t test_fields[] = {
    NMO_FIELD(test_ptr_array_struct_t, item_count, CKPGUID_UINT32),
    NMO_FIELD_PTR_ARRAY(test_ptr_array_struct_t, items, item_count, TEST_GUID_ELEM),
};

static const nmo_type_field_t multiplied_fields[] = {
    NMO_FIELD(multiplied_array_struct_t, face_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(multiplied_array_struct_t, indices, face_count, 3, CKPGUID_UINT16),
};

TEST(count_field_meta, ptr_array_has_count_field_name) {
    const nmo_type_field_t *items_field = &test_fields[1];
    ASSERT_NOT_NULL(items_field->count_field_name);
    ASSERT_EQ(0, strcmp(items_field->count_field_name, "item_count"));
}

TEST(count_field_meta, non_ptr_array_has_null_count) {
    const nmo_type_field_t *count_field = &test_fields[0];
    ASSERT_NULL(count_field->count_field_name);
}

static nmo_type_descriptor_t make_test_type(const nmo_type_field_t *fields, size_t count) {
    nmo_type_descriptor_t t;
    memset(&t, 0, sizeof(t));
    t.fields = fields;
    t.field_count = (uint32_t)count;
    return t;
}

TEST(count_field_meta, get_count_field_resolves_metadata) {
    nmo_type_descriptor_t type = make_test_type(test_fields, 2);
    const nmo_type_field_t *items = &test_fields[1];
    const nmo_type_field_t *cf = nmo_field_get_count_field(&type, items);
    ASSERT_NOT_NULL(cf);
    ASSERT_EQ(0, strcmp(cf->name, "item_count"));
}

TEST(count_field_meta, get_count_field_null_for_null_type) {
    const nmo_type_field_t *items = &test_fields[1];
    const nmo_type_field_t *cf = nmo_field_get_count_field(NULL, items);
    ASSERT_NULL(cf);
}

TEST(count_field_meta, resolve_count_uses_metadata_value) {
    nmo_type_descriptor_t type = make_test_type(test_fields, 2);
    test_ptr_array_struct_t instance;
    memset(&instance, 0, sizeof(instance));
    instance.item_count = 7;

    uint32_t count = 0;
    ASSERT_EQ(NMO_OK, nmo_field_resolve_count(
                          &type, &test_fields[1], &instance, &count));
    ASSERT_EQ(7u, count);
}

TEST(count_field_meta, resolve_count_applies_multiplier) {
    nmo_type_descriptor_t type = make_test_type(multiplied_fields, 2);
    multiplied_array_struct_t instance;
    memset(&instance, 0, sizeof(instance));
    instance.face_count = 7;

    uint32_t count = 0;
    ASSERT_EQ(NMO_OK, nmo_field_resolve_count(
                          &type, &multiplied_fields[1], &instance, &count));
    ASSERT_EQ(21u, count);
}

TEST(count_field_meta, counted_array_records_storage_element_size) {
    ASSERT_EQ(sizeof(uint16_t), multiplied_fields[1].element_size);

    nmo_type_descriptor_t uint16_type = {
        .size = sizeof(uint16_t),
    };
    ASSERT_EQ(sizeof(uint16_t),
              nmo_field_resolve_element_size(
                  &multiplied_fields[1], &uint16_type));
}

TEST(count_field_meta, unresolved_element_size_does_not_guess) {
    ASSERT_EQ(0u, test_fields[1].element_size);
    ASSERT_EQ(0u, nmo_field_resolve_element_size(&test_fields[1], NULL));

    nmo_type_descriptor_t element_type = {
        .size = 12u,
    };
    ASSERT_EQ(12u, nmo_field_resolve_element_size(
                       &test_fields[1], &element_type));
}

TEST(count_field_meta, resolve_count_field_returns_metadata_field) {
    nmo_type_descriptor_t type = make_test_type(test_fields, 2);
    const nmo_type_field_t *count_field =
        nmo_field_resolve_count_field(&type, &test_fields[1]);

    ASSERT_NOT_NULL(count_field);
    ASSERT_EQ(0, strcmp(count_field->name, "item_count"));
}

TEST(count_field_meta, resolve_count_reports_missing_count_field) {
    typedef struct missing_count_state {
        void *items;
    } missing_count_state_t;

    nmo_type_field_t fields[] = {
        {
            .name = "items",
            .type_guid = TEST_GUID_ELEM_INIT,
            .offset = (uint32_t)offsetof(missing_count_state_t, items),
            .size = sizeof(void *),
            .flags = NMO_FIELD_POINTER | NMO_FIELD_REPEATED,
            .count_field_name = NULL,
        },
    };
    nmo_type_descriptor_t type = make_test_type(fields, 1);
    missing_count_state_t instance;
    memset(&instance, 0, sizeof(instance));

    uint32_t count = 99;
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_field_resolve_count(
                                      &type, &fields[0], &instance, &count));
    ASSERT_EQ(0u, count);
}

TEST(count_field_meta, registry_rejects_raw_pointer_array_without_count_metadata) {
    nmo_type_field_t fields[] = {
        NMO_FIELD(raw_pointer_array_struct_t, item_count, CKPGUID_UINT32),
        {
            .name = "items",
            .type_guid = CKPGUID_UINT32_INIT,
            .offset = (uint32_t)offsetof(raw_pointer_array_struct_t, items),
            .size = sizeof(uint32_t *),
            .flags = NMO_FIELD_POINTER | NMO_FIELD_REPEATED,
            .count_field_name = NULL,
        },
    };
    nmo_type_descriptor_t desc = {
        .guid = NMO_GUID(0xABCDEF12u, 0x34567890u),
        .id = NMO_TYPE_ID_INVALID,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .name = "RawPointerArrayMissingCount",
        .size = (uint32_t)sizeof(raw_pointer_array_struct_t),
        .alignment = (uint32_t)alignof(raw_pointer_array_struct_t),
        .fields = fields,
        .field_count = sizeof(fields) / sizeof(fields[0]),
        .base_type = NMO_NULL_GUID,
        .base_type_id = NMO_TYPE_ID_INVALID,
        .creator_plugin_guid = NMO_NULL_GUID,
        .specialized_index = NMO_SPECIALIZED_INDEX_INVALID,
        .valid = true,
    };

    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NOT_NULL(registry);
    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    nmo_status_t status = nmo_type_registry_register(registry, &desc);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, status);

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

TEST(count_field_meta, ckmesh_raw_pointer_arrays_declare_count_metadata) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NOT_NULL(registry);
    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));
    ASSERT_EQ(NMO_OK, nmo_register_mesh_type(registry));

    const nmo_type_descriptor_t *mesh =
        nmo_type_registry_find_by_class_id(registry, NMO_CID_MESH);
    ASSERT_NOT_NULL(mesh);

    const nmo_type_field_t *faces = nmo_type_get_field_by_name(mesh, "faces");
    ASSERT_NOT_NULL(faces);
    ASSERT_NOT_NULL(faces->count_field_name);
    ASSERT_EQ(0, strcmp(faces->count_field_name, "face_count"));
    ASSERT_EQ(1u, faces->count_multiplier);

    const nmo_type_field_t *indices = nmo_type_get_field_by_name(mesh, "face_vertex_indices");
    ASSERT_NOT_NULL(indices);
    ASSERT_NOT_NULL(indices->count_field_name);
    ASSERT_EQ(0, strcmp(indices->count_field_name, "face_count"));
    ASSERT_EQ(3u, indices->count_multiplier);

    const nmo_type_field_t *lines = nmo_type_get_field_by_name(mesh, "line_indices");
    ASSERT_NOT_NULL(lines);
    ASSERT_NOT_NULL(lines->count_field_name);
    ASSERT_EQ(0, strcmp(lines->count_field_name, "line_count"));
    ASSERT_EQ(2u, lines->count_multiplier);

    const nmo_type_field_t *vertices = nmo_type_get_field_by_name(mesh, "vertices");
    ASSERT_NOT_NULL(vertices);
    ASSERT_NOT_NULL(vertices->count_field_name);
    ASSERT_EQ(0, strcmp(vertices->count_field_name, "vertex_count"));
    ASSERT_EQ(1u, vertices->count_multiplier);

    const nmo_type_field_t *colors =
        nmo_type_get_field_by_name(mesh, "vertex_colors");
    ASSERT_NOT_NULL(colors);
    ASSERT_EQ(sizeof(uint32_t), colors->element_size);

    const nmo_type_field_t *specular =
        nmo_type_get_field_by_name(mesh, "vertex_specular");
    ASSERT_NOT_NULL(specular);
    ASSERT_EQ(sizeof(uint32_t), specular->element_size);

    const nmo_type_field_t *groups = nmo_type_get_field_by_name(mesh, "material_groups");
    ASSERT_NOT_NULL(groups);
    ASSERT_NOT_NULL(groups->count_field_name);
    ASSERT_EQ(0, strcmp(groups->count_field_name, "material_group_count"));
    ASSERT_EQ(1u, groups->count_multiplier);

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

TEST(count_field_meta, dynamic_struct_pointer_arrays_record_storage_size) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);

    const nmo_type_descriptor_t *bitmap =
        nmo_type_registry_find_by_guid(
            registry, NMO_GUID_STRUCT_CKBITMAPDATA);
    ASSERT_NOT_NULL(bitmap);
    const nmo_type_field_t *pixels =
        nmo_type_get_field_by_name(bitmap, "pixel_data");
    ASSERT_NOT_NULL(pixels);
    ASSERT_EQ(sizeof(uint8_t), pixels->element_size);

    const nmo_type_descriptor_t *skin_vertex =
        nmo_type_registry_find_by_guid(
            registry, CKPGUID_CK3DENTITYSKINVERTEX);
    ASSERT_NOT_NULL(skin_vertex);
    const nmo_type_field_t *bone_indices =
        nmo_type_get_field_by_name(skin_vertex, "bone_indices");
    ASSERT_NOT_NULL(bone_indices);
    ASSERT_EQ(sizeof(uint32_t), bone_indices->element_size);

    nmo_context_release(ctx);
}

TEST(count_field_meta, keyedanimation_raw_pointer_arrays_declare_count_metadata) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NOT_NULL(registry);
    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));
    ASSERT_EQ(NMO_OK, nmo_register_animation_type(registry));
    ASSERT_EQ(NMO_OK, nmo_register_keyedanimation_type(registry));

    const nmo_type_descriptor_t *keyed =
        nmo_type_registry_find_by_class_id(registry, NMO_CID_KEYEDANIMATION);
    ASSERT_NOT_NULL(keyed);

    const nmo_type_field_t *animation_ids =
        nmo_type_get_field_by_name(keyed, "animation_ids");
    ASSERT_NOT_NULL(animation_ids);
    ASSERT_NOT_NULL(animation_ids->count_field_name);
    ASSERT_EQ(0, strcmp(animation_ids->count_field_name, "animation_count"));
    ASSERT_EQ(1u, animation_ids->count_multiplier);

    const nmo_type_field_t *subanims =
        nmo_type_get_field_by_name(keyed, "subanims");
    ASSERT_NOT_NULL(subanims);
    ASSERT_NOT_NULL(subanims->count_field_name);
    ASSERT_EQ(0, strcmp(subanims->count_field_name, "subanim_count"));
    ASSERT_EQ(1u, subanims->count_multiplier);

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(count_field_meta, ptr_array_has_count_field_name);
    REGISTER_TEST(count_field_meta, non_ptr_array_has_null_count);
    REGISTER_TEST(count_field_meta, get_count_field_resolves_metadata);
    REGISTER_TEST(count_field_meta, get_count_field_null_for_null_type);
    REGISTER_TEST(count_field_meta, resolve_count_uses_metadata_value);
    REGISTER_TEST(count_field_meta, resolve_count_applies_multiplier);
    REGISTER_TEST(count_field_meta, counted_array_records_storage_element_size);
    REGISTER_TEST(count_field_meta, unresolved_element_size_does_not_guess);
    REGISTER_TEST(count_field_meta, resolve_count_field_returns_metadata_field);
    REGISTER_TEST(count_field_meta, resolve_count_reports_missing_count_field);
    REGISTER_TEST(count_field_meta, registry_rejects_raw_pointer_array_without_count_metadata);
    REGISTER_TEST(count_field_meta, ckmesh_raw_pointer_arrays_declare_count_metadata);
    REGISTER_TEST(count_field_meta, dynamic_struct_pointer_arrays_record_storage_size);
    REGISTER_TEST(count_field_meta, keyedanimation_raw_pointer_arrays_declare_count_metadata);
TEST_MAIN_END()
