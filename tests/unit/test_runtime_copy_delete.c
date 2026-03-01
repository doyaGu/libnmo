#include "test_framework.h"
#include "app/nmo_context.h"
#include "app/nmo_session.h"

TEST(runtime_copy_delete, copy_then_delete_roundtrip) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t id = 0;
    nmo_runtime_report_t report = {0};
    ASSERT_EQ(
        NMO_OK,
        nmo_session_create_object(session, 1, "copy-src", (nmo_guid_t){0, 0}, &id, &report));
    ASSERT_TRUE(id != 0);

    ASSERT_EQ(
        NMO_OK,
        nmo_session_copy_objects(session, &id, 1, NMO_RUNTIME_REQUEST_DEFAULT, &report));
    ASSERT_EQ(1u, report.copied_objects);

    ASSERT_EQ(
        NMO_OK,
        nmo_session_destroy_objects(session, &id, 1, NMO_RUNTIME_REQUEST_DEFAULT, &report));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(runtime_copy_delete, copy_then_delete_roundtrip);
TEST_MAIN_END()
