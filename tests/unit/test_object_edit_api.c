#include "test_framework.h"

#include "object/nmo_object_edit.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_object.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_session_query.h"

TEST(object_edit_api, rename_through_workspace_owner) {
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);

    nmo_workspace_t *workspace = (nmo_workspace_t *)nmo_session_create(ctx);
    ASSERT_NOT_NULL(workspace);
    nmo_session_t *session = (nmo_session_t *)workspace;

    nmo_object_id_t object_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_session_create_object(
                  session, NMO_CID_OBJECT, "old-name", (nmo_guid_t){0, 0}, &object_id, NULL));

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "rename object", &edit));
    ASSERT_EQ(NMO_OK, nmo_object_edit_rename(edit, object_id, "new-name"));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *renamed = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_session_query_find_object_by_name(session, "new-name", &renamed));
    ASSERT_NOT_NULL(renamed);
    ASSERT_EQ(object_id, nmo_object_get_id(renamed));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(object_edit_api, rename_through_workspace_owner);
TEST_MAIN_END()
