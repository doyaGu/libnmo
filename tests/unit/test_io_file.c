/**
 * @file test_io_file.c
 * @brief Unit tests for file IO operations
 */

#include "../test_framework.h"
#include "nmo.h"
#include <unistd.h>

TEST(io_file, create_reader) {
    // Test with non-existent file - should handle gracefully
    nmo_io_reader *reader =
        nmo_io_file_reader_create(NULL, "/tmp/nonexistent.nmo");
    // Reader may be NULL or valid, just ensure we can clean up
    if (reader != NULL) {
        nmo_io_reader_release(reader);
    }
}

TEST(io_file, create_writer) {
    const char *tmpfile = "/tmp/test_io_file.tmp";
    nmo_io_writer *writer = nmo_io_file_writer_create(NULL, tmpfile);
    if (writer != NULL) {
        nmo_io_writer_release(writer);
        unlink(tmpfile);
    }
}

TEST(io_file, write_and_read) {
    const char *tmpfile = "/tmp/test_io_rw.tmp";
    uint8_t data[] = {0x12, 0x34, 0x56, 0x78};

    nmo_io_writer *writer = nmo_io_file_writer_create(NULL, tmpfile);
    if (writer != NULL) {
        nmo_io_write(writer, data, sizeof(data), NULL);
        nmo_io_writer_release(writer);

        nmo_io_reader *reader = nmo_io_file_reader_create(NULL, tmpfile);
        if (reader != NULL) {
            nmo_io_reader_release(reader);
        }
        unlink(tmpfile);
    }
}

int main(void) {
    return 0;
}
