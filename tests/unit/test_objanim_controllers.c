/**
 * @file test_objanim_controllers.c
 * @brief Unit tests for CKObjectAnimation controller parsing
 */

#include "test_framework.h"
#include "type/nmo_operations.h"
#include "object/nmo_object_types.h"
#include "type/nmo_object_guids.h"
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
#define CKANIMATION_TCBROT_CONTROL      0x45b52a02u
#define CKANIMATION_BEZIERPOS_CONTROL   0x921ab801u
#define CKANIMATION_BEZIERSCL_CONTROL   0x18ab4404u

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
    /* key_count 0: the data is an opaque blob and is written verbatim */
    controllers[0].key_count = 0;
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
 * CONTROLLERS blobs are [u32 key_count][keys]
 * ======================================================================== */

/* Serialize a CONTROLLERS-format animation holding the given controllers. */
static nmo_chunk_t *serialize_controllers(nmo_arena_t *arena,
                                          nmo_objanim_controller_t *controllers,
                                          uint32_t count,
                                          nmo_status_t *out_status)
{
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_objectanimation_state_t source;
    nmo_objectanimation_vtable.create(&source, NULL, NULL);
    source.format = CKOBJANIM_FORMAT_CONTROLLERS;
    source.has_length = 1;
    source.length = 1.0f;
    source.controller_count = count;
    source.controllers = controllers;

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    chunk->class_id = NMO_CID_OBJECTANIMATION;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    *out_status = nmo_objectanimation_serialize(&source, chunk, NULL, &ser_ctx);
    nmo_chunk_close(chunk);
    nmo_objectanimation_vtable.destroy(&source, NULL, NULL);
    return chunk;
}

static nmo_status_t load_controllers(nmo_arena_t *arena,
                                     nmo_chunk_t *chunk,
                                     nmo_objectanimation_state_t *loaded)
{
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_objectanimation_vtable.create(loaded, NULL, NULL);
    nmo_chunk_start_read(chunk);
    return nmo_objectanimation_deserialize(loaded, chunk, NULL, &des_ctx);
}

static bool chunks_have_same_payload(const nmo_chunk_t *a, const nmo_chunk_t *b)
{
    return a->data.count == b->data.count &&
           memcmp(a->data.data, b->data.data,
                  a->data.count * sizeof(uint32_t)) == 0;
}

/* Raw blob helper: [count][keys], size in bytes = 4 + keys_size. */
static void make_blob(uint32_t *blob, uint32_t count, const void *keys, size_t keys_size)
{
    blob[0] = count;
    memcpy(blob + 1, keys, keys_size);
}

TEST(objanim_controllers, controllers_read_strips_key_count_prefix) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);

    float pos_keys[8] = {0.0f, 1.0f, 2.0f, 3.0f, 5.0f, 4.0f, 5.0f, 6.0f};
    float tcb_key[10] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
    uint32_t pos_blob[9];
    uint32_t tcb_blob[11];
    make_blob(pos_blob, 2u, pos_keys, sizeof(pos_keys));
    make_blob(tcb_blob, 1u, tcb_key, sizeof(tcb_key));

    /* key_count 0 controllers are opaque blobs, written as they are. */
    nmo_objanim_controller_t raw[2] = {
        {.type = CKANIMATION_LINPOS_CONTROL, .key_count = 0u,
         .data_size = sizeof(pos_blob), .data = pos_blob},
        {.type = CKANIMATION_TCBROT_CONTROL, .key_count = 0u,
         .data_size = sizeof(tcb_blob), .data = tcb_blob},
    };
    nmo_status_t status = NMO_OK;
    nmo_chunk_t *chunk = serialize_controllers(arena, raw, 2u, &status);
    ASSERT_EQ(NMO_OK, status);

    nmo_objectanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, load_controllers(arena, chunk, &loaded));
    ASSERT_EQ(2u, loaded.controller_count);
    ASSERT_EQ(2u, loaded.controllers[0].key_count);
    ASSERT_EQ(sizeof(pos_keys), loaded.controllers[0].data_size);
    ASSERT_EQ(0, memcmp(pos_keys, loaded.controllers[0].data, sizeof(pos_keys)));
    ASSERT_EQ(1u, loaded.controllers[1].key_count);
    ASSERT_EQ(sizeof(tcb_key), loaded.controllers[1].data_size);
    ASSERT_EQ(0, memcmp(tcb_key, loaded.controllers[1].data, sizeof(tcb_key)));

    /* Writing the parsed state reproduces the blobs byte for byte. */
    nmo_chunk_t *again = serialize_controllers(
        arena, loaded.controllers, loaded.controller_count, &status);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_TRUE(chunks_have_same_payload(chunk, again));

    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, controllers_write_adds_key_count_prefix) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);

    float rot_keys[10] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    uint32_t blob[11];
    make_blob(blob, 2u, rot_keys, sizeof(rot_keys));

    nmo_objanim_controller_t keys_only = {
        .type = CKANIMATION_LINROT_CONTROL, .key_count = 2u,
        .data_size = sizeof(rot_keys), .data = rot_keys};
    nmo_objanim_controller_t opaque = {
        .type = CKANIMATION_LINROT_CONTROL, .key_count = 0u,
        .data_size = sizeof(blob), .data = blob};

    nmo_status_t status = NMO_OK;
    nmo_chunk_t *from_keys = serialize_controllers(arena, &keys_only, 1u, &status);
    ASSERT_EQ(NMO_OK, status);
    nmo_chunk_t *from_blob = serialize_controllers(arena, &opaque, 1u, &status);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_TRUE(chunks_have_same_payload(from_keys, from_blob));

    nmo_objectanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, load_controllers(arena, from_keys, &loaded));
    ASSERT_EQ(1u, loaded.controller_count);
    ASSERT_EQ(2u, loaded.controllers[0].key_count);
    ASSERT_EQ(sizeof(rot_keys), loaded.controllers[0].data_size);

    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, controllers_empty_controller_roundtrips_verbatim) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);

    /* A controller without keys is stored as a lone zero count. */
    uint32_t blob[1] = {0u};
    nmo_objanim_controller_t empty = {
        .type = CKANIMATION_LINPOS_CONTROL, .key_count = 0u,
        .data_size = sizeof(blob), .data = blob};
    nmo_status_t status = NMO_OK;
    nmo_chunk_t *chunk = serialize_controllers(arena, &empty, 1u, &status);
    ASSERT_EQ(NMO_OK, status);

    nmo_objectanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, load_controllers(arena, chunk, &loaded));
    ASSERT_EQ(1u, loaded.controller_count);
    ASSERT_EQ(0u, loaded.controllers[0].key_count);
    ASSERT_EQ(sizeof(blob), loaded.controllers[0].data_size);

    nmo_chunk_t *again = serialize_controllers(
        arena, loaded.controllers, loaded.controller_count, &status);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_TRUE(chunks_have_same_payload(chunk, again));

    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, controllers_mismatched_blobs_stay_raw) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);

    float keys[8] = {0.0f, 1.0f, 2.0f, 3.0f, 1.0f, 4.0f, 5.0f, 6.0f};
    uint32_t too_many[9];          /* claims 3 keys, holds 2 */
    uint32_t trailing[6];          /* 1 key plus a stray dword */
    uint32_t unknown_type[2] = {1u, 0xdeadbeefu};
    make_blob(too_many, 3u, keys, sizeof(keys));
    make_blob(trailing, 1u, keys, 16u);
    trailing[5] = 0x12345678u;

    nmo_objanim_controller_t raw[3] = {
        {.type = CKANIMATION_LINPOS_CONTROL, .key_count = 0u,
         .data_size = sizeof(too_many), .data = too_many},
        {.type = CKANIMATION_LINPOS_CONTROL, .key_count = 0u,
         .data_size = sizeof(trailing), .data = trailing},
        {.type = 0x100u, .key_count = 0u,
         .data_size = sizeof(unknown_type), .data = unknown_type},
    };
    nmo_status_t status = NMO_OK;
    nmo_chunk_t *chunk = serialize_controllers(arena, raw, 3u, &status);
    ASSERT_EQ(NMO_OK, status);

    nmo_objectanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, load_controllers(arena, chunk, &loaded));
    ASSERT_EQ(3u, loaded.controller_count);
    for (uint32_t i = 0; i < 3u; ++i) {
        ASSERT_EQ(0u, loaded.controllers[i].key_count);
        ASSERT_EQ(raw[i].data_size, loaded.controllers[i].data_size);
        ASSERT_EQ(0, memcmp(raw[i].data, loaded.controllers[i].data, raw[i].data_size));
    }

    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

/* Append one packed bezier key; returns the new byte length. */
static size_t append_bezier_key(uint8_t *out, size_t used, float time,
                                uint32_t flags, const float tangents[2][3])
{
    const float position[3] = {time + 1.0f, time + 2.0f, time + 3.0f};
    memcpy(out + used, &time, sizeof(float));
    memcpy(out + used + 4, position, sizeof(position));
    memcpy(out + used + 16, &flags, sizeof(flags));
    used += 20u;
    for (int t = 0; t < 2; ++t) {
        if (((flags >> (16 * t)) & 0x20u) != 0u) {
            memcpy(out + used, tangents[t], 12u);
            used += 12u;
        }
    }
    return used;
}

TEST(objanim_controllers, controllers_bezier_keys_are_packed) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);

    const float tangents[2][3] = {{0.5f, 0.25f, 0.125f}, {-1.0f, -2.0f, -3.0f}};
    uint32_t blob[1 + 25];
    uint8_t *keys = (uint8_t *)(blob + 1);
    size_t keys_size = 0u;
    keys_size = append_bezier_key(keys, keys_size, 0.0f, 0x00010001u, tangents);
    keys_size = append_bezier_key(keys, keys_size, 1.0f, 0x00000021u, tangents);
    keys_size = append_bezier_key(keys, keys_size, 2.0f, 0x00200020u, tangents);
    ASSERT_EQ(96u, keys_size);   /* 20 + 32 + 44 */
    blob[0] = 3u;

    const uint32_t types[2] = {CKANIMATION_BEZIERPOS_CONTROL, CKANIMATION_BEZIERSCL_CONTROL};
    nmo_objanim_controller_t raw[2];
    for (int i = 0; i < 2; ++i) {
        raw[i] = (nmo_objanim_controller_t){
            .type = types[i], .key_count = 0u,
            .data_size = (uint32_t)(sizeof(uint32_t) + keys_size), .data = blob};
    }
    nmo_status_t status = NMO_OK;
    nmo_chunk_t *chunk = serialize_controllers(arena, raw, 2u, &status);
    ASSERT_EQ(NMO_OK, status);

    nmo_objectanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, load_controllers(arena, chunk, &loaded));
    ASSERT_EQ(2u, loaded.controller_count);
    for (uint32_t i = 0; i < 2u; ++i) {
        ASSERT_EQ(3u, loaded.controllers[i].key_count);
        ASSERT_EQ(keys_size, loaded.controllers[i].data_size);
        ASSERT_EQ(0, memcmp(keys, loaded.controllers[i].data, keys_size));
    }

    nmo_objanim_bezier_key_t key;
    const uint8_t *cursor = loaded.controllers[0].data;
    size_t left = loaded.controllers[0].data_size;
    size_t size = nmo_objanim_bezier_key_decode(cursor, left, &key);
    ASSERT_EQ(20u, size);
    ASSERT_FALSE(key.has_tangent[0]);
    ASSERT_FALSE(key.has_tangent[1]);
    cursor += size; left -= size;
    size = nmo_objanim_bezier_key_decode(cursor, left, &key);
    ASSERT_EQ(32u, size);
    ASSERT_EQ(1.0f, key.time);
    ASSERT_EQ(2.0f, key.position[0]);
    ASSERT_TRUE(key.has_tangent[0]);
    ASSERT_FALSE(key.has_tangent[1]);
    ASSERT_EQ(0.5f, key.tangent[0][0]);
    cursor += size; left -= size;
    size = nmo_objanim_bezier_key_decode(cursor, left, &key);
    ASSERT_EQ(44u, size);
    ASSERT_TRUE(key.has_tangent[0]);
    ASSERT_TRUE(key.has_tangent[1]);
    ASSERT_EQ(-3.0f, key.tangent[1][2]);
    ASSERT_EQ(left, size);

    nmo_chunk_t *again = serialize_controllers(
        arena, loaded.controllers, loaded.controller_count, &status);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_TRUE(chunks_have_same_payload(chunk, again));

    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, controllers_truncated_bezier_blob_stays_raw) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);

    /* The second key announces a tangent that the blob does not contain. */
    uint32_t blob[1 + 10];
    uint8_t *keys = (uint8_t *)(blob + 1);
    const float tangents[2][3] = {{0}};
    size_t keys_size = append_bezier_key(keys, 0u, 0.0f, 0x00010001u, tangents);
    const float time = 1.0f;
    const uint32_t flags = 0x00000021u;
    memcpy(keys + keys_size, &time, sizeof(time));
    memset(keys + keys_size + 4, 0, 12u);
    memcpy(keys + keys_size + 16, &flags, sizeof(flags));
    keys_size += 20u;
    blob[0] = 2u;

    nmo_objanim_controller_t raw = {
        .type = CKANIMATION_BEZIERPOS_CONTROL, .key_count = 0u,
        .data_size = (uint32_t)(sizeof(uint32_t) + keys_size), .data = blob};
    nmo_status_t status = NMO_OK;
    nmo_chunk_t *chunk = serialize_controllers(arena, &raw, 1u, &status);
    ASSERT_EQ(NMO_OK, status);

    nmo_objectanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, load_controllers(arena, chunk, &loaded));
    ASSERT_EQ(1u, loaded.controller_count);
    ASSERT_EQ(0u, loaded.controllers[0].key_count);
    ASSERT_EQ(raw.data_size, loaded.controllers[0].data_size);

    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, controllers_reject_inconsistent_key_count) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NE(NULL, arena);

    float keys[8] = {0};
    nmo_objanim_controller_t wrong_size = {
        .type = CKANIMATION_LINPOS_CONTROL, .key_count = 3u,
        .data_size = sizeof(keys), .data = keys};
    nmo_objanim_controller_t unknown = {
        .type = 0x100u, .key_count = 1u,
        .data_size = 16u, .data = keys};

    nmo_status_t status = NMO_OK;
    serialize_controllers(arena, &wrong_size, 1u, &status);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, status);
    serialize_controllers(arena, &unknown, 1u, &status);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, status);

    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, keys_size_helper) {
    size_t size = 0u;
    ASSERT_TRUE(nmo_objanim_controller_keys_size(
        CKANIMATION_LINPOS_CONTROL, CKOBJANIM_FORMAT_CONTROLLERS, NULL, 48u, 3u, &size));
    ASSERT_EQ(48u, size);
    ASSERT_TRUE(nmo_objanim_controller_keys_size(
        CKANIMATION_TCBROT_CONTROL, CKOBJANIM_FORMAT_CONTROLLERS, NULL, 100u, 2u, &size));
    ASSERT_EQ(80u, size);
    ASSERT_TRUE(nmo_objanim_controller_keys_size(
        CKANIMATION_LINROT_CONTROL, CKOBJANIM_FORMAT_CONTROLLERS, NULL, 0u, 0u, &size));
    ASSERT_EQ(0u, size);
    ASSERT_FALSE(nmo_objanim_controller_keys_size(
        CKANIMATION_LINPOS_CONTROL, CKOBJANIM_FORMAT_CONTROLLERS, NULL, 47u, 3u, &size));
    ASSERT_FALSE(nmo_objanim_controller_keys_size(
        0x100u, CKOBJANIM_FORMAT_CONTROLLERS, NULL, 100u, 1u, &size));
    ASSERT_FALSE(nmo_objanim_controller_keys_size(
        CKANIMATION_LINPOS_CONTROL, CKOBJANIM_FORMAT_CONTROLLERS, NULL, 16u, UINT32_MAX, &size));

    const float tangents[2][3] = {{0}};
    uint8_t bezier[64];
    size_t used = append_bezier_key(bezier, 0u, 0.0f, 0x00000020u, tangents);
    used = append_bezier_key(bezier, used, 1.0f, 0x00000000u, tangents);
    ASSERT_EQ(52u, used);
    ASSERT_TRUE(nmo_objanim_controller_keys_size(
        CKANIMATION_BEZIERPOS_CONTROL, CKOBJANIM_FORMAT_CONTROLLERS, bezier, used, 2u, &size));
    ASSERT_EQ(52u, size);
    ASSERT_FALSE(nmo_objanim_controller_keys_size(
        CKANIMATION_BEZIERPOS_CONTROL, CKOBJANIM_FORMAT_CONTROLLERS, bezier, used - 1u, 2u, &size));
    ASSERT_FALSE(nmo_objanim_controller_keys_size(
        CKANIMATION_BEZIERPOS_CONTROL, CKOBJANIM_FORMAT_CONTROLLERS, bezier, used, 3u, &size));
    ASSERT_FALSE(nmo_objanim_controller_keys_size(
        CKANIMATION_BEZIERPOS_CONTROL, CKOBJANIM_FORMAT_CONTROLLERS, NULL, 0u, 1u, &size));

    ASSERT_TRUE(nmo_objanim_controller_is_bezier(CKANIMATION_BEZIERPOS_CONTROL));
    ASSERT_TRUE(nmo_objanim_controller_is_bezier(CKANIMATION_BEZIERSCL_CONTROL));
    ASSERT_FALSE(nmo_objanim_controller_is_bezier(CKANIMATION_LINPOS_CONTROL));
    ASSERT_EQ(0u, nmo_objanim_controller_key_size(CKANIMATION_BEZIERPOS_CONTROL));
}

/* ========================================================================
 * Test: key size helper
 * ======================================================================== */
TEST(objanim_controllers, format_key_size_helper) {
    /* NEWDATA and LEGACY store scale-axis keys as time, an unused float, quaternion. */
    ASSERT_EQ(24u, nmo_objanim_controller_format_key_size(
        CKANIMATION_LINSCLAXIS_CONTROL, CKOBJANIM_FORMAT_NEWDATA));
    ASSERT_EQ(24u, nmo_objanim_controller_format_key_size(
        CKANIMATION_LINSCLAXIS_CONTROL, CKOBJANIM_FORMAT_LEGACY));
    ASSERT_EQ(20u, nmo_objanim_controller_format_key_size(
        CKANIMATION_LINSCLAXIS_CONTROL, CKOBJANIM_FORMAT_CONTROLLERS));
    ASSERT_EQ(16u, nmo_objanim_controller_format_key_size(
        CKANIMATION_LINPOS_CONTROL, CKOBJANIM_FORMAT_NEWDATA));
    ASSERT_EQ(20u, nmo_objanim_controller_format_key_size(
        CKANIMATION_LINROT_CONTROL, CKOBJANIM_FORMAT_LEGACY));
    ASSERT_EQ(0u, nmo_objanim_controller_format_key_size(
        CKANIMATION_BEZIERPOS_CONTROL, CKOBJANIM_FORMAT_NEWDATA));

    size_t size = 0u;
    ASSERT_TRUE(nmo_objanim_controller_keys_size(
        CKANIMATION_LINSCLAXIS_CONTROL, CKOBJANIM_FORMAT_NEWDATA, NULL, 48u, 2u, &size));
    ASSERT_EQ(48u, size);
    ASSERT_FALSE(nmo_objanim_controller_keys_size(
        CKANIMATION_LINSCLAXIS_CONTROL, CKOBJANIM_FORMAT_NEWDATA, NULL, 40u, 2u, &size));
    ASSERT_TRUE(nmo_objanim_controller_keys_size(
        CKANIMATION_LINSCLAXIS_CONTROL, CKOBJANIM_FORMAT_CONTROLLERS, NULL, 40u, 2u, &size));
    ASSERT_EQ(40u, size);
}

TEST(objanim_controllers, key_size_helper) {
    ASSERT_EQ(16, nmo_objanim_controller_key_size(CKANIMATION_LINPOS_CONTROL));
    ASSERT_EQ(20, nmo_objanim_controller_key_size(CKANIMATION_LINROT_CONTROL));
    ASSERT_EQ(16, nmo_objanim_controller_key_size(CKANIMATION_LINSCL_CONTROL));
    ASSERT_EQ(20, nmo_objanim_controller_key_size(CKANIMATION_LINSCLAXIS_CONTROL));
    ASSERT_EQ(40, nmo_objanim_controller_key_size(CKANIMATION_TCBROT_CONTROL));
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

TEST(objanim_controllers, legacy_scale_axis_roundtrips_without_rotation) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NE(NULL, arena);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    uint32_t axis_payload[] = {0x12345678u, 0x90abcdefu};
    nmo_objanim_controller_t axis = {
        .type = CKANIMATION_LINSCLAXIS_CONTROL,
        .key_count = 1u,
        .data_size = sizeof(axis_payload),
        .data = axis_payload,
    };
    nmo_objectanimation_state_t source;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &source, NULL, NULL));
    source.format = CKOBJANIM_FORMAT_LEGACY;
    source.controller_count = 1u;
    source.controllers = &axis;

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
    ASSERT_EQ(1u, loaded.controller_count);
    ASSERT_EQ(CKANIMATION_LINSCLAXIS_CONTROL,
              loaded.controllers[0].type);
    ASSERT_EQ(sizeof(axis_payload), loaded.controllers[0].data_size);
    ASSERT_EQ(0, memcmp(axis_payload, loaded.controllers[0].data,
                        sizeof(axis_payload)));

    nmo_objectanimation_vtable.destroy(&source, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, version_zero_reads_0x1000_as_the_root_vector) {
    /* RCKObjectAnimation::Load tests the data version before any section: in a
     * version 0 chunk the identifier 0x1000 holds three floats, the root
     * vector, and the shared, controllers and new-data sections are not read. */
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
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_OBJANIMNEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk, 1.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk, 2.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk, 3.0f));
    nmo_chunk_close(chunk);

    nmo_objectanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_objectanimation_deserialize(
        &loaded, chunk, NULL, &des_ctx));
    ASSERT_EQ(CKOBJANIM_FORMAT_LEGACY, loaded.format);
    ASSERT_TRUE(loaded.has_root_pos);
    ASSERT_FLOAT_EQ(1.0f, loaded.root_pos.x, 0.0001f);
    ASSERT_FLOAT_EQ(2.0f, loaded.root_pos.y, 0.0001f);
    ASSERT_FLOAT_EQ(3.0f, loaded.root_pos.z, 0.0001f);

    /* What libnmo writes for version 0 reads back the same. */
    nmo_chunk_t *saved = nmo_chunk_create(arena);
    ASSERT_NE(NULL, saved);
    saved->class_id = NMO_CID_OBJECTANIMATION;
    saved->data_version = 0;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_serialize(
        &loaded, saved, NULL, &ser_ctx));
    nmo_chunk_close(saved);
    nmo_objectanimation_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_objectanimation_deserialize(
        &reloaded, saved, NULL, &des_ctx));
    ASSERT_TRUE(reloaded.has_root_pos);
    ASSERT_FLOAT_EQ(3.0f, reloaded.root_pos.z, 0.0001f);

    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&reloaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, objectanimation_enforces_format_version) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NE(NULL, arena);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_objectanimation_state_t legacy;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &legacy, NULL, NULL));
    legacy.format = CKOBJANIM_FORMAT_LEGACY;

    nmo_chunk_t *preserved = nmo_chunk_create(arena);
    ASSERT_NE(NULL, preserved);
    preserved->class_id = NMO_CID_OBJECTANIMATION;
    preserved->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        preserved, 0xabcdef01u));
    nmo_chunk_close(preserved);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_objectanimation_serialize(
        &legacy, preserved, NULL, &ser_ctx));
    ASSERT_EQ(7u, preserved->data_version);
    ASSERT_EQ(sizeof(uint32_t), nmo_chunk_get_data_size(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(preserved));
    uint32_t marker = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(preserved, &marker));
    ASSERT_EQ(0xabcdef01u, marker);

    nmo_objectanimation_state_t modern;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &modern, NULL, NULL));

    nmo_chunk_t *default_version = nmo_chunk_create(arena);
    ASSERT_NE(NULL, default_version);
    default_version->class_id = NMO_CID_OBJECTANIMATION;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_serialize(
        &modern, default_version, NULL, &ser_ctx));
    nmo_chunk_close(default_version);
    ASSERT_EQ(NMO_CHUNK_DATA_VERSION_CURRENT,
              default_version->data_version);

    nmo_objectanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_objectanimation_deserialize(
        &loaded, default_version, NULL, &des_ctx));
    ASSERT_EQ(CKOBJANIM_FORMAT_NONE, loaded.format);
    ASSERT_EQ(0u, loaded.raw_tail_size);

    nmo_objectanimation_vtable.destroy(&legacy, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&modern, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(objanim_controllers, legacy_empty_sections_roundtrip) {
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
        chunk, CK_STATESAVE_OBJANIMMORPHKEYS2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_OBJANIMPOSKEYS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_OBJANIMROTKEYS));
    for (size_t i = 0; i < 4u; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_OBJANIMSCLKEYS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_OBJANIMFLAGS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_OBJANIMENTITY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        chunk, NMO_OBJECT_ID_NONE));
    nmo_chunk_close(chunk);

    nmo_objectanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_objectanimation_deserialize(
        &loaded, chunk, NULL, &des_ctx));
    ASSERT_TRUE(loaded.has_morph_counts);
    ASSERT_TRUE(loaded.has_legacy_position_section);
    ASSERT_TRUE(loaded.has_legacy_rotation_section);
    ASSERT_TRUE(loaded.has_legacy_scale_section);
    ASSERT_TRUE(loaded.has_legacy_flags_section);
    ASSERT_TRUE(loaded.has_legacy_entity_section);
    ASSERT_EQ(0u, loaded.controller_count);

    nmo_chunk_t *saved = nmo_chunk_create(arena);
    ASSERT_NE(NULL, saved);
    saved->class_id = NMO_CID_OBJECTANIMATION;
    saved->data_version = 0;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_serialize(
        &loaded, saved, NULL, &ser_ctx));
    nmo_chunk_close(saved);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        saved, CK_STATESAVE_OBJANIMMORPHKEYS2));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        saved, CK_STATESAVE_OBJANIMPOSKEYS));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        saved, CK_STATESAVE_OBJANIMROTKEYS));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        saved, CK_STATESAVE_OBJANIMSCLKEYS));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        saved, CK_STATESAVE_OBJANIMFLAGS));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        saved, CK_STATESAVE_OBJANIMENTITY));

    nmo_objectanimation_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_objectanimation_deserialize(
        &reloaded, saved, NULL, &des_ctx));
    ASSERT_TRUE(nmo_objectanimation_vtable.equals(&loaded, &reloaded));
    ASSERT_EQ(nmo_objectanimation_vtable.hash(&loaded),
              nmo_objectanimation_vtable.hash(&reloaded));

    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
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

/* ========================================================================
 * Test: morph controller blob in the CONTROLLERS format
 * ======================================================================== */
TEST(objanim_controllers, morph_controller_blob_is_read_and_kept) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_EQ(NMO_OK, register_test_types(registry));
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(
        registry, CKPGUID_OBJECTANIMATION);
    ASSERT_NOT_NULL(type);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    /* RCKMorphController::DumpKeysTo: key count, vertex count, has normals,
     * then per key a time, 3 floats per vertex and 4 bytes per vertex of
     * compressed normal. Two keys of two vertices with normals. */
    uint32_t blob[3 + 2 * (1 + 6 + 2)];
    memset(blob, 0, sizeof(blob));
    blob[0] = 2u;
    blob[1] = 2u;
    blob[2] = 1u;
    float key_time[2] = {0.5f, 2.5f};
    for (uint32_t k = 0; k < 2u; ++k) {
        uint32_t *key = blob + 3 + k * 9u;
        memcpy(key, &key_time[k], sizeof(float));
        for (uint32_t f = 0; f < 6u; ++f) {
            float value = (float)(k * 10u + f);
            memcpy(key + 1 + f, &value, sizeof(float));
        }
        key[7] = 0x1111u * (k + 1u);
        key[8] = 0x2222u * (k + 1u);
    }

    nmo_objectanimation_state_t out_state;
    memset(&out_state, 0, sizeof(out_state));
    out_state.format = CKOBJANIM_FORMAT_CONTROLLERS;
    nmo_objanim_controller_t controller = {
        .type = NMO_OBJANIM_CONTROLLER_MORPH,
        .key_count = 0,
        .data_size = sizeof(blob),
        .data = blob,
    };
    out_state.controller_count = 1;
    out_state.controllers = &controller;

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, type->vtable->serialize(&out_state, chunk, type, &ser_ctx));
    nmo_chunk_close(chunk);
    nmo_chunk_start_read(chunk);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(arena, NULL, NULL, 0);
    nmo_objectanimation_state_t in_state;
    memset(&in_state, 0, sizeof(in_state));
    ASSERT_EQ(NMO_OK, type->vtable->deserialize(&in_state, chunk, type, &des_ctx));
    ASSERT_EQ(1u, in_state.controller_count);
    ASSERT_EQ(sizeof(blob), in_state.controllers[0].data_size);
    ASSERT_EQ(0, memcmp(blob, in_state.controllers[0].data, sizeof(blob)));

    nmo_objanim_morph_info_t info;
    ASSERT_TRUE(nmo_objanim_morph_controller_info(&in_state.controllers[0], &info));
    ASSERT_EQ(2u, info.key_count);
    ASSERT_EQ(2u, info.vertex_count);
    ASSERT_TRUE(info.has_normals);

    float time = 0.0f;
    const float *positions = NULL;
    const uint8_t *normals = NULL;
    ASSERT_TRUE(nmo_objanim_morph_controller_key(
        &in_state.controllers[0], &info, 1, &time, &positions, &normals));
    ASSERT_EQ(2.5f, time);
    ASSERT_EQ(10.0f, positions[0]);
    ASSERT_EQ(15.0f, positions[5]);
    uint32_t normal = 0;
    memcpy(&normal, normals, sizeof(normal));
    ASSERT_EQ(0x2222u, normal);
    ASSERT_FALSE(nmo_objanim_morph_controller_key(
        &in_state.controllers[0], &info, 2, &time, NULL, NULL));

    /* A size that does not match the counts is not a morph controller. */
    nmo_objanim_controller_t cut = in_state.controllers[0];
    cut.data_size -= 4u;
    ASSERT_FALSE(nmo_objanim_morph_controller_info(&cut, &info));

    /* An empty morph controller is the three header dwords. */
    uint32_t empty_blob[3] = {0u, 0u, 0u};
    nmo_objanim_controller_t empty = {
        .type = NMO_OBJANIM_CONTROLLER_MORPH,
        .data_size = sizeof(empty_blob),
        .data = empty_blob,
    };
    ASSERT_TRUE(nmo_objanim_morph_controller_info(&empty, &info));
    ASSERT_EQ(0u, info.key_count);
    ASSERT_FALSE(info.has_normals);

    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(objanim_controllers, controllers_roundtrip);
    REGISTER_TEST(objanim_controllers, controllers_empty);
    REGISTER_TEST(objanim_controllers, controllers_preserve_terminator_delimited_count);
    REGISTER_TEST(objanim_controllers, controllers_reject_unaligned_payload);
    REGISTER_TEST(objanim_controllers, shared_no_controllers);
    REGISTER_TEST(objanim_controllers, controllers_read_strips_key_count_prefix);
    REGISTER_TEST(objanim_controllers, controllers_write_adds_key_count_prefix);
    REGISTER_TEST(objanim_controllers, controllers_empty_controller_roundtrips_verbatim);
    REGISTER_TEST(objanim_controllers, controllers_mismatched_blobs_stay_raw);
    REGISTER_TEST(objanim_controllers, controllers_bezier_keys_are_packed);
    REGISTER_TEST(objanim_controllers, controllers_truncated_bezier_blob_stays_raw);
    REGISTER_TEST(objanim_controllers, controllers_reject_inconsistent_key_count);
    REGISTER_TEST(objanim_controllers, keys_size_helper);
    REGISTER_TEST(objanim_controllers, format_key_size_helper);
    REGISTER_TEST(objanim_controllers, key_size_helper);
    REGISTER_TEST(objanim_controllers, negative_morph_counts_are_rejected_atomically);
    REGISTER_TEST(objanim_controllers, oversized_morph_payload_is_rejected_before_allocation);
    REGISTER_TEST(objanim_controllers, newdata_rejects_inconsistent_controller_header);
    REGISTER_TEST(objanim_controllers, newdata_rejects_lossy_state);
    REGISTER_TEST(objanim_controllers, legacy_controllers_roundtrip_without_loss);
    REGISTER_TEST(objanim_controllers, legacy_inactive_merge_section_roundtrips);
    REGISTER_TEST(objanim_controllers, legacy_old_morphkeys_payload_roundtrips);
    REGISTER_TEST(objanim_controllers, legacy_scale_axis_roundtrips_without_rotation);
    REGISTER_TEST(objanim_controllers, objectanimation_enforces_format_version);
    REGISTER_TEST(objanim_controllers, version_zero_reads_0x1000_as_the_root_vector);
    REGISTER_TEST(objanim_controllers, legacy_empty_sections_roundtrip);
    REGISTER_TEST(objanim_controllers, legacy_rejects_inconsistent_controller_header);
    REGISTER_TEST(objanim_controllers, legacy_rejects_lossy_controller_state);
    REGISTER_TEST(objanim_controllers, copy_controllers);
    REGISTER_TEST(objanim_controllers, morph_controller_blob_is_read_and_kept);
TEST_MAIN_END()
