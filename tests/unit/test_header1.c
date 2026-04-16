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
#include <limits.h>
#include <stdint.h>

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
    nmo_status_t result = nmo_header1_serialize(&header, &out_data, &out_size, arena);
    ASSERT_EQ(result, NMO_OK);
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
    nmo_status_t result = nmo_header1_serialize(&header, &out_data, &out_size, arena);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_NOT_NULL(out_data);
    ASSERT_TRUE(out_size > 0);
    
    // Parse serialized data
    nmo_header1_t parsed_header;
    memset(&parsed_header, 0, sizeof(parsed_header));
    parsed_header.object_count = 0;  // Must be set before parsing
    
    result = nmo_header1_parse(out_data, out_size, &parsed_header, arena);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(parsed_header.object_count, 0);
    ASSERT_NULL(parsed_header.objects);
    ASSERT_EQ(parsed_header.plugin_dep_count, 0);
    ASSERT_EQ(parsed_header.included_file_count, 2u);
    /* Header1 only stores included-file count in clean-break runtime. */
    ASSERT_NULL(parsed_header.included_files);
    
    nmo_arena_destroy(arena);
}

TEST(header1, planned_write_matches_serialize) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_object_desc_t objects[2];
    memset(objects, 0, sizeof(objects));
    objects[0].file_id = 101;
    objects[0].class_id = 0x1111u;
    objects[0].file_index = 64;
    objects[0].name = "alpha";
    objects[1].file_id = 102;
    objects[1].class_id = 0x2222u;
    objects[1].file_index = 128;
    objects[1].name = "beta";
    objects[1].flags = NMO_OBJECT_REFERENCE_FLAG;

    nmo_plugin_dep_t deps[3];
    memset(deps, 0, sizeof(deps));
    deps[0].category = 11;
    deps[0].guid = (nmo_guid_t){0xAAAA0001u, 0xBBBB0001u};
    deps[1].category = 22;
    deps[1].guid = (nmo_guid_t){0xAAAA0002u, 0xBBBB0002u};
    deps[2].category = 11;
    deps[2].guid = (nmo_guid_t){0xAAAA0003u, 0xBBBB0003u};

    nmo_header1_t header;
    memset(&header, 0, sizeof(header));
    header.object_count = 2;
    header.objects = objects;
    header.plugin_dep_count = 3;
    header.plugin_deps = deps;
    header.included_file_count = 1;

    void* legacy_data = NULL;
    size_t legacy_size = 0;
    ASSERT_EQ(NMO_OK, nmo_header1_serialize(&header, &legacy_data, &legacy_size, arena));

    nmo_header1_layout_t layout;
    memset(&layout, 0, sizeof(layout));
    ASSERT_EQ(NMO_OK, nmo_header1_plan(&header, arena, &layout));
    ASSERT_EQ((size_t)41, layout.object_table_size);
    ASSERT_EQ((size_t)44, layout.plugin_dep_size);
    ASSERT_EQ((size_t)8, layout.included_metadata_size);
    ASSERT_EQ(legacy_size, layout.total_size);

    uint8_t* planned_data = NULL;
    size_t planned_size = 0;
    ASSERT_EQ(NMO_OK, nmo_header1_write_planned(&header, &layout, arena, &planned_data, &planned_size));
    ASSERT_EQ(legacy_size, planned_size);
    ASSERT_MEM_EQ(legacy_data, planned_data, legacy_size);

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

    nmo_status_t result = nmo_header1_parse(buffer, pos, &header, arena);
    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(2u, header.included_file_count);
    ASSERT_NULL(header.included_files);

    nmo_arena_destroy(arena);
}

TEST(header1, size_overflow) {
    if (SIZE_MAX > UINT32_MAX) {
        return;
    }

    nmo_arena_t* arena = nmo_arena_create(NULL, 1024);
    ASSERT_NOT_NULL(arena);

    nmo_header1_t header;
    memset(&header, 0, sizeof(header));
    header.object_count = 0;
    header.plugin_dep_count = (uint32_t)(SIZE_MAX / sizeof(uint32_t)) + 1u;
    header.plugin_deps = NULL;
    header.included_file_count = 0;

    void* out_data = NULL;
    size_t out_size = 0;
    nmo_status_t result = nmo_header1_serialize(&header, &out_data, &out_size, arena);
    ASSERT_EQ(result, NMO_ERR_CORRUPT);

    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(header1, serialization);
    REGISTER_TEST(header1, round_trip);
    REGISTER_TEST(header1, planned_write_matches_serialize);
    REGISTER_TEST(header1, included_metadata_only);
    REGISTER_TEST(header1, size_overflow);
TEST_MAIN_END()
