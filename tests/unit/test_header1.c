/**
 * @file test_header1.c
 * @brief Unit tests for NMO Header1 format
 */

#include "nmo.h"
#include "test_framework.h"
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
    header.included_file_count = 0;
    
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
    ASSERT_EQ(parsed_header.included_file_count, 0);
    
    nmo_arena_destroy(arena);
}

TEST(header1, included_metadata_only) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024);
    ASSERT_NOT_NULL(arena);

    /* Header layout: [category_count=0][included_count=2][table_size=0] */
    uint32_t buffer[] = {
        0, /* plugin categories */
        2, /* included files referenced */
        0  /* no inline descriptor table */
    };

    nmo_header1_t header;
    memset(&header, 0, sizeof(header));
    header.object_count = 0;

    nmo_result_t result = nmo_header1_parse(buffer, sizeof(buffer), &header, arena);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(2u, header.included_file_count);
    ASSERT_NULL(header.included_files);

    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(header1, serialization);
    REGISTER_TEST(header1, round_trip);
    REGISTER_TEST(header1, included_metadata_only);
TEST_MAIN_END()
