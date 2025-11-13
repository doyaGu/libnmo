/**
 * @file test_txn_utf8.c
 * @brief Test UTF-8 path support in Windows transactional file operations
 */

#include "test_framework.h"
#include "io/nmo_txn.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int file_exists_utf8(const char* utf8_path) {
    // Convert UTF-8 to UTF-16
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, NULL, 0);
    if (size <= 0) return 0;
    
    wchar_t* wpath = (wchar_t*)malloc(size * sizeof(wchar_t));
    if (!wpath) return 0;
    
    MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, wpath, size);
    DWORD attrs = GetFileAttributesW(wpath);
    free(wpath);
    
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

static void delete_file_utf8(const char* utf8_path) {
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, NULL, 0);
    if (size <= 0) return;
    
    wchar_t* wpath = (wchar_t*)malloc(size * sizeof(wchar_t));
    if (!wpath) return;
    
    MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, wpath, size);
    DeleteFileW(wpath);
    free(wpath);
}

/**
 * Test with Chinese characters (中文)
 */
TEST(txn_utf8, chinese_filename) {
    const char* test_file_chinese = "测试文件_中文.dat";
    const char* test_data = "UTF-8 content: 你好世界！";
    
    delete_file_utf8(test_file_chinese);

    nmo_txn_desc_t desc = {
        .path = test_file_chinese,
        .durability = NMO_TXN_FSYNC,
        .staging_dir = NULL
    };

    nmo_txn_handle_t* txn = nmo_txn_open(&desc);
    ASSERT_NOT_NULL(txn);

    nmo_result_t result = nmo_txn_write(txn, test_data, strlen(test_data));
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_txn_commit(txn);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_txn_close(txn);

    int exists = file_exists_utf8(test_file_chinese);
    ASSERT_TRUE(exists);
}

/**
 * Test with Japanese characters (日本語)
 */
TEST(txn_utf8, japanese_filename) {
    const char* test_file_japanese = "テスト_日本語.dat";
    
    delete_file_utf8(test_file_japanese);

    nmo_txn_desc_t desc = {
        .path = test_file_japanese,
        .durability = NMO_TXN_NONE,
        .staging_dir = NULL
    };

    nmo_txn_handle_t* txn = nmo_txn_open(&desc);
    ASSERT_NOT_NULL(txn);

    const char* jp_data = "日本語のコンテンツ";
    nmo_result_t result = nmo_txn_write(txn, jp_data, strlen(jp_data));
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_txn_commit(txn);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_txn_close(txn);

    int exists = file_exists_utf8(test_file_japanese);
    ASSERT_TRUE(exists);
}

/**
 * Test with Korean characters (한글)
 */
TEST(txn_utf8, korean_filename) {
    const char* test_file_korean = "테스트_한글.dat";
    
    delete_file_utf8(test_file_korean);

    nmo_txn_desc_t desc = {
        .path = test_file_korean,
        .durability = NMO_TXN_NONE,
        .staging_dir = NULL
    };

    nmo_txn_handle_t* txn = nmo_txn_open(&desc);
    ASSERT_NOT_NULL(txn);

    const char* kr_data = "한글 내용입니다";
    nmo_result_t result = nmo_txn_write(txn, kr_data, strlen(kr_data));
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_txn_commit(txn);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_txn_close(txn);

    int exists = file_exists_utf8(test_file_korean);
    ASSERT_TRUE(exists);
}

/**
 * Test with emoji
 */
TEST(txn_utf8, emoji_filename) {
    const char* test_file_emoji = "test_emoji_😀🎉.dat";
    
    delete_file_utf8(test_file_emoji);

    nmo_txn_desc_t desc = {
        .path = test_file_emoji,
        .durability = NMO_TXN_NONE,
        .staging_dir = NULL
    };

    nmo_txn_handle_t* txn = nmo_txn_open(&desc);
    ASSERT_NOT_NULL(txn);

    const char* emoji_data = "Content with emoji: 😀🎉🚀";
    nmo_result_t result = nmo_txn_write(txn, emoji_data, strlen(emoji_data));
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_txn_commit(txn);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_txn_close(txn);

    int exists = file_exists_utf8(test_file_emoji);
    ASSERT_TRUE(exists);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(txn_utf8, chinese_filename);
    REGISTER_TEST(txn_utf8, japanese_filename);
    REGISTER_TEST(txn_utf8, korean_filename);
    REGISTER_TEST(txn_utf8, emoji_filename);
TEST_MAIN_END()
