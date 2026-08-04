#include "test_framework.h"

#include "core/nmo_arena.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_context.h"
#include "format/nmo_id_remap.h"

TEST(object_ids, chunk_api_file_context_maps_ids) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(runtime_to_file);
    ASSERT_NOT_NULL(file_to_runtime);

    ASSERT_EQ(NMO_OK,
              nmo_id_remap_add(runtime_to_file, 1001, 5));
    ASSERT_EQ(NMO_OK,
              nmo_id_remap_add(runtime_to_file, 2002, 6));
    ASSERT_EQ(NMO_OK,
              nmo_id_remap_add(file_to_runtime, 5, 1001));
    ASSERT_EQ(NMO_OK,
              nmo_id_remap_add(file_to_runtime, 6, 2002));

    nmo_chunk_file_context_t file_ctx = {
        .runtime_to_file = runtime_to_file,
        .file_to_runtime = file_to_runtime
    };
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(chunk, &file_ctx);

    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(chunk, 1001));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(chunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(chunk, 2002));
    nmo_chunk_close(chunk);

    ASSERT_EQ(0u, chunk->ids.count);
    ASSERT_EQ(3u, chunk->data.count);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_EQ(5u, data[0]);
    ASSERT_EQ((uint32_t)NMO_OBJECT_ID_INVALID, data[1]);
    ASSERT_EQ(6u, data[2]);

    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    nmo_object_id_t read_id = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_object_id(chunk, &read_id));
    ASSERT_EQ(1001u, read_id);
    ASSERT_EQ(NMO_OK, nmo_chunk_read_object_id(chunk, &read_id));
    ASSERT_EQ(0u, read_id);
    ASSERT_EQ(NMO_OK, nmo_chunk_read_object_id(chunk, &read_id));
    ASSERT_EQ(2002u, read_id);

    nmo_arena_destroy(arena);
}

TEST(object_ids, chunk_api_id_array_mapping_failure_is_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(runtime_to_file);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(runtime_to_file, 1001, 5));

    nmo_chunk_file_context_t file_ctx = {
        .runtime_to_file = runtime_to_file
    };
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(chunk, &file_ctx);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0x12345678u));
    const uint32_t options_before = chunk->chunk_options;

    const nmo_object_id_t ids[] = {1001u, 9999u};
    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_chunk_write_object_id_array(chunk, ids, 2u));
    ASSERT_EQ(1u, nmo_chunk_get_position(chunk));
    ASSERT_EQ(1u, chunk->data.count);
    ASSERT_EQ(0u, chunk->ids.count);
    ASSERT_EQ(options_before, chunk->chunk_options);

    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(object_ids, chunk_api_file_context_maps_ids);
    REGISTER_TEST(object_ids, chunk_api_id_array_mapping_failure_is_atomic);
TEST_MAIN_END()
