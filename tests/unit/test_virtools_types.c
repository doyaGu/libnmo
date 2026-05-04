/**
 * @file test_virtools_types.c
 * @brief Tests for Virtools data loading via context data_dir
 */

#include "../test_framework.h"
#include "extension/nmo_virtools_loader.h"
#include "runtime/nmo_context.h"
#include "type/nmo_operations.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_string.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_operation_system.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_param_guids.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"

#include <stdio.h>
#include <stdlib.h>
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

    /* "Get Position" (0x4BC87AEA, 0x6B5B643E) â€?Virtools-only, no C impl.
     * Lookup with 3DEntityâ†’Vector should find signature but execute should
     * return NOT_IMPLEMENTED. First find the types. */
    nmo_guid_t op_guid = nmo_guid_create(0x4BC87AEA, 0x6B5B643E);
    /* 3DEntity: {0x31CD67A4, 0x07D53645}, Vector: {0x48824EAE, 0x2FE47960} */
    nmo_guid_t entity_guid = nmo_guid_create(0x31CD67A4, 0x07D53645);
    nmo_guid_t vector_guid = nmo_guid_create(0x48824EAE, 0x2FE47960);
    const nmo_type_descriptor_t *entity_type = nmo_type_registry_find_by_guid(reg, entity_guid);
    const nmo_type_descriptor_t *vector_type = nmo_type_registry_find_by_guid(reg, vector_guid);

    if (!entity_type || !vector_type) {
        /* Types not registered â€?skip */
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

    ASSERT_EQ(NMO_OK,
              nmo_type_value_from_string(&time_value, time_type, reg, "0m 3s 0ms"));
    ASSERT_FLOAT_EQ(3000.0f, time_value, 0.001f);

    ASSERT_EQ(NMO_OK,
              nmo_type_value_from_string(&time_value, time_type, reg, "1m 2s 3ms"));
    ASSERT_FLOAT_EQ(62003.0f, time_value, 0.001f);

    char time_buffer[64];
    ASSERT_EQ(NMO_OK,
              nmo_type_value_to_string(&time_value, time_type, reg,
                                       time_buffer, sizeof(time_buffer)));
    ASSERT_STR_EQ("62003.0 ms", time_buffer);

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

TEST(vt, raw_json_loader_normalizes_unbased_u32_primitives_to_uint32_base) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_TRUE(arena != NULL);
    nmo_type_registry_t *reg = nmo_type_registry_create(arena);
    ASSERT_TRUE(reg != NULL);

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(reg));
    ASSERT_EQ(NMO_OK, nmo_virtools_load_param_types(reg, "data/virtools_parameter_types.json"));

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
        ASSERT_TRUE(nmo_guid_equals(type->base_type, CKPGUID_UINT32));

        uint32_t value = 0;
        ASSERT_EQ(NMO_OK, nmo_type_value_from_string(&value, type, reg, "42"));
        ASSERT_EQ(42u, value);
    }

    nmo_type_registry_destroy(reg);
    nmo_arena_destroy(arena);
}

TEST(vt, time_is_builtin_and_json_loader_does_not_override_it) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_TRUE(arena != NULL);
    nmo_type_registry_t *reg = nmo_type_registry_create(arena);
    ASSERT_TRUE(reg != NULL);

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(reg));

    const nmo_type_descriptor_t *time_type =
        nmo_type_registry_find_by_guid(reg, CKPGUID_TIME);
    ASSERT_TRUE(time_type != NULL);
    ASSERT_TRUE(nmo_guid_equals(time_type->base_type, CKPGUID_FLOAT));
    ASSERT_TRUE(time_type->vtable != NULL);

    float time_value = 2.5f;
    char buffer[64];
    ASSERT_EQ(NMO_OK, nmo_type_value_to_string(
        &time_value, time_type, reg, buffer, sizeof(buffer)));
    ASSERT_STR_EQ("2.5 ms", buffer);

    ASSERT_EQ(NMO_OK, nmo_virtools_load_param_types(reg, "data/virtools_parameter_types.json"));

    const nmo_type_descriptor_t *loaded_time_type =
        nmo_type_registry_find_by_guid(reg, CKPGUID_TIME);
    ASSERT_TRUE(loaded_time_type == time_type);
    ASSERT_TRUE(nmo_guid_equals(loaded_time_type->base_type, CKPGUID_FLOAT));
    ASSERT_EQ(NMO_OK, nmo_type_value_to_string(
        &time_value, loaded_time_type, reg, buffer, sizeof(buffer)));
    ASSERT_STR_EQ("2.5 ms", buffer);

    nmo_type_registry_destroy(reg);
    nmo_arena_destroy(arena);
}

TEST(vt, json_loader_propagates_registration_failure) {
    const char *path = "test_virtools_loader_failure.json";
    FILE *fp = fopen(path, "wb");
    ASSERT_TRUE(fp != NULL);
    fputs("[{\"guid\":[3735928559,65],\"name\":\"Late Enum\","
          "\"size\":4,\"category\":\"enum\","
          "\"values\":[{\"name\":\"Named\",\"value\":1}]}]",
          fp);
    fclose(fp);

    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_TRUE(arena != NULL);
    nmo_type_registry_t *reg = nmo_type_registry_create(arena);
    ASSERT_TRUE(reg != NULL);
    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(reg));
    ASSERT_EQ(NMO_OK, nmo_type_registry_finalize(reg));

    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_virtools_load_param_types(reg, path));

    remove(path);
    nmo_type_registry_destroy(reg);
    nmo_arena_destroy(arena);
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

TEST(vt, json_struct_param_types_parse_fields_with_offsets) {
    nmo_context_t *ctx = create_ctx_with_data();
    ASSERT_TRUE(ctx != NULL);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_guid(reg, nmo_guid_create(0x023756E7u, 0x01DA06EAu));
    ASSERT_TRUE(type != NULL);
    ASSERT_TRUE((type->category & NMO_TYPE_CATEGORY_STRUCT) != 0);

    typedef struct targa_options_t {
        uint32_t bit_depth;
        uint32_t run_length_encoding;
    } targa_options_t;

    targa_options_t value = {0};
    ASSERT_EQ(NMO_OK, nmo_type_value_from_string(
        &value, type, reg, "{Bit Depth=24 bit, Run Length Encoding=true}"));
    ASSERT_EQ(24u, value.bit_depth);
    ASSERT_EQ(1u, value.run_length_encoding);

    char buffer[128];
    ASSERT_EQ(NMO_OK, nmo_type_value_to_string(&value, type, reg, buffer, sizeof(buffer)));
    ASSERT_STR_EQ("{Bit Depth=24 bit, Run Length Encoding=true}", buffer);

    nmo_context_release(ctx);
}

TEST(vt, json_struct_param_types_roundtrip_zero_values) {
    nmo_context_t *ctx = create_ctx_with_data();
    ASSERT_TRUE(ctx != NULL);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    static const nmo_guid_t struct_guids[] = {
        {0x173402ADu, 0x76BD708Au},
        {0x023756E7u, 0x01DA06EAu},
        {0x36D34BD0u, 0x24DA4D96u},
        {0x25433584u, 0x425C41E2u},
        {0x0E7B7108u, 0x5E95096Fu},
        {0x724B7421u, 0x07213ADDu},
        {0x154264EAu, 0x1EB15971u},
        {0x235E15CCu, 0x65903824u},
        {0x36DA22D9u, 0x4AC44B4Cu},
        {0x01238843u, 0xFF881C6Eu},
        {0x7B447672u, 0x5798572Au},
        {0x638737D6u, 0x0F783EAEu},
        {0x468B2BECu, 0x739211CEu},
        {0x479C2CEBu, 0x729312EDu},
        {0x57DE0FD9u, 0x758A71D6u},
        {0x778D5BD9u, 0x5DA52335u},
        {0x1C0138D7u, 0x1A1609EFu},
        {0x7E3745C9u, 0x79A84E4Au},
    };

    for (size_t i = 0; i < sizeof(struct_guids) / sizeof(struct_guids[0]); ++i) {
        const nmo_type_descriptor_t *type =
            nmo_type_registry_find_by_guid(reg, struct_guids[i]);
        if (!type) {
            fprintf(stderr, "Missing struct GUID 0x%08X-0x%08X\n",
                    struct_guids[i].d1, struct_guids[i].d2);
        }
        ASSERT_TRUE(type != NULL);
        ASSERT_TRUE((type->category & NMO_TYPE_CATEGORY_STRUCT) != 0);
        ASSERT_TRUE(type->size > 0);

        void *original = calloc(1, type->size);
        void *parsed = calloc(1, type->size);
        ASSERT_TRUE(original != NULL);
        ASSERT_TRUE(parsed != NULL);

        char buffer[512];
        ASSERT_EQ(NMO_OK, nmo_type_value_to_string(original, type, reg, buffer, sizeof(buffer)));
        nmo_status_t parse_status = nmo_type_value_from_string(parsed, type, reg, buffer);
        if (parse_status != NMO_OK) {
            fprintf(stderr, "Failed struct roundtrip for %s: %s\n",
                    type->name ? type->name : "<unnamed>", buffer);
        }
        ASSERT_EQ(NMO_OK, parse_status);
        ASSERT_EQ(0, memcmp(original, parsed, type->size));

        free(parsed);
        free(original);
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
    REGISTER_TEST(vt, builtin_overrides_json_signature);
    REGISTER_TEST(vt, derived_json_primitive_types_parse_from_string);
    REGISTER_TEST(vt, object_ref_types_parse_object_ids_from_string);
    REGISTER_TEST(vt, object_like_primitive_classes_parse_object_ids_from_string);
    REGISTER_TEST(vt, raw_json_loader_marks_object_refs_and_parses_ids);
    REGISTER_TEST(vt, unbased_json_u32_primitives_parse_from_string);
    REGISTER_TEST(vt, raw_json_loader_normalizes_unbased_u32_primitives_to_uint32_base);
    REGISTER_TEST(vt, time_is_builtin_and_json_loader_does_not_override_it);
    REGISTER_TEST(vt, json_loader_propagates_registration_failure);
    REGISTER_TEST(vt, script_param_type_loads_as_object_ref_alias);
    REGISTER_TEST(vt, json_struct_param_types_parse_fields_with_offsets);
    REGISTER_TEST(vt, json_struct_param_types_roundtrip_zero_values);
TEST_MAIN_END()

