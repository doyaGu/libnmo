#include "test_framework.h"
#include "app/nmo_context.h"
#include "app/nmo_session.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_group_schemas.h"
#include "format/nmo_object.h"

static int integration_contains_id(const nmo_object_id_t *ids, size_t count, nmo_object_id_t id)
{
    for (size_t i = 0; i < count; i++) {
        if (ids[i] == id) {
            return 1;
        }
    }
    return 0;
}

TEST(runtime_copy_behavior_graph, copy_behavior_like_object) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t source_id = 0;
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "behavior-root", (nmo_guid_t){0, 0}, &source_id, NULL));
    ASSERT_TRUE(source_id != 0);

    nmo_object_id_t member_id = 0;
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "behavior-member", (nmo_guid_t){0, 0}, &member_id, NULL));
    ASSERT_TRUE(member_id != 0);

    nmo_object_id_t group_id = 0;
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, NMO_CID_GROUP, "behavior-group", (nmo_guid_t){0, 0}, &group_id, NULL));
    ASSERT_TRUE(group_id != 0);

    nmo_object_t *group_obj = nmo_object_repository_find_by_id(repo, group_id);
    ASSERT_NOT_NULL(group_obj);
    ASSERT_NOT_NULL(group_obj->state);
    nmo_group_state_t *group_state = (nmo_group_state_t *)group_obj->state;
    ASSERT_EQ(NMO_OK, nmo_array_reserve(&group_state->object_ids, 2));
    nmo_object_id_t *refs = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_extend(&group_state->object_ids, 2, (void **)&refs));
    refs[0] = source_id;
    refs[1] = member_id;

    nmo_object_id_t copy_ids[3] = {group_id, source_id, member_id};
    nmo_runtime_report_t report = {0};
    ASSERT_EQ(
        NMO_OK,
        nmo_session_copy_objects(session, copy_ids, 3, NMO_RUNTIME_REQUEST_STRICT, &report));
    ASSERT_EQ(3u, report.copied_objects);

    nmo_object_t **objects = NULL;
    size_t count = 0;
    ASSERT_EQ(NMO_OK, nmo_session_get_objects(session, &objects, &count));
    ASSERT_EQ(6u, count);

    nmo_object_id_t cloned_group_id = 0;
    nmo_object_id_t cloned_members[2] = {0, 0};
    size_t cloned_member_count = 0;
    for (size_t i = 0; i < count; i++) {
        nmo_object_t *obj = objects[i];
        if (obj->id == group_id || obj->id == source_id || obj->id == member_id) {
            continue;
        }
        if (obj->class_id == NMO_CID_GROUP) {
            cloned_group_id = obj->id;
            continue;
        }
        if (obj->class_id == NMO_CID_OBJECT && cloned_member_count < 2) {
            cloned_members[cloned_member_count++] = obj->id;
        }
    }

    ASSERT_TRUE(cloned_group_id != 0);
    ASSERT_EQ(2u, cloned_member_count);

    nmo_object_t *cloned_group = nmo_object_repository_find_by_id(repo, cloned_group_id);
    ASSERT_NOT_NULL(cloned_group);
    ASSERT_NOT_NULL(cloned_group->state);
    nmo_group_state_t *cloned_group_state = (nmo_group_state_t *)cloned_group->state;
    ASSERT_EQ(2u, cloned_group_state->object_ids.count);
    const nmo_object_id_t *cloned_refs = NMO_ARRAY_DATA(nmo_object_id_t, &cloned_group_state->object_ids);
    ASSERT_TRUE(integration_contains_id(cloned_refs, cloned_group_state->object_ids.count, cloned_members[0]) != 0);
    ASSERT_TRUE(integration_contains_id(cloned_refs, cloned_group_state->object_ids.count, cloned_members[1]) != 0);
    ASSERT_TRUE(integration_contains_id(cloned_refs, cloned_group_state->object_ids.count, source_id) == 0);
    ASSERT_TRUE(integration_contains_id(cloned_refs, cloned_group_state->object_ids.count, member_id) == 0);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(runtime_copy_behavior_graph, copy_behavior_like_object);
TEST_MAIN_END()
