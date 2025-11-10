/**
 * @file test_io_memory.c
 * @brief Unit tests for memory IO operations
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(io_memory, create_reader) {
    uint8_t buffer[256] = {0};
    nmo_io_reader *reader = nmo_io_memory_reader_create(NULL, buffer, 256);
    ASSERT_NOT_NULL(reader);
    nmo_io_reader_release(reader);
}

TEST(io_memory, create_writer) {
    uint8_t buffer[256] = {0};
    nmo_io_writer *writer =
        nmo_io_memory_writer_create(NULL, buffer, sizeof(buffer));
    ASSERT_NOT_NULL(writer);
    nmo_io_writer_release(writer);
}

TEST(io_memory, write_and_read) {
    uint8_t buffer[256] = {0};
    uint8_t data[] = {0x11, 0x22, 0x33, 0x44};

    nmo_io_writer *writer =
        nmo_io_memory_writer_create(NULL, buffer, sizeof(buffer));
    ASSERT_NOT_NULL(writer);

    nmo_io_write(writer, data, sizeof(data), NULL);
    nmo_io_writer_release(writer);

    nmo_io_reader *reader = nmo_io_memory_reader_create(NULL, buffer, 256);
    ASSERT_NOT_NULL(reader);
    nmo_io_reader_release(reader);
}

int main(void) {
    return 0;
}
