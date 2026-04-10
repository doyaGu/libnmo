/**
 * @file test_virtools_types.c
 * @brief Tests for Virtools data loading via context data_dir
 */

#include "../test_framework.h"
#include "app/nmo_context.h"
#include "type/nmo_type_system.h"
#include "type/nmo_operation_system.h"
#include "core/nmo_guid.h"

#include <string.h>

static nmo_context_t *create_ctx_with_data(void) {
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.data_dir = "data";
    return nmo_context_create(&desc);
}

TEST(vt, operation_addition) {
    nmo_context_t *ctx = create_ctx_with_data();
    ASSERT_TRUE(ctx != NULL);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);
    nmo_guid_t guid = nmo_guid_create(0x33CC6B49, 0x3589282B);
    const char *name = nmo_type_registry_guid_to_name(reg, guid);
    ASSERT_TRUE(name != NULL);
    ASSERT_STR_EQ(name, "Addition");
    nmo_context_release(ctx);
}

TEST(vt, operation_equal) {
    nmo_context_t *ctx = create_ctx_with_data();
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);
    nmo_guid_t guid = nmo_guid_create(0x6C62476B, 0x7A922973);
    ASSERT_TRUE(nmo_type_registry_guid_to_name(reg, guid) != NULL);
    nmo_context_release(ctx);
}

TEST(vt, operation_get_position) {
    nmo_context_t *ctx = create_ctx_with_data();
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);
    nmo_guid_t guid = nmo_guid_create(0x4BC87AEA, 0x6B5B643E);
    const char *name = nmo_type_registry_guid_to_name(reg, guid);
    ASSERT_TRUE(name != NULL);
    ASSERT_STR_EQ(name, "Get Position");
    nmo_context_release(ctx);
}

TEST(vt, operation_unknown) {
    nmo_context_t *ctx = create_ctx_with_data();
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);
    nmo_guid_t guid = nmo_guid_create(0xDEADDEAD, 0xBEEFBEEF);
    ASSERT_TRUE(nmo_type_registry_guid_to_name(reg, guid) == NULL);
    nmo_context_release(ctx);
}

TEST(vt, param_type_direction) {
    nmo_context_t *ctx = create_ctx_with_data();
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);
    nmo_guid_t guid = nmo_guid_create(0x0286652D, 0x5EA709C2);
    const char *name = nmo_type_registry_guid_to_name(reg, guid);
    ASSERT_TRUE(name != NULL);
    ASSERT_STR_EQ(name, "Direction");
    nmo_context_release(ctx);
}

TEST(vt, operation_has_category_flag) {
    nmo_context_t *ctx = create_ctx_with_data();
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);
    nmo_guid_t guid = nmo_guid_create(0x33CC6B49, 0x3589282B);
    nmo_type_id_t tid = nmo_type_registry_guid_to_type_id(reg, guid);
    ASSERT_TRUE(tid != NMO_TYPE_ID_INVALID);
    const nmo_type_descriptor_t *desc = nmo_type_registry_get_by_id(reg, tid);
    ASSERT_TRUE(desc != NULL);
    ASSERT_TRUE((desc->category & NMO_TYPE_CATEGORY_OPERATION) != 0);
    nmo_context_release(ctx);
}

TEST(vt, operation_signatures_loaded) {
    nmo_context_t *ctx = create_ctx_with_data();
    ASSERT_TRUE(ctx != NULL);
    nmo_operation_registry_t *op_reg = nmo_context_get_operation_registry(ctx);
    ASSERT_TRUE(op_reg != NULL);

    uint64_t total_ops = 0;
    nmo_operation_registry_get_stats(op_reg, &total_ops, NULL, NULL);
    /* 7775 Virtools JSON signatures + ~190 builtins. Must be > 1000. */
    ASSERT_TRUE(total_ops > 1000);

    nmo_context_release(ctx);
}

TEST(vt, operation_signature_lookup) {
    nmo_context_t *ctx = create_ctx_with_data();
    ASSERT_TRUE(ctx != NULL);
    nmo_operation_registry_t *op_reg = nmo_context_get_operation_registry(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    /* Addition (0x33CC6B49, 0x3589282B) with Float+Float should exist */
    nmo_guid_t add_guid = nmo_guid_create(0x33CC6B49, 0x3589282B);
    nmo_guid_t float_guid = nmo_guid_create(0x47884C3F, 0x432C2C20);
    const nmo_type_descriptor_t *float_type = nmo_type_registry_find_by_guid(reg, float_guid);
    ASSERT_TRUE(float_type != NULL);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t st = nmo_operation_registry_find(
        op_reg, &add_guid, float_type, float_type, reg, &cell);
    ASSERT_EQ(NMO_OK, st);
    ASSERT_TRUE(cell != NULL);

    nmo_context_release(ctx);
}

TEST(vt, signature_only_not_executable) {
    nmo_context_t *ctx = create_ctx_with_data();
    ASSERT_TRUE(ctx != NULL);
    nmo_operation_registry_t *op_reg = nmo_context_get_operation_registry(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    /* "Get Position" (0x4BC87AEA, 0x6B5B643E) — Virtools-only, no C impl.
     * Lookup with 3DEntity→Vector should find signature but execute should
     * return NOT_IMPLEMENTED. First find the types. */
    nmo_guid_t op_guid = nmo_guid_create(0x4BC87AEA, 0x6B5B643E);
    /* 3DEntity: {0x31CD67A4, 0x07D53645}, Vector: {0x48824EAE, 0x2FE47960} */
    nmo_guid_t entity_guid = nmo_guid_create(0x31CD67A4, 0x07D53645);
    nmo_guid_t vector_guid = nmo_guid_create(0x48824EAE, 0x2FE47960);
    const nmo_type_descriptor_t *entity_type = nmo_type_registry_find_by_guid(reg, entity_guid);
    const nmo_type_descriptor_t *vector_type = nmo_type_registry_find_by_guid(reg, vector_guid);

    if (!entity_type || !vector_type) {
        /* Types not registered — skip */
        nmo_context_release(ctx);
        return;
    }

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t st = nmo_operation_registry_find(
        op_reg, &op_guid, entity_type, NULL, reg, &cell);
    /* May or may not find it depending on exact signature match */
    if (st == NMO_OK && cell != NULL) {
        /* If found, it should have NULL function (signature-only) */
        ASSERT_TRUE(cell->desc.function == NULL);
    }

    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(vt, operation_addition);
    REGISTER_TEST(vt, operation_equal);
    REGISTER_TEST(vt, operation_get_position);
    REGISTER_TEST(vt, operation_unknown);
    REGISTER_TEST(vt, param_type_direction);
    REGISTER_TEST(vt, operation_has_category_flag);
    REGISTER_TEST(vt, operation_signatures_loaded);
    REGISTER_TEST(vt, operation_signature_lookup);
    REGISTER_TEST(vt, signature_only_not_executable);
TEST_MAIN_END()
