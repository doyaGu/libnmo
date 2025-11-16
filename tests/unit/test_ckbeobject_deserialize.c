/**
 * @file test_ckbeobject_deserialize.c
 * @brief Tests for CKBeObject schema-based deserialization
 */

#include "../../tests/test_framework.h"
#include "schema/nmo_ckbeobject_schemas.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include <string.h>

/* Identifier constants */
#define CK_STATESAVE_SCRIPTS         0x00000003
#define CK_STATESAVE_DATAS           0x00000004
#define CK_STATESAVE_NEWATTRIBUTES   0x00000010
#define CK_DATAS_VERSION_FLAG        0x10000000

/* ATTRIBUTE_MANAGER_GUID */
static const nmo_guid_t ATTRIBUTE_MANAGER_GUID = {0x6BED328B, 0x141F5148};

/**
 * Test: Deserialize CKBeObject with no data (empty state)
 */
TEST(ckbeobject_deserialize, empty_state) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NE(NULL, arena);

    /* Create empty chunk */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    chunk->class_id = 0;
    chunk->data_version = 0;
    chunk->chunk_version = 0;
    
    nmo_result_t read_result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, read_result.code);

    /* Deserialize */
    nmo_ckbeobject_deserialize_fn deserialize = nmo_get_ckbeobject_deserialize();
    ASSERT_NE(NULL, deserialize);

    nmo_ckbeobject_state_t state;
    nmo_result_t result = deserialize(chunk, arena, &state);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(0, state.script_count);
    ASSERT_EQ(NULL, state.script_ids);
    ASSERT_EQ(0, state.priority);
    ASSERT_EQ(0, state.attribute_count);

    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/**
 * Test: Deserialize CKBeObject with scripts
 */
TEST(ckbeobject_deserialize, with_scripts) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NE(NULL, arena);

    /* Create chunk with scripts */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    chunk->class_id = 0;
    chunk->data_version = 0;
    chunk->chunk_version = 0;
    
    nmo_result_t write_result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, write_result.code);
    
    /* Write SCRIPTS identifier and data */
    write_result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_SCRIPTS);
    ASSERT_EQ(NMO_OK, write_result.code);
    write_result = nmo_chunk_write_dword(chunk, 3); /* 3 scripts */
    ASSERT_EQ(NMO_OK, write_result.code);
    write_result = nmo_chunk_write_object_id(chunk, 100);
    ASSERT_EQ(NMO_OK, write_result.code);
    write_result = nmo_chunk_write_object_id(chunk, 101);
    ASSERT_EQ(NMO_OK, write_result.code);
    write_result = nmo_chunk_write_object_id(chunk, 102);
    ASSERT_EQ(NMO_OK, write_result.code);
    
    /* Switch to read mode */
    nmo_result_t read_result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, read_result.code);

    /* Deserialize */
    nmo_ckbeobject_deserialize_fn deserialize = nmo_get_ckbeobject_deserialize();
    nmo_ckbeobject_state_t state;
    nmo_result_t result = deserialize(chunk, arena, &state);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(3, state.script_count);
    ASSERT_NE(NULL, state.script_ids);
    ASSERT_EQ(100, state.script_ids[0]);
    ASSERT_EQ(101, state.script_ids[1]);
    ASSERT_EQ(102, state.script_ids[2]);

    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/**
 * Test: Deserialize CKBeObject with priority
 */
TEST(ckbeobject_deserialize, with_priority) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NE(NULL, arena);

    /* Create chunk with priority */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    chunk->class_id = 0;
    chunk->data_version = 0;
    chunk->chunk_version = 0;
    
    nmo_result_t write_result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, write_result.code);
    
    /* Write DATAS identifier and priority */
    write_result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_DATAS);
    ASSERT_EQ(NMO_OK, write_result.code);
    write_result = nmo_chunk_write_dword(chunk, CK_DATAS_VERSION_FLAG);
    ASSERT_EQ(NMO_OK, write_result.code);
    write_result = nmo_chunk_write_int(chunk, 42); /* Priority = 42 */
    ASSERT_EQ(NMO_OK, write_result.code);
    
    nmo_result_t read_result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, read_result.code);

    /* Deserialize */
    nmo_ckbeobject_deserialize_fn deserialize = nmo_get_ckbeobject_deserialize();
    nmo_ckbeobject_state_t state;
    nmo_result_t result = deserialize(chunk, arena, &state);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(42, state.priority);

    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/**
 * Test: Serialize CKBeObject with scripts
 */
TEST(ckbeobject_serialize, with_scripts) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NE(NULL, arena);

    /* Create chunk */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    chunk->class_id = 0;
    chunk->data_version = 0;
    chunk->chunk_version = 0;
    
    nmo_result_t write_result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, write_result.code);

    /* Prepare state with scripts */
    nmo_object_id_t script_ids[] = {200, 201, 202};
    nmo_ckbeobject_state_t state = {0};
    state.script_ids = script_ids;
    state.script_count = 3;
    state.priority = 0;

    /* Serialize */
    nmo_ckbeobject_serialize_fn serialize = nmo_get_ckbeobject_serialize();
    ASSERT_NE(NULL, serialize);

    nmo_result_t result = serialize(&state, chunk, arena);
    ASSERT_EQ(NMO_OK, result.code);

    /* Verify SCRIPTS identifier was written */
    nmo_result_t read_result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, read_result.code);
    nmo_result_t seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SCRIPTS);
    ASSERT_EQ(NMO_OK, seek_result.code);

    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/**
 * Test: Round-trip with scripts and priority
 */
TEST(ckbeobject_roundtrip, scripts_and_priority) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NE(NULL, arena);

    /* Create chunk */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    chunk->class_id = 0;
    chunk->data_version = 0;
    chunk->chunk_version = 0;
    
    nmo_result_t write_result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, write_result.code);

    /* Original state */
    nmo_object_id_t script_ids[] = {10, 20, 30};
    nmo_ckbeobject_state_t original_state = {0};
    original_state.script_ids = script_ids;
    original_state.script_count = 3;
    original_state.priority = 99;

    /* Serialize */
    nmo_ckbeobject_serialize_fn serialize = nmo_get_ckbeobject_serialize();
    nmo_result_t result = serialize(&original_state, chunk, arena);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Switch to read mode */
    nmo_result_t read_result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, read_result.code);

    /* Deserialize */
    nmo_ckbeobject_deserialize_fn deserialize = nmo_get_ckbeobject_deserialize();
    nmo_ckbeobject_state_t restored_state;
    result = deserialize(chunk, arena, &restored_state);
    ASSERT_EQ(NMO_OK, result.code);

    /* Verify round-trip */
    ASSERT_EQ(original_state.script_count, restored_state.script_count);
    ASSERT_EQ(original_state.priority, restored_state.priority);
    for (uint32_t i = 0; i < original_state.script_count; i++) {
        ASSERT_EQ(original_state.script_ids[i], restored_state.script_ids[i]);
    }

    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/**
 * Test: Error handling - null chunk
 */
TEST(ckbeobject_deserialize, error_null_chunk) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NE(NULL, arena);

    nmo_ckbeobject_deserialize_fn deserialize = nmo_get_ckbeobject_deserialize();
    nmo_ckbeobject_state_t state;
    
    nmo_result_t result = deserialize(NULL, arena, &state);
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);

    nmo_arena_destroy(arena);
}

/**
 * Test: Error handling - null state
 */
TEST(ckbeobject_deserialize, error_null_state) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NE(NULL, arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    chunk->class_id = 0;
    chunk->data_version = 0;
    chunk->chunk_version = 0;

    nmo_ckbeobject_deserialize_fn deserialize = nmo_get_ckbeobject_deserialize();
    
    nmo_result_t result = deserialize(chunk, arena, NULL);
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);

    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/* Test suite main */
TEST_MAIN_BEGIN()
    REGISTER_TEST(ckbeobject_deserialize, empty_state);
    REGISTER_TEST(ckbeobject_deserialize, with_scripts);
    REGISTER_TEST(ckbeobject_deserialize, with_priority);
    REGISTER_TEST(ckbeobject_serialize, with_scripts);
    REGISTER_TEST(ckbeobject_roundtrip, scripts_and_priority);
    REGISTER_TEST(ckbeobject_deserialize, error_null_chunk);
    REGISTER_TEST(ckbeobject_deserialize, error_null_state);
TEST_MAIN_END()
