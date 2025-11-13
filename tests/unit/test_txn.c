/**
 * @file test_txn.c
 * @brief Tests for transactional file IO
 */

#include "test_framework.h"
#include "io/nmo_txn.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Helper: Generate unique test file path */
static void get_test_path(char *buffer, size_t size, const char *name) {
    snprintf(buffer, size, "test_txn_%s_%ld.dat", name, (long)time(NULL));
}

/* Helper: Check if file exists */
static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}

/* Helper: Read entire file */
static int read_file(const char *path, char *buffer, size_t size) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    
    size_t bytes_read = fread(buffer, 1, size, f);
    fclose(f);
    return (int)bytes_read;
}

/* Helper: Cleanup test file */
static void remove_test_file(const char *path) {
    remove(path);
}

/* Test: Open and close transaction */
TEST(txn, open_and_close) {
    char path[256];
    get_test_path(path, sizeof(path), "open");

    nmo_txn_desc_t desc = {
        .path = path,
        .durability = NMO_TXN_NONE,
        .staging_dir = NULL
    };

    nmo_txn_handle_t *txn = nmo_txn_open(&desc);
    ASSERT_NOT_NULL(txn);

    nmo_txn_close(txn);
    remove_test_file(path);
}

/* Test: Write and commit transaction */
TEST(txn, write_and_commit) {
    char path[256];
    get_test_path(path, sizeof(path), "write_commit");

    const char *data = "Transaction test data";
    const size_t data_size = strlen(data);

    /* Create and write transaction */
    nmo_txn_desc_t desc = {
        .path = path,
        .durability = NMO_TXN_NONE,
        .staging_dir = NULL
    };

    nmo_txn_handle_t *txn = nmo_txn_open(&desc);
    ASSERT_NOT_NULL(txn);

    nmo_result_t result = nmo_txn_write(txn, data, data_size);
    ASSERT_EQ(result.code, NMO_OK);

    result = nmo_txn_commit(txn);
    ASSERT_EQ(result.code, NMO_OK);

    nmo_txn_close(txn);

    /* Verify file was created and contains correct data */
    ASSERT_TRUE(file_exists(path));

    char buffer[256] = {0};
    int bytes_read = read_file(path, buffer, sizeof(buffer));
    ASSERT_EQ(bytes_read, (int)data_size);
    ASSERT_STR_EQ(buffer, data);

    remove_test_file(path);
}

/* Test: Rollback transaction */
TEST(txn, write_and_rollback) {
    char path[256];
    get_test_path(path, sizeof(path), "write_rollback");

    const char *data = "This should be rolled back";
    const size_t data_size = strlen(data);

    /* Create and write transaction */
    nmo_txn_desc_t desc = {
        .path = path,
        .durability = NMO_TXN_NONE,
        .staging_dir = NULL
    };

    nmo_txn_handle_t *txn = nmo_txn_open(&desc);
    ASSERT_NOT_NULL(txn);

    nmo_result_t result = nmo_txn_write(txn, data, data_size);
    ASSERT_EQ(result.code, NMO_OK);

    result = nmo_txn_rollback(txn);
    ASSERT_EQ(result.code, NMO_OK);

    nmo_txn_close(txn);

    /* Verify file was NOT created */
    ASSERT_FALSE(file_exists(path));

    remove_test_file(path); /* Just in case */
}

/* Test: Multiple writes before commit */
TEST(txn, multiple_writes) {
    char path[256];
    get_test_path(path, sizeof(path), "multiple_writes");

    const char *chunk1 = "Hello, ";
    const char *chunk2 = "World!";
    const size_t total_size = strlen(chunk1) + strlen(chunk2);

    nmo_txn_desc_t desc = {
        .path = path,
        .durability = NMO_TXN_NONE,
        .staging_dir = NULL
    };

    nmo_txn_handle_t *txn = nmo_txn_open(&desc);
    ASSERT_NOT_NULL(txn);

    nmo_result_t result = nmo_txn_write(txn, chunk1, strlen(chunk1));
    ASSERT_EQ(result.code, NMO_OK);

    result = nmo_txn_write(txn, chunk2, strlen(chunk2));
    ASSERT_EQ(result.code, NMO_OK);

    result = nmo_txn_commit(txn);
    ASSERT_EQ(result.code, NMO_OK);

    nmo_txn_close(txn);

    /* Verify both chunks were written */
    char buffer[256] = {0};
    int bytes_read = read_file(path, buffer, sizeof(buffer));
    ASSERT_EQ(bytes_read, (int)total_size);
    ASSERT_STR_EQ(buffer, "Hello, World!");

    remove_test_file(path);
}

/* Test: Overwrite existing file */
TEST(txn, overwrite_existing) {
    char path[256];
    get_test_path(path, sizeof(path), "overwrite");

    const char *original_data = "Original content";
    const char *new_data = "New content";

    /* Create original file */
    FILE *f = fopen(path, "wb");
    ASSERT_NOT_NULL(f);
    fwrite(original_data, 1, strlen(original_data), f);
    fclose(f);

    /* Transaction to overwrite */
    nmo_txn_desc_t desc = {
        .path = path,
        .durability = NMO_TXN_NONE,
        .staging_dir = NULL
    };

    nmo_txn_handle_t *txn = nmo_txn_open(&desc);
    ASSERT_NOT_NULL(txn);

    nmo_result_t result = nmo_txn_write(txn, new_data, strlen(new_data));
    ASSERT_EQ(result.code, NMO_OK);

    result = nmo_txn_commit(txn);
    ASSERT_EQ(result.code, NMO_OK);

    nmo_txn_close(txn);

    /* Verify file contains new data */
    char buffer[256] = {0};
    int bytes_read = read_file(path, buffer, sizeof(buffer));
    ASSERT_EQ(bytes_read, (int)strlen(new_data));
    ASSERT_STR_EQ(buffer, new_data);

    remove_test_file(path);
}

/* Test: Different durability modes */
TEST(txn, durability_modes) {
    char path1[256], path2[256], path3[256];
    get_test_path(path1, sizeof(path1), "durability_none");
    get_test_path(path2, sizeof(path2), "durability_fdatasync");
    get_test_path(path3, sizeof(path3), "durability_fsync");

    const char *data = "Durability test";
    const size_t data_size = strlen(data);

    /* Test NMO_TXN_NONE */
    {
        nmo_txn_desc_t desc = {
            .path = path1,
            .durability = NMO_TXN_NONE,
            .staging_dir = NULL
        };

        nmo_txn_handle_t *txn = nmo_txn_open(&desc);
        ASSERT_NOT_NULL(txn);
        nmo_txn_write(txn, data, data_size);
        nmo_result_t result = nmo_txn_commit(txn);
        ASSERT_EQ(result.code, NMO_OK);
        nmo_txn_close(txn);
        ASSERT_TRUE(file_exists(path1));
    }

    /* Test NMO_TXN_FDATASYNC */
    {
        nmo_txn_desc_t desc = {
            .path = path2,
            .durability = NMO_TXN_FDATASYNC,
            .staging_dir = NULL
        };

        nmo_txn_handle_t *txn = nmo_txn_open(&desc);
        ASSERT_NOT_NULL(txn);
        nmo_txn_write(txn, data, data_size);
        nmo_result_t result = nmo_txn_commit(txn);
        ASSERT_EQ(result.code, NMO_OK);
        nmo_txn_close(txn);
        ASSERT_TRUE(file_exists(path2));
    }

    /* Test NMO_TXN_FSYNC */
    {
        nmo_txn_desc_t desc = {
            .path = path3,
            .durability = NMO_TXN_FSYNC,
            .staging_dir = NULL
        };

        nmo_txn_handle_t *txn = nmo_txn_open(&desc);
        ASSERT_NOT_NULL(txn);
        nmo_txn_write(txn, data, data_size);
        nmo_result_t result = nmo_txn_commit(txn);
        ASSERT_EQ(result.code, NMO_OK);
        nmo_txn_close(txn);
        ASSERT_TRUE(file_exists(path3));
    }

    remove_test_file(path1);
    remove_test_file(path2);
    remove_test_file(path3);
}

/* Test: Empty transaction commit */
TEST(txn, empty_commit) {
    char path[256];
    get_test_path(path, sizeof(path), "empty");

    nmo_txn_desc_t desc = {
        .path = path,
        .durability = NMO_TXN_NONE,
        .staging_dir = NULL
    };

    nmo_txn_handle_t *txn = nmo_txn_open(&desc);
    ASSERT_NOT_NULL(txn);

    /* Commit without writing anything */
    nmo_result_t result = nmo_txn_commit(txn);
    ASSERT_EQ(result.code, NMO_OK);

    nmo_txn_close(txn);

    /* Verify empty file was created */
    ASSERT_TRUE(file_exists(path));

    char buffer[256] = {0};
    int bytes_read = read_file(path, buffer, sizeof(buffer));
    ASSERT_EQ(bytes_read, 0); /* Empty file */

    remove_test_file(path);
}

/* Test: Invalid parameters */
TEST(txn, invalid_parameters) {
    /* NULL descriptor */
    nmo_txn_handle_t *txn = nmo_txn_open(NULL);
    ASSERT_NULL(txn);

    /* NULL path in descriptor */
    nmo_txn_desc_t desc = {
        .path = NULL,
        .durability = NMO_TXN_NONE,
        .staging_dir = NULL
    };
    txn = nmo_txn_open(&desc);
    ASSERT_NULL(txn);
}

/* Test: Implicit rollback on close */
TEST(txn, implicit_rollback) {
    char path[256];
    get_test_path(path, sizeof(path), "implicit_rollback");

    const char *data = "Should not be committed";

    nmo_txn_desc_t desc = {
        .path = path,
        .durability = NMO_TXN_NONE,
        .staging_dir = NULL
    };

    nmo_txn_handle_t *txn = nmo_txn_open(&desc);
    ASSERT_NOT_NULL(txn);

    nmo_txn_write(txn, data, strlen(data));

    /* Close without commit or rollback - should rollback implicitly */
    nmo_txn_close(txn);

    /* Verify file was NOT created */
    ASSERT_FALSE(file_exists(path));

    remove_test_file(path); /* Just in case */
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(txn, open_and_close);
    REGISTER_TEST(txn, write_and_commit);
    REGISTER_TEST(txn, write_and_rollback);
    REGISTER_TEST(txn, multiple_writes);
    REGISTER_TEST(txn, overwrite_existing);
    REGISTER_TEST(txn, durability_modes);
    REGISTER_TEST(txn, empty_commit);
    REGISTER_TEST(txn, invalid_parameters);
    REGISTER_TEST(txn, implicit_rollback);
TEST_MAIN_END()
