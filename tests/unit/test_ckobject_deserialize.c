/**
 * @file test_ckobject_deserialize.c
 * @brief Tests for CKObject schema-based deserialization
 */

#include "../../tests/test_framework.h"
#include "schema/nmo_ckobject_schemas.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include <string.h>

/* Identifier constants */
#define CK_STATESAVE_OBJECTHIDDEN      0x00000001
#define CK_STATESAVE_OBJECTHIERAHIDDEN 0x00000002

/**
 * Test: Deserialize visible object (default state, no identifiers)
 */
TEST(ckobject_deserialize, visible_object_default) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NE(NULL, arena);

    /* Create empty chunk (no identifiers = visible) */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    
    /* Initialize chunk for reading (even though empty) */
    nmo_result_t read_result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, read_result.code);

    /* Deserialize */
    nmo_ckobject_deserialize_fn deserialize = nmo_get_ckobject_deserialize();
    ASSERT_NE(NULL, deserialize);

    nmo_ckobject_state_t state;
    nmo_result_t result = deserialize(chunk, arena, &state);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, state.visibility_flags);

    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/**
 * Test: Deserialize completely hidden object (OBJECTHIDDEN identifier)
 */
TEST(ckobject_deserialize, hidden_object) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NE(NULL, arena);

    /* Create chunk with OBJECTHIDDEN identifier */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    
    /* Write OBJECTHIDDEN identifier */
    nmo_result_t write_result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, write_result.code);
    write_result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_OBJECTHIDDEN);
    ASSERT_EQ(NMO_OK, write_result.code);
    
    /* Prepare chunk for reading */
    nmo_result_t read_result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, read_result.code);

    /* Deserialize */
    nmo_ckobject_deserialize_fn deserialize = nmo_get_ckobject_deserialize();
    ASSERT_NE(NULL, deserialize);

    nmo_ckobject_state_t state;
    nmo_result_t result = deserialize(chunk, arena, &state);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(0, state.visibility_flags);  /* No flags set = completely hidden */

    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/**
 * Test: Deserialize hierarchically hidden object (OBJECTHIERAHIDDEN identifier)
 */
TEST(ckobject_deserialize, hierarchical_hidden_object) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NE(NULL, arena);

    /* Create chunk with OBJECTHIERAHIDDEN identifier */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    
    /* Write OBJECTHIERAHIDDEN identifier */
    nmo_result_t write_result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, write_result.code);
    write_result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_OBJECTHIERAHIDDEN);
    ASSERT_EQ(NMO_OK, write_result.code);
    
    /* Prepare chunk for reading */
    nmo_result_t read_result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, read_result.code);

    /* Deserialize */
    nmo_ckobject_deserialize_fn deserialize = nmo_get_ckobject_deserialize();
    ASSERT_NE(NULL, deserialize);

    nmo_ckobject_state_t state;
    nmo_result_t result = deserialize(chunk, arena, &state);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(NMO_CKOBJECT_HIERARCHICAL, state.visibility_flags);  /* Only HIERARCHICAL flag */

    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/**
 * Test: Serialize visible object (no identifier written)
 */
TEST(ckobject_serialize, visible_object) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NE(NULL, arena);

    /* Create chunk */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    
    /* Start write mode */
    nmo_result_t write_result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, write_result.code);

    /* Prepare visible state */
    nmo_ckobject_state_t state;
    state.visibility_flags = NMO_CKOBJECT_VISIBLE;

    /* Serialize */
    nmo_ckobject_serialize_fn serialize = nmo_get_ckobject_serialize();
    ASSERT_NE(NULL, serialize);

    nmo_result_t result = serialize(&state, chunk, arena);
    ASSERT_EQ(NMO_OK, result.code);

    /* Verify no identifiers written (chunk should be empty or minimal) */
    /* For visible objects, Virtools writes nothing */
    size_t data_size = nmo_chunk_get_data_size(chunk);
    ASSERT_EQ(0, data_size);  /* No data written for default visible state */

    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/**
 * Test: Serialize hidden object (OBJECTHIDDEN identifier)
 */
TEST(ckobject_serialize, hidden_object) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NE(NULL, arena);

    /* Create chunk */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    
    /* Start write mode */
    nmo_result_t write_result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, write_result.code);

    /* Prepare hidden state */
    nmo_ckobject_state_t state;
    state.visibility_flags = 0;  /* No flags = completely hidden */

    /* Serialize */
    nmo_ckobject_serialize_fn serialize = nmo_get_ckobject_serialize();
    ASSERT_NE(NULL, serialize);

    nmo_result_t result = serialize(&state, chunk, arena);
    ASSERT_EQ(NMO_OK, result.code);

    /* Verify OBJECTHIDDEN identifier was written */
    nmo_result_t read_result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, read_result.code);
    nmo_result_t seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJECTHIDDEN);
    ASSERT_EQ(NMO_OK, seek_result.code);

    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/**
 * Test: Serialize hierarchically hidden object (OBJECTHIERAHIDDEN identifier)
 */
TEST(ckobject_serialize, hierarchical_hidden_object) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NE(NULL, arena);

    /* Create chunk */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    
    /* Start write mode */
    nmo_result_t write_result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, write_result.code);

    /* Prepare hierarchically hidden state */
    nmo_ckobject_state_t state;
    state.visibility_flags = NMO_CKOBJECT_HIERARCHICAL;  /* Only HIERARCHICAL, not VISIBLE */

    /* Serialize */
    nmo_ckobject_serialize_fn serialize = nmo_get_ckobject_serialize();
    ASSERT_NE(NULL, serialize);

    nmo_result_t result = serialize(&state, chunk, arena);
    ASSERT_EQ(NMO_OK, result.code);

    /* Verify OBJECTHIERAHIDDEN identifier was written */
    nmo_result_t read_result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, read_result.code);
    nmo_result_t seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJECTHIERAHIDDEN);
    ASSERT_EQ(NMO_OK, seek_result.code);

    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/**
 * Test: Round-trip visible object
 */
TEST(ckobject_roundtrip, visible_object) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NE(NULL, arena);

    /* Create chunk */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    
    /* Start write mode */
    nmo_result_t write_result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, write_result.code);

    /* Original state */
    nmo_ckobject_state_t original_state;
    original_state.visibility_flags = NMO_CKOBJECT_VISIBLE;

    /* Serialize */
    nmo_ckobject_serialize_fn serialize = nmo_get_ckobject_serialize();
    nmo_result_t result = serialize(&original_state, chunk, arena);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Switch to read mode */
    nmo_result_t read_result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, read_result.code);

    /* Deserialize */
    nmo_ckobject_deserialize_fn deserialize = nmo_get_ckobject_deserialize();
    nmo_ckobject_state_t restored_state;
    result = deserialize(chunk, arena, &restored_state);
    ASSERT_EQ(NMO_OK, result.code);

    /* Verify round-trip */
    ASSERT_EQ(original_state.visibility_flags, restored_state.visibility_flags);

    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/**
 * Test: Round-trip hidden object
 */
TEST(ckobject_roundtrip, hidden_object) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NE(NULL, arena);

    /* Create chunk */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    
    /* Start write mode */
    nmo_result_t write_result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, write_result.code);

    /* Original state */
    nmo_ckobject_state_t original_state;
    original_state.visibility_flags = 0;  /* Completely hidden */

    /* Serialize */
    nmo_ckobject_serialize_fn serialize = nmo_get_ckobject_serialize();
    nmo_result_t result = serialize(&original_state, chunk, arena);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Switch to read mode */
    nmo_result_t read_result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, read_result.code);

    /* Deserialize */
    nmo_ckobject_deserialize_fn deserialize = nmo_get_ckobject_deserialize();
    nmo_ckobject_state_t restored_state;
    result = deserialize(chunk, arena, &restored_state);
    ASSERT_EQ(NMO_OK, result.code);

    /* Verify round-trip */
    ASSERT_EQ(original_state.visibility_flags, restored_state.visibility_flags);

    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/**
 * Test: Round-trip hierarchically hidden object
 */
TEST(ckobject_roundtrip, hierarchical_hidden_object) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NE(NULL, arena);

    /* Create chunk */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);
    
    /* Start write mode */
    nmo_result_t write_result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(NMO_OK, write_result.code);

    /* Original state */
    nmo_ckobject_state_t original_state;
    original_state.visibility_flags = NMO_CKOBJECT_HIERARCHICAL;

    /* Serialize */
    nmo_ckobject_serialize_fn serialize = nmo_get_ckobject_serialize();
    nmo_result_t result = serialize(&original_state, chunk, arena);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Switch to read mode */
    nmo_result_t read_result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(NMO_OK, read_result.code);

    /* Deserialize */
    nmo_ckobject_deserialize_fn deserialize = nmo_get_ckobject_deserialize();
    nmo_ckobject_state_t restored_state;
    result = deserialize(chunk, arena, &restored_state);
    ASSERT_EQ(NMO_OK, result.code);

    /* Verify round-trip */
    ASSERT_EQ(original_state.visibility_flags, restored_state.visibility_flags);

    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/**
 * Test: Error handling - null chunk
 */
TEST(ckobject_deserialize, error_null_chunk) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NE(NULL, arena);

    nmo_ckobject_deserialize_fn deserialize = nmo_get_ckobject_deserialize();
    nmo_ckobject_state_t state;
    
    nmo_result_t result = deserialize(NULL, arena, &state);
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);

    nmo_arena_destroy(arena);
}

/**
 * Test: Error handling - null output state
 */
TEST(ckobject_deserialize, error_null_state) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NE(NULL, arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NE(NULL, chunk);

    nmo_ckobject_deserialize_fn deserialize = nmo_get_ckobject_deserialize();
    
    nmo_result_t result = deserialize(chunk, arena, NULL);
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);

    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/* Test suite main */
TEST_MAIN_BEGIN()
    REGISTER_TEST(ckobject_deserialize, visible_object_default);
    REGISTER_TEST(ckobject_deserialize, hidden_object);
    REGISTER_TEST(ckobject_deserialize, hierarchical_hidden_object);
    REGISTER_TEST(ckobject_serialize, visible_object);
    REGISTER_TEST(ckobject_serialize, hidden_object);
    REGISTER_TEST(ckobject_serialize, hierarchical_hidden_object);
    REGISTER_TEST(ckobject_roundtrip, visible_object);
    REGISTER_TEST(ckobject_roundtrip, hidden_object);
    REGISTER_TEST(ckobject_roundtrip, hierarchical_hidden_object);
    REGISTER_TEST(ckobject_deserialize, error_null_chunk);
    REGISTER_TEST(ckobject_deserialize, error_null_state);
TEST_MAIN_END()
