/**
 * @file test_data_section_plan.c
 * @brief Unit tests for single-pass Data section serialization plans
 */

#include "../test_framework.h"

#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_data.h"

static nmo_data_section_t make_mixed_data_section(nmo_arena_t *arena) {
    nmo_data_section_t section = {0};
    section.manager_count = 1;
    section.object_count = 1;
    section.managers = nmo_arena_alloc(arena, sizeof(nmo_manager_data_t), 8);
    section.objects = nmo_arena_alloc(arena, sizeof(nmo_object_data_t), 8);
    memset(section.managers, 0, sizeof(nmo_manager_data_t));
    memset(section.objects, 0, sizeof(nmo_object_data_t));

    section.managers[0].guid = (nmo_guid_t){0x11223344u, 0x55667788u};
    section.managers[0].chunk = nmo_chunk_create(arena);
    nmo_chunk_write_dword(section.managers[0].chunk, 0x10u);
    nmo_chunk_write_dword(section.managers[0].chunk, 0x20u);
    nmo_chunk_write_dword(section.managers[0].chunk, 0x30u);
    nmo_chunk_write_dword(section.managers[0].chunk, 0x40u);

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
    ASSERT_GT(plan.manager_slices[0].size, 0u);
    ASSERT_GT(plan.object_slices[0].size, 0u);

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

TEST(data_section_plan, rejects_missing_storage) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_data_section_t section = {0};
    section.manager_count = 1;

    nmo_data_section_plan_t plan;
    memset(&plan, 0xA5, sizeof(plan));
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_data_section_plan_build(&section, 8, arena, &plan));
    ASSERT_EQ(0u, plan.total_size);
    ASSERT_EQ(0u, plan.manager_count);
    ASSERT_NULL(plan.manager_slices);

    uint8_t output[16] = {0};
    size_t bytes_written = SIZE_MAX;
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_data_section_serialize(
                  &section, 8, output, sizeof(output),
                  &bytes_written, arena));
    ASSERT_EQ(0u, bytes_written);

    section.manager_count = 0;
    section.object_count = 1;
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_data_section_plan_build(&section, 8, arena, &plan));

    nmo_arena_destroy(arena);
}

TEST(data_section_plan, build_failure_does_not_publish_prefix) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_manager_data_t manager = {0};
    nmo_object_data_t object = {0};
    object.chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(object.chunk);
    object.chunk->data.count = 1;
    object.chunk->data.capacity = 1;
    object.chunk->data.data = NULL;

    nmo_data_section_t section = {0};
    section.manager_count = 1;
    section.managers = &manager;
    section.object_count = 1;
    section.objects = &object;

    nmo_data_section_plan_t plan;
    memset(&plan, 0xA5, sizeof(plan));
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_data_section_plan_build(&section, 8, arena, &plan));
    ASSERT_EQ(0u, plan.total_size);
    ASSERT_EQ(0u, plan.manager_count);
    ASSERT_EQ(0u, plan.object_count);
    ASSERT_NULL(plan.manager_slices);
    ASSERT_NULL(plan.object_slices);

    nmo_arena_destroy(arena);
}

TEST(data_section_plan, calculate_size_uses_serialized_chunks) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_data_section_t section = make_mixed_data_section(arena);
    section.managers[0].data_size = 1;
    section.objects[0].data_size = UINT32_MAX;

    nmo_data_section_plan_t plan = {0};
    ASSERT_EQ(NMO_OK, nmo_data_section_plan_build(&section, 8, arena, &plan));

    size_t calculated = nmo_data_section_calculate_size(&section, 8, arena);
    ASSERT_EQ(plan.total_size, calculated);

    uint8_t *output = nmo_arena_alloc(arena, calculated, 16);
    ASSERT_NOT_NULL(output);
    size_t bytes_written = 0;
    ASSERT_EQ(NMO_OK,
              nmo_data_section_serialize(
                  &section, 8, output, calculated, &bytes_written, arena));
    ASSERT_EQ(calculated, bytes_written);

    section.managers[0].chunk = NULL;
    section.objects[0].chunk = NULL;
    ASSERT_EQ(16u, nmo_data_section_calculate_size(&section, 8, arena));

    nmo_arena_destroy(arena);
}

TEST(data_section_plan, rejects_invalid_plan_before_writing) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_data_section_t section = make_mixed_data_section(arena);
    nmo_data_section_plan_t plan = {0};
    ASSERT_EQ(NMO_OK, nmo_data_section_plan_build(&section, 6, arena, &plan));
    ASSERT_LT(plan.total_size, 256u);

    uint8_t output[256];
    uint8_t expected[256];
    memset(output, 0xA5, sizeof(output));
    memset(expected, 0xA5, sizeof(expected));

    section.objects[0].object_id = 0;
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_data_section_plan_write(
                  &section, &plan, 6, output, sizeof(output)));
    ASSERT_MEM_EQ(expected, output, sizeof(output));

    section.objects[0].object_id = 42;
    const uint8_t *object_bytes = plan.object_slices[0].bytes;
    plan.object_slices[0].bytes = NULL;
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_data_section_plan_write(
                  &section, &plan, 6, output, sizeof(output)));
    ASSERT_MEM_EQ(expected, output, sizeof(output));

    plan.object_slices[0].bytes = object_bytes;
    plan.total_size++;
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_data_section_plan_write(
                  &section, &plan, 6, output, sizeof(output)));
    ASSERT_MEM_EQ(expected, output, sizeof(output));

    nmo_arena_destroy(arena);
}

TEST(data_section_plan, rejects_missing_legacy_id_during_build) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_data_section_t section = make_mixed_data_section(arena);
    section.objects[0].object_id = 0;

    nmo_data_section_plan_t plan;
    memset(&plan, 0xA5, sizeof(plan));
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_data_section_plan_build(&section, 6, arena, &plan));
    ASSERT_EQ(0u, plan.total_size);
    ASSERT_EQ(0u, plan.manager_count);
    ASSERT_EQ(0u, plan.object_count);
    ASSERT_NULL(plan.manager_slices);
    ASSERT_NULL(plan.object_slices);

    nmo_arena_destroy(arena);
}

TEST(data_section_plan, legacy_serialize_failure_preserves_output) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_data_section_t section = make_mixed_data_section(arena);
    section.objects[0].object_id = 0;

    uint8_t output[256];
    uint8_t expected[256];
    memset(output, 0xA5, sizeof(output));
    memset(expected, 0xA5, sizeof(expected));
    size_t bytes_written = SIZE_MAX;

    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_data_section_serialize(
                  &section, 6, output, sizeof(output), &bytes_written, arena));
    ASSERT_EQ(0u, bytes_written);
    ASSERT_MEM_EQ(expected, output, sizeof(output));

    nmo_arena_destroy(arena);
}

typedef struct plan_allocator_state {
    nmo_allocator_t base;
    size_t large_requests;
} plan_allocator_state_t;

static void *plan_bounded_alloc(void *user_data, size_t size, size_t alignment) {
    plan_allocator_state_t *state = user_data;
    if (size > 64u * 1024u * 1024u) {
        ++state->large_requests;
        return NULL;
    }
    return state->base.alloc(state->base.user_data, size, alignment);
}

static void plan_bounded_free(void *user_data, void *ptr) {
    plan_allocator_state_t *state = user_data;
    state->base.free(state->base.user_data, ptr);
}

TEST(data_section_plan, preflights_signed_section_sizes_before_allocating) {
    for (uint32_t version = 6u; version <= 8u; version += 2u) {
        for (uint32_t mode = 0u; mode < 5u; ++mode) {
            plan_allocator_state_t state = {nmo_allocator_default(), 0u};
            nmo_allocator_t allocator = nmo_allocator_custom(
                plan_bounded_alloc, plan_bounded_free, &state);
            nmo_arena_t *arena = nmo_arena_create(&allocator, 4096u);
            ASSERT_NOT_NULL(arena);
            nmo_chunk_t *chunk = nmo_chunk_create(arena);
            ASSERT_NOT_NULL(chunk);
            uint32_t dummy = 0u;
            /* Metadata-only views: correct preflight never dereferences the
             * advertised payload, nor attempts a multi-gigabyte allocation. */
            chunk->data.data = &dummy;
            chunk->data.count = mode == 2u ? (size_t)INT32_MAX / 8u : (size_t)INT32_MAX / 4u;
            chunk->data.capacity = chunk->data.count;
            nmo_object_data_t objects[2] = {
                {.object_id = 1u, .chunk = chunk}, {.object_id = 2u, .chunk = chunk}
            };
            nmo_manager_data_t manager = {.chunk = chunk};
            nmo_data_section_t section = {0};
            if (mode == 1u || mode == 4u) {
                section.manager_count = mode == 4u ? UINT32_MAX : 1u;
                section.managers = &manager;
            } else {
                section.object_count = mode == 3u ? UINT32_MAX : mode == 2u ? 2u : 1u;
                section.objects = objects;
            }
            nmo_data_section_plan_t plan;
            memset(&plan, 0xA5, sizeof(plan));
            const nmo_status_t status = nmo_data_section_plan_build(&section, version, arena, &plan);
            ASSERT_EQ(NMO_ERR_CORRUPT, status);
            ASSERT_EQ(0u, state.large_requests);
            ASSERT_EQ(0u, plan.total_size);
            ASSERT_NULL(plan.manager_slices);
            ASSERT_NULL(plan.object_slices);
            ASSERT_EQ(0u, nmo_data_section_calculate_size(&section, version, arena));
            ASSERT_EQ(0u, state.large_requests);
            nmo_arena_destroy(arena);
        }
    }
}

TEST(data_section_plan, rejects_signed_overflow_before_writing_plan_bytes) {
    for (uint32_t manager_mode = 0u; manager_mode < 2u; ++manager_mode) {
        const size_t header_size = manager_mode ? 12u : 4u;
        const size_t sizes[] = {
            (size_t)INT32_MAX - header_size,
            (size_t)INT32_MAX - header_size + 1u,
            INT32_MAX, UINT32_MAX
        };
        uint8_t dummy = 0u;
        nmo_data_chunk_slice_t slices[2] = {{.bytes = &dummy}, {.bytes = &dummy}};
        nmo_object_data_t objects[2] = {{.object_id = 1u}, {.object_id = 2u}};
        nmo_manager_data_t managers[2] = {0};
        nmo_data_section_t section = {
            .object_count = manager_mode ? 0u : 1u, .objects = objects,
            .manager_count = manager_mode ? 1u : 0u, .managers = managers
        };
        nmo_data_section_plan_t plan = {
            .object_count = section.object_count, .object_slices = slices,
            .manager_count = section.manager_count, .manager_slices = slices
        };
        uint8_t output[16], expected[16];
        memset(output, 0xA5, sizeof(output));
        memset(expected, 0xA5, sizeof(expected));
        for (size_t i = 0u; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
            slices[0].size = sizes[i];
            plan.total_size = sizes[i] + header_size;
            const nmo_status_t status = nmo_data_section_plan_write(
                &section, &plan, 8u, output, sizeof(output));
            ASSERT_EQ(i == 0u ? NMO_ERR_BUFFER_OVERRUN : NMO_ERR_CORRUPT, status);
            ASSERT_MEM_EQ(expected, output, sizeof(output));
        }
        slices[0].size = slices[1].size = (size_t)INT32_MAX / 2u;
        section.manager_count = manager_mode ? 2u : 0u;
        section.object_count = manager_mode ? 0u : 2u;
        plan.manager_count = section.manager_count;
        plan.object_count = section.object_count;
        plan.total_size = 2u * (slices[0].size + header_size);
        const nmo_status_t status = nmo_data_section_plan_write(
            &section, &plan, 8u, output, sizeof(output));
        ASSERT_EQ(NMO_ERR_CORRUPT, status);
        ASSERT_MEM_EQ(expected, output, sizeof(output));
    }
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(data_section_plan, rejects_signed_overflow_before_writing_plan_bytes);
    REGISTER_TEST(data_section_plan, preflights_signed_section_sizes_before_allocating);
    REGISTER_TEST(data_section_plan, matches_legacy_serialization);
    REGISTER_TEST(data_section_plan, rejects_short_output_buffer);
    REGISTER_TEST(data_section_plan, rejects_missing_storage);
    REGISTER_TEST(data_section_plan, build_failure_does_not_publish_prefix);
    REGISTER_TEST(data_section_plan, calculate_size_uses_serialized_chunks);
    REGISTER_TEST(data_section_plan, rejects_invalid_plan_before_writing);
    REGISTER_TEST(data_section_plan, rejects_missing_legacy_id_during_build);
    REGISTER_TEST(data_section_plan, legacy_serialize_failure_preserves_output);
TEST_MAIN_END()
