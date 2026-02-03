#include "test_framework.h"
#include "type/type_system.h"
#include "type/operation_system.h"
#include "type/dynamic_types.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include <string.h>

/* ============================================================================
 * Test Data & Helpers
 * ============================================================================ */

/* Custom GUIDs for testing */
static const nmo_guid_t GUID_ENUM_COLOR = {0x11111111, 0xAAAAAAAA};
static const nmo_guid_t GUID_STRUCT_PIXEL = {0x22222222, 0xBBBBBBBB};
static const nmo_guid_t GUID_OP_ADD = {0x33333333, 0xCCCCCCCC};
static const nmo_guid_t GUID_MANAGER_PIXEL = {0x44444444, 0xDDDDDDDD};

/* Pixel struct definition */
typedef struct {
    float r, g, b;
    int32_t color_type; /* Enum value */
} pixel_t;

/* Operation implementation: Pixel + Pixel = Pixel (Component-wise add) */
static nmo_status_t op_pixel_add(
    const void *p1_data, const nmo_type_descriptor_t *p1_type,
    const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type,
    void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    
    const pixel_t *a = (const pixel_t*)p1_data;
    const pixel_t *b = (const pixel_t*)p2_data;
    pixel_t *res = (pixel_t*)result_data;
    
    res->r = a->r + b->r;
    res->g = a->g + b->g;
    res->b = a->b + b->b;
    res->color_type = a->color_type; /* Keep first color type */
    
    NMO_RETURN_OK();
}

/* Mock Manager Serialization */
static nmo_status_t mock_manager_serialize(const void *instance, struct nmo_chunk *chunk, void *ctx) {
    (void)instance; (void)chunk; (void)ctx;
    NMO_RETURN_OK();
}

static nmo_status_t mock_manager_deserialize(void *instance, struct nmo_chunk *chunk, void *ctx) {
    (void)instance; (void)chunk; (void)ctx;
    NMO_RETURN_OK();
}

/* ============================================================================
 * Integration Test
 * ============================================================================ */

TEST(integration, type_system_complete_workflow) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024 * 1024); // 1MB
    ASSERT_NE(NULL, arena);

    /* 1. Initialize Registries */
    nmo_type_registry_t *type_reg = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, type_reg);
    
    nmo_operation_registry_t *op_reg = nmo_operation_registry_create(arena);
    ASSERT_NE(NULL, op_reg);

    /* 2. Register Custom Enum (Color) */
    /* RED=1, GREEN=2, BLUE=3 */
    const char *enum_def = "RED=1,GREEN=2,BLUE=3";
    nmo_status_t res = nmo_type_registry_register_enum_string(
        type_reg, GUID_ENUM_COLOR, "ColorEnum", enum_def
    );
    ASSERT_EQ(NMO_OK, res);
    
    /* Verify Enum */
    const nmo_type_descriptor_t *enum_type = nmo_type_registry_find_by_guid(type_reg, GUID_ENUM_COLOR);
    ASSERT_NE(NULL, enum_type);
    ASSERT_EQ(NMO_TYPE_CATEGORY_ENUM, enum_type->category & NMO_TYPE_CATEGORY_ENUM);

    /* 3. Register Custom Struct (Pixel) */
    /* Fields: r (Float), g (Float), b (Float), type (ColorEnum) */
    
    /* Manually registering FLOAT for isolation */
    nmo_guid_t guid_float = {0x00000001, 0x00000000}; // Mock Float GUID
    nmo_type_descriptor_t float_desc = {0};
    float_desc.guid = guid_float;
    float_desc.name = "Float";
    float_desc.size = sizeof(float);
    float_desc.category = NMO_TYPE_CATEGORY_SCALAR;
    nmo_type_registry_register(type_reg, &float_desc);

    nmo_struct_field_def_t fields[] = {
        {"r", NULL, guid_float, NULL, 0, NULL},
        {"g", NULL, guid_float, NULL, 0, NULL},
        {"b", NULL, guid_float, NULL, 0, NULL},
        {"type", NULL, GUID_ENUM_COLOR, NULL, 0, NULL}
    };

    nmo_struct_type_def_t struct_def = {
        .name = "PixelStruct",
        .guid = GUID_STRUCT_PIXEL,
        .fields = fields,
        .field_count = 4,
        .alignment = 0,
        .packed = false
    };

    res = nmo_type_registry_register_struct(
        type_reg, &struct_def, NULL
    );
    ASSERT_EQ(NMO_OK, res);

    /* Verify Struct */
    const nmo_type_descriptor_t *struct_type = nmo_type_registry_find_by_guid(type_reg, GUID_STRUCT_PIXEL);
    ASSERT_NE(NULL, struct_type);
    ASSERT_EQ(sizeof(pixel_t), struct_type->size);

    /* 4. Register Operation (Add) */
    nmo_operation_desc_t op_desc = {
        .operation_guid = GUID_OP_ADD,
        .p1_type_guid = GUID_STRUCT_PIXEL,
        .p2_type_guid = GUID_STRUCT_PIXEL,
        .result_type_guid = GUID_STRUCT_PIXEL,
        .function = op_pixel_add,
        .name = "Add",
        .flags = NMO_OP_BINARY
    };

    res = nmo_operation_registry_register(op_reg, &op_desc, type_reg);
    ASSERT_EQ(NMO_OK, res);

    /* 5. Execute Operation */
    /* Find operation */
    const nmo_operation_tree_cell_t *cell = NULL;
    res = nmo_operation_registry_find(
        op_reg, &GUID_OP_ADD, struct_type, struct_type, type_reg, &cell
    );
    ASSERT_EQ(NMO_OK, res);
    ASSERT_NE(NULL, cell);
    ASSERT_NE(NULL, cell->desc.function);

    /* Run it */
    pixel_t p1 = {1.0f, 0.5f, 0.0f, 1}; // Red
    pixel_t p2 = {0.0f, 0.5f, 1.0f, 3}; // Blue
    pixel_t result_pixel = {0};

    cell->desc.function(&p1, struct_type, &p2, struct_type, &result_pixel, struct_type, NULL);

    ASSERT_FLOAT_EQ(1.0f, result_pixel.r, 0.001f);
    ASSERT_FLOAT_EQ(1.0f, result_pixel.g, 0.001f);
    ASSERT_FLOAT_EQ(1.0f, result_pixel.b, 0.001f);
    ASSERT_EQ(1, result_pixel.color_type);

    /* 6. Custom Manager Integration */
    res = nmo_type_registry_register_saver_manager(
        type_reg, GUID_MANAGER_PIXEL, "PixelManager",
        mock_manager_serialize, mock_manager_deserialize, NULL
    );
    ASSERT_EQ(NMO_OK, res);

    /* Associate Manager with Type */
    res = nmo_type_registry_set_type_manager(type_reg, GUID_STRUCT_PIXEL, GUID_MANAGER_PIXEL);
    ASSERT_EQ(NMO_OK, res);

    /* Verify Association */
    const nmo_saver_manager_t *mgr = nmo_type_registry_get_type_manager(
        type_reg, GUID_STRUCT_PIXEL
    );
    ASSERT_NE(NULL, mgr);
    ASSERT_TRUE(nmo_guid_equals(GUID_MANAGER_PIXEL, mgr->guid));

    /* 7. Statistics Check */
    size_t enum_count = nmo_type_registry_get_enum_count(type_reg);
    size_t struct_count = nmo_type_registry_get_struct_count(type_reg);
    
    ASSERT_EQ(1, enum_count);
    ASSERT_EQ(1, struct_count);

    /* Cleanup */
    nmo_operation_registry_destroy(op_reg);
    nmo_type_registry_destroy(type_reg); // Should handle freeing internal structures
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(integration, type_system_complete_workflow);
TEST_MAIN_END()
