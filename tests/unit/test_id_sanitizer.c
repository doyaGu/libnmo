/**
 * @file test_id_sanitizer.c
 * @brief Unit tests for the ID sanitization pipeline (Phase 1.1)
 */

#include "session/nmo_id_sanitizer.h"
#include "core/nmo_arena.h"
#include "test_framework.h"

TEST(id_sanitizer, strips_mask_and_passthrough) {
    ASSERT_EQ(nmo_id_sanitize(NMO_ID_REF_MASK | 0x42u), 0x42u);
    ASSERT_EQ(nmo_id_sanitize(0x00001234u), 0x00001234u);
    ASSERT_EQ(nmo_id_sanitize(0u), 0u);
}

TEST(id_sanitizer, registers_bidirectional_mappings) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_id_sanitizer_t *s = nmo_id_sanitizer_create(arena);
    ASSERT_NOT_NULL(s);

    /* Register two mappings */
    ASSERT_EQ(nmo_id_sanitizer_register(s, 1, 1), NMO_OK);
    ASSERT_EQ(nmo_id_sanitizer_register(s, 5, 42), NMO_OK);

    /* File -> runtime lookups */
    ASSERT_EQ(nmo_id_file_to_runtime(s, 1), 1u);
    ASSERT_EQ(nmo_id_file_to_runtime(s, 5), 42u);
    ASSERT_EQ(nmo_id_file_to_runtime(s, 99), NMO_OBJECT_ID_INVALID);

    /* Runtime -> file lookups */
    ASSERT_EQ(nmo_id_runtime_to_file(s, 1), 1u);
    ASSERT_EQ(nmo_id_runtime_to_file(s, 42), 5u);
    ASSERT_EQ(nmo_id_runtime_to_file(s, 999), NMO_OBJECT_ID_INVALID);

    /* Reset should clear mappings */
    nmo_id_sanitizer_reset(s);
    ASSERT_EQ(nmo_id_file_to_runtime(s, 1), NMO_OBJECT_ID_INVALID);
    ASSERT_EQ(nmo_id_runtime_to_file(s, 42), NMO_OBJECT_ID_INVALID);

    nmo_id_sanitizer_destroy(s);
    nmo_arena_destroy(arena);
}

TEST(id_sanitizer, reset_clears_state) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 2048);
    ASSERT_NOT_NULL(arena);

    nmo_id_sanitizer_t *s = nmo_id_sanitizer_create(arena);
    ASSERT_NOT_NULL(s);

    ASSERT_EQ(nmo_id_sanitizer_register(s, 1, 10), NMO_OK);
    ASSERT_EQ(nmo_id_sanitizer_register(s, 2, 20), NMO_OK);
    ASSERT_EQ(nmo_id_file_to_runtime(s, 1), 10u);
    ASSERT_EQ(nmo_id_runtime_to_file(s, 20), 2u);

    nmo_id_sanitizer_reset(s);
    ASSERT_EQ(nmo_id_file_to_runtime(s, 1), NMO_OBJECT_ID_INVALID);
    ASSERT_EQ(nmo_id_runtime_to_file(s, 20), NMO_OBJECT_ID_INVALID);

    nmo_id_sanitizer_destroy(s);
    nmo_arena_destroy(arena);
}

TEST(id_sanitizer, mask_handling_on_registration) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 2048);
    ASSERT_NOT_NULL(arena);

    nmo_id_sanitizer_t *s = nmo_id_sanitizer_create(arena);
    ASSERT_NOT_NULL(s);

    /* Runtime ID carries mask; lookup should strip it */
    ASSERT_EQ(nmo_id_sanitizer_register(s, 7, NMO_ID_REF_MASK | 0x21u), NMO_OK);
    ASSERT_EQ(nmo_id_file_to_runtime(s, 7), 0x21u);
    ASSERT_EQ(nmo_id_runtime_to_file(s, NMO_ID_REF_MASK | 0x21u), 7u);

    nmo_id_sanitizer_destroy(s);
    nmo_arena_destroy(arena);
}

TEST(id_sanitizer, reseed_bulk_loads_mappings) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_id_sanitizer_t *s = nmo_id_sanitizer_create(arena);
    ASSERT_NOT_NULL(s);

    uint32_t file_ids[] = {1, 2, 3};
    uint32_t runtime[] = {10, 20, 30};
    ASSERT_EQ(nmo_id_sanitizer_reseed(s, file_ids, runtime, 3), NMO_OK);

    ASSERT_EQ(nmo_id_file_to_runtime(s, 1), 10u);
    ASSERT_EQ(nmo_id_file_to_runtime(s, 2), 20u);
    ASSERT_EQ(nmo_id_file_to_runtime(s, 3), 30u);

    nmo_id_sanitizer_destroy(s);
    nmo_arena_destroy(arena);
}

TEST(id_sanitizer, tracks_external_negative_ids) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_id_sanitizer_t *s = nmo_id_sanitizer_create(arena);
    ASSERT_NOT_NULL(s);

    /* Negative IDs become positive runtime IDs and are tracked */
    ASSERT_EQ(nmo_id_register_external(s, -5), 5);
    ASSERT_EQ(nmo_id_original_external(s, 5), -5);

    /* Masked negatives still resolve to the same runtime key */
    ASSERT_EQ(nmo_id_original_external(s, NMO_ID_REF_MASK | 5u), -5);

    /* Non-negative inputs are passed through without tracking */
    ASSERT_EQ(nmo_id_register_external(s, 15), 15);
    ASSERT_EQ(nmo_id_original_external(s, 15), 0);

    /* Invalid inputs */
    ASSERT_EQ(nmo_id_register_external(NULL, -3), (int32_t)NMO_OBJECT_ID_INVALID);
    ASSERT_EQ(nmo_id_register_external(s, 0), (int32_t)NMO_OBJECT_ID_INVALID);

    nmo_id_sanitizer_destroy(s);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(id_sanitizer, strips_mask_and_passthrough);
    REGISTER_TEST(id_sanitizer, registers_bidirectional_mappings);
    REGISTER_TEST(id_sanitizer, reset_clears_state);
    REGISTER_TEST(id_sanitizer, mask_handling_on_registration);
    REGISTER_TEST(id_sanitizer, reseed_bulk_loads_mappings);
    REGISTER_TEST(id_sanitizer, tracks_external_negative_ids);
TEST_MAIN_END()
