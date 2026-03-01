#include "test_framework.h"
#include "app/nmo_context.h"
#include "app/nmo_session.h"

TEST(runtime_copy_behavior_graph, copy_behavior_like_object) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t source_id = 0;
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, 1, "behavior-root", (nmo_guid_t){0, 0}, &source_id, NULL));
    ASSERT_TRUE(source_id != 0);

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(
        NMO_OK,
        nmo_session_copy_objects(session, &source_id, 1, NMO_RUNTIME_REQUEST_STRICT, &report));
    ASSERT_EQ(1u, report.copied_objects);

    nmo_object_t **objects = NULL;
    size_t count = 0;
    ASSERT_EQ(NMO_OK, nmo_session_get_objects(session, &objects, &count));
    ASSERT_TRUE(count >= 2);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(runtime_copy_behavior_graph, copy_behavior_like_object);
TEST_MAIN_END()
