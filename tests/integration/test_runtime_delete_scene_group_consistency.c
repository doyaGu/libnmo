#include "test_framework.h"
#include "app/nmo_context.h"
#include "app/nmo_session.h"

TEST(runtime_delete_scene_group_consistency, delete_multiple_objects) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t ids[2] = {0, 0};
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, 1, "scene-like", (nmo_guid_t){0, 0}, &ids[0], NULL));
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, 1, "group-like", (nmo_guid_t){0, 0}, &ids[1], NULL));

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(
        NMO_OK,
        nmo_session_destroy_objects(
            session,
            ids,
            2,
            NMO_RUNTIME_REQUEST_SAFE_DETACH,
            &report));
    ASSERT_EQ(2u, report.deleted_objects);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(runtime_delete_scene_group_consistency, delete_multiple_objects);
TEST_MAIN_END()
