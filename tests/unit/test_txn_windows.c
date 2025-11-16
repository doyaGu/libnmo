/**
 * @file test_txn_windows.c
 * @brief Test program for Windows transactional file operations
 */

#include "test_framework.h"
#include "io/nmo_txn.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

#define TEST_FILE "test_transaction.dat"
#define TEST_DATA "Hello, transactional world!"

static int file_exists(const char* path) {
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

static int file_contains(const char* path, const char* expected_data) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;

    char buffer[256] = {0};
    size_t read = fread(buffer, 1, sizeof(buffer) - 1, f);
    fclose(f);

    return (read == strlen(expected_data) &&
            memcmp(buffer, expected_data, read) == 0);
}

/**
 * Test 1: Basic commit
 */
TEST(txn_windows, basic_commit) {
    // Clean up any existing test file
    DeleteFileA(TEST_FILE);

    nmo_txn_desc_t desc = {
        .path = TEST_FILE,
        .durability = NMO_TXN_NONE,
        .staging_dir = NULL
    };

    nmo_txn_handle_t* txn = nmo_txn_open(&desc);
    ASSERT_NOT_NULL(txn);

    nmo_result_t result = nmo_txn_write(txn, TEST_DATA, strlen(TEST_DATA));
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_txn_commit(txn);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_txn_close(txn);

    int exists = file_exists(TEST_FILE);
    ASSERT_TRUE(exists);

    int correct = file_contains(TEST_FILE, TEST_DATA);
    ASSERT_TRUE(correct);
}

/**
 * Test 2: Rollback (file should not exist)
 */
TEST(txn_windows, rollback) {
    const char* rollback_file = "test_rollback.dat";
    DeleteFileA(rollback_file);

    nmo_txn_desc_t desc = {
        .path = rollback_file,
        .durability = NMO_TXN_NONE,
        .staging_dir = NULL
    };

    nmo_txn_handle_t* txn = nmo_txn_open(&desc);
    ASSERT_NOT_NULL(txn);

    nmo_txn_write(txn, "This should disappear", 21);
    nmo_result_t result = nmo_txn_rollback(txn);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_txn_close(txn);

    int exists = file_exists(rollback_file);
    ASSERT_FALSE(exists);
}

/**
 * Test 3: Replace existing file
 */
TEST(txn_windows, replace_existing) {
    const char* new_data = "Updated content!";

    nmo_txn_desc_t desc = {
        .path = TEST_FILE,
        .durability = NMO_TXN_FSYNC,
        .staging_dir = NULL
    };

    nmo_txn_handle_t* txn = nmo_txn_open(&desc);
    ASSERT_NOT_NULL(txn);

    nmo_txn_write(txn, new_data, strlen(new_data));
    nmo_result_t result = nmo_txn_commit(txn);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_txn_close(txn);

    int correct = file_contains(TEST_FILE, new_data);
    ASSERT_TRUE(correct);
}

/**
 * Test 4: Multiple writes
 */
TEST(txn_windows, multiple_writes) {
    const char* multi_file = "test_multi.dat";
    DeleteFileA(multi_file);

    nmo_txn_desc_t desc = {
        .path = multi_file,
        .durability = NMO_TXN_NONE,
        .staging_dir = NULL
    };

    nmo_txn_handle_t* txn = nmo_txn_open(&desc);
    ASSERT_NOT_NULL(txn);

    nmo_result_t result1 = nmo_txn_write(txn, "Part1", 5);
    nmo_result_t result2 = nmo_txn_write(txn, "Part2", 5);
    nmo_result_t result3 = nmo_txn_write(txn, "Part3", 5);

    ASSERT_TRUE(result1.code == NMO_OK &&
                result2.code == NMO_OK &&
                result3.code == NMO_OK);

    nmo_txn_commit(txn);
    nmo_txn_close(txn);

    int correct = file_contains(multi_file, "Part1Part2Part3");
    ASSERT_TRUE(correct);

    DeleteFileA(multi_file);
}

/**
 * Test 5: Implicit rollback on close
 */
TEST(txn_windows, implicit_rollback) {
    const char* implicit_file = "test_implicit.dat";
    DeleteFileA(implicit_file);

    nmo_txn_desc_t desc = {
        .path = implicit_file,
        .durability = NMO_TXN_NONE,
        .staging_dir = NULL
    };

    nmo_txn_handle_t* txn = nmo_txn_open(&desc);
    if (txn) {
        nmo_txn_write(txn, "Should not persist", 18);
        nmo_txn_close(txn);  // Close without commit
    }

    int exists = file_exists(implicit_file);
    ASSERT_FALSE(exists);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(txn_windows, basic_commit);
    REGISTER_TEST(txn_windows, rollback);
    REGISTER_TEST(txn_windows, replace_existing);
    REGISTER_TEST(txn_windows, multiple_writes);
    REGISTER_TEST(txn_windows, implicit_rollback);
TEST_MAIN_END()

#else
int main(void) {
    fprintf(stderr, "Skipping Windows-specific txn tests on this platform.\n");
    return 0;
}
#endif
