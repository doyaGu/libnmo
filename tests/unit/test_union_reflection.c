/**
 * @file test_union_reflection.c
 * @brief Unit tests for union reflection metadata
 */

#include "test_framework.h"
#include "type/nmo_dynamic_types.h"
#include "type/nmo_type_system.h"
#include "type/nmo_reflection.h"
#include "type/nmo_operations.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"

static nmo_arena_t *arena = NULL;
static nmo_type_registry_t *registry = NULL;

static void setup(void) {
    arena = nmo_arena_create(NULL, 4096);
    ASSERT_NE(NULL, arena);

    registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));
}

static void teardown(void) {
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
    registry = NULL;
    arena = NULL;
}

TEST(union_reflection, pointer_and_array_metadata) {
    setup();

    nmo_struct_field_def_t fields[] = {
        {
            .name = "ptr_int",
            .type_name = "int*",
            .type_guid = NMO_NULL_GUID,
            .description = NULL,
            .flags = 0,
            .default_value = NULL
        },
        {
            .name = "ptr_array",
            .type_name = "int[3]*",
            .type_guid = NMO_NULL_GUID,
            .description = NULL,
            .flags = 0,
            .default_value = NULL
        },
        {
            .name = "scalar",
            .type_name = "float",
            .type_guid = NMO_NULL_GUID,
            .description = NULL,
            .flags = 0,
            .default_value = NULL
        }
    };

    nmo_union_type_def_t union_def = {
        .name = "TestUnion",
        .description = NULL,
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = sizeof(fields) / sizeof(fields[0]),
        .alignment = 0,
        .packed = false
    };

    nmo_guid_t union_guid = NMO_NULL_GUID;
    ASSERT_EQ(NMO_OK, nmo_type_registry_register_union(registry, &union_def, &union_guid));

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, union_guid);
    ASSERT_NE(NULL, type);
    ASSERT_EQ(3, type->field_count);
    ASSERT_NE(NULL, type->fields);

    const nmo_type_field_t *field0 = nmo_type_get_field_by_index(type, 0);
    const nmo_type_field_t *field1 = nmo_type_get_field_by_index(type, 1);
    const nmo_type_field_t *field2 = nmo_type_get_field_by_index(type, 2);
    ASSERT_NE(NULL, field0);
    ASSERT_NE(NULL, field1);
    ASSERT_NE(NULL, field2);

    ASSERT_EQ(CKPGUID_POINTER.d1, field0->type_guid.d1);
    ASSERT_EQ(CKPGUID_POINTER.d2, field0->type_guid.d2);
    ASSERT_EQ(CKPGUID_POINTER.d1, field1->type_guid.d1);
    ASSERT_EQ(CKPGUID_POINTER.d2, field1->type_guid.d2);
    ASSERT_EQ(CKPGUID_FLOAT.d1, field2->type_guid.d1);
    ASSERT_EQ(CKPGUID_FLOAT.d2, field2->type_guid.d2);

    const nmo_specialized_metadata_t *metadata = nmo_type_registry_get_metadata(registry, type->id);
    ASSERT_NE(NULL, metadata);
    ASSERT_EQ(NMO_METADATA_TYPE_UNION, metadata->metadata_type);
    ASSERT_EQ(3, metadata->union_meta.field_count);

    const nmo_struct_descriptor_t *m0 = &metadata->union_meta.fields[0];
    const nmo_struct_descriptor_t *m1 = &metadata->union_meta.fields[1];
    const nmo_struct_descriptor_t *m2 = &metadata->union_meta.fields[2];

    ASSERT_EQ(0, m0->array_count);
    ASSERT_EQ(1, m0->pointer_depth);
    ASSERT_EQ(CKPGUID_INT.d1, m0->pointee_guid.d1);
    ASSERT_EQ(CKPGUID_INT.d2, m0->pointee_guid.d2);

    ASSERT_EQ(3, m1->array_count);
    ASSERT_EQ(1, m1->pointer_depth);
    ASSERT_EQ(CKPGUID_INT.d1, m1->pointee_guid.d1);
    ASSERT_EQ(CKPGUID_INT.d2, m1->pointee_guid.d2);
    ASSERT_EQ((uint32_t)(sizeof(void *) * 3), m1->size);

    ASSERT_EQ(0, m2->array_count);
    ASSERT_EQ(0, m2->pointer_depth);
    ASSERT_EQ(0u, m2->pointee_guid.d1 | m2->pointee_guid.d2);

    teardown();
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(union_reflection, pointer_and_array_metadata);
TEST_MAIN_END()
