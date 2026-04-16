/**
 * @file test_chunk_truncation.c
 * @brief Tests that truncated chunk reads return NMO_ERR_TRUNCATED_CHUNK
 *        and do not crash or advance the parser position.
 */

#include "../test_framework.h"
#include <string.h>
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "core/nmo_math.h"
#include "core/nmo_color.h"

/* Helper: create a chunk with exactly `ndwords` of data, positioned at start */
static nmo_chunk_t *make_chunk(nmo_arena_t *arena, size_t ndwords) {
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    if (!chunk) return NULL;

    nmo_chunk_start_write(chunk);
    for (size_t i = 0; i < ndwords; i++) {
        nmo_chunk_write_dword(chunk, (uint32_t)i);
    }
    nmo_chunk_close(chunk);
    nmo_chunk_start_read(chunk);
    return chunk;
}

/* ---------- Tests ---------- */

TEST(chunk_truncation, read_byte_on_empty_chunk) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = make_chunk(arena, 0);
    ASSERT_NOT_NULL(chunk);

    uint8_t val = 0xFF;
    nmo_status_t result = nmo_chunk_read_byte(chunk, &val);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_int_on_empty_chunk) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = make_chunk(arena, 0);
    ASSERT_NOT_NULL(chunk);

    int32_t val = -1;
    nmo_status_t result = nmo_chunk_read_int(chunk, &val);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_float_on_empty_chunk) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = make_chunk(arena, 0);
    ASSERT_NOT_NULL(chunk);

    float val = -1.0f;
    nmo_status_t result = nmo_chunk_read_float(chunk, &val);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_guid_insufficient_data) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    /* GUID needs 2 DWORDs (d1+d2); provide only 1 */
    nmo_chunk_t *chunk = make_chunk(arena, 1);
    ASSERT_NOT_NULL(chunk);

    nmo_guid_t guid;
    memset(&guid, 0xFF, sizeof(guid));
    nmo_status_t result = nmo_chunk_read_guid(chunk, &guid);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_string_truncated_payload) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    /* Write a string header claiming 100 chars but provide no payload.
       nmo_chunk_read_string returns size_t (not nmo_status_t).
       On truncation it returns 0 and out_str is NULL. */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);
    nmo_chunk_write_dword(chunk, 100); /* declared length: 100 bytes */
    nmo_chunk_close(chunk);
    nmo_chunk_start_read(chunk);

    char *str = (char *)0xDEAD; /* sentinel */
    size_t len = nmo_chunk_read_string(chunk, &str);
    ASSERT_EQ(len, 0u);
    ASSERT_NULL(str);
    /* Position should be rolled back to 0 */
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_string_max_length_overflow_keeps_position) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);
    nmo_chunk_write_dword(chunk, UINT32_MAX);
    nmo_chunk_close(chunk);
    nmo_chunk_start_read(chunk);

    char *str = (char *)0xDEAD;
    size_t len = nmo_chunk_read_string(chunk, &str);
    ASSERT_EQ(len, 0u);
    ASSERT_NULL(str);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_buffer_truncated_payload) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    /* Write a buffer header claiming 16 bytes, but only provide 4 bytes of data */
    nmo_chunk_start_write(chunk);
    nmo_chunk_write_dword(chunk, 16);     /* declared size: 16 bytes */
    nmo_chunk_write_dword(chunk, 0xAAAA); /* only 4 bytes of actual data */
    nmo_chunk_close(chunk);
    nmo_chunk_start_read(chunk);

    void *data = NULL;
    size_t size = 0;
    nmo_status_t result = nmo_chunk_read_buffer(chunk, &data, &size);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_buffer_max_size_overflow_is_truncated) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);
    nmo_chunk_write_dword(chunk, UINT32_MAX);
    nmo_chunk_close(chunk);
    nmo_chunk_start_read(chunk);

    void *data = (void *)0xDEAD;
    size_t size = 0;
    nmo_status_t result = nmo_chunk_read_buffer(chunk, &data, &size);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_object_id_on_empty_chunk) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = make_chunk(arena, 0);
    ASSERT_NOT_NULL(chunk);

    nmo_object_id_t id = 0xFFFFFFFF;
    nmo_status_t result = nmo_chunk_read_object_id(chunk, &id);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_dword_array_insufficient) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    /* Fabricate a chunk with count=10 but only 1 data DWORD */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);
    nmo_chunk_write_dword(chunk, 10);  /* claim 10 elements */
    nmo_chunk_write_dword(chunk, 42);  /* only 1 data DWORD */
    nmo_chunk_close(chunk);
    nmo_chunk_start_read(chunk);

    uint32_t *arr = NULL;
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_dword_array(chunk, &arr, &count, arena);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_vector3_insufficient) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    /* Vector3 needs 3 DWORDs; provide only 1 */
    nmo_chunk_t *chunk = make_chunk(arena, 1);
    ASSERT_NOT_NULL(chunk);

    nmo_vector_t vec = {0};
    nmo_status_t result = nmo_chunk_read_vector3(chunk, &vec);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_matrix_insufficient) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    /* Matrix needs 16 DWORDs (4x4 floats); provide only 8 */
    nmo_chunk_t *chunk = make_chunk(arena, 8);
    ASSERT_NOT_NULL(chunk);

    nmo_matrix_t mat = {0};
    nmo_status_t result = nmo_chunk_read_matrix(chunk, &mat);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_word_on_empty_chunk) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = make_chunk(arena, 0);
    ASSERT_NOT_NULL(chunk);

    uint16_t val = 0xFFFF;
    nmo_status_t result = nmo_chunk_read_word(chunk, &val);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_dword_on_empty_chunk) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = make_chunk(arena, 0);
    ASSERT_NOT_NULL(chunk);

    uint32_t val = 0xDEADBEEF;
    nmo_status_t result = nmo_chunk_read_dword(chunk, &val);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_vector2_insufficient) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    /* Vector2 needs 2 DWORDs; provide only 1 */
    nmo_chunk_t *chunk = make_chunk(arena, 1);
    ASSERT_NOT_NULL(chunk);

    nmo_vector2_t vec = {0};
    nmo_status_t result = nmo_chunk_read_vector2(chunk, &vec);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_vector4_insufficient) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    /* Vector4 needs 4 DWORDs; provide only 2 */
    nmo_chunk_t *chunk = make_chunk(arena, 2);
    ASSERT_NOT_NULL(chunk);

    nmo_vector4_t vec = {0};
    nmo_status_t result = nmo_chunk_read_vector4(chunk, &vec);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_quaternion_insufficient) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    /* Quaternion needs 4 DWORDs; provide only 3 */
    nmo_chunk_t *chunk = make_chunk(arena, 3);
    ASSERT_NOT_NULL(chunk);

    nmo_quaternion_t quat = {0};
    nmo_status_t result = nmo_chunk_read_quaternion(chunk, &quat);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_color_insufficient) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    /* Color needs 4 DWORDs (r,g,b,a); provide only 2 */
    nmo_chunk_t *chunk = make_chunk(arena, 2);
    ASSERT_NOT_NULL(chunk);

    nmo_color_t color = {0};
    nmo_status_t result = nmo_chunk_read_color(chunk, &color);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_fill_buffer_nosize_insufficient) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    /* Provide 1 DWORD but request 8 bytes (2 DWORDs) */
    nmo_chunk_t *chunk = make_chunk(arena, 1);
    ASSERT_NOT_NULL(chunk);

    uint8_t buf[8] = {0};
    size_t filled = nmo_chunk_read_and_fill_buffer_nosize(chunk, buf, sizeof(buf));
    ASSERT_EQ(filled, 0u);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_sequence_start_on_empty_chunk) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = make_chunk(arena, 0);
    ASSERT_NOT_NULL(chunk);

    size_t count = 999;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);

    nmo_arena_destroy(arena);
}

TEST(chunk_truncation, read_sub_chunk_on_empty_chunk) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = make_chunk(arena, 0);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_t *sub = NULL;
    nmo_status_t result = nmo_chunk_read_sub_chunk(chunk, &sub);
    ASSERT_TRUE(result != NMO_OK);
    ASSERT_NULL(sub);

    nmo_arena_destroy(arena);
}

/* ---------- Main ---------- */

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_truncation, read_byte_on_empty_chunk);
    REGISTER_TEST(chunk_truncation, read_word_on_empty_chunk);
    REGISTER_TEST(chunk_truncation, read_int_on_empty_chunk);
    REGISTER_TEST(chunk_truncation, read_dword_on_empty_chunk);
    REGISTER_TEST(chunk_truncation, read_float_on_empty_chunk);
    REGISTER_TEST(chunk_truncation, read_guid_insufficient_data);
    REGISTER_TEST(chunk_truncation, read_string_truncated_payload);
    REGISTER_TEST(chunk_truncation, read_string_max_length_overflow_keeps_position);
    REGISTER_TEST(chunk_truncation, read_buffer_truncated_payload);
    REGISTER_TEST(chunk_truncation, read_buffer_max_size_overflow_is_truncated);
    REGISTER_TEST(chunk_truncation, read_object_id_on_empty_chunk);
    REGISTER_TEST(chunk_truncation, read_dword_array_insufficient);
    REGISTER_TEST(chunk_truncation, read_vector2_insufficient);
    REGISTER_TEST(chunk_truncation, read_vector3_insufficient);
    REGISTER_TEST(chunk_truncation, read_vector4_insufficient);
    REGISTER_TEST(chunk_truncation, read_quaternion_insufficient);
    REGISTER_TEST(chunk_truncation, read_matrix_insufficient);
    REGISTER_TEST(chunk_truncation, read_color_insufficient);
    REGISTER_TEST(chunk_truncation, read_fill_buffer_nosize_insufficient);
    REGISTER_TEST(chunk_truncation, read_sequence_start_on_empty_chunk);
    REGISTER_TEST(chunk_truncation, read_sub_chunk_on_empty_chunk);
TEST_MAIN_END()
