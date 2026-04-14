#include "test_framework.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_group_schemas.h"
#include "format/nmo_object.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_runtime.h"
#include "core/nmo_array.h"

/* ── Helpers ──────────────────────────────────────────────────── */

static nmo_type_vtable_t *get_mutable_vtable(nmo_context_t *ctx, nmo_class_id_t cid)
{
    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_class_id_inherited(registry, cid);
    if (type == NULL || type->vtable == NULL) return NULL;
    return (nmo_type_vtable_t *)(void *)type->vtable;
}

static void set_group_members(
    nmo_object_t *group_obj,
    const nmo_object_id_t *member_ids,
    size_t member_count)
{
    nmo_group_state_t *state = (nmo_group_state_t *)group_obj->state;
    nmo_array_clear(&state->object_ids);
    if (member_count == 0) return;

    nmo_array_reserve(&state->object_ids, member_count);
    nmo_object_id_t *ids = NULL;
    nmo_array_extend(&state->object_ids, member_count, (void **)&ids);
    for (size_t i = 0; i < member_count; i++) {
        ids[i] = member_ids[i];
    }
}

/* ── Tests ────────────────────────────────────────────────────── */

/**
 * When a surviving object (CKGroup) references a to-be-deleted object,
 * and its type lacks remap_dependencies, SAFE_DETACH + STRICT must fail.
 * The deleted object must remain in the repository.
 */
TEST(safe_detach_validation, rejects_missing_remap_hook) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    /* Remove remap_dependencies from CKGroup type */
    nmo_type_vtable_t *group_vt = get_mutable_vtable(ctx, NMO_CID_GROUP);
    ASSERT_NOT_NULL(group_vt);
    nmo_type_remap_dependencies_fn old_remap = group_vt->remap_dependencies;
    group_vt->remap_dependencies = NULL;

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    /* Create member + group referencing it */
    nmo_object_id_t member_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "member",
            (nmo_guid_t){0, 0}, &member_id, NULL));

    nmo_object_id_t group_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_GROUP, "group",
            (nmo_guid_t){0, 0}, &group_id, NULL));

    nmo_object_t *group_obj = nmo_object_repository_find_by_id(repo, group_id);
    ASSERT_NOT_NULL(group_obj);
    set_group_members(group_obj, &member_id, 1);

    /* Delete member with SAFE_DETACH + STRICT — should fail validation */
    nmo_runtime_report_t report = {0};
    int result = nmo_session_destroy_objects(
        session, &member_id, 1,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
        &report);

    group_vt->remap_dependencies = old_remap;

    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, result);

    /* Member must still be in the repository */
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, member_id));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * When the referencing type HAS remap_dependencies, SAFE_DETACH succeeds.
 */
TEST(safe_detach_validation, accepts_with_remap_hook) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_id_t member_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "member",
            (nmo_guid_t){0, 0}, &member_id, NULL));

    nmo_object_id_t group_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_GROUP, "group",
            (nmo_guid_t){0, 0}, &group_id, NULL));

    nmo_object_t *group_obj = nmo_object_repository_find_by_id(repo, group_id);
    ASSERT_NOT_NULL(group_obj);
    set_group_members(group_obj, &member_id, 1);

    /* CKGroup has remap_dependencies by default — should succeed */
    nmo_runtime_report_t report = {0};
    int result = nmo_session_destroy_objects(
        session, &member_id, 1,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
        &report);

    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(1u, report.deleted_objects);

    /* Member should be gone, group should survive */
    ASSERT_NULL(nmo_object_repository_find_by_id(repo, member_id));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, group_id));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * Deleting an unreferenced object with SAFE_DETACH succeeds trivially.
 */
TEST(safe_detach_validation, no_referrers_succeeds) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "orphan",
            (nmo_guid_t){0, 0}, &id, NULL));

    nmo_runtime_report_t report = {0};
    int result = nmo_session_destroy_objects(
        session, &id, 1,
        NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
        &report);

    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(1u, report.deleted_objects);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(safe_detach_validation, rejects_missing_remap_hook);
    REGISTER_TEST(safe_detach_validation, accepts_with_remap_hook);
    REGISTER_TEST(safe_detach_validation, no_referrers_succeeds);
TEST_MAIN_END()
