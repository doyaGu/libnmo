#include "test_framework.h"
#include "session/nmo_context.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_view.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "format/nmo_object.h"

TEST(type_view, from_class_id_returns_stable_snapshot) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);

    nmo_type_view_t view;
    ASSERT_EQ(NMO_OK, nmo_type_view_from_class_id(registry, NMO_CID_OBJECT, &view));
    ASSERT_EQ(NMO_CID_OBJECT, view.class_id);
    ASSERT_TRUE(nmo_guid_equals(view.guid, CKPGUID_OBJECT));
    ASSERT_STR_EQ(nmo_type_registry_guid_to_name(registry, CKPGUID_OBJECT), view.name);
    ASSERT_TRUE(view.has_reflection);
    ASSERT_TRUE(view.ui_visible);
    ASSERT_TRUE(view.type_id != NMO_TYPE_ID_INVALID);

    nmo_context_release(ctx);
}

TEST(type_view, from_guid_reports_scalar_metadata_without_descriptor_pointer) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);

    nmo_type_view_t view;
    ASSERT_EQ(NMO_OK, nmo_type_view_from_guid(registry, CKPGUID_FLOAT, &view));
    ASSERT_TRUE(nmo_guid_equals(view.guid, CKPGUID_FLOAT));
    ASSERT_STR_EQ(nmo_type_registry_guid_to_name(registry, CKPGUID_FLOAT), view.name);
    ASSERT_TRUE((view.category & NMO_TYPE_CATEGORY_SCALAR) != 0);
    ASSERT_TRUE(view.size == sizeof(float));
    ASSERT_FALSE(view.has_reflection);

    nmo_context_release(ctx);
}

TEST(type_view, from_type_id_returns_stable_snapshot) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);

    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_guid(registry, CKPGUID_FLOAT);
    ASSERT_NOT_NULL(type);

    nmo_type_view_t view;
    ASSERT_EQ(NMO_OK, nmo_type_view_from_type_id(registry, type->id, &view));
    ASSERT_EQ(type->id, view.type_id);
    ASSERT_TRUE(nmo_guid_equals(view.guid, CKPGUID_FLOAT));
    ASSERT_STR_EQ(nmo_type_registry_guid_to_name(registry, CKPGUID_FLOAT), view.name);
    ASSERT_FALSE(view.has_reflection);

    nmo_context_release(ctx);
}

TEST(type_view, missing_guid_returns_not_found_and_clears_view) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);

    nmo_type_view_t view = {
        .type_id = 123,
        .class_id = 456,
        .name = "stale"
    };
    nmo_guid_t missing = NMO_GUID_INIT(0xdeadbeefu, 0x01020304u);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_type_view_from_guid(registry, missing, &view));
    ASSERT_EQ(NMO_TYPE_ID_INVALID, view.type_id);
    ASSERT_EQ(0u, view.class_id);
    ASSERT_NULL(view.name);
    ASSERT_TRUE(nmo_guid_is_null(view.guid));

    nmo_context_release(ctx);
}

TEST(type_view, from_object_prefers_explicit_type_guid_metadata) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);

    nmo_object_t *object = nmo_object_create(NULL, 101, NMO_CID_PARAMETERLOCAL);
    ASSERT_NOT_NULL(object);
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(object, CKPGUID_FLOAT));

    nmo_type_view_t view;
    ASSERT_EQ(NMO_OK, nmo_type_view_from_object(registry, object, &view));
    ASSERT_TRUE(nmo_guid_equals(view.guid, CKPGUID_FLOAT));
    ASSERT_STR_EQ(nmo_type_registry_guid_to_name(registry, CKPGUID_FLOAT), view.name);
    ASSERT_FALSE(view.has_reflection);

    nmo_object_destroy(object);
    nmo_context_release(ctx);
}

TEST(type_view, from_object_falls_back_to_class_metadata_when_type_guid_missing) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);

    nmo_object_t *object = nmo_object_create(NULL, 102, NMO_CID_CAMERA);
    ASSERT_NOT_NULL(object);

    nmo_type_view_t view;
    ASSERT_EQ(NMO_OK, nmo_type_view_from_object(registry, object, &view));
    ASSERT_EQ(NMO_CID_CAMERA, view.class_id);
    ASSERT_TRUE(nmo_guid_equals(view.guid, CKPGUID_CAMERA));
    ASSERT_STR_EQ(nmo_type_registry_guid_to_name(registry, CKPGUID_CAMERA), view.name);
    ASSERT_TRUE(view.has_reflection);

    nmo_object_destroy(object);
    nmo_context_release(ctx);
}

TEST(type_view, from_guid_hides_invalidated_type) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NOT_NULL(registry);

    nmo_guid_t guid = NMO_GUID_INIT(0x0A0B0C0Du, 0x01020304u);
    nmo_type_descriptor_t type = {0};
    type.guid = guid;
    type.name = "TransientType";
    type.category = NMO_TYPE_CATEGORY_SCALAR;
    type.size = sizeof(uint32_t);
    type.alignment = _Alignof(uint32_t);
    type.valid = true;

    ASSERT_EQ(NMO_OK, nmo_type_registry_register(registry, &type));

    nmo_type_view_t view;
    ASSERT_EQ(NMO_OK, nmo_type_view_from_guid(registry, guid, &view));
    ASSERT_TRUE(nmo_guid_equals(guid, view.guid));

    ASSERT_EQ(NMO_OK, nmo_type_registry_invalidate(registry, guid));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_type_view_from_guid(registry, guid, &view));
    ASSERT_EQ(NMO_TYPE_ID_INVALID, view.type_id);
    ASSERT_NULL(view.name);
    ASSERT_TRUE(nmo_guid_is_null(view.guid));

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

TEST(type_view, from_type_id_hides_invalidated_type) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NOT_NULL(registry);

    nmo_guid_t guid = NMO_GUID_INIT(0x0E0F1011u, 0x11121314u);
    nmo_type_descriptor_t type = {0};
    type.guid = guid;
    type.name = "TransientTypeById";
    type.category = NMO_TYPE_CATEGORY_SCALAR;
    type.size = sizeof(uint32_t);
    type.alignment = _Alignof(uint32_t);
    type.valid = true;
    ASSERT_EQ(NMO_OK, nmo_type_registry_register(registry, &type));

    const nmo_type_descriptor_t *registered =
        nmo_type_registry_find_by_guid(registry, guid);
    ASSERT_NOT_NULL(registered);

    nmo_type_view_t view;
    ASSERT_EQ(NMO_OK, nmo_type_view_from_type_id(registry, registered->id, &view));
    ASSERT_TRUE(nmo_guid_equals(guid, view.guid));

    ASSERT_EQ(NMO_OK, nmo_type_registry_invalidate(registry, guid));
    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_type_view_from_type_id(registry, registered->id, &view));
    ASSERT_EQ(NMO_TYPE_ID_INVALID, view.type_id);
    ASSERT_NULL(view.name);
    ASSERT_TRUE(nmo_guid_is_null(view.guid));

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

TEST(type_view, from_object_falls_back_when_explicit_type_guid_is_not_visible) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);

    nmo_guid_t missing_guid = NMO_GUID_INIT(0x0BADF00Du, 0x00010002u);
    nmo_object_t *object = nmo_object_create(NULL, 103, NMO_CID_CAMERA);
    ASSERT_NOT_NULL(object);
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(object, missing_guid));

    nmo_type_view_t view;
    ASSERT_EQ(NMO_OK, nmo_type_view_from_object(registry, object, &view));
    ASSERT_EQ(NMO_CID_CAMERA, view.class_id);
    ASSERT_TRUE(nmo_guid_equals(view.guid, CKPGUID_CAMERA));
    ASSERT_STR_EQ(nmo_type_registry_guid_to_name(registry, CKPGUID_CAMERA), view.name);
    ASSERT_TRUE(view.has_reflection);

    nmo_object_destroy(object);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(type_view, from_class_id_returns_stable_snapshot);
    REGISTER_TEST(type_view, from_guid_reports_scalar_metadata_without_descriptor_pointer);
    REGISTER_TEST(type_view, from_type_id_returns_stable_snapshot);
    REGISTER_TEST(type_view, missing_guid_returns_not_found_and_clears_view);
    REGISTER_TEST(type_view, from_object_prefers_explicit_type_guid_metadata);
    REGISTER_TEST(type_view, from_object_falls_back_to_class_metadata_when_type_guid_missing);
    REGISTER_TEST(type_view, from_guid_hides_invalidated_type);
    REGISTER_TEST(type_view, from_type_id_hides_invalidated_type);
    REGISTER_TEST(type_view, from_object_falls_back_when_explicit_type_guid_is_not_visible);
TEST_MAIN_END()
