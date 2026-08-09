/**
 * @file test_objanim_controllers.c
 * @brief Unit tests for CKObjectAnimation controller parsing
 */

#include "test_framework.h"
#include "type/nmo_operations.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_guids.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "type/nmo_type_system.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_statesave_ids.h"
#include <string.h>

/* Controller type constants (matching ckanimation_schemas.c) */
#define CKANIMATION_LINPOS_CONTROL      0x637c4301u
#define CKANIMATION_LINROT_CONTROL      0x49ed4002u
#define CKANIMATION_LINSCL_CONTROL      0x654a3a04u
#define CKANIMATION_LINSCLAXIS_CONTROL  0x2f200b08u

static nmo_status_t register_test_types(nmo_type_registry_t *registry) {
    nmo_status_t result = nmo_register_builtin_types(registry);
    if (result != NMO_OK) return result;
    return nmo_register_object_types(registry);
}

/* ========================================================================
 * Test: CONTROLLERS format round-trip
 * ======================================================================== */
TEST(objanim_controllers, controllers_roundtrip) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);

    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_status_t result = register_test_types(registry);
    ASSERT_EQ(NMO_OK, result);

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(
        registry, CKPGUID_OBJECTANIMATION);
    ASSERT_NE(NULL, type);

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    /* Build state with 2 controllers */
    nmo_objectanimation_state_t state_out;
    memset(&state_out, 0, sizeof(state_out));
    state_out.base.base.visibility_flags = 1;
    state_out.format = CKOBJANIM_FORMAT_CONTROLLERS;
    state_out.flags = 0x01;
    state_out.has_length = 1;
    state_out.length = 10.0f;

    /* Fake position key data: 2 keys x 16 bytes = 32 bytes */
    float pos_keys[8] = {
        0.0f, 1.0f, 2.0f, 3.0f,   /* key0: time=0, pos=(1,2,3) */
        5.0f, 4.0f, 5.0f, 6.0f    /* key1: time=5, pos=(4,5,6) */
    };
    /* Fake rotation key data: 1 key x 20 bytes = 20 bytes */
    float rot_keys[5] = {
        0.0f, 0.0f, 0.0f, 0.0f, 1.0f  /* time=0, quat=(0,0,0,1) */
    };

    nmo_objanim_controller_t controllers[2];
    controllers[0].type = CKANIMATION_LINPOS_CONTROL;
    controllers[0].key_count = 0;  /* CONTROLLERS format doesn't store key_count */
    controllers[0].data_size = 32;
    controllers[0].data = pos_keys;
    controllers[1].type = CKANIMATION_LINROT_CONTROL;
    controllers[1].key_count = 0;
    controllers[1].data_size = 20;
    controllers[1].data = rot_keys;

    state_out.controller_count = 2;
    state_out.controllers = controllers;

    /* Serialize */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    nmo_chunk_start_write(chunk);
    result = type->vtable->serialize(&state_out, chunk, type, &ser_ctx);
    ASSERT_EQ(NMO_OK, result);
    nmo_chunk_close(chunk);

    /* Deserialize */
    nmo_chunk_start_read(chunk);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(arena, NULL, NULL, 0);
    nmo_objectanimation_state_t state_in;
    memset(&state_in, 0, sizeof(state_in));
    result = type->vtable->deserialize(&state_in, chunk, type, &des_ctx);
    ASSERT_EQ(NMO_OK, result);

    /* Verify format */
    ASSERT_EQ(CKOBJANIM_FORMAT_CONTROLLERS, state_in.format);

    /* Verify controllers parsed */
    ASSERT_EQ(2, state_in.controller_count);
    ASSERT_NE(NULL, state_in.controllers);

    /* Verify controller 0: position */
    ASSERT_EQ(CKANIMATION_LINPOS_CONTROL, state_in.controllers[0].type);
    ASSERT_EQ(32, state_in.controllers[0].data_size);
    ASSERT_NE(NULL, state_in.controllers[0].data);
    ASSERT_EQ(0, memcmp(pos_keys, state_in.controllers[0].data, 32));

    /* Verify controller 1: rotation */
    ASSERT_EQ(CKANIMATION_LINROT_CONTROL, state_in.controllers[1].type);
    ASSERT_EQ(20, state_in.controllers[1].data_size);
    ASSERT_NE(NULL, state_in.controllers[1].data);
    ASSERT_EQ(0, memcmp(rot_keys, state_in.controllers[1].data, 20));

    /* Verify header fields */
    ASSERT_EQ(1, state_in.has_length);
    ASSERT_EQ(10.0f, state_in.length);

    /* No raw_tail should remain */
    ASSERT_EQ(0, state_in.raw_tail_size);

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ========================================================================
 * Test: CONTROLLERS format empty (just terminator)
 * ======================================================================== */
TEST(objanim_controllers, controllers_empty) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_status_t result = register_test_types(registry);
    ASSERT_EQ(NMO_OK, result);

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(
        registry, CKPGUID_OBJECTANIMATION);
    ASSERT_NE(NULL, type);

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    /* State with no controllers */
    nmo_objectanimation_state_t state_out;
    memset(&state_out, 0, sizeof(state_out));
    state_out.base.base.visibility_flags = 1;
    state_out.format = CKOBJANIM_FORMAT_CONTROLLERS;
    state_out.has_length = 1;
    state_out.length = 5.0f;

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    nmo_chunk_start_write(chunk);
    result = type->vtable->serialize(&state_out, chunk, type, &ser_ctx);
    ASSERT_EQ(NMO_OK, result);
    nmo_chunk_close(chunk);

    nmo_chunk_start_read(chunk);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(arena, NULL, NULL, 0);
    nmo_objectanimation_state_t state_in;
    memset(&state_in, 0, sizeof(state_in));
    result = type->vtable->deserialize(&state_in, chunk, type, &des_ctx);
    ASSERT_EQ(NMO_OK, result);

    ASSERT_EQ(CKOBJANIM_FORMAT_CONTROLLERS, state_in.format);
    ASSERT_EQ(0, state_in.controller_count);
    ASSERT_EQ(0, state_in.raw_tail_size);

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, controllers_preserve_terminator_delimited_count) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    uint32_t payloads[9];
    nmo_objanim_controller_t controllers[9];
    for (uint32_t i = 0; i < 9u; ++i) {
        payloads[i] = 0xabc00000u + i;
        controllers[i].type = 0x100u + i;
        controllers[i].key_count = 0u;
        controllers[i].data_size = sizeof(payloads[i]);
        controllers[i].data = &payloads[i];
    }

    nmo_objectanimation_state_t source;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &source, NULL, NULL));
    source.format = CKOBJANIM_FORMAT_CONTROLLERS;
    source.has_length = 1;
    source.controller_count = 9u;
    source.controllers = controllers;

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    chunk->class_id = NMO_CID_OBJECTANIMATION;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_serialize(
        &source, chunk, NULL, &ser_ctx));
    nmo_chunk_close(chunk);

    nmo_objectanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_objectanimation_deserialize(
        &loaded, chunk, NULL, &des_ctx));
    ASSERT_EQ(9u, loaded.controller_count);
    ASSERT_EQ(0x108u, loaded.controllers[8].type);
    ASSERT_EQ(payloads[8], *(uint32_t *)loaded.controllers[8].data);

    ASSERT_TRUE(chunk->data.count > 0u);
    chunk->data.count--;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    nmo_objectanimation_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &failed, NULL, NULL));
    failed.flags = 0x12345678u;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_objectanimation_deserialize(
        &failed, chunk, NULL, &des_ctx));
    ASSERT_EQ(0x12345678u, failed.flags);

    nmo_objectanimation_vtable.destroy(&source, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&failed, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, controllers_reject_unaligned_payload) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NE(NULL, arena);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    uint8_t payload[3] = {1u, 2u, 3u};
    nmo_objanim_controller_t controller = {
        .type = 0x100u,
        .key_count = 0u,
        .data_size = sizeof(payload),
        .data = payload,
    };
    nmo_objectanimation_state_t source;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &source, NULL, NULL));
    source.format = CKOBJANIM_FORMAT_CONTROLLERS;
    source.controller_count = 1u;
    source.controllers = &controller;

    nmo_chunk_t *preserved = nmo_chunk_create(arena);
    ASSERT_NE(NULL, preserved);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        preserved, 0xabcdef01u));
    nmo_chunk_close(preserved);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_objectanimation_serialize(
        &source, preserved, NULL, &ser_ctx));
    ASSERT_EQ(sizeof(uint32_t), nmo_chunk_get_data_size(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(preserved));
    uint32_t marker = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(preserved, &marker));
    ASSERT_EQ(0xabcdef01u, marker);

    nmo_objectanimation_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(arena);
}

/* ========================================================================
 * Test: SHARED format -- no controllers
 * ======================================================================== */
TEST(objanim_controllers, shared_no_controllers) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_status_t result = register_test_types(registry);
    ASSERT_EQ(NMO_OK, result);

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(
        registry, CKPGUID_OBJECTANIMATION);
    ASSERT_NE(NULL, type);

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    nmo_objectanimation_state_t state_out;
    memset(&state_out, 0, sizeof(state_out));
    state_out.base.base.visibility_flags = 1;
    state_out.format = CKOBJANIM_FORMAT_SHARED;
    state_out.has_shared_anim = 1;
    state_out.shared_anim = nmo_ref_from_id(42);
    state_out.flags = 0x01;

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    nmo_chunk_start_write(chunk);
    result = type->vtable->serialize(&state_out, chunk, type, &ser_ctx);
    ASSERT_EQ(NMO_OK, result);
    nmo_chunk_close(chunk);

    nmo_chunk_start_read(chunk);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(arena, NULL, NULL, 0);
    nmo_objectanimation_state_t state_in;
    memset(&state_in, 0, sizeof(state_in));
    result = type->vtable->deserialize(&state_in, chunk, type, &des_ctx);
    ASSERT_EQ(NMO_OK, result);

    ASSERT_EQ(CKOBJANIM_FORMAT_SHARED, state_in.format);
    ASSERT_EQ(0, state_in.controller_count);
    ASSERT_EQ(0, state_in.morph_key_parsed_count);

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ========================================================================
 * Test: key size helper
 * ======================================================================== */
TEST(objanim_controllers, key_size_helper) {
    ASSERT_EQ(16, nmo_objanim_controller_key_size(CKANIMATION_LINPOS_CONTROL));
    ASSERT_EQ(20, nmo_objanim_controller_key_size(CKANIMATION_LINROT_CONTROL));
    ASSERT_EQ(16, nmo_objanim_controller_key_size(CKANIMATION_LINSCL_CONTROL));
    ASSERT_EQ(20, nmo_objanim_controller_key_size(CKANIMATION_LINSCLAXIS_CONTROL));
    ASSERT_EQ(0, nmo_objanim_controller_key_size(0));          /* unknown */
    ASSERT_EQ(0, nmo_objanim_controller_key_size(0xDEADBEEF)); /* unknown */
}

TEST(objanim_controllers, negative_morph_counts_are_rejected_atomically) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NE(NULL, arena);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, 0);

    nmo_chunk_t *legacy = nmo_chunk_create(arena);
    ASSERT_NE(NULL, legacy);
    legacy->class_id = NMO_CID_OBJECTANIMATION;
    legacy->data_version = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy, CK_STATESAVE_OBJANIMMORPHKEYS2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(legacy, -1));
    nmo_chunk_close(legacy);

    nmo_objectanimation_state_t state;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &state, NULL, NULL));
    state.format = CKOBJANIM_FORMAT_SHARED;
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_objectanimation_deserialize(
        &state, legacy, NULL, &des_ctx));
    ASSERT_EQ(CKOBJANIM_FORMAT_SHARED, state.format);

    nmo_chunk_t *modern = nmo_chunk_create(arena);
    ASSERT_NE(NULL, modern);
    modern->class_id = NMO_CID_OBJECTANIMATION;
    modern->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(modern));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        modern, CK_STATESAVE_OBJANIMNEWDATA));
    nmo_vector_t root = {0.0f, 0.0f, 0.0f};
    ASSERT_EQ(NMO_OK, nmo_chunk_write_vector3(modern, &root));
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_float(modern, 0.0f));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(modern, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(modern, -1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(modern, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        modern, NMO_OBJECT_ID_NONE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(modern, 0.0f));
    nmo_chunk_close(modern);

    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_objectanimation_deserialize(
        &state, modern, NULL, &des_ctx));
    ASSERT_EQ(CKOBJANIM_FORMAT_SHARED, state.format);

    nmo_objectanimation_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, oversized_morph_payload_is_rejected_before_allocation) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NE(NULL, arena);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, 0);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    chunk->class_id = NMO_CID_OBJECTANIMATION;
    chunk->data_version = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_OBJANIMMORPHKEYS2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk, 0.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, UINT32_MAX));
    nmo_chunk_close(chunk);

    nmo_objectanimation_state_t state;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &state, NULL, NULL));
    state.format = CKOBJANIM_FORMAT_SHARED;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_objectanimation_deserialize(
        &state, chunk, NULL, &des_ctx));
    ASSERT_EQ(CKOBJANIM_FORMAT_SHARED, state.format);

    nmo_objectanimation_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, newdata_rejects_inconsistent_controller_header) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NE(NULL, arena);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    chunk->class_id = NMO_CID_OBJECTANIMATION;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_OBJANIMNEWDATA));
    nmo_vector_t zero = {0};
    ASSERT_EQ(NMO_OK, nmo_chunk_write_vector3(chunk, &zero));
    for (size_t i = 0; i < 4u; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk, 0.0f));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        chunk, NMO_OBJECT_ID_NONE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk, 0.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 4u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0x12345678u));
    nmo_chunk_close(chunk);

    nmo_objectanimation_state_t state;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &state, NULL, NULL));
    state.format = CKOBJANIM_FORMAT_SHARED;
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_objectanimation_deserialize(
        &state, chunk, NULL, &des_ctx));
    ASSERT_EQ(CKOBJANIM_FORMAT_SHARED, state.format);

    nmo_objectanimation_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, newdata_rejects_lossy_state) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NE(NULL, arena);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_chunk_t *preserved = nmo_chunk_create(arena);
    ASSERT_NE(NULL, preserved);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        preserved, 0xabcdef01u));
    nmo_chunk_close(preserved);

    nmo_objectanimation_state_t source;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &source, NULL, NULL));
    source.format = CKOBJANIM_FORMAT_NEWDATA;
    source.morph_key_count = 1;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_objectanimation_serialize(
        &source, preserved, NULL, &ser_ctx));

    source.morph_key_count = 0;
    uint32_t payload = 0u;
    nmo_objanim_controller_t controllers[2] = {{
        .type = 0xdeadbeefu,
        .key_count = 1u,
        .data_size = sizeof(payload),
        .data = &payload,
    }};
    source.controller_count = 1u;
    source.controllers = controllers;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_objectanimation_serialize(
        &source, preserved, NULL, &ser_ctx));

    controllers[0].type = CKANIMATION_LINPOS_CONTROL;
    controllers[0].data_size = 16u;
    uint32_t controller_payload[4] = {0};
    controllers[0].data = controller_payload;
    controllers[1] = controllers[0];
    source.controller_count = 2u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_objectanimation_serialize(
        &source, preserved, NULL, &ser_ctx));

    source.controller_count = 1u;
    controllers[0].key_count = 0u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_objectanimation_serialize(
        &source, preserved, NULL, &ser_ctx));

    source.controller_count = 0u;
    source.controllers = NULL;
    source.morph_normals_id = CK_STATESAVE_OBJANIMMORPHCOMP;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_objectanimation_serialize(
        &source, preserved, NULL, &ser_ctx));

    ASSERT_EQ(sizeof(uint32_t), nmo_chunk_get_data_size(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(preserved));
    uint32_t marker = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(preserved, &marker));
    ASSERT_EQ(0xabcdef01u, marker);

    nmo_objectanimation_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, legacy_controllers_roundtrip_without_loss) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NE(NULL, arena);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    uint32_t rotation_payload = 0x12345678u;
    uint32_t axis_payload = 0x87654321u;
    nmo_objanim_controller_t controllers[2] = {
        {
            .type = CKANIMATION_LINROT_CONTROL,
            .key_count = 1u,
            .data_size = sizeof(rotation_payload),
            .data = &rotation_payload,
        },
        {
            .type = CKANIMATION_LINSCLAXIS_CONTROL,
            .key_count = 1u,
            .data_size = sizeof(axis_payload),
            .data = &axis_payload,
        },
    };
    nmo_objectanimation_state_t source;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &source, NULL, NULL));
    source.format = CKOBJANIM_FORMAT_LEGACY;
    source.controller_count = 2u;
    source.controllers = controllers;

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    chunk->class_id = NMO_CID_OBJECTANIMATION;
    chunk->data_version = 0;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_serialize(
        &source, chunk, NULL, &ser_ctx));
    nmo_chunk_close(chunk);

    nmo_objectanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_objectanimation_deserialize(
        &loaded, chunk, NULL, &des_ctx));
    ASSERT_EQ(CKOBJANIM_FORMAT_LEGACY, loaded.format);
    ASSERT_EQ(2u, loaded.controller_count);
    ASSERT_EQ(CKANIMATION_LINROT_CONTROL, loaded.controllers[0].type);
    ASSERT_EQ(rotation_payload, *(uint32_t *)loaded.controllers[0].data);
    ASSERT_EQ(CKANIMATION_LINSCLAXIS_CONTROL, loaded.controllers[1].type);
    ASSERT_EQ(axis_payload, *(uint32_t *)loaded.controllers[1].data);

    nmo_objectanimation_vtable.destroy(&source, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, legacy_inactive_merge_section_roundtrips) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NE(NULL, arena);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    chunk->class_id = NMO_CID_OBJECTANIMATION;
    chunk->data_version = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_OBJANIMMERGE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk, 0.25f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(chunk, 701));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(chunk, 702));
    nmo_chunk_close(chunk);

    nmo_objectanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_objectanimation_deserialize(
        &loaded, chunk, NULL, &des_ctx));
    ASSERT_TRUE(loaded.has_merge);
    ASSERT_EQ(0u, loaded.flags & 0x80u);
    ASSERT_EQ(0.25f, loaded.merge_factor);
    ASSERT_EQ(701u, loaded.anim1.raw_id);
    ASSERT_EQ(702u, loaded.anim2.raw_id);

    nmo_chunk_t *saved = nmo_chunk_create(arena);
    ASSERT_NE(NULL, saved);
    saved->class_id = NMO_CID_OBJECTANIMATION;
    saved->data_version = 0;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_serialize(
        &loaded, saved, NULL, &ser_ctx));
    nmo_chunk_close(saved);

    nmo_objectanimation_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_objectanimation_deserialize(
        &reloaded, saved, NULL, &des_ctx));
    ASSERT_TRUE(reloaded.has_merge);
    ASSERT_EQ(0u, reloaded.flags & 0x80u);
    ASSERT_EQ(0.25f, reloaded.merge_factor);
    ASSERT_EQ(701u, reloaded.anim1.raw_id);
    ASSERT_EQ(702u, reloaded.anim2.raw_id);

    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&reloaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, legacy_old_morphkeys_payload_roundtrips) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NE(NULL, arena);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    const uint32_t payload[] = {0x12345678u, 0x90abcdefu, 0x01020304u};

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    chunk->class_id = NMO_CID_OBJECTANIMATION;
    chunk->data_version = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_OBJANIMMORPHKEYS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_buffer_no_size(
        chunk, payload, sizeof(payload)));
    nmo_chunk_close(chunk);

    nmo_objectanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_objectanimation_deserialize(
        &loaded, chunk, NULL, &des_ctx));
    ASSERT_TRUE(loaded.has_legacy_morphkeys);
    ASSERT_EQ(sizeof(payload), loaded.legacy_morphkeys_size);
    ASSERT_EQ(0, memcmp(
        payload, loaded.legacy_morphkeys, sizeof(payload)));

    nmo_objectanimation_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &copied, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.copy(
        &loaded, &copied, NULL, arena));
    ASSERT_NE(loaded.legacy_morphkeys, copied.legacy_morphkeys);
    ASSERT_TRUE(nmo_objectanimation_vtable.equals(&loaded, &copied));
    ASSERT_EQ(nmo_objectanimation_vtable.hash(&loaded),
              nmo_objectanimation_vtable.hash(&copied));

    nmo_chunk_t *saved = nmo_chunk_create(arena);
    ASSERT_NE(NULL, saved);
    saved->class_id = NMO_CID_OBJECTANIMATION;
    saved->data_version = 0;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_serialize(
        &loaded, saved, NULL, &ser_ctx));
    nmo_chunk_close(saved);

    nmo_objectanimation_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_objectanimation_deserialize(
        &reloaded, saved, NULL, &des_ctx));
    ASSERT_TRUE(reloaded.has_legacy_morphkeys);
    ASSERT_EQ(sizeof(payload), reloaded.legacy_morphkeys_size);
    ASSERT_EQ(0, memcmp(
        payload, reloaded.legacy_morphkeys, sizeof(payload)));

    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&copied, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&reloaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, legacy_rejects_inconsistent_controller_header) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NE(NULL, arena);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    chunk->class_id = NMO_CID_OBJECTANIMATION;
    chunk->data_version = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_OBJANIMROTKEYS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, sizeof(uint32_t)));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0x12345678u));
    nmo_chunk_close(chunk);

    nmo_objectanimation_state_t state;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &state, NULL, NULL));
    state.flags = 0xabcdef01u;
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_objectanimation_deserialize(
        &state, chunk, NULL, &des_ctx));
    ASSERT_EQ(0xabcdef01u, state.flags);

    nmo_objectanimation_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, legacy_rejects_lossy_controller_state) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NE(NULL, arena);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_chunk_t *preserved = nmo_chunk_create(arena);
    ASSERT_NE(NULL, preserved);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        preserved, 0xabcdef01u));
    nmo_chunk_close(preserved);

    uint32_t payload = 0u;
    nmo_objanim_controller_t controllers[2] = {{
        .type = 0xdeadbeefu,
        .key_count = 1u,
        .data_size = sizeof(payload),
        .data = &payload,
    }};
    nmo_objectanimation_state_t source;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &source, NULL, NULL));
    source.format = CKOBJANIM_FORMAT_LEGACY;
    source.controller_count = 1u;
    source.controllers = controllers;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_objectanimation_serialize(
        &source, preserved, NULL, &ser_ctx));

    controllers[0].type = CKANIMATION_LINPOS_CONTROL;
    controllers[1] = controllers[0];
    source.controller_count = 2u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_objectanimation_serialize(
        &source, preserved, NULL, &ser_ctx));

    controllers[0].type = CKANIMATION_LINSCLAXIS_CONTROL;
    source.controller_count = 1u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_objectanimation_serialize(
        &source, preserved, NULL, &ser_ctx));

    controllers[0].type = CKANIMATION_LINPOS_CONTROL;
    controllers[0].key_count = 0u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_objectanimation_serialize(
        &source, preserved, NULL, &ser_ctx));

    source.controller_count = 0u;
    source.controllers = NULL;
    source.morph_key_count = 1;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_objectanimation_serialize(
        &source, preserved, NULL, &ser_ctx));

    ASSERT_EQ(sizeof(uint32_t), nmo_chunk_get_data_size(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(preserved));
    uint32_t marker = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(preserved, &marker));
    ASSERT_EQ(0xabcdef01u, marker);

    nmo_objectanimation_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(arena);
}

/* ========================================================================
 * Test: deep copy preserves controller data
 * ======================================================================== */
TEST(objanim_controllers, copy_controllers) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 131072);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_status_t result = register_test_types(registry);
    ASSERT_EQ(NMO_OK, result);

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(
        registry, CKPGUID_OBJECTANIMATION);
    ASSERT_NE(NULL, type);

    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    /* Build state with 1 controller */
    float pos_keys[4] = { 0.0f, 1.0f, 2.0f, 3.0f };
    nmo_objanim_controller_t ctrl;
    ctrl.type = CKANIMATION_LINPOS_CONTROL;
    ctrl.key_count = 0;
    ctrl.data_size = 16;
    ctrl.data = pos_keys;

    nmo_objectanimation_state_t state_out;
    memset(&state_out, 0, sizeof(state_out));
    state_out.base.base.visibility_flags = 1;
    state_out.format = CKOBJANIM_FORMAT_CONTROLLERS;
    state_out.has_length = 1;
    state_out.length = 1.0f;
    state_out.controller_count = 1;
    state_out.controllers = &ctrl;

    /* Serialize -> deserialize to get arena-owned state */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    nmo_chunk_start_write(chunk);
    result = type->vtable->serialize(&state_out, chunk, type, &ser_ctx);
    ASSERT_EQ(NMO_OK, result);
    nmo_chunk_close(chunk);

    nmo_chunk_start_read(chunk);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(arena, NULL, NULL, 0);
    nmo_objectanimation_state_t src;
    memset(&src, 0, sizeof(src));
    result = type->vtable->deserialize(&src, chunk, type, &des_ctx);
    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(1, src.controller_count);

    /* Copy */
    nmo_objectanimation_state_t dst;
    memset(&dst, 0, sizeof(dst));
    result = type->vtable->copy(&src, &dst, type, arena);
    ASSERT_EQ(NMO_OK, result);

    /* Verify deep copy */
    ASSERT_EQ(src.controller_count, dst.controller_count);
    ASSERT_NE(src.controllers, dst.controllers); /* different pointers */
    ASSERT_EQ(src.controllers[0].type, dst.controllers[0].type);
    ASSERT_EQ(src.controllers[0].data_size, dst.controllers[0].data_size);
    ASSERT_NE(src.controllers[0].data, dst.controllers[0].data); /* different buffers */
    ASSERT_EQ(0, memcmp(src.controllers[0].data, dst.controllers[0].data,
                        src.controllers[0].data_size));
    ASSERT_TRUE(type->vtable->equals(&src, &dst));
    ASSERT_EQ(type->vtable->hash(&src), type->vtable->hash(&dst));

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(objanim_controllers, controllers_roundtrip);
    REGISTER_TEST(objanim_controllers, controllers_empty);
    REGISTER_TEST(objanim_controllers, controllers_preserve_terminator_delimited_count);
    REGISTER_TEST(objanim_controllers, controllers_reject_unaligned_payload);
    REGISTER_TEST(objanim_controllers, shared_no_controllers);
    REGISTER_TEST(objanim_controllers, key_size_helper);
    REGISTER_TEST(objanim_controllers, negative_morph_counts_are_rejected_atomically);
    REGISTER_TEST(objanim_controllers, oversized_morph_payload_is_rejected_before_allocation);
    REGISTER_TEST(objanim_controllers, newdata_rejects_inconsistent_controller_header);
    REGISTER_TEST(objanim_controllers, newdata_rejects_lossy_state);
    REGISTER_TEST(objanim_controllers, legacy_controllers_roundtrip_without_loss);
    REGISTER_TEST(objanim_controllers, legacy_inactive_merge_section_roundtrips);
    REGISTER_TEST(objanim_controllers, legacy_old_morphkeys_payload_roundtrips);
    REGISTER_TEST(objanim_controllers, legacy_rejects_inconsistent_controller_header);
    REGISTER_TEST(objanim_controllers, legacy_rejects_lossy_controller_state);
    REGISTER_TEST(objanim_controllers, copy_controllers);
TEST_MAIN_END()
