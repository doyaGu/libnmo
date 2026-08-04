/**
 * @file test_io_file.c
 * @brief Unit tests for the file IO interface
 */

#include "../test_framework.h"
#include "io/nmo_io_file.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

TEST(io_file, interface_round_trip) {
    const char *path = "test_file_io_round_trip.dat";
    const char *expected = "File IO interface";
    nmo_io_interface_t *io = nmo_file_io_open(
        path, NMO_IO_WRITE | NMO_IO_CREATE);
    ASSERT_NOT_NULL(io);

    ASSERT_EQ(NMO_OK, nmo_io_write(io, expected, strlen(expected)));
    ASSERT_EQ(NMO_OK, nmo_io_close(io));

    io = nmo_file_io_open(path, NMO_IO_READ);
    ASSERT_NOT_NULL(io);

    char actual[32] = {0};
    size_t bytes_read = 0;
    ASSERT_EQ(NMO_OK,
              nmo_io_read(io, actual, strlen(expected), &bytes_read));
    ASSERT_EQ(strlen(expected), bytes_read);
    ASSERT_STR_EQ(expected, actual);
    ASSERT_EQ(NMO_OK, nmo_io_close(io));

    remove(path);
}

TEST(io_file, large_offset_seek_windows_paths) {
    const char *path = "test_large_seek_windows.dat";

#if defined(_WIN32)
    nmo_io_interface_t *io = nmo_file_io_open(
        path, NMO_IO_WRITE | NMO_IO_CREATE);
    ASSERT_NOT_NULL(io);

    int64_t large_offset = (int64_t) LONG_MAX + 12345ll;
    ASSERT_EQ(NMO_OK, nmo_io_seek(io, large_offset, NMO_SEEK_SET));

    uint8_t marker = 0x5Au;
    ASSERT_EQ(NMO_OK, nmo_io_write(io, &marker, sizeof(marker)));
    ASSERT_EQ(large_offset + 1, nmo_io_tell(io));
    ASSERT_EQ(NMO_OK, nmo_io_close(io));

    io = nmo_file_io_open(path, NMO_IO_READ);
    ASSERT_NOT_NULL(io);
    ASSERT_EQ(NMO_OK, nmo_io_seek(io, large_offset, NMO_SEEK_SET));
    ASSERT_EQ(large_offset, nmo_io_tell(io));
    ASSERT_EQ(NMO_OK, nmo_io_close(io));
    remove(path);
#else
    (void) path;
    ASSERT_TRUE(1);
#endif
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(io_file, interface_round_trip);
    REGISTER_TEST(io_file, large_offset_seek_windows_paths);
TEST_MAIN_END()
