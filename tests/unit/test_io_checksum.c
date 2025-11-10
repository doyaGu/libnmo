/**
 * @file test_io_checksum.c
 * @brief Unit tests for checksum IO operations
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(io_checksum, create_checksum_reader) {
    uint8_t buffer[256] = {0};
    nmo_io_reader *base_reader =
        nmo_io_memory_reader_create(NULL, buffer, 256);
    ASSERT_NOT_NULL(base_reader);

    nmo_io_reader *checksum_reader =
        nmo_io_checksum_reader_wrap(NULL, base_reader, NMO_CHECKSUM_CRC32);
    if (checksum_reader != NULL) {
        nmo_io_reader_release(checksum_reader);
    }
    nmo_io_reader_release(base_reader);
}

TEST(io_checksum, create_checksum_writer) {
    uint8_t buffer[256] = {0};
    nmo_io_writer *base_writer =
        nmo_io_memory_writer_create(NULL, buffer, sizeof(buffer));
    ASSERT_NOT_NULL(base_writer);

    nmo_io_writer *checksum_writer =
        nmo_io_checksum_writer_wrap(NULL, base_writer, NMO_CHECKSUM_CRC32);
    if (checksum_writer != NULL) {
        nmo_io_writer_release(checksum_writer);
    }
    nmo_io_writer_release(base_writer);
}

int main(void) {
    return 0;
}
