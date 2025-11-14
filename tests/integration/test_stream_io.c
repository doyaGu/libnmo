#include "test_framework.h"
#include "io/nmo_io_stream.h"
#include "format/nmo_object.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_header.h"
#include "core/nmo_arena.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

static void assert_result_ok(nmo_result_t result) {
    ASSERT_EQ(NMO_OK, result.code);
}

static void create_test_object(nmo_arena_t *arena,
                               nmo_object_id_t id,
                               nmo_class_id_t class_id,
                               const char *name,
                               int payload_value,
                               nmo_object_t **out_object) {
    ASSERT_NOT_NULL(out_object);

    nmo_object_t *object = nmo_object_create(arena, id, class_id);
    ASSERT_NOT_NULL(object);

    if (name != NULL) {
        int name_result = nmo_object_set_name(object, name, arena);
        ASSERT_EQ(NMO_OK, name_result);
    }

    int index_result = nmo_object_set_file_index(object, id);
    ASSERT_EQ(NMO_OK, index_result);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    assert_result_ok(nmo_chunk_start_write(chunk));
    assert_result_ok(nmo_chunk_write_int(chunk, payload_value));
    assert_result_ok(nmo_chunk_write_int(chunk, payload_value + 1));

    int chunk_result = nmo_object_set_chunk(object, chunk);
    ASSERT_EQ(NMO_OK, chunk_result);
    *out_object = object;
}

static void assert_chunk_payload(const nmo_object_t *object, int expected_base) {
    ASSERT_NOT_NULL(object);
    nmo_chunk_t *chunk = nmo_object_get_chunk(object);
    ASSERT_NOT_NULL(chunk);

    assert_result_ok(nmo_chunk_start_read(chunk));

    int32_t value = 0;
    assert_result_ok(nmo_chunk_read_int(chunk, &value));
    ASSERT_EQ(expected_base, value);

    assert_result_ok(nmo_chunk_read_int(chunk, &value));
    ASSERT_EQ(expected_base + 1, value);
}

static void run_stream_roundtrip(int compress_flag) {
    const char *path = compress_flag ? "stream_io_compressed.nmo" : "stream_io_plain.nmo";

    nmo_file_header_t header;
    memset(&header, 0, sizeof(header));
    memcpy(header.signature, "Nemo Fi\0", 8);
    header.ck_version = 0x01020304;
    header.file_version = 6;
    header.file_write_mode = compress_flag ? NMO_FILE_WRITE_COMPRESS_DATA : 0;
    header.manager_count = 0;
    header.object_count = 2;
    header.max_id_saved = 2;

    nmo_stream_writer_options_t options;
    memset(&options, 0, sizeof(options));
    options.compress_data = compress_flag;
    options.buffer_size = 32 * 1024;

    nmo_stream_writer_t *writer = nmo_stream_writer_create(path, &header, &options);
    ASSERT_NOT_NULL(writer);

    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_object_t *obj_a = NULL;
    nmo_object_t *obj_b = NULL;
    create_test_object(arena, 1, 0x10, "ObjA", 100, &obj_a);
    create_test_object(arena, 2, 0x20, "ObjB", 200, &obj_b);

    assert_result_ok(nmo_stream_writer_write_object(writer, obj_a));
    assert_result_ok(nmo_stream_writer_write_object(writer, obj_b));

    assert_result_ok(nmo_stream_writer_finalize(writer));
    nmo_stream_writer_destroy(writer);

    nmo_arena_destroy(arena);

    nmo_stream_reader_t *reader = nmo_stream_reader_create(path, NULL);
    ASSERT_NOT_NULL(reader);

    const nmo_file_header_t *parsed = nmo_stream_reader_get_header(reader);
    ASSERT_NOT_NULL(parsed);
    ASSERT_EQ(2U, parsed->object_count);

    nmo_arena_t *object_arena = nmo_arena_create(NULL, 32 * 1024);
    ASSERT_NOT_NULL(object_arena);

    nmo_object_t *loaded = NULL;
    assert_result_ok(nmo_stream_reader_read_next_object(reader, object_arena, &loaded));
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ(1U, nmo_object_get_id(loaded));
    assert_chunk_payload(loaded, 100);

    nmo_arena_reset(object_arena);
    assert_result_ok(nmo_stream_reader_read_next_object(reader, object_arena, &loaded));
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ(2U, nmo_object_get_id(loaded));
    assert_chunk_payload(loaded, 200);

    nmo_result_t eof_result = nmo_stream_reader_read_next_object(reader, object_arena, &loaded);
    ASSERT_EQ(NMO_ERR_EOF, eof_result.code);

    nmo_arena_destroy(object_arena);
    nmo_stream_reader_destroy(reader);

    remove(path);
}

TEST(stream_io, reader_writer_roundtrip) {
    run_stream_roundtrip(0);
    run_stream_roundtrip(1);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(stream_io, reader_writer_roundtrip);
TEST_MAIN_END()
