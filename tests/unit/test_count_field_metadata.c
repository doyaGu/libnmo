#include "test_framework.h"
#include "type/nmo_type_system.h"
#include "type/nmo_reflection.h"
#include "type/nmo_operations.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena.h"

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

static const nmo_type_field_t test_fields[] = {
    NMO_FIELD(test_ptr_array_struct_t, item_count, CKPGUID_UINT32),
    NMO_FIELD_PTR_ARRAY(test_ptr_array_struct_t, items, item_count, TEST_GUID_ELEM),
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

TEST_MAIN_BEGIN()
    REGISTER_TEST(count_field_meta, ptr_array_has_count_field_name);
    REGISTER_TEST(count_field_meta, non_ptr_array_has_null_count);
    REGISTER_TEST(count_field_meta, get_count_field_resolves_metadata);
    REGISTER_TEST(count_field_meta, get_count_field_null_for_null_type);
    REGISTER_TEST(count_field_meta, resolve_count_uses_metadata_value);
    REGISTER_TEST(count_field_meta, resolve_count_field_returns_metadata_field);
    REGISTER_TEST(count_field_meta, resolve_count_reports_missing_count_field);
    REGISTER_TEST(count_field_meta, registry_rejects_raw_pointer_array_without_count_metadata);
TEST_MAIN_END()
