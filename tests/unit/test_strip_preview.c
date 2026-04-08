#include "test_framework.h"
#include "app/nmo_context.h"
#include "app/nmo_session.h"
#include "core/nmo_arena.h"
#include <string.h>

/* ---- Invalid arguments ---- */

TEST(strip_preview, null_session_returns_error) {
    ASSERT_EQ(
        NMO_ERR_INVALID_ARGUMENT,
        nmo_session_preview_destroy(NULL, NULL, 0, 0, NULL, NULL, NULL));
}

TEST(strip_preview, null_ids_returns_error) {
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t *out_ids = NULL;
    size_t out_count = 0;
    nmo_arena_t *arena = nmo_session_get_arena(session);
    ASSERT_EQ(
        NMO_ERR_INVALID_ARGUMENT,
        nmo_session_preview_destroy(session, NULL, 1, 0, arena, &out_ids, &out_count));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(strip_preview, zero_count_returns_error) {
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t id = 1;
    nmo_object_id_t *out_ids = NULL;
    size_t out_count = 0;
    nmo_arena_t *arena = nmo_session_get_arena(session);
    ASSERT_EQ(
        NMO_ERR_INVALID_ARGUMENT,
        nmo_session_preview_destroy(session, &id, 0, 0, arena, &out_ids, &out_count));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(strip_preview, null_output_params_returns_error) {
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t id = 1;
    nmo_arena_t *arena = nmo_session_get_arena(session);
    ASSERT_EQ(
        NMO_ERR_INVALID_ARGUMENT,
        nmo_session_preview_destroy(session, &id, 1, 0, arena, NULL, NULL));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/* ---- Preview without cascade ---- */

TEST(strip_preview, preview_returns_direct_ids_only) {
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    /* Create two objects */
    nmo_object_id_t id_a = 0, id_b = 0;
    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, 1, "objA", (nmo_guid_t){0, 0}, &id_a, &report));
    ASSERT_TRUE(id_a != 0);
    memset(&report, 0, sizeof(report));
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, 1, "objB", (nmo_guid_t){0, 0}, &id_b, &report));
    ASSERT_TRUE(id_b != 0);

    /* Preview delete of objA only, no cascade */
    nmo_object_id_t *out_ids = NULL;
    size_t out_count = 0;
    nmo_arena_t *arena = nmo_session_get_arena(session);
    ASSERT_EQ(NMO_OK,
        nmo_session_preview_destroy(session, &id_a, 1,
            NMO_RUNTIME_REQUEST_DEFAULT, arena, &out_ids, &out_count));

    /* Should return exactly the one ID requested */
    ASSERT_EQ(1u, (unsigned)out_count);
    ASSERT_NOT_NULL(out_ids);
    ASSERT_EQ(id_a, out_ids[0]);

    /* Verify both objects still exist (preview is non-destructive) */
    nmo_object_t **objects = NULL;
    size_t obj_count = 0;
    ASSERT_EQ(NMO_OK, nmo_session_get_objects(session, &objects, &obj_count));
    ASSERT_EQ(2u, (unsigned)obj_count);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/* ---- No cascade without flag ---- */

TEST(strip_preview, no_cascade_without_flag) {
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    /* Create two objects */
    nmo_object_id_t id_a = 0, id_b = 0;
    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, 1, "objA", (nmo_guid_t){0, 0}, &id_a, &report));
    memset(&report, 0, sizeof(report));
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, 2, "objB", (nmo_guid_t){0, 0}, &id_b, &report));

    /* Preview delete of objA without CASCADE flag -- should get only objA */
    nmo_object_id_t *out_ids = NULL;
    size_t out_count = 0;
    nmo_arena_t *arena = nmo_session_get_arena(session);
    ASSERT_EQ(NMO_OK,
        nmo_session_preview_destroy(session, &id_a, 1,
            NMO_RUNTIME_REQUEST_DEFAULT, arena, &out_ids, &out_count));

    ASSERT_EQ(1u, (unsigned)out_count);
    ASSERT_EQ(id_a, out_ids[0]);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/* ---- Preview with cascade (no type system = no extra deps) ---- */

TEST(strip_preview, cascade_flag_without_refs_returns_direct_only) {
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t id_a = 0;
    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, 1, "solo", (nmo_guid_t){0, 0}, &id_a, &report));

    nmo_object_id_t *out_ids = NULL;
    size_t out_count = 0;
    nmo_arena_t *arena = nmo_session_get_arena(session);
    ASSERT_EQ(NMO_OK,
        nmo_session_preview_destroy(session, &id_a, 1,
            NMO_RUNTIME_REQUEST_CASCADE, arena, &out_ids, &out_count));

    /* Without type runtime / typed refs, cascade adds nothing */
    ASSERT_EQ(1u, (unsigned)out_count);
    ASSERT_EQ(id_a, out_ids[0]);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/* ---- Multiple direct IDs ---- */

TEST(strip_preview, multiple_direct_ids) {
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t ids[3];
    nmo_runtime_report_t report;
    for (int i = 0; i < 3; i++) {
        memset(&report, 0, sizeof(report));
        ids[i] = 0;
        ASSERT_EQ(NMO_OK,
            nmo_session_create_object(session, 1, "obj", (nmo_guid_t){0, 0}, &ids[i], &report));
    }

    nmo_object_id_t *out_ids = NULL;
    size_t out_count = 0;
    nmo_arena_t *arena = nmo_session_get_arena(session);
    ASSERT_EQ(NMO_OK,
        nmo_session_preview_destroy(session, ids, 3,
            NMO_RUNTIME_REQUEST_DEFAULT, arena, &out_ids, &out_count));

    ASSERT_EQ(3u, (unsigned)out_count);

    /* All three IDs should appear in the output */
    for (int i = 0; i < 3; i++) {
        int found = 0;
        for (size_t j = 0; j < out_count; j++) {
            if (out_ids[j] == ids[i]) {
                found = 1;
                break;
            }
        }
        ASSERT_TRUE(found);
    }

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/* ---- Nonexistent ID without STRICT is OK ---- */

TEST(strip_preview, nonexistent_id_skipped_without_strict) {
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t bogus = 9999;
    nmo_object_id_t *out_ids = NULL;
    size_t out_count = 0;
    nmo_arena_t *arena = nmo_session_get_arena(session);
    ASSERT_EQ(NMO_OK,
        nmo_session_preview_destroy(session, &bogus, 1,
            NMO_RUNTIME_REQUEST_DEFAULT, arena, &out_ids, &out_count));

    /* Bogus ID is silently skipped */
    ASSERT_EQ(0u, (unsigned)out_count);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/* ---- Nonexistent ID with STRICT returns error ---- */

TEST(strip_preview, nonexistent_id_strict_returns_error) {
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t bogus = 9999;
    nmo_object_id_t *out_ids = NULL;
    size_t out_count = 0;
    nmo_arena_t *arena = nmo_session_get_arena(session);
    ASSERT_EQ(NMO_ERR_NOT_FOUND,
        nmo_session_preview_destroy(session, &bogus, 1,
            NMO_RUNTIME_REQUEST_STRICT, arena, &out_ids, &out_count));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(strip_preview, null_session_returns_error);
REGISTER_TEST(strip_preview, null_ids_returns_error);
REGISTER_TEST(strip_preview, zero_count_returns_error);
REGISTER_TEST(strip_preview, null_output_params_returns_error);
REGISTER_TEST(strip_preview, preview_returns_direct_ids_only);
REGISTER_TEST(strip_preview, no_cascade_without_flag);
REGISTER_TEST(strip_preview, cascade_flag_without_refs_returns_direct_only);
REGISTER_TEST(strip_preview, multiple_direct_ids);
REGISTER_TEST(strip_preview, nonexistent_id_skipped_without_strict);
REGISTER_TEST(strip_preview, nonexistent_id_strict_returns_error);
TEST_MAIN_END()
