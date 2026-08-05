/**
 * @file test_object_import_api.c
 * @brief Direct tests for owner-level object import API.
 */

#include "test_framework.h"
#include "nmo.h"

#include "document/nmo_document.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_sound_schemas.h"
#include "runtime/nmo_workspace.h"
#include "session/nmo_session.h"
#include "session/nmo_session_pipeline.h"
#include "../../src/runtime/runtime_internal.h"
#include "type/nmo_reflection.h"

#include <stdalign.h>
#include <stdio.h>
#include <string.h>

#define IMPORT_RAW_ARRAY_GUID_INIT NMO_GUID_INIT(0x51A4E002u, 0x00000001u)
#define IMPORT_RAW_ARRAY_CLASS_ID  0x51A4E002u
#define IMPORT_INLINE_ARRAY_GUID_INIT NMO_GUID_INIT(0x51A4E003u, 0x00000001u)
#define IMPORT_INLINE_ARRAY_CLASS_ID  0x51A4E003u
#define IMPORT_COUNTED_ARRAY_GUID_INIT NMO_GUID_INIT(0x51A4E004u, 0x00000001u)
#define IMPORT_COUNTED_ARRAY_CLASS_ID  0x51A4E004u

typedef struct import_raw_array_state {
    uint32_t item_count;
    uint32_t *items;
} import_raw_array_state_t;

typedef struct import_inline_array_state {
    nmo_array_t values;
} import_inline_array_state_t;

typedef struct import_counted_array_state {
    uint32_t face_count;
    uint16_t *indices;
} import_counted_array_state_t;

typedef struct import_api_fixture {
    nmo_context_t *ctx;
    nmo_document_t *document;
    nmo_workspace_t *workspace;
    nmo_session_t *session;
    nmo_type_registry_t *registry;
} import_api_fixture_t;

static const nmo_type_field_t import_raw_array_fields[] = {
    NMO_FIELD(import_raw_array_state_t, item_count, CKPGUID_UINT32),
    NMO_FIELD_PTR_ARRAY(import_raw_array_state_t, items, item_count, CKPGUID_UINT32),
};

static const nmo_type_field_t import_inline_array_fields[] = {
    NMO_FIELD_ARRAY(import_inline_array_state_t, values, CKPGUID_UINT32),
};

static const nmo_type_field_t import_counted_array_fields[] = {
    NMO_FIELD(import_counted_array_state_t, face_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(import_counted_array_state_t, indices, face_count, 3, CKPGUID_UINT16),
};

static bool import_api_fixture_init(import_api_fixture_t *fixture)
{
    if (fixture == NULL) {
        return false;
    }

    memset(fixture, 0, sizeof(*fixture));
    fixture->ctx = nmo_context_create(NULL);
    if (fixture->ctx == NULL) {
        return false;
    }
    fixture->document = nmo_document_create(fixture->ctx);
    if (fixture->document == NULL) {
        nmo_context_release(fixture->ctx);
        memset(fixture, 0, sizeof(*fixture));
        return false;
    }
    if (nmo_workspace_create(fixture->ctx, fixture->document, &fixture->workspace) != NMO_OK ||
        fixture->workspace == NULL) {
        nmo_document_destroy(fixture->document);
        nmo_context_release(fixture->ctx);
        memset(fixture, 0, sizeof(*fixture));
        return false;
    }
    fixture->session = nmo_workspace_internal_session(fixture->workspace);
    fixture->registry = nmo_context_get_type_registry(fixture->ctx);
    if (fixture->session == NULL || fixture->registry == NULL) {
        nmo_workspace_destroy(fixture->workspace);
        nmo_document_destroy(fixture->document);
        nmo_context_release(fixture->ctx);
        memset(fixture, 0, sizeof(*fixture));
        return false;
    }
    return true;
}

static void import_api_fixture_destroy(import_api_fixture_t *fixture)
{
    if (fixture == NULL) {
        return;
    }
    if (fixture->workspace != NULL) {
        nmo_workspace_destroy(fixture->workspace);
    }
    if (fixture->document != NULL) {
        nmo_document_destroy(fixture->document);
    }
    if (fixture->ctx != NULL) {
        nmo_context_release(fixture->ctx);
    }
    memset(fixture, 0, sizeof(*fixture));
}

static nmo_status_t dummy_serialize(
    const void *instance,
    struct nmo_chunk *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)chunk;
    (void)type;
    (void)context;
    return NMO_OK;
}

static nmo_status_t dummy_deserialize(
    void *instance,
    struct nmo_chunk *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)chunk;
    (void)type;
    (void)context;
    return NMO_OK;
}

static const nmo_type_vtable_t import_raw_array_vtable = {
    .serialize = dummy_serialize,
    .deserialize = dummy_deserialize,
};

static bool break_registered_count_metadata(nmo_type_registry_t *registry)
{
    const nmo_guid_t guid = IMPORT_RAW_ARRAY_GUID_INIT;
    const nmo_type_descriptor_t *registered =
        nmo_type_registry_find_by_guid(registry, guid);
    if (!registered || registered->field_count != 2u) {
        return false;
    }

    nmo_type_field_t *fields = (nmo_type_field_t *)(void *)registered->fields;
    fields[1].count_field_name = NULL;
    return true;
}

static nmo_object_t *create_import_test_object(nmo_session_t *session)
{
    nmo_object_t *obj = nmo_object_create(NULL, 9001u, IMPORT_RAW_ARRAY_CLASS_ID);
    if (!obj) {
        return NULL;
    }
    if (nmo_object_alloc_state(obj, sizeof(import_raw_array_state_t)) != NMO_OK) {
        nmo_object_destroy(obj);
        return NULL;
    }
    nmo_object_t *owned = obj;
    if (nmo_object_repository_add(nmo_session_get_repository(session), &owned) != NMO_OK) {
        nmo_object_destroy(obj);
        return NULL;
    }
    if (owned != NULL) {
        nmo_object_destroy(owned);
        return NULL;
    }
    return nmo_object_repository_find_by_id(nmo_session_get_repository(session), 9001u);
}

static nmo_object_t *create_import_inline_array_object(nmo_session_t *session)
{
    nmo_object_t *obj = nmo_object_create(NULL, 9101u, IMPORT_INLINE_ARRAY_CLASS_ID);
    if (!obj) {
        return NULL;
    }
    if (nmo_object_alloc_state(obj, sizeof(import_inline_array_state_t)) != NMO_OK) {
        nmo_object_destroy(obj);
        return NULL;
    }

    import_inline_array_state_t *state = (import_inline_array_state_t *)nmo_object_get_state(obj);
    if (!state || nmo_array_init(&state->values, sizeof(uint32_t), 0, NULL) != NMO_OK) {
        nmo_object_destroy(obj);
        return NULL;
    }

    uint32_t existing = 7u;
    if (nmo_array_append(&state->values, &existing) != NMO_OK) {
        nmo_array_dispose(&state->values);
        nmo_object_destroy(obj);
        return NULL;
    }

    nmo_object_t *owned = obj;
    if (nmo_object_repository_add(nmo_session_get_repository(session), &owned) != NMO_OK) {
        nmo_array_dispose(&state->values);
        nmo_object_destroy(obj);
        return NULL;
    }
    if (owned != NULL) {
        nmo_array_dispose(&state->values);
        nmo_object_destroy(owned);
        return NULL;
    }
    return nmo_object_repository_find_by_id(nmo_session_get_repository(session), 9101u);
}

static nmo_object_t *create_import_counted_array_object(nmo_session_t *session)
{
    nmo_object_t *obj = nmo_object_create(NULL, 9201u, IMPORT_COUNTED_ARRAY_CLASS_ID);
    if (!obj) {
        return NULL;
    }
    if (nmo_object_alloc_state(obj, sizeof(import_counted_array_state_t)) != NMO_OK) {
        nmo_object_destroy(obj);
        return NULL;
    }
    nmo_object_t *owned = obj;
    if (nmo_object_repository_add(nmo_session_get_repository(session), &owned) != NMO_OK) {
        nmo_object_destroy(obj);
        return NULL;
    }
    if (owned != NULL) {
        nmo_object_destroy(owned);
        return NULL;
    }
    return nmo_object_repository_find_by_id(nmo_session_get_repository(session), 9201u);
}

static bool register_import_raw_array_type(nmo_type_registry_t *registry)
{
    nmo_type_descriptor_t desc = {
        .guid = IMPORT_RAW_ARRAY_GUID_INIT,
        .id = NMO_TYPE_ID_INVALID,
        .class_id = IMPORT_RAW_ARRAY_CLASS_ID,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = 0,
        .name = "ImportRawArrayState",
        .description = NULL,
        .base_type = NMO_NULL_GUID,
        .base_type_id = NMO_TYPE_ID_INVALID,
        .size = (uint32_t)sizeof(import_raw_array_state_t),
        .alignment = (uint32_t)alignof(import_raw_array_state_t),
        .fields = import_raw_array_fields,
        .field_count = sizeof(import_raw_array_fields) / sizeof(import_raw_array_fields[0]),
        .vtable = &import_raw_array_vtable,
        .creator_plugin_guid = NMO_NULL_GUID,
        .saver_manager = 0,
        .specialized_index = NMO_SPECIALIZED_INDEX_INVALID,
        .valid = true,
        .version = 0,
        .min_compatible_version = 0,
        .ext = NULL,
    };
    nmo_status_t status = nmo_type_registry_register(registry, &desc);
    return status == NMO_OK;
}

static bool register_import_inline_array_type(nmo_type_registry_t *registry)
{
    nmo_type_descriptor_t desc = {
        .guid = IMPORT_INLINE_ARRAY_GUID_INIT,
        .id = NMO_TYPE_ID_INVALID,
        .class_id = IMPORT_INLINE_ARRAY_CLASS_ID,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = 0,
        .name = "ImportInlineArrayState",
        .description = NULL,
        .base_type = NMO_NULL_GUID,
        .base_type_id = NMO_TYPE_ID_INVALID,
        .size = (uint32_t)sizeof(import_inline_array_state_t),
        .alignment = (uint32_t)alignof(import_inline_array_state_t),
        .fields = import_inline_array_fields,
        .field_count = sizeof(import_inline_array_fields) / sizeof(import_inline_array_fields[0]),
        .vtable = &import_raw_array_vtable,
        .creator_plugin_guid = NMO_NULL_GUID,
        .saver_manager = 0,
        .specialized_index = NMO_SPECIALIZED_INDEX_INVALID,
        .valid = true,
        .version = 0,
        .min_compatible_version = 0,
        .ext = NULL,
    };
    nmo_status_t status = nmo_type_registry_register(registry, &desc);
    return status == NMO_OK;
}

static bool register_import_counted_array_type(nmo_type_registry_t *registry)
{
    nmo_type_descriptor_t desc = {
        .guid = IMPORT_COUNTED_ARRAY_GUID_INIT,
        .id = NMO_TYPE_ID_INVALID,
        .class_id = IMPORT_COUNTED_ARRAY_CLASS_ID,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = 0,
        .name = "ImportCountedArrayState",
        .description = NULL,
        .base_type = NMO_NULL_GUID,
        .base_type_id = NMO_TYPE_ID_INVALID,
        .size = (uint32_t)sizeof(import_counted_array_state_t),
        .alignment = (uint32_t)alignof(import_counted_array_state_t),
        .fields = import_counted_array_fields,
        .field_count = sizeof(import_counted_array_fields) / sizeof(import_counted_array_fields[0]),
        .vtable = &import_raw_array_vtable,
        .creator_plugin_guid = NMO_NULL_GUID,
        .saver_manager = 0,
        .specialized_index = NMO_SPECIALIZED_INDEX_INVALID,
        .valid = true,
        .version = 0,
        .min_compatible_version = 0,
        .ext = NULL,
    };
    nmo_status_t status = nmo_type_registry_register(registry, &desc);
    return status == NMO_OK;
}

TEST(object_import_api, raw_pointer_array_missing_count_metadata_does_not_mutate) {
    import_api_fixture_t fixture;
    ASSERT_TRUE(import_api_fixture_init(&fixture));

    nmo_type_registry_t *registry = fixture.registry;
    nmo_session_t *session = fixture.session;
    nmo_status_t status = nmo_type_registry_begin_update(registry);
    ASSERT_EQ(NMO_OK, status);

    ASSERT_TRUE(register_import_raw_array_type(registry));
    ASSERT_TRUE(break_registered_count_metadata(registry));

    nmo_object_t *obj = create_import_test_object(session);
    ASSERT_NOT_NULL(obj);
    import_raw_array_state_t *state = (import_raw_array_state_t *)nmo_object_get_state(obj);
    ASSERT_NOT_NULL(state);
    ASSERT_EQ(0u, state->item_count);
    ASSERT_NULL(state->items);

    const char json[] =
        "{\"objects\":[{\"id\":9001,\"fields\":["
        "{\"name\":\"items\",\"kind\":\"array\",\"type_guid\":\"{4E4D4F03-00200000}\","
        "\"count\":2,\"value\":[1,2],\"items\":[1,2]}]}]}";
    nmo_import_result_t result;
    status = nmo_object_edit_import_json(fixture.workspace, json, 0, 0, &result);

    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(1u, result.objects_updated);
    ASSERT_EQ(0u, result.fields_written);
    ASSERT_EQ(1u, result.errors);
    ASSERT_EQ(0u, state->item_count);
    ASSERT_NULL(state->items);

    import_api_fixture_destroy(&fixture);
}

TEST(object_import_api, old_flat_map_schema_is_rejected) {
    import_api_fixture_t fixture;
    ASSERT_TRUE(import_api_fixture_init(&fixture));

    nmo_type_registry_t *registry = fixture.registry;
    nmo_session_t *session = fixture.session;
    nmo_status_t status = nmo_type_registry_begin_update(registry);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_TRUE(register_import_raw_array_type(registry));
    ASSERT_NOT_NULL(create_import_test_object(session));

    const char json[] =
        "{\"objects\":[{\"id\":9001,\"fields\":{\"items\":[1,2]}}]}";
    nmo_import_result_t result;
    status = nmo_object_edit_import_json(fixture.workspace, json, 0, 0, &result);

    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, status);

    import_api_fixture_destroy(&fixture);
}

TEST(object_import_api, old_value_str_bridge_schema_is_rejected) {
    import_api_fixture_t fixture;
    ASSERT_TRUE(import_api_fixture_init(&fixture));

    nmo_type_registry_t *registry = fixture.registry;
    nmo_session_t *session = fixture.session;
    nmo_status_t status = nmo_type_registry_begin_update(registry);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_TRUE(register_import_raw_array_type(registry));
    ASSERT_NOT_NULL(create_import_test_object(session));

    const char json[] =
        "{\"objects\":[{\"id\":9001,\"fields\":{\"fields\":["
        "{\"name\":\"item_count\",\"value_str\":\"2\"}]}}]}";
    nmo_import_result_t result;
    status = nmo_object_edit_import_json(fixture.workspace, json, 0, 0, &result);

    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, status);

    import_api_fixture_destroy(&fixture);
}

TEST(object_import_api, snapshot_raw_pointer_array_imports_all_items) {
    import_api_fixture_t fixture;
    ASSERT_TRUE(import_api_fixture_init(&fixture));

    nmo_type_registry_t *registry = fixture.registry;
    nmo_session_t *session = fixture.session;
    nmo_status_t status = nmo_type_registry_begin_update(registry);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_TRUE(register_import_raw_array_type(registry));

    nmo_object_t *obj = create_import_test_object(session);
    ASSERT_NOT_NULL(obj);
    import_raw_array_state_t *state = (import_raw_array_state_t *)nmo_object_get_state(obj);
    ASSERT_NOT_NULL(state);

    const char json[] =
        "{\"objects\":[{\"id\":9001,\"fields\":["
        "{\"name\":\"items\",\"kind\":\"array\",\"type_guid\":\"{4E4D4F03-00200000}\","
        "\"count\":3,\"value\":[11,22,33],\"items\":[11,22,33]}]}]}";
    nmo_import_result_t result;
    status = nmo_object_edit_import_json(fixture.workspace, json, 0, 0, &result);

    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(1u, result.objects_updated);
    ASSERT_EQ(1u, result.fields_written);
    ASSERT_EQ(0u, result.errors);
    ASSERT_EQ(3u, state->item_count);
    ASSERT_NOT_NULL(state->items);
    ASSERT_EQ(11u, state->items[0]);
    ASSERT_EQ(22u, state->items[1]);
    ASSERT_EQ(33u, state->items[2]);

    import_api_fixture_destroy(&fixture);
}

TEST(object_import_api, counted_raw_pointer_array_imports_items_over_raw_hex) {
    import_api_fixture_t fixture;
    ASSERT_TRUE(import_api_fixture_init(&fixture));

    nmo_type_registry_t *registry = fixture.registry;
    nmo_session_t *session = fixture.session;
    nmo_status_t status = nmo_type_registry_begin_update(registry);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_TRUE(register_import_counted_array_type(registry));

    nmo_object_t *obj = create_import_counted_array_object(session);
    ASSERT_NOT_NULL(obj);
    import_counted_array_state_t *state =
        (import_counted_array_state_t *)nmo_object_get_state(obj);
    ASSERT_NOT_NULL(state);

    const char json[] =
        "{\"objects\":[{\"id\":9201,\"fields\":["
        "{\"name\":\"indices\",\"kind\":\"array\",\"type_guid\":\"{4E4D4F03-00100000}\","
        "\"count\":6,\"value\":null,\"items\":[1,2,3,4,5,6],"
        "\"raw_hex\":\"090009000900090009000900\"}]}]}";
    nmo_import_result_t result;
    status = nmo_object_edit_import_json(fixture.workspace, json, 0, 0, &result);

    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(1u, result.objects_updated);
    ASSERT_EQ(1u, result.fields_written);
    ASSERT_EQ(0u, result.errors);
    ASSERT_EQ(2u, state->face_count);
    ASSERT_NOT_NULL(state->indices);
    ASSERT_EQ(1u, state->indices[0]);
    ASSERT_EQ(2u, state->indices[1]);
    ASSERT_EQ(3u, state->indices[2]);
    ASSERT_EQ(4u, state->indices[3]);
    ASSERT_EQ(5u, state->indices[4]);
    ASSERT_EQ(6u, state->indices[5]);

    import_api_fixture_destroy(&fixture);
}

TEST(object_import_api, raw_pointer_array_parse_failure_does_not_mutate) {
    import_api_fixture_t fixture;
    ASSERT_TRUE(import_api_fixture_init(&fixture));

    nmo_type_registry_t *registry = fixture.registry;
    nmo_session_t *session = fixture.session;
    nmo_status_t status = nmo_type_registry_begin_update(registry);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_TRUE(register_import_raw_array_type(registry));

    nmo_object_t *obj = create_import_test_object(session);
    ASSERT_NOT_NULL(obj);
    import_raw_array_state_t *state = (import_raw_array_state_t *)nmo_object_get_state(obj);
    ASSERT_NOT_NULL(state);
    ASSERT_EQ(0u, state->item_count);
    ASSERT_NULL(state->items);

    const char json[] =
        "{\"objects\":[{\"id\":9001,\"fields\":["
        "{\"name\":\"items\",\"kind\":\"array\",\"type_guid\":\"{4E4D4F03-00200000}\","
        "\"count\":2,\"value\":[1,\"bad\"],\"items\":[1,\"bad\"]}]}]}";
    nmo_import_result_t result;
    status = nmo_object_edit_import_json(fixture.workspace, json, 0, 0, &result);

    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, status);
    ASSERT_EQ(0u, result.objects_updated);
    ASSERT_EQ(0u, result.fields_written);
    ASSERT_EQ(1u, result.errors);
    ASSERT_EQ(0u, state->item_count);
    ASSERT_NULL(state->items);

    import_api_fixture_destroy(&fixture);
}

TEST(object_import_api, inline_array_parse_failure_does_not_mutate) {
    import_api_fixture_t fixture;
    ASSERT_TRUE(import_api_fixture_init(&fixture));

    nmo_type_registry_t *registry = fixture.registry;
    nmo_session_t *session = fixture.session;
    nmo_status_t status = nmo_type_registry_begin_update(registry);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_TRUE(register_import_inline_array_type(registry));

    nmo_object_t *obj = create_import_inline_array_object(session);
    ASSERT_NOT_NULL(obj);
    import_inline_array_state_t *state = (import_inline_array_state_t *)nmo_object_get_state(obj);
    ASSERT_NOT_NULL(state);
    ASSERT_EQ(1u, nmo_array_size(&state->values));
    uint32_t *existing = (uint32_t *)nmo_array_get(&state->values, 0);
    ASSERT_NOT_NULL(existing);
    ASSERT_EQ(7u, *existing);

    const char json[] =
        "{\"objects\":[{\"id\":9101,\"fields\":["
        "{\"name\":\"values\",\"kind\":\"array\",\"type_guid\":\"{4E4D4F03-00200000}\","
        "\"count\":2,\"value\":[1,\"bad\"],\"items\":[1,\"bad\"]}]}]}";
    nmo_import_result_t result;
    status = nmo_object_edit_import_json(fixture.workspace, json, 0, 0, &result);

    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, status);
    ASSERT_EQ(0u, result.objects_updated);
    ASSERT_EQ(0u, result.fields_written);
    ASSERT_EQ(1u, result.errors);
    ASSERT_EQ(1u, nmo_array_size(&state->values));
    existing = (uint32_t *)nmo_array_get(&state->values, 0);
    ASSERT_NOT_NULL(existing);
    ASSERT_EQ(7u, *existing);

    nmo_array_dispose(&state->values);
    import_api_fixture_destroy(&fixture);
}

TEST(object_import_api, snapshot_inline_array_imports_all_items) {
    import_api_fixture_t fixture;
    ASSERT_TRUE(import_api_fixture_init(&fixture));

    nmo_type_registry_t *registry = fixture.registry;
    nmo_session_t *session = fixture.session;
    nmo_status_t status = nmo_type_registry_begin_update(registry);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_TRUE(register_import_inline_array_type(registry));

    nmo_object_t *obj = create_import_inline_array_object(session);
    ASSERT_NOT_NULL(obj);
    import_inline_array_state_t *state = (import_inline_array_state_t *)nmo_object_get_state(obj);
    ASSERT_NOT_NULL(state);

    const char json[] =
        "{\"objects\":[{\"id\":9101,\"fields\":["
        "{\"name\":\"values\",\"kind\":\"array\",\"type_guid\":\"{4E4D4F03-00200000}\","
        "\"count\":3,\"value\":[3,4,5],\"items\":[3,4,5]}]}]}";
    nmo_import_result_t result;
    status = nmo_object_edit_import_json(fixture.workspace, json, 0, 0, &result);

    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(1u, result.objects_updated);
    ASSERT_EQ(1u, result.fields_written);
    ASSERT_EQ(0u, result.errors);
    ASSERT_EQ(3u, nmo_array_size(&state->values));
    uint32_t *v0 = (uint32_t *)nmo_array_get(&state->values, 0);
    uint32_t *v1 = (uint32_t *)nmo_array_get(&state->values, 1);
    uint32_t *v2 = (uint32_t *)nmo_array_get(&state->values, 2);
    ASSERT_NOT_NULL(v0);
    ASSERT_NOT_NULL(v1);
    ASSERT_NOT_NULL(v2);
    ASSERT_EQ(3u, *v0);
    ASSERT_EQ(4u, *v1);
    ASSERT_EQ(5u, *v2);

    nmo_array_dispose(&state->values);
    import_api_fixture_destroy(&fixture);
}

TEST(object_import_api, snapshot_disambiguates_inherited_fields) {
    import_api_fixture_t fixture;
    ASSERT_TRUE(import_api_fixture_init(&fixture));

    nmo_object_id_t object_id = 0;
    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        fixture.session,
        NMO_CID_WAVESOUND,
        "duplicate priority",
        NMO_GUID_NULL,
        &object_id,
        NULL));

    nmo_object_t *obj = nmo_object_repository_find_by_id(
        nmo_session_get_repository(fixture.session), object_id);
    ASSERT_NOT_NULL(obj);
    nmo_wavesound_state_t *state =
        (nmo_wavesound_state_t *)nmo_object_get_state(obj);
    ASSERT_NOT_NULL(state);

    char json[1024];
    int written = snprintf(
        json,
        sizeof(json),
        "{\"objects\":[{\"id\":%u,\"fields\":["
        "{\"name\":\"priority\",\"owner_type_guid\":\"{71D80779-402F42F3}\","
        "\"kind\":\"scalar\",\"type_guid\":\"{5A5716FD-44E276D7}\",\"value\":7},"
        "{\"name\":\"priority\",\"owner_type_guid\":\"{4BF74E5E-45F409EF}\","
        "\"kind\":\"scalar\",\"type_guid\":\"{47884C3F-432C2C20}\",\"value\":0.75}]}]}",
        object_id);
    ASSERT_TRUE(written > 0 && (size_t)written < sizeof(json));

    nmo_import_result_t result;
    nmo_status_t status = nmo_object_edit_import_json(
        fixture.workspace, json, (size_t)written, 0, &result);

    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(1u, result.objects_updated);
    ASSERT_EQ(2u, result.fields_written);
    ASSERT_EQ(0u, result.errors);
    ASSERT_EQ(7, state->base.base.priority);
    ASSERT_FLOAT_EQ(0.75f, state->priority, 0.0001f);

    import_api_fixture_destroy(&fixture);
}

TEST(object_import_api, object_owner_import_wrapper_imports_snapshot) {
    import_api_fixture_t fixture;
    ASSERT_TRUE(import_api_fixture_init(&fixture));

    nmo_type_registry_t *registry = fixture.registry;
    nmo_session_t *session = fixture.session;
    nmo_status_t status = nmo_type_registry_begin_update(registry);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_TRUE(register_import_inline_array_type(registry));

    nmo_object_t *obj = create_import_inline_array_object(session);
    ASSERT_NOT_NULL(obj);
    import_inline_array_state_t *state = (import_inline_array_state_t *)nmo_object_get_state(obj);
    ASSERT_NOT_NULL(state);

    const char json[] =
        "{\"objects\":[{\"id\":9101,\"fields\":["
        "{\"name\":\"values\",\"kind\":\"array\",\"type_guid\":\"{4E4D4F03-00200000}\","
        "\"count\":2,\"value\":[8,9],\"items\":[8,9]}]}]}";
    nmo_import_result_t result;
    status = nmo_object_edit_import_json(fixture.workspace, json, 0, 0, &result);

    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(1u, result.objects_updated);
    ASSERT_EQ(1u, result.fields_written);
    ASSERT_EQ(0u, result.errors);
    ASSERT_EQ(2u, nmo_array_size(&state->values));
    ASSERT_EQ(8u, *(uint32_t *)nmo_array_get(&state->values, 0));
    ASSERT_EQ(9u, *(uint32_t *)nmo_array_get(&state->values, 1));

    nmo_array_dispose(&state->values);
    import_api_fixture_destroy(&fixture);
}

TEST(object_import_api, dry_run_create_missing_does_not_create_object_or_use_session_arena) {
    import_api_fixture_t fixture;
    ASSERT_TRUE(import_api_fixture_init(&fixture));

    nmo_type_registry_t *registry = fixture.registry;
    nmo_session_t *session = fixture.session;
    nmo_status_t status = nmo_type_registry_begin_update(registry);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_TRUE(register_import_raw_array_type(registry));

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    size_t before_count = nmo_object_repository_get_count(repo);
    size_t before_arena = nmo_arena_bytes_used(nmo_session_get_arena(session));

    const char json[] =
        "{\"objects\":[{\"id\":9301,\"class_name\":\"ImportRawArrayState\",\"name\":\"dry_run_new\","
        "\"fields\":[{\"name\":\"items\",\"kind\":\"array\",\"type_guid\":\"{4E4D4F03-00200000}\","
        "\"count\":3,\"value\":[1,2,3],\"items\":[1,2,3]}]}]}";
    nmo_import_result_t result;
    status = nmo_object_edit_import_json(
        fixture.workspace,
        json,
        0,
        NMO_IMPORT_CREATE_MISSING | NMO_IMPORT_DRY_RUN,
        &result);

    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(1u, result.objects_created);
    ASSERT_EQ(1u, result.objects_updated);
    ASSERT_EQ(1u, result.fields_written);
    ASSERT_EQ(0u, result.errors);
    ASSERT_EQ(before_count, nmo_object_repository_get_count(repo));
    ASSERT_NULL(nmo_object_repository_find_by_id(repo, 9301u));
    ASSERT_EQ(before_arena, nmo_arena_bytes_used(nmo_session_get_arena(session)));

    import_api_fixture_destroy(&fixture);
}

TEST(object_import_api, failed_create_missing_import_does_not_leave_object_or_use_session_arena) {
    import_api_fixture_t fixture;
    ASSERT_TRUE(import_api_fixture_init(&fixture));

    nmo_type_registry_t *registry = fixture.registry;
    nmo_session_t *session = fixture.session;
    nmo_status_t status = nmo_type_registry_begin_update(registry);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_TRUE(register_import_raw_array_type(registry));

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    size_t before_count = nmo_object_repository_get_count(repo);
    size_t before_arena = nmo_arena_bytes_used(nmo_session_get_arena(session));

    const char json[] =
        "{\"objects\":[{\"id\":9302,\"class_name\":\"ImportRawArrayState\",\"name\":\"bad_new\","
        "\"fields\":[{\"name\":\"items\",\"kind\":\"array\",\"type_guid\":\"{4E4D4F03-00200000}\","
        "\"count\":2,\"value\":[1,\"bad\"],\"items\":[1,\"bad\"]}]}]}";
    nmo_import_result_t result;
    status = nmo_object_edit_import_json(
        fixture.workspace,
        json,
        0,
        NMO_IMPORT_CREATE_MISSING,
        &result);

    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, status);
    ASSERT_EQ(1u, result.objects_created);
    ASSERT_EQ(0u, result.objects_updated);
    ASSERT_EQ(0u, result.fields_written);
    ASSERT_EQ(1u, result.errors);
    ASSERT_EQ(before_count, nmo_object_repository_get_count(repo));
    ASSERT_NULL(nmo_object_repository_find_by_id(repo, 9302u));
    ASSERT_EQ(before_arena, nmo_arena_bytes_used(nmo_session_get_arena(session)));

    import_api_fixture_destroy(&fixture);
}

TEST(object_import_api, dry_run_existing_pointer_array_does_not_use_session_arena) {
    import_api_fixture_t fixture;
    ASSERT_TRUE(import_api_fixture_init(&fixture));

    nmo_type_registry_t *registry = fixture.registry;
    nmo_session_t *session = fixture.session;
    nmo_status_t status = nmo_type_registry_begin_update(registry);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_TRUE(register_import_raw_array_type(registry));

    nmo_object_t *obj = create_import_test_object(session);
    ASSERT_NOT_NULL(obj);
    import_raw_array_state_t *state = (import_raw_array_state_t *)nmo_object_get_state(obj);
    ASSERT_NOT_NULL(state);
    ASSERT_EQ(0u, state->item_count);
    ASSERT_NULL(state->items);

    size_t before_arena = nmo_arena_bytes_used(nmo_session_get_arena(session));
    const char json[] =
        "{\"objects\":[{\"id\":9001,\"fields\":["
        "{\"name\":\"items\",\"kind\":\"array\",\"type_guid\":\"{4E4D4F03-00200000}\","
        "\"count\":3,\"value\":[11,22,33],\"items\":[11,22,33]}]}]}";
    nmo_import_result_t result;
    status = nmo_object_edit_import_json(
        fixture.workspace,
        json,
        0,
        NMO_IMPORT_DRY_RUN,
        &result);

    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(0u, state->item_count);
    ASSERT_NULL(state->items);
    ASSERT_EQ(before_arena, nmo_arena_bytes_used(nmo_session_get_arena(session)));

    import_api_fixture_destroy(&fixture);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(object_import_api, raw_pointer_array_missing_count_metadata_does_not_mutate);
    REGISTER_TEST(object_import_api, old_flat_map_schema_is_rejected);
    REGISTER_TEST(object_import_api, old_value_str_bridge_schema_is_rejected);
    REGISTER_TEST(object_import_api, snapshot_raw_pointer_array_imports_all_items);
    REGISTER_TEST(object_import_api, counted_raw_pointer_array_imports_items_over_raw_hex);
    REGISTER_TEST(object_import_api, raw_pointer_array_parse_failure_does_not_mutate);
    REGISTER_TEST(object_import_api, inline_array_parse_failure_does_not_mutate);
    REGISTER_TEST(object_import_api, snapshot_inline_array_imports_all_items);
    REGISTER_TEST(object_import_api, snapshot_disambiguates_inherited_fields);
    REGISTER_TEST(object_import_api, object_owner_import_wrapper_imports_snapshot);
    REGISTER_TEST(object_import_api, dry_run_create_missing_does_not_create_object_or_use_session_arena);
    REGISTER_TEST(object_import_api, failed_create_missing_import_does_not_leave_object_or_use_session_arena);
    REGISTER_TEST(object_import_api, dry_run_existing_pointer_array_does_not_use_session_arena);
TEST_MAIN_END()


