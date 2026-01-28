/**
 * @file test_io_mmap.c
 * @brief Unit tests for memory-mapped file IO (Phase 2.1)
 */

#include "../test_framework.h"
#include "io/nmo_io_mmap.h"
#include "io/nmo_io.h"
#include <stdio.h>
#include <string.h>

/* Test file paths */
static char test_file_path[512];
static const char *test_data = "Hello, mmap! This is test data for memory-mapped IO.";
static size_t test_data_len;

/* ============================================================================
 * Test Fixtures
 * ============================================================================ */

static int create_test_file(void) {
    snprintf(test_file_path, sizeof(test_file_path), "%s/mmap_test_file.bin", NMO_TEST_DATA_DIR);
    test_data_len = strlen(test_data);
    
    FILE *f = fopen(test_file_path, "wb");
    if (f == NULL) {
        return 0;
    }
    
    size_t written = fwrite(test_data, 1, test_data_len, f);
    fclose(f);
    
    return written == test_data_len;
}

static void cleanup_test_file(void) {
    remove(test_file_path);
}

/* ============================================================================
 * Platform Support Tests
 * ============================================================================ */

TEST(io_mmap, mmap_supported) {
    /* mmap should be supported on all major platforms */
    int supported = nmo_io_mmap_supported();
    ASSERT_TRUE(supported);
}

/* ============================================================================
 * Basic Open/Close Tests
 * ============================================================================ */

TEST(io_mmap, open_close_basic) {
    ASSERT_TRUE(create_test_file());
    
    nmo_io_mmap_t *mmap = nmo_io_mmap_open(test_file_path);
    ASSERT_NOT_NULL(mmap);
    
    /* Verify data pointer and size */
    ASSERT_NOT_NULL(nmo_io_mmap_data(mmap));
    ASSERT_EQ(test_data_len, nmo_io_mmap_size(mmap));
    
    nmo_io_mmap_close(mmap);
    cleanup_test_file();
}

TEST(io_mmap, open_null_path) {
    nmo_io_mmap_t *mmap = nmo_io_mmap_open(NULL);
    ASSERT_NULL(mmap);
}

TEST(io_mmap, open_nonexistent_file) {
    nmo_io_mmap_t *mmap = nmo_io_mmap_open("/nonexistent/path/file.bin");
    ASSERT_NULL(mmap);
}

TEST(io_mmap, close_null) {
    /* Should not crash */
    nmo_io_mmap_close(NULL);
}

/* ============================================================================
 * Data Access Tests
 * ============================================================================ */

TEST(io_mmap, data_contents) {
    ASSERT_TRUE(create_test_file());
    
    nmo_io_mmap_t *mmap = nmo_io_mmap_open(test_file_path);
    ASSERT_NOT_NULL(mmap);
    
    const void *data = nmo_io_mmap_data(mmap);
    ASSERT_NOT_NULL(data);
    ASSERT_EQ(0, memcmp(data, test_data, test_data_len));
    
    nmo_io_mmap_close(mmap);
    cleanup_test_file();
}

TEST(io_mmap, data_null_context) {
    ASSERT_NULL(nmo_io_mmap_data(NULL));
}

TEST(io_mmap, size_null_context) {
    ASSERT_EQ(0, nmo_io_mmap_size(NULL));
}

/* ============================================================================
 * Read Tests
 * ============================================================================ */

TEST(io_mmap, read_full_file) {
    ASSERT_TRUE(create_test_file());
    
    nmo_io_mmap_t *mmap = nmo_io_mmap_open(test_file_path);
    ASSERT_NOT_NULL(mmap);
    
    char buffer[256] = {0};
    size_t nread = nmo_io_mmap_read(mmap, buffer, test_data_len);
    
    ASSERT_EQ(test_data_len, nread);
    ASSERT_EQ(0, memcmp(buffer, test_data, test_data_len));
    
    nmo_io_mmap_close(mmap);
    cleanup_test_file();
}

TEST(io_mmap, read_partial) {
    ASSERT_TRUE(create_test_file());
    
    nmo_io_mmap_t *mmap = nmo_io_mmap_open(test_file_path);
    ASSERT_NOT_NULL(mmap);
    
    /* Read first 10 bytes */
    char buffer[32] = {0};
    size_t nread = nmo_io_mmap_read(mmap, buffer, 10);
    ASSERT_EQ(10, nread);
    ASSERT_EQ(0, memcmp(buffer, "Hello, mma", 10));
    
    /* Position should advance */
    ASSERT_EQ(10, nmo_io_mmap_tell(mmap));
    
    /* Read next 5 bytes */
    nread = nmo_io_mmap_read(mmap, buffer, 5);
    ASSERT_EQ(5, nread);
    ASSERT_EQ(0, memcmp(buffer, "p! Th", 5));
    
    nmo_io_mmap_close(mmap);
    cleanup_test_file();
}

TEST(io_mmap, read_past_eof) {
    ASSERT_TRUE(create_test_file());
    
    nmo_io_mmap_t *mmap = nmo_io_mmap_open(test_file_path);
    ASSERT_NOT_NULL(mmap);
    
    /* Try to read more than available */
    char buffer[256] = {0};
    size_t nread = nmo_io_mmap_read(mmap, buffer, 256);
    
    /* Should only get what's available */
    ASSERT_EQ(test_data_len, nread);
    ASSERT_EQ(0, memcmp(buffer, test_data, test_data_len));
    
    /* Position should be at end */
    ASSERT_EQ((int64_t)test_data_len, nmo_io_mmap_tell(mmap));
    
    /* Further reads should return 0 */
    nread = nmo_io_mmap_read(mmap, buffer, 10);
    ASSERT_EQ(0, nread);
    
    nmo_io_mmap_close(mmap);
    cleanup_test_file();
}

TEST(io_mmap, read_null_context) {
    char buffer[32];
    ASSERT_EQ(0, nmo_io_mmap_read(NULL, buffer, sizeof(buffer)));
}

TEST(io_mmap, read_null_buffer) {
    ASSERT_TRUE(create_test_file());
    
    nmo_io_mmap_t *mmap = nmo_io_mmap_open(test_file_path);
    ASSERT_NOT_NULL(mmap);
    
    ASSERT_EQ(0, nmo_io_mmap_read(mmap, NULL, 10));
    
    nmo_io_mmap_close(mmap);
    cleanup_test_file();
}

/* ============================================================================
 * Seek/Tell Tests
 * ============================================================================ */

TEST(io_mmap, seek_set) {
    ASSERT_TRUE(create_test_file());
    
    nmo_io_mmap_t *mmap = nmo_io_mmap_open(test_file_path);
    ASSERT_NOT_NULL(mmap);
    
    int64_t pos = nmo_io_mmap_seek(mmap, 10, SEEK_SET);
    ASSERT_EQ(10, pos);
    ASSERT_EQ(10, nmo_io_mmap_tell(mmap));
    
    /* Read from position */
    char buffer[8] = {0};
    nmo_io_mmap_read(mmap, buffer, 7);
    ASSERT_EQ(0, memcmp(buffer, "p! This", 7));
    
    nmo_io_mmap_close(mmap);
    cleanup_test_file();
}

TEST(io_mmap, seek_cur) {
    ASSERT_TRUE(create_test_file());
    
    nmo_io_mmap_t *mmap = nmo_io_mmap_open(test_file_path);
    ASSERT_NOT_NULL(mmap);
    
    /* Seek forward from start */
    nmo_io_mmap_seek(mmap, 5, SEEK_SET);
    int64_t pos = nmo_io_mmap_seek(mmap, 5, SEEK_CUR);
    ASSERT_EQ(10, pos);
    
    /* Seek backward */
    pos = nmo_io_mmap_seek(mmap, -3, SEEK_CUR);
    ASSERT_EQ(7, pos);
    
    nmo_io_mmap_close(mmap);
    cleanup_test_file();
}

TEST(io_mmap, seek_end) {
    ASSERT_TRUE(create_test_file());
    
    nmo_io_mmap_t *mmap = nmo_io_mmap_open(test_file_path);
    ASSERT_NOT_NULL(mmap);
    
    /* Seek to end */
    int64_t pos = nmo_io_mmap_seek(mmap, 0, SEEK_END);
    ASSERT_EQ((int64_t)test_data_len, pos);
    
    /* Seek 10 bytes before end */
    pos = nmo_io_mmap_seek(mmap, -10, SEEK_END);
    ASSERT_EQ((int64_t)(test_data_len - 10), pos);
    
    nmo_io_mmap_close(mmap);
    cleanup_test_file();
}

TEST(io_mmap, seek_out_of_bounds) {
    ASSERT_TRUE(create_test_file());
    
    nmo_io_mmap_t *mmap = nmo_io_mmap_open(test_file_path);
    ASSERT_NOT_NULL(mmap);
    
    /* Seek before start */
    int64_t pos = nmo_io_mmap_seek(mmap, -10, SEEK_SET);
    ASSERT_EQ(-1, pos);
    
    /* Seek past end */
    pos = nmo_io_mmap_seek(mmap, (int64_t)test_data_len + 100, SEEK_SET);
    ASSERT_EQ(-1, pos);
    
    nmo_io_mmap_close(mmap);
    cleanup_test_file();
}

TEST(io_mmap, tell_null_context) {
    ASSERT_EQ(-1, nmo_io_mmap_tell(NULL));
}

/* ============================================================================
 * Pointer Access Tests
 * ============================================================================ */

TEST(io_mmap, ptr_at_basic) {
    ASSERT_TRUE(create_test_file());
    
    nmo_io_mmap_t *mmap = nmo_io_mmap_open(test_file_path);
    ASSERT_NOT_NULL(mmap);
    
    /* Get pointer at offset 7 */
    const void *ptr = nmo_io_mmap_ptr_at(mmap, 7, 5);
    ASSERT_NOT_NULL(ptr);
    ASSERT_EQ(0, memcmp(ptr, "mmap!", 5));
    
    /* Get pointer at start */
    ptr = nmo_io_mmap_ptr_at(mmap, 0, 6);
    ASSERT_NOT_NULL(ptr);
    ASSERT_EQ(0, memcmp(ptr, "Hello,", 6));
    
    nmo_io_mmap_close(mmap);
    cleanup_test_file();
}

TEST(io_mmap, ptr_at_out_of_bounds) {
    ASSERT_TRUE(create_test_file());
    
    nmo_io_mmap_t *mmap = nmo_io_mmap_open(test_file_path);
    ASSERT_NOT_NULL(mmap);
    
    /* Request past end */
    const void *ptr = nmo_io_mmap_ptr_at(mmap, test_data_len - 5, 10);
    ASSERT_NULL(ptr);
    
    /* Request at end */
    ptr = nmo_io_mmap_ptr_at(mmap, test_data_len, 1);
    ASSERT_NULL(ptr);
    
    nmo_io_mmap_close(mmap);
    cleanup_test_file();
}

TEST(io_mmap, ptr_at_null_context) {
    ASSERT_NULL(nmo_io_mmap_ptr_at(NULL, 0, 10));
}

/* ============================================================================
 * IO Interface Wrapper Tests
 * ============================================================================ */

TEST(io_mmap, io_interface_basic) {
    ASSERT_TRUE(create_test_file());
    
    nmo_io_interface_t *io = nmo_mmap_io_open(test_file_path);
    ASSERT_NOT_NULL(io);
    
    /* Read through interface */
    char buffer[256] = {0};
    size_t nread;
    int result = nmo_io_read(io, buffer, test_data_len, &nread);
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(test_data_len, nread);
    ASSERT_EQ(0, memcmp(buffer, test_data, test_data_len));
    
    nmo_io_close(io);
    cleanup_test_file();
}

TEST(io_mmap, io_interface_seek_tell) {
    ASSERT_TRUE(create_test_file());
    
    nmo_io_interface_t *io = nmo_mmap_io_open(test_file_path);
    ASSERT_NOT_NULL(io);
    
    /* Seek and tell through interface */
    int result = nmo_io_seek(io, 10, NMO_SEEK_SET);
    ASSERT_EQ(NMO_OK, result);
    
    int64_t pos = nmo_io_tell(io);
    ASSERT_EQ(10, pos);
    
    /* Seek relative */
    result = nmo_io_seek(io, 5, NMO_SEEK_CUR);
    ASSERT_EQ(NMO_OK, result);
    
    pos = nmo_io_tell(io);
    ASSERT_EQ(15, pos);
    
    nmo_io_close(io);
    cleanup_test_file();
}

TEST(io_mmap, io_interface_write_fails) {
    ASSERT_TRUE(create_test_file());
    
    nmo_io_interface_t *io = nmo_mmap_io_open(test_file_path);
    ASSERT_NOT_NULL(io);
    
    /* Write should fail (mmap is read-only) */
    const char *data = "test";
    int result = nmo_io_write(io, data, 4);
    ASSERT_NE(NMO_OK, result);
    
    nmo_io_close(io);
    cleanup_test_file();
}

TEST(io_mmap, io_interface_null_path) {
    nmo_io_interface_t *io = nmo_mmap_io_open(NULL);
    ASSERT_NULL(io);
}

/* ============================================================================
 * Test Registration
 * ============================================================================ */

TEST_MAIN_BEGIN()
    /* Platform support */
    REGISTER_TEST(io_mmap, mmap_supported);
    
    /* Basic open/close */
    REGISTER_TEST(io_mmap, open_close_basic);
    REGISTER_TEST(io_mmap, open_null_path);
    REGISTER_TEST(io_mmap, open_nonexistent_file);
    REGISTER_TEST(io_mmap, close_null);
    
    /* Data access */
    REGISTER_TEST(io_mmap, data_contents);
    REGISTER_TEST(io_mmap, data_null_context);
    REGISTER_TEST(io_mmap, size_null_context);
    
    /* Read */
    REGISTER_TEST(io_mmap, read_full_file);
    REGISTER_TEST(io_mmap, read_partial);
    REGISTER_TEST(io_mmap, read_past_eof);
    REGISTER_TEST(io_mmap, read_null_context);
    REGISTER_TEST(io_mmap, read_null_buffer);
    
    /* Seek/tell */
    REGISTER_TEST(io_mmap, seek_set);
    REGISTER_TEST(io_mmap, seek_cur);
    REGISTER_TEST(io_mmap, seek_end);
    REGISTER_TEST(io_mmap, seek_out_of_bounds);
    REGISTER_TEST(io_mmap, tell_null_context);
    
    /* Pointer access */
    REGISTER_TEST(io_mmap, ptr_at_basic);
    REGISTER_TEST(io_mmap, ptr_at_out_of_bounds);
    REGISTER_TEST(io_mmap, ptr_at_null_context);
    
    /* IO interface wrapper */
    REGISTER_TEST(io_mmap, io_interface_basic);
    REGISTER_TEST(io_mmap, io_interface_seek_tell);
    REGISTER_TEST(io_mmap, io_interface_write_fails);
    REGISTER_TEST(io_mmap, io_interface_null_path);
TEST_MAIN_END()
