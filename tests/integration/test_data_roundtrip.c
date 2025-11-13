/**
 * @file test_data_roundtrip.c
 * @brief Test data section serialization round-trip
 * 
 * NOTE: This test currently focuses on testing GUID and structure round-trip
 * without chunk data, since nmo_chunk_serialize() produces a different format
 * than nmo_chunk_parse() expects (new format vs VERSION1 format).
 * 
 * TODO: Implement proper VERSION1 chunk serialization or update parse to use
 * the new format (P0 task).
 */

#include "../test_framework.h"
#include <string.h>
#include "format/nmo_data.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"

/* Test registration */
static void test_empty_data_section(void);
static void test_manager_guid_roundtrip(void);
static void test_object_metadata_roundtrip(void);
static void test_mixed_data_roundtrip(void);
static void test_manager_with_chunk_data(void);

static void register_tests(void) {
    test_register("data_roundtrip", "empty_data_section", test_empty_data_section);
    test_register("data_roundtrip", "manager_guid_roundtrip", test_manager_guid_roundtrip);
    test_register("data_roundtrip", "object_metadata_roundtrip", test_object_metadata_roundtrip);
    test_register("data_roundtrip", "mixed_data_roundtrip", test_mixed_data_roundtrip);
    test_register("data_roundtrip", "manager_with_chunk_data", test_manager_with_chunk_data);
}

static void test_empty_data_section(void) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    nmo_data_section_t data = {0};
    data.manager_count = 0;
    data.object_count = 0;
    
    /* Calculate size */
    size_t size = nmo_data_section_calculate_size(&data, 8, arena);
    ASSERT_EQ(size, 0);
    
    /* Serialize */
    uint8_t buffer[100];
    size_t bytes_written = 0;
    nmo_result_t result = nmo_data_section_serialize(&data, 8, buffer, sizeof(buffer), &bytes_written, arena);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(bytes_written, 0);
    
    /* Parse back */
    nmo_data_section_t parsed = {0};
    result = nmo_data_section_parse(buffer, bytes_written, 8, &parsed, arena);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(parsed.manager_count, 0);
    ASSERT_EQ(parsed.object_count, 0);
    
    nmo_arena_destroy(arena);
}

static void test_manager_guid_roundtrip(void) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    /* Create data section with one manager */
    nmo_data_section_t data = {0};
    data.manager_count = 1;
    data.managers = (nmo_manager_data_t*)nmo_arena_alloc(arena, sizeof(nmo_manager_data_t), 8);
    ASSERT_NOT_NULL(data.managers);
    memset(data.managers, 0, sizeof(nmo_manager_data_t));
    
    /* Setup manager GUID */
    data.managers[0].guid.d1 = 0x12345678;
    data.managers[0].guid.d2 = 0x9ABCDEF0;
    data.managers[0].data_size = 0;  /* No chunk data */
    data.managers[0].chunk = NULL;
    
    /* Calculate size: GUID (8) + size (4) = 12 bytes */
    size_t expected_size = 12;
    size_t calculated_size = nmo_data_section_calculate_size(&data, 8, arena);
    ASSERT_EQ(calculated_size, expected_size);
    
    /* Serialize */
    uint8_t* buffer = (uint8_t*)nmo_arena_alloc(arena, calculated_size + 100, 16);
    ASSERT_NOT_NULL(buffer);
    size_t bytes_written = 0;
    nmo_result_t result = nmo_data_section_serialize(&data, 8, buffer, calculated_size + 100, &bytes_written, arena);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(bytes_written, expected_size);
    
    /* Parse it back */
    nmo_data_section_t parsed = {0};
    parsed.manager_count = 1;
    result = nmo_data_section_parse(buffer, bytes_written, 8, &parsed, arena);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(parsed.manager_count, 1);
    ASSERT_NOT_NULL(parsed.managers);
    
    /* Verify GUID */
    ASSERT_EQ(parsed.managers[0].guid.d1, 0x12345678);
    ASSERT_EQ(parsed.managers[0].guid.d2, 0x9ABCDEF0);
    ASSERT_EQ(parsed.managers[0].data_size, 0);
    ASSERT_NULL(parsed.managers[0].chunk);
    
    nmo_arena_destroy(arena);
}

static void test_object_metadata_roundtrip(void) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    /* Create data section with two objects */
    nmo_data_section_t data = {0};
    data.object_count = 2;
    data.objects = (nmo_object_data_t*)nmo_arena_alloc(arena, sizeof(nmo_object_data_t) * 2, 8);
    ASSERT_NOT_NULL(data.objects);
    memset(data.objects, 0, sizeof(nmo_object_data_t) * 2);
    
    /* No chunk data for now */
    data.objects[0].data_size = 0;
    data.objects[0].chunk = NULL;
    data.objects[1].data_size = 0;
    data.objects[1].chunk = NULL;
    
    /* Calculate size: 2 * size (4) = 8 bytes */
    size_t expected_size = 8;
    size_t calculated_size = nmo_data_section_calculate_size(&data, 8, arena);
    ASSERT_EQ(calculated_size, expected_size);
    
    /* Serialize */
    uint8_t* buffer = (uint8_t*)nmo_arena_alloc(arena, calculated_size + 100, 16);
    ASSERT_NOT_NULL(buffer);
    size_t bytes_written = 0;
    nmo_result_t result = nmo_data_section_serialize(&data, 8, buffer, calculated_size + 100, &bytes_written, arena);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(bytes_written, expected_size);
    
    /* Parse it back */
    nmo_data_section_t parsed = {0};
    parsed.object_count = 2;
    result = nmo_data_section_parse(buffer, bytes_written, 8, &parsed, arena);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(parsed.object_count, 2);
    ASSERT_NOT_NULL(parsed.objects);
    
    /* Verify objects */
    ASSERT_EQ(parsed.objects[0].data_size, 0);
    ASSERT_NULL(parsed.objects[0].chunk);
    ASSERT_EQ(parsed.objects[1].data_size, 0);
    ASSERT_NULL(parsed.objects[1].chunk);
    
    nmo_arena_destroy(arena);
}

static void test_mixed_data_roundtrip(void) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    /* Create data section */
    nmo_data_section_t data = {0};
    data.manager_count = 2;
    data.object_count = 3;
    
    /* Allocate arrays */
    data.managers = (nmo_manager_data_t*)nmo_arena_alloc(arena, sizeof(nmo_manager_data_t) * 2, 8);
    data.objects = (nmo_object_data_t*)nmo_arena_alloc(arena, sizeof(nmo_object_data_t) * 3, 8);
    ASSERT_NOT_NULL(data.managers);
    ASSERT_NOT_NULL(data.objects);
    memset(data.managers, 0, sizeof(nmo_manager_data_t) * 2);
    memset(data.objects, 0, sizeof(nmo_object_data_t) * 3);
    
    /* Setup managers */
    data.managers[0].guid.d1 = 0xAAAAAAAA;
    data.managers[0].guid.d2 = 0xBBBBBBBB;
    data.managers[1].guid.d1 = 0xCCCCCCCC;
    data.managers[1].guid.d2 = 0xDDDDDDDD;
    
    /* Calculate size: 2 * (GUID + size) + 3 * size = 2*12 + 3*4 = 36 bytes */
    size_t expected_size = 36;
    size_t calculated_size = nmo_data_section_calculate_size(&data, 8, arena);
    ASSERT_EQ(calculated_size, expected_size);
    
    /* Serialize */
    uint8_t* buffer = (uint8_t*)nmo_arena_alloc(arena, calculated_size + 100, 16);
    ASSERT_NOT_NULL(buffer);
    size_t bytes_written = 0;
    nmo_result_t result = nmo_data_section_serialize(&data, 8, buffer, calculated_size + 100, &bytes_written, arena);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(bytes_written, expected_size);
    
    /* Parse it back */
    nmo_data_section_t parsed = {0};
    parsed.manager_count = 2;
    parsed.object_count = 3;
    result = nmo_data_section_parse(buffer, bytes_written, 8, &parsed, arena);
    ASSERT_EQ(result.code, NMO_OK);
    
    /* Verify managers */
    ASSERT_EQ(parsed.managers[0].guid.d1, 0xAAAAAAAA);
    ASSERT_EQ(parsed.managers[0].guid.d2, 0xBBBBBBBB);
    ASSERT_EQ(parsed.managers[1].guid.d1, 0xCCCCCCCC);
    ASSERT_EQ(parsed.managers[1].guid.d2, 0xDDDDDDDD);
    
    nmo_arena_destroy(arena);
}

static void test_manager_with_chunk_data(void) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    
    /* Create data section with one manager */
    nmo_data_section_t data = {0};
    data.manager_count = 1;
    data.managers = (nmo_manager_data_t*)nmo_arena_alloc(arena, sizeof(nmo_manager_data_t), 8);
    ASSERT_NOT_NULL(data.managers);
    memset(data.managers, 0, sizeof(nmo_manager_data_t));
    
    /* Setup manager GUID */
    data.managers[0].guid.d1 = 0x12345678;
    data.managers[0].guid.d2 = 0x9ABCDEF0;
    
    /* Create a chunk with some data */
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    /* Set chunk properties using API */
    nmo_chunk_set_data_version(chunk, 1);
    // Note: chunk_version and chunk_class_id don't have public setters,
    // they are set during chunk creation/serialization
    
    /* Write data using API instead of direct manipulation */
    nmo_result_t write_result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(write_result.code, NMO_OK);
    write_result = nmo_chunk_write_dword(chunk, 0xDEADBEEF);
    ASSERT_EQ(write_result.code, NMO_OK);
    write_result = nmo_chunk_write_dword(chunk, 0xCAFEBABE);
    ASSERT_EQ(write_result.code, NMO_OK);
    write_result = nmo_chunk_write_dword(chunk, 0x12345678);
    ASSERT_EQ(write_result.code, NMO_OK);
    nmo_chunk_close(chunk);
    
    /* Serialize chunk to get its size */
    void* chunk_data = NULL;
    size_t chunk_size = 0;
    nmo_result_t result = nmo_chunk_serialize_version1(chunk, &chunk_data, &chunk_size, arena);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NOT_NULL(chunk_data);
    ASSERT_GT(chunk_size, 0);
    
    printf("  Chunk serialized to %zu bytes\n", chunk_size);
    
    /* Store serialized data in chunk's raw_data for round-trip */
    chunk->raw_data = chunk_data;
    chunk->raw_size = chunk_size;
    
    data.managers[0].chunk = chunk;
    data.managers[0].data_size = (uint32_t)chunk_size;
    
    /* Calculate total size: GUID (8) + size (4) + chunk_data */
    size_t expected_size = 8 + 4 + chunk_size;
    size_t calculated_size = nmo_data_section_calculate_size(&data, 8, arena);
    ASSERT_EQ(calculated_size, expected_size);
    
    /* Serialize */
    uint8_t* buffer = (uint8_t*)nmo_arena_alloc(arena, calculated_size + 100, 16);
    ASSERT_NOT_NULL(buffer);
    size_t bytes_written = 0;
    result = nmo_data_section_serialize(&data, 8, buffer, calculated_size + 100, &bytes_written, arena);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(bytes_written, expected_size);
    
    printf("  Data section serialized to %zu bytes\n", bytes_written);
    
    /* Parse it back */
    nmo_data_section_t parsed = {0};
    parsed.manager_count = 1;
    result = nmo_data_section_parse(buffer, bytes_written, 8, &parsed, arena);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(parsed.manager_count, 1);
    ASSERT_NOT_NULL(parsed.managers);
    
    /* Verify GUID */
    ASSERT_EQ(parsed.managers[0].guid.d1, 0x12345678);
    ASSERT_EQ(parsed.managers[0].guid.d2, 0x9ABCDEF0);
    ASSERT_EQ(parsed.managers[0].data_size, chunk_size);
    ASSERT_NOT_NULL(parsed.managers[0].chunk);
    
    /* Verify chunk properties using API */
    nmo_chunk_t* parsed_chunk = parsed.managers[0].chunk;
    ASSERT_EQ(nmo_chunk_get_data_version(parsed_chunk), 1);
    // Note: chunk_version and chunk_class_id don't have public getters
    // We verify the data size and content instead
    ASSERT_EQ(nmo_chunk_get_data_size(parsed_chunk), 12);  /* 3 DWORDs = 12 bytes */
    
    /* Verify chunk data by reading it back */
    nmo_result_t read_result = nmo_chunk_start_read(parsed_chunk);
    ASSERT_EQ(read_result.code, NMO_OK);
    uint32_t dword1, dword2, dword3;
    read_result = nmo_chunk_read_dword(parsed_chunk, &dword1);
    ASSERT_EQ(read_result.code, NMO_OK);
    ASSERT_EQ(dword1, 0xDEADBEEF);
    read_result = nmo_chunk_read_dword(parsed_chunk, &dword2);
    ASSERT_EQ(read_result.code, NMO_OK);
    ASSERT_EQ(dword2, 0xCAFEBABE);
    read_result = nmo_chunk_read_dword(parsed_chunk, &dword3);
    ASSERT_EQ(read_result.code, NMO_OK);
    ASSERT_EQ(dword3, 0x12345678);
    
    nmo_arena_destroy(arena);
}

int main(void) {
    register_tests();
    return test_framework_run();
}