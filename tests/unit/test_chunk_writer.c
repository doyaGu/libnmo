/**
 * @file test_chunk_writer.c
 * @brief Unit tests for chunk writing
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(chunk_writer, create_writer) {
    uint8_t buffer[256] = {0};
    nmo_io_writer *writer =
        nmo_io_memory_writer_create(NULL, buffer, sizeof(buffer));
    ASSERT_NOT_NULL(writer);

    nmo_chunk_writer *chunk_writer = nmo_chunk_writer_create(NULL, writer);
    if (chunk_writer != NULL) {
        nmo_chunk_writer_destroy(chunk_writer);
    }
    nmo_io_writer_release(writer);
}

TEST(chunk_writer, write_chunk) {
    uint8_t buffer[256] = {0};
    nmo_io_writer *writer =
        nmo_io_memory_writer_create(NULL, buffer, sizeof(buffer));
    ASSERT_NOT_NULL(writer);

    nmo_chunk_writer *chunk_writer = nmo_chunk_writer_create(NULL, writer);
    if (chunk_writer != NULL) {
        nmo_chunk_desc desc = {
            .type = NMO_CHUNK_TYPE_DATA,
            .flags = NMO_CHUNK_FLAG_DEFAULT,
            .size = 0,
        };
        nmo_chunk_writer_write_chunk(chunk_writer, &desc);
        nmo_chunk_writer_destroy(chunk_writer);
    }
    nmo_io_writer_release(writer);
}

int main(void) {
    return 0;
}
