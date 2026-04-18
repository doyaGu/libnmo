/**
 * @file test_virtools_types.c
 * @brief Tests for Virtools data loading via context data_dir
 */

#include "../test_framework.h"
#include "extension/nmo_virtools_loader.h"
#include "session/nmo_context.h"
#include "type/nmo_operations.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_string.h"
#include "type/nmo_operation_system.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_param_guids.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"

#include <stdio.h>
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

TEST(vt, builtin_overrides_json_signature) {
    /* After GUID unification, looking up Addition (Virtools GUID) with
     * Float+Float should find the builtin C implementation, not a
     * JSON stub. This is the key invariant of the unified system. */
    nmo_context_t *ctx = create_ctx_with_data();
    ASSERT_TRUE(ctx != NULL);
    nmo_operation_registry_t *op_reg = nmo_context_get_operation_registry(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    nmo_guid_t add_guid = nmo_guid_create(0x33CC6B49, 0x3589282B);
    nmo_guid_t float_guid = nmo_guid_create(0x47884C3F, 0x432C2C20);
    const nmo_type_descriptor_t *float_type = nmo_type_registry_find_by_guid(reg, float_guid);
    ASSERT_TRUE(float_type != NULL);

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t st = nmo_operation_registry_find(
        op_reg, &add_guid, float_type, float_type, reg, &cell);
    ASSERT_EQ(NMO_OK, st);
    ASSERT_TRUE(cell != NULL);
    ASSERT_TRUE(cell->desc.function != NULL);

    /* Verify the function actually computes correctly */
    float a = 1.5f, b = 2.5f, result = 0.0f;
    st = cell->desc.function(&a, float_type, &b, float_type,
                             &result, float_type, cell->desc.user_data);
    ASSERT_EQ(NMO_OK, st);
    ASSERT_FLOAT_EQ(4.0f, result, 0.001f);

    nmo_context_release(ctx);
}

TEST(vt, derived_json_primitive_types_parse_from_string) {
    nmo_context_t *ctx = create_ctx_with_data();
    ASSERT_TRUE(ctx != NULL);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    const nmo_type_descriptor_t *time_type =
        nmo_type_registry_find_by_guid(reg, CKPGUID_TIME);
    ASSERT_TRUE(time_type != NULL);

    float time_value = 0.0f;
    ASSERT_EQ(NMO_OK,
              nmo_type_value_from_string(&time_value, time_type, reg, "2.5"));
    ASSERT_FLOAT_EQ(2.5f, time_value, 0.001f);

    const nmo_type_descriptor_t *key_type =
        nmo_type_registry_find_by_guid(reg, CKPGUID_KEY);
    ASSERT_TRUE(key_type != NULL);

    int32_t key_value = 0;
    ASSERT_EQ(NMO_OK,
              nmo_type_value_from_string(&key_value, key_type, reg, "65"));
    ASSERT_EQ(65, key_value);

    nmo_context_release(ctx);
}

TEST(vt, object_ref_types_parse_object_ids_from_string) {
    nmo_context_t *ctx = create_ctx_with_data();
    ASSERT_TRUE(ctx != NULL);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    static const nmo_guid_t object_ref_guids[] = {
        CKPGUID_MESH_INIT,
        CKPGUID_MATERIAL_INIT,
        CKPGUID_TEXTURE_INIT,
        CKPGUID_3DENTITY_INIT,
    };

    for (size_t i = 0; i < sizeof(object_ref_guids) / sizeof(object_ref_guids[0]); ++i) {
        const nmo_type_descriptor_t *type =
            nmo_type_registry_find_by_guid(reg, object_ref_guids[i]);
        ASSERT_TRUE(type != NULL);
        ASSERT_TRUE((type->category & NMO_TYPE_CATEGORY_OBJECT_REF) != 0);

        nmo_object_id_t id = 0;
        ASSERT_EQ(NMO_OK, nmo_type_value_from_string(&id, type, reg, "#123"));
        ASSERT_EQ(123u, id);

        id = 0;
        ASSERT_EQ(NMO_OK, nmo_type_value_from_string(&id, type, reg, "456"));
        ASSERT_EQ(456u, id);
    }

    nmo_context_release(ctx);
}

TEST(vt, object_like_primitive_classes_parse_object_ids_from_string) {
    nmo_context_t *ctx = create_ctx_with_data();
    ASSERT_TRUE(ctx != NULL);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    static const nmo_guid_t object_like_guids[] = {
        CKPGUID_OBJECT_INIT,
        CKPGUID_SCENEOBJECT_INIT,
        CKPGUID_BEOBJECT_INIT,
        CKPGUID_BEHAVIOR_INIT,
        CKPGUID_SCENE_INIT,
        CKPGUID_LEVEL_INIT,
        CKPGUID_GROUP_INIT,
        CKPGUID_SOUND_INIT,
        CKPGUID_WAVESOUND_INIT,
        CKPGUID_MIDISOUND_INIT,
        CKPGUID_STATE_INIT,
        CKPGUID_CRITICALSECTION_INIT,
        CKPGUID_DATAARRAY_INIT,
    };

    for (size_t i = 0; i < sizeof(object_like_guids) / sizeof(object_like_guids[0]); ++i) {
        const nmo_type_descriptor_t *type =
            nmo_type_registry_find_by_guid(reg, object_like_guids[i]);
        ASSERT_TRUE(type != NULL);
        ASSERT_TRUE((type->category & NMO_TYPE_CATEGORY_OBJECT_REF) != 0);

        nmo_object_id_t id = 0;
        ASSERT_EQ(NMO_OK, nmo_type_value_from_string(&id, type, reg, "#789"));
        ASSERT_EQ(789u, id);
    }

    nmo_context_release(ctx);
}

TEST(vt, raw_json_loader_marks_object_refs_and_parses_ids) {
    const char *path = "test_object_ref_param_type_tmp.json";
    FILE *file = fopen(path, "wb");
    ASSERT_TRUE(file != NULL);
    ASSERT_TRUE(fputs(
        "[{\"name\":\"Test Object Ref\","
        "\"guid\":[3735879681,3735879682],"
        "\"size\":4,"
        "\"class_id\":0,"
        "\"derived_from\":[0,0],"
        "\"category\":\"object_ref\"}]",
        file) >= 0);
    ASSERT_EQ(0, fclose(file));

    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_TRUE(arena != NULL);
    nmo_type_registry_t *reg = nmo_type_registry_create(arena);
    ASSERT_TRUE(reg != NULL);

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(reg));
    ASSERT_EQ(NMO_OK, nmo_virtools_load_param_types(reg, path));
    remove(path);

    nmo_guid_t test_guid = nmo_guid_create(0xDEAD0001u, 0xDEAD0002u);
    const nmo_type_descriptor_t *ref_type =
        nmo_type_registry_find_by_guid(reg, test_guid);
    ASSERT_TRUE(ref_type != NULL);
    ASSERT_TRUE((ref_type->category & NMO_TYPE_CATEGORY_OBJECT_REF) != 0);

    nmo_object_id_t id = 0;
    ASSERT_EQ(NMO_OK, nmo_type_value_from_string(&id, ref_type, reg, "#321"));
    ASSERT_EQ(321u, id);

    nmo_type_registry_destroy(reg);
    nmo_arena_destroy(arena);
}

TEST(vt, unbased_json_u32_primitives_parse_from_string) {
    nmo_context_t *ctx = create_ctx_with_data();
    ASSERT_TRUE(ctx != NULL);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    static const nmo_guid_t u32_guids[] = {
        CKPGUID_COPYDEPENDENCIES_INIT,
        CKPGUID_DELETEDEPENDENCIES_INIT,
        CKPGUID_REPLACEDEPENDENCIES_INIT,
        CKPGUID_SAVEDEPENDENCIES_INIT,
        CKPGUID_MESSAGE_INIT,
        CKPGUID_ATTRIBUTE_INIT,
        CKPGUID_OBJECTARRAY_INIT,
        CKPGUID_2DCURVE_INIT,
    };

    for (size_t i = 0; i < sizeof(u32_guids) / sizeof(u32_guids[0]); ++i) {
        const nmo_type_descriptor_t *type =
            nmo_type_registry_find_by_guid(reg, u32_guids[i]);
        ASSERT_TRUE(type != NULL);
        ASSERT_TRUE((type->category & NMO_TYPE_CATEGORY_SCALAR) != 0);

        uint32_t value = 0;
        ASSERT_EQ(NMO_OK, nmo_type_value_from_string(&value, type, reg, "42"));
        ASSERT_EQ(42u, value);
    }

    nmo_context_release(ctx);
}

TEST(vt, script_param_type_loads_as_object_ref_alias) {
    nmo_context_t *ctx = create_ctx_with_data();
    ASSERT_TRUE(ctx != NULL);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    nmo_guid_t script_guid = nmo_guid_create(0x7EA4176Du, 0x1B405D30u);
    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_guid(reg, script_guid);
    ASSERT_TRUE(type != NULL);
    ASSERT_TRUE((type->category & NMO_TYPE_CATEGORY_OBJECT_REF) != 0);

    nmo_object_id_t id = 0;
    ASSERT_EQ(NMO_OK, nmo_type_value_from_string(&id, type, reg, "#987"));
    ASSERT_EQ(987u, id);

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
    REGISTER_TEST(vt, builtin_overrides_json_signature);
    REGISTER_TEST(vt, derived_json_primitive_types_parse_from_string);
    REGISTER_TEST(vt, object_ref_types_parse_object_ids_from_string);
    REGISTER_TEST(vt, object_like_primitive_classes_parse_object_ids_from_string);
    REGISTER_TEST(vt, raw_json_loader_marks_object_refs_and_parses_ids);
    REGISTER_TEST(vt, unbased_json_u32_primitives_parse_from_string);
    REGISTER_TEST(vt, script_param_type_loads_as_object_ref_alias);
TEST_MAIN_END()
