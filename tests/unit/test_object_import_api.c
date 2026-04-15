/**
 * @file test_object_import_api.c
 * @brief Direct tests for app-level object import API.
 */

#include "test_framework.h"
#include "nmo.h"

#include "app/nmo_object_import.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"

#include <stdalign.h>
#include <string.h>

#define IMPORT_RAW_ARRAY_GUID_INIT NMO_GUID_INIT(0x51A4E002u, 0x00000001u)
#define IMPORT_RAW_ARRAY_CLASS_ID  0x51A4E002u

typedef struct import_raw_array_state {
    uint32_t item_count;
    uint32_t *items;
} import_raw_array_state_t;

static const nmo_type_field_t import_raw_array_fields[] = {
    NMO_FIELD(import_raw_array_state_t, item_count, CKPGUID_UINT32),
    NMO_FIELD_PTR_ARRAY(import_raw_array_state_t, items, item_count, CKPGUID_UINT32),
};

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

TEST(object_import_api, raw_pointer_array_missing_count_metadata_does_not_mutate) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);
    nmo_status_t status = nmo_type_registry_begin_update(registry);
    ASSERT_EQ(NMO_OK, status);

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
    status = nmo_type_registry_register(registry, &desc);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_TRUE(break_registered_count_metadata(registry));

    nmo_object_t *obj = create_import_test_object(session);
    ASSERT_NOT_NULL(obj);
    import_raw_array_state_t *state = (import_raw_array_state_t *)nmo_object_get_state(obj);
    ASSERT_NOT_NULL(state);
    ASSERT_EQ(0u, state->item_count);
    ASSERT_NULL(state->items);

    const char json[] =
        "{\"objects\":[{\"id\":9001,\"fields\":{\"items\":[1,2]}}]}";
    nmo_import_result_t result;
    status = nmo_object_import_json(
        session,
        registry,
        nmo_session_get_arena(session),
        json,
        0,
        0,
        &result);

    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(1u, result.objects_updated);
    ASSERT_EQ(0u, result.fields_written);
    ASSERT_EQ(1u, result.errors);
    ASSERT_EQ(0u, state->item_count);
    ASSERT_NULL(state->items);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(object_import_api, raw_pointer_array_missing_count_metadata_does_not_mutate);
TEST_MAIN_END()
