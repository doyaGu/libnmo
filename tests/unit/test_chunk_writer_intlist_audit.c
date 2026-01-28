/**
 * @file test_chunk_writer_intlist_audit.c
 * @brief Unit tests for the IntList Auditor (Phase 2.3)
 *
 * Tests the DEBUG-mode IntList auditor that validates object ID counts.
 * This ensures that declared ID counts match actual written counts,
 * preventing undefined behavior in the Reference SDK.
 *
 * By default, the auditor returns NMO_ERR_CORRUPT on mismatch.
 * Define NMO_INTLIST_AUDIT_HARD at compile time to enable assertions.
 */

#include "format/nmo_chunk_writer.h"
#include "core/nmo_arena.h"
#include "test_framework.h"

/* ============================================================================
 * Basic Audit Tests
 * ============================================================================ */

TEST(intlist_audit, basic_matching_count) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* Write count first */
    nmo_chunk_writer_write_dword(w, 3);

    /* Begin audit with expected count of 3 */
    nmo_chunk_writer_begin_intlist(w, 3, "test.object_ids");

#ifndef NDEBUG
    /* Verify audit is active (DEBUG mode only) */
    ASSERT_EQ(nmo_chunk_writer_intlist_audit_active(w), 1);
    ASSERT_NOT_NULL(nmo_chunk_writer_intlist_audit_context(w));
#endif

    /* Write exactly 3 IDs */
    ASSERT_EQ(nmo_chunk_writer_write_object_id_audited(w, 100), NMO_OK);
    ASSERT_EQ(nmo_chunk_writer_write_object_id_audited(w, 200), NMO_OK);
    ASSERT_EQ(nmo_chunk_writer_write_object_id_audited(w, 300), NMO_OK);

    /* End audit - should succeed (counts match) */
    ASSERT_EQ(nmo_chunk_writer_end_intlist(w), NMO_OK);

#ifndef NDEBUG
    /* Verify audit is no longer active */
    ASSERT_EQ(nmo_chunk_writer_intlist_audit_active(w), 0);
#endif

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(intlist_audit, zero_count) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* Write count of 0 */
    nmo_chunk_writer_write_dword(w, 0);

    /* Begin audit with expected count of 0 */
    nmo_chunk_writer_begin_intlist(w, 0, "test.empty_list");

    /* Write no IDs - should match */
    ASSERT_EQ(nmo_chunk_writer_end_intlist(w), NMO_OK);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

#ifndef NDEBUG
TEST(intlist_audit, context_string) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* Begin with a specific context string */
    nmo_chunk_writer_begin_intlist(w, 1, "CKObject.children");

    const char *ctx = nmo_chunk_writer_intlist_audit_context(w);
    ASSERT_NOT_NULL(ctx);
    ASSERT_STR_EQ(ctx, "CKObject.children");

    ASSERT_EQ(nmo_chunk_writer_write_object_id_audited(w, 42), NMO_OK);
    ASSERT_EQ(nmo_chunk_writer_end_intlist(w), NMO_OK);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(intlist_audit, null_context_string) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* Begin with NULL context */
    nmo_chunk_writer_begin_intlist(w, 1, NULL);

    const char *ctx = nmo_chunk_writer_intlist_audit_context(w);
    ASSERT_NOT_NULL(ctx);
    ASSERT_EQ(ctx[0], '\0');  /* Empty string */

    ASSERT_EQ(nmo_chunk_writer_write_object_id_audited(w, 42), NMO_OK);
    ASSERT_EQ(nmo_chunk_writer_end_intlist(w), NMO_OK);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}
#endif /* NDEBUG */

/* ============================================================================
 * Mismatch Detection Tests (require NMO_INTLIST_AUDIT_SOFT and DEBUG)
 * ============================================================================ */

#ifndef NDEBUG
TEST(intlist_audit, mismatch_too_few) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* Declare 3 IDs */
    nmo_chunk_writer_write_dword(w, 3);
    nmo_chunk_writer_begin_intlist(w, 3, "test.too_few");

    /* Write only 2 IDs (one short) */
    nmo_chunk_writer_write_object_id_audited(w, 100);
    nmo_chunk_writer_write_object_id_audited(w, 200);

    /* End audit - should detect mismatch (soft mode returns error) */
    int result = nmo_chunk_writer_end_intlist(w);
    ASSERT_EQ(result, NMO_ERR_CORRUPT);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(intlist_audit, mismatch_too_many) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* Declare 2 IDs */
    nmo_chunk_writer_write_dword(w, 2);
    nmo_chunk_writer_begin_intlist(w, 2, "test.too_many");

    /* Write 3 IDs (one extra) */
    nmo_chunk_writer_write_object_id_audited(w, 100);
    nmo_chunk_writer_write_object_id_audited(w, 200);
    nmo_chunk_writer_write_object_id_audited(w, 300);

    /* End audit - should detect mismatch */
    int result = nmo_chunk_writer_end_intlist(w);
    ASSERT_EQ(result, NMO_ERR_CORRUPT);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(intlist_audit, mismatch_expected_zero_got_some) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* Declare 0 IDs */
    nmo_chunk_writer_write_dword(w, 0);
    nmo_chunk_writer_begin_intlist(w, 0, "test.expected_zero");

    /* Write 1 ID (shouldn't have any) */
    nmo_chunk_writer_write_object_id_audited(w, 100);

    /* End audit - should detect mismatch */
    int result = nmo_chunk_writer_end_intlist(w);
    ASSERT_EQ(result, NMO_ERR_CORRUPT);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(intlist_audit, end_without_begin) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* Try to end without beginning - should fail */
    int result = nmo_chunk_writer_end_intlist(w);
    ASSERT_EQ(result, NMO_ERR_INVALID_STATE);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(intlist_audit, long_context_string) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* Use a very long context string (should be truncated) */
    const char *long_context =
        "CKBehavior.very_long_field_name_that_exceeds_the_maximum_length_limit_for_context_strings";

    nmo_chunk_writer_begin_intlist(w, 1, long_context);

    const char *ctx = nmo_chunk_writer_intlist_audit_context(w);
    ASSERT_NOT_NULL(ctx);
    /* Should be truncated but still valid */
    ASSERT_TRUE(strlen(ctx) < NMO_INTLIST_CONTEXT_MAX);

    nmo_chunk_writer_write_object_id_audited(w, 42);
    ASSERT_EQ(nmo_chunk_writer_end_intlist(w), NMO_OK);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}
#endif /* NDEBUG */

/* ============================================================================
 * Edge Cases (work in both Debug and Release)
 * ============================================================================ */

TEST(intlist_audit, audited_write_without_audit) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* Write audited ID without begin_intlist - should still work */
    ASSERT_EQ(nmo_chunk_writer_write_object_id_audited(w, 42), NMO_OK);

    /* Verify the ID was written */
    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(w);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(intlist_audit, multiple_sequences) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* First IntList: 2 IDs */
    nmo_chunk_writer_write_dword(w, 2);
    nmo_chunk_writer_begin_intlist(w, 2, "first_list");
    nmo_chunk_writer_write_object_id_audited(w, 10);
    nmo_chunk_writer_write_object_id_audited(w, 20);
    ASSERT_EQ(nmo_chunk_writer_end_intlist(w), NMO_OK);

    /* Second IntList: 3 IDs */
    nmo_chunk_writer_write_dword(w, 3);
    nmo_chunk_writer_begin_intlist(w, 3, "second_list");
    nmo_chunk_writer_write_object_id_audited(w, 100);
    nmo_chunk_writer_write_object_id_audited(w, 200);
    nmo_chunk_writer_write_object_id_audited(w, 300);
    ASSERT_EQ(nmo_chunk_writer_end_intlist(w), NMO_OK);

    /* Third IntList: 0 IDs */
    nmo_chunk_writer_write_dword(w, 0);
    nmo_chunk_writer_begin_intlist(w, 0, "empty_list");
    ASSERT_EQ(nmo_chunk_writer_end_intlist(w), NMO_OK);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(intlist_audit, null_writer) {
    /* NULL writer should not crash */
    nmo_chunk_writer_begin_intlist(NULL, 5, "test");
    ASSERT_EQ(nmo_chunk_writer_write_object_id_audited(NULL, 42), NMO_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(nmo_chunk_writer_intlist_audit_active(NULL), 0);
    ASSERT_NULL(nmo_chunk_writer_intlist_audit_context(NULL));
}

/* ============================================================================
 * Integration Test
 * ============================================================================ */

TEST(intlist_audit, integration_with_chunk_finalize) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x5678, 7);

    /* Write some regular data */
    nmo_chunk_writer_write_dword(w, 0xABCD1234);
    nmo_chunk_writer_write_string(w, "test_object");

    /* Write an audited IntList */
    nmo_chunk_writer_write_dword(w, 2);  /* count */
    nmo_chunk_writer_begin_intlist(w, 2, "CKObject.dependencies");
    nmo_chunk_writer_write_object_id_audited(w, 1000);
    nmo_chunk_writer_write_object_id_audited(w, 2000);
    ASSERT_EQ(nmo_chunk_writer_end_intlist(w), NMO_OK);

    /* Write more data after the IntList */
    nmo_chunk_writer_write_float(w, 3.14159f);

    /* Finalize and verify chunk is valid */
    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(w);
    ASSERT_NOT_NULL(chunk);
    ASSERT_TRUE(chunk->data.count > 0);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test Registration
 * ============================================================================ */

TEST_MAIN_BEGIN()
    /* Basic tests (work in both Debug and Release) */
    REGISTER_TEST(intlist_audit, basic_matching_count);
    REGISTER_TEST(intlist_audit, zero_count);

#ifndef NDEBUG
    /* DEBUG-only tests */
    REGISTER_TEST(intlist_audit, context_string);
    REGISTER_TEST(intlist_audit, null_context_string);

    /* Mismatch detection tests */
    REGISTER_TEST(intlist_audit, mismatch_too_few);
    REGISTER_TEST(intlist_audit, mismatch_too_many);
    REGISTER_TEST(intlist_audit, mismatch_expected_zero_got_some);

    /* Edge cases that need DEBUG */
    REGISTER_TEST(intlist_audit, end_without_begin);
    REGISTER_TEST(intlist_audit, long_context_string);
#endif

    /* Edge cases (work in both Debug and Release) */
    REGISTER_TEST(intlist_audit, audited_write_without_audit);
    REGISTER_TEST(intlist_audit, multiple_sequences);
    REGISTER_TEST(intlist_audit, null_writer);

    /* Integration test */
    REGISTER_TEST(intlist_audit, integration_with_chunk_finalize);
TEST_MAIN_END()
