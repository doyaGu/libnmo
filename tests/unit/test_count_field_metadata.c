#include "test_framework.h"
#include "type/nmo_type_system.h"
#include "type/nmo_reflection.h"
#include "core/nmo_guid.h"

#include <string.h>

/* Dummy GUIDs for test types */
#define TEST_GUID_ELEM      {0xDEAD0001, 0x00000000}
#define TEST_GUID_ELEM_INIT TEST_GUID_ELEM

/* Test struct with pointer array + count */
typedef struct test_ptr_array_struct {
    uint32_t item_count;
    void *items;
} test_ptr_array_struct_t;

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

/* Build a minimal type descriptor from the test fields */
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

/**
 * Verify that the heuristic fallback still works for fields without
 * explicit count_field_name metadata (legacy path).
 * We construct a field with count_field_name = NULL but whose name
 * follows the "{singular}_count" convention, and verify the helper
 * returns NULL (forcing callers to fall back to the heuristic).
 */
TEST(count_field_meta, heuristic_fallback_when_no_metadata) {
    /* Simulate a legacy field: pointer+repeated with NULL count_field_name */
    nmo_type_field_t legacy_count = {
        .name = "widget_count",
        .type_guid = CKPGUID_UINT32_INIT,
        .offset = 0,
        .size = sizeof(uint32_t),
        .flags = 0,
    };
    nmo_type_field_t legacy_array = {
        .name = "widgets",
        .type_guid = TEST_GUID_ELEM_INIT,
        .offset = sizeof(uint32_t),
        .size = sizeof(void *),
        .flags = NMO_FIELD_POINTER | NMO_FIELD_REPEATED,
        .count_field_name = NULL,  /* no metadata — heuristic required */
    };
    nmo_type_field_t legacy_fields[2] = { legacy_count, legacy_array };
    nmo_type_descriptor_t type = make_test_type(legacy_fields, 2);

    /* Helper returns NULL because count_field_name is NULL */
    const nmo_type_field_t *cf = nmo_field_get_count_field(&type, &legacy_fields[1]);
    ASSERT_NULL(cf);

    /* But the count field IS present by name — consumers' heuristic
     * fallback (strip trailing 's', append '_count') would find it.
     * We verify the naming convention matches. */
    const nmo_type_field_t *heuristic_cf = nmo_type_get_field_by_name(&type, "widget_count");
    ASSERT_NOT_NULL(heuristic_cf);
    ASSERT_EQ(0, strcmp(heuristic_cf->name, "widget_count"));
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(count_field_meta, ptr_array_has_count_field_name);
    REGISTER_TEST(count_field_meta, non_ptr_array_has_null_count);
    REGISTER_TEST(count_field_meta, get_count_field_resolves_metadata);
    REGISTER_TEST(count_field_meta, get_count_field_null_for_null_type);
    REGISTER_TEST(count_field_meta, heuristic_fallback_when_no_metadata);
TEST_MAIN_END()
