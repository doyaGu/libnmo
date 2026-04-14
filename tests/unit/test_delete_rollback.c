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

TEST_MAIN_BEGIN()
    REGISTER_TEST(delete_rollback, pre_delete_failure_strict_no_detach);
    REGISTER_TEST(delete_rollback, pre_delete_failure_non_strict_proceeds);
    REGISTER_TEST(delete_rollback, all_hooks_pass);
TEST_MAIN_END()
