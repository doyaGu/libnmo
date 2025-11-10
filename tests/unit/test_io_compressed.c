/**
 * @file test_io_compressed.c
 * @brief Unit tests for compressed IO operations
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(io_compressed, create_compressed_reader) {
    uint8_t buffer[256] = {0};
    nmo_io_reader *base_reader =
        nmo_io_memory_reader_create(NULL, buffer, 256);
    ASSERT_NOT_NULL(base_reader);

    nmo_io_reader *compressed_reader =
        nmo_io_compressed_reader_wrap(NULL, base_reader);
    if (compressed_reader != NULL) {
        nmo_io_reader_release(compressed_reader);
    }
    nmo_io_reader_release(base_reader);
}

TEST(io_compressed, create_compressed_writer) {
    uint8_t buffer[256] = {0};
    nmo_io_writer *base_writer =
        nmo_io_memory_writer_create(NULL, buffer, sizeof(buffer));
    ASSERT_NOT_NULL(base_writer);

    nmo_io_writer *compressed_writer =
        nmo_io_compressed_writer_wrap(NULL, base_writer);
    if (compressed_writer != NULL) {
        nmo_io_writer_release(compressed_writer);
    }
    nmo_io_writer_release(base_writer);
}

int main(void) {
    return 0;
}
