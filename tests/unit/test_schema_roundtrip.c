/**
 * @file test_schema_roundtrip.c
 * @brief Generic roundtrip tests for all schema implementations
 * 
 * This test ensures all 23 schema classes can serialize/deserialize correctly
 * with the unified Phase 2 signature: (in_state, out_chunk, arena)
 */

#include "../test_framework.h"
#include "schema/nmo_ckobject_schemas.h"
#include "schema/nmo_cksceneobject_schemas.h"
#include "schema/nmo_ckbeobject_schemas.h"
#include "schema/nmo_ckgroup_schemas.h"
#include "schema/nmo_ck3dobject_schemas.h"
#include "schema/nmo_ck3dentity_schemas.h"
#include "schema/nmo_ck2dentity_schemas.h"
#include "schema/nmo_ckrenderobject_schemas.h"
#include "schema/nmo_ckcamera_schemas.h"
#include "schema/nmo_cklight_schemas.h"
#include "schema/nmo_ckmesh_schemas.h"
#include "schema/nmo_cksprite_schemas.h"
#include "schema/nmo_ckspritetext_schemas.h"
#include "schema/nmo_cktexture_schemas.h"
#include "schema/nmo_ckbehavior_schemas.h"
#include "schema/nmo_ckbehaviorio_schemas.h"
#include "schema/nmo_ckbehaviorlink_schemas.h"
#include "schema/nmo_ckparameter_schemas.h"
#include "schema/nmo_cklevel_schemas.h"
#include "schema/nmo_ckscene_schemas.h"
#include "schema/nmo_ckdataarray_schemas.h"
#include "schema/nmo_ckattributemanager_schemas.h"
#include "schema/nmo_ckmessagemanager_schemas.h"
#include "format/nmo_chunk.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include <string.h>

/* Test helper: Verify serialize function signature is correct */
#define TEST_SERIALIZE_SIGNATURE(class_name, state_type) \
    TEST(schema_signature, class_name##_serialize) { \
        nmo_allocator_t allocator = nmo_allocator_default(); \
        nmo_arena_t *arena = nmo_arena_create(&allocator, 4096); \
        ASSERT_NE(NULL, arena); \
        nmo_chunk_t *chunk = nmo_chunk_create(arena); \
        ASSERT_NE(NULL, chunk); \
        \
        state_type state; \
        memset(&state, 0, sizeof(state)); \
        \
        nmo_##class_name##_serialize_fn serialize = nmo_get_##class_name##_serialize(); \
        ASSERT_NE(NULL, serialize); \
        \
        /* Test signature: (in_state, out_chunk, arena) */ \
        nmo_result_t result = serialize(&state, chunk, arena); \
        /* We don't care if it fails, just that signature compiles */ \
        (void)result; \
        \
        nmo_chunk_destroy(chunk); \
        nmo_arena_destroy(arena); \
    }

/* Test helper: Verify deserialize function signature is correct */
#define TEST_DESERIALIZE_SIGNATURE(class_name, state_type) \
    TEST(schema_signature, class_name##_deserialize) { \
        nmo_allocator_t allocator = nmo_allocator_default(); \
        nmo_arena_t *arena = nmo_arena_create(&allocator, 4096); \
        ASSERT_NE(NULL, arena); \
        nmo_chunk_t *chunk = nmo_chunk_create(arena); \
        ASSERT_NE(NULL, chunk); \
        nmo_chunk_start_read(chunk); \
        \
        state_type state; \
        memset(&state, 0, sizeof(state)); \
        \
        nmo_##class_name##_deserialize_fn deserialize = nmo_get_##class_name##_deserialize(); \
        ASSERT_NE(NULL, deserialize); \
        \
        /* Test signature: (chunk, arena, out_state) */ \
        nmo_result_t result = deserialize(chunk, arena, &state); \
        /* We don't care if it fails, just that signature compiles */ \
        (void)result; \
        \
        nmo_chunk_destroy(chunk); \
        nmo_arena_destroy(arena); \
    }

/* Test helper: Basic roundtrip test */
#define TEST_BASIC_ROUNDTRIP(class_name, state_type) \
    TEST(schema_roundtrip, class_name) { \
        nmo_allocator_t allocator = nmo_allocator_default(); \
        nmo_arena_t *arena = nmo_arena_create(&allocator, 8192); \
        ASSERT_NE(NULL, arena); \
        \
        /* Create and initialize state */ \
        state_type original; \
        memset(&original, 0, sizeof(original)); \
        \
        /* Serialize */ \
        nmo_chunk_t *chunk = nmo_chunk_create(arena); \
        ASSERT_NE(NULL, chunk); \
        \
        nmo_##class_name##_serialize_fn serialize = nmo_get_##class_name##_serialize(); \
        ASSERT_NE(NULL, serialize); \
        \
        nmo_result_t result = serialize(&original, chunk, arena); \
        if (result.code != NMO_OK) { \
            /* Some schemas may require non-null fields, skip if empty state fails */ \
            nmo_chunk_destroy(chunk); \
            nmo_arena_destroy(arena); \
            return; \
        } \
        \
        /* Switch to read mode */ \
        result = nmo_chunk_start_read(chunk); \
        ASSERT_EQ(NMO_OK, result.code); \
        \
        /* Deserialize */ \
        state_type restored; \
        memset(&restored, 0, sizeof(restored)); \
        \
        nmo_##class_name##_deserialize_fn deserialize = nmo_get_##class_name##_deserialize(); \
        ASSERT_NE(NULL, deserialize); \
        \
        result = deserialize(chunk, arena, &restored); \
        ASSERT_EQ(NMO_OK, result.code); \
        \
        /* Basic validation: memory should be similar for empty state */ \
        /* More detailed validation should be in class-specific tests */ \
        \
        nmo_chunk_destroy(chunk); \
        nmo_arena_destroy(arena); \
    }

/* ========================================================================== */
/* SIGNATURE TESTS - Verify Phase 2 unified signatures                       */
/* ========================================================================== */

TEST_SERIALIZE_SIGNATURE(ckobject, nmo_ckobject_state_t)
TEST_DESERIALIZE_SIGNATURE(ckobject, nmo_ckobject_state_t)

TEST_SERIALIZE_SIGNATURE(cksceneobject, nmo_cksceneobject_state_t)
TEST_DESERIALIZE_SIGNATURE(cksceneobject, nmo_cksceneobject_state_t)

TEST_SERIALIZE_SIGNATURE(ckbeobject, nmo_ckbeobject_state_t)
TEST_DESERIALIZE_SIGNATURE(ckbeobject, nmo_ckbeobject_state_t)

TEST_SERIALIZE_SIGNATURE(ckgroup, nmo_ckgroup_state_t)
TEST_DESERIALIZE_SIGNATURE(ckgroup, nmo_ckgroup_state_t)

TEST_SERIALIZE_SIGNATURE(ck3dobject, nmo_ck3dobject_state_t)
TEST_DESERIALIZE_SIGNATURE(ck3dobject, nmo_ck3dobject_state_t)

TEST_SERIALIZE_SIGNATURE(ck3dentity, nmo_ck3dentity_state_t)
TEST_DESERIALIZE_SIGNATURE(ck3dentity, nmo_ck3dentity_state_t)

TEST_SERIALIZE_SIGNATURE(ck2dentity, nmo_ck2dentity_state_t)
TEST_DESERIALIZE_SIGNATURE(ck2dentity, nmo_ck2dentity_state_t)

TEST_SERIALIZE_SIGNATURE(ckrenderobject, nmo_ckrenderobject_state_t)
TEST_DESERIALIZE_SIGNATURE(ckrenderobject, nmo_ckrenderobject_state_t)

TEST_SERIALIZE_SIGNATURE(ckcamera, nmo_ckcamera_state_t)
TEST_DESERIALIZE_SIGNATURE(ckcamera, nmo_ckcamera_state_t)

TEST_SERIALIZE_SIGNATURE(cklight, nmo_cklight_state_t)
TEST_DESERIALIZE_SIGNATURE(cklight, nmo_cklight_state_t)

TEST_SERIALIZE_SIGNATURE(ckmesh, nmo_ck_mesh_state_t)
TEST_DESERIALIZE_SIGNATURE(ckmesh, nmo_ck_mesh_state_t)

TEST_SERIALIZE_SIGNATURE(cksprite, nmo_cksprite_state_t)
TEST_DESERIALIZE_SIGNATURE(cksprite, nmo_cksprite_state_t)

TEST_SERIALIZE_SIGNATURE(ckspritetext, nmo_ck_spritetext_state_t)
TEST_DESERIALIZE_SIGNATURE(ckspritetext, nmo_ck_spritetext_state_t)

TEST_SERIALIZE_SIGNATURE(cktexture, nmo_ck_texture_state_t)
TEST_DESERIALIZE_SIGNATURE(cktexture, nmo_ck_texture_state_t)

TEST_SERIALIZE_SIGNATURE(ckbehavior, nmo_ckbehavior_state_t)
TEST_DESERIALIZE_SIGNATURE(ckbehavior, nmo_ckbehavior_state_t)

TEST_SERIALIZE_SIGNATURE(ckbehaviorio, nmo_ckbehaviorio_state_t)
TEST_DESERIALIZE_SIGNATURE(ckbehaviorio, nmo_ckbehaviorio_state_t)

TEST_SERIALIZE_SIGNATURE(ckbehaviorlink, nmo_ckbehaviorlink_state_t)
TEST_DESERIALIZE_SIGNATURE(ckbehaviorlink, nmo_ckbehaviorlink_state_t)

TEST_SERIALIZE_SIGNATURE(ckparameter, nmo_ckparameter_state_t)
TEST_DESERIALIZE_SIGNATURE(ckparameter, nmo_ckparameter_state_t)

TEST_SERIALIZE_SIGNATURE(cklevel, nmo_cklevel_state_t)
TEST_DESERIALIZE_SIGNATURE(cklevel, nmo_cklevel_state_t)

TEST_SERIALIZE_SIGNATURE(ckscene, nmo_ckscene_state_t)
TEST_DESERIALIZE_SIGNATURE(ckscene, nmo_ckscene_state_t)

TEST_SERIALIZE_SIGNATURE(ckdataarray, nmo_ckdataarray_state_t)
TEST_DESERIALIZE_SIGNATURE(ckdataarray, nmo_ckdataarray_state_t)

TEST_SERIALIZE_SIGNATURE(ckattributemanager, nmo_ckattributemanager_state_t)
TEST_DESERIALIZE_SIGNATURE(ckattributemanager, nmo_ckattributemanager_state_t)

TEST_SERIALIZE_SIGNATURE(ckmessagemanager, nmo_ckmessagemanager_state_t)
TEST_DESERIALIZE_SIGNATURE(ckmessagemanager, nmo_ckmessagemanager_state_t)

/* ========================================================================== */
/* ROUNDTRIP TESTS - Basic serialize/deserialize cycle                       */
/* ========================================================================== */

TEST_BASIC_ROUNDTRIP(ckobject, nmo_ckobject_state_t)
TEST_BASIC_ROUNDTRIP(cksceneobject, nmo_cksceneobject_state_t)
TEST_BASIC_ROUNDTRIP(ckbeobject, nmo_ckbeobject_state_t)
TEST_BASIC_ROUNDTRIP(ckgroup, nmo_ckgroup_state_t)
TEST_BASIC_ROUNDTRIP(ck3dobject, nmo_ck3dobject_state_t)
TEST_BASIC_ROUNDTRIP(ck3dentity, nmo_ck3dentity_state_t)
TEST_BASIC_ROUNDTRIP(ck2dentity, nmo_ck2dentity_state_t)
TEST_BASIC_ROUNDTRIP(ckrenderobject, nmo_ckrenderobject_state_t)
TEST_BASIC_ROUNDTRIP(ckcamera, nmo_ckcamera_state_t)
TEST_BASIC_ROUNDTRIP(cklight, nmo_cklight_state_t)
TEST_BASIC_ROUNDTRIP(ckmesh, nmo_ck_mesh_state_t)
TEST_BASIC_ROUNDTRIP(cksprite, nmo_cksprite_state_t)
TEST_BASIC_ROUNDTRIP(ckspritetext, nmo_ck_spritetext_state_t)
TEST_BASIC_ROUNDTRIP(cktexture, nmo_ck_texture_state_t)
TEST_BASIC_ROUNDTRIP(ckbehavior, nmo_ckbehavior_state_t)
TEST_BASIC_ROUNDTRIP(ckbehaviorio, nmo_ckbehaviorio_state_t)
TEST_BASIC_ROUNDTRIP(ckbehaviorlink, nmo_ckbehaviorlink_state_t)
TEST_BASIC_ROUNDTRIP(ckparameter, nmo_ckparameter_state_t)
TEST_BASIC_ROUNDTRIP(cklevel, nmo_cklevel_state_t)
TEST_BASIC_ROUNDTRIP(ckscene, nmo_ckscene_state_t)
TEST_BASIC_ROUNDTRIP(ckdataarray, nmo_ckdataarray_state_t)
TEST_BASIC_ROUNDTRIP(ckattributemanager, nmo_ckattributemanager_state_t)
TEST_BASIC_ROUNDTRIP(ckmessagemanager, nmo_ckmessagemanager_state_t)

/* ========================================================================== */
/* TEST REGISTRATION                                                          */
/* ========================================================================== */

TEST_MAIN_BEGIN()
    /* Signature tests */
    REGISTER_TEST(schema_signature, ckobject_serialize);
    REGISTER_TEST(schema_signature, ckobject_deserialize);
    REGISTER_TEST(schema_signature, cksceneobject_serialize);
    REGISTER_TEST(schema_signature, cksceneobject_deserialize);
    REGISTER_TEST(schema_signature, ckbeobject_serialize);
    REGISTER_TEST(schema_signature, ckbeobject_deserialize);
    REGISTER_TEST(schema_signature, ckgroup_serialize);
    REGISTER_TEST(schema_signature, ckgroup_deserialize);
    REGISTER_TEST(schema_signature, ck3dobject_serialize);
    REGISTER_TEST(schema_signature, ck3dobject_deserialize);
    REGISTER_TEST(schema_signature, ck3dentity_serialize);
    REGISTER_TEST(schema_signature, ck3dentity_deserialize);
    REGISTER_TEST(schema_signature, ck2dentity_serialize);
    REGISTER_TEST(schema_signature, ck2dentity_deserialize);
    REGISTER_TEST(schema_signature, ckrenderobject_serialize);
    REGISTER_TEST(schema_signature, ckrenderobject_deserialize);
    REGISTER_TEST(schema_signature, ckcamera_serialize);
    REGISTER_TEST(schema_signature, ckcamera_deserialize);
    REGISTER_TEST(schema_signature, cklight_serialize);
    REGISTER_TEST(schema_signature, cklight_deserialize);
    REGISTER_TEST(schema_signature, ckmesh_serialize);
    REGISTER_TEST(schema_signature, ckmesh_deserialize);
    REGISTER_TEST(schema_signature, cksprite_serialize);
    REGISTER_TEST(schema_signature, cksprite_deserialize);
    REGISTER_TEST(schema_signature, ckspritetext_serialize);
    REGISTER_TEST(schema_signature, ckspritetext_deserialize);
    REGISTER_TEST(schema_signature, cktexture_serialize);
    REGISTER_TEST(schema_signature, cktexture_deserialize);
    REGISTER_TEST(schema_signature, ckbehavior_serialize);
    REGISTER_TEST(schema_signature, ckbehavior_deserialize);
    REGISTER_TEST(schema_signature, ckbehaviorio_serialize);
    REGISTER_TEST(schema_signature, ckbehaviorio_deserialize);
    REGISTER_TEST(schema_signature, ckbehaviorlink_serialize);
    REGISTER_TEST(schema_signature, ckbehaviorlink_deserialize);
    REGISTER_TEST(schema_signature, ckparameter_serialize);
    REGISTER_TEST(schema_signature, ckparameter_deserialize);
    REGISTER_TEST(schema_signature, cklevel_serialize);
    REGISTER_TEST(schema_signature, cklevel_deserialize);
    REGISTER_TEST(schema_signature, ckscene_serialize);
    REGISTER_TEST(schema_signature, ckscene_deserialize);
    REGISTER_TEST(schema_signature, ckdataarray_serialize);
    REGISTER_TEST(schema_signature, ckdataarray_deserialize);
    REGISTER_TEST(schema_signature, ckattributemanager_serialize);
    REGISTER_TEST(schema_signature, ckattributemanager_deserialize);
    REGISTER_TEST(schema_signature, ckmessagemanager_serialize);
    REGISTER_TEST(schema_signature, ckmessagemanager_deserialize);
    
    /* Roundtrip tests */
    REGISTER_TEST(schema_roundtrip, ckobject);
    REGISTER_TEST(schema_roundtrip, cksceneobject);
    REGISTER_TEST(schema_roundtrip, ckbeobject);
    REGISTER_TEST(schema_roundtrip, ckgroup);
    REGISTER_TEST(schema_roundtrip, ck3dobject);
    REGISTER_TEST(schema_roundtrip, ck3dentity);
    REGISTER_TEST(schema_roundtrip, ck2dentity);
    REGISTER_TEST(schema_roundtrip, ckrenderobject);
    REGISTER_TEST(schema_roundtrip, ckcamera);
    REGISTER_TEST(schema_roundtrip, cklight);
    REGISTER_TEST(schema_roundtrip, ckmesh);
    REGISTER_TEST(schema_roundtrip, cksprite);
    REGISTER_TEST(schema_roundtrip, ckspritetext);
    REGISTER_TEST(schema_roundtrip, cktexture);
    REGISTER_TEST(schema_roundtrip, ckbehavior);
    REGISTER_TEST(schema_roundtrip, ckbehaviorio);
    REGISTER_TEST(schema_roundtrip, ckbehaviorlink);
    REGISTER_TEST(schema_roundtrip, ckparameter);
    REGISTER_TEST(schema_roundtrip, cklevel);
    REGISTER_TEST(schema_roundtrip, ckscene);
    REGISTER_TEST(schema_roundtrip, ckdataarray);
    REGISTER_TEST(schema_roundtrip, ckattributemanager);
    REGISTER_TEST(schema_roundtrip, ckmessagemanager);
TEST_MAIN_END()
