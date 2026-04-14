#include "test_framework.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_object.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_runtime.h"

/* ── Hook probe state ─────────────────────────────────────────── */

static int g_pre_delete_call_count = 0;
static int g_pre_delete_fail_at = -1; /* fail when call_count == this value */

static nmo_status_t pre_delete_counting_hook(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
    g_pre_delete_call_count++;
    if (g_pre_delete_call_count == g_pre_delete_fail_at) {
        return NMO_ERR_INVALID_STATE;
    }
    return NMO_OK;
}

/* ── Helpers ──────────────────────────────────────────────────── */

static nmo_type_vtable_t *get_mutable_vtable(nmo_context_t *ctx, nmo_class_id_t cid)
{
    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_class_id_inherited(registry, cid);
    if (type == NULL || type->vtable == NULL) return NULL;
    return (nmo_type_vtable_t *)(void *)type->vtable;
}

/* ── Tests ────────────────────────────────────────────────────── */

/**
 * When pre_delete hook fails on the 2nd object under STRICT mode,
 * no objects should be detached — all 3 must remain in the repository.
 */
TEST(delete_rollback, pre_delete_failure_strict_no_detach) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_type_vtable_t *vt = get_mutable_vtable(ctx, NMO_CID_OBJECT);
    ASSERT_NOT_NULL(vt);
    nmo_type_pre_delete_fn old_pre_delete = vt->pre_delete;
    vt->pre_delete = pre_delete_counting_hook;

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t ids[3] = {0};
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(NMO_OK,
            nmo_session_create_object(session, NMO_CID_OBJECT, "obj",
                (nmo_guid_t){0, 0}, &ids[i], NULL));
    }

    /* Hook will fail on the 2nd call */
    g_pre_delete_call_count = 0;
    g_pre_delete_fail_at = 2;

    nmo_runtime_report_t report = {0};
    int result = nmo_session_destroy_objects(
        session, ids, 3,
        NMO_RUNTIME_REQUEST_STRICT,
        &report);

    vt->pre_delete = old_pre_delete;

    /* Delete must have failed */
    ASSERT_NE(NMO_OK, result);

    /* All 3 objects must still be in the repository (no partial detach) */
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    for (int i = 0; i < 3; i++) {
        ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, ids[i]));
    }

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * Without STRICT, hook failure is tolerated and all objects are deleted.
 */
TEST(delete_rollback, pre_delete_failure_non_strict_proceeds) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_type_vtable_t *vt = get_mutable_vtable(ctx, NMO_CID_OBJECT);
    ASSERT_NOT_NULL(vt);
    nmo_type_pre_delete_fn old_pre_delete = vt->pre_delete;
    vt->pre_delete = pre_delete_counting_hook;

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t ids[3] = {0};
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(NMO_OK,
            nmo_session_create_object(session, NMO_CID_OBJECT, "obj",
                (nmo_guid_t){0, 0}, &ids[i], NULL));
    }

    g_pre_delete_call_count = 0;
    g_pre_delete_fail_at = 2; /* still fails on 2nd, but non-STRICT */

    nmo_runtime_report_t report = {0};
    int result = nmo_session_destroy_objects(
        session, ids, 3,
        NMO_RUNTIME_REQUEST_DEFAULT,
        &report);

    vt->pre_delete = old_pre_delete;

    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(3u, report.deleted_objects);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * When all hooks pass, normal deletion proceeds as expected.
 */
TEST(delete_rollback, all_hooks_pass) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_type_vtable_t *vt = get_mutable_vtable(ctx, NMO_CID_OBJECT);
    ASSERT_NOT_NULL(vt);
    nmo_type_pre_delete_fn old_pre_delete = vt->pre_delete;
    vt->pre_delete = pre_delete_counting_hook;

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t ids[3] = {0};
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(NMO_OK,
            nmo_session_create_object(session, NMO_CID_OBJECT, "obj",
                (nmo_guid_t){0, 0}, &ids[i], NULL));
    }

    g_pre_delete_call_count = 0;
    g_pre_delete_fail_at = -1; /* never fail */

    nmo_runtime_report_t report = {0};
    int result = nmo_session_destroy_objects(
        session, ids, 3,
        NMO_RUNTIME_REQUEST_STRICT,
        &report);

    vt->pre_delete = old_pre_delete;

    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(3u, report.deleted_objects);

    /* All objects should be gone */
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    for (int i = 0; i < 3; i++) {
        ASSERT_NULL(nmo_object_repository_find_by_id(repo, ids[i]));
    }

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * CASCADE delete should remove orphaned included files whose owners are all gone.
 */
TEST(delete_rollback, cascade_removes_orphaned_included_files) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    /* Create an object that will own an included file */
    nmo_object_id_t owner_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, 1, "texture-owner",
            (nmo_guid_t){0, 0}, &owner_id, NULL));

    /* Add an included file owned by this object */
    const char payload[] = "fake-texture-data";
    nmo_included_file_metadata_t meta = {0};
    meta.owner_ids = &owner_id;
    meta.owner_count = 1;
    ASSERT_EQ(NMO_OK,
        nmo_session_add_included_file_ex(session, "texture.bmp",
            payload, sizeof(payload), &meta));

    /* Verify the file exists */
    uint32_t file_count = 0;
    nmo_session_get_included_files(session, &file_count);
    ASSERT_EQ(1u, file_count);

    /* Delete the owner with CASCADE — should also remove the included file */
    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_OK,
        nmo_session_destroy_objects(session, &owner_id, 1,
            NMO_RUNTIME_REQUEST_CASCADE, &report));
    ASSERT_EQ(1u, report.deleted_objects);

    /* Included file should be gone */
    nmo_session_get_included_files(session, &file_count);
    ASSERT_EQ(0u, file_count);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * Non-CASCADE delete should NOT remove included files (even if owner is deleted).
 */
TEST(delete_rollback, non_cascade_preserves_included_files) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t owner_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, 1, "texture-owner",
            (nmo_guid_t){0, 0}, &owner_id, NULL));

    const char payload[] = "fake-texture-data";
    nmo_included_file_metadata_t meta = {0};
    meta.owner_ids = &owner_id;
    meta.owner_count = 1;
    ASSERT_EQ(NMO_OK,
        nmo_session_add_included_file_ex(session, "texture.bmp",
            payload, sizeof(payload), &meta));

    /* Delete owner WITHOUT CASCADE */
    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_OK,
        nmo_session_destroy_objects(session, &owner_id, 1,
            NMO_RUNTIME_REQUEST_DEFAULT, &report));

    /* Included file should still exist */
    uint32_t file_count = 0;
    nmo_session_get_included_files(session, &file_count);
    ASSERT_EQ(1u, file_count);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(delete_rollback, pre_delete_failure_strict_no_detach);
    REGISTER_TEST(delete_rollback, pre_delete_failure_non_strict_proceeds);
    REGISTER_TEST(delete_rollback, all_hooks_pass);
    REGISTER_TEST(delete_rollback, cascade_removes_orphaned_included_files);
    REGISTER_TEST(delete_rollback, non_cascade_preserves_included_files);
TEST_MAIN_END()
