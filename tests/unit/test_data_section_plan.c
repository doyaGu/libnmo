/**
 * @file test_data_section_plan.c
 * @brief Unit tests for single-pass Data section serialization plans
 */

#include "../test_framework.h"

#include "core/nmo_arena.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_data.h"

static nmo_data_section_t make_mixed_data_section(nmo_arena_t *arena) {
    static const uint8_t raw_manager_data[] = {
        0x10, 0x00, 0x00, 0x00,
        0x20, 0x00, 0x00, 0x00,
        0x30, 0x00, 0x00, 0x00,
        0x40, 0x00, 0x00, 0x00,
    };

    nmo_data_section_t section = {0};
    section.manager_count = 1;
    section.object_count = 1;
    section.managers = nmo_arena_alloc(arena, sizeof(nmo_manager_data_t), 8);
    section.objects = nmo_arena_alloc(arena, sizeof(nmo_object_data_t), 8);

    section.managers[0].guid = (nmo_guid_t){0x11223344u, 0x55667788u};
    section.managers[0].data_size = (uint32_t)sizeof(raw_manager_data);
    section.managers[0].chunk = nmo_chunk_create(arena);
    section.managers[0].chunk->raw_data = raw_manager_data;
    section.managers[0].chunk->raw_size = sizeof(raw_manager_data);

    section.objects[0].object_id = 42;
    section.objects[0].chunk = nmo_chunk_create(arena);
    nmo_chunk_write_int(section.objects[0].chunk, 123);
    nmo_chunk_write_dword(section.objects[0].chunk, 0xAABBCCDDu);

    return section;
}

TEST(data_section_plan, matches_legacy_serialization) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_data_section_t section = make_mixed_data_section(arena);

    size_t legacy_size = nmo_data_section_calculate_size(&section, 8, arena);
    ASSERT_GT(legacy_size, 0);

    uint8_t *legacy = nmo_arena_alloc(arena, legacy_size, 16);
    ASSERT_NOT_NULL(legacy);

    size_t legacy_written = 0;
    ASSERT_EQ(NMO_OK, nmo_data_section_serialize(&section, 8, legacy, legacy_size, &legacy_written, arena));
    ASSERT_EQ(legacy_size, legacy_written);

    nmo_data_section_plan_t plan = {0};
    ASSERT_EQ(NMO_OK, nmo_data_section_plan_build(&section, 8, arena, &plan));
    ASSERT_EQ(legacy_written, plan.total_size);
    ASSERT_EQ(1, plan.manager_count);
    ASSERT_EQ(1, plan.object_count);
    ASSERT_TRUE(plan.manager_slices[0].borrowed);
    ASSERT_FALSE(plan.object_slices[0].borrowed);

    uint8_t *planned = nmo_arena_alloc(arena, plan.total_size, 16);
    ASSERT_NOT_NULL(planned);

    ASSERT_EQ(NMO_OK, nmo_data_section_plan_write(&section, &plan, 8, planned, plan.total_size));
    ASSERT_MEM_EQ(legacy, planned, legacy_written);

    nmo_arena_destroy(arena);
}

TEST(data_section_plan, rejects_short_output_buffer) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_data_section_t section = make_mixed_data_section(arena);
    nmo_data_section_plan_t plan = {0};
    ASSERT_EQ(NMO_OK, nmo_data_section_plan_build(&section, 8, arena, &plan));
    ASSERT_GT(plan.total_size, 0);

    uint8_t *output = nmo_arena_alloc(arena, plan.total_size, 16);
    ASSERT_NOT_NULL(output);

    ASSERT_EQ(NMO_ERR_BUFFER_OVERRUN,
              nmo_data_section_plan_write(&section, &plan, 8, output, plan.total_size - 1));

    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(data_section_plan, matches_legacy_serialization);
    REGISTER_TEST(data_section_plan, rejects_short_output_buffer);
TEST_MAIN_END()
