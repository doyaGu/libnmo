/**
 * @file test_io_memory.c
 * @brief Unit tests for memory IO operations
 */

#include "../test_framework.h"
#include "io/nmo_io_memory.h"
#include <string.h>

TEST(io_memory, create_and_destroy) {
    const char *data = "Hello, World!";
    size_t size = strlen(data);
    
    nmo_io_memory_t *io = nmo_io_memory_create(data, size, 1);
    ASSERT_NOT_NULL(io);
    
    nmo_io_memory_destroy(io);
}

TEST(io_memory, create_empty) {
    nmo_io_memory_t *io = nmo_io_memory_create_empty(256);
    ASSERT_NOT_NULL(io);
    
    nmo_io_memory_destroy(io);
}

TEST(io_memory, read_from_buffer) {
    const char *data = "Hello, World!";
    size_t data_size = strlen(data);
    
    nmo_io_memory_t *io = nmo_io_memory_create(data, data_size, 1);
    ASSERT_NOT_NULL(io);
    
    char buffer[20] = {0};
    size_t bytes_read = nmo_io_memory_read(io, buffer, data_size);
    ASSERT_EQ(bytes_read, data_size);
    ASSERT_STR_EQ(buffer, data);
    
    nmo_io_memory_destroy(io);
}

TEST(io_memory, write_to_buffer) {
    nmo_io_memory_t *io = nmo_io_memory_create_empty(256);
    ASSERT_NOT_NULL(io);
    
    const char *data = "Test Data";
    size_t data_size = strlen(data);
    
    size_t bytes_written = nmo_io_memory_write(io, data, data_size);
    ASSERT_EQ(bytes_written, data_size);
    
    // Verify written data
    size_t buffer_size;
    const void *buffer = nmo_io_memory_get_buffer(io, &buffer_size);
    ASSERT_NOT_NULL(buffer);
    ASSERT_EQ(buffer_size, data_size);
    
    nmo_io_memory_destroy(io);
}

TEST(io_memory, seek_and_tell) {
    const char *data = "0123456789";
    size_t data_size = strlen(data);
    
    nmo_io_memory_t *io = nmo_io_memory_create(data, data_size, 1);
    ASSERT_NOT_NULL(io);
    
    // Test tell at beginning
    int64_t pos = nmo_io_memory_tell(io);
    ASSERT_EQ(pos, 0);
    
    // Seek to middle
    int64_t new_pos = nmo_io_memory_seek(io, 5, NMO_SEEK_SET);
    ASSERT_EQ(new_pos, 5);
    
    pos = nmo_io_memory_tell(io);
    ASSERT_EQ(pos, 5);
    
    // Seek relative
    new_pos = nmo_io_memory_seek(io, 2, NMO_SEEK_CUR);
    ASSERT_EQ(new_pos, 7);
    
    // Seek from end
    new_pos = nmo_io_memory_seek(io, -3, NMO_SEEK_END);
    ASSERT_EQ(new_pos, (int64_t)(data_size - 3));
    
    nmo_io_memory_destroy(io);
}

TEST(io_memory, read_after_seek) {
    const char *data = "0123456789";
    size_t data_size = strlen(data);
    
    nmo_io_memory_t *io = nmo_io_memory_create(data, data_size, 1);
    ASSERT_NOT_NULL(io);
    
    // Seek to position 5
    nmo_io_memory_seek(io, 5, NMO_SEEK_SET);
    
    // Read from position 5
    char buffer[6] = {0};
    size_t bytes_read = nmo_io_memory_read(io, buffer, 5);
    ASSERT_EQ(bytes_read, 5);
    ASSERT_STR_EQ(buffer, "56789");
    
    nmo_io_memory_destroy(io);
}

TEST(io_memory, get_buffer) {
    const char *data = "Test Buffer";
    size_t data_size = strlen(data);
    
    nmo_io_memory_t *io = nmo_io_memory_create(data, data_size, 1);
    ASSERT_NOT_NULL(io);
    
    size_t buffer_size;
    const void *buffer = nmo_io_memory_get_buffer(io, &buffer_size);
    ASSERT_NOT_NULL(buffer);
    ASSERT_EQ(buffer_size, data_size);
    ASSERT_EQ(memcmp(buffer, data, data_size), 0);
    
    nmo_io_memory_destroy(io);
}

TEST(io_memory, no_copy_buffer) {
    const char *data = "No Copy Test";
    size_t data_size = strlen(data);
    
    // Create without copying
    nmo_io_memory_t *io = nmo_io_memory_create(data, data_size, 0);
    ASSERT_NOT_NULL(io);
    
    // Read should still work
    char buffer[20] = {0};
    size_t bytes_read = nmo_io_memory_read(io, buffer, data_size);
    ASSERT_EQ(bytes_read, data_size);
    ASSERT_STR_EQ(buffer, data);
    
    nmo_io_memory_destroy(io);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(io_memory, create_and_destroy);
    REGISTER_TEST(io_memory, create_empty);
    REGISTER_TEST(io_memory, read_from_buffer);
    REGISTER_TEST(io_memory, write_to_buffer);
    REGISTER_TEST(io_memory, seek_and_tell);
    REGISTER_TEST(io_memory, read_after_seek);
    REGISTER_TEST(io_memory, get_buffer);
    REGISTER_TEST(io_memory, no_copy_buffer);
TEST_MAIN_END()
