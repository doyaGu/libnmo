/**
 * @file test_object_serialization.c
 * @brief Integration tests for object type serialization (Phase 6.1)
 */

#include "test_framework.h"
#include "object/nmo_object_types.h"
#include "type/nmo_operations.h"
#include "type/nmo_type_system.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "object/nmo_serialize_context.h"
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

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_NONE);

    /* Setup type registry */
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    nmo_status_t result = register_test_object_types(registry);
    ASSERT_EQ(NMO_OK, result);

    /* Get CKObject type */
    const nmo_type_descriptor_t *ckobject_type = nmo_type_registry_find_by_guid(
        registry, NMO_GUID_CKOBJECT);
    ASSERT_NE(NULL, ckobject_type);
    ASSERT_NE(NULL, ckobject_type->vtable);
    ASSERT_NE(NULL, ckobject_type->vtable->serialize);
    ASSERT_NE(NULL, ckobject_type->vtable->deserialize);

    /* Create chunk for serialization */
    nmo_chunk_t *chunk = nmo_chunk_create(arena, 1024);
    ASSERT_NE(NULL, chunk);
    nmo_chunk_start_write(chunk);

    /* Create object state (visible) */
    nmo_object_state_t state_out = {
        .visibility_flags = NMO_CKOBJECT_VISIBLE
    };

    /* Serialize */
    result = ckobject_type->vtable->serialize(&state_out, chunk, ckobject_type, &ser_ctx);
    ASSERT_EQ(NMO_OK, result);

    /* Prepare for deserialization */
    nmo_chunk_start_read(chunk);

    /* Deserialize */
    nmo_object_state_t state_in = {0};
    result = ckobject_type->vtable->deserialize(&state_in, chunk, ckobject_type, &ser_ctx);
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

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_NONE);

    const nmo_type_descriptor_t *ckobject_type = nmo_type_registry_find_by_guid(
        registry, NMO_GUID_CKOBJECT);
    ASSERT_NE(NULL, ckobject_type);

    nmo_chunk_t *chunk = nmo_chunk_create(arena, 1024);
    nmo_chunk_start_write(chunk);

    /* Create hidden object state */
    nmo_object_state_t state_out = {
        .visibility_flags = 0  /* Completely hidden */
    };

    /* Serialize */
    nmo_status_t result = ckobject_type->vtable->serialize(&state_out, chunk, ckobject_type, &ser_ctx);
    ASSERT_EQ(NMO_OK, result);

    /* Deserialize */
    nmo_chunk_start_read(chunk);
    nmo_object_state_t state_in = {0};
    result = ckobject_type->vtable->deserialize(&state_in, chunk, ckobject_type, &ser_ctx);
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

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_NONE);

    const nmo_type_descriptor_t *ckobject_type = nmo_type_registry_find_by_guid(
        registry, NMO_GUID_CKOBJECT);
    ASSERT_NE(NULL, ckobject_type);

    nmo_chunk_t *chunk = nmo_chunk_create(arena, 1024);
    nmo_chunk_start_write(chunk);

    /* Create hierarchically hidden object state */
    nmo_object_state_t state_out = {
        .visibility_flags = NMO_CKOBJECT_HIERARCHICAL  /* No VISIBLE flag */
    };

    /* Serialize */
    nmo_status_t result = ckobject_type->vtable->serialize(&state_out, chunk, ckobject_type, &ser_ctx);
    ASSERT_EQ(NMO_OK, result);

    /* Deserialize */
    nmo_chunk_start_read(chunk);
    nmo_object_state_t state_in = {0};
    result = ckobject_type->vtable->deserialize(&state_in, chunk, ckobject_type, &ser_ctx);
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

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_NONE);

    const nmo_type_descriptor_t *ckobject_type = nmo_type_registry_find_by_guid(
        registry, NMO_GUID_CKOBJECT);
    ASSERT_NE(NULL, ckobject_type);

    nmo_chunk_t *chunk = nmo_chunk_create(arena, 1024);
    nmo_object_state_t state = {0};

    /* NULL instance in serialize */
    nmo_status_t result = ckobject_type->vtable->serialize(NULL, chunk, ckobject_type, &ser_ctx);
    ASSERT_NE(NMO_OK, result);

    /* NULL chunk in serialize */
    result = ckobject_type->vtable->serialize(&state, NULL, ckobject_type, &ser_ctx);
    ASSERT_NE(NMO_OK, result);

    /* NULL instance in deserialize */
    result = ckobject_type->vtable->deserialize(NULL, chunk, ckobject_type, &ser_ctx);
    ASSERT_NE(NMO_OK, result);

    /* NULL chunk in deserialize */
    result = ckobject_type->vtable->deserialize(&state, NULL, ckobject_type, &ser_ctx);
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
        arena, NULL, NMO_SERIALIZE_FLAG_NONE);

    const nmo_type_descriptor_t *entity3d_type = nmo_type_registry_find_by_guid(
        registry, NMO_GUID_CK3DENTITY);
    ASSERT_NE(NULL, entity3d_type);
    ASSERT_NE(NULL, entity3d_type->vtable);
    ASSERT_NE(NULL, entity3d_type->vtable->serialize);
    ASSERT_NE(NULL, entity3d_type->vtable->deserialize);

    nmo_chunk_t *chunk = nmo_chunk_create(arena, 4096);
    nmo_chunk_start_write(chunk);

    /* Create 3D entity state with identity matrix */
    nmo_3dentity_state_t state_out = {
        .base = { .visibility_flags = NMO_CKOBJECT_VISIBLE },
        .flags = 0x12345678,
        .world_matrix = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        },
        .zorder = 100
    };

    /* Serialize */
    nmo_status_t result = entity3d_type->vtable->serialize(&state_out, chunk, entity3d_type, &ser_ctx);
    ASSERT_EQ(NMO_OK, result);

    /* Deserialize */
    nmo_chunk_start_read(chunk);
    nmo_3dentity_state_t state_in = {0};
    result = entity3d_type->vtable->deserialize(&state_in, chunk, entity3d_type, &ser_ctx);
    ASSERT_EQ(NMO_OK, result);

    /* Verify base object state */
    ASSERT_EQ(state_out.base.visibility_flags, state_in.base.visibility_flags);

    /* Verify 3D-specific state */
    ASSERT_EQ(state_out.flags, state_in.flags);
    ASSERT_EQ(state_out.zorder, state_in.zorder);

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
        arena, NULL, NMO_SERIALIZE_FLAG_NONE);

    const nmo_type_descriptor_t *entity3d_type = nmo_type_registry_find_by_guid(
        registry, NMO_GUID_CK3DENTITY);
    ASSERT_NE(NULL, entity3d_type);

    nmo_chunk_t *chunk = nmo_chunk_create(arena, 4096);
    nmo_chunk_start_write(chunk);

    /* Create 3D entity with translation */
    nmo_3dentity_state_t state_out = {
        .base = { .visibility_flags = NMO_CKOBJECT_VISIBLE },
        .flags = 0x00000001,
        .world_matrix = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            10.0f, 20.0f, 30.0f, 1.0f  /* Translation */
        },
        .zorder = 50
    };

    /* Serialize and deserialize */
    nmo_status_t result = entity3d_type->vtable->serialize(&state_out, chunk, entity3d_type, &ser_ctx);
    ASSERT_EQ(NMO_OK, result);

    nmo_chunk_start_read(chunk);
    nmo_3dentity_state_t state_in = {0};
    result = entity3d_type->vtable->deserialize(&state_in, chunk, entity3d_type, &ser_ctx);
    ASSERT_EQ(NMO_OK, result);

    /* Verify translation components */
    ASSERT_EQ(10.0f, state_in.world_matrix[12]);
    ASSERT_EQ(20.0f, state_in.world_matrix[13]);
    ASSERT_EQ(30.0f, state_in.world_matrix[14]);

    nmo_arena_destroy(arena);
}
