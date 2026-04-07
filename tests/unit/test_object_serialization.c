/**
 * @file test_object_serialization.c
 * @brief Integration tests for object type serialization
 */

#include "test_framework.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_guids.h"
#include "object/builtin/nmo_object_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "type/nmo_operations.h"
#include "type/nmo_type_system.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_statesave_ids.h"
#include <string.h>

static nmo_status_t register_test_object_types(nmo_type_registry_t *registry) {
    nmo_status_t result = nmo_register_builtin_types(registry);
    if (result != NMO_OK) {
        return result;
    }
    return nmo_register_object_types(registry);
}

/* Test: Serialize and deserialize CKObject state */
TEST(object_serialization, ckobject_roundtrip) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create_nonfile(
        arena, NULL, 0);

    /* Setup type registry */
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    nmo_status_t result = register_test_object_types(registry);
    ASSERT_EQ(NMO_OK, result);

    /* Get CKObject type */
    const nmo_type_descriptor_t *ckobject_type = nmo_type_registry_find_by_guid(
        registry, CKPGUID_OBJECT);
    ASSERT_NE(NULL, ckobject_type);
    ASSERT_NE(NULL, ckobject_type->vtable);
    ASSERT_NE(NULL, ckobject_type->vtable->serialize);
    ASSERT_NE(NULL, ckobject_type->vtable->deserialize);

    /* Create chunk for serialization */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    nmo_chunk_start_write(chunk);

    /* Create object state (visible) */
    nmo_object_state_t state_out = {
        .visibility_flags = NMO_CKOBJECT_VISIBLE
    };

    /* Serialize */
    result = ckobject_type->vtable->serialize(&state_out, chunk, ckobject_type, &ser_ctx);
    ASSERT_EQ(NMO_OK, result);
    nmo_chunk_close(chunk);

    /* Deserialize */
    nmo_chunk_start_read(chunk);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(arena, NULL, NULL, 0);
    nmo_object_state_t state_in = {0};
    result = ckobject_type->vtable->deserialize(&state_in, chunk, ckobject_type, &des_ctx);
    ASSERT_EQ(NMO_OK, result);

    /* Verify */
    ASSERT_EQ(state_out.visibility_flags, state_in.visibility_flags);
    ASSERT_TRUE(state_in.visibility_flags & NMO_CKOBJECT_VISIBLE);

    nmo_arena_destroy(arena);
}

/* Test: Serialize hidden object */
TEST(object_serialization, ckobject_hidden) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_status_t result = register_test_object_types(registry);
    ASSERT_EQ(NMO_OK, result);

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create_nonfile(
        arena, NULL, 0);

    const nmo_type_descriptor_t *ckobject_type = nmo_type_registry_find_by_guid(
        registry, CKPGUID_OBJECT);
    ASSERT_NE(NULL, ckobject_type);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    nmo_chunk_start_write(chunk);

    /* Create hidden object state */
    nmo_object_state_t state_out = {
        .visibility_flags = 0  /* Completely hidden */
    };

    /* Serialize */
    result = ckobject_type->vtable->serialize(&state_out, chunk, ckobject_type, &ser_ctx);
    ASSERT_EQ(NMO_OK, result);
    nmo_chunk_close(chunk);

    /* Deserialize */
    nmo_chunk_start_read(chunk);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(arena, NULL, NULL, 0);
    nmo_object_state_t state_in = {0};
    result = ckobject_type->vtable->deserialize(&state_in, chunk, ckobject_type, &des_ctx);
    ASSERT_EQ(NMO_OK, result);

    /* Verify hidden state */
    ASSERT_EQ(0, state_in.visibility_flags);
    ASSERT_FALSE(state_in.visibility_flags & NMO_CKOBJECT_VISIBLE);

    nmo_arena_destroy(arena);
}

/* Test: Serialize hierarchically hidden object */
TEST(object_serialization, ckobject_hierarchical_hidden) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_status_t result = register_test_object_types(registry);
    ASSERT_EQ(NMO_OK, result);

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create_nonfile(
        arena, NULL, 0);

    const nmo_type_descriptor_t *ckobject_type = nmo_type_registry_find_by_guid(
        registry, CKPGUID_OBJECT);
    ASSERT_NE(NULL, ckobject_type);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    nmo_chunk_start_write(chunk);

    /* Create hierarchically hidden object state */
    nmo_object_state_t state_out = {
        .visibility_flags = NMO_CKOBJECT_HIERARCHICAL  /* No VISIBLE flag */
    };

    /* Serialize */
    result = ckobject_type->vtable->serialize(&state_out, chunk, ckobject_type, &ser_ctx);
    ASSERT_EQ(NMO_OK, result);
    nmo_chunk_close(chunk);

    /* Deserialize */
    nmo_chunk_start_read(chunk);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(arena, NULL, NULL, 0);
    nmo_object_state_t state_in = {0};
    result = ckobject_type->vtable->deserialize(&state_in, chunk, ckobject_type, &des_ctx);
    ASSERT_EQ(NMO_OK, result);

    /* Verify hierarchical hidden state */
    ASSERT_EQ(NMO_CKOBJECT_HIERARCHICAL, state_in.visibility_flags);
    ASSERT_FALSE(state_in.visibility_flags & NMO_CKOBJECT_VISIBLE);
    ASSERT_TRUE(state_in.visibility_flags & NMO_CKOBJECT_HIERARCHICAL);

    nmo_arena_destroy(arena);
}

/* Test: NULL checks */
TEST(object_serialization, null_checks) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_status_t result = register_test_object_types(registry);
    ASSERT_EQ(NMO_OK, result);

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create_nonfile(
        arena, NULL, 0);

    const nmo_type_descriptor_t *ckobject_type = nmo_type_registry_find_by_guid(
        registry, CKPGUID_OBJECT);
    ASSERT_NE(NULL, ckobject_type);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(arena, NULL, NULL, 0);
    nmo_object_state_t state = {0};

    /* NULL instance in serialize */
    result = ckobject_type->vtable->serialize(NULL, chunk, ckobject_type, &ser_ctx);
    ASSERT_NE(NMO_OK, result);

    /* NULL chunk in serialize */
    result = ckobject_type->vtable->serialize(&state, NULL, ckobject_type, &ser_ctx);
    ASSERT_NE(NMO_OK, result);

    /* NULL instance in deserialize */
    result = ckobject_type->vtable->deserialize(NULL, chunk, ckobject_type, &des_ctx);
    ASSERT_NE(NMO_OK, result);

    /* NULL chunk in deserialize */
    result = ckobject_type->vtable->deserialize(&state, NULL, ckobject_type, &des_ctx);
    ASSERT_NE(NMO_OK, result);

    nmo_arena_destroy(arena);
}

/* Test: CK3dEntity roundtrip */
TEST(object_serialization, ck3dentity_roundtrip) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_status_t result = register_test_object_types(registry);
    ASSERT_EQ(NMO_OK, result);

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    const nmo_type_descriptor_t *entity3d_type = nmo_type_registry_find_by_guid(
        registry, CKPGUID_3DENTITY);
    ASSERT_NE(NULL, entity3d_type);
    ASSERT_NE(NULL, entity3d_type->vtable);
    ASSERT_NE(NULL, entity3d_type->vtable->serialize);
    ASSERT_NE(NULL, entity3d_type->vtable->deserialize);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    nmo_chunk_start_write(chunk);

    /* Create 3D entity state with identity matrix */
    nmo_3dentity_state_t state_out;
    memset(&state_out, 0, sizeof(state_out));
    state_out.base.base.base.base.visibility_flags = NMO_CKOBJECT_VISIBLE;
    state_out.entity_flags = 0x12345678;
    state_out.world_matrix[0]  = 1.0f;
    state_out.world_matrix[5]  = 1.0f;
    state_out.world_matrix[10] = 1.0f;
    state_out.world_matrix[15] = 1.0f;
    state_out.z_order = 100;

    /* Serialize */
    result = entity3d_type->vtable->serialize(&state_out, chunk, entity3d_type, &ser_ctx);
    ASSERT_EQ(NMO_OK, result);
    nmo_chunk_close(chunk);

    /* Deserialize */
    nmo_chunk_start_read(chunk);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(arena, NULL, NULL, 0);
    nmo_3dentity_state_t state_in;
    memset(&state_in, 0, sizeof(state_in));
    result = entity3d_type->vtable->deserialize(&state_in, chunk, entity3d_type, &des_ctx);
    ASSERT_EQ(NMO_OK, result);

    /* Verify base object state */
    ASSERT_EQ(state_out.base.base.base.base.visibility_flags,
              state_in.base.base.base.base.visibility_flags);

    /* Verify 3D-specific state */
    ASSERT_EQ(state_out.entity_flags, state_in.entity_flags);
    ASSERT_EQ(state_out.z_order, state_in.z_order);

    /* Verify matrix */
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ(state_out.world_matrix[i], state_in.world_matrix[i]);
    }

    nmo_arena_destroy(arena);
}

/* Test: CK3dEntity with transform */
TEST(object_serialization, ck3dentity_transform) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_status_t result = register_test_object_types(registry);
    ASSERT_EQ(NMO_OK, result);

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    const nmo_type_descriptor_t *entity3d_type = nmo_type_registry_find_by_guid(
        registry, CKPGUID_3DENTITY);
    ASSERT_NE(NULL, entity3d_type);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    nmo_chunk_start_write(chunk);

    /* Create 3D entity with translation */
    nmo_3dentity_state_t state_out;
    memset(&state_out, 0, sizeof(state_out));
    state_out.base.base.base.base.visibility_flags = NMO_CKOBJECT_VISIBLE;
    state_out.entity_flags = 0x00000001;
    state_out.world_matrix[0]  = 1.0f;
    state_out.world_matrix[5]  = 1.0f;
    state_out.world_matrix[10] = 1.0f;
    state_out.world_matrix[12] = 10.0f;  /* Translation X */
    state_out.world_matrix[13] = 20.0f;  /* Translation Y */
    state_out.world_matrix[14] = 30.0f;  /* Translation Z */
    state_out.world_matrix[15] = 1.0f;

    /* Serialize and deserialize */
    result = entity3d_type->vtable->serialize(&state_out, chunk, entity3d_type, &ser_ctx);
    ASSERT_EQ(NMO_OK, result);
    nmo_chunk_close(chunk);

    nmo_chunk_start_read(chunk);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(arena, NULL, NULL, 0);
    nmo_3dentity_state_t state_in;
    memset(&state_in, 0, sizeof(state_in));
    result = entity3d_type->vtable->deserialize(&state_in, chunk, entity3d_type, &des_ctx);
    ASSERT_EQ(NMO_OK, result);

    /* Verify translation components */
    ASSERT_EQ(10.0f, state_in.world_matrix[12]);
    ASSERT_EQ(20.0f, state_in.world_matrix[13]);
    ASSERT_EQ(30.0f, state_in.world_matrix[14]);

    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(object_serialization, ckobject_roundtrip);
    REGISTER_TEST(object_serialization, ckobject_hidden);
    REGISTER_TEST(object_serialization, ckobject_hierarchical_hidden);
    REGISTER_TEST(object_serialization, null_checks);
    REGISTER_TEST(object_serialization, ck3dentity_roundtrip);
    REGISTER_TEST(object_serialization, ck3dentity_transform);
TEST_MAIN_END()
