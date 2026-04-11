#include "test_framework.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_group_schemas.h"
#include "format/nmo_object.h"

TEST(runtime_delete_scene_group_consistency, delete_multiple_objects) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t ids[2] = {0, 0};
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "scene-like", (nmo_guid_t){0, 0}, &ids[0], NULL));
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, NMO_CID_GROUP, "group-like", (nmo_guid_t){0, 0}, &ids[1], NULL));

    nmo_object_t *group_obj = nmo_object_repository_find_by_id(repo, ids[1]);
    ASSERT_NOT_NULL(group_obj);
    ASSERT_NOT_NULL(group_obj->state);
    nmo_group_state_t *group_state = (nmo_group_state_t *)group_obj->state;
    ASSERT_EQ(NMO_OK, nmo_array_reserve(&group_state->object_ids, 1));
    nmo_object_id_t *group_refs = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_extend(&group_state->object_ids, 1, (void **)&group_refs));
    group_refs[0] = ids[0];

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(
        NMO_OK,
        nmo_session_destroy_objects(
            session,
            &ids[0],
            1,
            NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
            &report));
    ASSERT_EQ(1u, report.deleted_objects);

    nmo_object_t *remaining_group = nmo_object_repository_find_by_id(repo, ids[1]);
    ASSERT_NOT_NULL(remaining_group);
    ASSERT_NOT_NULL(remaining_group->state);
    nmo_group_state_t *remaining_group_state = (nmo_group_state_t *)remaining_group->state;
    ASSERT_EQ(0u, remaining_group_state->object_ids.count);

    nmo_object_id_t cascade_member = 0;
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "cascade-member", (nmo_guid_t){0, 0}, &cascade_member, NULL));
    nmo_object_id_t cascade_group = 0;
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, NMO_CID_GROUP, "cascade-group", (nmo_guid_t){0, 0}, &cascade_group, NULL));

    nmo_object_t *cascade_group_obj = nmo_object_repository_find_by_id(repo, cascade_group);
    ASSERT_NOT_NULL(cascade_group_obj);
    ASSERT_NOT_NULL(cascade_group_obj->state);
    nmo_group_state_t *cascade_group_state = (nmo_group_state_t *)cascade_group_obj->state;
    ASSERT_EQ(NMO_OK, nmo_array_reserve(&cascade_group_state->object_ids, 1));
    nmo_object_id_t *cascade_refs = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_extend(&cascade_group_state->object_ids, 1, (void **)&cascade_refs));
    cascade_refs[0] = cascade_member;

    report = (nmo_runtime_report_t){0};
    ASSERT_EQ(
        NMO_OK,
        nmo_session_destroy_objects(
            session,
            &cascade_member,
            1,
            NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_CASCADE,
            &report));
    ASSERT_EQ(2u, report.deleted_objects);
    ASSERT_NULL(nmo_object_repository_find_by_id(repo, cascade_member));
    ASSERT_NULL(nmo_object_repository_find_by_id(repo, cascade_group));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(runtime_delete_scene_group_consistency, delete_multiple_objects);
TEST_MAIN_END()
