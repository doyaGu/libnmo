/**
 * @file test_object_serialization.c
 * @brief Integration tests for object type serialization
 */

#include "test_framework.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_object_system.h"
#include "object/nmo_object_guids.h"
#include "object/builtin/nmo_object_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "object/builtin/nmo_attributemanager_schemas.h"
#include "object/builtin/nmo_interfaceobjectmanager_schemas.h"
#include "object/builtin/nmo_messagemanager_schemas.h"
#include "type/nmo_operations.h"
#include "type/nmo_type_system.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
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

typedef struct schema_fail_allocator {
    nmo_allocator_t base;
    bool fail_allocations;
} schema_fail_allocator_t;

static void *schema_fail_alloc(void *user_data, size_t size, size_t alignment) {
    schema_fail_allocator_t *ctx = (schema_fail_allocator_t *)user_data;
    if (ctx->fail_allocations) {
        return NULL;
    }
    return nmo_alloc(&ctx->base, size, alignment);
}

static void schema_fail_free(void *user_data, void *ptr) {
    schema_fail_allocator_t *ctx = (schema_fail_allocator_t *)user_data;
    nmo_free(&ctx->base, ptr);
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

    nmo_type_registry_destroy(registry);
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

    nmo_type_registry_destroy(registry);
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

    nmo_type_registry_destroy(registry);
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

    nmo_type_registry_destroy(registry);
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

    nmo_type_registry_destroy(registry);
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

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

TEST(object_serialization, ckmaterial_uses_four_bit_compare_functions) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    nmo_chunk_set_data_version(chunk, 5u);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_MATDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0xFFFFFFFFu));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0xFF000000u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0xFF101010u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0xFF202020u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk, 2.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(chunk, NMO_OBJECT_ID_NONE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0xAA1F1F06u));
    nmo_chunk_close(chunk);

    nmo_material_state_t state;
    memset(&state, 0, sizeof(state));
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(arena, NULL, NULL, 0);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    ASSERT_EQ(NMO_OK, nmo_material_deserialize(&state, chunk, NULL, &des_ctx));
    ASSERT_EQ(0xAA0F0F06u, state.packed_flags);

    nmo_chunk_t *written = nmo_chunk_create(arena);
    ASSERT_NE(NULL, written);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(written));
    state.packed_flags = 0xAA1F1F06u;
    ASSERT_EQ(NMO_OK, nmo_material_serialize(&state, written, NULL, &ser_ctx));
    nmo_chunk_close(written);

    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(written));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(written, CK_STATESAVE_MATDATA));
    uint32_t ignored = 0;
    float ignored_float = 0.0f;
    nmo_object_id_t ignored_id = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(written, &ignored));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(written, &ignored));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(written, &ignored));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(written, &ignored));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_float(written, &ignored_float));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_object_id(written, &ignored_id));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(written, &ignored));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(written, &ignored));
    uint32_t written_flags = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(written, &written_flags));
    ASSERT_EQ(0xAA0F0F06u, written_flags);

    nmo_arena_destroy(arena);
}

static nmo_status_t deserialize_texture_count_section(
    nmo_arena_t *arena,
    uint32_t identifier,
    int32_t count,
    nmo_texture_state_t *state)
{
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    if (!chunk) return NMO_ERR_NOMEM;
    nmo_chunk_set_data_version(chunk, 5u);
    nmo_status_t result = nmo_chunk_start_write(chunk);
    if (result != NMO_OK) return result;
    result = nmo_chunk_write_identifier(chunk, identifier);
    if (result != NMO_OK) return result;
    result = nmo_chunk_write_int(chunk, count);
    if (result != NMO_OK) return result;
    nmo_chunk_close(chunk);

    result = nmo_chunk_start_read(chunk);
    if (result != NMO_OK) return result;
    nmo_deserialize_context_t context =
        nmo_deserialize_context_create(arena, NULL, NULL, 0);
    return nmo_texture_deserialize(state, chunk, NULL, &context);
}

TEST(object_serialization, cktexture_rejects_invalid_counts_before_allocation) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);

    nmo_texture_state_t state;
    memset(&state, 0, sizeof(state));
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, deserialize_texture_count_section(
        arena, CK_STATESAVE_TEXCOMPRESSED, -1, &state));
    ASSERT_EQ(0u, state.slot_count);
    ASSERT_EQ(NULL, state.raw_slots);

    memset(&state, 0, sizeof(state));
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, deserialize_texture_count_section(
        arena, CK_STATESAVE_TEXCOMPRESSED, INT32_MAX, &state));
    ASSERT_EQ(0u, state.slot_count);
    ASSERT_EQ(NULL, state.raw_slots);

    memset(&state, 0, sizeof(state));
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, deserialize_texture_count_section(
        arena, CK_STATESAVE_TEXREADER, 1, &state));
    ASSERT_EQ(0u, state.slot_count);
    ASSERT_EQ(NULL, state.reader_slots);

    nmo_arena_destroy(arena);
}

TEST(object_serialization, manager_truncation_does_not_publish_partial_arrays) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);
    nmo_deserialize_context_t context =
        nmo_deserialize_context_create(arena, NULL, NULL, 0);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, 0x53u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(chunk, "only-one"));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    nmo_messagemanager_state_t message_state = {0};
    ASSERT_NE(NMO_OK, nmo_messagemanager_deserialize(
        &message_state, chunk, NULL, &context));
    ASSERT_EQ(0u, message_state.message_type_count);
    ASSERT_EQ(NULL, message_state.message_type_names);

    chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, 0x52u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    nmo_attributemanager_state_t attribute_state = {0};
    ASSERT_NE(NMO_OK, nmo_attributemanager_deserialize(
        &attribute_state, chunk, NULL, &context));
    ASSERT_EQ(0u, attribute_state.category_count);
    ASSERT_EQ(NULL, attribute_state.categories);
    ASSERT_EQ(0u, attribute_state.attribute_count);
    ASSERT_EQ(NULL, attribute_state.attributes);

    chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, 0x01234567u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 1));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    nmo_chunk_t *preserved_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(preserved_chunk);
    nmo_chunk_t *preserved_chunks[] = {preserved_chunk};
    nmo_interfaceobjectmanager_state_t interface_state = {
        .base.visibility_flags = 0,
        .chunk_count = 1,
        .chunks = preserved_chunks,
        .has_chunks_chunk = 1,
        .guid = {0x11223344u, 0x55667788u},
        .has_guid_chunk = 1,
    };
    ASSERT_NE(NMO_OK, nmo_interfaceobjectmanager_deserialize(
        &interface_state, chunk, NULL, &context));
    ASSERT_EQ(0u, interface_state.base.visibility_flags);
    ASSERT_EQ(1, interface_state.chunk_count);
    ASSERT_EQ(preserved_chunks, interface_state.chunks);
    ASSERT_EQ(preserved_chunk, interface_state.chunks[0]);
    ASSERT_TRUE(interface_state.has_chunks_chunk);
    ASSERT_EQ(0x11223344u, interface_state.guid.d1);
    ASSERT_EQ(0x55667788u, interface_state.guid.d2);
    ASSERT_TRUE(interface_state.has_guid_chunk);

    nmo_arena_destroy(arena);
}

TEST(object_serialization, interface_manager_round_trip_is_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context =
        nmo_serialize_context_create_nonfile(arena, NULL, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, 0);

    nmo_chunk_t *subchunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(subchunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(subchunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(subchunk, 0x1234ABCDu));
    nmo_chunk_close(subchunk);
    nmo_chunk_t *chunks[] = {subchunk};

    nmo_interfaceobjectmanager_state_t source;
    ASSERT_EQ(NMO_OK, nmo_interfaceobjectmanager_vtable.create(
        &source, NULL, NULL));
    source.base.visibility_flags = 0;
    source.chunk_count = 1;
    source.chunks = chunks;
    source.has_chunks_chunk = 1;
    source.has_guid_chunk = 0;

    nmo_chunk_t *serialized = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(serialized);
    serialized->class_id = NMO_CID_INTERFACEOBJECTMANAGER;
    serialized->chunk_version = NMO_CHUNK_VERSION4;
    serialized->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_interfaceobjectmanager_serialize(
        &source, serialized, NULL, &serialize_context));
    nmo_chunk_close(serialized);
    ASSERT_NE(NMO_OK, nmo_chunk_seek_identifier(
        serialized, 0x87654321u));

    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(serialized));
    nmo_interfaceobjectmanager_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_interfaceobjectmanager_vtable.create(
        &loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_interfaceobjectmanager_deserialize(
        &loaded, serialized, NULL, &deserialize_context));
    ASSERT_EQ(0u, loaded.base.visibility_flags);
    ASSERT_TRUE(loaded.has_chunks_chunk);
    ASSERT_FALSE(loaded.has_guid_chunk);
    ASSERT_EQ(1, loaded.chunk_count);
    ASSERT_NOT_NULL(loaded.chunks);
    ASSERT_NOT_NULL(loaded.chunks[0]);
    ASSERT_TRUE(nmo_interfaceobjectmanager_vtable.equals(&source, &loaded));
    ASSERT_EQ(nmo_interfaceobjectmanager_vtable.hash(&source),
              nmo_interfaceobjectmanager_vtable.hash(&loaded));

    nmo_interfaceobjectmanager_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_interfaceobjectmanager_vtable.create(
        &copied, NULL, NULL));
    const nmo_type_descriptor_t type = {
        .size = sizeof(nmo_interfaceobjectmanager_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_interfaceobjectmanager_vtable.copy(
        &loaded, &copied, &type, arena));
    ASSERT_TRUE(copied.chunks != loaded.chunks);
    ASSERT_TRUE(copied.chunks[0] != loaded.chunks[0]);
    ASSERT_TRUE(nmo_interfaceobjectmanager_vtable.equals(&loaded, &copied));
    copied.has_guid_chunk = 1;
    copied.guid = (nmo_guid_t){0xAABBCCDDu, 0xEEFF0011u};
    ASSERT_FALSE(nmo_interfaceobjectmanager_vtable.equals(&loaded, &copied));

    nmo_chunk_t *preserved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(preserved);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(preserved, 0xCAFEBABEu));
    nmo_chunk_close(preserved);
    nmo_interfaceobjectmanager_state_t invalid = source;
    invalid.chunks = NULL;
    ASSERT_NE(NMO_OK, nmo_interfaceobjectmanager_serialize(
        &invalid, preserved, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(preserved));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(preserved, &marker));
    ASSERT_EQ(0xCAFEBABEu, marker);

    invalid = source;
    invalid.chunk_count = -1;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_interfaceobjectmanager_vtable.validate(
                  &invalid, NULL, NULL));

    nmo_interfaceobjectmanager_vtable.destroy(&source, NULL, NULL);
    nmo_interfaceobjectmanager_vtable.destroy(&loaded, NULL, NULL);
    nmo_interfaceobjectmanager_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_serialization, failed_schema_releases_object_state) {
    nmo_arena_t *registry_arena = nmo_arena_create(NULL, 131072);
    nmo_arena_t *chunk_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(registry_arena);
    ASSERT_NOT_NULL(chunk_arena);

    nmo_type_registry_t *registry = nmo_type_registry_create(registry_arena);
    ASSERT_NOT_NULL(registry);
    ASSERT_EQ(NMO_OK, register_test_object_types(registry));
    nmo_type_runtime_t type_runtime = { .types = registry, .ops = NULL };

    nmo_allocator_t base = nmo_allocator_default();
    nmo_allocator_stats_t stats = {0};
    nmo_allocator_tracking_t tracking = {0};
    nmo_allocator_t tracked = nmo_allocator_tracking_init(&tracking, base, &stats);

    nmo_object_repository_t *repo = nmo_object_repository_create(NULL);
    ASSERT_NOT_NULL(repo);
    nmo_object_t *object = nmo_object_create(&tracked, 1, NMO_CID_TEXTURE);
    ASSERT_NOT_NULL(object);

    nmo_chunk_t *chunk = nmo_chunk_create(chunk_arena);
    ASSERT_NOT_NULL(chunk);
    nmo_chunk_set_data_version(chunk, 5u);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_TEXREADER));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1));
    nmo_chunk_close(chunk);
    object->chunk = chunk;

    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &object));
    ASSERT_NULL(object);
    nmo_object_t *owned = nmo_object_repository_get_by_index(repo, 0);
    ASSERT_NOT_NULL(owned);
    const size_t bytes_before = stats.current_bytes;

    nmo_object_system_deserialize_stats_t deserialize_stats = {0};
    ASSERT_EQ(NMO_OK, nmo_object_system_deserialize_repository(
        repo, &type_runtime, registry_arena, NULL, NULL, 0, &deserialize_stats));
    ASSERT_EQ((size_t)1, deserialize_stats.errors);
    ASSERT_NULL(owned->state);
    ASSERT_EQ(0u, owned->state_size);
    ASSERT_EQ((size_t)0,
              nmo_arena_bytes_used(nmo_object_get_storage_arena(owned)));
    ASSERT_EQ(bytes_before, stats.current_bytes);

    nmo_object_repository_destroy(repo);
    ASSERT_EQ((size_t)0, stats.current_bytes);
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(chunk_arena);
    nmo_arena_destroy(registry_arena);
}

TEST(object_serialization, repository_deserialize_uses_explicit_object_type) {
    nmo_arena_t *registry_arena = nmo_arena_create(NULL, 131072);
    nmo_arena_t *chunk_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(registry_arena);
    ASSERT_NOT_NULL(chunk_arena);

    nmo_type_registry_t *registry = nmo_type_registry_create(registry_arena);
    ASSERT_NOT_NULL(registry);
    ASSERT_EQ(NMO_OK, register_test_object_types(registry));
    nmo_type_registry_compute_state_layouts(registry);
    nmo_type_runtime_t type_runtime = {.types = registry, .ops = NULL};

    nmo_object_repository_t *repo = nmo_object_repository_create(NULL);
    ASSERT_NOT_NULL(repo);
    nmo_object_t *object = nmo_object_create(NULL, 1u, 0);
    ASSERT_NOT_NULL(object);
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(object, CKPGUID_OBJECT));
    object->chunk = nmo_chunk_create(chunk_arena);
    ASSERT_NOT_NULL(object->chunk);
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &object));
    ASSERT_NULL(object);

    nmo_object_system_deserialize_stats_t stats = {0};
    ASSERT_EQ(NMO_OK, nmo_object_system_deserialize_repository(
        repo, &type_runtime, registry_arena, NULL, NULL, 0, &stats));
    ASSERT_EQ(1u, stats.deserialized);
    ASSERT_EQ(0u, stats.no_schema);
    nmo_object_t *loaded = nmo_object_repository_get_by_index(repo, 0);
    ASSERT_NOT_NULL(loaded);
    ASSERT_NOT_NULL(loaded->state);
    ASSERT_EQ(sizeof(nmo_object_state_t), loaded->state_size);

    nmo_object_repository_destroy(repo);
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(chunk_arena);
    nmo_arena_destroy(registry_arena);
}

TEST(object_serialization, schema_allocation_failure_is_transactional) {
    nmo_arena_t *chunk_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(chunk_arena);

    schema_fail_allocator_t fail_ctx = {
        .base = nmo_allocator_default(),
        .fail_allocations = false,
    };
    nmo_allocator_t allocator = nmo_allocator_custom(
        schema_fail_alloc, schema_fail_free, &fail_ctx);
    nmo_object_t *object = nmo_object_create(&allocator, 1, NMO_CID_TEXTURE);
    ASSERT_NOT_NULL(object);
    nmo_arena_t *storage_arena = nmo_object_get_storage_arena(object);
    ASSERT_NOT_NULL(storage_arena);
    ASSERT_NOT_NULL(nmo_arena_alloc(
        storage_arena, nmo_arena_total_allocated(storage_arena), 1));

    nmo_chunk_t *chunk = nmo_chunk_create(chunk_arena);
    ASSERT_NOT_NULL(chunk);
    nmo_chunk_set_data_version(chunk, 5u);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_TEXREADER));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 32));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));

    nmo_deserialize_context_t context =
        nmo_deserialize_context_create(chunk_arena, NULL, NULL, 0);
    nmo_deserialize_context_set_object(&context, object);
    nmo_texture_state_t state = {0};
    fail_ctx.fail_allocations = true;
    ASSERT_EQ(NMO_ERR_NOMEM,
              nmo_texture_deserialize(&state, chunk, NULL, &context));
    ASSERT_EQ(0u, state.slot_count);
    ASSERT_NULL(state.reader_slots);

    fail_ctx.fail_allocations = false;
    nmo_object_destroy(object);
    nmo_arena_destroy(chunk_arena);
}

TEST(object_serialization, object_system_propagates_allocation_failure) {
    nmo_arena_t *registry_arena = nmo_arena_create(NULL, 131072);
    nmo_arena_t *chunk_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(registry_arena);
    ASSERT_NOT_NULL(chunk_arena);

    nmo_type_registry_t *registry = nmo_type_registry_create(registry_arena);
    ASSERT_NOT_NULL(registry);
    ASSERT_EQ(NMO_OK, register_test_object_types(registry));
    nmo_type_runtime_t type_runtime = { .types = registry, .ops = NULL };

    schema_fail_allocator_t fail_ctx = {
        .base = nmo_allocator_default(),
        .fail_allocations = false,
    };
    nmo_allocator_t allocator = nmo_allocator_custom(
        schema_fail_alloc, schema_fail_free, &fail_ctx);
    nmo_object_repository_t *repo = nmo_object_repository_create(NULL);
    ASSERT_NOT_NULL(repo);
    nmo_object_t *object = nmo_object_create(&allocator, 1, NMO_CID_TEXTURE);
    ASSERT_NOT_NULL(object);

    nmo_chunk_t *chunk = nmo_chunk_create(chunk_arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0));
    nmo_chunk_close(chunk);
    object->chunk = chunk;
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &object));
    ASSERT_NULL(object);

    fail_ctx.fail_allocations = true;
    nmo_object_system_deserialize_stats_t deserialize_stats = {0};
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_object_system_deserialize_repository(
        repo, &type_runtime, registry_arena, NULL, NULL, 0, &deserialize_stats));
    ASSERT_EQ((size_t)1, deserialize_stats.errors);
    nmo_object_t *owned = nmo_object_repository_get_by_index(repo, 0);
    ASSERT_NOT_NULL(owned);
    ASSERT_NULL(owned->state);
    ASSERT_EQ(chunk, owned->chunk);

    fail_ctx.fail_allocations = false;
    nmo_object_repository_destroy(repo);
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(chunk_arena);
    nmo_arena_destroy(registry_arena);
}

TEST(object_serialization, repository_deserialize_propagates_chunk_reader_oom) {
    nmo_arena_t *registry_arena = nmo_arena_create(NULL, 131072);
    ASSERT_NOT_NULL(registry_arena);

    schema_fail_allocator_t fail_ctx = {
        .base = nmo_allocator_default(),
        .fail_allocations = false,
    };
    nmo_allocator_t allocator = nmo_allocator_custom(
        schema_fail_alloc, schema_fail_free, &fail_ctx);
    nmo_arena_config_t chunk_config = nmo_arena_default_config();
    chunk_config.initial_block_size = 4096;
    chunk_config.growth_factor = 1.0f;
    nmo_arena_t *chunk_arena = nmo_arena_create_ex(
        &allocator, &chunk_config);
    ASSERT_NOT_NULL(chunk_arena);

    nmo_type_registry_t *registry = nmo_type_registry_create(registry_arena);
    ASSERT_NOT_NULL(registry);
    ASSERT_EQ(NMO_OK, register_test_object_types(registry));
    nmo_type_runtime_t type_runtime = { .types = registry, .ops = NULL };

    nmo_object_repository_t *repo = nmo_object_repository_create(NULL);
    ASSERT_NOT_NULL(repo);
    nmo_object_t *object = nmo_object_create(NULL, 1, NMO_CID_OBJECT);
    ASSERT_NOT_NULL(object);
    nmo_chunk_t *chunk = nmo_chunk_create(chunk_arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 123));
    nmo_chunk_close(chunk);
    chunk->parser_state = NULL;
    object->chunk = chunk;
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &object));
    ASSERT_NULL(object);

    ASSERT_NOT_NULL(nmo_arena_alloc(
        chunk_arena, nmo_arena_total_allocated(chunk_arena), 1));
    fail_ctx.fail_allocations = true;

    nmo_object_system_deserialize_stats_t stats = {0};
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_object_system_deserialize_repository(
        repo, &type_runtime, registry_arena, NULL, NULL, 0, &stats));
    ASSERT_EQ((size_t)1, stats.errors);
    nmo_object_t *owned = nmo_object_repository_get_by_index(repo, 0);
    ASSERT_NOT_NULL(owned);
    ASSERT_NULL(owned->state);
    ASSERT_EQ(0u, owned->state_size);
    ASSERT_EQ(chunk, owned->chunk);

    fail_ctx.fail_allocations = false;
    nmo_object_repository_destroy(repo);
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(chunk_arena);
    nmo_arena_destroy(registry_arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(object_serialization, ckobject_roundtrip);
    REGISTER_TEST(object_serialization, ckobject_hidden);
    REGISTER_TEST(object_serialization, ckobject_hierarchical_hidden);
    REGISTER_TEST(object_serialization, null_checks);
    REGISTER_TEST(object_serialization, ck3dentity_roundtrip);
    REGISTER_TEST(object_serialization, ck3dentity_transform);
    REGISTER_TEST(object_serialization, ckmaterial_uses_four_bit_compare_functions);
    REGISTER_TEST(object_serialization, cktexture_rejects_invalid_counts_before_allocation);
    REGISTER_TEST(object_serialization, manager_truncation_does_not_publish_partial_arrays);
    REGISTER_TEST(object_serialization, interface_manager_round_trip_is_atomic);
    REGISTER_TEST(object_serialization, failed_schema_releases_object_state);
    REGISTER_TEST(object_serialization, repository_deserialize_uses_explicit_object_type);
    REGISTER_TEST(object_serialization, schema_allocation_failure_is_transactional);
    REGISTER_TEST(object_serialization, object_system_propagates_allocation_failure);
    REGISTER_TEST(object_serialization, repository_deserialize_propagates_chunk_reader_oom);
TEST_MAIN_END()
