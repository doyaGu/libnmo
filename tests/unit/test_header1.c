/**
 * @file test_header1.c
 * @brief Unit tests for NMO Header1 format
 */

#include "nmo.h"
#include "test_framework.h"
#include "core/nmo_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TEST(header1, serialization) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    // Create test header with no objects
    nmo_header1_t header;
    memset(&header, 0, sizeof(header));
    header.object_count = 0;
    header.plugin_dep_count = 0;
    header.included_file_count = 0;
    
    void* out_data;
    size_t out_size;
    nmo_result_t result = nmo_header1_serialize(&header, &out_data, &out_size, arena);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NOT_NULL(out_data);
    ASSERT_TRUE(out_size > 0);
    
    nmo_arena_destroy(arena);
}

TEST(header1, round_trip) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    // Create test header with no objects
    nmo_header1_t header;
    memset(&header, 0, sizeof(header));
    header.object_count = 0;
    header.plugin_dep_count = 0;
    nmo_included_file_desc_t included[2];
    included[0].name = "a.txt";
    included[0].data_size = 10;
    included[1].name = "b.bin";
    included[1].data_size = 20;

    header.included_file_count = 2;
    header.included_files = included;
    
    void* out_data;
    size_t out_size;
    nmo_result_t result = nmo_header1_serialize(&header, &out_data, &out_size, arena);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NOT_NULL(out_data);
    ASSERT_TRUE(out_size > 0);
    
    // Parse serialized data
    nmo_header1_t parsed_header;
    memset(&parsed_header, 0, sizeof(parsed_header));
    parsed_header.object_count = 0;  // Must be set before parsing
    
    result = nmo_header1_parse(out_data, out_size, &parsed_header, arena);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(parsed_header.object_count, 0);
    ASSERT_NULL(parsed_header.objects);
    ASSERT_EQ(parsed_header.plugin_dep_count, 0);
    ASSERT_EQ(parsed_header.included_file_count, 2u);
    ASSERT_NOT_NULL(parsed_header.included_files);
    ASSERT_STR_EQ(parsed_header.included_files[0].name, "a.txt");
    ASSERT_EQ(parsed_header.included_files[0].data_size, 10u);
    ASSERT_STR_EQ(parsed_header.included_files[1].name, "b.bin");
    ASSERT_EQ(parsed_header.included_files[1].data_size, 20u);
    
    nmo_arena_destroy(arena);
}

TEST(header1, included_metadata_only) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024);
    ASSERT_NOT_NULL(arena);

    const char name1[] = "a.txt";
    const char name2[] = "b.bin";

    uint8_t buffer[64];
    size_t pos = 0;

    /* Header layout: [category_count=0][payload_size][included_count][entries...] */
    nmo_write_u32_le(buffer + pos, 0);
    pos += sizeof(uint32_t);

    uint32_t payload_size = sizeof(uint32_t) +
                            (sizeof(uint32_t) + (uint32_t)strlen(name1) + sizeof(uint32_t)) +
                            (sizeof(uint32_t) + (uint32_t)strlen(name2) + sizeof(uint32_t));

    nmo_write_u32_le(buffer + pos, payload_size);
    pos += sizeof(uint32_t);

    nmo_write_u32_le(buffer + pos, 2);
    pos += sizeof(uint32_t);

    nmo_write_u32_le(buffer + pos, (uint32_t)strlen(name1));
    pos += sizeof(uint32_t);
    memcpy(buffer + pos, name1, strlen(name1));
    pos += strlen(name1);
    nmo_write_u32_le(buffer + pos, 10);
    pos += sizeof(uint32_t);

    nmo_write_u32_le(buffer + pos, (uint32_t)strlen(name2));
    pos += sizeof(uint32_t);
    memcpy(buffer + pos, name2, strlen(name2));
    pos += strlen(name2);
    nmo_write_u32_le(buffer + pos, 20);
    pos += sizeof(uint32_t);

    nmo_header1_t header;
    memset(&header, 0, sizeof(header));
    header.object_count = 0;

    nmo_result_t result = nmo_header1_parse(buffer, pos, &header, arena);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(2u, header.included_file_count);
    ASSERT_NOT_NULL(header.included_files);
    ASSERT_STR_EQ(header.included_files[0].name, "a.txt");
    ASSERT_EQ(header.included_files[0].data_size, 10u);
    ASSERT_STR_EQ(header.included_files[1].name, "b.bin");
    ASSERT_EQ(header.included_files[1].data_size, 20u);

    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(header1, serialization);
    REGISTER_TEST(header1, round_trip);
    REGISTER_TEST(header1, included_metadata_only);
TEST_MAIN_END()
