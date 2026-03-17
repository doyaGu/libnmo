/**
 * @file test_io_file.c
 * @brief Unit tests for file IO operations
 */

#include "../test_framework.h"
#include "io/nmo_io_file.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>

// Helper: Create a temporary test file
static const char *create_temp_file(const char *content) {
    static char temp_path[256];
    snprintf(temp_path, sizeof(temp_path), "test_temp_%d.dat", (int)time(NULL));
    
    FILE *f = fopen(temp_path, "wb");
    if (f) {
        if (content) {
            fwrite(content, 1, strlen(content), f);
        }
        fclose(f);
    }
    return temp_path;
}

// Helper: Remove temporary test file
static void remove_temp_file(const char *path) {
    remove(path);
}

TEST(io_file, create_for_reading) {
    const char *test_data = "Test file content";
    const char *path = create_temp_file(test_data);
    
    nmo_io_file_t *io = nmo_io_file_create(path, "rb");
    ASSERT_NOT_NULL(io);
    
    nmo_io_file_destroy(io);
    remove_temp_file(path);
}

TEST(io_file, create_for_writing) {
    const char *path = "test_write_temp.dat";
    
    nmo_io_file_t *io = nmo_io_file_create(path, "wb");
    ASSERT_NOT_NULL(io);
    
    nmo_io_file_destroy(io);
    remove_temp_file(path);
}

TEST(io_file, read_from_file) {
    const char *test_data = "Hello, File IO!";
    const char *path = create_temp_file(test_data);
    
    nmo_io_file_t *io = nmo_io_file_create(path, "rb");
    ASSERT_NOT_NULL(io);
    
    char buffer[50] = {0};
    size_t bytes_read = nmo_io_file_read(io, buffer, strlen(test_data));
    ASSERT_EQ(bytes_read, strlen(test_data));
    ASSERT_STR_EQ(buffer, test_data);
    
    nmo_io_file_destroy(io);
    remove_temp_file(path);
}

TEST(io_file, write_to_file) {
    const char *path = "test_write_content.dat";
    const char *test_data = "Written by test";
    
    nmo_io_file_t *io = nmo_io_file_create(path, "wb");
    ASSERT_NOT_NULL(io);
    
    size_t bytes_written = nmo_io_file_write(io, test_data, strlen(test_data));
    ASSERT_EQ(bytes_written, strlen(test_data));
    
    nmo_io_file_destroy(io);
    
    // Verify by reading back
    io = nmo_io_file_create(path, "rb");
    ASSERT_NOT_NULL(io);
    
    char buffer[50] = {0};
    size_t bytes_read = nmo_io_file_read(io, buffer, strlen(test_data));
    ASSERT_EQ(bytes_read, strlen(test_data));
    ASSERT_STR_EQ(buffer, test_data);
    
    nmo_io_file_destroy(io);
    remove_temp_file(path);
}

TEST(io_file, seek_and_tell) {
    const char *test_data = "0123456789";
    const char *path = create_temp_file(test_data);
    
    nmo_io_file_t *io = nmo_io_file_create(path, "rb");
    ASSERT_NOT_NULL(io);
    
    // Test tell at beginning
    int64_t pos = nmo_io_file_tell(io);
    ASSERT_EQ(pos, 0);
    
    // Seek to position 5
    int64_t new_pos = nmo_io_file_seek(io, 5, NMO_SEEK_SET);
    ASSERT_EQ(new_pos, 5);
    
    pos = nmo_io_file_tell(io);
    ASSERT_EQ(pos, 5);
    
    // Seek relative
    new_pos = nmo_io_file_seek(io, 2, NMO_SEEK_CUR);
    ASSERT_EQ(new_pos, 7);
    
    nmo_io_file_destroy(io);
    remove_temp_file(path);
}

TEST(io_file, read_after_seek) {
    const char *test_data = "ABCDEFGHIJ";
    const char *path = create_temp_file(test_data);
    
    nmo_io_file_t *io = nmo_io_file_create(path, "rb");
    ASSERT_NOT_NULL(io);
    
    // Seek to position 3
    nmo_io_file_seek(io, 3, NMO_SEEK_SET);
    
    // Read from position 3
    char buffer[8] = {0};
    size_t bytes_read = nmo_io_file_read(io, buffer, 5);
    ASSERT_EQ(bytes_read, 5);
    ASSERT_STR_EQ(buffer, "DEFGH");
    
    nmo_io_file_destroy(io);
    remove_temp_file(path);
}

TEST(io_file, close_file) {
    const char *path = create_temp_file("test");
    
    nmo_io_file_t *io = nmo_io_file_create(path, "rb");
    ASSERT_NOT_NULL(io);
    
    nmo_status_t result = nmo_io_file_close(io);
    ASSERT_TRUE(result == NMO_OK);
    
    nmo_io_file_destroy(io);
    remove_temp_file(path);
}

TEST(io_file, create_nonexistent_file_read) {
    nmo_io_file_t *io = nmo_io_file_create("nonexistent_file_12345.dat", "rb");
    // Should fail or return NULL
    if (io != NULL) {
        nmo_io_file_destroy(io);
    }
}

TEST(io_file, large_offset_seek_windows_paths) {
    const char *path = "test_large_seek_windows.dat";

#if defined(_WIN32)
    nmo_io_interface_t *io = nmo_file_io_open(path, NMO_IO_WRITE | NMO_IO_CREATE);
    ASSERT_NOT_NULL(io);

    int64_t large_offset = (int64_t) LONG_MAX + 12345ll;
    int result = nmo_io_seek(io, large_offset, NMO_SEEK_SET);
    ASSERT_EQ(result, NMO_OK);

    uint8_t marker = 0x5Au;
    result = nmo_io_write(io, &marker, sizeof(marker));
    ASSERT_EQ(result, NMO_OK);

    int64_t pos = nmo_io_tell(io);
    ASSERT_EQ(pos, large_offset + 1);

    result = nmo_io_close(io);
    ASSERT_EQ(result, NMO_OK);

    nmo_io_file_t *legacy = nmo_io_file_create(path, "rb");
    ASSERT_NOT_NULL(legacy);

    int64_t legacy_pos = nmo_io_file_seek(legacy, large_offset, NMO_SEEK_SET);
    ASSERT_EQ(legacy_pos, large_offset);
    ASSERT_EQ(nmo_io_file_tell(legacy), large_offset);

    nmo_io_file_destroy(legacy);
    remove_temp_file(path);
#else
    (void) path;
    ASSERT_TRUE(1);
#endif
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(io_file, create_for_reading);
    REGISTER_TEST(io_file, create_for_writing);
    REGISTER_TEST(io_file, read_from_file);
    REGISTER_TEST(io_file, write_to_file);
    REGISTER_TEST(io_file, seek_and_tell);
    REGISTER_TEST(io_file, read_after_seek);
    REGISTER_TEST(io_file, close_file);
    REGISTER_TEST(io_file, create_nonexistent_file_read);
    REGISTER_TEST(io_file, large_offset_seek_windows_paths);
TEST_MAIN_END()
